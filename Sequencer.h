// ============================================================================
//  Sequencer.h  -  Pattern data model + dual-core playback engine
//
//  THREADING MODEL
//    core0 (UI)     : edits `data` via clamped setters, reads `data` for display,
//                     and raises volatile request flags (start/stop/panic/...).
//    core1 (engine) : owns timing + the UART. Calls engineBegin() once then
//                     engineService() in a tight loop. Reconciles `data` changes
//                     into MIDI messages and drives the transport.
//
//  All shared reads/writes are single 8/16-bit aligned scalars, which are
//  atomic on the Cortex-M0+, so the audio-rate path never has to take a lock.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

// ---- POD pattern data (safe to memcpy for save/load) -----------------------
struct Step {
  uint8_t active;   // 0/1
  uint8_t note;     // 0..127 (drum note on ch10)
  uint8_t vel;      // 1..127
  uint8_t gate;     // 1..100  (% of step length)
  uint8_t prob;     // 0..100  (% chance to fire)
  uint8_t ratchet;  // 1..MAX_RATCHET retriggers within the step
  int8_t  micro;    // -12..+12 ticks of micro-timing
  uint8_t tie;      // 0/1  (hold across the step boundary)
};

struct Track {
  // sound
  uint8_t channel;    // 1..16
  uint8_t program;    // 0..127 GM program
  uint8_t bankMSB;    // 0 = GM, 127 = MT-32
  int8_t  octave;     // -3..+3 transpose (octaves)
  int8_t  transpose;  // -12..+12 semitones
  // playback
  uint8_t length;     // 1..MAX_STEPS
  uint8_t direction;  // TrackDir
  uint8_t clkDiv;     // index into kClockDivOptions
  uint8_t humVel;     // 0..32   random velocity spread (+/-)
  uint8_t humTime;    // 0..12   random tick offset (0..n, late only)
  // mixer
  uint8_t vol;        // CC7   0..127
  uint8_t pan;        // CC10  0..127 (64 centre)
  uint8_t revSend;    // CC91  0..127
  uint8_t choSend;    // CC93  0..127
  uint8_t expression; // CC11  0..127
  uint8_t modulation; // CC1   0..127
  // synth voice (NRPN, 64 = neutral)
  uint8_t cutoff;     // NRPN 01 20
  uint8_t resonance;  // NRPN 01 21
  uint8_t attack;     // NRPN 01 63
  uint8_t decay;      // NRPN 01 64
  uint8_t release;    // NRPN 01 66
  uint8_t vibRate;    // NRPN 01 08
  uint8_t vibDepth;   // NRPN 01 09
  uint8_t vibDelay;   // NRPN 01 0A
  uint8_t bendRange;  // RPN 00 00, semitones 0..24
  uint8_t portaOn;    // CC65 0/1
  uint8_t portaTime;  // CC5  0..127
  uint8_t sustain;    // CC64 0/1
  // state
  uint8_t mute;       // 0/1
  uint8_t solo;       // 0/1
  uint8_t stepField;  // current StepField being edited on SEQ page
  uint8_t curStep;    // engine-written playhead (display only)
  Step    steps[MAX_STEPS];
};

struct Song {
  uint16_t bpm;        // 20..300
  uint8_t  swing;      // 0..75 (%)
  uint8_t  spbIndex;   // index into kStepsPerBeatOptions
  uint8_t  clockSrc;   // ClockSource
  // scale lock
  uint8_t  scaleRoot;  // 0..11 (C..B)
  uint8_t  scaleType;  // index into kScales
  uint8_t  scaleLock;  // ScaleLockMode
  // global FX
  uint8_t  revType;    // 0..7 reverb program (CC80, all channels)
  uint8_t  revLevel;   // GS master reverb level  (40 01 33)
  uint8_t  revTime;    // GS master reverb time   (40 01 34)
  uint8_t  choType;    // 0..7 chorus program (CC81, all channels)
  uint8_t  choLevel;   // GS master chorus level  (40 01 3A)
  uint8_t  choRate;    // GS master chorus rate   (40 01 3B)
  uint8_t  choDepth;   // GS master chorus depth  (40 01 3C)
  uint8_t  masterVol;  // 0..127 (GM master volume SysEx)
  Track    tracks[MAX_TRACKS];
};

class Sequencer {
public:
  Song data;

  // ---- volatile cross-core request flags (set by core0, cleared by core1) ----
  volatile uint8_t reqStart     = 0;   // start from the top
  volatile uint8_t reqContinue  = 0;   // resume from current position
  volatile uint8_t reqStop      = 0;
  volatile uint8_t reqPanic     = 0;
  volatile uint8_t reqGmReset   = 0;   // re-send GM reset + all settings
  volatile uint8_t reqResendAll = 0;   // force full reconcile (after load)
  volatile uint8_t isRunning    = 0;   // engine -> UI transport state

  // audition (preview a note from the UI)
  volatile uint8_t audReq  = 0;
  volatile uint8_t audCh   = 1;
  volatile uint8_t audNote = 60;
  volatile uint8_t audVel  = 100;

  // ---- lifecycle ----
  void initDefaultSong();

  // ---- core1 engine ----
  void engineBegin();      // call once on core1 (after GMSynth::begin)
  void engineService();    // call repeatedly on core1

  // ---- core0 helpers (clamped, atomic) ----
  bool running() const { return isRunning; }
  uint8_t stepsPerBeat() const { return kStepsPerBeatOptions[data.spbIndex % NUM_SPB_OPTIONS]; }
  uint16_t ticksPerStep() const { return PPQN / stepsPerBeat(); }
  bool trackAudible(uint8_t t) const;
  bool anySolo() const;
  bool isDrumTrack(uint8_t t) const { return data.tracks[t].channel == DRUM_CHANNEL; }
  uint16_t scaleMask() const;

  // transport requests
  void play()     { if (isRunning) reqStop = 1; else reqStart = 1; }
  void stop()     { reqStop = 1; }
  void startTop() { reqStart = 1; }
  void cont()     { reqContinue = 1; }
  void panic()    { reqPanic = 1; }
  void resendAll(){ reqResendAll = 1; }
  void gmReset()  { reqGmReset = 1; }
  void audition(uint8_t t, uint8_t note);

  // step edits
  void toggleStep(uint8_t t, uint8_t s);
  void editStepField(uint8_t t, uint8_t s, uint8_t field, int delta);

  // track edits
  void setProgram(uint8_t t, int d);
  void setBank(uint8_t t);                 // toggle GM <-> MT-32
  void setChannel(uint8_t t, int d);
  void setOctave(uint8_t t, int d);
  void setTranspose(uint8_t t, int d);
  void setLength(uint8_t t, int d);
  void setDirection(uint8_t t, int d);
  void setClkDiv(uint8_t t, int d);
  void setHumVel(uint8_t t, int d);
  void setHumTime(uint8_t t, int d);
  void setVol(uint8_t t, int d);
  void setPan(uint8_t t, int d);
  void setRevSend(uint8_t t, int d);
  void setChoSend(uint8_t t, int d);
  void setExpression(uint8_t t, int d);
  void setModulation(uint8_t t, int d);
  void setCutoff(uint8_t t, int d);
  void setResonance(uint8_t t, int d);
  void setAttack(uint8_t t, int d);
  void setDecay(uint8_t t, int d);
  void setRelease(uint8_t t, int d);
  void setVibRate(uint8_t t, int d);
  void setVibDepth(uint8_t t, int d);
  void setVibDelay(uint8_t t, int d);
  void setBendRange(uint8_t t, int d);
  void togglePorta(uint8_t t);
  void setPortaTime(uint8_t t, int d);
  void toggleSustain(uint8_t t);
  void toggleMute(uint8_t t);
  void toggleSolo(uint8_t t);

  // absolute setters used by the macro pots (0..127, already scaled)
  void potSetCutoff(uint8_t t, uint8_t v)    { data.tracks[t].cutoff    = v > 127 ? 127 : v; }
  void potSetResonance(uint8_t t, uint8_t v) { data.tracks[t].resonance = v > 127 ? 127 : v; }
  void potSetRevSend(uint8_t t, uint8_t v)   { data.tracks[t].revSend   = v > 127 ? 127 : v; }
  void potSetVol(uint8_t t, uint8_t v)       { data.tracks[t].vol       = v > 127 ? 127 : v; }

  // global edits
  void setBpm(int d);
  void setSwing(int d);
  void setSpb(int d);
  void setScaleRoot(int d);
  void setScaleType(int d);
  void setScaleLock(int d);
  void setRevType(int d);
  void setRevLevel(int d);
  void setRevTime(int d);
  void setChoType(int d);
  void setChoLevel(int d);
  void setChoRate(int d);
  void setChoDepth(int d);
  void setMasterVol(int d);

  // scale actions
  void quantizeTrack(uint8_t t);
  void quantizeAll();

  // clearing
  void clearTrack(uint8_t t);

private:
  // ---- engine-owned timing state (core1 only) ----
  bool      eRunning   = false;
  uint64_t  nextUs     = 0;
  uint32_t  gTick      = 0;
  uint32_t  periodDen  = 1;
  uint32_t  periodWhole= 0;
  uint32_t  periodFrac = 0;
  uint64_t  accFrac    = 0;
  uint16_t  lastBpm    = 0;
  uint8_t   lastSpb    = 0;

  // per-track playhead state (direction / clock-divider aware)
  int16_t   ePos[MAX_TRACKS];
  int8_t    ePp[MAX_TRACKS];        // ping-pong direction
  uint32_t  eAdv[MAX_TRACKS];       // advance counter (drives swing parity)

  // audition (wall-clock) state
  bool      audActive  = false;
  uint64_t  audOffUs   = 0;
  uint8_t   audActiveCh= 1;
  uint8_t   audActiveNote = 60;

  // ---- event scheduler ----
  enum EvType : uint8_t { EV_OFF = 0, EV_ON = 1 };
  struct Ev { uint32_t tick; uint8_t type; uint8_t ch; uint8_t note; uint8_t vel; uint8_t used; };
  Ev events[MAX_EVENTS];

  // ---- shadow of last-applied settings (for change detection) ----
  struct TrShadow {
    uint8_t channel, program, bank;
    uint8_t vol, pan, rev, cho, expr, mod;
    uint8_t cutoff, reso, atk, dcy, rel;
    uint8_t vibR, vibD, vibDl;
    uint8_t bend, portaOn, portaT, sus;
  };
  TrShadow shadow[MAX_TRACKS];
  uint8_t  shRevType, shRevLevel, shRevTime;
  uint8_t  shChoType, shChoLevel, shChoRate, shChoDepth;
  uint8_t  shMasterVol;
  bool     shadowValid = false;

  // ---- rng (xorshift32) ----
  uint32_t rngState = 0xC0FFEEu;
  inline uint32_t rng() {
    rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
    return rngState;
  }

  void   refreshPeriod();
  void   reconcile();
  void   resetPlayheads();
  void   startFromTop();
  void   startContinue();
  void   stopTransport();
  void   tick();
  void   advanceTrack(uint8_t t);
  void   triggerStep(uint32_t stepCount);
  void   serviceEvents(uint32_t curTick);
  void   scheduleEvent(uint32_t tick, uint8_t type, uint8_t ch, uint8_t note, uint8_t vel);
  void   flushAllOffs();
};

extern Sequencer seq;   // single global instance (defined in the .ino)
