#pragma once
// ============================================================================
//  Medusa SAM  --  storage.h
//  Persist whole Songs to the RP2350's flash via LittleFS.  core0 only, and
//  ONLY while the engine is parked (see UI save/load): flash writes stall XIP,
//  so core1 is paused + locked out for the duration.
//
//  Arduino IDE: pick a Flash Size with a filesystem partition, e.g.
//  "2MB (Sketch: 1MB, FS: 1MB)".  A Song is ~66 KB, so 8 slots need ~528 KB.
// ============================================================================
#include <Arduino.h>
#include "model.h"

namespace Storage {
  bool begin();                              // mount (format on first run)
  bool save(uint8_t slot, const Song& s);
  bool load(uint8_t slot, Song& s);
  bool exists(uint8_t slot);
}
