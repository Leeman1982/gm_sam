# GM Sequencer — RP2350 (Pico 2) + Dream SAM2695

A professional dual-core MIDI step sequencer for the Raspberry Pi Pico 2 (RP2350)
that drives a Dream **SAM2695** General-MIDI module (the AliExpress "GM 2.0
synthesis module"). 16 tracks, 64 steps, per-step micro-timing/probability,
rock-solid drift-free timing, and a clear SH1106 OLED UI.

The SAM2695 is a 64-voice GM/GS/MT-32 synth-on-a-chip with onboard reverb +
chorus and a 4-band EQ, controlled over standard serial MIDI at 31250 baud.
This firmware exposes that feature set: per-track instrument/bank, volume, pan,
reverb & chorus sends, global reverb/chorus types and master volume, plus a GM
reset.

---

## Architecture

Two cores, no locks in the audio path:

- **core1 — engine.** Owns all timing and is the *only* core that touches the
  MIDI UART. Internal resolution is **96 PPQN** with a drift-free integer tick
  accumulator (no floats in the hot loop — the RP2350's M33 has an FPU, but
  integer timing stays bit-exact and drift-free, so it isn't needed). Emits MIDI clock (0xF8) on the
  24-PPQN grid plus Start/Stop/Continue — the foundation for future external
  sync. An event scheduler handles note on/off with swing and per-step micro
  offsets (offs fire before ons at the same tick).
- **core0 — UI.** SH1106 rendering (~30 fps), encoder + buttons, and LittleFS
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
| MIDI TX → module  | GP0      | UART0 TX to the SAM2695 MIDI-IN / RX pad     |
| MIDI RX (future)  | GP1      | reserved for external clock-sync input       |
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

Power the SAM2695 and OLED per their own requirements; share a common ground
with the Pico. Add a series resistor on the MIDI line per the module's docs if
it expects an opto-isolated/standard MIDI input.

### Using an Arduino-style MIDI shield (DIN-5 IN/OUT/THRU)

A generic opto-isolated MIDI shield gives you proper DIN-5 sockets; drive the
SAM2695 from its MIDI OUT. These are **Uno form-factor** boards, so they do **not**
plug onto a Pico 2 — hand-wire them by **function**, not by the silkscreen pin
number. They use the Arduino convention where pad 0 = RX and pad 1 = TX, and MIDI
OUT is driven by the TX (D1) pad:

| Pico pin     | Shield pad            | Why                                  |
|--------------|-----------------------|--------------------------------------|
| GP0 (TX)     | "1 / TX" pad          | drives MIDI OUT (→ SAM2695 MIDI IN)  |
| GP1 (RX)     | "0 / RX" pad          | opto output (future clock sync-in)   |
| 3V3          | shield 3V3 / VCC pad  | see warning below                    |
| GND          | shield GND            | common ground                        |

**Prefer a 3.3 V-native shield** (run its VCC from the Pico 2's 3V3 pin) — this is
the cleanest option and needs no level-shifting. The RP2350 is *not* 5V-tolerant: a
shield's MIDI-IN opto output idles at the shield's VCC, so at 5 V that line would
over-volt GP1 when you wire the sync-in. At 3.3 V the RX line stays safe and MIDI
OUT still drives the SAM2695 fine. If you only have a 5 V-only shield, powering it
from 3V3 is the fallback that keeps GP1 safe.

The shield's **RX-enable switch** (the ON/OFF slider, sometimes labelled "S2") only
connects/disconnects MIDI IN from the RX pin — it has no effect on MIDI OUT or
playback. Leave it ON only when you wire up the GP1 sync-in. If you get silence on
OUT, the most likely cause is GP0 landing on the RX pad instead of the TX pad.

---

## Arduino IDE setup

1. Install the **arduino-pico** core (Earle Philhower), **v4.0.1 or newer** (the
   release that added RP2350 / Pico 2 support). Boards Manager URL:
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
2. Install the **U8g2** library (Library Manager).
3. Select **Raspberry Pi Pico 2** (Tools → Board).
4. **Flash Size:** choose a 4 MB layout that includes a filesystem, e.g.
   *"Sketch 2MB / FS 2MB"*. Song save/load needs the FS partition.
5. Open `GM_Sequencer.ino` (keep all files in the same folder) and upload.

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
- **INST** — Channel (1–16; ch 10 = GM drums), Program (GM instrument name, or
  drum-kit name on ch 10), Bank (GM / MT-32), Octave (−3…+3), Length.
- **MIX** — Volume (CC7), Pan (CC10, shown L/C/R), Reverb send (CC91), Chorus
  send (CC93).
- **FX** — global Reverb type (0–7), Chorus type (0–7), Master Volume (GM SysEx).
- **SONG** — BPM (20–300), Swing (0–75%), Resolution (steps/beat: 1,2,3,4,6,8),
  Clock source (INT / EXT-reserved), Slot (1–8, `*` = used), then the action
  rows **Save**, **Load**, **GM Reset** (navigate to one and click).

---

## Feature summary

- 16 tracks (the GM channel ceiling; track 10 → channel 10 = drums).
- Up to 64 steps per track, independent per-track length (polymeter).
- Per step: on/off, note, velocity, gate %, probability %, ±12-tick micro-timing.
- Swing, selectable grid resolution, 20–300 BPM, drift-free 96-PPQN timing.
- Full SAM2695 control: program/bank, volume, pan, reverb/chorus sends, global
  reverb/chorus type, master volume, GM reset.
- 8 song slots in flash (LittleFS).
- MIDI clock + transport output already emitted, ready for syncing external
  gear; GP1/GP14 reserved for future clock-in and analog click-out.

---

## Versioning

Each release lives on its own git branch (e.g. `v1.0.0`) so you can always roll
back. Create the next version on a fresh branch before editing.
