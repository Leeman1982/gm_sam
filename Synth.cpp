// ============================================================================
//  Synth.cpp  -  On-chip SoundFont voice engine (see Synth.h)
// ============================================================================
#include "Synth.h"
#include "SF2.h"
#include <math.h>
#include <string.h>

// ---- baked SoundFont headers (each guarded by its own FONT_* define) -------
#if FONT_SYNTHGMS
  #include "Soundfont_SYNTHGMS.h"
#endif
#if FONT_VINTAGEDREAMS
  #include "Soundfont_VintageDreams.h"
#endif
#if !FONT_SYNTHGMS && !FONT_VINTAGEDREAMS && !FONT_POWERGM
  #error "Enable at least one SoundFont in Config.h (FONT_SYNTHGMS / _VINTAGEDREAMS / _POWERGM)"
#endif

namespace {

// ---- per-MIDI-channel state ------------------------------------------------
struct Channel {
  uint8_t program = 0;
  uint8_t bankMSB = 0;
  uint8_t vol     = 100;   // CC7
  uint8_t expr    = 127;   // CC11
  uint8_t pan     = 64;    // CC10
  int16_t bend    = 0;     // -8192..+8191
};
Channel  ch_[16];
uint8_t  master_ = 120;    // GM master volume 0..127

// ---- voice state -----------------------------------------------------------
enum Stage : uint8_t { ST_DELAY, ST_ATTACK, ST_HOLD, ST_DECAY, ST_SUSTAIN, ST_RELEASE, ST_DONE };

struct Voice {
  bool     active = false;
  bool     released = false;
  uint8_t  ch = 0, key = 0, vel = 0, exClass = 0;
  uint32_t age = 0;

  // playback (32.32 fixed phase into the PCM pool)
  uint64_t phase = 0;
  double   baseIncF = 0;        // frames-per-output-sample at bend == 0
  uint32_t start = 0, end = 0, startloop = 0, endloop = 0;
  uint8_t  loopMode = 0;
  bool     looping = false;

  int16_t  pan = 0;            // region pan -500..500
  int32_t  baseAtten = 65536;  // Q16, velocity * region attenuation

  // DAHDSR envelope (linear, Q16 amplitude)
  uint8_t  stage = ST_DONE;
  int32_t  env = 0, envInc = 0;
  uint32_t stageSamples = 0;
  int32_t  sustainLevel = 65536;
  uint32_t atkS = 0, holdS = 0, decS = 0, relS = 0;
};
Voice    voices_[SYNTH_MAX_VOICES];
uint32_t ageCounter_ = 1;
bool     ready_ = false;

// ---- resident font registry (built once in begin) --------------------------
struct FontEntry { const char* name; const uint8_t* ptr; uint32_t len; };
FontEntry fonts_[4];
uint8_t   fontCount_  = 0;
uint8_t   activeFont_ = 0;

inline int16_t clip16(int32_t v){ return v > 32767 ? 32767 : (v < -32768 ? -32768 : (int16_t)v); }

// centibels of attenuation -> Q16 linear gain (0..65536)
inline int32_t cb2gainQ16(int16_t cb){
  if (cb <= 0)   return 65536;
  if (cb >= 960) return 0;                 // ~ -96 dB, inaudible
  float g = powf(10.0f, -cb / 200.0f);
  return (int32_t)(g * 65536.0f);
}

void advanceStage(Voice& v){
  switch (v.stage) {
    case ST_DELAY:
      v.stage = ST_ATTACK;
      if (v.atkS) { v.envInc = (65536 - v.env) / (int32_t)v.atkS; v.stageSamples = v.atkS; break; }
      v.env = 65536; /* fallthrough into hold */
    case ST_ATTACK:
      v.env = 65536; v.stage = ST_HOLD;
      if (v.holdS) { v.envInc = 0; v.stageSamples = v.holdS; break; }
    case ST_HOLD:
      v.stage = ST_DECAY;
      if (v.decS) { v.envInc = (v.sustainLevel - 65536) / (int32_t)v.decS; v.stageSamples = v.decS; break; }
      v.env = v.sustainLevel;
    case ST_DECAY:
      v.env = v.sustainLevel; v.stage = ST_SUSTAIN; v.envInc = 0; v.stageSamples = 0xFFFFFFFF;
      break;
    case ST_RELEASE:
      v.env = 0; v.stage = ST_DONE; v.active = false;
      break;
    default:
      v.stage = ST_DONE; v.active = false;
      break;
  }
}

void startRelease(Voice& v){
  if (!v.active || v.stage == ST_RELEASE || v.stage == ST_DONE) return;
  v.released = true;
  v.stage = ST_RELEASE;
  if (v.relS == 0 || v.env <= 0) { v.env = 0; v.stage = ST_DONE; v.active = false; return; }
  v.envInc = -v.env / (int32_t)v.relS;
  v.stageSamples = v.relS;
}

// pick a voice slot: free one, else steal the quietest.
int allocVoice(){
  int best = 0; int32_t bestScore = 0x7FFFFFFF;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
    if (!voices_[i].active) return i;
    // prefer stealing released/quiet voices
    int32_t score = voices_[i].env + (voices_[i].released ? 0 : 1 << 20);
    if (score < bestScore) { bestScore = score; best = i; }
  }
  return best;
}

void startVoice(Voice& v, uint8_t channel, const SF2::Region& r, uint8_t key, uint8_t vel){
  uint8_t playKey = (r.overrideKey != 255) ? r.overrideKey : key;
  uint8_t playVel = (r.overrideVel != 255) ? r.overrideVel : vel;

  v.active = true; v.released = false;
  v.ch = channel; v.key = key; v.vel = playVel; v.exClass = r.exclusiveClass;
  v.age = ageCounter_++;

  v.start = r.start; v.end = r.end;
  v.startloop = r.startloop; v.endloop = r.endloop;
  v.loopMode = r.loopMode;
  v.looping  = (r.loopMode == 1 || r.loopMode == 3) && r.endloop > r.startloop;
  v.phase = (uint64_t)r.start << 32;

  // pitch: cents = (key - root) * scaleTuning + fixed tune
  float cents = (float)((int)playKey - (int)r.rootKey) * (float)r.scaleTuning + (float)r.tuneCents;
  double pitchRatio = pow(2.0, cents / 1200.0);
  v.baseIncF = pitchRatio * (double)r.sampleRate / (double)AUDIO_RATE;

  v.pan = r.pan;

  // velocity (squared curve) * region initial attenuation
  int32_t velG = ((int32_t)playVel * playVel * 65536) / (127 * 127);
  v.baseAtten = (int32_t)(((int64_t)velG * cb2gainQ16(r.attenuation_cb)) >> 16);

  // envelope sample counts
  v.atkS  = (uint32_t)(r.attack  * AUDIO_RATE);
  v.holdS = (uint32_t)(r.hold    * AUDIO_RATE);
  v.decS  = (uint32_t)(r.decay   * AUDIO_RATE);
  v.relS  = (uint32_t)(r.release * AUDIO_RATE);
  v.sustainLevel = cb2gainQ16(r.sustain_cb);
  uint32_t delS = (uint32_t)(r.delay * AUDIO_RATE);

  v.env = 0; v.stage = ST_DELAY;
  if (delS) { v.envInc = 0; v.stageSamples = delS; }
  else      { advanceStage(v); }   // jump straight into attack
}

} // namespace

namespace Synth {

bool begin(){
  for (int i = 0; i < 16; i++) ch_[i] = Channel();
  ch_[DRUM_CHANNEL - 1].bankMSB = 128;          // GM percussion bank

  // ---- build the resident font registry -----------------------------------
  fontCount_ = 0; activeFont_ = 0;
#if FONT_SYNTHGMS
  fonts_[fontCount_++] = { "SYNTHGMS", g_sf2_SYNTHGMS, (uint32_t)g_sf2_SYNTHGMS_len };
#endif
#if FONT_VINTAGEDREAMS
  fonts_[fontCount_++] = { "Vintage Drm", g_sf2_VintageDreams, (uint32_t)g_sf2_VintageDreams_len };
#endif
#if FONT_POWERGM
  {
    // Power GM lives at a fixed XIP flash address (written once with picotool);
    // its length is the RIFF chunk size (bytes 4..7) + 8.  Skip it if the
    // region has not been flashed yet (erased flash is not a valid header).
    const uint8_t* base = (const uint8_t*)(0x10000000UL + FONT_FLASH_OFFSET);
    if (base[0]=='R'&&base[1]=='I'&&base[2]=='F'&&base[3]=='F') {
      uint32_t len = ((uint32_t)base[4] | (base[5]<<8) | (base[6]<<16) |
                      ((uint32_t)base[7]<<24)) + 8;
      fonts_[fontCount_++] = { "Power GM", base, len };
    }
  }
#endif

  ready_ = (fontCount_ > 0) &&
           SF2::begin(fonts_[activeFont_].ptr, fonts_[activeFont_].len);
  return ready_;
}

uint8_t     fontCount()   { return fontCount_; }
uint8_t     currentFont() { return activeFont_; }
const char* fontName(uint8_t i){ return (i < fontCount_) ? fonts_[i].name : ""; }

bool setFont(uint8_t i){
  if (i >= fontCount_) return false;
  panic();                              // voices index into the old sample pool
  activeFont_ = i;
  ready_ = SF2::begin(fonts_[i].ptr, fonts_[i].len);
  return ready_;
}

void noteOn(uint8_t ch, uint8_t note, uint8_t vel){
  if (!ready_) return;
  if (vel == 0) { noteOff(ch, note); return; }
  uint8_t c = (ch ? ch - 1 : 0) & 15;
  uint8_t bank = (c == DRUM_CHANNEL - 1) ? 128 : ch_[c].bankMSB;

  SF2::Region regs[8];
  int n = SF2::noteRegions(bank, ch_[c].program, note, vel, regs, 8);
  for (int i = 0; i < n; i++) {
    // exclusive class: cut same-class voices on this channel (hi-hats etc.)
    if (regs[i].exclusiveClass) {
      for (int j = 0; j < SYNTH_MAX_VOICES; j++)
        if (voices_[j].active && voices_[j].ch == c &&
            voices_[j].exClass == regs[i].exclusiveClass)
          startRelease(voices_[j]);
    }
    int slot = allocVoice();
    startVoice(voices_[slot], c, regs[i], note, vel);
  }
}

void noteOff(uint8_t ch, uint8_t note){
  uint8_t c = (ch ? ch - 1 : 0) & 15;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++)
    if (voices_[i].active && !voices_[i].released &&
        voices_[i].ch == c && voices_[i].key == note)
      startRelease(voices_[i]);
}

void programChange(uint8_t ch, uint8_t program){ ch_[(ch?ch-1:0)&15].program = program & 127; }
void bankSelect  (uint8_t ch, uint8_t bankMSB){ ch_[(ch?ch-1:0)&15].bankMSB = bankMSB; }
void pitchBend   (uint8_t ch, int16_t bend14){ ch_[(ch?ch-1:0)&15].bend = bend14; }
void setVolume   (uint8_t ch, uint8_t v){ ch_[(ch?ch-1:0)&15].vol  = v & 127; }
void setPan      (uint8_t ch, uint8_t v){ ch_[(ch?ch-1:0)&15].pan  = v & 127; }
void setExpression(uint8_t ch, uint8_t v){ ch_[(ch?ch-1:0)&15].expr = v & 127; }

void allNotesOff(uint8_t ch){
  uint8_t c = (ch?ch-1:0)&15;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++)
    if (voices_[i].active && voices_[i].ch == c) startRelease(voices_[i]);
}
void allSoundOff(uint8_t ch){
  uint8_t c = (ch?ch-1:0)&15;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++)
    if (voices_[i].active && voices_[i].ch == c) { voices_[i].active = false; voices_[i].stage = ST_DONE; }
}
void panic(){ for (int i = 0; i < SYNTH_MAX_VOICES; i++) { voices_[i].active = false; voices_[i].stage = ST_DONE; } }

void reset(){
  panic();
  for (int i = 0; i < 16; i++) ch_[i] = Channel();
  ch_[DRUM_CHANNEL - 1].bankMSB = 128;
}

void masterVolume(uint8_t v){ master_ = v & 127; }

const char* patchName(uint8_t ch, uint8_t program){
  if (!ready_) return nullptr;
  uint8_t bank = ((ch?ch-1:0)&15) == DRUM_CHANNEL - 1 ? 128 : 0;
  return SF2::presetName(bank, program);
}

uint8_t activeVoices(){
  uint8_t n = 0;
  for (int i = 0; i < SYNTH_MAX_VOICES; i++) if (voices_[i].active) n++;
  return n;
}

// ---- audio rendering -------------------------------------------------------
void render(int16_t* out, uint32_t frames){
  static int32_t acc[AUDIO_BLOCK * 2];
  if (frames > AUDIO_BLOCK) frames = AUDIO_BLOCK;
  memset(acc, 0, frames * 2 * sizeof(int32_t));

  if (ready_) {
    const int16_t* pcm = SF2::pcm();

    // per-channel combined gain (vol * expr * master), Q15, recomputed/block
    int32_t chGain[16];
    double  chBend[16];
    int32_t panL[16], panR[16];
    for (int c = 0; c < 16; c++) {
      int32_t g = (int32_t)((int64_t)ch_[c].vol * ch_[c].expr * master_ * 32768
                             / (127L * 127L * 127L));
      chGain[c] = g;
      double semis = (double)ch_[c].bend / 8192.0 * SYNTH_BEND_RANGE;
      chBend[c] = pow(2.0, semis / 12.0);
      // channel pan (CC10) -> equal-ish power split, Q15
      float p = ch_[c].pan / 127.0f;            // 0..1
      panL[c] = (int32_t)(cosf(p * 1.5707963f) * 32767);
      panR[c] = (int32_t)(sinf(p * 1.5707963f) * 32767);
    }

    for (int vi = 0; vi < SYNTH_MAX_VOICES; vi++) {
      Voice& v = voices_[vi];
      if (!v.active) continue;
      uint8_t c = v.ch;

      // combine region pan (-500..500) with channel pan, Q15
      float rp = (v.pan + 500) / 1000.0f;        // 0..1
      int32_t rpL = (int32_t)(cosf(rp * 1.5707963f) * 32767);
      int32_t rpR = (int32_t)(sinf(rp * 1.5707963f) * 32767);
      int32_t gL = (int32_t)(((int64_t)panL[c] * rpL) >> 15);
      int32_t gR = (int32_t)(((int64_t)panR[c] * rpR) >> 15);
      // fold in channel gain and voice base attenuation -> final Q15 L/R
      int32_t cg  = (int32_t)(((int64_t)chGain[c] * v.baseAtten) >> 16);  // Q15
      gL = (int32_t)(((int64_t)gL * cg) >> 15);
      gR = (int32_t)(((int64_t)gR * cg) >> 15);

      uint64_t inc = (uint64_t)(v.baseIncF * chBend[c] * 4294967296.0);
      uint64_t phase = v.phase;
      int32_t  env = v.env, envInc = v.envInc;
      uint32_t stageSamples = v.stageSamples;

      for (uint32_t n = 0; n < frames; n++) {
        uint32_t idx  = (uint32_t)(phase >> 32);
        uint32_t frac = (uint32_t)(phase >> 16) & 0xFFFF;
        int32_t s0 = pcm[idx];
        int32_t s1 = pcm[idx + 1];
        int32_t s  = s0 + (((s1 - s0) * (int32_t)frac) >> 16);

        s = (s * (env >> 4)) >> 12;              // apply envelope (Q16 -> scale)
        acc[n * 2]     += (s * gL) >> 15;
        acc[n * 2 + 1] += (s * gR) >> 15;

        // advance amplitude envelope
        if (v.stage != ST_SUSTAIN) {
          env += envInc;
          if (env < 0) env = 0;
          if (--stageSamples == 0) {
            v.env = env; v.stageSamples = stageSamples;
            advanceStage(v);
            env = v.env; envInc = v.envInc; stageSamples = v.stageSamples;
            if (!v.active) break;
          }
        }
        // free a voice once it has faded out (e.g. decayed drums that loop
        // forever at a silent sustain level) so it stops eating polyphony.
        if (env < 16 && v.stage >= ST_DECAY) { v.active = false; v.stage = ST_DONE; break; }

        // advance phase, handle looping / end
        phase += inc;
        uint32_t pi = (uint32_t)(phase >> 32);
        if (v.looping) {
          uint64_t loopLen = (uint64_t)(v.endloop - v.startloop) << 32;
          while ((uint32_t)(phase >> 32) >= v.endloop) phase -= loopLen;
        } else if (pi + 1 >= v.end) {
          v.active = false; v.stage = ST_DONE; break;
        }
      }

      v.phase = phase;
      if (v.active) { v.env = env; v.envInc = envInc; v.stageSamples = stageSamples; }
    }
  }

  for (uint32_t i = 0; i < frames * 2; i++)
    out[i] = clip16(acc[i] >> SYNTH_HEADROOM);
}

} // namespace Synth
