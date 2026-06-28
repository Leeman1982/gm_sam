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
  int8_t  micro;    // -12..+12 ticks of micro-timing
  uint8_t tie;      // 0/1  (hold across the step boundary)
  uint8_t _pad;
};

struct Track {
  uint8_t channel;    // 1..16
  uint8_t program;    // 0..127 GM program
  uint8_t bankMSB;    // 0 = GM, 127 = MT-32
  int8_t  octave;     // -3..+3 transpose
  uint8_t length;     // 1..MAX_STEPS
  uint8_t vol;        // CC7   0..127
  uint8_t pan;        // CC10  0..127 (64 centre)
  uint8_t revSend;    // CC91  0..127
  uint8_t choSend;    // CC93  0..127
  uint8_t mute;       // 0/1
  uint8_t solo;       // 0/1
  uint8_t stepField;  // current StepField being edited on SEQ page
  uint8_t curStep;    // engine-written playhead (display only)
  uint8_t _pad[3];
  Step    steps[MAX_STEPS];
};

struct Song {
  uint16_t bpm;        // 20..300
  uint8_t  swing;      // 0..75 (%)
  uint8_t  spbIndex;   // index into kStepsPerBeatOptions
  uint8_t  revType;    // 0..7 global reverb program (CC80)
  uint8_t  choType;    // 0..7 global chorus program (CC81)
  uint8_t  masterVol;  // 0..127 (GM master volume SysEx)
  uint8_t  clockSrc;   // ClockSource
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
  volatile uint8_t reqFont      = 0;   // 0 = none, else (font index + 1)
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

  // transport requests
  void play()     { if (isRunning) reqStop = 1; else reqStart = 1; }
  void stop()     { reqStop = 1; }
  void startTop() { reqStart = 1; }
  void cont()     { reqContinue = 1; }
  void panic()    { reqPanic = 1; }
  void resendAll(){ reqResendAll = 1; }
  void gmReset()  { reqGmReset = 1; }
  void setFont(int idx) { reqFont = (uint8_t)(idx + 1); }   // applied on core1
  void audition(uint8_t t, uint8_t note);

  // step edits
  void toggleStep(uint8_t t, uint8_t s);
  void editStepField(uint8_t t, uint8_t s, uint8_t field, int delta);

  // track edits
  void setProgram(uint8_t t, int delta);
  void setBank(uint8_t t);                 // toggle GM <-> MT-32
  void setChannel(uint8_t t, int delta);
  void setOctave(uint8_t t, int delta);
  void setLength(uint8_t t, int delta);
  void setVol(uint8_t t, int delta);
  void setPan(uint8_t t, int delta);
  void setRevSend(uint8_t t, int delta);
  void setChoSend(uint8_t t, int delta);
  void toggleMute(uint8_t t);
  void toggleSolo(uint8_t t);

  // global edits
  void setBpm(int delta);
  void setSwing(int delta);
  void setSpb(int delta);
  void setRevType(int delta);
  void setChoType(int delta);
  void setMasterVol(int delta);

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
  struct TrShadow { uint8_t channel, program, bank, vol, pan, rev, cho; };
  TrShadow shadow[MAX_TRACKS];
  uint8_t  shRevType, shChoType, shMasterVol;
  bool     shadowValid = false;

  // ---- rng (xorshift32) ----
  uint32_t rngState = 0xC0FFEEu;
  inline uint32_t rng() {
    rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
    return rngState;
  }

  void   refreshPeriod();
  void   reconcile();
  void   startFromTop();
  void   startContinue();
  void   stopTransport();
  void   tick();
  void   triggerStep(uint32_t stepCount);
  void   serviceEvents(uint32_t curTick);
  void   scheduleEvent(uint32_t tick, uint8_t type, uint8_t ch, uint8_t note, uint8_t vel);
  void   flushAllOffs();
};

extern Sequencer seq;   // single global instance (defined in the .ino)
