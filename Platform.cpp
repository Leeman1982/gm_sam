// ============================================================================
//  Platform.cpp  -  64-bit microsecond clock for non-pico builds
// ============================================================================
#include "Platform.h"

#if !defined(ARDUINO_ARCH_RP2040)

// Extend the 32-bit Arduino micros() to 64 bits. micros() rolls over at 2^32
// microseconds; each time we see it go backwards we've crossed one boundary.
// Called from a single context (the main loop / engine) so no locking needed,
// but we keep the bookkeeping in one place to guarantee a single shared state.
uint64_t time_us_64() {
  static uint32_t last = 0;
  static uint64_t high = 0;        // accumulated 2^32-us epochs
  uint32_t now = micros();
  if (now < last) high += (uint64_t)1 << 32;
  last = now;
  return high + now;
}

#endif
