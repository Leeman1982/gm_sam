// ============================================================================
//  Medusa SAM  --  a 16-track professional MIDI groovebox for the SAM2695
//  ---------------------------------------------------------------------------
//  The Medusa GM sequencer + UI, with the baked SoundFont engine replaced by
//  a Dream SAM2695 General-MIDI module driven over serial MIDI.  Because the
//  RP2040 only shuttles MIDI bytes, the CPU load is tiny -- the whole of
//  core1 belongs to timing, which is why the groove is rock solid.
//
//  Hardware:  Raspberry Pi Pico (RP2040)  |  Arduino IDE w/ arduino-pico core
//             SAM2695 GM module (MIDI in @31250)  |  1.3" SH1106 I2C OLED
//             EC11 rotary encoder + 5 buttons     |  Medusa pinout (config.h)
//
//  CORE SPLIT
//    core0 : UI (SH1106 via U8g2) + encoder/buttons + LittleFS storage.
//    core1 : 96-PPQN transport + event scheduler; the ONLY core that talks
//            to the MIDI UART.
//  The cores share the global Song + Engine through atomic byte fields and
//  volatile request flags (see engine.h).
//
//  Build (Arduino IDE):
//    Board:      your Raspberry Pi Pico (RP2040)
//    Flash Size: a layout WITH a filesystem, e.g. "2MB (Sketch 1MB / FS 1MB)"
//    Libraries:  U8g2 (Library Manager)
//  See README.md for wiring and the full manual.
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "model.h"
#include "sam2695.h"
#include "engine.h"
#include "controls.h"
#include "storage.h"
#include "ui.h"

Song     song;
Engine   engine;
Controls controls;
UI       ui;

// core1 must not run the engine until core0 has built the song.
static volatile bool g_songReady = false;

// ----------------------------------------------------------------------------
//  CORE 0  --  user interface
// ----------------------------------------------------------------------------
void setup() {
#ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
#endif
    songInitDefault(song);
    g_songReady = true;

    Storage::begin();          // mount LittleFS (formats on first run)
    controls.begin();
    ui.begin(&song, &engine, &controls);
    delay(600);                // let the boot splash be seen
}

void loop() {
    controls.update();
    ui.handleInput();
    ui.render();               // self-throttled to ~25 fps

#ifdef LED_BUILTIN
    // 1 Hz heartbeat: proves core0 is alive even with a dead OLED.
    digitalWrite(LED_BUILTIN, (millis() % 1000) < 500 ? HIGH : LOW);
#endif
}

// ----------------------------------------------------------------------------
//  CORE 1  --  real-time engine (owns Serial1 / MIDI)
// ----------------------------------------------------------------------------
void setup1() {
    SAM::begin();                       // UART @31250 + GM reset
    while (!g_songReady) { delay(1); }
    engine.begin(&song);                // arms timing + queues a full resend
}

void loop1() {
    engine.service();                   // tight real-time loop, never blocks
}
