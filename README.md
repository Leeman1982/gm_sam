# GM Sequencer — RP2350 (Pico 2) + VS1053B

A professional dual-core MIDI step sequencer for the Raspberry Pi Pico 2 (RP2350)
that drives a **VS1053B** General-MIDI synth module (the cheap red "VS1053 MP3
Shield" clones). 16 tracks, 64 steps, per-step micro-timing/probability,
rock-solid drift-free timing, and a clear SSD1309 OLED UI.

The VS1053B is a 64-voice (40 sustained) GM1 + one-bank-GM2 synth-on-a-chip with
onboard reverb. Rather than a UART MIDI port, it is driven over **SPI**: the chip
is brought up in real-time MIDI mode with a small software start patch, then fed
0x00-padded MIDI bytes on its SDI (data) bus. This firmware exposes the GM feature
set: per-track instrument/bank, volume, pan, reverb & chorus sends, global
reverb/chorus types and master volume, plus a GM reset.

---

## Architecture

Two cores, no locks in the audio path:

- **core1 — engine.** Owns all timing and is the *only* core that touches the
  VS1053B over SPI. Internal resolution is **96 PPQN** with a drift-free integer
  tick accumulator (no floats in the hot loop — the RP2350's M33 has an FPU, but
  integer timing stays bit-exact and drift-free, so it isn't needed). An event
  scheduler handles note on/off with swing and per-step micro offsets (offs fire
  before ons at the same tick). The transport/clock-out hooks are no-ops on the
  VS1053 (it's an internal synth with no MIDI-OUT port), but remain in the API for
  a future external-sync output.
- **core0 — UI.** SSD1309 rendering (~30 fps), encoder + buttons, and LittleFS
  song storage.

Cross-core sharing uses single aligned 8/16-bit scalars (atomic on the M33) and
volatile request flags. Setting changes are reconciled to MIDI each engine pass,
so edits are audible immediately. Loading a song is the one whole-struct swap, so
it briefly parks the engine with a quiesce handshake before the copy — playback
itself stays lock-free.

---

## Wiring

All buttons and the encoder are **active-low** with internal pull-ups — wire the
common side to **GND**.

The Pico 2 (RP2350A) is **pin-compatible** with the original Pico, so every pin
below is unchanged. The active-low + internal-pull-up scheme also sidesteps RP2350
erratum **E9**: that GPIO input-latch issue affects internal *pull-downs*, but here
every input is pulled **up** and its switch hard-drives it to GND, so reads stay
clean — no external resistors needed.

| Signal            | Pico pin | Notes                                        |
|-------------------|----------|----------------------------------------------|
| VS1053 SCK        | GP2      | SPI0 SCLK                                     |
| VS1053 MOSI (SI)  | GP3      | SPI0 TX                                       |
| VS1053 MISO (SO)  | GP0      | SPI0 RX                                       |
| VS1053 XCS        | GP1      | SCI (command) chip-select                     |
| VS1053 XDCS       | GP15     | SDI (data) chip-select                        |
| VS1053 DREQ       | GP16     | data-request, input to the Pico               |
| VS1053 XRESET     | GP17     | active-low reset, Pico drives it high         |
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

The VS1053B core logic is **3.3 V**, so it wires directly to the Pico 2 with **no
level shifting**. Feed the module 5 V on its VIN/5V pin (it has its own regulator)
and share ground with the Pico. The on-board microSD shares the SPI bus but has its
own CARD-CS — leave it unconnected for pure MIDI use. Pin assignments live in
`Config.h` (`PIN_VS_*`); SCK/MOSI/MISO must stay within a valid arduino-pico SPI0
pin group if you change them.

### How the VS1053B is brought up (SPI real-time MIDI)

On the cheap red clone shields the hardware GPIO0/GPIO1 "MIDI mode" strap is tied
low through 100k and not broken out, so the firmware enables MIDI **in software**:

1. **Hardware reset** — pulse XRESET low, wait for DREQ to rise.
2. **Clock multiplier** — write `SCI_CLOCKF` (`VS_CLOCKF`, default 3.0×) *before*
   loading the patch; raise to 3.5×/4.0× for more polyphony/reverb headroom.
3. **Load the start patch** — the 28-word `vs1053b-rtmidistart` plugin is written
   into instruction RAM; its last record auto-starts real-time MIDI mode.
4. **Stream MIDI over SDI** — each MIDI byte is sent as `0x00` + byte (the
   datasheet's "pad with 0xFF" is a known error). SPI runs slow during init, fast
   for note streaming, and every op waits on DREQ.

Master volume uses the chip's native `SCI_VOL` register (no SysEx), and GM-Reset is
emulated with All-Sound-Off / All-Notes-Off / Reset-All-Controllers because the
VS1053 ROM MIDI parser does not skip SysEx.

### Troubleshooting: no sound

`GM_VS_DIAG` (default 1 in `Config.h`) shows a **live** VS1053 scan on the OLED at
boot (no serial needed — works for UF2 builds): `ver=<n> AUDATA=<hhhh>` with
`RT-MIDI: OK/FAIL` and a spinner. core1 keeps re-attempting the bring-up
(`GMSynth::serviceHealth`), so the screen updates in real time — **wiggle a solder
joint and watch it flip from FAIL to `RT-MIDI OK` the instant the chip answers.**
It also auto-recovers if the module is reset/plugged in after boot. Any button
dismisses it (it also auto-continues ~3 s after coming alive, or after 60 s).

- **`ver=4  AUDATA=AC45  RT-MIDI: OK`** — SPI, chip, and MIDI mode are all good, so
  the chip *is* playing the notes. The fault is the **audio output**: use the
  module's own headphone/line jack, and make sure the headphone ground goes to the
  VS1053 **audio ground (GBUF/AGND)**, not a digital GND. The output is line/phone
  level — it will *not* drive a bare speaker; use headphones or a powered amp.
- **`RT-MIDI: FAIL  ver=0  AUDATA=0000`** — all-zero reads = MISO held low = the
  chip is **powered down or held in reset**. Per the VS1053 guide, the board has a
  weak pull-down on XRESET: if it isn't driven high "the chip stays powered down and
  ignores everything." Check **5V power + GND** to the module, and that
  **XRESET→GP17 measures ~3.3 V** after boot (i.e. the reset line really reaches the
  chip). This is the most common bring-up fault.
- **`RT-MIDI: FAIL  AUDATA=FFFF` (or `ver=15`)** — all-one reads = MISO floating
  high = a bus/wiring issue. Check **MISO→GP0**, that **XCS/XDCS aren't swapped**,
  and — on modules with an SD slot — that the **microSD card-CS is HIGH** (a
  floating SD-CS can corrupt the shared bus; tie it to 3V3 or set `PIN_VS_XCARDCS`).
  Note the VS1053 guide says CARD-CS *can* be left unconnected for pure MIDI, so try
  the MISO/XCS path first.

For a byte-level view, `MIDI_TX_DEBUG 1` additionally streams every MIDI message as
hex over USB serial (115200). Set both flags back to 0 once you have sound.

---

## Arduino IDE setup

1. Install the **arduino-pico** core (Earle Philhower), **v4.0.1 or newer** (the
   release that added RP2350 / Pico 2 support). Boards Manager URL:
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
2. Install the **U8g2** library (Library Manager). The VS1053 driver uses only the
   built-in **SPI** library — no extra VS1053 library is required.
3. Select **Raspberry Pi Pico 2** (Tools → Board).
4. **Flash Size:** choose a 4 MB layout that includes a filesystem, e.g.
   *"Sketch 2MB / FS 2MB"*. Song save/load needs the FS partition.
5. Open `GM_Sequencer.ino` (keep all files in the same folder) and upload.

---

## UI manual

The control model is identical on every page, so it becomes muscle memory:

| Input              | Action                                                       |
|--------------------|--------------------------------------------------------------|
| Rotate encoder     | move cursor (menus: change the value while a row is in edit-mode) |
| SHIFT + rotate     | change the value at the cursor (menu shortcut; no edit-mode needed) |
| Click encoder      | SEQ: toggle step · menus: enter/exit edit on the row · SONG action rows: run |
| SHIFT + click      | secondary (SEQ: cycle the per-step field)                    |
| Long-press encoder | audition / preview the selected note                         |
| PLAY               | start / stop (SHIFT = start from top)                         |
| PAGE               | next page (SHIFT = previous)                                  |
| TRACK              | next track (SHIFT = previous · long-hold = clear track)      |
| MUTE               | mute track (SHIFT = solo track)                              |

The status bar shows page, track, MIDI channel, mute/solo flag, transport
(filled square = running) and BPM.

On the list pages (INST / MIX / FX / SONG) the encoder **click enters edit-mode**
on the highlighted row — shown as `[value]` — so you rotate to change it and click
again to confirm (no need to hold SHIFT; SHIFT+rotate still works as a shortcut).
The SONG action rows (Save / Load / GM Reset) run immediately on click.

### Pages

- **SEQ** — the step grid (hollow = empty, solid = active, halo = cursor; the
  playhead inverts the current step while running). The bottom line shows the
  cursor step number, its note name (or drum name on channel 10), and the
  selected per-step field/value. SHIFT+rotate edits the selected field; SHIFT+
  click cycles through **NOTE · VEL · GATE · PROB · MICRO**. Each track has its
  own length (1–64) for polymetric patterns.
- **INST** — Channel (1–16; ch 10 = GM drums), Program (GM instrument name, or
  drum-kit name on ch 10), Bank (GM / MT-32), Octave (−3…+3), Length.
- **MIX** — Volume (CC7), Pan (CC10, shown L/C/R), Reverb send (CC91), Chorus
  send (CC93).
- **FX** — global Reverb type (0–7), Chorus type (0–7), Master Volume (native
  VS1053 `SCI_VOL`).
- **SONG** — BPM (20–300), Swing (0–75%), Resolution (steps/beat: 1,2,3,4,6,8),
  Clock source (INT / EXT-reserved), Slot (1–8, `*` = used), then the action
  rows **Save**, **Load**, **GM Reset** (navigate to one and click).

---

## Feature summary

- 16 tracks (the GM channel ceiling; track 10 → channel 10 = drums).
- Up to 64 steps per track, independent per-track length (polymeter).
- Per step: on/off, note, velocity, gate %, probability %, ±12-tick micro-timing.
- Swing, selectable grid resolution, 20–300 BPM, drift-free 96-PPQN timing.
- Full VS1053B GM control: program/bank, volume, pan, reverb/chorus sends, global
  reverb/chorus type, master volume, GM reset.
- 8 song slots in flash (LittleFS).
- Driven over SPI in real-time MIDI mode (software start patch, SDI streaming);
  GP14 reserved for a future analog click-out.

---

## Versioning

Each release lives on its own git branch (e.g. `v1.0.0`) so you can always roll
back. Create the next version on a fresh branch before editing.
