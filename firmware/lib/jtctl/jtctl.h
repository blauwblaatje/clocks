// Jam timer role: button handling and what the screen shows, for standalone
// (jamcore) and connected (crg::Client) mode. No LVGL or Arduino: the screen
// code (lib/jtui) draws a View, so all behaviour here is host-testable.
// Behaviour: HANDOVER.md section 6, docs/display.md, docs/protocol.md 7 and 10.
#pragma once
#include <stdint.h>
#include <string>

#include "crgclient.h"
#include "jamcore.h"

namespace jt {

enum class Btn : uint8_t { Crown, Timeout, Undo };
enum class Mode : uint8_t { Standalone, Connected };
enum class Screen : uint8_t { Main, Picker, NoConn, Rejoin };
enum class Tone : uint8_t { Jam, Lineup, Timeout, Intermission, Idle };
enum class Feedback : uint8_t { None, Waiting, NotTaken, NotAllowed };

struct View {
    Screen screen = Screen::Main;
    Mode mode = Mode::Standalone;
    std::string context; // "P1 · Jam 7"
    std::string clock;   // "1:42"
    std::string state;   // "JAM"
    Tone tone = Tone::Idle;
    int ringPermille = 0; // 0..1000, ring progress
    bool ringOn = true;   // false during the off phase of the five-second flash
    long long ringTotalMs = 0; // the full time ringPermille is a part of (faces mark their dials with it)
    bool ringCountsUp = false; // the clock counts up (lineup, timeout): ringPermille is time used, not time left
    int periodPermille = -1;   // period clock left, 0..1000; -1 = not known
    std::string period;   // "Period 24:10" (" stopped" in a timeout)
    std::string hint;     // "Crown: Stop Jam"
    bool hintDim = false; // crown blocked for 1 s after a press
    int holdPermille = 0; // progress of a hold the hint asks for (0 = none)
    int holdStage = 0;    // undo button: 0 = towards undo (1 s), 1 = towards the menu (3 s)
    Feedback feedback = Feedback::None;
    bool settingsWarning = false; // CRG's eJT settings differ (protocol.md section 8)
    bool feedbackRed = false; // red phase of the "not taken" flash
    // Picker
    std::string pickerClock;
    // No connection
    std::string lastKnownClock, lastKnownWhat;
    // Running locally after "run locally" and the scoreboard answers again:
    // "CRG back" in the status pill; the 3 s hold offers to rejoin.
    bool scoreboardBack = false;
};

struct Timing {
    uint32_t crownBlockMs = 1000;   // connected: crown blocked after a press
    uint32_t holdMs = 1000;         // crown hold in an intermission, undo hold
    uint32_t menuMs = 3000;         // undo button held this long: menu
    uint32_t notTakenMs = 2000;     // how long "NOT TAKEN" stays
    uint32_t pickerIdleMs = 15000;  // picker closes by itself
    uint32_t lineupMs = 30000;      // connected lineup length for ring and flash
    uint32_t timeoutRingMs = 60000;
};

class Controller {
  public:
    Controller(jamcore::JamTimer& local, crg::Client* client, Mode mode, const Timing& t = Timing());

    Mode mode() const { return mode_; }
    void setMode(Mode m) {
        mode_ = m;
        tookOver_ = rejoin_ = false;
    }

    void button(Btn b, bool down, uint32_t now);
    // Touch in display coordinates (0..239), only used on the picker.
    void touch(int x, int y, uint32_t now);
    // Call often: holds, results of presses, standalone clock events.
    void tick(uint32_t now);
    View view(uint32_t now) const;

    // Menu: the undo button held 3 s asks for it (once); allowed while no
    // clock runs, the intermission clock excepted (Rogier, 2026-11-19). The
    // device layer opens the menu.
    bool takeMenuRequest();
    bool canOpenMenu() const;
    // No clock runs, intermission included: role changes, updates and
    // automatic sleep wait for this, so none of them happens mid-intermission.
    bool idle() const;
    void resetLocalGame(); // menu: standalone game reset

    // Picker hit test, shared with the screen code: 0 none, 1 T1 TTO,
    // 2 T1 OR, 3 T2 TTO, 4 T2 OR, 5 OTO.
    static int pickerHit(int x, int y);
    // "Rejoin scoreboard?" prompt (layout as the box timer's prompts):
    // 1 yes, 0 no, -1 none.
    static int promptHit(int x, int y);
    // Ran locally after a lost connection and the scoreboard answers again.
    bool scoreboardBack() const;

  private:
    bool stopped(bool intermissionToo) const; // canOpenMenu, idle
    enum class Hold : uint8_t { None, Crown, Undo };
    enum class CState : uint8_t { Idle, Lineup, Jam, Timeout, Intermission, GameOver };

    // connected
    CState cstate() const;
    bool crownNeedsHold() const;
    std::string crownHoldText() const;
    void sendCrown(uint32_t now);
    void track(uint16_t id, uint32_t now);
    void takeOver(uint32_t now);
    void rejoin(uint32_t now);
    void undoAction(uint32_t now);
    bool ready() const;
    void startLineupIfReady(uint32_t now);
    void viewConnected(View& v, uint32_t now) const;
    void viewLocal(View& v, uint32_t now) const;

    jamcore::JamTimer& local_;
    crg::Client* client_;
    Mode mode_;
    Timing t_;

    Hold hold_ = Hold::None;
    uint32_t holdStart_ = 0;
    bool holdDone_ = false;
    uint32_t lastCrown_ = 0;
    bool crownPressed_ = false;
    bool picker_ = false;
    uint32_t pickerSince_ = 0;
    uint16_t pressId_ = 0; // last press waiting for a result
    uint32_t pressAt_ = 0;
    crg::Outcome lastOutcome_ = crg::Outcome::Pending;
    uint32_t outcomeAt_ = 0;
    uint16_t timeoutPressId_ = 0;
    bool menuRequested_ = false;
    uint32_t menuRefusedAt_ = 0;
    bool tookOver_ = false;   // standalone because of "run locally", not by choice
    bool rejoin_ = false;     // "Rejoin scoreboard?" open
    uint32_t rejoinSince_ = 0;
    mutable View lastKnown_;
    mutable bool haveLastKnown_ = false;
};

std::string formatClock(long long ms, bool countsDown);

} // namespace jt
