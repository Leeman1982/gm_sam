#pragma once
// ============================================================================
//  Medusa SAM  --  ui.h
//  SH1106 OLED interface driven by the encoder + 5 buttons (core0).
//
//  Control scheme (SHIFT = hold the SHIFT button) -- identical to Medusa GM:
//    PLAY              play / stop          SHIFT+PLAY   continue (resume)
//    PAGE              next view            SHIFT+PAGE   previous view
//    TRACK             next track           SHIFT+TRACK  previous track
//    TRACK (long)      clear current track row
//    encoder rotate    context value / cursor
//    encoder push      context action (toggle step / next field)
//    encoder long      clear current track row (STEP view)
//    SHIFT + rotate    tempo (BPM), from any view
//    REC (hold)+rotate STEP: edit the selected per-step field
//    REC (tap)         STEP: toggle accent  SHIFT+REC   toggle tie
//                      lists: audition / execute the [action] row
//    SHIFT + enc push  STEP: cycle per-step field NOTE/VEL/GATE/PROB/MICRO/RAT
//
//  Views: STEP, INST, SHAPE, MIX, FX, MASTER, XPRT, SONG.
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "model.h"
#include "engine.h"
#include "controls.h"
#include "storage.h"

enum class View : uint8_t { STEP, INST, SHAPE, MIX, FX, MASTER, XPRT, SONG, NUM };

// Per-step field selected for REC-hold editing on the STEP view.
enum StepField : uint8_t { SF_NOTE = 0, SF_VEL, SF_GATE, SF_PROB, SF_MICRO,
                           SF_RATCH, SF_COUNT };

class UI {
public:
    void begin(Song* song, Engine* eng, Controls* ctl);
    void handleInput();
    void render();               // self-throttled; call every loop

private:
    Song*     _song = nullptr;
    Engine*   _eng  = nullptr;
    Controls* _ctl  = nullptr;

    View    _view    = View::STEP;
    uint8_t _track   = 0;
    uint8_t _cursor  = 0;         // STEP view step cursor
    uint8_t _stepField = SF_NOTE;
    uint8_t _field   = 0;         // list-view cursor
    uint8_t _scroll  = 0;         // list-view scroll offset
    uint8_t _editPat = 0;         // pattern selected on the SONG view
    uint8_t _copySrc = 0;
    uint8_t _slot    = 0;
    bool    _recEditing = false;  // REC-rotate happened (suppress tap action)

    // XPRT staging values
    uint8_t _xCcCh = 1,  _xCcNum = 91, _xCcVal = 64;
    uint8_t _xNrCh = 1,  _xNrMsb = 0x01, _xNrLsb = 0x20, _xNrVal = 64;
    uint8_t _xGsA1 = 0x40, _xGsA2 = 0x01, _xGsA3 = 0x33, _xGsVal = 64;

    char     _toast[22] = {0};
    uint32_t _toastUntil = 0;
    uint32_t _lastDraw   = 0;

    void toast(const char* msg);
    void nextTrack(int d);
    void clearTrackRow();
    void audition();
    void saveLoad(bool load);
    Pattern& soundingPat();       // pattern the engine is playing (STEP edits)

    void onStepView(int enc, bool shift);
    void onListView(int enc, bool shift);   // INST/SHAPE/FX/MASTER/XPRT/SONG
    void onMixView(int enc, bool shift);

    // list-view plumbing: each view provides rows + edit + action callbacks
    uint8_t listRowCount();
    void    listRowText(uint8_t row, char* buf, size_t n);
    bool    listRowIsAction(uint8_t row);
    void    listRowEdit(uint8_t row, int enc);
    void    listRowAction(uint8_t row);

    void drawHeader();
    void drawStepView();
    void drawMixView();
    void drawListView();

    const char* trackLabel(uint8_t t, char* buf, size_t n);
};
