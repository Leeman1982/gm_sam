// ============================================================================
//  Medusa SAM  --  a 16-track professional MIDI groovebox for the SAM2695
//  ---------------------------------------------------------------------------
//  The Medusa GM sequencer + UI, with the baked SoundFont engine replaced by
//  a Dream SAM2695 General-MIDI module driven over serial MIDI.  Because the
//  RP2350 only shuttles MIDI bytes, the CPU load is tiny -- the whole of
//  core1 belongs to timing, which is why the groove is rock solid.
//
//  Hardware:  Raspberry Pi Pico 2 (RP2350)  |  Arduino IDE w/ arduino-pico core
//             SAM2695 GM module (MIDI in @31250)  |  1.3" ST7567S COG LCD
//             EC11 rotary encoder + 5 buttons     |  Medusa pinout (config.h)
//
//  CORE SPLIT
//    core0 : UI (ST7567S via U8g2) + encoder/buttons + LittleFS storage.
//    core1 : 96-PPQN transport + event scheduler; the ONLY core that talks
//            to the MIDI UART.
//  The cores share the global Song + Engine through atomic byte fields and
//  volatile request flags (see engine.h).
//
//  Build (Arduino IDE):
//    Board:      Raspberry Pi Pico 2  (RP2350)
//    Flash Size: a layout WITH a filesystem, e.g. "4MB (Sketch 3.75MB/FS 256KB)"
//    Libraries:  U8g2 (Library Manager)
//  See README.md for wiring and the full manual.
// ============================================================================
#include <Arduino.h>
#include <pico/multicore.h>
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

// Dedicated 8 KB stack for core1 (matches the proven RP2350 build).  core1 is
// launched MANUALLY at the very end of setup() -- NOT via setup1()/loop1() --
// so it stays completely idle while core0 brings up the display and touches
// flash.  Booting core1 concurrently (the setup1/loop1 model) races core0's
// hardware init and hangs the chip before the panel ever comes up.
static uint32_t core1Stack[2048];

// ----------------------------------------------------------------------------
//  CORE 1  --  real-time engine (owns Serial1 / MIDI).  Entered only once,
//  from the last line of core0's setup(), after all core0 init is done.
// ----------------------------------------------------------------------------
static void core1Entry() {
    SAM::begin();                       // UART @31250 + GM reset
    multicore_lockout_victim_init();    // lets core0 park us for flash writes
    engine.begin(&song);                // arms timing + queues a full resend
    for (;;) engine.service();          // tight real-time loop, never blocks
}

// ----------------------------------------------------------------------------
//  CORE 0  --  user interface
// ----------------------------------------------------------------------------
void setup() {
#ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
#endif
    // Build the default song first so the display and engine have valid data.
    songInitDefault(song);

    // ── Display FIRST ───────────────────────────────────────────────────────
    // Bring the panel up before anything else, exactly like the proven RP2350
    // build: core1 is not running yet, so nothing can race the I2C bring-up.
    controls.begin();
    ui.begin(&song, &engine, &controls);   // splash shows here

    // ── Flash work while core1 does not yet exist ───────────────────────────
    // Storage runs before core1 is launched, so there is zero chance of core0
    // formatting flash while core1 executes from XIP (the classic hard-hang).
    Storage::begin();          // mount LittleFS (formats on first run)

    // ── Launch core1 LAST ───────────────────────────────────────────────────
    multicore_launch_core1_with_stack(core1Entry, core1Stack, sizeof(core1Stack));

    delay(400);                // let the boot splash be seen
}

void loop() {
    controls.update();
    ui.handleInput();
    ui.render();               // self-throttled to ~25 fps

#ifdef LED_BUILTIN
    // 1 Hz heartbeat: proves core0 reached loop() (not hung in setup()).
    digitalWrite(LED_BUILTIN, (millis() % 1000) < 500 ? HIGH : LOW);
#endif
}
