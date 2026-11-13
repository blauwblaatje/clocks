#include "jamcore.h"

namespace jamcore {

JamTimer::JamTimer(const Config& cfg) : cfg_(cfg) {
    if (cfg_.undoLevels > kMaxUndo) { cfg_.undoLevels = kMaxUndo; }
}

void JamTimer::save() {
    if (cfg_.undoLevels == 0) { return; }
    undo_[undoTop_] = s_;
    undoTop_ = (undoTop_ + 1) % kMaxUndo;
    if (undoCount_ < cfg_.undoLevels) { undoCount_++; }
}

void JamTimer::enter(State st, uint32_t nowMs) {
    s_.state = st;
    s_.stateStart = nowMs;
    s_.fiveSent = false;
}

uint32_t JamTimer::periodUsed(uint32_t nowMs) const {
    return s_.periodUsed + (s_.periodRunning ? nowMs - s_.periodRunSince : 0);
}

void JamTimer::runPeriod(bool run, uint32_t nowMs) {
    if (run == s_.periodRunning) { return; }
    if (run) {
        s_.periodRunSince = nowMs;
    } else {
        s_.periodUsed = periodUsed(nowMs);
    }
    s_.periodRunning = run;
}

uint32_t JamTimer::periodRemaining(uint32_t nowMs) const {
    uint32_t used = periodUsed(nowMs);
    return used >= cfg_.periodMs ? 0 : cfg_.periodMs - used;
}

uint32_t JamTimer::jamRemaining(uint32_t nowMs) const {
    if (s_.state != State::Jam) { return cfg_.jamMs; }
    uint32_t el = nowMs - s_.stateStart;
    return el >= cfg_.jamMs ? 0 : cfg_.jamMs - el;
}

uint32_t JamTimer::lineupElapsed(uint32_t nowMs) const {
    return s_.state == State::Lineup ? nowMs - s_.stateStart : 0;
}

uint32_t JamTimer::timeoutElapsed(uint32_t nowMs) const {
    return s_.state == State::Timeout ? nowMs - s_.stateStart : 0;
}

const char* JamTimer::crownLabel() const {
    switch (s_.state) {
    case State::Jam: return "Stop Jam";
    case State::Timeout: return "End Timeout";
    case State::PeriodOver: return s_.period < cfg_.periods ? "Next Period" : "---";
    default: return "Start Jam";
    }
}

uint8_t JamTimer::startLineup(uint32_t nowMs) {
    if (s_.state != State::Idle) { return EvIgnored; }
    save();
    enter(State::Lineup, nowMs);
    return EvNone;
}

void JamTimer::resumeAt(State st, uint8_t period, uint16_t jam, uint32_t stateElapsedMs, uint32_t periodRemainingMs,
                        bool periodRunning, uint32_t nowMs) {
    s_ = Snapshot();
    s_.state = st;
    s_.period = period ? period : 1;
    s_.jam = jam;
    s_.stateStart = nowMs - stateElapsedMs;
    s_.periodUsed = periodRemainingMs >= cfg_.periodMs ? 0 : cfg_.periodMs - periodRemainingMs;
    s_.periodRunSince = nowMs;
    s_.periodRunning = periodRunning;
    // No five-second cue for a lineup that is already past it.
    s_.fiveSent = st == State::Lineup && cfg_.lineupMs >= 5000 && stateElapsedMs >= cfg_.lineupMs - 5000;
    undoCount_ = 0;
}

uint8_t JamTimer::tick(uint32_t nowMs) {
    uint8_t ev = EvNone;

    if (cfg_.jamAutoEnd && s_.state == State::Jam && jamRemaining(nowMs) == 0) {
        // The jam ends at exactly jamMs, not at the time tick() happened to run.
        uint32_t end = s_.stateStart + cfg_.jamMs;
        enter(State::Lineup, end);
        ev |= EvJamEnded;
    }

    if (s_.state == State::Lineup && !s_.fiveSent && cfg_.lineupMs >= 5000 &&
        lineupElapsed(nowMs) >= cfg_.lineupMs - 5000) {
        s_.fiveSent = true;
        ev |= EvFiveSeconds;
    }

    // A period ends when its clock is out and no jam is running. A timeout
    // stops the period clock, so it cannot expire during one.
    if (s_.state == State::Lineup && periodRemaining(nowMs) == 0) {
        runPeriod(false, nowMs);
        enter(State::PeriodOver, nowMs);
        undoCount_ = 0;
        ev |= EvPeriodOver;
    }
    return ev;
}

uint8_t JamTimer::press(Button b, uint32_t nowMs) {
    uint8_t ev = tick(nowMs);

    if (b == Button::Undo) {
        if (undoCount_ == 0) { return ev | EvIgnored; }
        undoTop_ = (undoTop_ + kMaxUndo - 1) % kMaxUndo;
        undoCount_--;
        s_ = undo_[undoTop_];
        return ev | EvUndone;
    }

    if (b == Button::Timeout) {
        if (s_.state == State::Timeout) {
            // New timeout: the timeout clock starts again.
            save();
            enter(State::Timeout, nowMs);
            return ev | EvTimeoutStarted;
        }
        if (s_.state != State::Lineup && s_.state != State::Jam) { return ev | EvIgnored; }
        save();
        if (s_.state == State::Jam) { ev |= EvJamEnded; }
        if (cfg_.periodAfterTimeout != PeriodAfterTimeout::KeepsRunning) { runPeriod(false, nowMs); }
        enter(State::Timeout, nowMs);
        return ev | EvTimeoutStarted;
    }

    // Crown
    switch (s_.state) {
    case State::Idle:
    case State::Lineup:
        save();
        s_.jam++;
        runPeriod(true, nowMs);
        enter(State::Jam, nowMs);
        return ev | EvJamStarted;
    case State::Jam:
        save();
        enter(State::Lineup, nowMs);
        return ev | EvJamEnded;
    case State::Timeout:
        // By default the period clock restarts with the next jam, as in the rules.
        save();
        if (cfg_.periodAfterTimeout == PeriodAfterTimeout::TimeoutEnd) { runPeriod(true, nowMs); }
        enter(State::Lineup, nowMs);
        return ev;
    case State::PeriodOver:
        if (s_.period >= cfg_.periods) { return ev | EvIgnored; }
    {
        // Next period: fresh period clock, jam numbering restarts.
        uint8_t next = s_.period + 1;
        s_ = Snapshot();
        s_.period = next;
        s_.stateStart = nowMs;
        undoCount_ = 0;
        return ev;
    }
    }
    return ev | EvIgnored;
}

} // namespace jamcore
