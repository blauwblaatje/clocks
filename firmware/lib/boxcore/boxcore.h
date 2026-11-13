// Standalone penalty box clocks (HANDOVER.md section 6, "Sides, hands,
// standalone"): clocks only, no roster numbers, no jam state. A clock runs
// from the moment the skater sits until the official pauses all clocks by
// hand. Release is always explicit. No Arduino dependencies: time comes in
// as an argument.
#pragma once
#include <stdint.h>

namespace boxcore {

constexpr uint8_t kMaxClocks = 4;
constexpr uint8_t kMaxUndo = 8;

enum class Phase : uint8_t { Serving, Stand, Done };

struct Config {
    uint32_t penaltyMs = 30000; // fixed in firmware (HANDOVER section 6)
    uint32_t standMs = 10000;   // "stand" from this much left
    uint8_t undoLevels = 1;
};

struct Clock {
    uint8_t id = 0;      // stable while the clock exists, 1..255
    int8_t side = -1;    // jammer role: 0 left, 1 right; -1 for blockers
    uint8_t penalties = 1;
    uint8_t shortened = 0; // penalties shortened by a jammer swap
    uint32_t maxMs = 0;    // grows by penaltyMs per "+ Penalty", shrinks in a swap
    uint32_t usedMs = 0;   // time served before runSince
};

class BoxTimer {
  public:
    explicit BoxTimer(const Config& cfg = Config());

    // A skater sat down. Returns the new clock id, 0 if all clocks are in use.
    // side: jammer role (0 left, 1 right) triggers the jammer swap rule.
    uint8_t sit(uint32_t now, int8_t side = -1);
    // Take over a clock from the scoreboard with this much left (no swap rule).
    uint8_t sitRemaining(uint32_t now, int8_t side, int32_t remainingMs);
    bool release(uint8_t id, uint32_t now);
    bool addPenalty(uint8_t id, uint32_t now); // "+ Penalty": 30 s more
    bool cancel(uint8_t id, uint32_t now);     // trip entered by mistake
    void setPaused(bool paused, uint32_t now);
    bool paused() const { return s_.paused; }
    bool undo(uint32_t now);
    bool canUndo() const { return undoCount_ > 0; }
    void clear(); // "Clear clocks" in the menu

    uint8_t count() const { return s_.n; }
    const Clock& at(uint8_t i) const { return s_.clocks[i]; }
    const Clock* find(uint8_t id) const;
    const Clock* bySide(int8_t side) const;
    // Remaining time, negative once served (the device shows Done).
    int32_t remaining(const Clock& c, uint32_t now) const;
    Phase phase(const Clock& c, uint32_t now) const;

  private:
    struct State {
        Clock clocks[kMaxClocks];
        uint8_t n = 0;
        uint8_t nextId = 1;
        bool paused = false;
        uint32_t runSince = 0; // clocks run since this time unless paused
        uint32_t runTotal = 0; // all running time folded in so far
    };
    void save();
    void settle(uint32_t now); // fold running time into usedMs
    Clock* get(uint8_t id);
    void jammerSwap(Clock& added, uint32_t now);

    Config cfg_;
    State s_;
    State undo_[kMaxUndo];
    uint8_t undoTop_ = 0, undoCount_ = 0;
};

} // namespace boxcore
