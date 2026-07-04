# GM Supreme Sequencer — RP2040 + Dream SAM2695

A dual-core MIDI step sequencer for the Raspberry Pi Pico (RP2040) that drives
a Dream **SAM2695** General-MIDI module (the AliExpress "GM 2.0 / MIDI digital
music module"). 16 tracks, 16–64 steps per track, **scale lock**, per-step
ratchets/probability/micro-timing, per-track play direction and clock divide,
a 4x4 keypad for live step entry and mutes, 2 analog macro pots, and **every**
SAM2695 parameter reachable from the SH1106 OLED UI.

---

## Hardware

| Part | Role |
|------|------|
| Raspberry Pi Pico (RP2040) | dual-core brain |
| SAM2695 GM MIDI module | the sound engine (serial MIDI @ 31250) |
| 1.3" SH1106 OLED module with EC11 encoder + BACK/CONFIRM buttons | display + main controls |
| 4x4 matrix keypad **or** 16 buttons + CD74HC4067 mux | step keys / track keys / mutes |
| 2x 10k linear pots (optional) | context-sensitive macro controls |

## Architecture

Two cores, no locks in the audio path:

- **core1 — engine.** Owns all timing and is the *only* core that touches the
  MIDI UART. Internal resolution is **96 PPQN** with a drift-free integer tick
  accumulator (no floats in the hot loop). Emits MIDI clock (0xF8) plus
  Start/Stop/Continue. An event scheduler handles note on/off with swing,
  per-step micro offsets and ratchet subdivisions (offs fire before ons at the
  same tick).
- **core0 — UI.** SH1106 rendering (~30 fps), encoder + buttons + keypad +
  pots, and LittleFS song storage.

Cross-core sharing uses single aligned 8/16-bit scalars (atomic on the M0+) and
volatile request flags. Setting changes are reconciled to MIDI each engine pass,
so every edit is audible immediately.

---

## Wiring

All buttons, keys and the encoder are **active-low** with internal pull-ups —
wire common sides to **GND**. Share one ground between Pico, OLED, keypad and
the SAM2695 module.

### OLED module (header: CON SDA SCL PSH TRA TRB BAK GND VCC)

| Module pad | Pico pin | Function |
|------------|----------|----------|
| SDA        | GP4      | I2C0 data |
| SCL        | GP5      | I2C0 clock, 400 kHz |
| TRA        | GP6      | encoder A |
| TRB        | GP7      | encoder B |
| PSH        | GP8      | encoder push |
| CON        | GP9      | CONFIRM = play/stop (hold BAK = from top, long = panic) |
| BAK        | GP10     | BACK = next page / exit edit; **hold = SHIFT** |
| VCC / GND  | 3V3 / GND | |

### MIDI

| Signal            | Pico pin | Notes                                    |
|-------------------|----------|------------------------------------------|
| MIDI TX → module  | GP0      | UART0 TX to the SAM2695 MIDI-IN / RX pad |
| MIDI RX (future)  | GP1      | reserved for external clock-sync input   |

### 4x4 matrix keypad (KEYPAD_MODE 1, default)

| Keypad pin | Pico pin |
|------------|----------|
| Row 1..4   | GP16 GP17 GP18 GP19 |
| Col 1..4   | GP20 GP21 GP22 GP2  |

Key numbering is physical: left-to-right, top-to-bottom = key 1..16
(`1 2 3 A / 4 5 6 B / 7 8 9 C / * 0 # D`).

### CD74HC4067 option (KEYPAD_MODE 2)

16 discrete buttons instead of a matrix: each mux channel C0..C15 goes to one
button, other side of every button to GND. Set `KEYPAD_MODE` to `2` in
`Config.h`.

| Mux pin | Pico pin |
|---------|----------|
| S0 S1 S2 S3 | GP16 GP17 GP18 GP19 |
| SIG     | GP20 (internal pull-up) |
| EN      | GND |
| VCC/GND | 3V3 / GND |

### Macro pots (optional, `ENABLE_POTS`)

| Pot | Pico pin | Melodic track | Drum track (ch 10) |
|-----|----------|---------------|--------------------|
| POT1 wiper | GP26 / ADC0 | **filter cutoff** | **reverb send** |
| POT2 wiper | GP27 / ADC1 | **filter resonance** | **track volume** |

Pot ends to 3V3 and GND. The pots use **soft pickup**: after switching tracks
a pot stays inactive until the knob sweeps across the parameter's current
value, so the sound never jumps.

### Optional extra buttons (leave unwired if you don't want them)

| Button | Pico pin | Action |
|--------|----------|--------|
| PAGE   | GP11     | next page (SHIFT = previous) |
| TRACK  | GP12     | next track (SHIFT = prev, long = clear track) |
| MUTE   | GP13     | mute (SHIFT = solo) |

---

## Arduino IDE setup

1. Install the **arduino-pico** core (Earle Philhower). Boards Manager URL:
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
2. Install the **U8g2** library (Library Manager).
3. Select your Pico board.
4. **Flash Size:** choose a layout that includes a filesystem, e.g.
   *"2MB (Sketch 1MB / FS 1MB)"*. Song save/load needs the FS partition.
5. Open `GM_Sequencer.ino` (keep all files in the same folder) and upload.

---

## Control model

| Input                | Action                                                        |
|----------------------|---------------------------------------------------------------|
| Rotate encoder       | move cursor / selection                                       |
| Click encoder        | SEQ: toggle step · menu: enter/exit **edit mode** · `>` rows: run |
| Hold **BACK**        | SHIFT modifier                                                |
| SHIFT + rotate       | change the value at the cursor directly                       |
| SHIFT + click        | SEQ: cycle the per-step field · PERF: solo                    |
| Long-press encoder   | audition the selected note                                    |
| **BACK** (short)     | exit edit mode, else next page (wraps)                        |
| **CONFIRM**          | play / stop (SHIFT = from top · long-hold = STOP + PANIC)     |
| Keypad key           | SEQ: toggle step · PERF: mute track · menus: select track     |
| SHIFT + key          | select track 1–16 (works on every page)                       |
| Hold key + rotate    | SEQ: edit that step's selected field without moving the cursor|
| Long-press key       | SEQ: jump cursor + audition · PERF: solo track                |
| POT1 / POT2          | macro control for the selected track (see wiring table)       |

The status bar shows page, track, MIDI channel, mute/solo flag, **L** when the
scale lock is on, transport state and BPM.

### Pages (BACK steps through them)

- **SEQ** — the step grid (hollow = empty, solid = active, halo = cursor; the
  playhead inverts the running step). With >16 steps the grid wraps onto up to
  4 rows; the keypad always addresses the cursor's row of 16. The bottom line
  shows the cursor step, its note/drum name, and the selected per-step field.
  SHIFT+click cycles **NOTE · VEL · GATE · PROB · RTCH · MICRO · TIE**.
- **PERF** — 4x4 performance grid mirroring the keypad: tap = mute,
  long-press = solo. The dot marks audible tracks.
- **TRK** — Channel (1–16; ch 10 = GM drums), Program (GM name / drum kit),
  Bank (GM / MT-32), Octave (−3…+3), Transpose (±12 st), Length (1–64),
  Direction (FWD/REV/PNG/RND), Clock Div (/1…/8), Humanize Vel, Humanize Time.
- **MIX** — Volume, Pan (L/C/R), Reverb send, Chorus send, Expression,
  Mod wheel, Mute, Solo.
- **SYN** — the SAM2695 voice controls (NRPN, ± around neutral): Cutoff,
  Resonance, Attack, Decay, Release, Vibrato Rate/Depth/Delay, plus
  Bend Range (RPN), Portamento on/time, Sustain.
- **FX** — Reverb type/level/time, Chorus type/level/rate/depth
  (GS SysEx masters), Master Volume.
- **SCL** — **scale lock**: Root (C…B), Scale (Chromatic, Major, Minor,
  Harmonic/Melodic Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian,
  Major/Minor Pentatonic, Blues, Whole Tone, Diminished, Double Harmonic),
  Lock ON/OFF, plus `> Quant Trk` / `> Quant All` to snap existing notes.
  When locked, note editing walks scale degrees and playback (after octave +
  transpose) is quantized to the scale. Channel 10 drums are never quantized.
- **SONG** — BPM (20–300), Swing (0–75%), Resolution (1–8 steps/beat), Clock
  source (INT / EXT-reserved), Slot (1–8, `*` = used), then `> Save`,
  `> Load`, `> GM Reset`, `> Panic`.

---

## Feature summary

- 16 tracks (the GM channel ceiling; track 10 → channel 10 = drums).
- 16–64 steps per track, independent per-track length (polymeter), play
  direction (forward / reverse / ping-pong / random) and clock divider
  (polyrhythm).
- Per step: on/off, note, velocity, gate %, probability %, **ratchet x1–x4**,
  ±12-tick micro-timing, tie.
- **Scale lock** with 16 scales + root, degree-walking note entry, playback
  quantization and one-shot pattern quantize.
- Per-track humanize (velocity spread + timing slop).
- Full SAM2695 control: program/bank, volume, pan, reverb/chorus sends,
  expression, modulation, filter cutoff/resonance, envelope attack/decay/
  release, vibrato rate/depth/delay, bend range, portamento, sustain, global
  reverb/chorus type + GS master level/time/rate/depth, master volume, GM reset.
- 2 macro pots with soft pickup, auto-mapped to the most effective parameters
  of the selected track.
- Swing, selectable grid resolution, 20–300 BPM, drift-free 96-PPQN timing.
- 8 song slots in flash (LittleFS).
- MIDI clock + transport output already emitted; GP1/GP14 reserved for future
  clock-in and analog click-out.

---

## Versioning

Each release lives on its own git branch so you can always roll back. Create
the next version on a fresh branch before editing. Song files are versioned
(`SONG_VERSION`); saves from older firmware are politely ignored rather than
mis-loaded.
