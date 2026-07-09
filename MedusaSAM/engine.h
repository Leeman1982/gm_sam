#pragma once
// ============================================================================
//  Medusa SAM  --  engine.h
//  The core1 real-time engine: 96-PPQN drift-free transport, note-event
//  scheduler (swing / micro / gate / probability / ratchet / tie), pattern
//  chaining, and the settings-reconcile pass that keeps the SAM2695 in sync
//  with the Song.
//
//  THREADING MODEL
//    core0 (UI)     : edits the Song, raises the volatile request flags below,
//                     reads the volatile playhead fields for display.
//    core1 (engine) : calls begin() once, then service() in a tight loop.
//                     The ONLY core that touches the MIDI UART.
//  Shared state is single aligned 8/16-bit scalars -- atomic on the M33.
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "model.h"

class Engine {
public:
    // ---- request flags (core0 sets, core1 clears) ----
    volatile uint8_t reqStart     = 0;   // start from the top
    volatile uint8_t reqContinue  = 0;   // resume from current position
    volatile uint8_t reqStop      = 0;
    volatile uint8_t reqPanic     = 0;
    volatile uint8_t reqGmReset   = 0;   // GM reset + full resend
    volatile uint8_t reqGsReset   = 0;   // GS reset + full resend
    volatile uint8_t reqResendAll = 0;   // full reconcile (after song load)
    volatile uint8_t reqPattern   = 0xFF;// switch pattern (queued to bar end)

    // Flash-safe save/load is handled in ui.cpp with the Pico SDK multicore
    // lockout (it parks this core in a RAM ISR for the flash write), so no
    // request-flag handshake is needed here.

    // ---- engine -> UI state ----
    volatile uint8_t isRunning  = 0;
    volatile uint8_t playPat    = 0;                  // sounding pattern
    volatile uint8_t trackStep[NUM_TRACKS] = {0};     // per-track playhead

    // ---- audition mailbox (UI preview notes) ----
    volatile uint8_t audReq = 0, audCh = 1, audNote = 60, audVel = 100;

    // ---- XPRT raw-send mailbox ----
    // kind: 1 = CC(a=ch,b=num,c=val)  2 = NRPN(a=ch,b=msb,c=lsb,d=val)
    //       3 = GS SysEx(a,b,c = address, d = value)
    volatile uint8_t xKind = 0, xA = 0, xB = 0, xC = 0, xD = 0;

    void begin(Song* song);      // core1, after SAM::begin()
    void service();              // core1 tight loop

    bool running() const { return isRunning != 0; }

private:
    Song* _song = nullptr;

    // transport / timing (core1 only)
    bool     eRunning    = false;
    uint64_t nextUs      = 0;
    uint32_t gTick       = 0;
    uint32_t stepCount   = 0;    // free-running step counter (polymeter base)
    uint8_t  barPos      = 0;    // 0..pattern.length-1 (chain / bar logic)
    uint8_t  patIdx      = 0;
    uint8_t  chainPos    = 0;
    uint32_t periodDen   = 1, periodWhole = 0, periodFrac = 0;
    uint64_t accFrac     = 0;
    uint16_t lastBpm     = 0;
    uint8_t  lastSpb     = 0;

    // tie / held-note state per track
    uint8_t  heldNote[NUM_TRACKS];
    uint8_t  heldChan[NUM_TRACKS];
    uint8_t  heldDest[NUM_TRACKS];
    uint8_t  tieHeld[NUM_TRACKS];

    // audition state
    bool     audActive = false;
    uint64_t audOffUs  = 0;
    uint8_t  audOnCh = 1, audOnNote = 60;

    // event scheduler
    enum EvType : uint8_t { EV_OFF = 0, EV_ON = 1 };
    struct Ev { uint32_t tick; uint8_t type, dest, ch, note, vel, used; };
    Ev events[MAX_EVENTS];

    // shadow of last-applied settings (change detection -> minimal MIDI)
    struct TrShadow {
        uint8_t channel, program, bank, rhythm, vol, pan, revSend, choSend, modWheel;
        uint8_t cutoff, reso, attack, decay, release;
        uint8_t vibRate, vibDepth, vibDelay;
        uint8_t bendRange, portaTime;
    };
    TrShadow shadow[NUM_TRACKS];
    FxParams shFx;

    // Invalidate = fill the shadows with 0x80: never equal to any legal value
    // (all uint8 params are <= 127; the int8s never reach -128), so every
    // field reads as "changed" and the normal diff pass resends it.  Progress
    // survives TX-budget interruptions because each sent field updates its
    // shadow immediately.
    void invalidateShadow();

    // xorshift32 for probability
    uint32_t rngState = 0xC0FFEEu;
    inline uint32_t rng() {
        rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
        return rngState;
    }

    uint16_t ticksPerStep() const {
        uint8_t spb = kStepsPerBeatOptions[_song->spbIndex % NUM_SPB_OPTIONS];
        return PPQN / spb;
    }
    bool anySolo() const;
    bool trackAudible(uint8_t t) const;

    void refreshPeriod();
    void reconcile();
    bool reconcileTrack(uint8_t t);   // false = TX budget exhausted
    bool reconcileFx();
    void startFromTop();
    void stopTransport();
    void tick();
    void triggerStep();
    void serviceEvents(uint32_t curTick);
    void scheduleEvent(uint32_t tick, uint8_t type, uint8_t dest,
                       uint8_t ch, uint8_t note, uint8_t vel);
    void releaseHeld(uint8_t t, uint32_t atTick);
    void flushAllOffs();
    void applyPatternSwitch(uint8_t idx);
};

extern Engine engine;    // single instance, defined in MedusaSAM.ino
