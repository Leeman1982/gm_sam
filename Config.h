// ============================================================================
//  Config.h  -  Pin map, hard limits and enums for the GM Sequencer
//
//  Target : Raspberry Pi Pico 2 (RP2350), Earle Philhower arduino-pico core (>=4.0.1).
//  Synth  : Dream SAM2695 GM module, serial MIDI in @ 31250 baud (TTL).
//  Display: SSD1309 2.42" 128x64 I2C OLED on I2C0 (U8g2 library).
// ============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  PIN MAP  (RP2350 / Pico 2 "GPxx"; pinout is identical to the original Pico)
// ---------------------------------------------------------------------------
// MIDI -- Serial1 / UART0.  TX goes to the SAM2695 module's MIDI-IN / RX pad.
// RX is reserved for a FUTURE external clock-sync input (MIDI IN from a master).
#define PIN_MIDI_TX        0     // GP0  -> module RX  (required)
#define PIN_MIDI_RX        1     // GP1  <- future MIDI IN for clock sync

// OLED  (I2C0; SSD1309 2.42" 128x64 panel @ 0x3C)
#define PIN_OLED_SDA       4     // GP4
#define PIN_OLED_SCL       5     // GP5
#define OLED_I2C_HZ        400000UL

// Rotary encoder (EC11 type, with detents + push switch). Active-low to GND,
// internal pull-ups enabled in firmware -> wire the common pin to GND.
#define PIN_ENC_A          6     // GP6
#define PIN_ENC_B          7     // GP7
#define PIN_ENC_SW         8     // GP8  (encoder push button)

// Momentary switches (active-low to GND, internal pull-ups).
#define PIN_BTN_PLAY       9     // GP9   transport start/stop  (SHIFT = from top)
#define PIN_BTN_SHIFT     10     // GP10  modifier (hold)
#define PIN_BTN_PAGE      11     // GP11  next page  (SHIFT = previous page)
#define PIN_BTN_TRACK     12     // GP12  next track (SHIFT = previous track)
#define PIN_BTN_MUTE      13     // GP13  mute track (SHIFT = solo track)

// Optional analog "click out" gate pulse for syncing analog gear later.
// Not driven in v1.0.0 but reserved so the pin won't be reused.
#define PIN_CLICK_OUT     14     // GP14

// ---------------------------------------------------------------------------
//  SEQUENCER LIMITS
// ---------------------------------------------------------------------------
#define MAX_TRACKS        16     // GM/GM2 has exactly 16 MIDI channels -> 16 tracks
#define MAX_STEPS         64     // per-track pattern length ceiling
#define DEFAULT_STEPS     16
#define MAX_EVENTS        160    // scheduled note on/off events in flight
#define DRUM_CHANNEL      10     // GM percussion channel (1-based)

// ---------------------------------------------------------------------------
//  TIMING
// ---------------------------------------------------------------------------
#define PPQN              96     // internal resolution (pulses per quarter note)
#define MIDI_CLOCK_DIV    (PPQN / 24)   // emit a 0xF8 clock every N internal ticks
#define BPM_MIN           20
#define BPM_MAX           300
#define BPM_DEFAULT       120

// Steps-per-beat options select the grid resolution (ticks-per-step = PPQN/spb).
// All values divide PPQN(=96) evenly.
static const uint8_t kStepsPerBeatOptions[] = {1, 2, 3, 4, 6, 8};
#define NUM_SPB_OPTIONS   (sizeof(kStepsPerBeatOptions) / sizeof(kStepsPerBeatOptions[0]))

// ---------------------------------------------------------------------------
//  STORAGE
// ---------------------------------------------------------------------------
#define NUM_SONG_SLOTS    8
#define SONG_MAGIC        0x474D5351UL   // "GMSQ"
#define SONG_VERSION      1

// ---------------------------------------------------------------------------
//  UI ENUMS
// ---------------------------------------------------------------------------
enum Page : uint8_t {
  PAGE_SEQ = 0,   // step grid + per-step parameter editing
  PAGE_INST,      // per-track sound: channel / program / bank / octave / length
  PAGE_MIX,       // per-track volume / pan / reverb send / chorus send
  PAGE_FX,        // global reverb type / chorus type / master volume
  PAGE_SONG,      // bpm / swing / resolution / clock source / save / load
  PAGE_COUNT
};

// Which per-step field SHIFT+rotate edits on the SEQ page.
enum StepField : uint8_t {
  SF_NOTE = 0,
  SF_VEL,
  SF_GATE,
  SF_PROB,
  SF_MICRO,
  SF_COUNT
};

enum ClockSource : uint8_t {
  CLK_INTERNAL = 0,
  CLK_EXTERNAL          // reserved: slave to incoming MIDI clock on PIN_MIDI_RX
};
