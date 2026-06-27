// ============================================================================
//  Storage.h  -  Save/load Songs to RP2350 internal flash via LittleFS.
//                Runs on core0. Returns false on any error.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Sequencer.h"

namespace Storage {
  bool begin();                                  // mount LittleFS (formats if blank)
  bool save(uint8_t slot, const Song& song);     // slot 0..NUM_SONG_SLOTS-1
  bool load(uint8_t slot, Song& song);
  bool exists(uint8_t slot);
}
