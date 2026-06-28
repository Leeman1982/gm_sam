// ============================================================================
//  Config.h  -  Pin map, hard limits and enums for the GM Sequencer
//
//  Target : Raspberry Pi Pico (RP2040) with Earle Philhower arduino-pico core.
//  Synth  : ON-CHIP SoundFont (SF2) player -- no external GM module needed.
//           Audio is rendered in software and sent out I2S (default) or PWM.
//  Display: SH1106 1.3" 128x64 I2C OLED on I2C0 (U8g2 library).
// ============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  PIN MAP  (all GPIO numbers are RP2040 "GPxx")
// ---------------------------------------------------------------------------
// AUDIO OUT.  The old design sent serial MIDI out GP0 to an external synth
// chip; now the Pico makes the sound itself, so GP0..GP2 carry the audio.
//
//   I2S mode (default, AUDIO_BACKEND_I2S): wire a small PCM5102/UDA1334 DAC.
//     GP0 = BCLK, GP1 = LRCLK (must be BCLK+1), GP2 = DATA (DIN).
//   PWM mode (AUDIO_BACKEND_PWM): no DAC, just an RC low-pass per channel.
//     GP0 = LEFT, GP1 = RIGHT.  (GP2 unused.)
#define PIN_I2S_BCLK       0     // GP0  (LRCLK is forced to GP1 by the I2S core)
#define PIN_I2S_DATA       2     // GP2  DAC data in
#define PIN_PWM_L          0     // GP0  PWM left  (PWM backend only)
#define PIN_PWM_R          1     // GP1  PWM right (PWM backend only)

// OLED  (I2C0, matches your tested SH1106 setup guide)
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
//  ON-CHIP SOUNDFONT SYNTH
// ---------------------------------------------------------------------------
// Pick exactly one audio backend.  I2S sounds far better; PWM needs no DAC.
#define AUDIO_BACKEND_I2S  1
#define AUDIO_BACKEND_PWM  0

#define AUDIO_RATE        22050  // output sample rate (Hz).  44100 is possible
                                 // but leaves less CPU for polyphony.
#define AUDIO_BLOCK       64     // frames rendered per pump iteration
#define SYNTH_MAX_VOICES  16     // simultaneous sample voices (polyphony cap)
#define SYNTH_BEND_RANGE  2      // pitch-bend range in semitones (+/-)
#define SYNTH_HEADROOM    1      // output right-shift to leave mix headroom

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
  CLK_EXTERNAL          // reserved: slave to an external clock source (future)
};
