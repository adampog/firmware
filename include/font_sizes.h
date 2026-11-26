// clang-format off
#pragma once
//
// Font-size constants: small / medium / large -> Fonte Pequena / Media / Grande.
//
// These used to be global preprocessor macros named FP / FM / FG. As bare macros
// they leaked into *every* translation unit -- including third-party libraries --
// and collided with library identifiers. Concretely, FastLED 3.10.4+ declares
//
//     using FP = fl::fixed_point<16, 16>;   // inside namespace fl
//
// and a global `#define FP 1` rewrites that to `using 1 = ...`, which fails to
// compile in every FastLED TU that pulls in the FFT header.
//
// Defining FP/FM/FG as real (scoped) constants instead of macros keeps every
// existing call site unchanged -- tft.setTextSize(FP), FP * LH, etc. -- while no
// longer polluting the global preprocessor namespace: an ordinary identifier
// `::FP` does not clash with the namespaced `fl::FP`.
//
// Per-board overrides still work through the BRUCE_FP / BRUCE_FM / BRUCE_FG
// config macros, set with -D in boards/<board>/*.ini and only when they differ
// from the 1/2/3 defaults (e.g. boards/marauder-mini uses FM=1, FG=2). Upstream
// Bruce spells these -DFP/-DFM/-DFG; when merging a new upstream board, rename
// them to -DBRUCE_FP/-DBRUCE_FM/-DBRUCE_FG (the #error below catches a miss).
// Per-board pins_arduino.h headers no longer exist (upstream "refactor fase 1");
// the generic one is still included so the constants see the same environment.
//
// This header is force-included into every Bruce source file via build_src_flags
// in platformio.ini, so FP/FM/FG are available everywhere the old macros were.
// It deliberately does NOT include precompiler_flags.h: that header's PSRAM/stack
// logic uses #pragma once, so evaluating it this early (before the board sets
// BOARD_HAS_PSRAM) could lock in wrong values.
//
#include <pins_arduino.h>  // pick up any board header BRUCE_F* overrides

#if defined(FP) || defined(FM) || defined(FG)
#error "FP/FM/FG must not be macros: use -DBRUCE_FP/-DBRUCE_FM/-DBRUCE_FG (see include/font_sizes.h)"
#endif

#ifndef BRUCE_FP
#define BRUCE_FP 1  // small
#endif
#ifndef BRUCE_FM
#define BRUCE_FM 2  // medium
#endif
#ifndef BRUCE_FG
#define BRUCE_FG 3  // large
#endif

#ifdef __cplusplus
[[maybe_unused]] static constexpr int FP = BRUCE_FP;
[[maybe_unused]] static constexpr int FM = BRUCE_FM;
[[maybe_unused]] static constexpr int FG = BRUCE_FG;
#endif
