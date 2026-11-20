// Box timer roles (blockers of one team, or both jammers): buttons and touch
// to commands, and what the screen shows. Standalone uses boxcore, connected
// uses crg::Client (docs/protocol.md section 12). No LVGL or Arduino.
// Behaviour: HANDOVER.md section 6, docs/display.md.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

#include "boxcore.h"
#include "crgclient.h"

namespace box {

enum class Role : uint8_t { Blockers, Jammers };
enum class Mode : uint8_t { Standalone, Connected };
// Left = 10 o'clock, Right = 2 o'clock. Blockers: Right = release the first
// skater whose time is up (connected) or pause/resume (standalone).
enum class Btn : uint8_t { Crown, Left, Right };
int32_t teamColour(const crg::TeamState& t); // "#rrggbb" -> 0xrrggbb, -1 = none
enum class Screen : uint8_t { Main, Picker, Menu, NoConn, SwapPrompt, RejoinPrompt };
enum class Phase : uint8_t { Serving, Stand, Done, Waiting, Paused }; // Waiting: between jams (connected)
enum class Feedback : uint8_t { None, Waiting, NotTaken, NotAllowed };

struct Slot {
    std::string key;    // what a tap refers to: trip id, or standalone clock id
    std::string number; // roster number; "?" unknown; "" standalone
    std::string clock;  // "0:23"
    Phase phase = Phase::Serving;
    bool swapDone = false; // jammer whose time was cut by a swap and is served
    int side = -1;         // jammers: 0 left, 1 right
};

struct PickItem {
    std::string label;  // "Blocker 1" or "Penalty"
    std::string number; // roster number or "?"
    bool unserved = false;
    bool inBox = false;
};

struct View {
    Screen screen = Screen::Main;
    Role role = Role::Blockers;
    Mode mode = Mode::Standalone;
    std::string status;   // "T1 blockers · Jam on"
    bool paused = false;  // standalone: all clocks paused by hand
    std::vector<Slot> slots; // blockers: in sit order; jammers: left, right (empty key = nobody)
    std::string hint;     // "Crown: skater sat down"
    std::string notice;   // short message over the screen for 2 s ("" = none)
    std::string emptyLeft, emptyRight; // jammers: text on an empty half
    std::string teamLeft, teamRight;   // jammers: team chips
    // Team colours from CRG (0xrrggbb; -1 = none): the jammer halves, and the
    // team a blockers role times. Standalone: none.
    int32_t colourLeft = -1, colourRight = -1, colourTeam = -1;
    int holdPermille = 0;
    int holdStage = 0; // undo hold: 0 = towards undo (1 s), 1 = towards the menu (3 s)
    Feedback feedback = Feedback::None;
    // Picker ("Who sat down?")
    std::string pickTitle;
    PickItem pick[4];
    // Menu (tap a clock)
    std::string menuNumber, menuClock;
    bool menuCanPenalty = true, menuCanChange = false;
    // Running locally after "run locally" and the scoreboard answers again:
    // "CRG back" in the status line; the 3 s hold offers to rejoin.
    bool scoreboardBack = false;
};

struct Timing {
    uint32_t holdMs = 1000;     // undo hold
    uint32_t menuMs = 3000;     // undo held this long: menu
    uint32_t notTakenMs = 2000;
    uint32_t menuIdleMs = 10000;
};

class Controller {
  public:
    // team: 1 or 2 for the blocker role (the team being timed). For the
    // jammer role, left = team 1 unless swapSides.
    Controller(Role role, int team, boxcore::BoxTimer& local, crg::Client* client, Mode mode,
               const Timing& t = Timing());

    Mode mode() const { return mode_; }
    void setMode(Mode m) {
        mode_ = m;
        screen_ = Screen::Main;
        tookOver_ = false;
    }
    // Ran locally after a lost connection and the scoreboard answers again.
    bool scoreboardBack() const;
    void setSwapSides(bool s) { swap_ = s; } // jammers: flip left/right; blockers: time the other team
    bool swapSides() const { return swap_; }

    void button(Btn b, bool down, uint32_t now);
    // Touch in display coordinates (0..239).
    void touch(int x, int y, uint32_t now);
    void tick(uint32_t now);
    View view(uint32_t now) const;

    // Menu: the undo gesture held 3 s asks for it (once), only while no box
    // clock runs. Side swaps are refused while a clock runs or someone is in
    // the box (HANDOVER section 6).
    bool takeMenuRequest();
    bool canOpenMenu(uint32_t now) const;
    bool canSwap(uint32_t now) const;
    bool requestSwap(uint32_t now); // false = refused
    void clearClocks();             // menu, standalone
    // "Swap sides?" and "Rejoin scoreboard?" prompts: 1 yes, 0 no, -1 none.
    static int promptHit(int x, int y);

    // Hit tests shared with the screen code.
    // Main screen: index into View::slots, or -1.
    static int slotHit(const View& v, int x, int y);
    // Picker: 0..3 item, 4 "Decide later", -1 none.
    static int pickHit(int x, int y);
    // Menu: 0 Release, 1 + Penalty, 2 Change, 3 Cancel, -1 none.
    static int menuHit(int x, int y);

  private:
    struct Act { // last own action, for undo in connected mode
        enum Kind : uint8_t { None, Sat, Released, Penalty } kind = None;
        std::string trip;
        int team = 0;
        crg::PosId pos = crg::PosJammer;
        std::string skater;
        int penaltyNumber = 0;
    };

    bool anyClockRunning(uint32_t now) const;
    int team() const;                 // blocker role: the team being timed
    int teamOfSide(int side) const;   // jammer role
    bool online() const;
    void blockersSat(uint32_t now);
    void jammerSat(int side, uint32_t now);
    void openPicker(const std::string& trip, uint32_t now);
    void pickPosition(int item, uint32_t now);
    void menuAction(int item, uint32_t now);
    void undo(uint32_t now);
    void releaseFirstDone(uint32_t now);
    void takeOver(uint32_t now);
    void rejoin(uint32_t now);
    void track(uint16_t id, uint32_t now);
    void pickPositions(crg::PosId out[4]) const;
    std::vector<Slot> connectedSlots(uint32_t now) const;
    std::vector<Slot> localSlots(uint32_t now) const;

    Role role_;
    int team_;
    boxcore::BoxTimer& local_;
    crg::Client* client_;
    Mode mode_;
    Timing t_;
    bool swap_ = false;

    bool holding_ = false;
    Btn holdBtn_ = Btn::Crown;
    uint32_t holdStart_ = 0;
    bool holdDone_ = false;

    Screen screen_ = Screen::Main;
    uint32_t screenSince_ = 0;
    std::string menuKey_;     // slot the menu is for
    std::string pickTrip_;    // trip the picker assigns ("" = the one being created)
    bool pickForNewTrip_ = false;
    int pendingPick_ = -1;    // chosen before the new trip's id was known
    std::vector<std::string> knownGameTrips_; // unassigned trips before our StartBoxTrip
    std::vector<std::string> ownTrips_;       // unassigned trips this device created

    uint16_t pressId_ = 0;
    crg::Outcome lastOutcome_ = crg::Outcome::Pending;
    uint32_t outcomeAt_ = 0;
    Act last_;
    bool menuRequested_ = false;
    uint32_t menuRefusedAt_ = 0;
    uint32_t nobodyDoneAt_ = 0;
    int promptedPeriod_ = -1;
    bool tookOver_ = false; // standalone because of "run locally", not by choice
};

std::string formatBox(int32_t ms); // remaining time, "0:00" once served

} // namespace box
