#include "storage.h"
#include <LittleFS.h>

namespace {
  struct Header { uint32_t magic; uint16_t version; uint16_t size16; uint32_t bytes; };

  void slotPath(uint8_t slot, char* buf, size_t n) {
    snprintf(buf, n, "/song%u.msam", (unsigned)slot);
  }
}

namespace Storage {

bool begin() {
  if (LittleFS.begin()) return true;
  if (LittleFS.format() && LittleFS.begin()) return true;   // fresh chip
  return false;
}

bool save(uint8_t slot, const Song& s) {
  if (slot >= NUM_SONG_SLOTS) return false;
  char p[24]; slotPath(slot, p, sizeof(p));
  File f = LittleFS.open(p, "w");
  if (!f) return false;
  Header h { SONG_MAGIC, SONG_VERSION, 0, (uint32_t)sizeof(Song) };
  bool ok = f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h);
  ok = ok && f.write((const uint8_t*)&s, sizeof(Song)) == sizeof(Song);
  f.close();
  return ok;
}

bool load(uint8_t slot, Song& s) {
  if (slot >= NUM_SONG_SLOTS) return false;
  char p[24]; slotPath(slot, p, sizeof(p));
  File f = LittleFS.open(p, "r");
  if (!f) return false;
  Header h{};
  bool ok = f.read((uint8_t*)&h, sizeof(h)) == (int)sizeof(h) &&
            h.magic == SONG_MAGIC && h.version == SONG_VERSION &&
            h.bytes == sizeof(Song);
  if (ok) ok = f.read((uint8_t*)&s, sizeof(Song)) == (int)sizeof(Song);
  f.close();
  return ok;
}

bool exists(uint8_t slot) {
  if (slot >= NUM_SONG_SLOTS) return false;
  char p[24]; slotPath(slot, p, sizeof(p));
  return LittleFS.exists(p);
}

} // namespace Storage
