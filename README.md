# GM Sequencer — RP2040 + on-chip SoundFont synth

A professional dual-core step sequencer for the Raspberry Pi Pico (RP2040) with
a **built-in SoundFont (SF2) synthesizer** — no external sound module required.
16 tracks, 64 steps, per-step micro-timing/probability, rock-solid drift-free
timing, and a clear SH1106 OLED UI.

Earlier versions drove an external Dream SAM2695/VS1053 GM chip over serial
MIDI. This version **renders the audio itself**: a General-MIDI SoundFont is
baked into flash and played back by a software sample engine on core1, straight
out an I2S DAC (or PWM). The sequencer, UI and control pinout are unchanged — the
same per-track instrument/bank, volume, pan, octave and master-volume controls
now address the on-chip synth.

**First baked SoundFont:** *Vintage Dreams Waves v2.0* by Ian Wilson — a
GM-layout bank of 128 melodic synth/vintage patches plus 8 drum kits (banks 0
and 128, 136 presets, ~308 KB). It lives in flash and is read directly from
memory-mapped XIP, so the sample data costs no RAM. More GM sets can be added
and switched in (see *Adding more SoundFonts*).

---

## Architecture

Two cores, no locks in the audio path:

- **core1 — engine + synth.** Owns all timing *and* the audio. Sequencer
  resolution is **96 PPQN** with a drift-free integer tick accumulator (no
  floats in the hot loop). Each pass it turns scheduled steps into note on/off
  calls on the software synth (`Synth`), then tops up the audio output buffer
  (`Audio`). The synth allocates sample voices from the baked SoundFont (`SF2`)
  and mixes them with fixed-point pitch (32.32 phase) and a linear DAHDSR
  envelope.
- **core0 — UI.** SH1106 rendering (~30 fps), encoder + buttons, and LittleFS
  song storage.

Cross-core sharing uses single aligned 8/16-bit scalars (atomic on the M0+) and
volatile request flags. Setting changes are reconciled into the synth each
engine pass, so edits are audible immediately. All synth/audio calls happen on
core1 only, so no locking is needed in the render path.

---

## Wiring

All buttons and the encoder are **active-low** with internal pull-ups — wire the
common side to **GND**. The control pinout is identical to previous versions;
only the three freed "MIDI" pins now carry audio.

| Signal            | Pico pin | Notes                                        |
|-------------------|----------|----------------------------------------------|
| I2S BCLK          | GP0      | bit clock → DAC                               |
| I2S LRCLK         | GP1      | word-select (forced to BCLK+1 by the core)    |
| I2S DATA (DIN)    | GP2      | audio data → DAC                              |
| OLED SDA          | GP4      | I2C0                                          |
| OLED SCL          | GP5      | I2C0, 400 kHz                                 |
| Encoder A         | GP6      | quadrature (interrupt-driven)                 |
| Encoder B         | GP7      | quadrature                                    |
| Encoder switch    | GP8      | push                                          |
| PLAY button       | GP9      | start/stop (SHIFT = from top)                 |
| SHIFT button      | GP10     | modifier (hold)                               |
| PAGE button       | GP11     | next page (SHIFT = previous)                  |
| TRACK button      | GP12     | next track (SHIFT = prev, long = clear)       |
| MUTE button       | GP13     | mute (SHIFT = solo)                           |
| CLICK OUT (future)| GP14     | reserved analog gate-sync pulse               |

Share a common ground between the Pico, the DAC and the OLED.

### Audio output options

Pick the backend in `Config.h` (`AUDIO_BACKEND_I2S` *or* `AUDIO_BACKEND_PWM`).

**I2S DAC (default, recommended).** Wire a small PCM5102 or UDA1334 board:

| Pico pin | DAC pin            |
|----------|--------------------|
| GP0      | BCK / BCLK         |
| GP1      | LCK / LRCK / WS    |
| GP2      | DIN / DATA         |
| 3V3      | VIN (3.3 V)        |
| GND      | GND                |

On a PCM5102 also tie SCK→GND (uses internal PLL) and FLT/DEMP/XSMT per the
board's defaults (XSMT high to un-mute). Output is line level — feed an amp or
powered speakers.

**PWM (no DAC).** Set `AUDIO_BACKEND_PWM 1` / `AUDIO_BACKEND_I2S 0`. Audio comes
straight off **GP0 (left)** and **GP1 (right)**; pass each through a simple RC
low-pass (e.g. 1 kΩ + 10 nF) and AC-couple into an amp. Noisier and lower
resolution than I2S, but needs no extra chip.

---

## Arduino IDE setup

1. Install the **arduino-pico** core (Earle Philhower). Boards Manager URL:
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
   (Bundles the **I2S** and **PWMAudio** libraries used here — no separate install.)
2. Install the **U8g2** library (Library Manager).
3. Select your Pico board.
4. **Flash Size:** choose a layout that includes a filesystem, e.g.
   *"2MB (Sketch 1MB / FS 1MB)"*. The sketch + baked SoundFont need ~1.4 MB of
   program flash, and song save/load needs the FS partition.
5. Open `GM_Sequencer.ino` (keep all files in the same folder) and upload.

---

## Adding more SoundFonts

The font is baked into flash as a C array by `tools/sf2_to_cpp.py`:

```
python3 tools/sf2_to_cpp.py soundfonts/YourFont.sf2 --name YourFont --out .
```

That writes `Soundfont_YourFont.cpp/.h`. To switch the active font, include that
header in `Synth.cpp` and point `SF2::begin(...)` at its array. Keep fonts small
(GM banks of a few hundred KB) so they fit alongside the sketch, and prefer
16-bit PCM samples (the reader streams `int16` directly from flash).

---

## UI manual

The control model is identical on every page, so it becomes muscle memory:

| Input              | Action                                                       |
|--------------------|--------------------------------------------------------------|
| Rotate encoder     | move cursor / selection                                      |
| SHIFT + rotate     | change the value at the cursor                               |
| Click encoder      | primary action (SEQ: toggle step · SONG: run the action row) |
| SHIFT + click      | secondary (SEQ: cycle the per-step field)                    |
| Long-press encoder | audition / preview the selected note                         |
| PLAY               | start / stop (SHIFT = start from top)                         |
| PAGE               | next page (SHIFT = previous)                                  |
| TRACK              | next track (SHIFT = previous · long-hold = clear track)      |
| MUTE               | mute track (SHIFT = solo track)                              |

The status bar shows page, track, MIDI channel, mute/solo flag, transport
(filled square = running) and BPM.

### Pages

- **SEQ** — the step grid (hollow = empty, solid = active, halo = cursor; the
  playhead inverts the current step while running). The bottom line shows the
  cursor step number, its note name (or drum name on channel 10), and the
  selected per-step field/value. SHIFT+rotate edits the selected field; SHIFT+
  click cycles through **NOTE · VEL · GATE · PROB · MICRO**. Each track has its
  own length (1–64) for polymetric patterns.
- **INST** — Channel (1–16; ch 10 = GM drums), Program (shows the real SoundFont
  patch name, or drum-kit name on ch 10), Bank, Octave (−3…+3), Length.
- **MIX** — Volume (CC7), Pan (CC10, shown L/C/R), Reverb send, Chorus send.
  *(Reverb/chorus sends are stored per track but have no audible effect yet —
  there is no on-chip FX engine; they are reserved for a future version.)*
- **FX** — global Reverb type (0–7), Chorus type (0–7), Master Volume.
  *(Reverb/chorus type reserved as above; Master Volume scales the synth output.)*
- **SONG** — BPM (20–300), Swing (0–75%), Resolution (steps/beat: 1,2,3,4,6,8),
  Clock source (INT / EXT-reserved), Slot (1–8, `*` = used), then the action
  rows **Save**, **Load**, **GM Reset** (navigate to one and click).

---

## Feature summary

- 16 tracks (the GM channel ceiling; track 10 → channel 10 = drums).
- Up to 64 steps per track, independent per-track length (polymeter).
- Per step: on/off, note, velocity, gate %, probability %, ±12-tick micro-timing,
  tie.
- Swing, selectable grid resolution, 20–300 BPM, drift-free 96-PPQN timing.
- On-chip SoundFont synth: up to 16 sample voices, per-track program/bank,
  volume, pan, octave and master volume; SF2 played directly from flash.
- Audio out via I2S DAC (default) or PWM — selectable in `Config.h`.
- 8 song slots in flash (LittleFS).
- GP14 reserved for a future analog click-out.

---

## Versioning

Each release lives on its own git branch (e.g. `v1.0.0`) so you can always roll
back. Create the next version on a fresh branch before editing.
