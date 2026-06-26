> **Update — synth backend migrated to VS1053B (SPI).** The SAM2695 module was
> damaged and replaced with a VS1053B, which is driven over **SPI** (real-time
> MIDI patch + SDI streaming) instead of UART. The audit below was written for the
> SAM2695 UART path; sections 1–4 (message-byte correctness, note/event flow) still
> hold because the `GMSynth` public API and message construction are unchanged. The
> transport-layer specifics now differ: TX is SPI/SDI (not GP0 UART), `gmReset()`
> and `masterVolume()` no longer use SysEx, and clock/transport are no-ops. The
> three diagnostics in section 6 still apply — `GM_MIDI_SELFTEST`, the on-screen
> `notesSent` dot, and `MIDI_TX_DEBUG` (which now prints the boot `SCI_AUDATA`
> check, expect `0xAC45`, plus a hex trace of SDI MIDI). See README "How the
> VS1053B is brought up" for the new wiring/bring-up.

---

# MIDI Signal Audit — "no MIDI reaching the GM module"

Scope of the request: verify that the firmware is generating and transmitting the
correct MIDI messages out of **GP0 (GPIO 0 / "gio 0")**, the UART0 TX pin that
feeds the SAM2695 GM module's signal input.

Target hardware (this branch): **Raspberry Pi Pico 2 / RP2350**, arduino-pico
core ≥ 4.0.1, SSD1309 2.42" OLED, Dream SAM2695 GM module.

**Bottom line:** the firmware's MIDI generation and the GP0/UART0 setup are
correct. Every message type is byte-accurate and is routed to GP0. No firmware
defect was found that would zero out the signal, so a true "nothing on the wire"
symptom points at the connection between GP0 and the module, not the code. The
branch already has two diagnostics for this (boot self-test + on-screen activity
dot); this audit adds a third (USB byte-level monitor) so you can see the exact
bytes leaving GP0.

---

## 1. Transmit path — verified correct

| Item | Location | Result |
|------|----------|--------|
| TX pin | `Config.h` `PIN_MIDI_TX 0` → `GMSynth::begin()` `Serial1.setTX(0)` | ✅ GP0 is UART0 TX on the arduino-pico core — matches README wiring |
| Baud rate | `Serial1.begin(31250)` | ✅ Standard MIDI 31250, as the SAM2695 expects |
| Pin set order | `setTX/setRX/setFIFOSize` **before** `begin()` | ✅ Correct order for arduino-pico |
| Core ownership | `setup1()`/`loop1()` on core1 only | ✅ Only core1 touches the UART — no contention on GP0 |
| Boot traffic | `begin()` → `gmReset()` + `masterVolume()` + self-test, then full `reconcile()` | ✅ A burst of MIDI is emitted at power-up *before any button press* — a scope on GP0 shows activity immediately |

## 2. Message construction — every type is byte-accurate

Checked against MIDI 1.0 / GM:

- **Note On** `0x90|ch, note, vel` ✅
- **Note Off** `0x80|ch, note, 0x40` ✅
- **Control Change** `0xB0|ch, cc, val` ✅ (CC7 vol, CC10 pan, CC91 rev, CC93 cho, CC123/120 all-notes/sound-off)
- **Program Change** `0xC0|ch, prog` ✅
- **Pitch Bend** `0xE0|ch, LSB, MSB`, centre 8192 ✅
- **Channel encoding** `status | (ch-1)`, clamped 1..16 ✅
- **Real-time** clock `0xF8`, start `0xFA`, continue `0xFB`, stop `0xFC` ✅
- **GM System On SysEx** `F0 7E 7F 09 01 F7` ✅
- **GM Master Volume SysEx** `F0 7F 7F 04 01 00 vv F7` ✅

Data bytes are clamped to 7 bits (`clamp7`) so no value can set the high bit and
be misread as a status byte. Good.

## 3. Note/event flow — will actually fire

- `serviceEvents()` runs **offs before ons** at the same tick and fires anything
  with `tick <= curTick`, so the first note plays on the same pass. ✅
- Transport `play()` toggles start/stop; `startFromTop()` emits `0xFA`. ✅
- `audition()` (long-press preview) sends a note independent of the transport. ✅

> The default pattern has every step inactive, so a fresh boot is silent until you
> program steps or audition a note. "Press play, hear nothing" is **not** evidence
> of a signal fault — use the boot self-test to get sound with no programming.

## 4. Minor / benign observations (not the cause of silence)

- `bankSelect()` sends only CC0 (bank MSB), not CC32 (LSB). Fine for GM bank 0.
- `setReverbType`/`setChorusType` use CC80/CC81 (general-purpose controllers);
  whether they map to reverb/chorus *program* depends on the SAM2695 variant.
  They do not affect note audibility.

None of these can produce "no signal on GP0."

---

## 5. Most likely root causes (hardware), in order

Because the firmware TX is correct, a genuine *no-signal* symptom is almost
certainly one of these — check against your module's `G V S` connector:

1. **Signal pin not on GP0.** The module's **S** pin must go to **Pico GP0**. If
   it landed on GP1 you get silence — GP1 is RX, it transmits nothing.
2. **No common ground.** The module **G** pin and the Pico **GND** must share a
   ground. Without it the UART line has no reference. This is the #1 cause of
   "wired up but dead."
3. **Module power.** The **V** pin needs the module's required voltage (these
   GM 2.0 modules are typically **5 V**). The board's "Power" LED confirms V/G.
4. **TX/RX swap on a DIN-5 shield.** Drive MIDI OUT from the **TX/D1** pad and run
   the shield at **3V3** (RP2350 is not 5 V tolerant). See README.
5. **Baud / protocol mismatch.** Confirm your module variant takes raw 31250-baud
   TTL serial MIDI on the signal pin (the SAM2695 does).

---

## 6. How to confirm GP0 is alive (three diagnostics)

### A. Boot self-test — `GM_MIDI_SELFTEST` (already in this branch, currently `1`)
Plays a C-major arpeggio on channel 1 ~1 s after boot. Hear it → GP0 + wiring +
module are all good and the issue is pattern/transport. Set back to `0` once you
have sound.

### B. On-screen activity dot — `GMSynth::notesSent` (already in this branch)
The UI dot blinks each time a Note-On is *queued*. Confirms the engine is
producing notes — but it does not prove the bytes physically left the UART.

### C. USB byte-level monitor — `MIDI_TX_DEBUG` (added by this audit, default `0`)
Set `#define MIDI_TX_DEBUG 1` in `Config.h`, rebuild, open the Arduino Serial
Monitor at **115200 baud**. Every byte transmitted on GP0 is echoed as hex:
- **Bytes stream** (e.g. `F0 7E 7F 09 01 F7` at boot, `90 3C 6E …` on notes) →
  the firmware *is* transmitting on GP0; the fault is 100% on the wire/module.
- **Nothing** → core1/UART isn't running; recheck the build (board = RP2350 /
  Pico 2, arduino-pico core) and that the sketch uploaded.

Diagnostic B says "a note was generated"; C says "these exact bytes left GP0".
Together they isolate the fault to either the firmware side or the wire in
seconds. Both B and C default to non-intrusive (C is off; leave A on only while
debugging).
