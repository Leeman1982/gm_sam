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

**Default SoundFont:** *SYNTHGMS* — a complete 1 MB General-MIDI set (128
melodic programs + a drum kit, banks 0 and 128). It is baked into flash and read
directly from memory-mapped XIP, so the sample data costs no RAM, and it fits
both the 4 MB and 16 MB boards. Two more fonts ship ready to select in
`Config.h` (see *SoundFonts* below):

- **Vintage Dreams Waves v2.0** (0.3 MB) — characterful synth/vintage GM set.
- **Power GM 1.5** (8 MB) — high-quality GM; needs a 16 MB board and is loaded
  from a flash partition rather than baked (see the `picotool` steps below).

### Target hardware

Built for an **RP2040** board (e.g. the Raspberry Pi Pico, or a TENSTAR RP2040
Pro Micro in its 4 MB / 16 MB flash variants). The code is kept portable to the
**RP2350 / Pico 2**, which adds a hardware FPU — there you can raise
`AUDIO_RATE` to 44100 and `SYNTH_MAX_VOICES` for more polyphony and headroom.

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

## SoundFonts

Enable **any subset** of fonts in `Config.h` — every enabled one stays resident
in flash and you switch between them **live** on the SONG page (the *Font* row,
SHIFT+rotate). The first enabled font is active at boot. Switching cuts sounding
voices for an instant, then the next note uses the new font; each channel keeps
its program number, so e.g. program 0 plays the new font's program 0.

| Define              | Font              | Size  | How it's stored        | Board   |
|---------------------|-------------------|-------|------------------------|---------|
| `FONT_SYNTHGMS`     | SYNTHGMS (GM)     | 1 MB  | baked C array          | 4/16 MB |
| `FONT_VINTAGEDREAMS`| Vintage Dreams    | 0.3 MB| baked C array          | 4/16 MB |
| `FONT_POWERGM`      | Power GM 1.5 (GM) | 8 MB  | flash partition        | 16 MB   |

The defaults enable all three (a 16 MB board fits SYNTHGMS + Vintage Dreams baked
in the low ~1.3 MB, Power GM at the 2 MB flash offset, and the song FS above it).
`FONT_POWERGM` is skipped automatically until you actually write the font to
flash, so it simply won't appear in the selector before then.

Each font has a **makeup gain** (`gainQ12` in the `Synth.cpp` registry, 4096 =
unity) so loudness stays consistent when you switch — fonts are mastered at very
different levels. The shipped values were measured from a representative GM
phrase; adjust them to taste if a font sounds hot or quiet.

Use **16-bit PCM** SoundFonts (the reader streams `int16` straight from flash).
Compressed SF2/SF3 (Ogg-Vorbis samples) are **not** supported.

### Baking a small font (≤ ~1.5 MB)

`tools/sf2_to_cpp.py` turns a `.sf2` into a flash-resident C array:

```
python3 tools/sf2_to_cpp.py soundfonts/YourFont.sf2 --name YourFont --out .
```

That writes `Soundfont_YourFont.cpp/.h` (the array is auto-guarded behind a
`FONT_YOURFONT` define). To make it selectable, add `#define FONT_YOURFONT 1` in
`Config.h`, then in `Synth.cpp` include its header and add one registry line in
`begin()` (mirror the existing `fonts_[fontCount_++] = { ... }` entries).

### Loading a large font: Power GM 1.5 (8 MB, 16 MB board only)

An 8 MB font is too big to compile as a C array, so it is written once to a
fixed flash region and read in place from XIP:

1. In `Config.h` set `FONT_POWERGM 1` (and the others to `0`).
2. Build/upload the sketch as usual (it occupies the low ~0.4 MB of flash).
3. Write the font once with [`picotool`](https://github.com/raspberrypi/picotool)
   at the offset `FONT_FLASH_OFFSET` (default `0x200000` = 2 MB):

   ```
   picotool load soundfonts/PowerGM_1.5.sf2 -o 0x10200000
   picotool reboot
   ```

   `0x10000000` is the RP2040 XIP base; `0x10200000` = base + 2 MB. The font
   (8 MB) then lives at 2–10 MB, clear of the sketch below and the LittleFS song
   partition above. The font survives ordinary sketch re-uploads (they only
   rewrite the low flash). Adjust `FONT_FLASH_OFFSET` if your sketch ever grows
   past 2 MB.

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
  Clock source (INT / EXT-reserved), Slot (1–8, `*` = used), **Font** (SHIFT+
  rotate to switch the active SoundFont live — see below), then the action rows
  **Save**, **Load**, **GM Reset** (navigate to one and click).

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
