#include "boxcore.h"

namespace boxcore {

BoxTimer::BoxTimer(const Config& cfg) : cfg_(cfg) {
    if (cfg_.undoLevels > kMaxUndo) cfg_.undoLevels = kMaxUndo;
}

void BoxTimer::save() {
    if (cfg_.undoLevels == 0) return;
    // callers settle first, so the snapshot's times are current
    undo_[undoTop_] = s_;
    undoTop_ = (undoTop_ + 1) % kMaxUndo;
    if (undoCount_ < cfg_.undoLevels) undoCount_++;
}

bool BoxTimer::undo(uint32_t now) {
    if (undoCount_ == 0) return false;
    settle(now);
    undoTop_ = (undoTop_ + kMaxUndo - 1) % kMaxUndo;
    undoCount_--;
    // Time keeps running while undoing: a restored clock has served all the
    // running time since the snapshot, also one that was released meanwhile.
    State old = undo_[undoTop_];
    for (uint8_t i = 0; i < old.n; i++) {
        const Clock* cur = find(old.clocks[i].id);
        old.clocks[i].usedMs = cur ? cur->usedMs : old.clocks[i].usedMs + (s_.runTotal - old.runTotal);
    }
    old.paused = s_.paused;
    old.runSince = s_.runSince;
    old.runTotal = s_.runTotal;
    s_ = old;
    return true;
}

void BoxTimer::settle(uint32_t now) {
    if (!s_.paused) {
        uint32_t d = now - s_.runSince;
        for (uint8_t i = 0; i < s_.n; i++) s_.clocks[i].usedMs += d;
        s_.runTotal += d;
    }
    s_.runSince = now;
}

Clock* BoxTimer::get(uint8_t id) {
    for (uint8_t i = 0; i < s_.n; i++) {
        if (s_.clocks[i].id == id) return &s_.clocks[i];
    }
    return nullptr;
}

const Clock* BoxTimer::find(uint8_t id) const { return const_cast<BoxTimer*>(this)->get(id); }

const Clock* BoxTimer::bySide(int8_t side) const {
    for (uint8_t i = 0; i < s_.n; i++) {
        if (s_.clocks[i].side == side) return &s_.clocks[i];
    }
    return nullptr;
}

int32_t BoxTimer::remaining(const Clock& c, uint32_t now) const {
    uint32_t used = c.usedMs + (s_.paused ? 0 : now - s_.runSince);
    return (int32_t)c.maxMs - (int32_t)used;
}

Phase BoxTimer::phase(const Clock& c, uint32_t now) const {
    int32_t r = remaining(c, now);
    if (r <= 0) return Phase::Done;
    if (r <= (int32_t)cfg_.standMs) return Phase::Stand;
    return Phase::Serving;
}

// Both jammers in the box: the one who sat first is released early and the
// other serves only as long as the first had served, as CRG does
// (BoxTripImpl.itemAdded): both clocks shorten by min(first's remaining,
// second's remaining, one penalty).
void BoxTimer::jammerSwap(Clock& added, uint32_t now) {
    if (added.side < 0 || added.penalties <= added.shortened) return;
    Clock* other = nullptr;
    for (uint8_t i = 0; i < s_.n; i++) {
        if (s_.clocks[i].id != added.id && s_.clocks[i].side >= 0 && s_.clocks[i].side != added.side)
            other = &s_.clocks[i];
    }
    if (!other || other->penalties <= other->shortened) return;
    int32_t otherRem = remaining(*other, now);
    if (otherRem <= 0) return;
    int32_t ownRem = remaining(added, now);
    int32_t cut = otherRem < ownRem ? otherRem : ownRem;
    if (cut > (int32_t)cfg_.penaltyMs) cut = cfg_.penaltyMs;
    if (cut <= 0) return;
    other->shortened++;
    added.shortened = added.penalties;
    other->maxMs -= cut;
    added.maxMs -= cut;
}

uint8_t BoxTimer::sit(uint32_t now, int8_t side) {
    if (s_.n >= kMaxClocks) return 0;
    if (side >= 0 && bySide(side)) return 0; // that jammer is already in the box
    settle(now);
    save();
    Clock& c = s_.clocks[s_.n++];
    c = Clock();
    c.id = s_.nextId++;
    if (s_.nextId == 0) s_.nextId = 1;
    c.side = side;
    c.maxMs = cfg_.penaltyMs;
    jammerSwap(c, now);
    return c.id;
}

uint8_t BoxTimer::sitRemaining(uint32_t now, int8_t side, int32_t remainingMs) {
    if (s_.n >= kMaxClocks) return 0;
    settle(now);
    Clock& c = s_.clocks[s_.n++];
    c = Clock();
    c.id = s_.nextId++;
    if (s_.nextId == 0) s_.nextId = 1;
    c.side = side;
    c.maxMs = cfg_.penaltyMs;
    c.usedMs = remainingMs >= (int32_t)cfg_.penaltyMs ? 0 : (uint32_t)((int32_t)cfg_.penaltyMs - remainingMs);
    if (remainingMs > (int32_t)cfg_.penaltyMs) c.maxMs = (uint32_t)remainingMs;
    return c.id;
}

bool BoxTimer::release(uint8_t id, uint32_t now) {
    Clock* c = get(id);
    if (!c) return false;
    settle(now);
    save();
    uint8_t i = (uint8_t)(c - s_.clocks);
    for (; i + 1 < s_.n; i++) s_.clocks[i] = s_.clocks[i + 1];
    s_.n--;
    return true;
}

bool BoxTimer::cancel(uint8_t id, uint32_t now) { return release(id, now); }

bool BoxTimer::addPenalty(uint8_t id, uint32_t now) {
    Clock* c = get(id);
    if (!c) return false;
    settle(now);
    save();
    c->penalties++;
    c->maxMs += cfg_.penaltyMs;
    jammerSwap(*c, now);
    return true;
}

void BoxTimer::setPaused(bool paused, uint32_t now) {
    if (paused == s_.paused) return;
    settle(now);
    s_.paused = paused;
}

void BoxTimer::clear() {
    s_ = State();
    undoCount_ = 0;
}

} // namespace boxcore
