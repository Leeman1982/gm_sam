// ============================================================================
//  Config.h  -  Pin map, hard limits and enums for the GM Sequencer
//
//  Target : Raspberry Pi Pico 2 (RP2350), Earle Philhower arduino-pico core (>=4.0.1).
//  Synth  : VS1053B GM synth module, driven over SPI (real-time MIDI patch + SDI).
//  Display: SSD1309 2.42" 128x64 I2C OLED on I2C0 (U8g2 library).
// ============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  PIN MAP  (RP2350 / Pico 2 "GPxx"; pinout is identical to the original Pico)
// ---------------------------------------------------------------------------
// MIDI synth -- VS1053B over SPI0. The chip is brought up in real-time MIDI mode
// (software patch) and fed 0x00-padded MIDI bytes on the SDI/XDCS data bus.
// SCK/MOSI/MISO must be valid arduino-pico SPI0 pin groups; XCS/XDCS/DREQ/XRESET
// are plain GPIOs.  (DREQ is an input from the chip; XRESET is driven high by us.)
#define PIN_VS_SCK         2     // GP2  -> VS1053 SCLK
#define PIN_VS_MOSI        3     // GP3  -> VS1053 SI  (MOSI)
#define PIN_VS_MISO        0     // GP0  <- VS1053 SO  (MISO)
#define PIN_VS_XCS         1     // GP1  -> VS1053 XCS  (SCI / command chip-select)
#define PIN_VS_XDCS       15     // GP15 -> VS1053 XDCS (SDI / data chip-select)
#define PIN_VS_DREQ       16     // GP16 <- VS1053 DREQ (data request / ready)
#define PIN_VS_XRESET     17     // GP17 -> VS1053 XRESET (active-low; we drive high)

// VS1053 clock multiplier (SCI_CLOCKF). 0x6000 = 3.0x (min for reverb, fine for
// moderate polyphony). Raise to 0x8000 (3.5x) or 0xA000 (4.0x) for more voices /
// reverb headroom. Keep SC_ADD = 0 so the internal clock never exceeds spec.
#define VS_CLOCKF          0x6000
// SPI clock: keep init slow (<= CLKI/7 at the 12.288 MHz boot clock ~1.75 MHz),
// then run fast once SCI_CLOCKF is set and DREQ has risen.
#define VS_SPI_HZ_INIT     1500000UL
#define VS_SPI_HZ_RUN      8000000UL

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
//  DIAGNOSTICS
// ---------------------------------------------------------------------------
// Power-on MIDI self-test. When 1, the engine plays a short C-major arpeggio on
// channel 1 right after MIDI init, so you can confirm the link to the SAM2695
// independently of the UI / sequencer. Set back to 0 once you have sound.
#define GM_MIDI_SELFTEST   1

// USB-serial MIDI monitor. When 1, every MIDI message streamed to the VS1053 over
// SDI is also echoed to the USB serial port as hex (open the Arduino Serial
// Monitor at 115200), plus the boot SCI_AUDATA check (expect 0xAC45 = RT-MIDI
// mode live). This complements the on-screen activity dot: the dot proves
// Note-Ons were *queued*, the hex stream proves the exact bytes sent. If you see
// bytes here but the module is silent, the firmware/SPI is fine and the fault is
// on the wire (SPI pins, common ground, or module power). Default 0.
#define MIDI_TX_DEBUG      0

// One-line boot diagnostic (quiet; distinct from the noisy per-message
// MIDI_TX_DEBUG). When 1, begin() reads the VS1053 back over SPI and prints
// "VS1053 ver=<n> AUDATA=0x<hhhh> (RT-MIDI OK|FAIL)" once over USB serial
// (115200). ver=4 + AUDATA=0xAC45 means SPI + chip + MIDI mode are all good, so
// any silence is then the audio output; bad/garbage reads mean the SPI/reset
// wiring isn't talking to the chip. Set back to 0 once you have sound.
#define GM_VS_DIAG         1

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
  CLK_EXTERNAL          // reserved: slave to an external MIDI clock (future input)
};
