#include "boxctl.h"
#include "buttons.h"
#include <algorithm>
#include <cstdio>

namespace box {

using crg::Outcome;
using crg::PosId;

static const char* const kDot = "\xc2\xb7";

std::string formatBox(int32_t ms) {
    if (ms < 0) ms = 0;
    int32_t s = (ms + 999) / 1000;
    char buf[32];
    snprintf(buf, sizeof buf, "%d:%02d", (int)(s / 60), (int)(s % 60));
    return buf;
}

static const char* posLabel(PosId p) {
    switch (p) {
    case crg::PosJammer: return "Jammer";
    case crg::PosPivot: return "Pivot";
    case crg::PosBlocker1: return "Blocker 1";
    case crg::PosBlocker2: return "Blocker 2";
    case crg::PosBlocker3: return "Blocker 3";
    default: return "?";
    }
}

Controller::Controller(Role role, int team, boxcore::BoxTimer& local, crg::Client* client, Mode mode, const Timing& t)
    : role_(role), team_(team == 2 ? 2 : 1), local_(local), client_(client), mode_(mode), t_(t) {}

int Controller::team() const { return swap_ ? 3 - team_ : team_; }
int Controller::teamOfSide(int side) const { return (side == 0) != swap_ ? 1 : 2; }

bool Controller::online() const {
    return mode_ == Mode::Connected && client_ && client_->connected() && client_->synced();
}

void Controller::track(uint16_t id, uint32_t now) {
    pressId_ = id;
    lastOutcome_ = Outcome::Pending;
    for (auto& r : client_->results()) {
        if (r.id == id) {
            lastOutcome_ = r.outcome;
            outcomeAt_ = now;
        }
    }
}

// ---------- hit tests (layout shared with lib/boxui) ----------

int Controller::slotHit(const View& v, int x, int y) {
    int dx = x - 120, dy = y - 120;
    if (dx * dx + dy * dy > 120 * 120) return -1;
    int n = (int)v.slots.size();
    if (v.role == Role::Jammers) {
        int side = x < 120 ? 0 : 1;
        for (int i = 0; i < n; i++) {
            if (v.slots[i].side == side && !v.slots[i].key.empty()) return i;
        }
        return -1;
    }
    if (n == 0) return -1;
    if (n == 1) return 0;
    if (n == 2) return y < 120 ? 0 : 1;
    return y < 76 ? 0 : (y < 164 ? 1 : 2);
}

int Controller::pickHit(int x, int y) {
    if (y >= 68 && y < 118) {
        if (x >= 42 && x < 118) return 0;
        if (x >= 123 && x < 198) return 1;
    }
    if (y >= 123 && y < 173) {
        if (x >= 42 && x < 118) return 2;
        if (x >= 123 && x < 198) return 3;
    }
    if (y >= 180 && y < 212 && x >= 60 && x < 180) return 4;
    return -1;
}

int Controller::menuHit(int x, int y) {
    if (x >= 40 && x < 200 && y >= 92 && y < 130) return 0;
    if (x >= 40 && x < 200 && y >= 134 && y < 172) return 1;
    if (y >= 176 && y < 206) {
        if (x >= 52 && x < 119) return 2;
        if (x >= 121 && x < 188) return 3;
    }
    return -1;
}

// ---------- connected ----------

void Controller::pickPositions(PosId out[4]) const {
    // Blockers and pivot; after a star pass the pivot is the jammer and the
    // old jammer is a blocker.
    bool sp = client_ && client_->game().team[team() - 1].starPass;
    out[0] = sp ? crg::PosJammer : crg::PosPivot;
    out[1] = crg::PosBlocker1;
    out[2] = crg::PosBlocker2;
    out[3] = crg::PosBlocker3;
}

void Controller::blockersSat(uint32_t now) {
    if (!online()) {
        local_.sit(now);
        return;
    }
    const crg::TeamState& ts = client_->game().team[team() - 1];
    PosId ps[4];
    pickPositions(ps);
    int noted = -1, count = 0;
    for (int i = 0; i < 4; i++) {
        const crg::PositionState& p = ts.pos[ps[i]];
        if (p.hasUnserved && !p.penaltyBox && !p.skater.empty()) {
            noted = i;
            count++;
        }
    }
    if (count == 1) {
        // Exactly one skater with a noted penalty: assign at once, no picker.
        track(client_->startBoxClock(team(), ps[noted], now), now);
        last_ = Act();
        last_.kind = Act::Sat;
        last_.team = team();
        last_.pos = ps[noted];
        return;
    }
    knownGameTrips_.clear();
    for (auto& t : client_->game().trips) {
        if (t.team == 0) knownGameTrips_.push_back(t.id);
    }
    track(client_->startBoxTrip(now), now);
    last_ = Act();
    last_.kind = Act::Sat;
    openPicker("", now);
    pickForNewTrip_ = true;
}

void Controller::jammerSat(int side, uint32_t now) {
    if (!online()) {
        local_.sit(now, (int8_t)side);
        return;
    }
    int tm = teamOfSide(side);
    PosId p = client_->jammerPos(tm);
    track(client_->startBoxClock(tm, p, now), now);
    last_ = Act();
    last_.kind = Act::Sat;
    last_.team = tm;
    last_.pos = p;
}

void Controller::openPicker(const std::string& trip, uint32_t now) {
    screen_ = Screen::Picker;
    screenSince_ = now;
    pickTrip_ = trip;
    pickForNewTrip_ = false;
    pendingPick_ = -1;
}

void Controller::pickPosition(int item, uint32_t now) {
    if (item == 4) { // decide later: the trip stays unassigned ("?")
        screen_ = Screen::Main;
        return;
    }
    if (pickTrip_.empty()) {
        pendingPick_ = item; // the new trip's id is not known yet; tick() assigns it
        screen_ = Screen::Main;
        return;
    }
    PosId ps[4];
    pickPositions(ps);
    track(client_->assignTrip(pickTrip_, team(), ps[item], now), now);
    screen_ = Screen::Main;
}

void Controller::menuAction(int item, uint32_t now) {
    std::string key = menuKey_;
    screen_ = Screen::Main;
    if (!online()) {
        int id = atoi(key.c_str());
        if (item == 0) local_.release((uint8_t)id, now);
        else if (item == 1) local_.addPenalty((uint8_t)id, now);
        else if (item == 3) local_.cancel((uint8_t)id, now);
        return;
    }
    const crg::BoxTripState* t = client_->trip(key);
    if (!t) return;
    last_ = Act();
    last_.trip = key;
    last_.team = t->team;
    PosId p;
    if (client_->tripPosition(*t, p)) last_.pos = p;
    if (item == 0) {
        track(client_->release(key, now), now);
        last_.kind = Act::Released;
    } else if (item == 1) {
        last_.kind = Act::Penalty;
        last_.skater = t->currentSkater;
        last_.penaltyNumber = t->totalPenalties + 1;
        track(client_->addPenalty(key, now), now);
    } else if (item == 2 && role_ == Role::Blockers) {
        openPicker(key, now);
    } else if (item == 3) {
        track(client_->cancelTrip(key, now), now);
        last_.kind = Act::None; // a deleted trip cannot be brought back
    }
}

void Controller::undo(uint32_t now) {
    if (!online()) {
        if (mode_ == Mode::Connected) takeOver(now);
        else local_.undo(now);
        return;
    }
    switch (last_.kind) {
    case Act::Sat: {
        // Cancel the trip that sat last (if CRG has it by now).
        const crg::BoxTripState* found = nullptr;
        for (auto& t : client_->game().trips) {
            if (!t.isCurrent) continue;
            PosId p;
            if (!last_.trip.empty() ? t.id == last_.trip
                                    : (t.team == last_.team && client_->tripPosition(t, p) && p == last_.pos))
                found = &t;
        }
        if (found) track(client_->cancelTrip(found->id, now), now);
        break;
    }
    case Act::Released: track(client_->unendTrip(last_.team, last_.pos, now), now); break;
    case Act::Penalty: {
        std::string k = "Team(" + std::to_string(last_.team) + ").Skater(" + last_.skater + ").Penalty(" +
                        std::to_string(last_.penaltyNumber) + ").Remove";
        track(client_->set(k, "true", "", {"Team(*).BoxTrip(" + last_.trip + ").PenaltyCodes"}, now), now);
        break;
    }
    case Act::None: break;
    }
    last_ = Act();
}

// Connection lost, "Hold: run locally": carry the clocks on as standalone.
void Controller::takeOver(uint32_t now) {
    std::vector<Slot> s = connectedSlots(now);
    local_.clear();
    for (auto& sl : s) {
        const crg::BoxTripState* t = client_->trip(sl.key);
        if (!t || !t->hasClock) continue;
        local_.sitRemaining(now, role_ == Role::Jammers ? (int8_t)sl.side : -1, (int32_t)t->clock.timeMs);
    }
    mode_ = Mode::Standalone;
    tookOver_ = true;
    screen_ = Screen::Main;
}

bool Controller::scoreboardBack() const {
    return tookOver_ && mode_ == Mode::Standalone && client_ && client_->connected() && client_->synced();
}

// Back to the scoreboard after "run locally" (Rogier, 2026-11-13): only on
// the official's 3 s hold and a Yes. The local clocks are dropped and
// nothing local is sent: skaters seated while the connection was down are
// not on the scoreboard, the box timer seats them again if needed.
void Controller::rejoin(uint32_t) {
    local_.clear();
    mode_ = Mode::Connected;
    tookOver_ = false;
    screen_ = Screen::Main;
    last_ = Act();
    pressId_ = 0;
    lastOutcome_ = Outcome::Pending;
    pickTrip_.clear();
    pickForNewTrip_ = false;
    pendingPick_ = -1;
}

// ---------- input ----------

void Controller::button(Btn b, bool down, uint32_t now) {
    // Jammers: left/right = that side's jammer sat, crown short = pause,
    // crown held = undo. Blockers: crown = sat, Right (2 o'clock) = pause,
    // Left (10 o'clock) held = undo.
    bool undoBtn = role_ == Role::Jammers ? b == Btn::Crown : b == Btn::Left;
    if (!down) {
        if (holding_ && holdBtn_ == b) {
            // short = pause (jammers' crown, standalone); released after 1 s =
            // undo; held to 3 s = menu (tick), then the release does nothing.
            uint32_t d = now - holdStart_;
            bool done = holdDone_;
            holding_ = false;
            if (done) return;
            if (d >= t_.holdMs) {
                undo(now);
            } else if (role_ == Role::Jammers && b == Btn::Crown && !online() && mode_ == Mode::Standalone) {
                local_.setPaused(!local_.paused(), now);
            }
        }
        return;
    }
    if (mode_ == Mode::Connected && !online()) {
        if (undoBtn) {
            holding_ = true;
            holdBtn_ = b;
            holdStart_ = now;
            holdDone_ = false;
        }
        return; // never sent while disconnected
    }
    if (undoBtn) {
        if (screen_ == Screen::RejoinPrompt) screen_ = Screen::Main; // any button closes it
        holding_ = true;
        holdBtn_ = b;
        holdStart_ = now;
        holdDone_ = false;
        return;
    }
    if (screen_ != Screen::Main) screen_ = Screen::Main;
    if (role_ == Role::Jammers) {
        jammerSat(b == Btn::Left ? 0 : 1, now);
    } else if (b == Btn::Crown) {
        blockersSat(now);
    } else if (b == Btn::Right && mode_ == Mode::Standalone) {
        local_.setPaused(!local_.paused(), now);
    } else if (b == Btn::Right) {
        releaseFirstDone(now);
    }
}

// Blocker role, connected: the 2 o'clock button releases the skater whose
// time ran out first (CRG's box clock goes below zero, so the lowest time).
// Nobody done: nothing is sent; early releases stay a tap on the clock.
void Controller::releaseFirstDone(uint32_t now) {
    std::vector<Slot> mine = connectedSlots(now);
    const crg::BoxTripState* best = nullptr;
    for (auto& s : mine) {
        const crg::BoxTripState* t = client_->trip(s.key);
        if (!t || !t->hasClock || t->clock.timeMs > 0) continue;
        if (!best || t->clock.timeMs < best->clock.timeMs) best = t;
    }
    if (!best) {
        nobodyDoneAt_ = now;
        return;
    }
    menuKey_ = best->id;
    menuAction(0, now); // Release, recorded for undo like the menu's
}

bool Controller::anyClockRunning(uint32_t now) const {
    if (mode_ == Mode::Standalone || !online()) {
        if (mode_ == Mode::Connected) return false; // connection lost: the menu is needed
        if (local_.paused()) return false;
        for (uint8_t i = 0; i < local_.count(); i++) {
            if (local_.remaining(local_.at(i), now) > 0) return true;
        }
        return false;
    }
    for (auto& t : client_->game().trips) {
        if (t.isCurrent && t.hasClock && t.clock.running) return true;
    }
    return false;
}

bool Controller::canOpenMenu(uint32_t now) const { return !anyClockRunning(now); }

bool Controller::canSwap(uint32_t now) const {
    if (anyClockRunning(now)) return false;
    bool someone = online() ? !connectedSlots(now).empty() : local_.count() > 0;
    return !someone;
}

bool Controller::takeMenuRequest() {
    bool r = menuRequested_;
    menuRequested_ = false;
    return r;
}

bool Controller::requestSwap(uint32_t now) {
    if (!canSwap(now)) return false;
    swap_ = !swap_;
    return true;
}

void Controller::clearClocks() { local_.clear(); }

int Controller::promptHit(int x, int y) {
    if (y < 130 || y >= 178) return -1;
    if (x >= 38 && x < 118) return 1; // yes
    if (x >= 122 && x < 202) return 0; // no
    return -1;
}

void Controller::touch(int x, int y, uint32_t now) {
    if (mode_ == Mode::Connected && !online()) return;
    if (screen_ == Screen::RejoinPrompt) {
        int h = promptHit(x, y);
        if (h == 1) rejoin(now);
        else if (h == 0) screen_ = Screen::Main;
        return;
    }
    if (screen_ == Screen::SwapPrompt) {
        int h = promptHit(x, y);
        if (h < 0) return;
        if (h == 1) requestSwap(now);
        screen_ = Screen::Main;
        return;
    }
    if (screen_ == Screen::Picker) {
        int h = pickHit(x, y);
        if (h >= 0) pickPosition(h, now);
        return;
    }
    if (screen_ == Screen::Menu) {
        int h = menuHit(x, y);
        if (h < 0) {
            screen_ = Screen::Main; // tap outside closes
            return;
        }
        if (h == 1) {
            View v = view(now);
            if (!v.menuCanPenalty) return;
        }
        if (h == 2 && (role_ != Role::Blockers || mode_ != Mode::Connected)) return;
        menuAction(h, now);
        return;
    }
    View v = view(now);
    int i = slotHit(v, x, y);
    if (i < 0) return;
    menuKey_ = v.slots[i].key;
    screen_ = Screen::Menu;
    screenSince_ = now;
}

void Controller::tick(uint32_t now) {
    if (holding_ && !holdDone_ && now - holdStart_ >= t_.menuMs) {
        holdDone_ = true;
        if (scoreboardBack()) {
            screen_ = Screen::RejoinPrompt;
            screenSince_ = now;
        } else if (canOpenMenu(now)) {
            menuRequested_ = true;
        } else {
            menuRefusedAt_ = now;
        }
    }
    if (screen_ == Screen::RejoinPrompt && (!scoreboardBack() || now - screenSince_ >= 20000)) screen_ = Screen::Main;
    // "Swap sides?" once per intermission, when a swap is allowed.
    if (online() && screen_ == Screen::Main) {
        const crg::GameState& g = client_->game();
        int p = g.clock[crg::ClkPeriod].number;
        bool interm = !g.inPeriod && g.clock[crg::ClkJam].number > 0 && p < g.periods;
        if (interm && p != promptedPeriod_ && canSwap(now)) {
            promptedPeriod_ = p;
            screen_ = Screen::SwapPrompt;
            screenSince_ = now;
        }
    }
    if (client_) {
        for (auto& r : client_->results()) {
            if (r.id == pressId_) {
                lastOutcome_ = r.outcome;
                outcomeAt_ = now;
            }
        }
        client_->results().clear();
        // Our StartBoxTrip: find the new unassigned trip.
        if (pickForNewTrip_ || pendingPick_ >= 0) {
            for (auto& t : client_->game().trips) {
                if (t.team != 0 || !t.isCurrent) continue;
                if (std::find(knownGameTrips_.begin(), knownGameTrips_.end(), t.id) != knownGameTrips_.end()) continue;
                knownGameTrips_.push_back(t.id);
                ownTrips_.push_back(t.id);
                if (last_.kind == Act::Sat && last_.trip.empty()) last_.trip = t.id; // undo cancels this one
                if (screen_ == Screen::Picker && pickTrip_.empty()) pickTrip_ = t.id;
                if (pendingPick_ >= 0) {
                    pickTrip_ = t.id;
                    int p = pendingPick_;
                    pendingPick_ = -1;
                    Screen keep = screen_;
                    pickPosition(p, now);
                    screen_ = keep;
                }
                pickForNewTrip_ = false;
                break;
            }
        }
    }
    if (screen_ == Screen::SwapPrompt && now - screenSince_ >= 20000) screen_ = Screen::Main;
    if (screen_ == Screen::Menu) {
        bool exists = false;
        for (auto& s : view(now).slots) exists |= s.key == menuKey_;
        if (!exists || now - screenSince_ >= t_.menuIdleMs) screen_ = Screen::Main;
    }
}

// ---------- view ----------

std::vector<Slot> Controller::connectedSlots(uint32_t now) const {
    std::vector<Slot> out;
    const crg::GameState& g = client_->game();
    for (auto& t : g.trips) {
        if (!t.isCurrent) continue;
        bool mine;
        if (role_ == Role::Jammers) {
            mine = t.jammer && t.team != 0;
        } else if (t.team == 0) {
            mine = std::find(ownTrips_.begin(), ownTrips_.end(), t.id) != ownTrips_.end();
        } else {
            mine = t.team == team() && !t.jammer;
        }
        if (!mine) continue;
        Slot s;
        s.key = t.id;
        s.number = t.rosterNumber.empty() ? "?" : t.rosterNumber;
        long long ms = t.hasClock ? client_->displayMs(t.clock, now) : 0;
        s.clock = formatBox((int32_t)ms);
        if (ms <= 0) s.phase = Phase::Done;
        else if (t.timingStopped) s.phase = Phase::Paused;
        else if (!t.clock.running && !g.inJam) s.phase = Phase::Waiting;
        else if (ms <= 10000) s.phase = Phase::Stand;
        else s.phase = Phase::Serving;
        s.swapDone = t.shortened > 0 && ms <= 0;
        if (role_ == Role::Jammers) s.side = (t.team == 1) != swap_ ? 0 : 1;
        out.push_back(s);
    }
    return out;
}

std::vector<Slot> Controller::localSlots(uint32_t now) const {
    std::vector<Slot> out;
    for (uint8_t i = 0; i < local_.count(); i++) {
        const boxcore::Clock& c = local_.at(i);
        Slot s;
        s.key = std::to_string(c.id);
        int32_t r = local_.remaining(c, now);
        s.clock = formatBox(r);
        boxcore::Phase p = local_.phase(c, now);
        s.phase = p == boxcore::Phase::Done ? Phase::Done : local_.paused() ? Phase::Paused
                                         : p == boxcore::Phase::Stand ? Phase::Stand : Phase::Serving;
        s.swapDone = c.shortened > 0 && r <= 0;
        s.side = c.side;
        out.push_back(s);
    }
    return out;
}

View Controller::view(uint32_t now) const {
    View v;
    v.role = role_;
    v.mode = mode_;
    if (mode_ == Mode::Connected && !online()) {
        v.screen = Screen::NoConn;
        return v;
    }
    bool conn = mode_ == Mode::Connected;
    v.slots = conn ? connectedSlots(now) : localSlots(now);
    v.paused = !conn && local_.paused();
    std::string what = role_ == Role::Jammers ? "Jammers" : "T" + std::to_string(team()) + " blockers";
    if (conn) {
        const crg::GameState& g = client_->game();
        v.status = what + " " + kDot + " " + (g.inJam ? "Jam on" : "Jam off");
    } else {
        v.scoreboardBack = scoreboardBack();
        v.status = what + " " + kDot + " " + (v.paused ? "PAUSED" : v.scoreboardBack ? "CRG back" : "standalone");
    }
    if (role_ == Role::Jammers) {
        // one slot per side, left first; empty key = nobody in that half
        std::vector<Slot> sides(2);
        sides[0].side = 0;
        sides[1].side = 1;
        for (auto& s : v.slots) {
            if (s.side == 0 || s.side == 1) sides[s.side] = s;
        }
        v.slots = sides;
        v.emptyLeft = btn::left("Left") + " button: sat down";
        v.emptyRight = btn::right("Right") + " button: sat down";
        v.teamLeft = "TEAM " + std::to_string(teamOfSide(0));
        v.teamRight = "TEAM " + std::to_string(teamOfSide(1));
        if (conn) {
            const crg::GameState& g = client_->game();
            if (!g.team[teamOfSide(0) - 1].name.empty()) v.teamLeft = g.team[teamOfSide(0) - 1].name;
            if (!g.team[teamOfSide(1) - 1].name.empty()) v.teamRight = g.team[teamOfSide(1) - 1].name;
        }
        v.hint = conn ? btn::crown() + " held: undo" : btn::crown() + ": pause, held: undo";
    } else {
        v.hint = btn::crown() + ": sat down\n" + btn::right() + (conn ? ": release" : ": pause");
    }
    if (conn) {
        if (lastOutcome_ == Outcome::Pending && pressId_ != 0) {
            v.feedback = Feedback::Waiting;
        } else if ((lastOutcome_ == Outcome::NoChange || lastOutcome_ == Outcome::Refused ||
                    lastOutcome_ == Outcome::NotSent) &&
                   now - outcomeAt_ < t_.notTakenMs) {
            v.feedback = lastOutcome_ == Outcome::NoChange ? Feedback::NotTaken : Feedback::NotAllowed;
        }
        if (!client_->mayWrite()) v.feedback = Feedback::NotAllowed;
        if (!client_->game().usePBT) v.hint = "Penalty box timing is off in CRG";
    }
    if (holding_ && !holdDone_) {
        uint32_t d = now - holdStart_;
        if (d >= t_.holdMs) {
            v.holdStage = 1;
            uint32_t m = d - t_.holdMs, span = t_.menuMs - t_.holdMs;
            v.holdPermille = m >= span ? 1000 : (int)(m * 1000 / span);
            v.hint = scoreboardBack() ? "Release: undo, hold: rejoin" : "Release: undo, hold: menu";
        } else {
            v.holdPermille = (int)(d * 1000 / t_.holdMs);
        }
    }
    if (menuRefusedAt_ && now - menuRefusedAt_ < t_.notTakenMs) v.notice = "No menu while a clock runs";
    if (nobodyDoneAt_ && now - nobodyDoneAt_ < t_.notTakenMs) v.notice = "Nobody's time is up\nTap a clock to release early";

    v.screen = screen_;
    if (screen_ == Screen::Picker) {
        v.pickTitle = "Who sat down?";
        PosId ps[4];
        const_cast<Controller*>(this)->pickPositions(ps);
        const crg::TeamState& ts = client_->game().team[team() - 1];
        for (int i = 0; i < 4; i++) {
            const crg::PositionState& p = ts.pos[ps[i]];
            v.pick[i].number = p.rosterNumber.empty() ? "?" : p.rosterNumber;
            v.pick[i].unserved = p.hasUnserved && !p.penaltyBox;
            v.pick[i].inBox = p.penaltyBox;
            v.pick[i].label = v.pick[i].unserved ? "Penalty" : posLabel(ps[i]);
        }
    } else if (screen_ == Screen::Menu) {
        for (auto& s : v.slots) {
            if (s.key != menuKey_) continue;
            v.menuNumber = s.number.empty() ? "" : "#" + s.number;
            v.menuClock = s.clock;
        }
        v.menuCanChange = conn && role_ == Role::Blockers;
        if (conn) {
            const crg::BoxTripState* t = client_->trip(menuKey_);
            v.menuCanPenalty = t && t->team != 0 && !t->currentSkater.empty();
        }
    }
    return v;
}

} // namespace box
