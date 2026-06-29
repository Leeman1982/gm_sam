// ============================================================================
//  SF2.cpp  -  SoundFont 2 reader implementation (see SF2.h)
// ============================================================================
#include "SF2.h"
#include <math.h>
#include <string.h>

namespace {

// ---- generator operator numbers we care about (SF2 2.04) -------------------
enum Gen : uint16_t {
  GEN_startAddrsOffset        = 0,
  GEN_endAddrsOffset          = 1,
  GEN_startloopAddrsOffset    = 2,
  GEN_endloopAddrsOffset      = 3,
  GEN_startAddrsCoarseOffset  = 4,
  GEN_pan                     = 17,
  GEN_delayVolEnv             = 33,
  GEN_attackVolEnv            = 34,
  GEN_holdVolEnv              = 35,
  GEN_decayVolEnv             = 36,
  GEN_sustainVolEnv           = 37,
  GEN_releaseVolEnv           = 38,
  GEN_instrument              = 41,
  GEN_keyRange                = 43,
  GEN_velRange                = 44,
  GEN_startloopAddrsCoarse    = 45,
  GEN_keynum                  = 46,
  GEN_velocity                = 47,
  GEN_initialAttenuation      = 48,
  GEN_endloopAddrsCoarse      = 50,
  GEN_coarseTune              = 51,
  GEN_fineTune                = 52,
  GEN_sampleID                = 53,
  GEN_sampleModes             = 54,
  GEN_scaleTuning             = 56,
  GEN_exclusiveClass          = 57,
  GEN_overridingRootKey       = 58,
};

// ---- raw record layouts (little-endian, packed in the file) ----------------
#pragma pack(push, 1)
struct PHdr { char name[20]; uint16_t preset, bank, bagNdx; uint32_t lib, genre, morph; };
struct Bag  { uint16_t genNdx, modNdx; };
struct GenU { uint16_t oper; union { struct { uint8_t lo, hi; } range; int16_t s; uint16_t u; } amt; };
struct Inst { char name[20]; uint16_t bagNdx; };
struct SHdr { char name[20]; uint32_t start, end, startloop, endloop, sampleRate;
              uint8_t  originalPitch; int8_t pitchCorrection; uint16_t sampleLink, sampleType; };
#pragma pack(pop)

// ---- parsed table pointers (all into flash) --------------------------------
struct {
  const PHdr* phdr; uint32_t nphdr;
  const Bag*  pbag; uint32_t npbag;
  const GenU* pgen; uint32_t npgen;
  const Inst* inst; uint32_t ninst;
  const Bag*  ibag; uint32_t nibag;
  const GenU* igen; uint32_t nigen;
  const SHdr* shdr; uint32_t nshdr;
  const int16_t* smpl; uint32_t nsmpl;
  bool ok;
} S;

// little-endian readers (flash may be unaligned for chunk sizes)
inline uint32_t rd32(const uint8_t* p){ return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24); }

// Walk RIFF sub-chunks within [p, p+len); returns data ptr + size for `id`.
const uint8_t* findChunk(const uint8_t* p, uint32_t len, const char* id, uint32_t* outSz){
  const uint8_t* end = p + len;
  while (p + 8 <= end) {
    uint32_t sz = rd32(p + 4);
    const uint8_t* body = p + 8;
    if (memcmp(p, id, 4) == 0) { if (outSz) *outSz = sz; return body; }
    p = body + sz + (sz & 1);
  }
  return nullptr;
}

// Find a LIST of the given form type ("sdta"/"pdta"); returns body + size.
const uint8_t* findList(const uint8_t* p, uint32_t len, const char* form, uint32_t* outSz){
  const uint8_t* end = p + len;
  while (p + 12 <= end) {
    uint32_t sz = rd32(p + 4);
    const uint8_t* body = p + 8;
    if (memcmp(p, "LIST", 4) == 0 && memcmp(body, form, 4) == 0) {
      if (outSz) *outSz = sz - 4;
      return body + 4;
    }
    p = body + sz + (sz & 1);
  }
  return nullptr;
}

// convert SF2 timecents to seconds
inline float tc2sec(int16_t tc){ return powf(2.0f, tc / 1200.0f); }

// A flattened generator set during zone resolution.
struct GenSet {
  // amounts indexed sparsely; we just keep the fields we use with defaults.
  int32_t startOff=0, endOff=0, startLoopOff=0, endLoopOff=0;
  int16_t pan=0;
  int16_t delay=-12000, attack=-12000, hold=-12000, decay=-12000, release=-12000;
  int16_t sustain=0;              // cB attenuation
  int16_t attenuation=0;          // cB
  int16_t coarse=0, fine=0, scale=100;
  int16_t rootOverride=-1;
  int16_t keynum=-1, velocity=-1;
  uint8_t loopMode=0, exClass=0;
  int16_t sampleID=-1, instrument=-1;
  uint8_t keyLo=0, keyHi=127, velLo=0, velHi=127;
  bool    hasKeyRange=false, hasVelRange=false;
};

// apply one generator; `additive` true for preset-level (offsets), false for
// instrument-level (absolute values).
void applyGen(GenSet& g, const GenU& gen, bool additive){
  int16_t v = gen.amt.s;
  switch (gen.oper) {
    case GEN_keyRange: g.keyLo=gen.amt.range.lo; g.keyHi=gen.amt.range.hi; g.hasKeyRange=true; break;
    case GEN_velRange: g.velLo=gen.amt.range.lo; g.velHi=gen.amt.range.hi; g.hasVelRange=true; break;
    case GEN_startAddrsOffset:      g.startOff     += v; break;
    case GEN_endAddrsOffset:        g.endOff       += v; break;
    case GEN_startloopAddrsOffset:  g.startLoopOff += v; break;
    case GEN_endloopAddrsOffset:    g.endLoopOff   += v; break;
    case GEN_startAddrsCoarseOffset:g.startOff     += (int32_t)v * 32768; break;
    case GEN_startloopAddrsCoarse:  g.startLoopOff += (int32_t)v * 32768; break;
    case GEN_endloopAddrsCoarse:    g.endLoopOff   += (int32_t)v * 32768; break;
    case GEN_pan:            additive ? (g.pan   += v) : (g.pan   = v); break;
    case GEN_delayVolEnv:    additive ? (g.delay += v) : (g.delay = v); break;
    case GEN_attackVolEnv:   additive ? (g.attack+= v) : (g.attack= v); break;
    case GEN_holdVolEnv:     additive ? (g.hold  += v) : (g.hold  = v); break;
    case GEN_decayVolEnv:    additive ? (g.decay += v) : (g.decay = v); break;
    case GEN_sustainVolEnv:  additive ? (g.sustain+=v) : (g.sustain=v); break;
    case GEN_releaseVolEnv:  additive ? (g.release+=v) : (g.release=v); break;
    case GEN_initialAttenuation: additive ? (g.attenuation+=v) : (g.attenuation=v); break;
    case GEN_coarseTune:     additive ? (g.coarse+= v) : (g.coarse= v); break;
    case GEN_fineTune:       additive ? (g.fine  += v) : (g.fine  = v); break;
    case GEN_scaleTuning:    additive ? (g.scale += v) : (g.scale = v); break;
    case GEN_sampleModes:    g.loopMode = (uint8_t)(gen.amt.u & 3); break;
    case GEN_exclusiveClass: g.exClass  = (uint8_t)gen.amt.u; break;
    case GEN_overridingRootKey: g.rootOverride = v; break;
    case GEN_keynum:         g.keynum = v; break;
    case GEN_velocity:       g.velocity = v; break;
    case GEN_sampleID:       g.sampleID = (int16_t)gen.amt.u; break;
    case GEN_instrument:     g.instrument = (int16_t)gen.amt.u; break;
    default: break;
  }
}

// Re-seed a GenSet for PRESET-level use: every additive field starts at 0 so
// preset generators act as pure offsets onto the instrument's absolute values
// (instrument env defaults are -12000/100; preset offsets must default to 0).
void presetZero(GenSet& g){
  g.delay = g.attack = g.hold = g.decay = g.release = 0;
  g.sustain = 0; g.attenuation = 0; g.coarse = 0; g.fine = 0; g.scale = 0; g.pan = 0;
  g.startOff = g.endOff = g.startLoopOff = g.endLoopOff = 0;
}

inline bool inRange(const GenSet& g, uint8_t key, uint8_t vel){
  if (g.hasKeyRange && (key < g.keyLo || key > g.keyHi)) return false;
  if (g.hasVelRange && (vel < g.velLo || vel > g.velHi)) return false;
  return true;
}

} // namespace

namespace SF2 {

bool begin(const uint8_t* sf2, uint32_t len){
  memset(&S, 0, sizeof(S));
  if (!sf2 || len < 12) return false;
  if (memcmp(sf2, "RIFF", 4) != 0 || memcmp(sf2 + 8, "sfbk", 4) != 0) return false;

  const uint8_t* body = sf2 + 12;
  uint32_t bodyLen = len - 12;

  uint32_t sz;
  const uint8_t* sdta = findList(body, bodyLen, "sdta", &sz);
  if (sdta) {
    const uint8_t* smpl = findChunk(sdta, sz, "smpl", &sz);
    if (smpl) { S.smpl = (const int16_t*)smpl; S.nsmpl = sz / 2; }
  }

  const uint8_t* pdta = findList(body, bodyLen, "pdta", &sz);
  if (!pdta || !S.smpl) return false;
  uint32_t pdtaLen = sz;

  auto grab = [&](const char* id, const void** ptr, uint32_t* count, uint32_t recSize){
    uint32_t s; const uint8_t* c = findChunk(pdta, pdtaLen, id, &s);
    *ptr = c; *count = c ? (s / recSize) : 0;
  };
  grab("phdr", (const void**)&S.phdr, &S.nphdr, sizeof(PHdr));
  grab("pbag", (const void**)&S.pbag, &S.npbag, sizeof(Bag));
  grab("pgen", (const void**)&S.pgen, &S.npgen, sizeof(GenU));
  grab("inst", (const void**)&S.inst, &S.ninst, sizeof(Inst));
  grab("ibag", (const void**)&S.ibag, &S.nibag, sizeof(Bag));
  grab("igen", (const void**)&S.igen, &S.nigen, sizeof(GenU));
  grab("shdr", (const void**)&S.shdr, &S.nshdr, sizeof(SHdr));

  S.ok = S.phdr && S.pbag && S.pgen && S.inst && S.ibag && S.igen && S.shdr &&
         S.nphdr >= 2 && S.nshdr >= 1;
  return S.ok;
}

bool ready(){ return S.ok; }
const int16_t* pcm(){ return S.smpl; }
uint32_t pcmFrames(){ return S.nsmpl; }

// Locate the phdr index for (bank, program), or -1.
static int findPreset(uint8_t bank, uint8_t program){
  for (uint32_t i = 0; i + 1 < S.nphdr; i++)
    if (S.phdr[i].bank == bank && S.phdr[i].preset == program) return (int)i;
  return -1;
}

const char* presetName(uint8_t bank, uint8_t program){
  int i = findPreset(bank, program);
  if (i < 0 && bank != 0) i = findPreset(0, program);
  if (i < 0) return nullptr;
  static char buf[21];
  memcpy(buf, S.phdr[i].name, 20); buf[20] = 0;
  return buf;
}

// Build a finished Region from a resolved generator set + sample header.
static bool finalize(const GenSet& g, uint8_t key, Region& r){
  if (g.sampleID < 0 || (uint32_t)g.sampleID >= S.nshdr) return false;
  const SHdr& sh = S.shdr[g.sampleID];

  int32_t start = (int32_t)sh.start + g.startOff;
  int32_t end   = (int32_t)sh.end   + g.endOff;
  if (start < 0) start = 0;
  if (end <= start) return false;
  if ((uint32_t)end > S.nsmpl) end = S.nsmpl;

  r.start     = (uint32_t)start;
  r.end       = (uint32_t)end;
  r.startloop = (uint32_t)((int32_t)sh.startloop + g.startLoopOff);
  r.endloop   = (uint32_t)((int32_t)sh.endloop   + g.endLoopOff);
  if (r.endloop <= r.startloop || r.endloop > r.end) { r.startloop = r.start; r.endloop = r.end; }
  r.sampleRate = sh.sampleRate ? sh.sampleRate : 22050;
  r.loopMode   = g.loopMode;
  r.rootKey    = (g.rootOverride >= 0) ? (uint8_t)g.rootOverride : sh.originalPitch;
  r.tuneCents  = g.coarse * 100 + g.fine + sh.pitchCorrection;
  r.scaleTuning= g.scale;
  r.attenuation_cb = g.attenuation < 0 ? 0 : g.attenuation;
  r.sustain_cb = g.sustain < 0 ? 0 : g.sustain;
  int pan = g.pan; if (pan < -500) pan = -500; if (pan > 500) pan = 500;
  r.pan        = (int16_t)pan;
  r.delay   = tc2sec(g.delay);
  r.attack  = tc2sec(g.attack);
  r.hold    = tc2sec(g.hold);
  r.decay   = tc2sec(g.decay);
  r.release = tc2sec(g.release);
  r.exclusiveClass = g.exClass;
  r.overrideKey = (g.keynum   >= 0 && g.keynum   <= 127) ? (uint8_t)g.keynum   : 255;
  r.overrideVel = (g.velocity >= 0 && g.velocity <= 127) ? (uint8_t)g.velocity : 255;
  return true;
}

// Resolve every matching instrument zone of one instrument into Regions.
static int expandInstrument(int instIdx, const GenSet& presetGens,
                            uint8_t key, uint8_t vel, Region* out, int maxR){
  if (instIdx < 0 || (uint32_t)instIdx + 1 >= S.ninst) return 0;
  uint32_t bagFrom = S.inst[instIdx].bagNdx;
  uint32_t bagTo   = S.inst[instIdx + 1].bagNdx;

  GenSet global;            // instrument global zone (no sampleID)
  bool haveGlobal = false;
  int n = 0;

  for (uint32_t b = bagFrom; b < bagTo && n < maxR; b++) {
    uint32_t gFrom = S.ibag[b].genNdx;
    uint32_t gTo   = S.ibag[b + 1].genNdx;

    GenSet z = haveGlobal ? global : GenSet();
    bool hasSample = false;
    for (uint32_t gi = gFrom; gi < gTo; gi++) {
      const GenU& gen = S.igen[gi];
      applyGen(z, gen, /*additive=*/false);
      if (gen.oper == GEN_sampleID) hasSample = true;
    }
    if (!hasSample) {            // a global zone: remember it for later zones
      if (b == bagFrom) { global = z; haveGlobal = true; }
      continue;
    }
    if (!inRange(z, key, vel)) continue;

    // layer the preset generators (pure additive offsets) on top ------------
    z.pan         += presetGens.pan;
    z.delay       += presetGens.delay;
    z.attack      += presetGens.attack;
    z.hold        += presetGens.hold;
    z.decay       += presetGens.decay;
    z.release     += presetGens.release;
    z.sustain     += presetGens.sustain;
    z.attenuation += presetGens.attenuation;
    z.coarse      += presetGens.coarse;
    z.fine        += presetGens.fine;
    z.scale       += presetGens.scale;
    z.startOff    += presetGens.startOff;
    z.endOff      += presetGens.endOff;
    z.startLoopOff+= presetGens.startLoopOff;
    z.endLoopOff  += presetGens.endLoopOff;

    if (finalize(z, key, out[n])) n++;
  }
  return n;
}

int noteRegions(uint8_t bank, uint8_t program, uint8_t key, uint8_t vel,
                Region* out, int maxRegions){
  if (!S.ok || maxRegions <= 0) return 0;

  int p = findPreset(bank, program);
  if (p < 0 && bank == 128) p = findPreset(128, 0);   // any drum kit
  if (p < 0)                p = findPreset(0, program); // melodic fallback
  if (p < 0)                p = findPreset(0, 0);
  if (p < 0) return 0;

  uint32_t bagFrom = S.phdr[p].bagNdx;
  uint32_t bagTo   = S.phdr[p + 1].bagNdx;

  GenSet pGlobal; presetZero(pGlobal); bool havePGlobal = false;
  int total = 0;

  for (uint32_t b = bagFrom; b < bagTo && total < maxRegions; b++) {
    uint32_t gFrom = S.pbag[b].genNdx;
    uint32_t gTo   = S.pbag[b + 1].genNdx;

    GenSet z; presetZero(z);                 // preset gens are zero-based offsets
    if (havePGlobal) z = pGlobal;
    bool hasInst = false;
    for (uint32_t gi = gFrom; gi < gTo; gi++) {
      const GenU& gen = S.pgen[gi];
      applyGen(z, gen, /*additive=*/true);
      if (gen.oper == GEN_instrument) hasInst = true;
    }
    if (!hasInst) {                 // preset global zone
      if (b == bagFrom) { pGlobal = z; havePGlobal = true; }
      continue;
    }
    if (!inRange(z, key, vel)) continue;

    total += expandInstrument(z.instrument, z, key, vel,
                              out + total, maxRegions - total);
  }
  return total;
}

} // namespace SF2
