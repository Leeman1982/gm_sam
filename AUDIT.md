# MIDI Signal Audit — "no MIDI reaching the GM module"

Scope of the request: verify that the firmware is generating and transmitting the
correct MIDI messages out of **GP0 (GPIO 0 / "gio 0")**, the UART0 TX pin that
feeds the SAM2695 GM module's signal input.

**Bottom line:** the firmware's MIDI generation and the GP0/UART0 setup are
correct. Every message type is byte-accurate and is routed to GP0. No firmware
defect was found that would zero out the signal, so a true "nothing on the wire"
symptom points at the connection between GP0 and the module, not at the code.
Opt-in diagnostics were added so you can prove this in 30 seconds.

---

## 1. Transmit path — verified correct

| Item | Location | Result |
|------|----------|--------|
| TX pin | `Config.h` `PIN_MIDI_TX 0` → `GMSynth::begin()` `Serial1.setTX(0)` | ✅ GP0 is UART0 TX on the arduino-pico core — matches the README wiring table |
| Baud rate | `Serial1.begin(31250)` | ✅ Standard MIDI 31250, as the SAM2695 expects |
| Pin set order | `setTX/setRX/setFIFOSize` **before** `begin()` | ✅ Required order for arduino-pico; correct |
| Core ownership | `setup1()`/`loop1()` on core1 only | ✅ Only core1 touches the UART — no cross-core contention on GP0 |
| Boot traffic | `begin()` → `gmReset()` + `masterVolume()`, then `engineBegin()` sets `reqResendAll` → full `reconcile()` | ✅ A burst of MIDI is emitted at power-up *before any button press* — a scope on GP0 should show activity immediately |

## 2. Message construction — every type is byte-accurate

Checked against the MIDI 1.0 / GM spec:

- **Note On** `0x90|ch, note, vel` ✅
- **Note Off** `0x80|ch, note, 0x40` ✅
- **Control Change** `0xB0|ch, cc, val` ✅ (CC7 vol, CC10 pan, CC91 rev, CC93 cho, CC123/120 all-notes/sound-off)
- **Program Change** `0xC0|ch, prog` ✅
- **Pitch Bend** `0xE0|ch, LSB, MSB`, centre 8192 ✅
- **Channel encoding** `status | (ch-1)`, clamped 1..16 ✅ (1-based tracks → 0-based wire nibble)
- **Real-time** clock `0xF8`, start `0xFA`, continue `0xFB`, stop `0xFC` ✅
- **GM System On SysEx** `F0 7E 7F 09 01 F7` ✅
- **GM Master Volume SysEx** `F0 7F 7F 04 01 00 vv F7` ✅ (coarse value in the MSB byte)

Data bytes are clamped to 7 bits (`clamp7`) so no value can accidentally set the
high bit and be misread as a status byte. Good.

## 3. Note/event flow — will actually fire

- `triggerStep()` schedules `EV_ON`/`EV_OFF`; `serviceEvents()` runs **offs before
  ons** at the same tick and fires anything with `tick <= curTick`, so the first
  note at `gTick==0` plays on the same pass. ✅
- Transport: `play()` toggles `reqStart`/`reqStop`; `startFromTop()` emits `0xFA`
  and begins ticking. ✅
- `audition()` (long-press preview) sends a note independent of the transport. ✅

> Note: the **default pattern has every step inactive** (`sp.active = 0`). On a
> fresh boot nothing plays until you program steps or audition a note. That is
> expected behaviour, not a fault — but it means "press play and hear nothing"
> is not evidence of a signal problem. Use the self-test below to get sound with
> no programming.

## 4. Minor / benign observations (not the cause of silence)

- `bankSelect()` sends only CC0 (bank MSB) without CC32 (LSB). Fine for GM bank 0
  on the SAM2695; noted for completeness.
- `setReverbType`/`setChorusType` use CC80/CC81. These are "general purpose"
  controllers in core MIDI; whether they map to reverb/chorus *program* depends on
  the SAM2695 firmware variant. They do not affect note audibility.
- `engineService()` does a `delay(5)` on core1 during an explicit GM-reset request.
  Harmless (only on a manual reset), but it briefly stalls the real-time loop.

None of these can produce "no signal on GP0."

---

## 5. Most likely root causes (hardware), in order

Because the firmware TX is correct, a genuine *no-signal* symptom is almost
certainly one of these — check against the photo of your module:

1. **Signal pin not on GP0.** The module's **S** pin (the "S" of the `G V S`
   silkscreen) must go to **Pico GP0**. If it landed on GP1 you get silence —
   GP1 is RX (reserved for future clock-in), it transmits nothing.
2. **No common ground.** The module **G** pin and the Pico **GND** must share a
   ground. Without it, the UART line has no reference and the module sees nothing.
   This is the #1 cause of "wired up but dead."
3. **Module power.** The **V** pin must get the voltage the module wants (these
   GM 2.0 modules are typically **5 V**). The "Power" LED on the board lighting up
   confirms V/G are good — but verify it's actually lit and at the right rail.
4. **TX/RX swap on a DIN-5 MIDI shield.** If you're going through an Arduino MIDI
   shield, drive MIDI OUT from the **TX/D1** pad and keep the shield on **3V3**
   (the RP2040 is not 5 V tolerant). See README "Using an Arduino-style MIDI
   shield."
5. **Baud / protocol mismatch.** Confirm your specific module variant takes raw
   31250-baud TTL serial MIDI on the signal pin (the SAM2695 does). A few clones
   expect a different rate or USB-MIDI only.

---

## 6. How to confirm GP0 is alive (added diagnostics, default OFF)

Two switches were added in `Config.h`. They change nothing when left at `0`.

### `#define MIDI_SELFTEST 1`
Plays a C-major arpeggio on channel 1 (grand piano) ~1 s after boot, before any
input. If you hear it → GP0, wiring, and the module are all good and the issue is
purely that no pattern was programmed. If you hear nothing → the fault is the
GP0↔module link (section 5).

### `#define MIDI_TX_DEBUG 1`
Mirrors **every byte sent on GP0** to the USB serial port as hex. Open the Arduino
Serial Monitor at **115200 baud**:
- **You see bytes streaming** (e.g. `F0 7E 7F 09 01 F7` at boot, `90 3C 6E …` on
  notes) → the firmware *is* transmitting correctly on GP0; the problem is 100% on
  the wire/module side.
- **You see nothing** → core1/UART isn't running as expected; recheck the build
  (board = RP2040, arduino-pico core) and that the sketch actually uploaded.

Enable one or both, rebuild, and you'll know within seconds which side of the GP0
pin the fault is on.
