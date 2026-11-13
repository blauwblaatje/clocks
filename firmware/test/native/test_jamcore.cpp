// Host test for the standalone state machine.
// Build: g++ -std=c++17 -Ilib/jamcore test/native/test_jamcore.cpp lib/jamcore/jamcore.cpp -o /tmp/t && /tmp/t
#include "jamcore.h"
#include <cstdio>
#include <cstdlib>

using namespace jamcore;

static int failures = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);          \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static void basicJamCycle() {
    JamTimer t;
    CHECK(t.state() == State::Idle);
    CHECK(t.press(Button::Crown, 1000) & EvJamStarted);
    CHECK(t.state() == State::Jam && t.jam() == 1);
    CHECK(t.jamRemaining(31000) == 90000);
    CHECK(t.periodRemaining(31000) == 1800000 - 30000);
    CHECK(t.press(Button::Crown, 61000) & EvJamEnded);
    CHECK(t.state() == State::Lineup);
    CHECK(t.lineupElapsed(71000) == 10000);
}

static void jamExpiresAtExactlyTwoMinutes() {
    JamTimer t;
    t.press(Button::Crown, 0);
    CHECK(t.tick(119999) == EvNone);
    // tick arrives late; lineup must still be measured from 120000
    CHECK(t.tick(123000) & EvJamEnded);
    CHECK(t.state() == State::Lineup);
    CHECK(t.lineupElapsed(123000) == 3000);
}

static void fiveSecondCueFiresOnce() {
    JamTimer t;
    t.press(Button::Crown, 0);
    t.press(Button::Crown, 10000); // lineup from 10000
    CHECK(!(t.tick(34999) & EvFiveSeconds));
    CHECK(t.tick(35000) & EvFiveSeconds);
    CHECK(!(t.tick(35100) & EvFiveSeconds));
}

static void timeoutStopsPeriodClock() {
    JamTimer t;
    t.press(Button::Crown, 0);
    t.press(Button::Crown, 60000);            // lineup, period still running
    CHECK(t.press(Button::Timeout, 70000) & EvTimeoutStarted);
    CHECK(t.state() == State::Timeout);
    CHECK(t.periodRemaining(70000) == 1800000 - 70000);
    CHECK(t.periodRemaining(130000) == 1800000 - 70000); // frozen
    CHECK(t.timeoutElapsed(130000) == 60000);
    t.press(Button::Crown, 130000);           // end timeout -> lineup
    CHECK(t.state() == State::Lineup);
    CHECK(t.periodRemaining(140000) == 1800000 - 70000); // still frozen
    t.press(Button::Crown, 140000);           // jam -> period runs again
    CHECK(t.periodRemaining(150000) == 1800000 - 80000);
    CHECK(t.jam() == 2);
}

static void timeoutDuringJamEndsJam() {
    JamTimer t;
    t.press(Button::Crown, 0);
    uint8_t ev = t.press(Button::Timeout, 5000);
    CHECK((ev & EvJamEnded) && (ev & EvTimeoutStarted));
}

static void undoRestoresPreviousState() {
    JamTimer t;
    CHECK(t.press(Button::Undo, 0) & EvIgnored);
    t.press(Button::Crown, 0);
    t.press(Button::Crown, 20000);  // accidental stop
    CHECK(t.state() == State::Lineup);
    CHECK(t.press(Button::Undo, 21000) & EvUndone);
    CHECK(t.state() == State::Jam && t.jam() == 1);
    CHECK(t.jamRemaining(21000) == 120000 - 21000); // jam clock kept running
    CHECK(t.press(Button::Undo, 22000) & EvIgnored); // single level
    // undo an accidental start: jam number goes back
    t.press(Button::Crown, 30000);
    t.press(Button::Crown, 60000);
    CHECK(t.jam() == 2);
    t.press(Button::Undo, 61000);
    CHECK(t.state() == State::Lineup && t.jam() == 1);
}

static void periodEndsAfterJamNotDuring() {
    Config c;
    c.periodMs = 100000;
    JamTimer t(c);
    t.press(Button::Crown, 0);
    t.press(Button::Crown, 50000);
    t.press(Button::Crown, 60000);       // jam 2
    CHECK(t.tick(110000) == EvNone);     // period clock out, jam continues
    CHECK(t.state() == State::Jam);
    CHECK(t.periodRemaining(110000) == 0);
    uint8_t ev = t.press(Button::Crown, 115000);
    CHECK((ev & EvJamEnded));
    CHECK(t.tick(115001) & EvPeriodOver);
    CHECK(t.state() == State::PeriodOver);
    CHECK(t.press(Button::Timeout, 116000) & EvIgnored);
    t.press(Button::Crown, 200000);      // next period
    CHECK(t.state() == State::Idle && t.period() == 2 && t.jam() == 0);
    CHECK(t.periodRemaining(200000) == 100000);
    t.press(Button::Crown, 210000);
    CHECK(t.jam() == 1 && t.periodRemaining(220000) == 90000);
}

static void lastPeriodStaysOver() {
    Config c;
    c.periodMs = 1000;
    c.periods = 1;
    JamTimer t(c);
    t.press(Button::Crown, 0);
    t.press(Button::Crown, 2000);
    t.tick(2001);
    CHECK(t.state() == State::PeriodOver);
    CHECK(t.press(Button::Crown, 3000) & EvIgnored);
}

static void millisWraparound() {
    JamTimer t;
    uint32_t start = 0xFFFFFFFFu - 5000;
    t.press(Button::Crown, start);
    CHECK(t.jamRemaining(start + 10000) == 110000); // crosses the 32-bit wrap
}

static void jamWithoutAutoEnd() {
    Config c;
    c.jamAutoEnd = false;
    JamTimer t(c);
    t.press(Button::Crown, 0);
    CHECK(!(t.tick(130000) & EvJamEnded));
    CHECK(t.state() == State::Jam && t.jamRemaining(130000) == 0);
    CHECK(t.press(Button::Crown, 131000) & EvJamEnded);
}

static void periodResumesAtTimeoutEnd() {
    Config c;
    c.periodAfterTimeout = PeriodAfterTimeout::TimeoutEnd;
    JamTimer t(c);
    t.press(Button::Crown, 0);
    t.press(Button::Crown, 60000);
    t.press(Button::Timeout, 70000);
    CHECK(t.periodRemaining(130000) == 1800000 - 70000); // frozen in timeout
    t.press(Button::Crown, 130000);                      // end timeout
    CHECK(t.periodRemaining(140000) == 1800000 - 80000); // runs in lineup
    t.press(Button::Undo, 141000);                       // back into the timeout
    CHECK(t.state() == State::Timeout && t.periodRemaining(150000) == 1800000 - 70000);
}

static void periodKeepsRunningInTimeout() {
    Config c;
    c.periodAfterTimeout = PeriodAfterTimeout::KeepsRunning;
    c.periodMs = 100000;
    JamTimer t(c);
    t.press(Button::Crown, 0);
    t.press(Button::Crown, 60000);
    t.press(Button::Timeout, 70000);
    CHECK(t.periodRemaining(90000) == 10000);
    CHECK(t.tick(120000) == EvNone);        // clock out, but no period end in a timeout
    CHECK(t.state() == State::Timeout);
    t.press(Button::Crown, 125000);          // end timeout
    CHECK(t.tick(125001) & EvPeriodOver);
}

static void severalUndoLevels() {
    Config c;
    c.undoLevels = 3;
    JamTimer t(c);
    t.press(Button::Crown, 0);      // jam 1
    t.press(Button::Crown, 10000);  // lineup
    t.press(Button::Crown, 20000);  // jam 2
    t.press(Button::Crown, 30000);  // lineup
    CHECK(t.press(Button::Undo, 31000) & EvUndone);
    CHECK(t.state() == State::Jam && t.jam() == 2);
    CHECK(t.press(Button::Undo, 32000) & EvUndone);
    CHECK(t.state() == State::Lineup && t.jam() == 1);
    CHECK(t.press(Button::Undo, 33000) & EvUndone);
    CHECK(t.state() == State::Jam && t.jam() == 1);
    CHECK(t.press(Button::Undo, 34000) & EvIgnored); // only three kept
}

static void undoLevelsClampedAndOff() {
    Config c;
    c.undoLevels = 0;
    JamTimer off(c);
    off.press(Button::Crown, 0);
    CHECK(!off.canUndo() && (off.press(Button::Undo, 1000) & EvIgnored));
    c.undoLevels = 200; // clamped to kMaxUndo
    JamTimer many(c);
    for (uint32_t i = 0; i < 20; i++) { many.press(Button::Crown, i * 10000); }
    int n = 0;
    while (many.press(Button::Undo, 300000) & EvUndone) { n++; }
    CHECK(n == kMaxUndo);
}

static void newTimeoutRestartsTimeoutClock() {
    JamTimer t;
    t.press(Button::Crown, 0);
    t.press(Button::Timeout, 10000);
    CHECK(t.timeoutElapsed(25000) == 15000);
    CHECK(t.press(Button::Timeout, 25000) & EvTimeoutStarted);
    CHECK(t.state() == State::Timeout && t.timeoutElapsed(26000) == 1000);
    CHECK(t.press(Button::Undo, 26000) & EvUndone);
    CHECK(t.timeoutElapsed(26000) == 16000);
}

static void resumeFromScoreboard() {
    JamTimer t;
    // jam 7 of period 2, 30 s in, period clock at 12:00 and running
    t.resumeAt(State::Jam, 2, 7, 30000, 720000, true, 100000);
    CHECK(t.state() == State::Jam && t.period() == 2 && t.jam() == 7);
    CHECK(t.jamRemaining(100000) == 90000);
    CHECK(t.periodRemaining(110000) == 710000);
    CHECK(!t.canUndo());
    CHECK(t.press(Button::Crown, 110000) & EvJamEnded);
    CHECK(t.lineupElapsed(112000) == 2000);
    // a lineup already past 25 s gives no five-second cue
    JamTimer l;
    l.resumeAt(State::Lineup, 1, 3, 27000, 600000, false, 50000);
    CHECK(!(l.tick(50100) & EvFiveSeconds));
    CHECK(l.periodRemaining(60000) == 600000);
}

static void lineupBeforeFirstJam() {
    JamTimer t;
    CHECK(t.startLineup(1000) == EvNone);
    CHECK(t.state() == State::Lineup && t.lineupElapsed(6000) == 5000);
    CHECK(t.startLineup(7000) == EvIgnored);
    CHECK(t.press(Button::Crown, 8000) & EvJamStarted);
    CHECK(t.jam() == 1);
}

int main() {
    lineupBeforeFirstJam();
    newTimeoutRestartsTimeoutClock();
    resumeFromScoreboard();
    jamWithoutAutoEnd();
    periodResumesAtTimeoutEnd();
    periodKeepsRunningInTimeout();
    severalUndoLevels();
    undoLevelsClampedAndOff();
    basicJamCycle();
    jamExpiresAtExactlyTwoMinutes();
    fiveSecondCueFiresOnce();
    timeoutStopsPeriodClock();
    timeoutDuringJamEndsJam();
    undoRestoresPreviousState();
    periodEndsAfterJamNotDuring();
    lastPeriodStaysOver();
    millisWraparound();
    std::printf(failures ? "%d FAILED\n" : "all tests passed\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
