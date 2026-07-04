// ============================================================================
//  Keypad.cpp  -  4x4 matrix / CD74HC4067 scanning with per-key debounce
// ============================================================================
#include "Keypad.h"
#include "Config.h"

#if KEYPAD_MODE != 0

namespace {
  const uint32_t DEBOUNCE_MS = 8;
  const uint32_t TAP_MS      = 450;
  const uint32_t LONG_MS     = 600;

  struct Key {
    bool     stable;       // true = down
    bool     lastRead;
    uint32_t lastChange;
    uint32_t pressTs;
    bool     tapEdge;
    bool     longEdge;
    bool     longFired;
    bool     modified;     // used as a modifier -> no tap/long on release
  };
  Key keys[16];
  int lastHeld = -1;

#if KEYPAD_MODE == 1
  const uint8_t kRows[4] = { PIN_KP_ROW0, PIN_KP_ROW1, PIN_KP_ROW2, PIN_KP_ROW3 };
  const uint8_t kCols[4] = { PIN_KP_COL0, PIN_KP_COL1, PIN_KP_COL2, PIN_KP_COL3 };

  uint16_t scanRaw() {
    uint16_t bits = 0;
    for (uint8_t r = 0; r < 4; r++) {
      pinMode(kRows[r], OUTPUT);
      digitalWrite(kRows[r], LOW);
      delayMicroseconds(3);                    // let the column lines settle
      for (uint8_t c = 0; c < 4; c++)
        if (digitalRead(kCols[c]) == LOW) bits |= (1u << (r * 4 + c));
      pinMode(kRows[r], INPUT);                // release the row (hi-z)
    }
    return bits;
  }
#else  // KEYPAD_MODE == 2 : CD74HC4067
  uint16_t scanRaw() {
    uint16_t bits = 0;
    for (uint8_t k = 0; k < 16; k++) {
      digitalWrite(PIN_MUX_S0, (k >> 0) & 1);
      digitalWrite(PIN_MUX_S1, (k >> 1) & 1);
      digitalWrite(PIN_MUX_S2, (k >> 2) & 1);
      digitalWrite(PIN_MUX_S3, (k >> 3) & 1);
      delayMicroseconds(3);                    // mux settle time
      if (digitalRead(PIN_MUX_SIG) == LOW) bits |= (1u << k);
    }
    return bits;
  }
#endif
}

namespace Keypad {

void begin() {
#if KEYPAD_MODE == 1
  for (uint8_t r = 0; r < 4; r++) pinMode(kRows[r], INPUT);
  for (uint8_t c = 0; c < 4; c++) pinMode(kCols[c], INPUT_PULLUP);
#else
  pinMode(PIN_MUX_S0, OUTPUT);
  pinMode(PIN_MUX_S1, OUTPUT);
  pinMode(PIN_MUX_S2, OUTPUT);
  pinMode(PIN_MUX_S3, OUTPUT);
  pinMode(PIN_MUX_SIG, INPUT_PULLUP);
#endif
  uint32_t now = millis();
  for (int k = 0; k < 16; k++) {
    keys[k] = { false, false, now, 0, false, false, false, false };
  }
}

void update() {
  uint16_t raw = scanRaw();
  uint32_t now = millis();

  for (uint8_t k = 0; k < 16; k++) {
    Key& key = keys[k];
    bool down = (raw >> k) & 1;

    if (down != key.lastRead) { key.lastRead = down; key.lastChange = now; }
    if ((now - key.lastChange) >= DEBOUNCE_MS && down != key.stable) {
      key.stable = down;
      if (down) {                               // pressed
        key.pressTs   = now;
        key.longFired = false;
        key.modified  = false;
        lastHeld = k;
      } else {                                  // released
        if (!key.longFired && !key.modified && (now - key.pressTs) < TAP_MS)
          key.tapEdge = true;
        if (lastHeld == k) {
          lastHeld = -1;
          for (int j = 0; j < 16; j++)          // fall back to any other held key
            if (keys[j].stable) { lastHeld = j; break; }
        }
      }
    }
    if (key.stable && !key.longFired && !key.modified &&
        (now - key.pressTs) >= LONG_MS) {
      key.longFired = true;
      key.longEdge  = true;
    }
  }
}

bool tap(uint8_t k)       { bool v = keys[k & 15].tapEdge;  keys[k & 15].tapEdge  = false; return v; }
bool longPress(uint8_t k) { bool v = keys[k & 15].longEdge; keys[k & 15].longEdge = false; return v; }
bool isDown(uint8_t k)    { return keys[k & 15].stable; }
int  heldKey()            { return lastHeld; }

void markActive() {
  for (int k = 0; k < 16; k++)
    if (keys[k].stable) { keys[k].modified = true; keys[k].longFired = true; }
}

} // namespace Keypad

#else  // KEYPAD_MODE == 0 : stubs so the rest of the firmware needs no #ifs

namespace Keypad {
  void begin() {}
  void update() {}
  bool tap(uint8_t)       { return false; }
  bool longPress(uint8_t) { return false; }
  bool isDown(uint8_t)    { return false; }
  int  heldKey()          { return -1; }
  void markActive()       {}
}

#endif
