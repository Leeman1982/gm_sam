// ============================================================================
//  SoundFont.cpp  -  TinySoundFont-backed synthesis engine
//
//  DEPENDENCY: add TinySoundFont's single header "tsf.h" to your sketch folder
//  (or as a library), exactly like U8g2. Download:
//      https://github.com/schellingb/TinySoundFont/blob/master/tsf.h
//
//  MEMORY REALITY (read this!)
//  ---------------------------
//  Mainline tsf_load_memory() expands every SF2 sample to a 32-bit float in
//  RAM. The RP2350 has ~520 KB, so the active bank's float-expanded samples
//  must fit free RAM. The default Vintage Dreams Waves bank (~314 KB) fits; a
//  large bank (e.g. the 28 MB Merlin GM) must first be SLIMMED in Polyphone.
//
//  Sf2Flash hands us the bank as a contiguous pointer + size. In the default
//  RAM mode it reads the (slimmed) bank from the external SPI flash into a heap
//  buffer; tsf_load_memory() then parses it. An advanced MMAP mode exists in
//  Sf2Flash for memory-mapped flash, but mainline tsf still copies samples to
//  float at load, so it does not by itself remove the RAM cost - a streaming
//  tsf fork would be required for a genuinely large in-place bank.
//  See the README "Flash & RAM reality" section.
// ============================================================================
#include "SoundFont.h"
#include "Sf2Flash.h"

// Pull in TinySoundFont. We DO NOT define TSF_IMPLEMENTATION here so that the
// heavy implementation can be compiled exactly once in its own translation unit
// (tsf_impl.cpp) with the build options documented above.
#if __has_include("tsf.h")
  #include "tsf.h"
  #define HAVE_TSF 1
#else
  #define HAVE_TSF 0
  #warning "tsf.h not found - add TinySoundFont to build the SoundFont engine. Engine will output silence."
#endif

namespace {

#if HAVE_TSF
  tsf* g_tsf = nullptr;
#endif
  bool        g_ready    = false;
  char        g_bankName[24] = "none";
  uint8_t     g_masterGain128 = 120;
  uint8_t     g_activeIndex = 0;

  // ---- render gate for safe live bank swaps -------------------------------
  // renderBlock() runs in the I2S DMA IRQ; serviceBankChange() runs in the
  // core1 thread. Before freeing/reloading tsf, the thread clears
  // g_renderEnabled and spins until any in-flight render leaves (g_inRender),
  // so the IRQ never touches a tsf being torn down.
  volatile bool    g_renderEnabled = true;
  volatile bool    g_inRender      = false;
  volatile bool    g_bankReq       = false;
  volatile uint8_t g_bankReqIdx    = 0;

  // --- lock-free single-producer/single-consumer MIDI ring -----------------
  // Producer: main-loop/engine (queue*). Consumer: audio IRQ (renderBlock).
  // Each message is 4 bytes. Size is a power of two for cheap masking.
  struct Msg { uint8_t type, a, b, c; };
  enum : uint8_t {
    M_NOTE_ON = 1, M_NOTE_OFF, M_CC, M_PROGRAM, M_BANK,
    M_PITCH, M_ALLOFF, M_PANIC, M_RESET
  };

  constexpr uint16_t RING_SZ = 256;           // 256 msgs in flight (power of 2)
  Msg               g_ring[RING_SZ];
  volatile uint16_t g_head = 0;               // written by producer
  volatile uint16_t g_tail = 0;               // written by consumer

  inline void push(uint8_t t, uint8_t a, uint8_t b, uint8_t c) {
    uint16_t h = g_head;
    uint16_t next = (uint16_t)((h + 1) & (RING_SZ - 1));
    if (next == g_tail) return;               // full: drop (extremely unlikely)
    g_ring[h] = { t, a, b, c };
    __sync_synchronize();                                  // publish payload before head
    g_head = next;
  }

#if HAVE_TSF
  // Apply one MIDI message to tsf. Runs in the audio context only.
  void apply(const Msg& m) {
    int ch = m.a - 1;                         // tsf channels are 0-based
    if (ch < 0) ch = 0;
    switch (m.type) {
      case M_NOTE_ON:
        tsf_channel_note_on(g_tsf, ch, m.b, m.c / 127.0f);
        break;
      case M_NOTE_OFF:
        tsf_channel_note_off(g_tsf, ch, m.b);
        break;
      case M_CC:
        tsf_channel_midi_control(g_tsf, ch, m.b, m.c);
        break;
      case M_PROGRAM:
        // drum flag set for the GM percussion channel (1-based 10 -> ch 9)
        tsf_channel_set_presetnumber(g_tsf, ch, m.b, (m.a == DRUM_CHANNEL));
        break;
      case M_BANK:
        tsf_channel_set_bank(g_tsf, ch, m.b);
        break;
      case M_PITCH: {
        int v = ((int)m.b | ((int)m.c << 7));  // 0..16383, centre 8192
        tsf_channel_set_pitchwheel(g_tsf, ch, v);
        break;
      }
      case M_ALLOFF:
        tsf_channel_note_off_all(g_tsf, ch);
        break;
      case M_PANIC:
        for (int c = 0; c < 16; ++c) tsf_channel_note_off_all(g_tsf, c);
        break;
      case M_RESET:
        // Reset every channel to a sane GM default (preset 0; ch10 = drums).
        for (int c = 0; c < 16; ++c) {
          tsf_channel_note_off_all(g_tsf, c);
          tsf_channel_set_presetnumber(g_tsf, c, 0, (c == DRUM_CHANNEL - 1));
          tsf_channel_set_bank(g_tsf, c, 0);
        }
        break;
    }
  }
#endif

} // namespace

namespace SoundFont {

bool begin() { return loadBank(SF2_DEFAULT_BANK); }

bool loadBank(uint8_t index) {
  g_ready = false;
#if HAVE_TSF
  if (g_tsf) { tsf_close(g_tsf); g_tsf = nullptr; }

  // Locate the bank in the flash directory and get a memory-mapped pointer.
  const void* base = nullptr;
  uint32_t    size = 0;
  if (!Sf2Flash::bank(index, &base, &size, g_bankName, sizeof(g_bankName)))
    return false;

  g_tsf = tsf_load_memory(base, (int)size);
  // tsf has copied the samples it needs; free the raw RAM-mode bank buffer
  // (no-op in MMAP mode) to reclaim that space before we render.
  Sf2Flash::releaseBankBuffer();
  if (!g_tsf) return false;

  tsf_set_max_voices(g_tsf, SYNTH_MAX_VOICES);
  tsf_set_output(g_tsf, TSF_STEREO_INTERLEAVED, AUDIO_SAMPLE_RATE, SYNTH_GAIN_DB);

  // GM init: every channel to preset 0, channel 10 to the drum bank.
  for (int c = 0; c < 16; ++c)
    tsf_channel_set_presetnumber(g_tsf, c, 0, (c == DRUM_CHANNEL - 1));

  setMasterGain(g_masterGain128);
  g_activeIndex = index;
  g_ready = true;
  return true;
#else
  (void)index;
  return false;
#endif
}

const char* activeBankName() { return g_bankName; }
uint8_t activeBankIndex()    { return g_activeIndex; }
uint8_t bankCount()          { return Sf2Flash::count(); }
bool ready()                 { return g_ready; }

void requestBank(uint8_t index) {
  g_bankReqIdx = index;
  g_bankReq    = true;          // picked up by serviceBankChange() on core1
}

bool serviceBankChange() {
  if (!g_bankReq) return false;
  g_bankReq = false;
  uint8_t idx = g_bankReqIdx;

  // Gate the IRQ renderer off and wait for any in-flight render to finish, so
  // tsf_close()/tsf_load_memory() never race the audio callback.
  g_renderEnabled = false;
  __sync_synchronize();
  while (g_inRender) { tight_loop_contents(); }

  loadBank(idx);               // closes old tsf, reads + parses the new bank

  __sync_synchronize();
  g_renderEnabled = true;
  return true;                 // caller forces a full settings resend
}

// ---- producer-side queue API ------------------------------------------------
void queueNoteOn (uint8_t ch, uint8_t note, uint8_t vel) { push(M_NOTE_ON,  ch, note, vel); }
void queueNoteOff(uint8_t ch, uint8_t note)              { push(M_NOTE_OFF, ch, note, 0);   }
void queueCC     (uint8_t ch, uint8_t cc, uint8_t value) { push(M_CC,       ch, cc,   value);}
void queueProgram(uint8_t ch, uint8_t program)           { push(M_PROGRAM,  ch, program, 0); }
void queueBank   (uint8_t ch, uint8_t bankMSB)           { push(M_BANK,     ch, bankMSB, 0); }
void queueAllOff (uint8_t ch)                            { push(M_ALLOFF,   ch, 0, 0);       }
void queuePanic  ()                                      { push(M_PANIC,    0, 0, 0);        }
void queueReset  ()                                      { push(M_RESET,    0, 0, 0);        }

void queuePitch(uint8_t ch, int16_t bend14) {
  int v = bend14 + 8192;
  if (v < 0) v = 0; if (v > 16383) v = 16383;
  push(M_PITCH, ch, (uint8_t)(v & 0x7F), (uint8_t)((v >> 7) & 0x7F));
}

void setMasterGain(uint8_t v0_127) {
  g_masterGain128 = v0_127;
#if HAVE_TSF
  if (g_tsf) tsf_set_volume(g_tsf, v0_127 / 127.0f);
#endif
}

// ---- consumer-side render (audio IRQ context) -------------------------------
void renderBlock(int16_t* out, int frames) {
  g_inRender = true;
  __sync_synchronize();
#if HAVE_TSF
  // Skip entirely while a bank swap is in progress (g_renderEnabled cleared) so
  // we never touch a tsf instance being torn down.
  if (g_renderEnabled && g_ready) {
    // Drain every queued event first so note-on/off align with this block.
    while (g_tail != g_head) {
      Msg m = g_ring[g_tail];
      __sync_synchronize();
      g_tail = (uint16_t)((g_tail + 1) & (RING_SZ - 1));
      apply(m);
    }
    tsf_render_short(g_tsf, out, frames, 0);   // 0 = overwrite (not mixing)
    g_inRender = false;
    return;
  }
#endif
  // No engine / mid-swap -> silence.
  for (int i = 0; i < frames * AUDIO_CHANNELS; ++i) out[i] = 0;
  g_inRender = false;
}

} // namespace SoundFont
