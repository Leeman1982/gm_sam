// ============================================================================
//  tsf_impl.cpp  -  Compile the TinySoundFont implementation exactly once.
//
//  Add TinySoundFont's "tsf.h" to the sketch folder (like U8g2 is a library).
//  This is the ONLY translation unit that defines TSF_IMPLEMENTATION, so the
//  heavy code is emitted once and the build options live in one place.
//
//  IMPORTANT - RAM: mainline TinySoundFont loads an SF2 and expands every PCM
//  sample to a 32-bit float in RAM (tsf::fontSamples). So the bank you put on
//  the flash MUST be slimmed (in Polyphone) until its float-expanded sample
//  data plus tsf overhead fits the RP2350's free RAM (budget a couple hundred
//  KB of samples). The full 28 MB OPL2/Merlin banks will NOT load as-is. See
//  the README "Flash & RAM reality" section.
// ============================================================================
#if __has_include("tsf.h")
  #define TSF_IMPLEMENTATION
  #include "tsf.h"
#endif
