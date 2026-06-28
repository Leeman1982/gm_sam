// ============================================================================
//  GM_Sequencer.ino  -  Dual-core GM step sequencer for the VS1053B
//
//  Target : Raspberry Pi Pico 2 (RP2350), Earle Philhower arduino-pico core (>=4.0.1).
//  Build  : select "Raspberry Pi Pico 2", set Flash Size to a 4MB layout WITH a
//           filesystem partition (e.g. "Sketch 2MB / FS 2MB") so song save/load
//           works. Install the "U8g2" library.
//
//  CORE SPLIT
//    core0 : UI (SSD1309 OLED) + encoder/buttons + LittleFS storage.
//    core1 : real-time transport + the ONLY core that drives the VS1053B (SPI).
//  The two cores share state through the single global `seq` instance using
//  atomic scalar fields and volatile request flags (see Sequencer.h).
// ============================================================================
#include <Arduino.h>
#include "Config.h"
#include "Sequencer.h"
#include "GMSynth.h"
#include "Controls.h"
#include "Storage.h"
#include "UI.h"

// The one and only sequencer instance (declared extern in Sequencer.h).
Sequencer seq;

// Hand-off flag: core1 must not read the song until core0 has built it.
static volatile bool g_songReady = false;

// ----------------------------------------------------------------------------
//  CORE 0  -  user interface
// ----------------------------------------------------------------------------
void setup() {
  seq.initDefaultSong();      // build the default pattern data first...
  g_songReady = true;         // ...then release core1 to start its engine.

  Storage::begin();           // mount LittleFS (formats on first run)
  Controls::begin();          // encoder interrupts + button pins
  UI::begin();                // I2C + SSD1309 OLED
  UI::bootDiag();             // GM_VS_DIAG: show the VS1053 chip-status on the OLED
}

void loop() {
  static uint32_t lastDraw = 0;
  Controls::update();         // poll buttons, accumulate encoder
  UI::handleInput();          // turn input into edits / navigation

  uint32_t now = millis();
  if (now - lastDraw >= 33) { // ~30 fps
    lastDraw = now;
    UI::render();
  }
}

// ----------------------------------------------------------------------------
//  CORE 1  -  real-time engine (owns the SPI bus / VS1053B)
// ----------------------------------------------------------------------------
void setup1() {
  GMSynth::begin();                 // SPI up + VS1053 reset/clock/RT-MIDI patch
  while (!g_songReady) delay(1);    // wait for core0 to initialise the song
  seq.engineBegin();                // arms timing + queues a full resend
}

void loop1() {
  GMSynth::serviceHealth();          // retry VS1053 bring-up until it answers (live diag)
  if (GMSynth::vsJustCameAlive) {    // chip just came up -> re-push all settings
    GMSynth::vsJustCameAlive = false;
    seq.resendAll();
  }
  seq.engineService();              // tight real-time loop (never blocks long)
}
