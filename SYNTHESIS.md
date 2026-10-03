# ESP32 Pentest Firmware Synthesis — Marauder × Bruce × Ghost ESP

Authorized context: penetration testing / CTF, on hardware and networks the operator owns or
is authorized to test. This document is an architecture plan for consolidating the useful,
*non-duplicated* capabilities of three public tools into one maintainable codebase.

## TL;DR decision

**Base the synthesis on Bruce.** Port Marauder's *unique* modules into it as Bruce-style
modules; reimplement only the genuinely unique Ghost features that are worth the framework
jump. Do **not** merge three codebases literally — it is not feasible (see "Why not a literal
3-way merge").

---

## 1. Three-way comparison

| Dimension | ESP32 Marauder | Bruce | Ghost ESP |
|---|---|---|---|
| Framework | Arduino (Arduino-CLI builds) | **Arduino on PlatformIO** | ESP-IDF (CMake/idf.py) |
| Language | C++ | C++ | C (one C++ shim) |
| First-party LOC | ~38–42k | **~108k src/ (+ boards/)** | ~26k |
| Code structure | Flat; 4 god-files = ⅔ of code | **core/ vs modules/, strict protocol isolation** | main/managers/ controllers |
| Board abstraction | Compile-time `configs.h` → `HAS_*` capability flags (good pattern) | **interface.h HAL + runtime `BruceConfigPins` (JSON, remappable)** | Compile-time Kconfig `CONFIG_*`, no runtime HAL |
| Board count | 27 targets, 5 SoCs | **~35 families, ~70 envs, 5 SoCs** | ~18 products, 5 SoCs |
| Flash/partitions | Arduino named schemes in FQBN; 1 custom CSV | **4/8/16MB CSVs already present** | Single 4MB CSV, no OTA — extra flash wasted |
| Protocols | WiFi, BLE | **WiFi, BLE, Sub-GHz, NFC/RFID, IR, NRF24, LoRa, FM** | WiFi, BLE |
| Scripting | — | **QuickJS engine + serial CLI + menus (3 surfaces)** | serial + web CLI |
| Tests | **26 Unity suites + Codecov** | — | — |
| Maintenance | Active, large community | **Active** | **Archived (dead)** |
| Toolchain risk | Low (stock Arduino core) | Medium (bespoke patched toolchain, ~40 forked deps) | Low (pinned IDF 5.4) |

### What each is best at
- **Bruce** — architecture, extensibility, protocol breadth, HAL quality, board count, and it
  already ships the 4/8/16MB partition matrix + a "add a new board" scaffold. Clear winner as a base.
- **Marauder** — engineering maturity (unit tests), the capability-flag pattern, and several
  *features Bruce lacks* (below). Same language/framework family as Bruce → cheap to port.
- **Ghost ESP** — lean IDF/C and a few unique integrations, but **archived** and in a different
  framework, so porting cost is high and ongoing-upstream value is zero.

---

## 2. Why not a literal 3-way merge

The decisive constraint: **Ghost is ESP-IDF/C; Marauder and Bruce are Arduino/C++.** There is no
cheap way to compile all three together — different build systems (idf.py vs Arduino-CLI vs
PlatformIO), different init models (`app_main` + FreeRTOS tasks vs `setup()/loop()`), different UI
stacks (LVGL vs TFT_eSPI). A real "synthesis" therefore means **pick one base and port into it**,
not blend. Picking Bruce also means we inherit a superset of protocols on day one, so most of the
"synthesis" is additive porting of a short list — not a rewrite.

---

## 3. Dedup map — "do not repeat functions"

Bruce already implements the overlapping core, so these are **NOT ported** from Marauder/Ghost
(they would be duplicates):

- WiFi scan / sniff / deauth-detect / evil-portal / wardriving → **Bruce has all of these**
- BLE scan / spam / sniff → **Bruce has these**
- PCAP capture, GPS/Wigle wardriving → **Bruce has these**
- Serial CLI, on-device menu, settings/NVS, SD storage → **Bruce core**

### Port INTO Bruce (unique, net-new) — from Marauder (same framework, low cost)
1. **Drone Remote-ID receive/decode** (`RemoteIdDecoder`, `RemoteIdModel`) — Bruce lacks this.
2. **Detection/defensive suite** (device/packet-type detection) — distinct from Bruce's set.
3. **Fox-hunt / signal-direction** utility — unique.
4. **Dual-band / 5 GHz scanning** path for ESP32-C5 — verify vs Bruce's C5 support; port if absent.
5. (Optional) Marauder's **pure value-objects + Unity tests** — bring test discipline Bruce lacks.

### Port INTO Bruce (unique, higher cost — IDF→Arduino reimplementation) — from Ghost
6. **DIAL / cast integration** (Chromecast/DIAL client) — unique to Ghost; reimplement as a module.
7. **Surveillance-camera / network-device detection** — if not covered by #2.
- Everything else in Ghost (LVGL UI, managers, RGB, mDNS/port-scan) is either duplicated by Bruce
  or not worth the archived-IDF porting cost. **Low priority / skip.**

Each ported item follows Bruce's convention: a `src/modules/<area>/` implementation + a
`core/menu_items/*Menu.cpp` screen + a `core/serial_commands/*_commands.cpp` entry (+ optional JS
binding). No central rewrite required — this is exactly what Bruce's structure is designed for.

---

## 4. Build matrix — 8MB / 16MB (and 4MB)

Bruce **already** ships the partition tables; the synthesis just assigns each build env to the
right one and keeps the ported modules within the app budget.

| Flash | Partition CSV | App (factory) | SPIFFS/LittleFS | Use for |
|---|---|---|---|---|
| 4 MB | `custom_4Mb.csv` / `_full` | ~2.5 / ~3.9 MB | small | CYD family, small boards (feature-trim: `-DLITE_VERSION=1`) |
| 8 MB | `custom_8Mb.csv` | ~4.9 MB | 3 MB | Cardputer, ESP32-C5, general |
| 16 MB | `custom_16Mb.csv` | ~4.5 MB | ~11.7 MB | T-Deck/T-Embed/T-HMI, Core2/CoreS3 — big SPIFFS for pcaps, logs, wordlists |

Two build axes already exist and we keep them:
- **Flash size** → partition CSV (above).
- **`LITE_VERSION`** → trims features to fit launcher/4MB targets.

Synthesis additions must be **capability/flag-gated** (Marauder's `HAS_*` idea, Bruce's `-D` board
macros) so that 4MB/LITE builds can exclude the heavier new modules (e.g. Remote-ID) while 8/16MB
builds include them.

---

## 5. Execution plan (incremental, on the `custom` branch of this Bruce fork)

1. **Baseline build** — confirm the stock Bruce fork compiles for one 8MB target
   (`m5stack-cardputer`) and one 16MB target (e.g. `lilygo-t-embed-cc1101`). Establishes the
   known-good starting point before any ports.
2. **Define a synthesis build env** per flash tier so ports are toggled by a single `-D`.
3. **Port #1 Remote-ID** (self-contained, defensive, no overlap) as the first vertical slice —
   module + menu + serial cmd + capability flag. Validates the porting pattern end-to-end.
4. **Port #2–#4** (detection suite, fox-hunt, dual-band) following the same slice pattern.
5. **(Optional) Port #6 DIAL/cast** from Ghost — budget a reimplementation, not a copy.
6. **Test harness** — bring Marauder's Unity/native test setup across for the pure-logic modules.
7. **Dedup pass** — remove any now-redundant code paths; ensure no feature exists twice.

Nothing here adds attack capability beyond what these public tools already provide; the work is
consolidation, deduplication, and build-system hygiene for authorized testing.
