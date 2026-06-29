// ============================================================================
//  GM_Sequencer.ino  -  Dual-core GM step sequencer with INTERNAL SoundFont synth
//
//  Target : Raspberry Pi Pico 2 (RP2350), Earle Philhower arduino-pico core.
//  Build  : select Raspberry Pi Pico 2, pick a Flash Size layout WITH a
//           filesystem partition (Sketch + FS) so song save/load works.
//           Install libraries: "U8g2", the bundled "I2S" (arduino-pico), and
//           add TinySoundFont's "tsf.h" to this folder (see README).
//
//  CORE SPLIT
//    core0 : UI (SH1106) + encoder/keypad/LEDs + LittleFS song storage.
//    core1 : real-time sequencer transport. It also brings up the SoundFont
//            engine + I2S audio; the actual audio render runs in the I2S DMA
//            interrupt and pulls note events from the engine through a
//            lock-free ring (see SoundFont.cpp), so it never starves.
//  The two cores share pattern state through the single global `seq` instance
//  using atomic scalar fields and volatile request flags (see Sequencer.h).
// ============================================================================
#include <Arduino.h>
#include "Config.h"
#include "Sequencer.h"
#include "GMSynth.h"
#include "SoundFont.h"
#include "AudioOut.h"
#include "Sf2Flash.h"
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
//  CORE 1  -  real-time engine + SoundFont/I2S audio bring-up
// ----------------------------------------------------------------------------
void setup1() {
  Sf2Flash::begin();                // external SPI flash holding the SF2 bank(s)
  SoundFont::begin();               // load the default bank + init tsf
  AudioOut::begin();                // start I2S streaming (DMA IRQ renders audio)
  GMSynth::begin();                 // no-op now; kept for symmetry
  while (!g_songReady) delay(1);    // wait for core0 to initialise the song
  seq.engineBegin();                // arms timing + queues a full resend
}

void loop1() {
  seq.engineService();              // tight real-time loop (never blocks long)
}
