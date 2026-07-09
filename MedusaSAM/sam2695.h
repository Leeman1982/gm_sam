#pragma once
// ============================================================================
//  Medusa SAM  --  sam2695.h
//  Complete serial-MIDI driver for the Dream SAM2695 GM/GS synthesizer.
//
//  The SAM2695 is a 64-voice GM/GS/MT-32 synth-on-a-chip with reverb, chorus,
//  a 4-band equalizer and a spatial effect.  Beyond plain GM it listens to:
//    * CC80 / CC81            -- reverb / chorus program select (Dream quirk)
//    * RPN  00.00/00.01/00.02 -- pitch-bend range, fine tune, coarse tune
//    * GS NRPN 01.xx          -- vibrato, TVF cutoff/resonance, envelope
//    * GS NRPN 18/1A/1C/1D/1E -- per-drum-note pitch/level/pan/reverb/chorus
//    * Dream NRPN 37.xx       -- the 4-band equalizer (gains + frequencies)
//    * GS SysEx 40 01 3x      -- reverb & chorus detail parameters
//    * GS SysEx 40 00 0x      -- master tune / volume / key-shift / pan
//    * GS SysEx 40 1x 15      -- "use for rhythm part" (drums on ANY channel)
//  Everything above is exposed by this driver; anything else in the datasheet
//  is reachable through the raw cc()/nrpn()/gsParam()/sysex() primitives that
//  back the UI's XPRT (expert) page.
//
//  THREADING: only core1 (the engine) may call these functions -- the UART
//  FIFO is not cross-core safe.  core0 asks for sends via engine flags.
// ============================================================================
#include <Arduino.h>
#include "config.h"

namespace SAM {

  void begin();                      // UART up + GM reset
  int  txFree();                     // bytes free in the TX FIFO (for budgeting)

  // ---- channel voice messages (ch is 1-based, 1..16) ----
  void noteOn (uint8_t ch, uint8_t note, uint8_t vel);
  void noteOff(uint8_t ch, uint8_t note);
  void cc(uint8_t ch, uint8_t num, uint8_t val);
  void programChange(uint8_t ch, uint8_t program);
  void pitchBend(uint8_t ch, int16_t bend14);      // -8192..+8191
  void channelPressure(uint8_t ch, uint8_t v);

  // ---- standard controllers ----
  void bankSelect(uint8_t ch, uint8_t bankMSB);    // CC0: 0 = GM, 127 = MT-32
  void setVolume(uint8_t ch, uint8_t v);           // CC7
  void setPan(uint8_t ch, uint8_t v);              // CC10
  void setExpression(uint8_t ch, uint8_t v);       // CC11
  void setModWheel(uint8_t ch, uint8_t v);         // CC1
  void setSustain(uint8_t ch, bool on);            // CC64
  void setPortamento(uint8_t ch, bool on);         // CC65
  void setPortamentoTime(uint8_t ch, uint8_t v);   // CC5
  void setReverbSend(uint8_t ch, uint8_t v);       // CC91
  void setChorusSend(uint8_t ch, uint8_t v);       // CC93
  void setReverbProgram(uint8_t t);                // CC80 (global, 0..7)
  void setChorusProgram(uint8_t t);                // CC81 (global, 0..7)

  // ---- RPN ----
  void rpn(uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t val);
  void setBendRange(uint8_t ch, uint8_t semis);    // RPN 00 00
  void setChannelFineTune(uint8_t ch, uint8_t v);  // RPN 00 01 (64 = centre)
  void setChannelCoarseTune(uint8_t ch, uint8_t v);// RPN 00 02 (64 = centre)

  // ---- NRPN (GS part parameters; 64 = preset default) ----
  void nrpn(uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t val);
  void setVibratoRate (uint8_t ch, uint8_t v);     // NRPN 01 08
  void setVibratoDepth(uint8_t ch, uint8_t v);     // NRPN 01 09
  void setVibratoDelay(uint8_t ch, uint8_t v);     // NRPN 01 0A
  void setCutoff      (uint8_t ch, uint8_t v);     // NRPN 01 20
  void setResonance   (uint8_t ch, uint8_t v);     // NRPN 01 21
  void setEnvAttack   (uint8_t ch, uint8_t v);     // NRPN 01 63
  void setEnvDecay    (uint8_t ch, uint8_t v);     // NRPN 01 64
  void setEnvRelease  (uint8_t ch, uint8_t v);     // NRPN 01 66

  // ---- NRPN (per-drum-note, on the rhythm channel) ----
  void setDrumPitch (uint8_t note, uint8_t v);     // NRPN 18 nn
  void setDrumLevel (uint8_t note, uint8_t v);     // NRPN 1A nn
  void setDrumPan   (uint8_t note, uint8_t v);     // NRPN 1C nn (0 = random)
  void setDrumReverb(uint8_t note, uint8_t v);     // NRPN 1D nn
  void setDrumChorus(uint8_t note, uint8_t v);     // NRPN 1E nn

  // ---- Dream-specific NRPN 37xx: the 4-band equalizer ----
  void setEqGain(uint8_t band, uint8_t v);         // band 0..3, NRPN 37 00..03
  void setEqFreq(uint8_t band, uint8_t v);         // band 0..3, NRPN 37 08..0B

  // ---- GS SysEx (address 40 xx xx with Roland checksum) ----
  void gsParam(uint8_t a1, uint8_t a2, uint8_t a3, uint8_t val);
  void gsParam2(uint8_t a1, uint8_t a2, uint8_t a3,
                const uint8_t* data, uint8_t n);   // multi-byte data
  void setReverbCharacter(uint8_t v);              // 40 01 31
  void setReverbPreLpf(uint8_t v);                 // 40 01 32 (0..7)
  void setReverbLevel(uint8_t v);                  // 40 01 33
  void setReverbTime(uint8_t v);                   // 40 01 34
  void setReverbDelayFeedback(uint8_t v);          // 40 01 35
  void setChorusPreLpf(uint8_t v);                 // 40 01 39 (0..7)
  void setChorusLevel(uint8_t v);                  // 40 01 3A
  void setChorusFeedback(uint8_t v);               // 40 01 3B
  void setChorusDelay(uint8_t v);                  // 40 01 3C
  void setChorusRate(uint8_t v);                   // 40 01 3D
  void setChorusDepth(uint8_t v);                  // 40 01 3E
  void setChorusToReverb(uint8_t v);               // 40 01 3F
  void setMasterTuneCents(int cents);              // 40 00 00, -100..+100
  void setMasterKeyShift(int8_t semis);            // 40 00 05, -24..+24
  void setMasterPanGS(uint8_t v);                  // 40 00 06
  void setRhythmPart(uint8_t ch, uint8_t mode);    // 40 1x 15: 0=melodic 1/2=drums

  // ---- universal / real-time system ----
  void masterVolume(uint8_t v);                    // F0 7F 7F 04 01 00 vv F7
  void gmReset();                                  // F0 7E 7F 09 01 F7
  void gm2On();                                    // F0 7E 7F 09 03 F7
  void gsReset();                                  // F0 41 00 42 12 40 00 7F 00 41 F7
  void sysex(const uint8_t* buf, uint16_t n);      // raw passthrough (XPRT page)

  void clockTick();                                // 0xF8
  void start();                                    // 0xFA
  void cont();                                     // 0xFB
  void stop();                                     // 0xFC

  void allNotesOff(uint8_t ch);                    // CC123
  void allSoundOff(uint8_t ch);                    // CC120
  void resetAllControllers(uint8_t ch);            // CC121
  void panic();                                    // silence all 16 channels

} // namespace SAM
