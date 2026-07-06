#include "model.h"

// ─── Scale lock ──────────────────────────────────────────────────────────────
const char* const SCALE_NAMES[SC_NUM] =
    { "Off", "Major", "Minor", "Dorian", "Mixo", "PentMa", "PentMi", "Blues" };
const char* const ROOT_NAMES[12] =
    { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// Bitmask of in-scale pitch classes (bit n = semitone n above the root).
static const uint16_t SCALE_MASK[SC_NUM] = {
    0x0FFF,                                            // Off (chromatic)
    (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<11), // Major
    (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10), // Minor (natural)
    (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<9)|(1<<10), // Dorian
    (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<10), // Mixolydian
    (1<<0)|(1<<2)|(1<<4)|(1<<7)|(1<<9),                // Pentatonic major
    (1<<0)|(1<<3)|(1<<5)|(1<<7)|(1<<10),               // Pentatonic minor
    (1<<0)|(1<<3)|(1<<5)|(1<<6)|(1<<7)|(1<<10),        // Blues
};

static inline bool inScale(uint8_t root, uint8_t type, int note) {
    if (type == SC_OFF || type >= SC_NUM) return true;
    return (SCALE_MASK[type] >> (((note - root) % 12 + 12) % 12)) & 1;
}

uint8_t scaleSnap(uint8_t root, uint8_t type, uint8_t note) {
    if (type == SC_OFF || type >= SC_NUM) return note;
    for (int d = 0; d <= 6; d++) {                 // nearest tone, downward bias
        if (note - d >= 0   && inScale(root, type, note - d)) return note - d;
        if (note + d <= 127 && inScale(root, type, note + d)) return note + d;
    }
    return note;
}

uint8_t scaleStep(uint8_t root, uint8_t type, uint8_t note, int dir) {
    if (type == SC_OFF || type >= SC_NUM)
        return (uint8_t)constrain((int)note + dir, 0, 127);
    int n = note;
    for (int i = 0; i < 12; i++) {                 // next in-scale tone in dir
        n += (dir >= 0) ? 1 : -1;
        if (n < 0 || n > 127) return scaleSnap(root, type, note);
        if (inScale(root, type, n)) return (uint8_t)n;
    }
    return note;
}

// ─── Default song ────────────────────────────────────────────────────────────
static void putStep(Pattern& p, uint8_t trk, uint8_t s, uint8_t note, uint8_t vel,
                    uint8_t flags = STEP_ON) {
    p.step[trk][s].note  = note;
    p.step[trk][s].vel   = vel;
    p.step[trk][s].flags = flags;
}

void patternReset(Pattern& p) {
    for (int t = 0; t < NUM_TRACKS; t++) {
        p.tlen[t] = DEFAULT_STEPS;
        for (int st = 0; st < NUM_STEPS; st++) p.step[t][st] = Step();
    }
    p.length = DEFAULT_STEPS;
    p.swing  = 0;
}

void songInitDefault(Song& s) {
    // Reset field by field: `s = Song()` would put a ~66 KB temporary on the
    // stack, far beyond the RP2040's per-core stack.
    for (int pi = 0; pi < NUM_PATTERNS; pi++) patternReset(s.pattern[pi]);
    s.fx        = FxParams();
    s.bpm       = BPM_DEFAULT;
    s.spbIndex  = 3;                 // 16th notes
    s.clockOut  = 1;
    s.scaleRoot = 0;
    s.scaleType = SC_OFF;

    // ── Track layout ─────────────────────────────────────────────────────────
    // Tracks 1-9 are melodic on channels 1-9; tracks 10-13 are drum LANES that
    // all speak on channel 10 (the GM kit), so kick/snare/hats each get their
    // own row like on the Medusa; tracks 14-16 are melodic on channels 14-16.
    for (int t = 0; t < NUM_TRACKS; t++) {
        TrackCfg& c = s.track[t];
        c = TrackCfg();
        c.channel = (uint8_t)(t + 1);
    }
    s.track[0].program  = 0;    s.track[0].revSend = 24;              // Piano
    s.track[1].program  = 33;   s.track[1].revSend = 0;               // Fing Bass
    s.track[1].vol      = 110;
    s.track[2].program  = 48;   s.track[2].pan = 48; s.track[2].revSend = 52; // Strings
    s.track[2].vol      = 80;
    s.track[3].program  = 80;   s.track[3].pan = 82; s.track[3].revSend = 40; // Sq Lead
    s.track[3].vol      = 85;
    for (int t = 4; t < 9; t++) {
        s.track[t].program = (uint8_t)(t * 4);
        s.track[t].vol     = 90;
        s.track[t].pan     = (uint8_t)(40 + (t - 4) * 12);            // spread L->R
    }
    // Drum lanes -- all on channel 10; note defaults give each row its voice.
    static const uint8_t laneNote[4] = {36, 38, 42, 46};  // kick snare chat ohat
    static const uint8_t laneVol[4]  = {120, 110, 90, 90};
    for (int i = 0; i < 4; i++) {
        TrackCfg& c = s.track[9 + i];
        c.channel = DRUM_CHANNEL;
        c.vol     = laneVol[i];
        c.revSend = (uint8_t)(i == 0 ? 0 : 14);
    }
    for (int t = 13; t < NUM_TRACKS; t++) {
        s.track[t].program = (uint8_t)(52 + (t - 13) * 8);
        s.track[t].vol     = 85;
    }

    // Give the drum lanes their default step notes in every pattern so
    // toggling a step immediately plays the lane's voice.
    for (int pi = 0; pi < NUM_PATTERNS; pi++)
        for (int i = 0; i < 4; i++)
            for (int st = 0; st < NUM_STEPS; st++)
                s.pattern[pi].step[9 + i][st].note = laneNote[i];

    // ── Pattern 1: a groove so the box makes sound out of the box ───────────
    Pattern& p = s.pattern[0];
    p.length = 16;
    for (int q = 0; q < 16; q += 4)  putStep(p,  9, q, 36, 120);      // kick 4/4
    putStep(p, 10, 4, 38, 110);  putStep(p, 10, 12, 38, 110);         // snare 2+4
    for (int e = 0; e < 16; e += 2)  putStep(p, 11, e, 42, 80);       // chat 8ths
    putStep(p, 12, 2, 46, 70);   putStep(p, 12, 10, 46, 70);          // ohat off
    static const uint8_t bass[16] = {33,0,33,0, 33,0,40,0, 33,0,33,0, 36,0,40,0};
    for (int i = 0; i < 16; i++) if (bass[i]) putStep(p, 1, i, bass[i], 110);
    putStep(p, 0, 0, 60, 90);    putStep(p, 0, 8, 64, 90);            // piano stabs

    s.bpm      = BPM_DEFAULT;
    s.chainLen = 1;
    s.chain[0] = 0;
    for (int i = 1; i < CHAIN_LEN; i++) s.chain[i] = 0xFF;
}
