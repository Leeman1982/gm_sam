// ============================================================================
//  GMSynth.h  -  Dream SAM2695 serial-MIDI driver (full feature surface)
//
//  Thin, allocation-free wrapper over Serial1 (UART0 @ 31250 baud).
//  IMPORTANT: only ONE core may call these functions. In this project that is
//  core1 (the sequencer engine). core0 never touches the UART.
//
//  Beyond plain channel voice messages this exposes the SAM2695's GS-style
//  sound controllers:
//    * NRPN 01xx : vibrato rate/depth/delay, TVF cutoff/resonance,
//                  envelope attack/decay/release       (value 64 = neutral)
//    * RPN 0000  : pitch-bend range in semitones
//    * GS SysEx  : master reverb level/time, master chorus level/rate/depth
//                  (Roland address map 40 01 3x, checksummed)
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

namespace GMSynth {

  // Bring up Serial1 on the configured pins and reset the SAM2695 to GM mode.
  void begin();

  // ---- Channel voice messages (channel is 1-based, 1..16) ----
  void noteOn (uint8_t ch, uint8_t note, uint8_t vel);
  void noteOff(uint8_t ch, uint8_t note);
  void controlChange(uint8_t ch, uint8_t cc, uint8_t value);
  void programChange(uint8_t ch, uint8_t program);
  void pitchBend(uint8_t ch, int16_t bend14);     // -8192..+8191, 0 = centre

  // ---- Basic per-channel controllers ----
  void bankSelect(uint8_t ch, uint8_t bankMSB);   // 0 = GM, 127 = MT-32
  void setVolume(uint8_t ch, uint8_t v);          // CC7
  void setPan(uint8_t ch, uint8_t v);             // CC10 (64 = centre)
  void setExpression(uint8_t ch, uint8_t v);      // CC11
  void setModulation(uint8_t ch, uint8_t v);      // CC1
  void setSustain(uint8_t ch, uint8_t on);        // CC64 (0/127)
  void setPortamento(uint8_t ch, uint8_t on);     // CC65 (0/127)
  void setPortaTime(uint8_t ch, uint8_t v);       // CC5
  void setReverbSend(uint8_t ch, uint8_t v);      // CC91
  void setChorusSend(uint8_t ch, uint8_t v);      // CC93
  void setReverbType(uint8_t ch, uint8_t t);      // CC80, 0..7
  void setChorusType(uint8_t ch, uint8_t t);      // CC81, 0..7

  // ---- RPN / NRPN sound controllers (value 64 = neutral for NRPNs) ----
  void rpn (uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t value);
  void nrpn(uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t value);
  void setBendRange(uint8_t ch, uint8_t semitones);   // RPN 00 00
  void setVibratoRate (uint8_t ch, uint8_t v);        // NRPN 01 08
  void setVibratoDepth(uint8_t ch, uint8_t v);        // NRPN 01 09
  void setVibratoDelay(uint8_t ch, uint8_t v);        // NRPN 01 0A
  void setCutoff      (uint8_t ch, uint8_t v);        // NRPN 01 20 (TVF cutoff)
  void setResonance   (uint8_t ch, uint8_t v);        // NRPN 01 21 (TVF reso)
  void setAttack      (uint8_t ch, uint8_t v);        // NRPN 01 63
  void setDecay       (uint8_t ch, uint8_t v);        // NRPN 01 64
  void setRelease     (uint8_t ch, uint8_t v);        // NRPN 01 66

  // ---- GS SysEx global effect masters (Roland addr 40 01 3x, checksummed) ----
  void gsParam(uint8_t a1, uint8_t a2, uint8_t a3, uint8_t v);
  void setMasterReverbLevel(uint8_t v);   // 40 01 33
  void setMasterReverbTime (uint8_t v);   // 40 01 34
  void setMasterChorusLevel(uint8_t v);   // 40 01 3A
  void setMasterChorusRate (uint8_t v);   // 40 01 3B
  void setMasterChorusDepth(uint8_t v);   // 40 01 3C

  // ---- Real-time / system ----
  void clockTick();        // 0xF8
  void start();            // 0xFA
  void stop();             // 0xFC
  void cont();             // 0xFB (continue)

  void allNotesOff(uint8_t ch);   // CC123
  void allSoundOff(uint8_t ch);   // CC120
  void panic();                   // notes+sound off on all 16 channels

  void gmReset();                          // GM-On SysEx (F0 7E 7F 09 01 F7)
  void masterVolume(uint8_t v);            // GM master volume SysEx (0..127)

} // namespace GMSynth
