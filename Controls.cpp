// ============================================================================
//  Controls.cpp  -  encoder + button handling
// ============================================================================
#include "Controls.h"
#include "Config.h"

namespace {
  // ---- encoder (interrupt quadrature decode) ----
  volatile int32_t encRaw = 0;       // raw quadrature counts (4 per detent typ.)
  volatile uint8_t encState = 0;
  // transition table: index = (prev2 << 2) | curr2, value = -1/0/+1
  const int8_t kEncTable[16] = { 0,-1, 1, 0,  1, 0, 0,-1, -1, 0, 0, 1,  0, 1,-1, 0 };
  const int    DETENT = 4;           // counts per physical detent

  int32_t encDetentAccum = 0;        // leftover counts toward next detent

  void encISR() {
    uint8_t s = (uint8_t)((digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B));
    encState = (uint8_t)(((encState << 2) | s) & 0x0F);
    encRaw += kEncTable[encState];
  }

  // ---- generic debounced button ----
  struct Button {
    uint8_t pin;
    bool    stable;        // true = released (HIGH), false = pressed (LOW)
    bool    lastRead;
    uint32_t lastChange;
    uint32_t pressTime;
    bool    edgePress;     // set on press, consumed by reader
    bool    edgeLong;      // set on long hold
    bool    longFired;
    bool    supportsLong;
    uint16_t longMs;
  };

  const uint32_t DEBOUNCE_MS = 5;

  Button bShift, bPlay, bPage, bTrack, bMute, bEncSw;

  void initButton(Button& b, uint8_t pin, bool supportsLong, uint16_t longMs = 600) {
    b.pin = pin;
    pinMode(pin, INPUT_PULLUP);
    b.stable = true; b.lastRead = true;
    b.lastChange = millis(); b.pressTime = 0;
    b.edgePress = false; b.edgeLong = false; b.longFired = false;
    b.supportsLong = supportsLong; b.longMs = longMs;
  }

  void serviceButton(Button& b) {
    bool r = (digitalRead(b.pin) != LOW) ? true : false;   // true = released
    uint32_t now = millis();
    if (r != b.lastRead) { b.lastRead = r; b.lastChange = now; }
    if ((now - b.lastChange) >= DEBOUNCE_MS && r != b.stable) {
      b.stable = r;
      if (!b.stable) {                       // just pressed
        b.pressTime = now;
        b.longFired = false;
        // Snappy buttons fire on press. Long-capable buttons defer the short
        // edge to release, so a hold-to-long-press never emits a phantom click.
        if (!b.supportsLong) b.edgePress = true;
      } else {                               // just released
        if (b.supportsLong && !b.longFired) b.edgePress = true;
      }
    }
    if (b.supportsLong && !b.stable && !b.longFired &&
        (now - b.pressTime) >= b.longMs) {
      b.longFired = true;
      b.edgeLong = true;                      // short edge already withheld
    }
  }

  bool takeEdge(bool& flag) { bool v = flag; flag = false; return v; }
}

namespace Controls {

void begin() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  encState = (uint8_t)((digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B));
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

  initButton(bEncSw, PIN_ENC_SW, true, 600);
  initButton(bShift, PIN_BTN_SHIFT, false);
  initButton(bPlay,  PIN_BTN_PLAY,  false);
  initButton(bPage,  PIN_BTN_PAGE,  false);
  initButton(bTrack, PIN_BTN_TRACK, true, 700);
  initButton(bMute,  PIN_BTN_MUTE,  false);
}

void update() {
  serviceButton(bEncSw);
  serviceButton(bShift);
  serviceButton(bPlay);
  serviceButton(bPage);
  serviceButton(bTrack);
  serviceButton(bMute);
}

int encDelta() {
  noInterrupts();
  int32_t raw = encRaw; encRaw = 0;
  interrupts();
  encDetentAccum += raw;
  int detents = encDetentAccum / DETENT;
  encDetentAccum -= detents * DETENT;
  return detents;
}

bool encClick()      { return takeEdge(bEncSw.edgePress); }
bool encLongPress()  { return takeEdge(bEncSw.edgeLong); }
bool shiftHeld()     { return !bShift.stable; }
bool playPressed()   { return takeEdge(bPlay.edgePress); }
bool pagePressed()   { return takeEdge(bPage.edgePress); }
bool trackPressed()  { return takeEdge(bTrack.edgePress); }
bool mutePressed()   { return takeEdge(bMute.edgePress); }
bool trackLongPress(){ return takeEdge(bTrack.edgeLong); }

} // namespace Controls
