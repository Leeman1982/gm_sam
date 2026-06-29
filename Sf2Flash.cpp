// ============================================================================
//  Sf2Flash.cpp  -  SPI NOR-flash reader for SF2 banks (RP2350 / arduino-pico)
//
//  Reads banks from an external SPI flash on SPI1. Supports both 3-byte (<=16MB)
//  and 4-byte (>16MB) addressing automatically. In the default RAM mode a bank
//  is copied into a malloc'd buffer for tsf; in MMAP mode the memory-mapped
//  base pointer is returned instead (no copy).
//
//  ON-FLASH LAYOUT (little-endian), written by the host tool (see tools/):
//    SF2_DIR_OFFSET:  DirHdr { uint32 magic='S','F','2','D'; uint32 count; }
//                     Entry[count] { uint32 offset; uint32 size; char name[24]; }
//    ...bank blobs at their offsets...
// ============================================================================
#include "Sf2Flash.h"
#include <SPI.h>

namespace {
  struct Entry  { uint32_t offset; uint32_t size; char name[24]; };
  struct DirHdr { uint32_t magic; uint32_t count; };
  constexpr uint32_t DIR_MAGIC = 0x44324653UL; // 'S','F','2','D' little-endian

  bool      g_ready   = false;
  DirHdr    g_dir     = { 0, 0 };
  uint8_t*  g_bankBuf = nullptr;       // RAM-mode bank buffer (malloc'd)

  // SPI1 instance. arduino-pico exposes SPI1 with settable pins.
  SPISettings g_spiCfg(FLASH_SPI_HZ, MSBFIRST, SPI_MODE0);

  inline void csLow()  { digitalWrite(PIN_FLASH_CS, LOW);  }
  inline void csHigh() { digitalWrite(PIN_FLASH_CS, HIGH); }

  // Issue a read at `addr` for `len` bytes. Uses 4-byte addressing for parts
  // larger than 16 MB (addr beyond the 24-bit range), else 3-byte fast read.
  bool spiRead(uint32_t addr, void* buf, uint32_t len) {
    uint8_t* p = (uint8_t*)buf;
    SPI1.beginTransaction(g_spiCfg);
    csLow();
    if (addr > 0x00FFFFFFUL) {
      SPI1.transfer(0x0C);                       // FAST READ, 4-byte address
      SPI1.transfer((addr >> 24) & 0xFF);
      SPI1.transfer((addr >> 16) & 0xFF);
      SPI1.transfer((addr >> 8)  & 0xFF);
      SPI1.transfer( addr        & 0xFF);
      SPI1.transfer(0x00);                       // dummy
    } else {
      SPI1.transfer(0x0B);                       // FAST READ, 3-byte address
      SPI1.transfer((addr >> 16) & 0xFF);
      SPI1.transfer((addr >> 8)  & 0xFF);
      SPI1.transfer( addr        & 0xFF);
      SPI1.transfer(0x00);                       // dummy
    }
    for (uint32_t i = 0; i < len; ++i) p[i] = SPI1.transfer(0x00);
    csHigh();
    SPI1.endTransaction();
    return true;
  }
}

namespace Sf2Flash {

bool ready() { return g_ready; }

bool begin() {
  pinMode(PIN_FLASH_CS, OUTPUT);
  csHigh();
  SPI1.setRX(PIN_FLASH_MISO);
  SPI1.setTX(PIN_FLASH_MOSI);
  SPI1.setSCK(PIN_FLASH_SCK);
  SPI1.begin();

#if SF2_USE_MMAP
  // Directory + banks are read from the memory-mapped window directly.
  memcpy(&g_dir, (const void*)(SF2_FLASH_MMAP_BASE + SF2_DIR_OFFSET), sizeof(g_dir));
#else
  read(SF2_DIR_OFFSET, &g_dir, sizeof(g_dir));
#endif

  g_ready = (g_dir.magic == DIR_MAGIC && g_dir.count > 0);
  return g_ready;
}

bool read(uint32_t addr, void* buf, uint32_t len) {
  return spiRead(addr, buf, len);
}

void releaseBankBuffer() {
  if (g_bankBuf) { free(g_bankBuf); g_bankBuf = nullptr; }
}

bool bank(uint8_t index, const void** outBase, uint32_t* outSize,
          char* nameBuf, size_t nameBufLen) {
  if (!g_ready || index >= g_dir.count) return false;

  Entry e;
#if SF2_USE_MMAP
  memcpy(&e, (const void*)(SF2_FLASH_MMAP_BASE + SF2_DIR_OFFSET + sizeof(DirHdr)
                           + (uint32_t)index * sizeof(Entry)), sizeof(e));
#else
  read(SF2_DIR_OFFSET + sizeof(DirHdr) + (uint32_t)index * sizeof(Entry), &e, sizeof(e));
#endif
  if (e.size == 0) return false;

  if (nameBuf && nameBufLen) {
    strncpy(nameBuf, e.name, nameBufLen - 1);
    nameBuf[nameBufLen - 1] = '\0';
  }

#if SF2_USE_MMAP
  if (outBase) *outBase = (const void*)(SF2_FLASH_MMAP_BASE + e.offset);
  if (outSize) *outSize = e.size;
  return true;
#else
  // RAM mode: (re)load the bank into a heap buffer. This only works if the
  // bank has been slimmed to fit free RAM - see README.
  if (g_bankBuf) { free(g_bankBuf); g_bankBuf = nullptr; }
  g_bankBuf = (uint8_t*)malloc(e.size);
  if (!g_bankBuf) return false;                 // bank too big for RAM
  read(e.offset, g_bankBuf, e.size);
  if (outBase) *outBase = g_bankBuf;
  if (outSize) *outSize = e.size;
  return true;
#endif
}

} // namespace Sf2Flash
