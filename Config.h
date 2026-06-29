// ============================================================================
//  Config.h  -  Pin map, hard limits and enums for the SoundFont GM Sequencer
//
//  Target : Raspberry Pi Pico 2 (RP2350), Earle Philhower arduino-pico core.
//           Dual core, 520 KB RAM, on-board QSPI flash (code + LittleFS songs).
//  Synth  : INTERNAL SoundFont (SF2) synthesis -> PCM5100A I2S stereo DAC.
//           No external GM module any more: the voices come from an SF2 bank.
//  Banks  : external SPI flash module ("128 MB flash module") holds the SF2
//           bank(s); songs stay in the Pico's on-board LittleFS.
//  Display: SH1106 1.3" 128x64 I2C OLED (U8g2 library).
//  Input  : EC11 rotary encoder w/ push switch (HOLD = SHIFT) + 4x4 matrix
//           keypad for step entry, with 3 LEDs showing the keypad page.
//
//  Every voice still flows through the GMSynth:: API; only the backend behind
//  it changed (serial MIDI -> SoundFont engine). See GMSynth.cpp / SoundFont.cpp.
// ============================================================================
#pragma once
#include "Platform.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  PIN MAP  (RP2350 "GPxx" numbers). All buttons/encoder are active-low with
//  internal pull-ups -> wire their common side to GND.
// ---------------------------------------------------------------------------

// OLED (I2C0).  U8g2 hardware-I2C on Wire.
#define PIN_OLED_SDA       4     // GP4
#define PIN_OLED_SCL       5     // GP5
#define OLED_I2C_HZ        400000UL

// Rotary encoder (EC11, detents + push switch). A and B are interrupt-driven.
//   short tap of the switch = click,   HOLD the switch = SHIFT.
#define PIN_ENC_A          6     // GP6
#define PIN_ENC_B          7     // GP7
#define PIN_ENC_SW         8     // GP8

// I2S out to the PCM5100A DAC. arduino-pico I2S needs BCLK and LRCLK on
// consecutive GPIOs (BCLK = base, LRCLK = base+1); DOUT is separate. The
// PCM5100A SCK/MCLK pin can be left unconnected (it self-clocks).
#define PIN_I2S_DOUT       9     // GP9  -> PCM5100A DIN (SD)
#define PIN_I2S_BCLK       10    // GP10 -> PCM5100A BCK   (LRCLK = GP11)
#define PIN_I2S_LRCLK      11    // GP11 -> PCM5100A WS/LRCK (must be BCLK+1)

// External SPI flash holding the SF2 bank(s) (SPI1).
#define PIN_FLASH_MISO     12    // GP12 (SPI1 RX)
#define PIN_FLASH_CS       13    // GP13
#define PIN_FLASH_SCK      14    // GP14 (SPI1 SCK)
#define PIN_FLASH_MOSI     15    // GP15 (SPI1 TX)
#define FLASH_SPI_HZ       40000000UL

// 4x4 matrix keypad. Rows driven LOW one at a time; columns read with pull-ups
// (active-low). 16 keys -> 16 steps on the SEQ keypad page.
#define KEYPAD_ROWS        4
#define KEYPAD_COLS        4
static const uint8_t kKeypadRowPins[KEYPAD_ROWS] = { 16, 17, 18, 19 };
static const uint8_t kKeypadColPins[KEYPAD_COLS] = { 20, 21, 22, 26 };

// 3 LEDs: one-hot indicator of the active keypad page (STEP / NOTE / CTRL).
#define PIN_LED_PAGE0      0     // GP0  STEP page
#define PIN_LED_PAGE1      1     // GP1  NOTE page
#define PIN_LED_PAGE2      2     // GP2  CONTROL page

// How long the encoder switch must be held before it counts as SHIFT (ms).
#define SHIFT_HOLD_MS      170

// ---------------------------------------------------------------------------
//  AUDIO  (SoundFont render + PCM5100A I2S DAC)
// ---------------------------------------------------------------------------
#define AUDIO_SAMPLE_RATE  44100    // SF2 render + I2S sample rate (Hz)
#define AUDIO_CHANNELS     2        // stereo to the PCM5100A
#define AUDIO_BLOCK_FRAMES 256      // stereo frames per DMA buffer
#define SYNTH_MAX_VOICES   24       // polyphony ceiling (tune to CPU headroom)
#define SYNTH_GAIN_DB      (-3.0f)  // global SoundFont output gain (headroom)

// ---------------------------------------------------------------------------
//  SF2 BANK SOURCE (external flash)
//
//  RP2350 cannot fit a multi-MB SF2 in 520 KB RAM, so a bank is used one of
//  two ways (see SoundFont.cpp / Sf2Flash.cpp and the README):
//    * MMAP : the bank is memory-mapped (RP2350 QMI second chip-select window)
//             and tsf indexes samples in place. Set SF2_USE_MMAP 1.
//    * RAM  : a SLIMMED bank (<~400 KB) is read over SPI into RAM. Default.
// ---------------------------------------------------------------------------
#define SF2_USE_MMAP          0
#define SF2_FLASH_MMAP_BASE   0x11000000UL   // RP2350 QMI CS1 window (if mapped)
#define SF2_DIR_OFFSET        0x00000000UL   // bank directory at flash start
#define SF2_DEFAULT_BANK      0              // 0 = Vintage Dreams Waves, 1 = Merlin GM

// ---------------------------------------------------------------------------
//  SEQUENCER LIMITS
// ---------------------------------------------------------------------------
#define MAX_TRACKS        16     // 16 MIDI channels -> 16 tracks
#define MAX_STEPS         64     // per-track pattern length ceiling
#define DEFAULT_STEPS     16
#define MAX_EVENTS        160    // scheduled note on/off events in flight
#define DRUM_CHANNEL      10     // GM percussion channel (1-based)

// ---------------------------------------------------------------------------
//  TIMING
// ---------------------------------------------------------------------------
#define PPQN              96     // internal resolution (pulses per quarter note)
#define MIDI_CLOCK_DIV    (PPQN / 24)   // (kept for parity; clock out is a no-op now)
#define BPM_MIN           20
#define BPM_MAX           300
#define BPM_DEFAULT       120

static const uint8_t kStepsPerBeatOptions[] = {1, 2, 3, 4, 6, 8};
#define NUM_SPB_OPTIONS   (sizeof(kStepsPerBeatOptions) / sizeof(kStepsPerBeatOptions[0]))

// ---------------------------------------------------------------------------
//  STORAGE  (songs live in the Pico's on-board LittleFS)
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
  PAGE_FX,        // global reverb / chorus / master volume
  PAGE_SONG,      // bpm / swing / resolution / clock source / save / load
  PAGE_COUNT
};

enum StepField : uint8_t {
  SF_NOTE = 0, SF_VEL, SF_GATE, SF_PROB, SF_MICRO, SF_COUNT
};

enum ClockSource : uint8_t {
  CLK_INTERNAL = 0,
  CLK_EXTERNAL          // reserved
};

// Keypad pages (mirrored by the 3 page LEDs).
enum KeypadPage : uint8_t {
  KP_STEP = 0,    // keys 0..15 toggle steps 0..15 of the current track
  KP_NOTE,        // keys 0..15 audition notes (play the synth live)
  KP_CTRL,        // keys = transport / track / mute / page (old hard buttons)
  KP_COUNT
};
