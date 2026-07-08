#pragma once
// ============================================================================
//  Medusa SAM  --  config.h
//
//  A 16-track professional MIDI groovebox for the Dream SAM2695 GM module.
//
//  Board:   Raspberry Pi Pico 2 (RP2350), arduino-pico core by earlephilhower
//  Synth:   Dream SAM2695 "GM 2.0 music module" -- serial MIDI @ 31250 baud
//  Display: 1.3" ST7567S COG LCD, 4-pin I2C (EstarDyn module, U8g2)
//  Input:   EC11 rotary encoder w/ push + 5 momentary buttons
//
//  This is the Medusa pinout: the OLED / encoder / button map is identical to
//  the Medusa GM groovebox, so the same front panel drives both firmwares.
//  All wiring is collected here -- change pins to match your build.
// ============================================================================

#include <Arduino.h>

// ─── MIDI out -> SAM2695 (UART0 / Serial1) ──────────────────────────────────
// GP0 TX drives the SAM2695 module's MIDI-IN / RX pad directly (both are
// 3.3 V logic).  Through an opto-isolated DIN MIDI shield, GP0 goes to the
// shield's "1 / TX" pad.  GP1 is reserved for a future MIDI-IN (external
// clock sync) and must never be driven above 3.3 V.
#define PIN_MIDI_TX      0
#define PIN_MIDI_RX      1        // reserved: external MIDI clock in
#define MIDI_BAUD        31250

// ─── 1.3" ST7567S COG LCD (EstarDyn GM12864-59N, 4-pin I2C) over I2C0 ───────
// The EstarDyn panel: CON SDA SCL PSH TRA TRB BAK GND VCC (4-wire: VCC GND SCL SDA).
//
// These values are taken verbatim from a confirmed-working RP2350 build of
// this exact panel: ENH_DG128064 profile, address 0x3F, 400 kHz, contrast 220,
// no external pull-ups needed.  (A multimeter reading ~2.4 V on SDA/SCL is just
// the meter averaging live I2C traffic while the UI redraws -- it is NORMAL and
// not a fault.)  See ui.cpp for the exact init order, which also matches.
#define PIN_OLED_SDA     4
#define PIN_OLED_SCL     5
#define OLED_ADDR        0x3F     // GM12864-59N (SA0 high).  U8g2 8-bit addr = 0x3F<<1 = 0x7E
#define OLED_I2C_HZ      400000   // 400 kHz -- confirmed working on this panel

// Panel driver profile.  Profile 0 (ENH_DG128064) is the confirmed-working
// match for this GM12864-59N.  Profiles 1/2 are documented fallbacks in case a
// different production batch needs them; leave this at 0 unless the screen is
// blank.  ui.cpp picks the U8g2 constructor from this.
#define ST7567_PROFILE   0        // 0 = ENH_DG128064 (working), 1 = JLX12864, 2 = ENH_DG128064I
#if   ST7567_PROFILE == 0
  #define OLED_CONTRAST  220      // ENH_DG128064: confirmed value; tune 180-240 by eye
#elif ST7567_PROFILE == 1
  #define OLED_CONTRAST  160      // JLX12864 fallback: tune 150-200 by eye
#else
  #define OLED_CONTRAST  230      // ENH_DG128064I fallback: tune 200-255 by eye
#endif

// The ST7567S COG glass on this module physically clips a few pixels at the
// left edge and produces garbled pixels in the rightmost columns.  All
// drawing code uses this safe zone instead of the raw 0..127 canvas.
#define SCREEN_L          5       // first safe column
#define SCREEN_R        120       // one past the last safe column
#define SCREEN_W        (SCREEN_R - SCREEN_L)

// ─── Rotary encoder (EC11 w/ push) ──────────────────────────────────────────
#define PIN_ENC_A        6
#define PIN_ENC_B        7
#define PIN_ENC_SW       8
#define ENC_TICKS_PER_DETENT 4

// ─── 5 momentary switches (active-low to GND, internal pull-ups) ────────────
#define PIN_BTN_PLAY     9        // play / stop            (shift = stop+rewind)
#define PIN_BTN_SHIFT   10        // modifier (hold)
#define PIN_BTN_PAGE    11        // next view              (shift = prev)
#define PIN_BTN_TRACK   12        // next track             (shift = prev)
#define PIN_BTN_REC     13        // context: accent/tie/edit/audition/save

#define DEBOUNCE_MS      5
#define LONG_PRESS_MS  600

// Temporary on-screen input monitor.  With this at 1, the bottom two lines of
// every screen show a live readout of the raw button pins, the debounced
// button states, the encoder count, and a frame counter -- so you can SEE
// exactly what the hardware is doing.  Set back to 0 once inputs are verified.
#define DEBUG_INPUT      1

// ─── Reserved pins (future expansion -- do not reuse) ───────────────────────
// GP14        : analog click/gate out for syncing modular gear
// GP15/16/17  : I2S BCLK/LRCLK/DIN to a PCM5102 DAC -- this is where the
//               planned onboard ("baked") synth / SoundFont layer will come
//               out, exactly as on the Medusa GM pinout.
#define PIN_CLICK_OUT   14
#define PIN_I2S_BCLK    15
#define PIN_I2S_DOUT    17

// ─── Sequencer dimensions ───────────────────────────────────────────────────
#define NUM_TRACKS      16        // 16 tracks -> the 16 GM MIDI channels
#define NUM_STEPS       64        // hard ceiling; each track loops at 1..64
#define DEFAULT_STEPS   16        // new tracks start at a classic 16
#define STEPS_PER_PAGE  16        // STEP view shows one 16-step window
#define NUM_PATTERNS     8        // patterns chainable into a song
#define CHAIN_LEN       16        // song chain entries (0xFF = unused)
#define DRUM_CHANNEL    10        // GM percussion channel (1-based)

// ─── Timing ─────────────────────────────────────────────────────────────────
#define PPQN            96        // internal resolution (pulses / quarter)
#define MIDI_CLOCK_DIV  (PPQN / 24)   // 0xF8 every N ticks = 24 PPQN out
#define BPM_MIN         20
#define BPM_MAX        300
#define BPM_DEFAULT    120
#define SWING_MAX       75        // % of a step the off-beat can be delayed

// Grid resolutions (steps per beat).  All divide PPQN evenly.
static const uint8_t kStepsPerBeatOptions[] = {1, 2, 3, 4, 6, 8};
#define NUM_SPB_OPTIONS (sizeof(kStepsPerBeatOptions) / sizeof(kStepsPerBeatOptions[0]))

// Engine event pool: worst case is 16 tracks x 4 ratchet hits x on+off
// in flight at once, plus tails from the previous step.
#define MAX_EVENTS     256

// ─── Track note destinations (future baked-synth layer) ────────────────────
// Every note event is routed by TrackCfg::dest.  DEST_SAM is the SAM2695
// over serial MIDI (implemented).  DEST_LAYER is the planned onboard synth /
// SoundFont voice (I2S DAC on GP15..17); the routing hook already exists in
// engine.cpp and sound_source.h so the layer can be dropped in later without
// touching the sequencer or UI.
enum TrackDest : uint8_t { DEST_SAM = 0, DEST_LAYER = 1, DEST_NUM = 2 };

// ─── Persistence ────────────────────────────────────────────────────────────
#define NUM_SONG_SLOTS   8
#define SONG_MAGIC       0x4D53414DUL   // "MSAM"
#define SONG_VERSION     1
