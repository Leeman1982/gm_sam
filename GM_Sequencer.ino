// ============================================================================
//  GM_Sequencer.ino  -  Dual-core GM step sequencer for the Dream SAM2695
//
//  Target : Raspberry Pi Pico (RP2040), Earle Philhower arduino-pico core.
//  Build  : select your Pico board, set Flash Size to a layout WITH a
//           filesystem partition (e.g. "2MB (Sketch 1MB / FS 1MB)") so song
//           save/load works. Install the "U8g2" library.
//
//  CORE SPLIT
//    core0 : UI (SH1106) + encoder/buttons + LittleFS storage.
//    core1 : real-time transport + the ONLY core that drives the MIDI UART.
//  The two cores share state through the single global `seq` instance using
//  atomic scalar fields and volatile request flags (see Sequencer.h).
// ============================================================================
#include <Arduino.h>
#include "Config.h"
#include "Sequencer.h"
#include "GMSynth.h"
#include "Synth.h"
#include "Audio.h"
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
  UI::begin();                // I2C + SH1106
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
//  CORE 1  -  real-time engine (owns the synth + audio output)
// ----------------------------------------------------------------------------
void setup1() {
  GMSynth::begin();                 // load the baked SoundFont + reset channels
  Audio::begin();                   // bring up I2S/PWM audio output
  while (!g_songReady) delay(1);    // wait for core0 to initialise the song
  seq.engineBegin();                // arms timing + queues a full resend
}

void loop1() {
  seq.engineService();              // sequencer timing -> note on/off into Synth
  Audio::service();                 // top up the audio buffer (renders a block)
}
