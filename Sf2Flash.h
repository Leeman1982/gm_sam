// ============================================================================
//  Sf2Flash.h  -  External SPI flash holding the SF2 bank(s)
//
//  The "128 MB flash module" stores one or more SF2 banks behind a small
//  directory at SF2_DIR_OFFSET. begin() reads the directory; bank() hands the
//  SoundFont engine a pointer + size for a chosen bank.
//
//  Two source modes (Config.h SF2_USE_MMAP):
//    RAM  (default) : the bank is read over SPI into a malloc'd RAM buffer and
//                     that pointer is returned. Only viable for a SLIMMED bank
//                     that fits in RAM (see README "Flash & RAM reality").
//    MMAP           : the bank is memory-mapped (RP2350 QMI second chip-select)
//                     and the mapped pointer is returned, so a large bank is
//                     read in place by tsf. Requires board/boot support.
//
//  Songs do NOT use this module - they live in the Pico's on-board LittleFS.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

namespace Sf2Flash {

  bool begin();                       // init SPI flash (or mmap window), read dir
  bool ready();

  // Provide a pointer + size for bank `index`, plus its name for the UI.
  // In RAM mode this reads the bank into RAM (freeing any previous bank).
  bool bank(uint8_t index, const void** outBase, uint32_t* outSize,
            char* nameBuf, size_t nameBufLen);

  // Free the RAM-mode bank buffer once the caller has parsed it (tsf copies the
  // data out at load, so the raw bank is dead weight afterwards). No-op in MMAP
  // mode. Call only after the bank has been consumed.
  void releaseBankBuffer();

  // Low-level read (used internally and exposed for tooling/diagnostics).
  bool read(uint32_t addr, void* buf, uint32_t len);

} // namespace Sf2Flash
