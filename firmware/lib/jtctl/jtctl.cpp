#include "jtctl.h"
#include <cstdio>

namespace jt {

using crg::ClkIntermission;
using crg::ClkJam;
using crg::ClkLineup;
using crg::ClkPeriod;
using crg::ClkTimeout;
using crg::CrownAction;
using crg::JtCommand;
using crg::Outcome;

static const char* const kDot = "\xc2\xb7"; // ·

std::string formatClock(long long ms, bool countsDown) {
    if (ms < 0) ms = 0;
    long long s = countsDown ? (ms + 999) / 1000 : ms / 1000;
    char buf[32];
    snprintf(buf, sizeof buf, "%lld:%02lld", s / 60, s % 60);
    return buf;
}

static int permille(long long part, long long whole) {
    if (whole <= 0 || part <= 0) return 0;
    if (part >= whole) return 1000;
    return (int)(part * 1000 / whole);
}

Controller::Controller(jamcore::JamTimer& local, crg::Client* client, Mode mode, const Timing& t)
    : local_(local), client_(client), mode_(mode), t_(t) {}

int Controller::pickerHit(int x, int y) {
    int dx = x - 120, dy = y - 120;
    if (dx * dx + dy * dy <= 45 * 45) return 5;
    if (dx * dx + dy * dy > 120 * 120) return 0;
    bool left = x < 120, top = y < 120;
    return left ? (top ? 1 : 2) : (top ? 3 : 4);
}

int Controller::promptHit(int x, int y) {
    if (y < 130 || y >= 178) return -1;
    if (x >= 38 && x < 118) return 1;  // yes
    if (x >= 122 && x < 202) return 0; // no
    return -1;
}

bool Controller::scoreboardBack() const {
    return tookOver_ && mode_ == Mode::Standalone && client_ && client_->connected() && client_->synced();
}

// Back to the scoreboard after "run locally" (Rogier, 2026-11-13): only on
// the official's 3 s hold and a Yes. The local clocks are dropped, nothing
// local is sent; the screen shows the scoreboard's state from here on.
void Controller::rejoin(uint32_t) {
    mode_ = Mode::Connected;
    tookOver_ = rejoin_ = false;
    hold_ = Hold::None;
    picker_ = false;
    crownPressed_ = false;
    pressId_ = 0;
    lastOutcome_ = Outcome::Pending;
}

// ---------- connected ----------

Controller::CState Controller::cstate() const {
    const crg::GameState& g = client_->game();
    if (g.inJam) return CState::Jam;
    if (g.labelStop == "End Timeout") return CState::Timeout;
    if (!g.inPeriod && g.clock[ClkJam].number > 0) {
        return g.clock[ClkPeriod].number >= g.periods ? CState::GameOver : CState::Intermission;
    }
    if (g.clock[ClkLineup].running) return CState::Lineup;
    return CState::Idle;
}

bool Controller::crownNeedsHold() const {
    CState s = cstate();
    return (s == CState::Intermission || s == CState::GameOver) && client_->crownAction() == CrownAction::StartJam;
}

// protocol.md section 10: Start Jam in the first half of an intermission (and
// always after the last period) extends the period, in the second half it
// starts the next one.
std::string Controller::crownHoldText() const {
    const crg::GameState& g = client_->game();
    int p = g.clock[ClkPeriod].number;
    const crg::ClockState& ic = g.clock[ClkIntermission];
    bool extend = cstate() == CState::GameOver || (ic.maxMs > 0 && ic.timeMs * 2 > ic.maxMs);
    return extend ? "Hold: Extend period " + std::to_string(p) : "Hold: Start period " + std::to_string(p + 1);
}

void Controller::track(uint16_t id, uint32_t now) {
    pressId_ = id;
    pressAt_ = now;
    lastOutcome_ = Outcome::Pending;
    // NotSent comes back at once.
    for (auto& r : client_->results()) {
        if (r.id == id) {
            lastOutcome_ = r.outcome;
            outcomeAt_ = now;
        }
    }
}

void Controller::sendCrown(uint32_t now) {
    CrownAction a = client_->crownAction();
    if (a == CrownAction::None) return;
    lastCrown_ = now;
    crownPressed_ = true;
    track(client_->press(a == CrownAction::StartJam ? JtCommand::StartJam : JtCommand::StopJam, now), now);
}

void Controller::takeOver(uint32_t now) {
    const crg::GameState& g = client_->game();
    using jamcore::State;
    State st = State::Lineup;
    uint32_t el = 0;
    if (lastKnown_.state == "JAM") {
        st = State::Jam;
        long long rem = g.clock[ClkJam].timeMs;
        long long max = g.clock[ClkJam].maxMs > 0 ? g.clock[ClkJam].maxMs : 120000;
        el = (uint32_t)(max > rem ? max - rem : 0);
    } else if (lastKnown_.state == "TIMEOUT") {
        st = State::Timeout;
        el = (uint32_t)g.clock[ClkTimeout].timeMs;
    } else {
        el = (uint32_t)g.clock[ClkLineup].timeMs;
    }
    int jam = g.clock[ClkJam].number;
    local_.resumeAt(st, (uint8_t)(g.clock[ClkPeriod].number ? g.clock[ClkPeriod].number : 1), (uint16_t)jam, el,
                    (uint32_t)g.clock[ClkPeriod].timeMs, g.clock[ClkPeriod].running, now);
    mode_ = Mode::Standalone;
    tookOver_ = true;
    picker_ = false;
}

bool Controller::ready() const {
    if (mode_ == Mode::Standalone) return local_.state() == jamcore::State::Idle;
    return client_ && client_->connected() && client_->synced() && cstate() == CState::Idle &&
           client_->game().labelStop == "Lineup";
}

void Controller::startLineupIfReady(uint32_t now) {
    if (!ready()) return;
    if (mode_ == Mode::Standalone) {
        local_.startLineup(now);
    } else {
        track(client_->press(JtCommand::StopJam, now), now); // CRG's "Lineup" before the first jam
    }
}

void Controller::button(Btn b, bool down, uint32_t now) {
    if (!down) {
        // Undo button: short = lineup in READY, released after 1 s = undo,
        // held to 3 s = menu (tick), then the release does nothing.
        if (b == Btn::Undo && hold_ == Hold::Undo && !holdDone_) {
            uint32_t d = now - holdStart_;
            hold_ = Hold::None;
            if (d < t_.holdMs) startLineupIfReady(now);
            else undoAction(now);
            return;
        }
        if (hold_ != Hold::None && ((b == Btn::Undo) == (hold_ == Hold::Undo))) hold_ = Hold::None;
        return;
    }
    bool online = mode_ == Mode::Connected && client_ && client_->connected() && client_->synced();
    if (mode_ == Mode::Connected && !online) {
        // Never send or queue while disconnected; only "Hold Undo: run locally".
        if (b == Btn::Undo) {
            hold_ = Hold::Undo;
            holdStart_ = now;
            holdDone_ = false;
        }
        return;
    }
    if (mode_ == Mode::Standalone) {
        rejoin_ = false; // any button closes "Rejoin scoreboard?" and does its own job
        if (b == Btn::Undo) {
            hold_ = Hold::Undo;
            holdStart_ = now;
            holdDone_ = false;
            return;
        }
        local_.press(b == Btn::Crown ? jamcore::Button::Crown : jamcore::Button::Timeout, now);
        return;
    }
    switch (b) {
    case Btn::Crown:
        if (crownPressed_ && now - lastCrown_ < t_.crownBlockMs) return; // blocked, the hint shows it
        if (crownNeedsHold()) {
            hold_ = Hold::Crown;
            holdStart_ = now;
            holdDone_ = false;
            return;
        }
        sendCrown(now);
        if (picker_) picker_ = false;
        return;
    case Btn::Timeout:
        timeoutPressId_ = client_->press(JtCommand::Timeout, now);
        track(timeoutPressId_, now);
        picker_ = true;
        pickerSince_ = now;
        return;
    case Btn::Undo:
        hold_ = Hold::Undo;
        holdStart_ = now;
        holdDone_ = false;
        return;
    }
}

void Controller::undoAction(uint32_t now) {
    bool online = client_ && client_->connected() && client_->synced();
    if (mode_ == Mode::Standalone) {
        local_.press(jamcore::Button::Undo, now);
    } else if (!online) {
        takeOver(now);
    } else {
        track(client_->press(JtCommand::ClockReplace, now), now);
    }
}

// The menu only opens while no clock runs (HANDOVER section 6), or while
// the connection is lost (to switch mode or run setup).
bool Controller::canOpenMenu() const {
    if (mode_ == Mode::Standalone) {
        jamcore::State s = local_.state();
        return s == jamcore::State::Idle || s == jamcore::State::PeriodOver;
    }
    if (!client_ || !client_->connected() || !client_->synced()) return true;
    const crg::GameState& g = client_->game();
    if (g.inJam) return false;
    for (int c : {ClkJam, ClkLineup, ClkTimeout, ClkIntermission}) {
        if (g.clock[c].running) return false;
    }
    return true;
}

bool Controller::takeMenuRequest() {
    bool r = menuRequested_;
    menuRequested_ = false;
    return r;
}

void Controller::resetLocalGame() { local_ = jamcore::JamTimer(local_.config()); }

void Controller::touch(int x, int y, uint32_t now) {
    if (rejoin_) {
        int h = promptHit(x, y);
        if (h == 1) rejoin(now);
        else if (h == 0) rejoin_ = false;
        return;
    }
    if (!picker_ || mode_ != Mode::Connected || !client_) return;
    static const JtCommand cmds[] = {JtCommand::Team1Timeout, JtCommand::Team1Review, JtCommand::Team2Timeout,
                                     JtCommand::Team2Review, JtCommand::OfficialTimeout};
    int h = pickerHit(x, y);
    if (h == 0) return;
    track(client_->press(cmds[h - 1], now), now);
    picker_ = false;
}

void Controller::tick(uint32_t now) {
    if (mode_ == Mode::Standalone) local_.tick(now);
    if (hold_ == Hold::Crown && !holdDone_ && now - holdStart_ >= t_.holdMs) {
        holdDone_ = true;
        bool online = client_ && client_->connected() && client_->synced();
        if (online && crownNeedsHold()) sendCrown(now);
    }
    if (hold_ == Hold::Undo && !holdDone_ && now - holdStart_ >= t_.menuMs) {
        holdDone_ = true;
        if (scoreboardBack()) {
            rejoin_ = true;
            rejoinSince_ = now;
        } else if (canOpenMenu()) {
            menuRequested_ = true;
        } else {
            menuRefusedAt_ = now;
        }
    }
    if (rejoin_ && (!scoreboardBack() || now - rejoinSince_ >= 20000)) rejoin_ = false;
    if (client_) {
        for (auto& r : client_->results()) {
            if (r.id == pressId_) {
                lastOutcome_ = r.outcome;
                outcomeAt_ = now;
            }
            if (r.id == timeoutPressId_ && r.outcome != Outcome::Confirmed) picker_ = false;
        }
        client_->results().clear();
    }
    if (picker_) {
        bool inTimeout = client_ && cstate() == CState::Timeout;
        if (now - pickerSince_ >= t_.pickerIdleMs || (!inTimeout && lastOutcome_ == Outcome::Confirmed && now - pickerSince_ > 3000))
            picker_ = false;
    }
}

void Controller::viewConnected(View& v, uint32_t now) const {
    const crg::GameState& g = client_->game();
    int p = g.clock[ClkPeriod].number ? g.clock[ClkPeriod].number : 1;
    int jam = g.clock[ClkJam].number;
    std::string P = "P" + std::to_string(p) + " " + kDot + " ";
    const crg::ClockState& pc = g.clock[ClkPeriod];
    v.period = "Period " + formatClock(client_->displayMs(pc, now), true);
    CrownAction a = client_->crownAction();
    v.hint = std::string("Crown: ") + (a == CrownAction::StopJam ? g.labelStop : a == CrownAction::StartJam ? "Start Jam" : "---");
    switch (cstate()) {
    case CState::Jam: {
        const crg::ClockState& c = g.clock[ClkJam];
        long long ms = client_->displayMs(c, now);
        v.context = P + "Jam " + std::to_string(jam);
        v.clock = formatClock(ms, true);
        v.state = "JAM";
        v.tone = Tone::Jam;
        v.ringPermille = permille(ms, c.maxMs > 0 ? c.maxMs : 120000);
        break;
    }
    case CState::Timeout: {
        long long ms = client_->displayMs(g.clock[ClkTimeout], now);
        v.context = P + "Next: Jam " + std::to_string(jam + 1);
        v.clock = formatClock(ms, false);
        v.tone = Tone::Timeout;
        v.ringPermille = permille(ms, t_.timeoutRingMs);
        const std::string& o = g.timeoutOwner;
        if (g.officialReview) v.state = "REVIEW";
        else if (o == "O") v.state = "OFFICIAL TO";
        else if (!o.empty() && o == g.team[0].id) v.state = "TEAM 1 TO";
        else if (!o.empty() && o == g.team[1].id) v.state = "TEAM 2 TO";
        else v.state = "TIMEOUT";
        if (!pc.running) v.period += " stopped";
        break;
    }
    case CState::Intermission:
    case CState::GameOver: {
        bool over = cstate() == CState::GameOver;
        const crg::ClockState& ic = g.clock[ClkIntermission];
        v.context = "After P" + std::to_string(p);
        v.clock = over ? formatClock(0, true) : formatClock(client_->displayMs(ic, now), true);
        v.state = over ? "GAME OVER" : "INTERMISSION";
        v.tone = Tone::Intermission;
        v.ringPermille = over ? 0 : permille(client_->displayMs(ic, now), ic.maxMs);
        if (a == CrownAction::StartJam) v.hint = crownHoldText();
        break;
    }
    case CState::Lineup:
    case CState::Idle: {
        const crg::ClockState& c = g.clock[ClkLineup];
        long long ms = client_->displayMs(c, now);
        v.context = P + "Next: Jam " + std::to_string(jam + 1);
        if (cstate() == CState::Idle) {
            v.clock = formatClock(g.clock[ClkJam].maxMs > 0 ? g.clock[ClkJam].maxMs : 120000, true);
            v.state = "READY";
            v.tone = Tone::Idle;
            v.ringPermille = 0;
            if (g.labelStop == "Lineup") v.hint = "Crown: Jam \xc2\xb7 Undo: Lineup";
        } else {
            v.clock = formatClock(ms, false);
            v.state = "LINEUP";
            v.tone = Tone::Lineup;
            v.ringPermille = permille(ms, t_.lineupMs);
            if (ms >= (long long)t_.lineupMs - 5000) v.ringOn = (now / 250) % 2 == 0;
        }
        break;
    }
    }
    // Press feedback
    if (lastOutcome_ == Outcome::Pending && pressId_ != 0) {
        v.feedback = Feedback::Waiting;
    } else if ((lastOutcome_ == Outcome::NoChange || lastOutcome_ == Outcome::Refused ||
                lastOutcome_ == Outcome::NotSent) &&
               now - outcomeAt_ < t_.notTakenMs) {
        v.feedback = lastOutcome_ == Outcome::NoChange ? Feedback::NotTaken : Feedback::NotAllowed;
        uint32_t d = now - outcomeAt_;
        v.feedbackRed = d < 800 && (d / 200) % 2 == 0; // two red flashes
    }
    if (!client_->mayWrite()) v.feedback = Feedback::NotAllowed;
    v.settingsWarning = !client_->ejtDifferences().empty();
    if (crownPressed_ && now - lastCrown_ < t_.crownBlockMs) v.hintDim = true;
}

void Controller::viewLocal(View& v, uint32_t now) const {
    using jamcore::State;
    const jamcore::Config& cfg = local_.config();
    std::string P = "P" + std::to_string(local_.period()) + " " + kDot + " ";
    v.period = "Period " + formatClock(local_.periodRemaining(now), true);
    v.hint = std::string("Crown: ") + local_.crownLabel();
    switch (local_.state()) {
    case State::Jam: {
        uint32_t ms = local_.jamRemaining(now);
        v.context = P + "Jam " + std::to_string(local_.jam());
        v.clock = formatClock(ms, true);
        v.state = "JAM";
        v.tone = Tone::Jam;
        v.ringPermille = permille(ms, cfg.jamMs);
        break;
    }
    case State::Lineup: {
        uint32_t ms = local_.lineupElapsed(now);
        v.context = P + "Next: Jam " + std::to_string(local_.jam() + 1);
        v.clock = formatClock(ms, false);
        v.state = "LINEUP";
        v.tone = Tone::Lineup;
        v.ringPermille = permille(ms, cfg.lineupMs);
        if (ms + 5000 >= cfg.lineupMs) v.ringOn = (now / 250) % 2 == 0;
        break;
    }
    case State::Timeout: {
        uint32_t ms = local_.timeoutElapsed(now);
        v.context = P + "Next: Jam " + std::to_string(local_.jam() + 1);
        v.clock = formatClock(ms, false);
        v.state = "TIMEOUT";
        v.tone = Tone::Timeout;
        v.ringPermille = permille(ms, cfg.timeoutMs);
        if (!local_.periodRunning()) v.period += " stopped";
        break;
    }
    case State::PeriodOver:
        v.context = "After P" + std::to_string(local_.period());
        v.clock = formatClock(0, true);
        v.state = local_.period() >= cfg.periods ? "GAME OVER" : "PERIOD OVER";
        v.tone = Tone::Intermission;
        break;
    case State::Idle:
        v.context = P + "Next: Jam " + std::to_string(local_.jam() + 1);
        v.clock = formatClock(cfg.jamMs, true);
        v.state = "READY";
        v.tone = Tone::Idle;
        v.hint = "Crown: Jam \xc2\xb7 Undo: Lineup";
        break;
    }
    if (!local_.canUndo()) {
        // nothing to show; undo is a hold, the hint stays on the crown
    }
}

View Controller::view(uint32_t now) const {
    View v;
    v.mode = mode_;
    bool online = mode_ == Mode::Connected && client_ && client_->connected() && client_->synced();
    if (mode_ == Mode::Connected && !online) {
        v.screen = Screen::NoConn;
        if (haveLastKnown_) {
            v.lastKnownClock = lastKnown_.clock;
            v.lastKnownWhat = "last known: " + lastKnown_.context.substr(lastKnown_.context.find(kDot) == std::string::npos
                                                                             ? 0
                                                                             : lastKnown_.context.find(kDot) + 3);
        } else {
            v.lastKnownClock = "-:--";
            v.lastKnownWhat = "no game state yet";
        }
    } else if (mode_ == Mode::Connected) {
        viewConnected(v, now);
        lastKnown_ = v;
        haveLastKnown_ = true;
        if (picker_) {
            v.screen = Screen::Picker;
            v.pickerClock = cstate() == CState::Timeout ? v.clock : "0:00";
        }
    } else {
        viewLocal(v, now);
        v.scoreboardBack = scoreboardBack();
        if (rejoin_) v.screen = Screen::Rejoin;
    }
    if (hold_ != Hold::None && !holdDone_) {
        uint32_t d = now - holdStart_;
        if (hold_ == Hold::Undo && d >= t_.holdMs) {
            // undo is armed; keep holding for the menu
            v.holdStage = 1;
            v.holdPermille = permille(d - t_.holdMs, t_.menuMs - t_.holdMs);
            v.hint = scoreboardBack() ? "Release: undo, hold: rejoin" : "Release: undo, hold: menu";
        } else {
            v.holdPermille = permille(d, t_.holdMs);
        }
    }
    if (menuRefusedAt_ && now - menuRefusedAt_ < t_.notTakenMs) v.hint = "No menu while a clock runs";
    return v;
}

} // namespace jt
