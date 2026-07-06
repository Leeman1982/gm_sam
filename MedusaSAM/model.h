#pragma once
// ============================================================================
//  Medusa SAM  --  model.h
//  Pure data: the Song / Pattern / Track / Step hierarchy plus the scale-lock
//  tables.  No behaviour here -- the engine and UI act on these.
//
//  THREADING: the UI (core0) edits these fields, the engine (core1) reads
//  them.  Every field is a single aligned 8/16-bit scalar, which the
//  Cortex-M0+ reads/writes atomically, so no locks are needed.
// ============================================================================
#include <Arduino.h>
#include "config.h"

// ─── One step ────────────────────────────────────────────────────────────────
struct Step {
    uint8_t note    = 60;    // MIDI note (drum note on a drum track)
    uint8_t vel     = 100;   // 1..127
    uint8_t gate    = 75;    // 1..100 % of the step interval
    uint8_t prob    = 100;   // 0..100 % chance to fire
    int8_t  micro   = 0;     // -12..+12 ticks of micro-timing (PPQN/step = 24 @16ths)
    uint8_t ratchet = 1;     // 1..4 sub-hits inside the step
    uint8_t flags   = 0;     // see StepFlag
    uint8_t _pad    = 0;     // keep sizeof(Step) == 8 for stable save files
};
enum StepFlag : uint8_t {
    STEP_ON     = 1 << 0,    // step plays
    STEP_ACCENT = 1 << 1,    // +velocity boost
    STEP_TIE    = 1 << 2,    // hold into the next step (legato / slide)
};
static inline bool stepOn(const Step& s)     { return s.flags & STEP_ON; }
static inline bool stepAccent(const Step& s) { return s.flags & STEP_ACCENT; }
static inline bool stepTie(const Step& s)    { return s.flags & STEP_TIE; }

// ─── Per-track drum-mode override (GS "use for rhythm part") ─────────────────
enum RhythmMode : uint8_t { RM_AUTO = 0, RM_MELODIC, RM_DRUM, RM_NUM };

// ─── Per-track configuration (shared across patterns) ───────────────────────
// Every SAM2695 per-part parameter lives here; the engine's reconcile pass
// diffs these against a shadow copy and sends only what changed, so edits are
// audible immediately without flooding the 31250-baud MIDI link.
// NRPN-style values are 0..127 with 64 = "no change from the preset".
struct TrackCfg {
    uint8_t channel   = 1;    // 1..16 (channel 10 = GM drums)
    uint8_t program   = 0;    // GM program 0..127 (drum-kit number on drums)
    uint8_t bankMSB   = 0;    // CC0: 0 = GM, 127 = MT-32 bank
    uint8_t rhythm    = RM_AUTO; // GS 40 1x 15 drum-part override
    int8_t  octave    = 0;    // -3..+3 transpose (melodic tracks)
    uint8_t dest      = DEST_SAM; // note routing (future baked-synth layer)

    // Mix
    uint8_t vol       = 100;  // CC7
    uint8_t pan       = 64;   // CC10 (64 = centre)
    uint8_t revSend   = 30;   // CC91 reverb send
    uint8_t choSend   = 0;    // CC93 chorus send
    uint8_t modWheel  = 0;    // CC1  modulation depth

    // Sound shaping (SAM2695 per-part NRPNs, 64 = preset default)
    uint8_t cutoff    = 64;   // NRPN 01 20  TVF cutoff
    uint8_t reso      = 64;   // NRPN 01 21  TVF resonance
    uint8_t attack    = 64;   // NRPN 01 63  envelope attack
    uint8_t decay     = 64;   // NRPN 01 64  envelope decay
    uint8_t release   = 64;   // NRPN 01 66  envelope release
    uint8_t vibRate   = 64;   // NRPN 01 08  vibrato rate
    uint8_t vibDepth  = 64;   // NRPN 01 09  vibrato depth
    uint8_t vibDelay  = 64;   // NRPN 01 0A  vibrato delay

    // Performance
    uint8_t bendRange = 2;    // RPN 00 00 pitch-bend range in semitones (0..24)
    uint8_t portaTime = 0;    // CC5; 0 = portamento off (CC65), >0 = glide time
    uint8_t mute      = 0;
    uint8_t solo      = 0;
    uint8_t _pad[2]   = {0, 0};
};

// A track sounds as drums when forced by the GS rhythm override, or by
// default when it sits on the GM percussion channel.
static inline bool trackIsDrum(const TrackCfg& c) {
    return c.rhythm == RM_DRUM || (c.rhythm == RM_AUTO && c.channel == DRUM_CHANNEL);
}

// ─── A pattern: 16 tracks x 64 steps + per-track loop lengths ────────────────
// Each track loops at its own length (polymeter).  `length` is the bar: it
// drives the master step counter, the pattern-chain advance and queued
// pattern switches; shorter tracks repeat inside the bar.
struct Pattern {
    Step    step[NUM_TRACKS][NUM_STEPS];
    uint8_t tlen[NUM_TRACKS];         // per-track loop length 1..NUM_STEPS
    uint8_t length = DEFAULT_STEPS;   // bar length in steps
    uint8_t swing  = 0;               // 0..SWING_MAX %
    uint8_t _pad[2] = {0, 0};
};

// ─── Scale lock ───────────────────────────────────────────────────────────────
enum ScaleType : uint8_t { SC_OFF = 0, SC_MAJOR, SC_MINOR, SC_DORIAN, SC_MIXO,
                           SC_PENT_MAJ, SC_PENT_MIN, SC_BLUES, SC_NUM };
extern const char* const SCALE_NAMES[SC_NUM];
extern const char* const ROOT_NAMES[12];

// Snap a MIDI note to the song scale (nearest tone, ties break downward).
uint8_t scaleSnap(uint8_t root, uint8_t type, uint8_t note);
// Walk to the next scale tone above/below `note` (dir = +1 / -1).
uint8_t scaleStep(uint8_t root, uint8_t type, uint8_t note, int dir);

// ─── Global effects / master section (the SAM2695 "hidden" parameters) ──────
// Defaults mirror the chip's GS power-on state.
struct FxParams {
    // Reverb (CC80 macro + GS 40 01 3x detail)
    uint8_t revType     = 4;    // 0..7  Room1..PanDelay (CC80 / GS 40 01 30)
    uint8_t revLevel    = 64;   // GS 40 01 33  master reverb level
    uint8_t revTime     = 64;   // GS 40 01 34  reverb time
    uint8_t revFeedback = 0;    // GS 40 01 35  delay feedback (Delay/PanDelay types)
    uint8_t revPreLpf   = 0;    // GS 40 01 32  pre-LPF 0..7
    // Chorus (CC81 macro + GS 40 01 3x detail)
    uint8_t choType     = 2;    // 0..7  Chorus1..FbDelay (CC81 / GS 40 01 38)
    uint8_t choLevel    = 64;   // GS 40 01 3A  master chorus level
    uint8_t choFeedback = 8;    // GS 40 01 3B
    uint8_t choDelay    = 80;   // GS 40 01 3C
    uint8_t choRate     = 3;    // GS 40 01 3D
    uint8_t choDepth    = 19;   // GS 40 01 3E
    uint8_t choToRev    = 0;    // GS 40 01 3F  chorus -> reverb send
    uint8_t choPreLpf   = 0;    // GS 40 01 39  pre-LPF 0..7
    // 4-band equalizer (SAM2695-specific NRPN 37xx; gain 64 = 0 dB)
    uint8_t eqGain[4]   = {64, 64, 64, 64};  // NRPN 37 00..03 lo/mid-lo/mid-hi/hi
    uint8_t eqFreq[4]   = {20, 40, 80, 110}; // NRPN 37 08..0B band frequencies
    // Master section
    uint8_t masterVol   = 110;  // GM universal SysEx master volume
    int8_t  keyShift    = 0;    // GS 40 00 05 master key shift, -24..+24 semis
    int8_t  fineTune    = 0;    // GS 40 00 00 master tune, -100..+100 cents (x1)
    uint8_t masterPan   = 64;   // GS 40 00 06
    uint8_t _pad        = 0;
};

// ─── The whole song in RAM (~66 KB -- comfortably inside the RP2040's 264 KB) ─
struct Song {
    TrackCfg track[NUM_TRACKS];
    Pattern  pattern[NUM_PATTERNS];
    FxParams fx;
    uint16_t bpm       = BPM_DEFAULT;
    uint8_t  spbIndex  = 3;           // 4 steps/beat = 16th notes
    uint8_t  clockOut  = 1;           // emit MIDI clock + start/stop
    // Song chain: ordered pattern indices, 0xFF = end of chain.
    uint8_t  chain[CHAIN_LEN];
    uint8_t  chainLen  = 1;           // 1 = loop a single pattern
    // Scale lock (melodic note entry AND playback snap).
    uint8_t  scaleRoot = 0;           // 0=C .. 11=B
    uint8_t  scaleType = SC_OFF;
    uint8_t  _pad[3]   = {0, 0, 0};
};

// Reset a pattern in place (steps off, lengths back to 16).  Used instead of
// `p = Pattern()`: a Pattern is ~8 KB, and a temporary that size would blow
// the RP2040's per-core stack.
void patternReset(Pattern& p);

// Build the default song in place (a playable groove out of the box).
// Never constructs Song/Pattern temporaries -- see patternReset().
void songInitDefault(Song& s);
