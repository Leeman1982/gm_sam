# GM Sequencer — Pico 2 (RP2350) + internal SoundFont synth

A dual-core MIDI step sequencer for the **Raspberry Pi Pico 2 (RP2350)** with an
**internal SoundFont (SF2) synthesizer** as its voice engine — no external GM
module. 16 tracks, 64 steps, per-step micro-timing/probability, drift-free
timing, an SH1106 OLED UI, a rotary-encoder + 4×4-keypad control surface, and
stereo audio out through a **PCM5100A I2S DAC**. SF2 banks live on an external
SPI flash module; songs live in the Pico's on-board LittleFS.

> Previously this firmware drove a Dream SAM2695 over serial MIDI. **Only the
> backend changed.** Every voice still flows through the `GMSynth::` API, so the
> sequencer engine and UI are unchanged — `GMSynth` now renders SF2 voices to
> the I2S DAC instead of sending MIDI to a chip. See `SoundFont.cpp`.

---

## Architecture

Two cores, lock-free audio:

- **core1 — engine + audio bring-up.** Owns the 96-PPQN drift-free transport
  (integer tick accumulator, no floats in the hot loop). It also starts the
  SoundFont engine and I2S output. Note events from the engine are pushed into a
  **lock-free single-producer/single-consumer ring**; the actual SF2 rendering
  happens in the **I2S DMA interrupt**, which drains the ring and renders a
  stereo block. Audio therefore never starves on the engine's timing waits.
- **core0 — UI.** SH1106 rendering (~30 fps), the encoder + keypad + page LEDs,
  and LittleFS song storage.

```
 sequencer engine ──GMSynth::──▶ SoundFont queue ──ring──▶ I2S DMA IRQ
   (core1)                         (lock-free)              renderBlock() ─▶ PCM5100A
```

Cross-core pattern state uses single aligned 8/16-bit scalars (atomic on the
RP2350) and volatile request flags, exactly as before.

---

## Bill of materials

| Part                                   | Role                                            |
|----------------------------------------|-------------------------------------------------|
| Raspberry Pi Pico 2 (RP2350)           | MCU (dual core, 520 KB RAM)                      |
| PCM5100A I2S DAC board (3.5 mm TRS)     | stereo audio output                             |
| SPI NOR flash module ("128 MB")        | holds the SF2 bank image                        |
| SH1106 1.3" 128×64 I2C OLED            | display                                         |
| EC11 rotary encoder w/ push switch     | navigate / edit / SHIFT                          |
| 4×4 matrix keypad                      | step entry + note play + transport              |
| 3 LEDs                                 | show the active keypad page                      |

The CD74HC4067 16-channel mux from your parts pile is **not required** by this
firmware (the keypad is read as an 8-pin matrix). It's left for future GPIO
expansion.

---

## Wiring (RP2350 `GPxx`)

All buttons/encoder are active-low with internal pull-ups — wire their common
side to **GND**. Pins are defined in `Config.h`; change them there if needed.

| Signal                | Pico 2 pin | Notes                                       |
|-----------------------|------------|---------------------------------------------|
| OLED SDA / SCL        | GP4 / GP5  | I2C0, 400 kHz                               |
| Encoder A / B / SW    | GP6 / GP7 / GP8 | quadrature + push (tap=click, hold=SHIFT) |
| I2S DIN (SD)          | GP9        | → PCM5100A SD                               |
| I2S BCLK / LRCLK      | GP10 / GP11 | → PCM5100A BCK / WS (LRCLK must be BCLK+1) |
| PCM5100A SCK/MCLK     | —          | leave unconnected (self-clocked)            |
| SF2 flash MISO/CS/SCK/MOSI | GP12 / GP13 / GP14 / GP15 | SPI1                         |
| Keypad rows           | GP16–GP19  | driven low one at a time                    |
| Keypad cols           | GP20, GP21, GP22, GP26 | read with pull-ups (active-low) |
| Page LEDs 0/1/2       | GP0 / GP1 / GP2 | STEP / NOTE / CTRL (active-high)        |

PCM5100A and the OLED have their own power needs; share a common ground with the
Pico. The SF2 flash module is a 3.3 V SPI part — power it from 3V3.

---

## Software setup (Arduino IDE)

1. Install the **arduino-pico** core (Earle Philhower). Boards Manager URL:
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
2. Install the **U8g2** library (Library Manager). The **I2S** library ships
   with arduino-pico.
3. Add **TinySoundFont**: download `tsf.h` from
   <https://github.com/schellingb/TinySoundFont> and drop it in this sketch
   folder (next to `tsf_impl.cpp`). It builds like any header — no extra steps.
4. Select **Raspberry Pi Pico 2** and a Flash Size layout **with a filesystem**
   partition (Sketch + FS) so song save/load works.
5. Open `GM_Sequencer.ino` (keep all files in the same folder) and upload.

If `tsf.h` is missing the project still builds and runs, but the synth outputs
silence (you'll see a compile warning).

---

## Flash & RAM reality (important)

Mainline TinySoundFont loads an SF2 and **expands every sample to a 32-bit float
in RAM**. The RP2350 has ~520 KB, so a bank's float-expanded samples must fit:

- The **default bank, `soundfonts/Vintage_Dreams_Waves_130_sounds.sf2`** (~314 KB,
  130 GM-style sounds), is compact and **fits as-is** — no slimming needed. It's
  committed in `soundfonts/` so the packing step below runs out of the box.
- A **large** bank like `Merlin_GM_V1.2_Bank.sf2` (~28 MB) will *not* load as-is.
  Slim it in **Polyphone** (free SF2 editor): remove unused presets,
  downsample/trim samples, export. Budget roughly **≤ ~200–250 KB of sample
  data** (it ~doubles as float) so it fits alongside the firmware.
- Keep a bank General-MIDI-shaped (preset 0 = melodic default, bank 128 = drums)
  so the INST page program names line up.

The external flash module just *stores* the bank for the SF2 reader; its large
capacity isn't the bottleneck — RAM is. (An advanced memory-mapped/streaming-tsf
path is stubbed in `Sf2Flash.cpp` / `Config.h SF2_USE_MMAP` for a future
large-bank build, but mainline tsf still copies samples to float, so it doesn't
remove the RAM cost on its own.)

### Flashing the SF2 bank

1. Pack one or more banks into a flash image (order = bank index; keep
   Vintage Dreams at index 0 to match the firmware default):
   ```
   python3 tools/pack_sf2.py -o sf2_image.bin \
       VintageDreams:soundfonts/Vintage_Dreams_Waves_130_sounds.sf2
   ```
   Add ` Merlin:Merlin_slim.sf2` as a second entry once you've slimmed it.
2. Write `sf2_image.bin` to **address 0** of the external SPI flash with your
   programmer. On first boot `Sf2Flash` reads the directory and `SoundFont`
   loads `SF2_DEFAULT_BANK` (0 = Vintage Dreams). Change the default in
   `Config.h`.

---

## Control surface

The encoder is the primary editor; the keypad adds fast step entry and replaces
the old hardware buttons. **Hold the encoder switch = SHIFT.**

| Input                | Action                                                       |
|----------------------|--------------------------------------------------------------|
| Rotate encoder       | move cursor / selection                                      |
| SHIFT (hold) + rotate| change the value at the cursor                               |
| Tap encoder          | primary action (SEQ: toggle step · SONG: run the action row) |
| SHIFT + keypad A/B/C | select keypad page **STEP / NOTE / CTRL** (LEDs follow)      |

### Keypad pages (shown one-hot on the 3 LEDs)

- **STEP** (LED0): keys 1–16 toggle steps 1–16 of the current track and park the
  cursor there for encoder fine-editing.
- **NOTE** (LED1): keys play the synth live (auditioned on the track's channel;
  drums from note 36, melodic from C3).
- **CTRL** (LED2): keys act as the old hardware buttons —
  `1`=Play/Stop · `2`=Page · `3`=Track · `4`=Mute · `5`=cycle SEQ step field.
  Combine with SHIFT for the secondary action (e.g. SHIFT+`2`=previous page,
  SHIFT+`3`=previous track, SHIFT+`4`=solo).

### Pages

- **SEQ** — the step grid (hollow = empty, solid = active, halo = cursor; the
  playhead inverts the current step). The bottom line shows the cursor step, its
  note/drum name, and the selected per-step field. SHIFT+rotate edits the field;
  CTRL-page key `5` cycles **NOTE · VEL · GATE · PROB · MICRO**. Per-track length
  (1–64) gives polymeter.
- **INST** — Channel (ch 10 = drums), Program (GM name from the bank), Bank,
  Octave (−3…+3), Length.
- **MIX** — Volume (CC7), Pan (CC10), Reverb send (CC91), Chorus send (CC93).
- **FX** — global Reverb/Chorus type and Master Volume. *Note:* TinySoundFont
  has no built-in reverb/chorus, so these are stored in the song but not yet
  audible; Master Volume scales the SF2 output gain.
- **SONG** — BPM (20–300), Swing (0–75%), Resolution, Clock source (reserved),
  Slot (1–8), then **Save / Load / GM Reset**.

---

## Feature summary

- 16 tracks (track 10 → channel 10 = drums), up to 64 steps, per-track length.
- Per step: on/off, note, velocity, gate %, probability %, ±12-tick micro-timing,
  tie.
- Swing, selectable grid resolution, 20–300 BPM, drift-free 96-PPQN timing.
- Internal SF2 synthesis → PCM5100A I2S stereo out, up to `SYNTH_MAX_VOICES`
  polyphony.
- Selectable SF2 banks from the external flash module.
- 8 song slots in on-board LittleFS.

---

## File map

| File                        | Role                                                      |
|-----------------------------|-----------------------------------------------------------|
| `GM_Sequencer.ino`          | dual-core setup/loop                                      |
| `Sequencer.*`               | pattern model + transport engine (unchanged)             |
| `GMSynth.*`                 | voice API — now forwards to the SoundFont engine          |
| `SoundFont.*`               | tsf wrapper, lock-free MIDI ring, block render            |
| `tsf_impl.cpp`              | compiles TinySoundFont once (add `tsf.h`)                 |
| `AudioOut.*`                | PCM5100A I2S output (PIO + DMA) driving the renderer       |
| `Sf2Flash.*`                | external SPI flash: read SF2 bank(s)                       |
| `Storage.*`                 | song save/load (LittleFS)                                 |
| `Controls.*`                | encoder (tap/hold) + 4×4 keypad + page LEDs               |
| `UI.*`                      | SH1106 rendering + input routing                          |
| `Config.h`                  | pins, audio/synth params, limits, enums                   |
| `Platform.*`                | portability shims (engine stays board-agnostic)           |
| `tools/pack_sf2.py`         | build the SF2 flash image                                 |

---

## Bring-up notes

- `AudioOut.cpp` targets the arduino-pico **I2S** library; if you hear nothing,
  confirm BCLK/LRCLK are adjacent GPIOs and the DAC's MCLK is left unconnected.
- The SF2 reader uses 3-byte addressing for parts ≤16 MB and 4-byte for larger;
  verify your flash's read opcodes (`0x0B` / `0x0C`) if a bank won't load.
- If audio stutters, lower `SYNTH_MAX_VOICES` or raise `AUDIO_BLOCK_FRAMES` in
  `Config.h`; watch `AudioOut::underruns()`.

## Versioning

Each release lives on its own git branch so you can always roll back.
