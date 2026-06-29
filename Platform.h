// ============================================================================
//  Platform.h  -  Portability shims so the engine code stays board-agnostic
//
//  The sequencer engine (Sequencer.cpp) was written against the Raspberry Pi
//  Pico SDK and uses time_us_64() / sleep_us() / tight_loop_contents(). Those
//  are pico-sdk primitives. The build target is the Pico 2 (RP2350), where the
//  arduino-pico core provides them natively. The fallback below lets the engine
//  also build on a non-pico Arduino core without touching Sequencer.cpp.
//
//  Config.h includes this header, and Sequencer.h includes Config.h, so the
//  shims are visible everywhere the engine is compiled.
// ============================================================================
#pragma once
#include <Arduino.h>

#if defined(ARDUINO_ARCH_RP2040)
  // arduino-pico (RP2040 & RP2350) already exposes time_us_64 / sleep_us /
  // tight_loop_contents through Arduino.h / the pico-sdk - nothing to add.
#else
  // --- Generic Arduino fallback (non-pico cores) ---------------------------
  // A monotonic 64-bit microsecond clock built on Arduino micros(), which wraps
  // every ~71.6 minutes (2^32 us). We extend it to 64 bits by counting wraps.
  // Defined once in Platform.cpp so every translation unit shares one counter.
  uint64_t time_us_64();

  static inline void sleep_us(uint32_t us)        { delayMicroseconds(us); }
  static inline void tight_loop_contents()        { __asm__ volatile("nop"); }
#endif
