// ============================================================================
//  GMNames.h  -  General MIDI instrument & percussion name tables
//
//  Stored in the Pico's memory-mapped flash (PROGMEM) so they cost no RAM.
//  Use gmInstrumentName(p) and gmDrumName(note) to read into a small buffer.
//
//  The Dream SAM2695 follows the GM1 program map for channels 1-9,11-16 and
//  the GM percussion map (notes 35-81) on channel 10.
// ============================================================================
#pragma once
#include <Arduino.h>

// ---- 128 General MIDI melodic program names --------------------------------
static const char gmName000[] PROGMEM = "Ac Grand Piano";
static const char gmName001[] PROGMEM = "Bright Piano";
static const char gmName002[] PROGMEM = "Elec Grand Pno";
static const char gmName003[] PROGMEM = "Honky-tonk Pno";
static const char gmName004[] PROGMEM = "Elec Piano 1";
static const char gmName005[] PROGMEM = "Elec Piano 2";
static const char gmName006[] PROGMEM = "Harpsichord";
static const char gmName007[] PROGMEM = "Clavinet";
static const char gmName008[] PROGMEM = "Celesta";
static const char gmName009[] PROGMEM = "Glockenspiel";
static const char gmName010[] PROGMEM = "Music Box";
static const char gmName011[] PROGMEM = "Vibraphone";
static const char gmName012[] PROGMEM = "Marimba";
static const char gmName013[] PROGMEM = "Xylophone";
static const char gmName014[] PROGMEM = "Tubular Bells";
static const char gmName015[] PROGMEM = "Dulcimer";
static const char gmName016[] PROGMEM = "Drawbar Organ";
static const char gmName017[] PROGMEM = "Perc Organ";
static const char gmName018[] PROGMEM = "Rock Organ";
static const char gmName019[] PROGMEM = "Church Organ";
static const char gmName020[] PROGMEM = "Reed Organ";
static const char gmName021[] PROGMEM = "Accordion";
static const char gmName022[] PROGMEM = "Harmonica";
static const char gmName023[] PROGMEM = "Tango Accord";
static const char gmName024[] PROGMEM = "Ac Guitar nyl";
static const char gmName025[] PROGMEM = "Ac Guitar stl";
static const char gmName026[] PROGMEM = "Jazz Guitar";
static const char gmName027[] PROGMEM = "Clean Guitar";
static const char gmName028[] PROGMEM = "Muted Guitar";
static const char gmName029[] PROGMEM = "Overdrive Gtr";
static const char gmName030[] PROGMEM = "Distortion Gtr";
static const char gmName031[] PROGMEM = "Gtr Harmonics";
static const char gmName032[] PROGMEM = "Acoustic Bass";
static const char gmName033[] PROGMEM = "Finger Bass";
static const char gmName034[] PROGMEM = "Pick Bass";
static const char gmName035[] PROGMEM = "Fretless Bass";
static const char gmName036[] PROGMEM = "Slap Bass 1";
static const char gmName037[] PROGMEM = "Slap Bass 2";
static const char gmName038[] PROGMEM = "Synth Bass 1";
static const char gmName039[] PROGMEM = "Synth Bass 2";
static const char gmName040[] PROGMEM = "Violin";
static const char gmName041[] PROGMEM = "Viola";
static const char gmName042[] PROGMEM = "Cello";
static const char gmName043[] PROGMEM = "Contrabass";
static const char gmName044[] PROGMEM = "Tremolo Strings";
static const char gmName045[] PROGMEM = "Pizz Strings";
static const char gmName046[] PROGMEM = "Orchestral Harp";
static const char gmName047[] PROGMEM = "Timpani";
static const char gmName048[] PROGMEM = "String Ens 1";
static const char gmName049[] PROGMEM = "String Ens 2";
static const char gmName050[] PROGMEM = "Synth Strings 1";
static const char gmName051[] PROGMEM = "Synth Strings 2";
static const char gmName052[] PROGMEM = "Choir Aahs";
static const char gmName053[] PROGMEM = "Voice Oohs";
static const char gmName054[] PROGMEM = "Synth Voice";
static const char gmName055[] PROGMEM = "Orchestra Hit";
static const char gmName056[] PROGMEM = "Trumpet";
static const char gmName057[] PROGMEM = "Trombone";
static const char gmName058[] PROGMEM = "Tuba";
static const char gmName059[] PROGMEM = "Muted Trumpet";
static const char gmName060[] PROGMEM = "French Horn";
static const char gmName061[] PROGMEM = "Brass Section";
static const char gmName062[] PROGMEM = "Synth Brass 1";
static const char gmName063[] PROGMEM = "Synth Brass 2";
static const char gmName064[] PROGMEM = "Soprano Sax";
static const char gmName065[] PROGMEM = "Alto Sax";
static const char gmName066[] PROGMEM = "Tenor Sax";
static const char gmName067[] PROGMEM = "Baritone Sax";
static const char gmName068[] PROGMEM = "Oboe";
static const char gmName069[] PROGMEM = "English Horn";
static const char gmName070[] PROGMEM = "Bassoon";
static const char gmName071[] PROGMEM = "Clarinet";
static const char gmName072[] PROGMEM = "Piccolo";
static const char gmName073[] PROGMEM = "Flute";
static const char gmName074[] PROGMEM = "Recorder";
static const char gmName075[] PROGMEM = "Pan Flute";
static const char gmName076[] PROGMEM = "Blown Bottle";
static const char gmName077[] PROGMEM = "Shakuhachi";
static const char gmName078[] PROGMEM = "Whistle";
static const char gmName079[] PROGMEM = "Ocarina";
static const char gmName080[] PROGMEM = "Lead Square";
static const char gmName081[] PROGMEM = "Lead Sawtooth";
static const char gmName082[] PROGMEM = "Lead Calliope";
static const char gmName083[] PROGMEM = "Lead Chiff";
static const char gmName084[] PROGMEM = "Lead Charang";
static const char gmName085[] PROGMEM = "Lead Voice";
static const char gmName086[] PROGMEM = "Lead Fifths";
static const char gmName087[] PROGMEM = "Lead Bass+Lead";
static const char gmName088[] PROGMEM = "Pad New Age";
static const char gmName089[] PROGMEM = "Pad Warm";
static const char gmName090[] PROGMEM = "Pad Polysynth";
static const char gmName091[] PROGMEM = "Pad Choir";
static const char gmName092[] PROGMEM = "Pad Bowed";
static const char gmName093[] PROGMEM = "Pad Metallic";
static const char gmName094[] PROGMEM = "Pad Halo";
static const char gmName095[] PROGMEM = "Pad Sweep";
static const char gmName096[] PROGMEM = "FX Rain";
static const char gmName097[] PROGMEM = "FX Soundtrack";
static const char gmName098[] PROGMEM = "FX Crystal";
static const char gmName099[] PROGMEM = "FX Atmosphere";
static const char gmName100[] PROGMEM = "FX Brightness";
static const char gmName101[] PROGMEM = "FX Goblins";
static const char gmName102[] PROGMEM = "FX Echoes";
static const char gmName103[] PROGMEM = "FX Sci-Fi";
static const char gmName104[] PROGMEM = "Sitar";
static const char gmName105[] PROGMEM = "Banjo";
static const char gmName106[] PROGMEM = "Shamisen";
static const char gmName107[] PROGMEM = "Koto";
static const char gmName108[] PROGMEM = "Kalimba";
static const char gmName109[] PROGMEM = "Bagpipe";
static const char gmName110[] PROGMEM = "Fiddle";
static const char gmName111[] PROGMEM = "Shanai";
static const char gmName112[] PROGMEM = "Tinkle Bell";
static const char gmName113[] PROGMEM = "Agogo";
static const char gmName114[] PROGMEM = "Steel Drums";
static const char gmName115[] PROGMEM = "Woodblock";
static const char gmName116[] PROGMEM = "Taiko Drum";
static const char gmName117[] PROGMEM = "Melodic Tom";
static const char gmName118[] PROGMEM = "Synth Drum";
static const char gmName119[] PROGMEM = "Reverse Cymbal";
static const char gmName120[] PROGMEM = "Gtr Fret Noise";
static const char gmName121[] PROGMEM = "Breath Noise";
static const char gmName122[] PROGMEM = "Seashore";
static const char gmName123[] PROGMEM = "Bird Tweet";
static const char gmName124[] PROGMEM = "Telephone Ring";
static const char gmName125[] PROGMEM = "Helicopter";
static const char gmName126[] PROGMEM = "Applause";
static const char gmName127[] PROGMEM = "Gunshot";

static const char* const kGMNames[128] PROGMEM = {
  gmName000,gmName001,gmName002,gmName003,gmName004,gmName005,gmName006,gmName007,
  gmName008,gmName009,gmName010,gmName011,gmName012,gmName013,gmName014,gmName015,
  gmName016,gmName017,gmName018,gmName019,gmName020,gmName021,gmName022,gmName023,
  gmName024,gmName025,gmName026,gmName027,gmName028,gmName029,gmName030,gmName031,
  gmName032,gmName033,gmName034,gmName035,gmName036,gmName037,gmName038,gmName039,
  gmName040,gmName041,gmName042,gmName043,gmName044,gmName045,gmName046,gmName047,
  gmName048,gmName049,gmName050,gmName051,gmName052,gmName053,gmName054,gmName055,
  gmName056,gmName057,gmName058,gmName059,gmName060,gmName061,gmName062,gmName063,
  gmName064,gmName065,gmName066,gmName067,gmName068,gmName069,gmName070,gmName071,
  gmName072,gmName073,gmName074,gmName075,gmName076,gmName077,gmName078,gmName079,
  gmName080,gmName081,gmName082,gmName083,gmName084,gmName085,gmName086,gmName087,
  gmName088,gmName089,gmName090,gmName091,gmName092,gmName093,gmName094,gmName095,
  gmName096,gmName097,gmName098,gmName099,gmName100,gmName101,gmName102,gmName103,
  gmName104,gmName105,gmName106,gmName107,gmName108,gmName109,gmName110,gmName111,
  gmName112,gmName113,gmName114,gmName115,gmName116,gmName117,gmName118,gmName119,
  gmName120,gmName121,gmName122,gmName123,gmName124,gmName125,gmName126,gmName127
};

// ---- GM percussion names (note 35..81 on channel 10) -----------------------
// Index 0 == note 35.  Notes outside 35..81 return "---".
static const char dr35[] PROGMEM = "Ac Bass Drum";
static const char dr36[] PROGMEM = "Bass Drum 1";
static const char dr37[] PROGMEM = "Side Stick";
static const char dr38[] PROGMEM = "Ac Snare";
static const char dr39[] PROGMEM = "Hand Clap";
static const char dr40[] PROGMEM = "Elec Snare";
static const char dr41[] PROGMEM = "Low Floor Tom";
static const char dr42[] PROGMEM = "Closed HiHat";
static const char dr43[] PROGMEM = "High Floor Tom";
static const char dr44[] PROGMEM = "Pedal HiHat";
static const char dr45[] PROGMEM = "Low Tom";
static const char dr46[] PROGMEM = "Open HiHat";
static const char dr47[] PROGMEM = "Low-Mid Tom";
static const char dr48[] PROGMEM = "Hi-Mid Tom";
static const char dr49[] PROGMEM = "Crash Cymbal 1";
static const char dr50[] PROGMEM = "High Tom";
static const char dr51[] PROGMEM = "Ride Cymbal 1";
static const char dr52[] PROGMEM = "Chinese Cymbal";
static const char dr53[] PROGMEM = "Ride Bell";
static const char dr54[] PROGMEM = "Tambourine";
static const char dr55[] PROGMEM = "Splash Cymbal";
static const char dr56[] PROGMEM = "Cowbell";
static const char dr57[] PROGMEM = "Crash Cymbal 2";
static const char dr58[] PROGMEM = "Vibraslap";
static const char dr59[] PROGMEM = "Ride Cymbal 2";
static const char dr60[] PROGMEM = "Hi Bongo";
static const char dr61[] PROGMEM = "Low Bongo";
static const char dr62[] PROGMEM = "Mute Hi Conga";
static const char dr63[] PROGMEM = "Open Hi Conga";
static const char dr64[] PROGMEM = "Low Conga";
static const char dr65[] PROGMEM = "High Timbale";
static const char dr66[] PROGMEM = "Low Timbale";
static const char dr67[] PROGMEM = "High Agogo";
static const char dr68[] PROGMEM = "Low Agogo";
static const char dr69[] PROGMEM = "Cabasa";
static const char dr70[] PROGMEM = "Maracas";
static const char dr71[] PROGMEM = "Short Whistle";
static const char dr72[] PROGMEM = "Long Whistle";
static const char dr73[] PROGMEM = "Short Guiro";
static const char dr74[] PROGMEM = "Long Guiro";
static const char dr75[] PROGMEM = "Claves";
static const char dr76[] PROGMEM = "Hi Wood Block";
static const char dr77[] PROGMEM = "Low Wood Block";
static const char dr78[] PROGMEM = "Mute Cuica";
static const char dr79[] PROGMEM = "Open Cuica";
static const char dr80[] PROGMEM = "Mute Triangle";
static const char dr81[] PROGMEM = "Open Triangle";

static const char* const kDrumNames[47] PROGMEM = {
  dr35,dr36,dr37,dr38,dr39,dr40,dr41,dr42,dr43,dr44,dr45,dr46,
  dr47,dr48,dr49,dr50,dr51,dr52,dr53,dr54,dr55,dr56,dr57,dr58,
  dr59,dr60,dr61,dr62,dr63,dr64,dr65,dr66,dr67,dr68,dr69,dr70,
  dr71,dr72,dr73,dr74,dr75,dr76,dr77,dr78,dr79,dr80,dr81
};

// Copy a melodic program name (0..127) into buf.
inline void gmInstrumentName(uint8_t prog, char* buf, size_t n) {
  if (prog > 127) prog = 127;
  const char* p = (const char*)pgm_read_ptr(&kGMNames[prog]);
  strncpy_P(buf, p, n - 1);
  buf[n - 1] = '\0';
}

// Copy a percussion name for a MIDI note into buf. Notes outside 35..81 -> "---".
inline void gmDrumName(uint8_t note, char* buf, size_t n) {
  if (note < 35 || note > 81) { strncpy(buf, "---", n - 1); buf[n - 1] = '\0'; return; }
  const char* p = (const char*)pgm_read_ptr(&kDrumNames[note - 35]);
  strncpy_P(buf, p, n - 1);
  buf[n - 1] = '\0';
}

// Convert a MIDI note number to a name like "C-4" / "F#3" into buf (>=5 bytes).
inline void noteName(uint8_t note, char* buf, size_t n) {
  static const char* const names[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  int octave = (int)note / 12 - 1;          // MIDI note 60 == C4 (middle C convention)
  snprintf(buf, n, "%s%d", names[note % 12], octave);
}
