// ============================================================================
//  Storage.cpp  -  LittleFS persistence
//
//  NOTE: In Arduino IDE select a Flash Size with a filesystem partition, e.g.
//  Tools -> Flash Size -> "2MB (Sketch: 1MB, FS: 1MB)" for a Raspberry Pi Pico.
// ============================================================================
#include "Storage.h"
#include <LittleFS.h>

namespace {
  struct Header { uint32_t magic; uint16_t version; uint16_t size; };

  void slotPath(uint8_t slot, char* buf, size_t n) {
    snprintf(buf, n, "/song%u.dat", (unsigned)slot);
  }
}

namespace Storage {

bool begin() {
  if (LittleFS.begin()) return true;
  // First run on a fresh chip: format then mount.
  if (LittleFS.format() && LittleFS.begin()) return true;
  return false;
}

bool save(uint8_t slot, const Song& song) {
  if (slot >= NUM_SONG_SLOTS) return false;
  char path[24]; slotPath(slot, path, sizeof(path));
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  Header h { SONG_MAGIC, SONG_VERSION, (uint16_t)sizeof(Song) };
  bool ok = (f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h));
  ok = ok && (f.write((const uint8_t*)&song, sizeof(Song)) == sizeof(Song));
  f.close();
  return ok;
}

bool load(uint8_t slot, Song& song) {
  if (slot >= NUM_SONG_SLOTS) return false;
  char path[24]; slotPath(slot, path, sizeof(path));
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  Header h;
  bool ok = (f.read((uint8_t*)&h, sizeof(h)) == (int)sizeof(h));
  ok = ok && h.magic == SONG_MAGIC && h.version == SONG_VERSION && h.size == sizeof(Song);
  if (ok) ok = (f.read((uint8_t*)&song, sizeof(Song)) == (int)sizeof(Song));
  f.close();
  return ok;
}

bool exists(uint8_t slot) {
  if (slot >= NUM_SONG_SLOTS) return false;
  char path[24]; slotPath(slot, path, sizeof(path));
  return LittleFS.exists(path);
}

} // namespace Storage
