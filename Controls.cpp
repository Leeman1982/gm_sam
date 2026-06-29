// ============================================================================
//  Controls.cpp  -  encoder + encoder-switch(SHIFT/click) + 4x4 keypad + LEDs
//
//  Runs on core0 (called from UI's loop). The encoder uses interrupt quadrature
//  decoding (unchanged from the original). Its push switch now does double duty:
//  a short tap is a click; holding it past SHIFT_HOLD_MS is the SHIFT modifier.
//  The keypad is scanned cooperatively and routed by the current keypad page.
// ============================================================================
#include "Controls.h"
#include "Config.h"

namespace {
  // ---- encoder quadrature (interrupt) -------------------------------------
  volatile int32_t encRaw = 0;
  volatile uint8_t encState = 0;
  const int8_t kEncTable[16] = { 0,-1, 1, 0,  1, 0, 0,-1, -1, 0, 0, 1,  0, 1,-1, 0 };
  const int    DETENT = 4;
  int32_t encDetentAccum = 0;

  void encISR() {
    uint8_t s = (uint8_t)((digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B));
    encState = (uint8_t)(((encState << 2) | s) & 0x0F);
    encRaw += kEncTable[encState];
  }

  // ---- encoder switch: tap = click, hold = SHIFT --------------------------
  const uint32_t DEBOUNCE_MS = 5;
  bool     swStable   = true;     // true = released (HIGH)
  bool     swLastRead = true;
  uint32_t swLastChange = 0;
  uint32_t swPressTime  = 0;
  bool     swShift      = false;  // held past threshold
  bool     swClickEdge  = false;  // consumed by encClick()

  void serviceEncSwitch() {
    bool r = (digitalRead(PIN_ENC_SW) != LOW);     // true = released
    uint32_t now = millis();
    if (r != swLastRead) { swLastRead = r; swLastChange = now; }
    if ((now - swLastChange) >= DEBOUNCE_MS && r != swStable) {
      swStable = r;
      if (!swStable) {                              // just pressed
        swPressTime = now; swShift = false;
      } else {                                      // just released
        if (!swShift) swClickEdge = true;           // short tap -> click
        swShift = false;
      }
    }
    if (!swStable && !swShift && (now - swPressTime) >= SHIFT_HOLD_MS)
      swShift = true;                               // hold -> SHIFT
  }

  // ---- keypad scan ---------------------------------------------------------
  bool     keyStable[KEYPAD_ROWS * KEYPAD_COLS];
  bool     keyLastRead[KEYPAD_ROWS * KEYPAD_COLS];
  uint32_t keyLastChange[KEYPAD_ROWS * KEYPAD_COLS];

  uint8_t  kpPage = KP_STEP;

  // page-routed edges, consumed by the getters
  bool playEdge = false, pageEdge = false, trackEdge = false, muteEdge = false;
  bool fieldCycleEdge = false;
  int  stepEdge = -1;          // KP_STEP: step index just pressed
  int  noteEdge = -1;          // KP_NOTE: key index just pressed

  // A key (row*COLS+col) just went down: route it by page / shift.
  void onKeyDown(uint8_t idx) {
    // SHIFT + right-hand column selects the keypad page.
    if (swShift) {
      switch (idx) {
        case 3:  kpPage = KP_STEP; break;   // 'A'
        case 7:  kpPage = KP_NOTE; break;   // 'B'
        case 11: kpPage = KP_CTRL; break;   // 'C'
        default: break;                     // other shifted keys reserved
      }
      return;
    }
    switch (kpPage) {
      case KP_STEP: stepEdge = idx; break;            // toggle step idx
      case KP_NOTE: noteEdge = idx; break;            // audition note idx
      case KP_CTRL:
        switch (idx) {
          case 0: playEdge       = true; break;       // play / stop
          case 1: pageEdge       = true; break;       // next UI page
          case 2: trackEdge      = true; break;       // next track
          case 4: muteEdge       = true; break;       // mute
          case 5: fieldCycleEdge = true; break;       // cycle SEQ step field
          default: break;                             // reserved
        }
        break;
    }
  }

  void serviceKeypad() {
    uint32_t now = millis();
    for (uint8_t r = 0; r < KEYPAD_ROWS; ++r) {
      // Drive this row LOW, others released (input pull-up via pinMode).
      for (uint8_t rr = 0; rr < KEYPAD_ROWS; ++rr)
        pinMode(kKeypadRowPins[rr], rr == r ? OUTPUT : INPUT);
      digitalWrite(kKeypadRowPins[r], LOW);
      delayMicroseconds(5);                            // let lines settle

      for (uint8_t c = 0; c < KEYPAD_COLS; ++c) {
        uint8_t idx = r * KEYPAD_COLS + c;
        bool pressed = (digitalRead(kKeypadColPins[c]) == LOW);  // active-low
        if (pressed != keyLastRead[idx]) { keyLastRead[idx] = pressed; keyLastChange[idx] = now; }
        if ((now - keyLastChange[idx]) >= DEBOUNCE_MS && pressed != keyStable[idx]) {
          keyStable[idx] = pressed;
          if (pressed) onKeyDown(idx);                 // edge on press
        }
      }
    }
  }

  void updateLeds() {
    digitalWrite(PIN_LED_PAGE0, kpPage == KP_STEP ? HIGH : LOW);
    digitalWrite(PIN_LED_PAGE1, kpPage == KP_NOTE ? HIGH : LOW);
    digitalWrite(PIN_LED_PAGE2, kpPage == KP_CTRL ? HIGH : LOW);
  }

  bool takeEdge(bool& f) { bool v = f; f = false; return v; }
  int  takeIdx (int&  f) { int  v = f; f = -1;    return v; }
}

namespace Controls {

void begin() {
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  encState = (uint8_t)((digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B));
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);

  pinMode(PIN_ENC_SW, INPUT_PULLUP);

  // Keypad: columns are inputs with pull-ups; rows start released (hi-Z).
  for (uint8_t c = 0; c < KEYPAD_COLS; ++c) pinMode(kKeypadColPins[c], INPUT_PULLUP);
  for (uint8_t r = 0; r < KEYPAD_ROWS; ++r) pinMode(kKeypadRowPins[r], INPUT);
  for (uint8_t i = 0; i < KEYPAD_ROWS * KEYPAD_COLS; ++i) {
    keyStable[i] = false; keyLastRead[i] = false; keyLastChange[i] = 0;
  }

  pinMode(PIN_LED_PAGE0, OUTPUT);
  pinMode(PIN_LED_PAGE1, OUTPUT);
  pinMode(PIN_LED_PAGE2, OUTPUT);
  updateLeds();
}

void update() {
  serviceEncSwitch();
  serviceKeypad();
  updateLeds();
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

bool encClick()       { return takeEdge(swClickEdge); }
bool encLongPress()   { return false; }              // hold is now SHIFT
bool shiftHeld()      { return swShift; }

bool playPressed()    { return takeEdge(playEdge); }
bool pagePressed()    { return takeEdge(pageEdge); }
bool trackPressed()   { return takeEdge(trackEdge); }
bool mutePressed()    { return takeEdge(muteEdge); }
bool trackLongPress() { return false; }

int  keypadStep()        { return takeIdx(stepEdge); }
int  keypadNote()        { return takeIdx(noteEdge); }
bool fieldCyclePressed() { return takeEdge(fieldCycleEdge); }
uint8_t keypadPage()     { return kpPage; }

} // namespace Controls
