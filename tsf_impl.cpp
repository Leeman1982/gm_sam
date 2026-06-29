// ============================================================================
//  tsf_impl.cpp  -  Compile the TinySoundFont implementation exactly once.
//
//  Add TinySoundFont's "tsf.h" to the sketch folder (like U8g2 is a library).
//  This is the ONLY translation unit that defines TSF_IMPLEMENTATION, so the
//  heavy code is emitted once and the build options live in one place.
//
//  IMPORTANT - RAM: mainline TinySoundFont loads an SF2 and expands every PCM
//  sample to a 32-bit float in RAM (tsf::fontSamples), so a bank's float-
//  expanded samples must fit the RP2350's free RAM. The default bank,
//  Vintage Dreams Waves (~314 KB, 130 sounds), is compact and fits. A large
//  bank (e.g. the 28 MB Merlin GM) must first be slimmed in Polyphone. See the
//  README "Flash & RAM reality" section.
// ============================================================================
#if __has_include("tsf.h")
  #define TSF_IMPLEMENTATION
  #include "tsf.h"
#endif
