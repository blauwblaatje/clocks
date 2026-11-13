// Standalone jam timer state machine. No Arduino dependencies: time comes in
// as a millisecond argument, so this builds and tests on a PC.
#pragma once
#include <stdint.h>

namespace jamcore {

enum class State : uint8_t { Idle, Lineup, Jam, Timeout, PeriodOver };
enum class Button : uint8_t { Crown, Timeout, Undo };

// Bit flags returned by tick()/press() so the caller can drive haptics.
enum Event : uint8_t {
    EvNone = 0,
    EvFiveSeconds = 1 << 0, // 5 s before the lineup clock reaches its limit
    EvJamStarted = 1 << 1,
    EvJamEnded = 1 << 2, // by button or by the jam clock running out
    EvTimeoutStarted = 1 << 3,
    EvPeriodOver = 1 << 4,
    EvUndone = 1 << 5,
    EvIgnored = 1 << 6, // press had no effect
};

// When the period clock runs again after a timeout.
enum class PeriodAfterTimeout : uint8_t {
    NextJam,    // stopped until the next jam starts (rules)
    TimeoutEnd, // runs again as soon as the timeout ends
    KeepsRunning, // a timeout does not stop the period clock
};

constexpr uint8_t kMaxUndo = 8;

// Standalone settings, set on the settings page. Defaults follow the rules.
struct Config {
    uint32_t jamMs = 120000;
    bool jamAutoEnd = true; // the jam ends by itself when the jam clock runs out
    PeriodAfterTimeout periodAfterTimeout = PeriodAfterTimeout::NextJam;
    uint8_t undoLevels = 1; // 0 = no undo, at most kMaxUndo
    uint32_t lineupMs = 30000;
    uint32_t timeoutMs = 60000; // display reference only, timeouts do not auto-end
    uint32_t periodMs = 1800000;
    uint8_t periods = 2;
};

class JamTimer {
  public:
    explicit JamTimer(const Config& cfg = Config());

    // Call regularly. Handles jam expiry, the five-second cue and period end.
    uint8_t tick(uint32_t nowMs);
    uint8_t press(Button b, uint32_t nowMs);

    State state() const { return s_.state; }
    uint8_t period() const { return s_.period; }
    uint16_t jam() const { return s_.jam; }
    bool canUndo() const { return undoCount_ > 0; }

    // Clock values in ms. Jam and period count down, lineup and timeout count up.
    uint32_t jamRemaining(uint32_t nowMs) const;
    uint32_t lineupElapsed(uint32_t nowMs) const;
    uint32_t timeoutElapsed(uint32_t nowMs) const;
    uint32_t periodRemaining(uint32_t nowMs) const;

    // What the crown will do if pressed now, for the display.
    const char* crownLabel() const;

    // Before the first jam of a period: start the lineup clock (undo button,
    // short press). Ignored in any other state.
    uint8_t startLineup(uint32_t nowMs);

    // Take over locally from the scoreboard's last known state ("Hold Undo:
    // run locally"): state, numbers, how long the state has run, and the
    // period clock. Clears undo.
    void resumeAt(State st, uint8_t period, uint16_t jam, uint32_t stateElapsedMs, uint32_t periodRemainingMs,
                  bool periodRunning, uint32_t nowMs);

    const Config& config() const { return cfg_; }
    bool periodRunning() const { return s_.periodRunning; }

  private:
    struct Snapshot {
        State state = State::Idle;
        uint8_t period = 1;
        uint16_t jam = 0;
        uint32_t stateStart = 0;   // when the current state began
        uint32_t periodUsed = 0;   // period clock ms consumed before periodRunSince
        uint32_t periodRunSince = 0;
        bool periodRunning = false;
        bool fiveSent = false;
    };

    void save();
    void enter(State st, uint32_t nowMs);
    void runPeriod(bool run, uint32_t nowMs);
    uint32_t periodUsed(uint32_t nowMs) const;

    Config cfg_;
    Snapshot s_;
    Snapshot undo_[kMaxUndo]; // ring buffer, newest at undoTop_ - 1
    uint8_t undoTop_ = 0;
    uint8_t undoCount_ = 0;
};

} // namespace jamcore
