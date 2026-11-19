// Host test for the box timer roles (buttons/touch -> commands, view).
// Build (one line): g++ -std=c++17 -Wall -Ilib/boxcore -Ilib/crgclient -Ilib/boxctl test/native/test_boxctl.cpp
//   lib/boxcore/boxcore.cpp lib/crgclient/*.cpp lib/boxctl/boxctl.cpp -o /tmp/tx && /tmp/tx
#include "buttons.h"
#include "boxctl.h"
#include <cstdio>

using namespace box;

static int failures = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);          \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static const std::string G = "ScoreBoard.CurrentGame.";
static std::string kv(const std::string& k, const std::string& v) { return "\"" + G + k + "\":" + v; }
static std::string st(std::initializer_list<std::string> parts) {
    std::string s = "{\"state\":{";
    bool first = true;
    for (auto& p : parts) {
        s += (first ? "" : ",") + p;
        first = false;
    }
    return s + "}}";
}
static bool sent(crg::Client& c, const std::string& part) {
    for (auto& f : c.outgoing()) {
        if (f.find(part) != std::string::npos) return true;
    }
    return false;
}
static void press(Controller& k, Btn b, uint32_t now, uint32_t ms = 50) {
    k.button(b, true, now);
    k.tick(now);
    k.tick(now + ms);
    k.button(b, false, now + ms);
}

static void online(crg::Client& c) {
    c.onConnecting(0);
    c.onOpen("", 0);
    c.onFrame(R"({"state":{"WS.Device.Id":"d1"}})", 0);
    c.onFrame(R"x({"state":{"ScoreBoard.Settings.Setting(ScoreBoard.Penalties.UsePBT)":true}})x", 0);
    c.onFrame(st({kv("InJam", "true"), kv("Team(1).Name", "\"Red\""), kv("Team(2).Name", "\"Blue\""),
                  kv("Team(1).Position(Pivot).Skater", "\"s21\""), kv("Team(1).Position(Pivot).RosterNumber", "\"21\""),
                  kv("Team(1).Position(Pivot).CurrentFielding", "\"tj_Pivot\""),
                  kv("Team(1).Position(Blocker1).Skater", "\"s22\""), kv("Team(1).Position(Blocker1).RosterNumber", "\"22\""),
                  kv("Team(1).Position(Blocker1).CurrentFielding", "\"tj_Blocker1\""),
                  kv("Team(1).Position(Blocker2).Skater", "\"s23\""), kv("Team(1).Position(Blocker2).RosterNumber", "\"23\""),
                  kv("Team(1).Position(Blocker2).CurrentFielding", "\"tj_Blocker2\"")}),
              0);
    c.outgoing().clear();
}

static void trip(crg::Client& c, const std::string& prefix, const std::string& id, const std::string& number,
                 long long ms, uint32_t now, const std::string& fielding = "", bool jammer = false) {
    std::string p = prefix + "BoxTrip(" + id + ").";
    c.onFrame(st({kv(p + "IsCurrent", "true"), kv(p + "RosterNumber", "\"" + number + "\""),
                  kv(p + "Jammer", jammer ? "true" : "false"), kv(p + "CurrentSkater", number.empty() ? "\"\"" : "\"s" + number + "\""),
                  kv(p + "CurrentFielding", "\"" + fielding + "\""), kv(p + "TotalPenalties", "1"),
                  kv(p + "Clock.Time", std::to_string(ms)), kv(p + "Clock.Running", "true"),
                  kv(p + "Clock.Direction", "true")}),
              now);
}

static void standaloneBlockers() {
    boxcore::BoxTimer b;
    Controller k(Role::Blockers, 1, b, nullptr, Mode::Standalone);
    press(k, Btn::Crown, 1000);
    press(k, Btn::Crown, 3000);
    View v = k.view(11000);
    CHECK(v.slots.size() == 2 && v.slots[0].clock == "0:20" && v.slots[1].clock == "0:22");
    CHECK(v.status == "T1 blockers \xc2\xb7 standalone");
    press(k, Btn::Right, 12000); // pause
    v = k.view(20000);
    CHECK(v.paused && v.status.find("PAUSED") != std::string::npos && v.slots[0].phase == Phase::Paused);
    CHECK(v.slots[0].clock == "0:19");
    press(k, Btn::Right, 21000); // resume
    // tap the top clock, then Release
    k.touch(120, 60, 22000);
    v = k.view(22000);
    CHECK(v.screen == Screen::Menu && v.menuClock == "0:18" && !v.menuCanChange);
    k.touch(120, 110, 22100);
    CHECK(k.view(22200).slots.size() == 1);
    // undo (10 o'clock held 1 s) brings it back
    press(k, Btn::Left, 23000, 1000);
    CHECK(k.view(24100).slots.size() == 2);
    // + Penalty on the bottom clock
    k.touch(120, 200, 25000);
    k.touch(120, 150, 25100);
    v = k.view(25100);
    CHECK(v.slots[1].clock == "0:47"); // 13.1 s served (9 s paused), + 30 s
}

static void standaloneJammers() {
    boxcore::BoxTimer b;
    Controller k(Role::Jammers, 1, b, nullptr, Mode::Standalone);
    View v = k.view(0);
    CHECK(v.slots.size() == 2 && v.slots[0].key.empty() && v.emptyLeft == "Left button: sat down");
    press(k, Btn::Left, 1000);
    press(k, Btn::Right, 6000);
    v = k.view(6100);
    CHECK(v.slots[0].phase == Phase::Done && v.slots[0].swapDone);
    CHECK(v.slots[1].clock == "0:05" && v.slots[1].side == 1);
    press(k, Btn::Crown, 7000); // short crown: pause
    CHECK(k.view(9000).paused);
    press(k, Btn::Crown, 9500); // resume
    press(k, Btn::Crown, 10000, 1000); // held: undo the right jammer
    CHECK(k.view(11100).slots[1].key.empty());
}

static void connectedBlockers() {
    boxcore::BoxTimer b;
    crg::ClientConfig cfg;
    cfg.role = crg::Role::BoxBlockers;
    crg::Client c(cfg);
    Controller k(Role::Blockers, 1, b, &c, Mode::Connected);
    online(c);
    CHECK(k.view(10).status == "T1 blockers \xc2\xb7 Jam on");

    // no noted penalty: StartBoxTrip, then the picker
    press(k, Btn::Crown, 1000);
    CHECK(sent(c, "\"ScoreBoard.CurrentGame.StartBoxTrip\""));
    c.outgoing().clear();
    View v = k.view(1010);
    CHECK(v.screen == Screen::Picker && v.pick[0].label == "Pivot" && v.pick[0].number == "21");
    CHECK(v.pick[3].number == "?");
    trip(c, "", "new1", "", 30000, 1020);
    k.tick(1020);
    CHECK(k.view(1030).slots.size() == 1 && k.view(1030).slots[0].number == "?"); // own unassigned trip shown
    k.touch(80, 140, 1500); // Blocker 2
    CHECK(sent(c, R"x("key":"ScoreBoard.CurrentGame.BoxTrip(new1).CurrentFielding","value":"tj_Blocker2")x"));
    c.outgoing().clear();
    c.onFrame(st({kv("BoxTrip(new1).IsCurrent", "null")}), 1520);
    trip(c, "Team(1).", "new1", "23", 29000, 1520, "tj_Blocker2");
    k.tick(1520);
    v = k.view(1530);
    CHECK(v.slots.size() == 1 && v.slots[0].number == "23" && v.screen == Screen::Main);

    // exactly one noted penalty: assigned at once, no picker
    c.onFrame(st({kv("Team(1).Position(Blocker1).HasUnserved", "true")}), 2000);
    press(k, Btn::Crown, 2100);
    CHECK(sent(c, "\"ScoreBoard.CurrentGame.Team(1).Position(Blocker1).StartBoxClock\""));
    CHECK(k.view(2200).screen == Screen::Main);
    c.outgoing().clear();
    trip(c, "Team(1).", "t22", "22", 30000, 2210, "tj_Blocker1");
    k.tick(2210);
    // other team's and jammer trips are not shown
    trip(c, "Team(2).", "x", "9", 30000, 2220);
    trip(c, "Team(1).", "j", "10", 30000, 2220, "tj_Jammer", true);
    v = k.view(2230);
    CHECK(v.slots.size() == 2);

    // menu on #23 (top band): + Penalty, Release, undo of the release
    k.touch(120, 60, 3000);
    v = k.view(3000);
    CHECK(v.screen == Screen::Menu && v.menuNumber == "#23" && v.menuCanPenalty && v.menuCanChange);
    k.touch(120, 150, 3100);
    CHECK(sent(c, R"x("key":"ScoreBoard.CurrentGame.Team(1).Skater(s23).Penalty(2).Code","value":"?")x"));
    c.outgoing().clear();
    k.touch(120, 60, 4000);
    k.touch(120, 110, 4100);
    CHECK(sent(c, R"x("key":"ScoreBoard.CurrentGame.Team(1).Position(Blocker2).PenaltyBox","value":false)x"));
    c.outgoing().clear();
    press(k, Btn::Left, 5000, 1000);
    CHECK(sent(c, "Team(1).Position(Blocker2).UnendBoxTrip"));
    c.outgoing().clear();
    // Change skater: menu -> Change -> picker for that trip
    k.touch(120, 60, 7000);
    k.touch(80, 190, 7100);
    CHECK(k.view(7110).screen == Screen::Picker);
    k.touch(160, 90, 7200); // Blocker 1
    CHECK(sent(c, R"x("key":"ScoreBoard.CurrentGame.Team(1).BoxTrip(new1).CurrentFielding","value":"tj_Blocker1")x"));
    c.outgoing().clear();
    // swap sides: time team 2
    k.setSwapSides(true);
    CHECK(k.view(8000).status.find("T2 blockers") == 0);
}

static void connectedJammers() {
    boxcore::BoxTimer b;
    crg::ClientConfig cfg;
    cfg.role = crg::Role::BoxJammers;
    crg::Client c(cfg);
    Controller k(Role::Jammers, 1, b, &c, Mode::Connected);
    online(c);
    c.onFrame(st({kv("Team(2).StarPass", "true")}), 10);
    View v = k.view(20);
    CHECK(v.teamLeft == "Red" && v.teamRight == "Blue" && v.hint == "Crown held: undo");
    press(k, Btn::Left, 1000);
    CHECK(sent(c, "\"ScoreBoard.CurrentGame.Team(1).Position(Jammer).StartBoxClock\""));
    press(k, Btn::Right, 2000);
    CHECK(sent(c, "\"ScoreBoard.CurrentGame.Team(2).Position(Pivot).StartBoxClock\"")); // after a star pass
    c.outgoing().clear();
    trip(c, "Team(1).", "a", "10", 30000, 2010, "tj_Jammer", true);
    trip(c, "Team(2).", "b", "7", 30000, 2010, "tj_Pivot", true);
    v = k.view(2020);
    CHECK(v.slots[0].number == "10" && v.slots[1].number == "7");
    k.setSwapSides(true);
    v = k.view(2030);
    CHECK(v.slots[0].number == "7" && v.teamLeft == "Blue");
}

static void lostAndTakeOver() {
    boxcore::BoxTimer b;
    crg::ClientConfig cfg;
    cfg.role = crg::Role::BoxBlockers;
    crg::Client c(cfg);
    Controller k(Role::Blockers, 1, b, &c, Mode::Connected);
    online(c);
    trip(c, "Team(1).", "t1", "22", 12000, 100, "tj_Blocker1");
    c.onClose(1000);
    CHECK(k.view(1000).screen == Screen::NoConn);
    press(k, Btn::Crown, 1100);
    CHECK(c.outgoing().empty());
    press(k, Btn::Left, 2000, 1000);
    View v = k.view(3000);
    CHECK(k.mode() == Mode::Standalone && v.slots.size() == 1 && v.slots[0].clock == "0:12");
    CHECK(k.view(8000).slots[0].clock == "0:07");

    // The scoreboard answers again: a quiet word, the local clock runs on.
    c.onConnecting(9000);
    c.onOpen("", 9000);
    c.onFrame(st({kv("InJam", "true")}), 9010);
    trip(c, "Team(1).", "t1", "22", 4000, 9010, "tj_Blocker1");
    c.outgoing().clear(); // the Register
    k.tick(9020);
    View w = k.view(9020);
    CHECK(w.scoreboardBack && w.status.find("CRG back") != std::string::npos && k.mode() == Mode::Standalone);
    CHECK(w.screen == Screen::Main && w.slots.size() == 1);
    // 10 o'clock held 3 s: the prompt, not the menu (a clock runs)
    k.button(Btn::Left, true, 10000);
    k.tick(11100);
    CHECK(k.view(11100).hint == "Release: undo, hold: rejoin");
    k.tick(13000);
    k.button(Btn::Left, false, 13100);
    CHECK(k.view(13100).screen == Screen::RejoinPrompt && !k.takeMenuRequest());
    k.touch(160, 150, 13200); // No
    CHECK(k.view(13200).screen == Screen::Main && k.mode() == Mode::Standalone);
    press(k, Btn::Left, 14000, 3100);
    CHECK(k.view(17200).screen == Screen::RejoinPrompt);
    press(k, Btn::Crown, 17300); // a button closes it and does its job: a local skater sits
    CHECK(k.view(17400).screen == Screen::Main && k.view(17400).slots.size() == 2 && c.outgoing().empty());
    press(k, Btn::Left, 18000, 3100);
    k.touch(78, 150, 21200); // Yes
    w = k.view(21200);
    CHECK(k.mode() == Mode::Connected && w.slots.size() == 1 && w.slots[0].number == "22"); // the scoreboard's trip only
    CHECK(c.outgoing().empty());
}

static void menuHoldAndSwapPrompt() {
    // standalone blockers: menu refused while a clock runs, fine once paused
    boxcore::BoxTimer b;
    Controller k(Role::Blockers, 1, b, nullptr, Mode::Standalone);
    press(k, Btn::Crown, 0);
    k.button(Btn::Left, true, 1000);
    k.tick(2000);
    View v = k.view(2000);
    CHECK(v.holdStage == 1 && v.hint == "Release: undo, hold: menu");
    k.tick(4000);
    CHECK(!k.takeMenuRequest() && k.view(4000).notice == "No menu while a clock runs");
    k.button(Btn::Left, false, 4100);
    CHECK(k.view(4200).slots.size() == 1); // nothing undone after the 3 s hold
    CHECK(!k.requestSwap(4300));            // someone in the box
    press(k, Btn::Right, 5000);             // pause
    k.button(Btn::Left, true, 6000);
    k.tick(9000);
    CHECK(k.takeMenuRequest());
    k.button(Btn::Left, false, 9100);
    k.clearClocks();
    CHECK(k.view(9200).slots.empty() && k.requestSwap(9300) && k.swapSides());

    // connected: "Swap sides?" at intermission, once
    boxcore::BoxTimer b2;
    crg::ClientConfig cfg;
    cfg.role = crg::Role::BoxBlockers;
    crg::Client c(cfg);
    Controller kc(Role::Blockers, 1, b2, &c, Mode::Connected);
    online(c);
    c.onFrame(st({kv("InJam", "false"), kv("InPeriod", "false"), kv("Clock(Jam).Number", "14"),
                  kv("Clock(Period).Number", "1"), kv("Rule(Period.Number)", "2")}),
              10000);
    kc.tick(10000);
    CHECK(kc.view(10000).screen == Screen::SwapPrompt);
    kc.touch(80, 150, 11000); // yes
    CHECK(kc.swapSides() && kc.view(11000).screen == Screen::Main && kc.view(11000).status.find("T2 blockers") == 0);
    kc.tick(12000);
    CHECK(kc.view(12000).screen == Screen::Main); // not asked again in this intermission
}

static void releaseButton() {
    boxcore::BoxTimer b;
    crg::ClientConfig cfg;
    cfg.role = crg::Role::BoxBlockers;
    crg::Client c(cfg);
    Controller k(Role::Blockers, 1, b, &c, Mode::Connected);
    online(c);
    trip(c, "Team(1).", "a", "21", 12000, 100, "tj_Pivot");
    trip(c, "Team(1).", "b", "22", -3000, 100, "tj_Blocker1");
    trip(c, "Team(1).", "c", "23", -1000, 100, "tj_Blocker2");
    CHECK(k.view(200).hint == "Crown: sat down\n2 o'clock: release");
    press(k, Btn::Right, 1000);
    // #22 ran out first (lowest time)
    CHECK(sent(c, R"x("key":"ScoreBoard.CurrentGame.Team(1).Position(Blocker1).PenaltyBox","value":false)x"));
    c.outgoing().clear();
    c.onFrame(st({kv("Team(1).BoxTrip(b).IsCurrent", "false")}), 1020);
    k.tick(1020);
    press(k, Btn::Left, 2000, 1000); // undo the release
    CHECK(sent(c, "Team(1).Position(Blocker1).UnendBoxTrip"));
    c.outgoing().clear();
    c.onFrame(st({kv("Team(1).BoxTrip(b).IsCurrent", "true")}), 3100);
    press(k, Btn::Right, 4000);
    c.onFrame(st({kv("Team(1).BoxTrip(b).IsCurrent", "false")}), 4020);
    press(k, Btn::Right, 5000); // next: #23
    CHECK(sent(c, R"x("key":"ScoreBoard.CurrentGame.Team(1).Position(Blocker2).PenaltyBox","value":false)x"));
    c.outgoing().clear();
    c.onFrame(st({kv("Team(1).BoxTrip(c).IsCurrent", "false")}), 5020);
    press(k, Btn::Right, 6000); // only #21 left, 12 s to go: nothing sent
    CHECK(c.outgoing().empty());
    CHECK(k.view(6100).notice.find("Nobody's time is up") == 0);
    // standalone keeps pause on the same button
    boxcore::BoxTimer b2;
    Controller s(Role::Blockers, 1, b2, nullptr, Mode::Standalone);
    press(s, Btn::Crown, 0);
    press(s, Btn::Right, 1000);
    CHECK(s.view(2000).paused);
}

// A board with coloured buttons (StopWatch) names them by colour.
static void colourNames() {
    btn::gNames = {"Yellow", "Red", "Blue"};
    boxcore::BoxTimer b;
    Controller j(Role::Jammers, 1, b, nullptr, Mode::Standalone);
    View v = j.view(0);
    CHECK(v.emptyLeft == "Red button: sat down" && v.emptyRight == "Blue button: sat down");
    CHECK(v.hint == "Yellow: pause, held: undo");
    boxcore::BoxTimer b2;
    Controller k(Role::Blockers, 1, b2, nullptr, Mode::Standalone);
    CHECK(k.view(0).hint == "Yellow: sat down\nBlue: pause");
    btn::gNames = {};
    CHECK(k.view(0).hint == "Crown: sat down\n2 o'clock: pause");
}

int main() {
    releaseButton();
    colourNames();
    menuHoldAndSwapPrompt();
    standaloneBlockers();
    standaloneJammers();
    connectedBlockers();
    connectedJammers();
    lostAndTakeOver();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all boxctl tests passed\n");
    return 0;
}
