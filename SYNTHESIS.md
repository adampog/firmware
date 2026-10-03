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

## Status — complete (2026-10-03)

All planned ports landed on the `custom` branch and merged to `main`, each as a vertical slice
(pure host-tested core + glue + `core/menu_items` screen + `core/serial_commands` entry +
`BRUCE_<FEATURE>` capability flag), CI-green on the 8/16 MB build matrix:

- ✅ **#1 Remote-ID** — OpenDroneID WiFi + BLE capture, on-device GPS radar
- ✅ **#2 PineScan** — WiFi Pineapple / evil-twin (rogue-AP) classifier
- ✅ **#3 Fox-hunt** — RSSI direction-finding / homing
- ✅ **#4 Dual-band / 5 GHz** — shared band plan, ESP32-C5 5 GHz hopping
- ✅ **#5 Unit tests** — host Unity suites for all five pure cores (remoteid, pinescan, foxhunt, bandplan, dial)
- ✅ **#6 DIAL / cast** — SSDP discovery + DIAL REST status / launch / stop
- ⚪ **#7 Surveillance-cam / network-device detection** — not pursued (per plan: low value, covered enough by #2)

Plus the ✅ **§7 dedup pass** (shared `wifiStartPassivePromiscuous` helper).

Deliberately deferred, optional follow-ups (noted in-tree): the YouTube **Lounge** video-cue
flow on top of #6 (external HTTPS + private API), and two hardware-verify dedup refactors
(Remote-ID BLE teardown + `radioHasMemForBle()` guard; a shared GPS accessor).

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
1. ✅ **Drone Remote-ID receive/decode** (`RemoteIdDecoder`, `RemoteIdModel`) — Bruce lacks this. **Done.**
2. ✅ **Detection/defensive suite** (PineScan evil-twin classifier) — distinct from Bruce's set. **Done.**
3. ✅ **Fox-hunt / signal-direction** utility — unique. **Done.**
4. ✅ **Dual-band / 5 GHz scanning** path for ESP32-C5 (shared `band_plan`). **Done.**
5. ✅ (Optional) Marauder's **pure value-objects + Unity tests** — brought across for all five cores. **Done.**

### Port INTO Bruce (unique, higher cost — IDF→Arduino reimplementation) — from Ghost
6. ✅ **DIAL / cast integration** (Chromecast/DIAL client) — reimplemented as a module. **Done.**
7. ⚪ **Surveillance-camera / network-device detection** — not pursued (covered enough by #2). **Skipped.**
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

1. ✅ **Baseline build** — the fork compiles for an 8MB target (`m5stack-cardputer`) and a 16MB
   target (`lilygo-t-embed-cc1101`); every port since is build-verified on both.
2. ✅ **Define a synthesis build env** per flash tier so ports are toggled by a single `-D`.
3. ✅ **Port #1 Remote-ID** (self-contained, defensive, no overlap) as the first vertical slice —
   module + menu + serial cmd + capability flag. Validated the porting pattern end-to-end.
4. ✅ **Port #2–#4** (PineScan detection, fox-hunt, dual-band) following the same slice pattern.
5. ✅ **Port #6 DIAL/cast** from Ghost — reimplemented (not copied) onto Bruce's WiFiUDP + HTTPClient.
6. ✅ **Test harness** — Marauder-style Unity/native host tests for all five pure-logic cores.
7. ✅ **Dedup pass** — collapsed the duplicated passive-promiscuous bring-up into one shared helper.

Nothing here adds attack capability beyond what these public tools already provide; the work is
consolidation, deduplication, and build-system hygiene for authorized testing.
