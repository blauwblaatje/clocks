// Host test for the jam timer controller (buttons -> commands, view).
// Build (one line): g++ -std=c++17 -Wall -Ilib/jamcore -Ilib/crgclient -Ilib/jtctl test/native/test_jtctl.cpp
//   lib/jamcore/jamcore.cpp lib/crgclient/*.cpp lib/jtctl/jtctl.cpp -o /tmp/tt && /tmp/tt
#include "jtctl.h"
#include <cstdio>

using namespace jt;

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
        if (!first) s += ",";
        first = false;
        s += p;
    }
    return s + "}}";
}
static bool sent(crg::Client& c, const std::string& part) {
    for (auto& f : c.outgoing()) {
        if (f.find(part) != std::string::npos) return true;
    }
    return false;
}

static void press(Controller& k, Btn b, uint32_t now, uint32_t holdMs = 50) {
    k.button(b, true, now);
    k.tick(now);
    k.tick(now + holdMs);
    k.button(b, false, now + holdMs);
}

// A connected client in a lineup before jam 1 (labels as captured).
static void online(crg::Client& c, uint32_t now) {
    c.onConnecting(now);
    c.onOpen("CRG_SCOREBOARD=x", now);
    c.onFrame(R"({"state":{"WS.Device.Id":"d1"}})", now);
    c.onFrame(st({kv("InJam", "false"), kv("InPeriod", "false"), kv("Label(Start)", "\"Start Jam\""),
                  kv("Label(Stop)", "\"Lineup\""), kv("Clock(Jam).Number", "0"), kv("Clock(Jam).MaximumTime", "120000"),
                  kv("Clock(Jam).Direction", "true"), kv("Clock(Period).Number", "0"),
                  kv("Clock(Period).Time", "1800000"), kv("Clock(Period).Direction", "true"),
                  kv("Team(1).Id", "\"g_1\""), kv("Team(2).Id", "\"g_2\"")}),
              now);
    c.outgoing().clear();
}

static void jamStarted(crg::Client& c, uint32_t now, int jam = 1) {
    c.onFrame(st({kv("InJam", "true"), kv("InPeriod", "true"), kv("Label(Start)", "\"---\""),
                  kv("Label(Stop)", "\"Stop Jam\""), kv("Clock(Jam).Number", std::to_string(jam)),
                  kv("Clock(Jam).Running", "true"), kv("Clock(Jam).Time", "120000"), kv("Clock(Period).Number", "1"),
                  kv("Clock(Period).Running", "true")}),
              now);
}

static void standalone() {
    jamcore::JamTimer jt;
    Controller k(jt, nullptr, Mode::Standalone);
    View v = k.view(0);
    CHECK(v.state == "READY" && v.clock == "2:00" && v.hint == "Crown: Jam \xc2\xb7 Undo: Lineup");
    press(k, Btn::Crown, 1000);
    v = k.view(31000);
    CHECK(v.state == "JAM" && v.clock == "1:30" && v.context == "P1 \xc2\xb7 Jam 1" && v.tone == Tone::Jam);
    CHECK(v.ringPermille == 750);
    CHECK(v.period == "Period 29:30");
    press(k, Btn::Crown, 61000);
    v = k.view(87000);
    CHECK(v.state == "LINEUP" && v.clock == "0:26" && v.context == "P1 \xc2\xb7 Next: Jam 2");
    CHECK(!k.view(87250).ringOn || !k.view(87000).ringOn); // flashing in the last 5 s
    // undo needs a 1 s hold
    press(k, Btn::Undo, 88000, 500);
    CHECK(k.view(88600).state == "LINEUP");
    k.button(Btn::Undo, true, 89000);
    k.tick(89500);
    CHECK(k.view(89500).holdPermille == 500);
    k.tick(90000);
    k.button(Btn::Undo, false, 90100);
    CHECK(k.view(90100).state == "JAM");
    press(k, Btn::Timeout, 91000);
    v = k.view(101000);
    CHECK(v.state == "TIMEOUT" && v.clock == "0:10" && v.period.find("stopped") != std::string::npos);
}

static void readyLineup() {
    jamcore::JamTimer jt;
    Controller k(jt, nullptr, Mode::Standalone);
    CHECK(k.view(0).hint == "Crown: Jam \xc2\xb7 Undo: Lineup");
    press(k, Btn::Undo, 1000, 300); // short press: the lineup starts on release
    View v = k.view(11300);
    CHECK(v.state == "LINEUP" && v.clock == "0:10");
    press(k, Btn::Undo, 12000, 300); // not READY any more: nothing
    CHECK(k.view(12400).state == "LINEUP");
    press(k, Btn::Crown, 13000);
    CHECK(k.view(13100).state == "JAM" && jt.jam() == 1);

    crg::Client c;
    jamcore::JamTimer jt2;
    Controller kc(jt2, &c, Mode::Connected);
    online(c, 0);
    CHECK(kc.view(10).hint == "Crown: Jam \xc2\xb7 Undo: Lineup");
    press(kc, Btn::Undo, 1000, 300);
    CHECK(sent(c, "\"ScoreBoard.CurrentGame.StopJam\""));
}

static void undoAndMenuHold() {
    jamcore::JamTimer jt;
    Controller k(jt, nullptr, Mode::Standalone);
    // READY: held 3 s opens the menu, no lineup, no undo
    k.button(Btn::Undo, true, 1000);
    k.tick(2500);
    View v = k.view(2500);
    CHECK(v.holdStage == 1 && v.holdPermille == 250 && v.hint == "Release: undo, hold: menu");
    k.tick(4000);
    CHECK(k.takeMenuRequest() && !k.takeMenuRequest());
    k.button(Btn::Undo, false, 4200);
    CHECK(k.view(4300).state == "READY");
    // in a jam: released after 1.5 s = undo; held 3 s = refused, nothing undone
    press(k, Btn::Crown, 5000);
    press(k, Btn::Crown, 6000);
    press(k, Btn::Crown, 7000); // jam 2
    k.button(Btn::Undo, true, 8000);
    k.tick(9000);
    CHECK(k.view(9000).state == "JAM"); // armed, not yet undone
    k.tick(11000);
    CHECK(!k.takeMenuRequest() && k.view(11000).hint == "No menu while a clock runs");
    k.button(Btn::Undo, false, 11100);
    CHECK(k.view(11200).state == "JAM" && jt.jam() == 2);
    k.button(Btn::Undo, true, 12000);
    k.tick(13500);
    k.button(Btn::Undo, false, 13500);
    CHECK(k.view(13600).state == "LINEUP" && jt.jam() == 1);
}

static void ejtWarning() {
    jamcore::JamTimer jt;
    crg::Client c;
    Controller k(jt, &c, Mode::Connected);
    online(c, 0);
    CHECK(!k.view(10).settingsWarning);
    c.onFrame(R"x({"state":{"ScoreBoard.Settings.Setting(ScoreBoard.AutoStart)":""}})x", 20);
    CHECK(k.view(30).settingsWarning);
}

static void connectedCrown() {
    jamcore::JamTimer jt;
    crg::Client c;
    Controller k(jt, &c, Mode::Connected);
    online(c, 0);
    View v = k.view(100);
    CHECK(v.screen == Screen::Main && v.state == "READY" && v.hint == "Crown: Jam \xc2\xb7 Undo: Lineup");
    press(k, Btn::Crown, 1000);
    CHECK(sent(c, "\"ScoreBoard.CurrentGame.StartJam\""));
    c.outgoing().clear();
    v = k.view(1010);
    CHECK(v.feedback == Feedback::Waiting);
    CHECK(v.state == "READY"); // nothing shown before the scoreboard confirms
    jamStarted(c, 1030);
    k.tick(1030);
    v = k.view(1040);
    CHECK(v.feedback == Feedback::None && v.state == "JAM" && v.hint == "Crown: Stop Jam");
    CHECK(v.hintDim); // blocked for 1 s
    press(k, Btn::Crown, 1500);
    CHECK(c.outgoing().empty()); // not sent while blocked
    CHECK(!k.view(2001).hintDim);
    press(k, Btn::Crown, 2100);
    CHECK(sent(c, "\"ScoreBoard.CurrentGame.StopJam\""));
    c.outgoing().clear();
    // nothing comes back: NOT TAKEN with two red flashes, gone after 2 s
    c.tick(3600);
    k.tick(3600);
    v = k.view(3650);
    CHECK(v.feedback == Feedback::NotTaken && v.feedbackRed);
    CHECK(!k.view(3850).feedbackRed && k.view(3850).feedback == Feedback::NotTaken);
    CHECK(k.view(5700).feedback == Feedback::None);
}

static void intermissionHold() {
    jamcore::JamTimer jt;
    crg::Client c;
    Controller k(jt, &c, Mode::Connected);
    online(c, 0);
    jamStarted(c, 10);
    // period 1 over: intermission 15:00 with 10:00 left (first half)
    c.onFrame(st({kv("InJam", "false"), kv("InPeriod", "false"), kv("Label(Start)", "\"Start Jam\""),
                  kv("Label(Stop)", "\"Lineup\""), kv("Clock(Jam).Running", "false"),
                  kv("Clock(Intermission).Running", "true"), kv("Clock(Intermission).Direction", "true"),
                  kv("Clock(Intermission).MaximumTime", "900000"), kv("Clock(Intermission).Time", "600000"),
                  kv("Rule(Period.Number)", "2")}),
              5000);
    View v = k.view(5000);
    CHECK(v.state == "INTERMISSION" && v.hint == "Hold: Extend period 1" && v.clock == "10:00");
    press(k, Btn::Crown, 6000, 600); // too short
    CHECK(c.outgoing().empty());
    k.button(Btn::Crown, true, 7000);
    k.tick(7500);
    CHECK(k.view(7500).holdPermille == 500);
    k.tick(8000);
    CHECK(sent(c, "StartJam"));
    k.button(Btn::Crown, false, 8100);
    c.outgoing().clear();
    c.onFrame(st({kv("Clock(Intermission).Time", "300000")}), 9000);
    CHECK(k.view(9000).hint == "Hold: Start period 2");
    // after the last period: always extend
    c.onFrame(st({kv("Clock(Period).Number", "2"), kv("Clock(Intermission).Running", "false")}), 9100);
    v = k.view(9100);
    CHECK(v.state == "GAME OVER" && v.hint == "Hold: Extend period 2");
}

static void timeoutPicker() {
    jamcore::JamTimer jt;
    crg::Client c;
    Controller k(jt, &c, Mode::Connected);
    online(c, 0);
    jamStarted(c, 10);
    CHECK(Controller::pickerHit(120, 120) == 5);
    CHECK(Controller::pickerHit(60, 60) == 1 && Controller::pickerHit(60, 180) == 2);
    CHECK(Controller::pickerHit(180, 60) == 3 && Controller::pickerHit(180, 180) == 4);
    CHECK(Controller::pickerHit(2, 2) == 0);
    press(k, Btn::Timeout, 2000);
    CHECK(sent(c, "\"ScoreBoard.CurrentGame.Timeout\""));
    c.outgoing().clear();
    CHECK(k.view(2010).screen == Screen::Picker);
    c.onFrame(st({kv("InJam", "false"), kv("Label(Stop)", "\"End Timeout\""), kv("Label(Timeout)", "\"New Timeout\""),
                  kv("Clock(Timeout).Running", "true"), kv("Clock(Timeout).Time", "0"), kv("Clock(Timeout).Number", "1")}),
              2020);
    k.tick(2020);
    View v = k.view(2030);
    CHECK(v.screen == Screen::Picker && v.pickerClock == "0:00");
    k.touch(60, 60, 4000);
    CHECK(sent(c, "\"ScoreBoard.CurrentGame.Team(1).Timeout\""));
    CHECK(k.view(4010).screen == Screen::Main);
    c.onFrame(st({kv("TimeoutOwner", "\"g_1\"")}), 4030);
    k.tick(4030);
    v = k.view(4040);
    CHECK(v.state == "TEAM 1 TO" && v.tone == Tone::Timeout && v.hint == "Crown: End Timeout");
}

static void lostAndTakeOver() {
    jamcore::JamTimer jt;
    crg::Client c;
    Controller k(jt, &c, Mode::Connected);
    online(c, 0);
    jamStarted(c, 10, 7);
    c.onFrame(st({kv("Clock(Jam).Time", "90000"), kv("Clock(Period).Time", "1500000")}), 30000);
    View v = k.view(30000);
    CHECK(v.state == "JAM" && v.clock == "1:30");
    c.onClose(30500);
    v = k.view(30600);
    CHECK(v.screen == Screen::NoConn && v.lastKnownClock == "1:30" && v.lastKnownWhat == "last known: Jam 7");
    press(k, Btn::Crown, 31000);
    press(k, Btn::Timeout, 31100);
    CHECK(c.outgoing().empty()); // never sent while disconnected
    k.button(Btn::Undo, true, 32000);
    k.tick(33000);
    k.button(Btn::Undo, false, 33100);
    CHECK(k.mode() == Mode::Standalone); // taken over on the release
    v = k.view(33100);
    CHECK(v.screen == Screen::Main && v.state == "JAM" && v.context == "P1 \xc2\xb7 Jam 7");
    CHECK(v.clock == "1:30"); // the local jam carries on from the last known value

    // The scoreboard answers again: only a quiet word, the local jam runs on.
    c.onConnecting(40000);
    c.onOpen("CRG_SCOREBOARD=x", 40000);
    c.onFrame(st({kv("InJam", "false"), kv("InPeriod", "true"), kv("Label(Start)", "\"Start Jam\""),
                  kv("Label(Stop)", "\"---\""), kv("Clock(Jam).Number", "8"), kv("Clock(Period).Number", "1")}),
              40010);
    c.outgoing().clear();
    k.tick(40020);
    v = k.view(40020);
    CHECK(v.screen == Screen::Main && v.scoreboardBack && k.mode() == Mode::Standalone && v.state == "JAM");
    // Undo held: 1 s undo stage says "rejoin", 3 s opens the prompt (not the menu)
    k.button(Btn::Undo, true, 41000);
    k.tick(42100);
    CHECK(k.view(42100).hint == "Release: undo, hold: rejoin");
    k.tick(44000);
    k.button(Btn::Undo, false, 44100);
    CHECK(k.view(44100).screen == Screen::Rejoin && !k.takeMenuRequest());
    // any button closes it and does its own job (here: a local timeout)
    press(k, Btn::Timeout, 45000);
    v = k.view(45100);
    CHECK(v.screen == Screen::Main && v.state != "JAM" && k.mode() == Mode::Standalone);
    CHECK(c.outgoing().empty());
    // No: stays local
    press(k, Btn::Undo, 46000, 3100);
    CHECK(k.view(49200).screen == Screen::Rejoin);
    k.touch(160, 150, 49300);
    CHECK(k.view(49300).screen == Screen::Main && k.mode() == Mode::Standalone);
    // Yes: the scoreboard's state from now on, nothing local sent
    press(k, Btn::Undo, 50000, 3100);
    k.touch(78, 150, 53200);
    v = k.view(53200);
    CHECK(k.mode() == Mode::Connected && v.screen == Screen::Main && v.context == "P1 \xc2\xb7 Next: Jam 9");
    CHECK(c.outgoing().empty());
    // lost again before a Yes: the prompt closes, the word goes
    c.onClose(54000);
    k.button(Btn::Undo, true, 54100);
    k.tick(55200);
    k.button(Btn::Undo, false, 55300);
    CHECK(k.mode() == Mode::Standalone);
    press(k, Btn::Undo, 56000, 3100);
    CHECK(k.view(59200).screen == Screen::Main && !k.view(59200).scoreboardBack); // no scoreboard: no prompt
}

int main() {
    standalone();
    readyLineup();
    ejtWarning();
    undoAndMenuHold();
    connectedCrown();
    intermissionHold();
    timeoutPicker();
    lostAndTakeOver();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all jtctl tests passed\n");
    return 0;
}
