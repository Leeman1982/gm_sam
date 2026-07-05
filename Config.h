// ============================================================================
//  Config.h  -  Pin map, hard limits and enums for the GM "Supreme" Sequencer
//
//  Target : Raspberry Pi Pico (RP2040) with Earle Philhower arduino-pico core.
//  Synth  : Dream SAM2695 GM module, serial MIDI in @ 31250 baud (TTL).
//  Display: SH1106 1.3" 128x64 I2C OLED module with EC11 encoder + BACK/CONFIRM
//           buttons (header: CON SDA SCL PSH TRA TRB BAK GND VCC), U8g2 library.
//  Keys   : 4x4 matrix keypad (direct GPIO) OR 16 buttons via CD74HC4067 mux.
//  Pots   : 2 analog pots on ADC0/ADC1 = context-sensitive macro controls.
// ============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  FEATURE SWITCHES
// ---------------------------------------------------------------------------
// KEYPAD_MODE: 0 = no keypad, 1 = 4x4 matrix on 8 GPIOs (default),
//              2 = 16 discrete buttons scanned through a CD74HC4067 mux
//                  (each mux channel -> one button -> GND, SIG pulled up).
#ifndef KEYPAD_MODE
#define KEYPAD_MODE        1
#endif

// ENABLE_POTS: 2 analog macro pots on ADC0/ADC1 (soft-pickup, per-track map).
#ifndef ENABLE_POTS
#define ENABLE_POTS        1
#endif

// ---------------------------------------------------------------------------
//  PIN MAP  (all GPIO numbers are RP2040 "GPxx")
// ---------------------------------------------------------------------------
// MIDI -- Serial1 / UART0.  TX goes to the SAM2695 module's MIDI-IN / RX pad.
// RX is reserved for a FUTURE external clock-sync input (MIDI IN from a master).
#define PIN_MIDI_TX        0     // GP0  -> module RX  (required)
#define PIN_MIDI_RX        1     // GP1  <- future MIDI IN for clock sync

// OLED module (I2C0). The module's header also carries the encoder + buttons.
#define PIN_OLED_SDA       4     // GP4  -> SDA
#define PIN_OLED_SCL       5     // GP5  -> SCL
#define OLED_I2C_HZ        400000UL

// Rotary encoder on the OLED module (TRA / TRB / PSH pads). Active-low,
// internal pull-ups enabled in firmware.
#define PIN_ENC_A          6     // GP6  -> TRA
#define PIN_ENC_B          7     // GP7  -> TRB
#define PIN_ENC_SW         8     // GP8  -> PSH (encoder push)

// OLED module buttons (active-low).
#define PIN_BTN_CONFIRM    9     // GP9  -> CON  play/stop (SHIFT = from top, long = PANIC)
#define PIN_BTN_BACK      10     // GP10 -> BAK  short = next page / exit edit, hold = SHIFT

// Optional extra momentary switches (active-low). Leave unwired if not used.
#define PIN_BTN_PAGE      11     // GP11  next page  (SHIFT = previous page)
#define PIN_BTN_TRACK     12     // GP12  next track (SHIFT = prev, long = clear track)
#define PIN_BTN_MUTE      13     // GP13  mute track (SHIFT = solo track)

// Optional analog "click out" gate pulse for syncing analog gear later.
#define PIN_CLICK_OUT     14     // GP14 (reserved)

// 4x4 matrix keypad (KEYPAD_MODE 1). Rows are driven, columns are read.
#define PIN_KP_ROW0       16     // GP16
#define PIN_KP_ROW1       17     // GP17
#define PIN_KP_ROW2       18     // GP18
#define PIN_KP_ROW3       19     // GP19
#define PIN_KP_COL0       20     // GP20
#define PIN_KP_COL1       21     // GP21
#define PIN_KP_COL2       22     // GP22
#define PIN_KP_COL3        2     // GP2

// CD74HC4067 mux (KEYPAD_MODE 2). S0..S3 select, SIG reads (pull-up), EN->GND.
#define PIN_MUX_S0        16     // GP16
#define PIN_MUX_S1        17     // GP17
#define PIN_MUX_S2        18     // GP18
#define PIN_MUX_S3        19     // GP19
#define PIN_MUX_SIG       20     // GP20

// Analog macro pots (RP2040 ADC inputs).
#define PIN_POT1          26     // GP26 / ADC0
#define PIN_POT2          27     // GP27 / ADC1

// ---------------------------------------------------------------------------
//  SEQUENCER LIMITS
// ---------------------------------------------------------------------------
#define MAX_TRACKS        16     // GM/GM2 has exactly 16 MIDI channels -> 16 tracks
#define MAX_STEPS         64     // per-track pattern length ceiling
#define MIN_STEPS         1
#define DEFAULT_STEPS     16
#define MAX_EVENTS        256    // scheduled note on/off events in flight (ratchets!)
#define DRUM_CHANNEL      10     // GM percussion channel (1-based)
#define MAX_RATCHET        4     // per-step retrigger count 1..4

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

// Per-track clock dividers: the track advances one step every N grid steps.
static const uint8_t kClockDivOptions[] = {1, 2, 3, 4, 6, 8};
#define NUM_DIV_OPTIONS   (sizeof(kClockDivOptions) / sizeof(kClockDivOptions[0]))

// ---------------------------------------------------------------------------
//  STORAGE
// ---------------------------------------------------------------------------
#define NUM_SONG_SLOTS    8
#define SONG_MAGIC        0x474D5351UL   // "GMSQ"
#define SONG_VERSION      2              // v2: supreme parameter set

// ---------------------------------------------------------------------------
//  UI ENUMS
// ---------------------------------------------------------------------------
enum Page : uint8_t {
  PAGE_SEQ = 0,   // step grid + per-step parameter editing
  PAGE_PERF,      // 4x4 performance grid: live mutes / solos per track
  PAGE_TRACK,     // channel / program / bank / octave / transpose / length /
                  // direction / clock divide / humanize
  PAGE_MIX,       // volume / pan / reverb send / chorus send / expression / mod
  PAGE_SYNTH,     // cutoff / resonance / ADR envelope / vibrato / bend range /
                  // portamento / sustain (SAM2695 NRPN + CC)
  PAGE_FX,        // global reverb & chorus masters + master volume
  PAGE_SCALE,     // scale root / type / lock + quantize actions
  PAGE_SONG,      // bpm / swing / resolution / clock source / save / load
  PAGE_COUNT
};

// Which per-step field the encoder edits on the SEQ page.
enum StepField : uint8_t {
  SF_NOTE = 0,
  SF_VEL,
  SF_GATE,
  SF_PROB,
  SF_RATCHET,
  SF_MICRO,
  SF_TIE,
  SF_COUNT
};

// Per-track play direction.
enum TrackDir : uint8_t {
  DIR_FWD = 0,
  DIR_REV,
  DIR_PINGPONG,
  DIR_RANDOM,
  DIR_COUNT
};

enum ClockSource : uint8_t {
  CLK_INTERNAL = 0,
  CLK_EXTERNAL          // reserved: slave to incoming MIDI clock on PIN_MIDI_RX
};

// Scale lock behaviour.
enum ScaleLockMode : uint8_t {
  LOCK_OFF = 0,   // free chromatic editing + playback
  LOCK_ON,        // note edits snap to scale AND playback is quantized
  LOCK_COUNT
};
