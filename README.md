# Medusa SAM — 16-track MIDI groovebox for the Dream SAM2695

The **Medusa** sequencer and UI, re-targeted at a hardware **Dream SAM2695**
General-MIDI module. The baked-in SoundFont engine is gone — the RP2040 only
shuttles MIDI bytes, so the CPU is nearly idle and the whole of core1 is spent
on timing. That is the point: the groove is rock solid.

- **16 tracks**, each with its own MIDI channel, instrument and length
- **16–64 steps per track** (any length 1–64; polymeter between tracks)
- **8 patterns**, chainable into a song (up to 16 chain entries)
- Per step: **note, velocity, gate, probability, micro-timing, ratchet (1–4),
  accent, tie/slide**
- Per pattern: **bar length + swing**; global **scale lock** (7 scales)
- **Every SAM2695 parameter** — including the hidden NRPN / GS SysEx set — is
  reachable from the front panel (see the parameter reference below)
- **Dual-core**: core0 = UI, core1 = a drift-free 96-PPQN engine that owns the
  MIDI UART
- 8 song slots on the Pico's flash (LittleFS)
- A routing hook + reserved I2S pins for a future onboard ("baked")
  synth/SoundFont layer

The firmware lives in [`MedusaSAM/`](MedusaSAM/) — open `MedusaSAM.ino` in the
Arduino IDE. (The previous-generation sequencer this repo used to hold is on
the `main` branch.)

---

## Hardware

| Part | Notes |
|------|-------|
| Raspberry Pi Pico (RP2040) | any RP2040 board with GP0–GP13 free |
| Dream SAM2695 GM module | the AliExpress "MIDI digital music module, GM 2.0, 128 tones" (Nulllab etc.) with speaker/phones out |
| 1.3" ST7567S COG LCD, 4-pin I2C (EstarDyn module) | the OLED-style encoder combo panel works perfectly |
| EC11 rotary encoder w/ push | often on the same panel as the OLED |
| 5× momentary buttons | PLAY, SHIFT, PAGE, TRACK, REC |
| *(optional)* DIN-5 MIDI shield | for driving other gear from the same MIDI stream |

## Wiring (the Medusa pinout)

This is the **same front-panel map as Medusa GM** — the OLED, encoder and all
five buttons land on the same GPIOs, so one panel build drives either
firmware. All buttons and the encoder are **active-low**: wire their common
side to **GND** (internal pull-ups are enabled in firmware).

| Signal | Pico pin | Notes |
|--------|----------|-------|
| MIDI TX → SAM2695 | **GP0** | to the module's MIDI-IN / RX pad (3.3 V TTL, direct) |
| MIDI RX (future) | GP1 | reserved for external clock sync — never above 3.3 V |
| LCD SDA | GP4 | I2C0 |
| LCD SCL | GP5 | I2C0 @ 400 kHz |
| Encoder A / B / push | GP6 / GP7 / GP8 | |
| PLAY | GP9 | |
| SHIFT | GP10 | modifier (hold) |
| PAGE | GP11 | |
| TRACK | GP12 | |
| REC | GP13 | context button (accent / edit / audition / execute) |
| click-out (future) | GP14 | reserved |
| I2S BCLK/LRCLK/DIN (future) | GP15/16/17 | reserved for the baked-synth layer's DAC |

### The SAM2695 module (3-pin PH2.0 "MIDI" connector)

| Module pin | Connect to |
|------------|------------|
| MIDI (RX)  | Pico **GP0** |
| VCC        | 5 V (VBUS) or 3V3 per your module's spec |
| GND        | Pico GND (common ground is essential) |

Audio comes straight off the module's speaker/phones output.

### Optional DIN-5 MIDI shield

Wire by function, not silkscreen: Pico **GP0 → the shield's "1 / TX" pad**
(drives MIDI OUT), GP1 → "0 / RX" (future sync-in), and **power the shield
from 3V3** — the RP2040 is not 5 V-tolerant, and the shield's opto output
idles at its VCC.

### The ST7567S display (EstarDyn module)

Same 4-pin I2C wiring as any OLED module (VCC/GND/SDA/SCL), but two things
are different from a plain SH1106/SSD1306 panel:

- **I2C address is `0x3F`**, not `0x3C` — the EstarDyn board pulls SA0 high.
  If your module has SA0 low, change `OLED_ADDR` in `config.h` to `0x3C`.
- **Contrast needs to be set explicitly** (`OLED_CONTRAST` in `config.h`,
  default 160, a confirmed-working value) — the ST7567S resets to a much
  dimmer level than an OLED and reads as a blank screen until contrast is
  turned up. Tune by eye in the 150–220 range if your panel looks too light
  or too dark.

The COG glass on this panel also clips a few pixels at the left edge and
garbles the rightmost columns, so the firmware confines all drawing to a
safe zone (`SCREEN_L`/`SCREEN_R` in `config.h`, default columns 5–120)
instead of the full 0–127 canvas.

## Building (Arduino IDE)

1. Install the **arduino-pico** core (Earle Philhower). Boards Manager URL:
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
2. Install **U8g2** from the Library Manager.
3. Board: your Raspberry Pi Pico. **Flash Size: pick a layout with a
   filesystem**, e.g. *2MB (Sketch 1MB / FS 1MB)* — a song slot is ~66 KB and
   there are 8 of them.
4. Open `MedusaSAM/MedusaSAM.ino` and upload.

On boot it loads a default groove (kick / snare / hats / bass / piano) —
press **PLAY**.

---

## UI manual

`SHIFT` = hold the SHIFT button. The scheme is the Medusa one:

| Input | Action |
|-------|--------|
| PLAY | play / stop |
| SHIFT + PLAY | continue from where you stopped |
| PAGE / SHIFT+PAGE | next / previous view |
| TRACK / SHIFT+TRACK | next / previous track |
| TRACK (hold) | clear the current track's row |
| SHIFT + rotate | tempo, from any view |
| rotate | move cursor / edit the selected field |
| encoder push | toggle step (STEP) / next field (lists) |
| encoder long-press | clear the current track's row (STEP) |
| REC | context: tap = accent, hold+rotate = edit, lists = audition / run `[action]` rows |

Views (PAGE cycles): **STEP → INST → SHAPE → MIX → FX → MASTR → XPRT → SONG**

- **STEP** — the grid, one 16-step window at a time (the cursor pages through
  windows automatically on longer tracks; `2/4` in the corner = window 2 of 4).
  Encoder = cursor, push = toggle. **SHIFT + push cycles the step field**
  (NOTE · VEL · GATE · PROB · MICRO · RATCHET); **hold REC + rotate edits it**.
  REC tap = accent, SHIFT+REC = tie. Ties on melodic tracks hold or legato-slide
  into the next step (with SHAPE→Porta > 0 the SAM glides); ratchet subdivides
  the step into 2–4 hits.
- **INST** — per track: Channel (1–16, 10 = drums), Mode (Auto/Melodic/**Drums
  on any channel** via GS), Program or drum Kit, Bank (GM/MT-32), Octave,
  Volume, Pan, Reverb & Chorus sends, Bend range, Out (SAM / future Layer),
  Mute. REC auditions.
- **SHAPE** — per track sound shaping, sent live as SAM2695 NRPNs: track
  Length, Portamento, Cutoff, Resonance, Attack, Decay, Release, Vibrato
  rate/depth/delay, Mod wheel. Values marked `*` are at the preset default (64).
- **MIX** — 16 volume bars: rotate = volume, push = mute, SHIFT+push = solo,
  REC = audition.
- **FX** — the global effects engine: Reverb type/level/time/feedback/pre-LPF,
  Chorus type/level/rate/depth/feedback/delay/→reverb/pre-LPF.
- **MASTR** — Master volume, Key shift (global transpose in the chip), Fine
  tune (cents), Master pan, the **4-band EQ** (gain and frequency per band),
  plus `[GM Reset]` `[GS Reset]` `[Panic]` `[Resend]` action rows (REC runs them).
- **XPRT** — the expert console: send **any CC, any NRPN (MSB/LSB/value), any
  GS SysEx parameter (address + value, checksum added for you)** to the chip.
  Anything in the SAM2695 datasheet that doesn't have a dedicated field —
  per-drum-note pitch/level/pan/reverb/chorus, reverb character, spatial
  effect, and so on — is reachable from here.
- **SONG** — Pattern (switches are **queued to the bar** while playing),
  Bar length, Swing, Resolution (1–8 steps/beat), MIDI Clock out on/off,
  Scale root + type, Chain length and the 16 chain slots, save Slot, and
  `[Save]` `[Load]` `[Copy Pn]` `[Clear pat]` action rows.

## SAM2695 parameter reference (what the firmware sends)

**Dedicated UI fields:**

| Parameter | MIDI message |
|-----------|--------------|
| Program / drum kit, Bank | CC0 + Program Change |
| Volume / Pan / Expression | CC7 / CC10 / CC11 |
| Reverb / Chorus send | CC91 / CC93 |
| Mod wheel | CC1 |
| Portamento | CC65 + CC5 |
| Pitch-bend range | RPN 00 00 |
| Vibrato rate / depth / delay | NRPN 01 08 / 01 09 / 01 0A |
| TVF cutoff / resonance | NRPN 01 20 / 01 21 |
| Envelope attack / decay / release | NRPN 01 63 / 01 64 / 01 66 |
| Reverb program / Chorus program | CC80 / CC81 (Dream-specific) |
| Reverb level / time / feedback / pre-LPF | GS SysEx 40 01 33 / 34 / 35 / 32 |
| Chorus level / fb / delay / rate / depth / →rev / pre-LPF | GS 40 01 3A…3F / 39 |
| 4-band EQ gains / frequencies | Dream NRPN 37 00–03 / 37 08–0B |
| Master volume | Universal SysEx F0 7F 7F 04 01 00 vv F7 |
| Master key shift / pan / fine tune | GS 40 00 05 / 06 / 00 |
| Drums on any channel | GS "use for rhythm part" 40 1x 15 |
| GM / GS reset | F0 7E 7F 09 01 F7 / GS reset |

**Via the XPRT console** (documented in the SAM2695 datasheet):
per-drum-note NRPNs (18 nn pitch, 1A nn level, 1C nn pan, 1D nn reverb,
1E nn chorus), reverb character (GS 40 01 31), channel fine/coarse tune
(RPN 00 01/02), the spatial effect and the rest of the Dream NRPN 37xx block.

> Note: the CC/RPN/GS-SysEx set above is standard and safe. The Dream-specific
> EQ NRPN numbers (37xx) follow the SAM2695 datasheet / common driver practice;
> if a band doesn't respond on your module revision, the XPRT page lets you
> probe the exact numbers straight from the datasheet without reflashing.

---

## Architecture

```
MedusaSAM/
  MedusaSAM.ino   core0: setup/loop (UI)   core1: setup1/loop1 (engine)
  config.h        pins + dimensions + timing constants
  model.h/.cpp    Song / Pattern / TrackCfg / Step + scales + default song
  sam2695.h/.cpp  the complete SAM2695 MIDI driver (see reference above)
  engine.h/.cpp   core1: 96-PPQN transport, event scheduler, reconcile pass
  sound_source.h  note routing: SAM today, the baked-synth layer tomorrow
  controls.h/.cpp encoder (ISR quadrature) + debounced buttons
  ui.h/.cpp       ST7567S views + input handling
  storage.h/.cpp  LittleFS song save/load
  gm_names.h      GM instrument / drum / kit / FX name tables (PROGMEM)
```

**Timing.** core1 runs a 96-PPQN tick from a drift-free integer accumulator
(no floats, immune to rounding drift at any BPM). Steps schedule note on/off
*events* with swing, ±12-tick micro offsets, gate %, probability and ratchet
subdivision; offs fire before ons on the same tick so retriggers are clean.
Ties keep a per-track held note and hand it over ON-before-OFF, so mono/porta
patches glide (303-style) instead of gapping. MIDI clock (0xF8, 24 PPQN) plus
Start/Stop/Continue go out with the notes — external gear follows the groove.

**Cross-core rules.** core0 edits the Song and raises volatile request flags;
core1 diff-reconciles the Song against a shadow every pass and sends only what
changed, budgeted against the UART FIFO so a big edit can never stall a tick.
All shared fields are single aligned 8/16-bit scalars — atomic on the M0+, no
locks anywhere. Song save/load parks the engine (handshake), then locks core1
out of XIP flash for the write.

## The future baked-synth layer

This firmware is deliberately light on the RP2040 so a second sound source can
live beside the SAM2695 later — the Medusa GM sample engine rendered over I2S
on the reserved GP15/16/17 pins. The seam is already cut:

- every note event routes through `sound_source.h` by `TrackCfg::dest`
- the INST page already has the per-track **Out: SAM / Layer** selector
  (Layer is silent until the engine lands)
- events arrive at the router *after* swing/micro/ratchet, so the layer will
  groove identically to the SAM

Adding the layer = implementing the three hooks in `sound_source.h` and
spending core1's idle time (currently ~99%) rendering audio.
