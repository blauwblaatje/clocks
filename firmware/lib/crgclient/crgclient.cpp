#include "crgclient.h"
#include <cstring>

namespace crg {

namespace {

const char* const kGame = "ScoreBoard.CurrentGame.";
const size_t kGameLen = 23;
const char* const kUsePBT = "ScoreBoard.Settings.Setting(ScoreBoard.Penalties.UsePBT)";
const char* const kSettingPrefix = "ScoreBoard.Settings.Setting(ScoreBoard.";
// The eJT's global settings and the values its "eJT defaults" button sets
// (protocol.md section 8).
const char* const kEjtNames[5] = {"AutoStart", "AutoStart5", "Auto5", "AutoEndJam", "AutoEndTTO"};
const char* const kEjtValues[5] = {"Timeout", "Jam", "true", "false", "true"};
const char* const kPosNames[PosCount] = {"Jammer", "Pivot", "Blocker1", "Blocker2", "Blocker3"};
const char* const kClockNames[ClkCount] = {"Period", "Jam", "Lineup", "Timeout", "Intermission"};

bool startsWith(const std::string& s, const char* p, size_t& rest) {
    size_t n = strlen(p);
    if (s.compare(0, n, p) != 0) return false;
    rest = n;
    return true;
}

// "Name(id)." at s[i]: id into out, i moved past the dot (or to the end).
bool takeId(const std::string& s, size_t& i, const char* name, std::string& out) {
    size_t n = strlen(name);
    if (s.compare(i, n, name) != 0 || i + n >= s.size() || s[i + n] != '(') return false;
    size_t close = s.find(')', i + n + 1);
    if (close == std::string::npos) return false;
    out = s.substr(i + n + 1, close - i - n - 1);
    i = close + 1;
    if (i < s.size() && s[i] == '.') i++;
    return true;
}

// Pattern match: "*" matches one id (up to the next ')'); the pattern may be
// a prefix of the key.
bool watchMatches(const std::string& pat, const std::string& key) {
    size_t p = 0, k = 0;
    while (p < pat.size()) {
        if (pat[p] == '*') {
            while (k < key.size() && key[k] != ')') k++;
            p++;
            continue;
        }
        if (k >= key.size() || key[k] != pat[p]) return false;
        p++;
        k++;
    }
    return true;
}

void urlEncode(std::string& out, const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
}

std::string teamPrefix(int team) { return std::string("Team(") + (char)('0' + team) + ")"; }

} // namespace

const char* positionName(PosId p) { return p < PosCount ? kPosNames[p] : "?"; }

std::string BoxTripState::path() const {
    std::string s = team ? teamPrefix(team) + "." : "";
    return s + "BoxTrip(" + id + ")";
}

Client::Client(const ClientConfig& cfg) : cfg_(cfg) {}

// ---------- session ----------

std::string Client::urlPath() const {
    std::string s = "/WS/?source=";
    urlEncode(s, cfg_.source);
    s += "&platform=";
    urlEncode(s, cfg_.platform);
    return s;
}

std::string Client::cookieHeader() const { return cookie_.empty() ? "" : "CRG_SCOREBOARD=" + cookie_; }

bool Client::takeCookieChanged() {
    bool c = cookieChanged_;
    cookieChanged_ = false;
    return c;
}

// ---------- transport events ----------

bool Client::wantConnect(uint32_t now) const {
    return !connected_ && !connecting_ && (int32_t)(now - nextAttempt_) >= 0;
}

void Client::onConnecting(uint32_t) { connecting_ = true; }

void Client::onConnectFailed(uint32_t now) {
    connecting_ = false;
    backoff_ = backoff_ ? backoff_ * 2 : cfg_.reconnectMinMs;
    if (backoff_ > cfg_.reconnectMaxMs) backoff_ = cfg_.reconnectMaxMs;
    nextAttempt_ = now + backoff_;
}

void Client::onOpen(const std::string& setCookie, uint32_t now) {
    connecting_ = false;
    connected_ = true;
    synced_ = false;
    registered_ = mayWriteRegistered_ = false;
    backoff_ = 0;
    lastRx_ = lastPing_ = now;
    // "CRG_SCOREBOARD=node0abc.node0; Path=/; ..." (several cookies may be joined with ", ")
    size_t at = setCookie.find("CRG_SCOREBOARD=");
    if (at != std::string::npos) {
        at += 15;
        size_t end = setCookie.find_first_of(";, ", at);
        std::string v = setCookie.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (!v.empty() && v != cookie_) {
            cookie_ = v;
            cookieChanged_ = true;
        }
    }
    // The mirror is rebuilt from the Register reply; never keep stale state.
    bool pbt = g_.usePBT;
    g_ = GameState();
    g_.usePBT = pbt;
    sendRegister();
}

void Client::onClose(uint32_t now) {
    bool was = connected_ || connecting_;
    connected_ = connecting_ = synced_ = false;
    // Presses in flight can no longer be confirmed. Never resend them.
    while (!pending_.empty()) finish(0, Outcome::NoChange, now);
    if (was) {
        backoff_ = cfg_.reconnectMinMs;
        nextAttempt_ = now + backoff_;
    }
}

bool Client::tick(uint32_t now) {
    for (size_t i = 0; i < pending_.size();) {
        if (now - pending_[i].sentAt >= cfg_.confirmMs) {
            finish(i, Outcome::NoChange, now);
        } else {
            i++;
        }
    }
    if (!connected_) return false;
    if (now - lastRx_ >= cfg_.deadMs) return true;
    // Ping only when the line is quiet: while clocks run, CRG sends updates
    // anyway. A lost Wi-Fi then shows within deadMs (Rogier, 2026-11-13).
    if (now - lastRx_ >= cfg_.pingMs && now - lastPing_ >= cfg_.pingMs) {
        out_.push_back("{\"action\":\"Ping\"}");
        lastPing_ = now;
    }
    return false;
}

void Client::sendRegister() {
    static const char* const common[] = {
        "Clock(*).Time", "Clock(*).Running", "Clock(*).Direction", "Clock(*).Number", "Clock(*).MaximumTime",
        "InJam", "InPeriod", "NoMoreJam", "OfficialScore", "TimeoutOwner", "OfficialReview", "FiveSeconds",
        "Label(*)", "Team(*).Id", "Team(*).Name", "Team(*).Timeouts", "Team(*).OfficialReviews",
        "Team(*).StarPass", "Team(*).Color(*)", "Rule(Period.Number)",
    };
    static const char* const posProps[] = {"Skater", "RosterNumber", "CurrentFielding",
                                           "PenaltyBox", "HasUnserved", "PenaltyTime"};
    static const char* const tripProps[] = {"IsCurrent", "Jammer", "TimingStopped", "Shortened",
                                            "EndJamNumber", "RosterNumber", "CurrentSkater", "CurrentFielding",
                                            "PenaltyCodes", "TotalPenalties", "Clock.Time", "Clock.Running",
                                            "Clock.MaximumTime", "Clock.Direction"};
    std::string f = "{\"action\":\"Register\",\"paths\":[";
    bool first = true;
    auto add = [&](const std::string& p) {
        if (!first) f += ',';
        first = false;
        appendQuoted(f, p);
    };
    for (const char* p : common) add(std::string(kGame) + p);
    if (cfg_.role != Role::JamTimer) {
        for (const char* p : posProps) add(std::string(kGame) + "Team(*).Position(*)." + p);
        for (const char* p : tripProps) {
            add(std::string(kGame) + "BoxTrip(*)." + p);
            add(std::string(kGame) + "Team(*).BoxTrip(*)." + p);
        }
        add(kUsePBT);
    } else {
        for (const char* n : kEjtNames) add(std::string(kSettingPrefix) + n + ")");
    }
    f += "]}";
    out_.push_back(f);
    registered_ = true;
}

struct FrameCtx {
    Client* c;
    bool sawState = false;
    bool authRefused = false;
};

bool Client::frameVisitor(void* ctx, const char* section, const std::string& key, const JsonValue& v) {
    FrameCtx* fc = (FrameCtx*)ctx;
    if (section[0]) {
        fc->sawState = true;
        fc->c->apply(key, v, fc->c->frameNow_);
    } else if (key == "authorization") {
        fc->authRefused = true;
    }
    return true;
}

void Client::onFrame(const std::string& text, uint32_t now) {
    lastRx_ = now;
    frameNow_ = now;
    FrameCtx fc{this};
    parseFrame(text, frameVisitor, &fc);
    if (fc.authRefused) {
        // One refusal frame per refused Set, in order.
        if (!pending_.empty()) finish(0, Outcome::Refused, now);
        mayWrite_ = false;
    }
    if (fc.sawState && registered_ && !deviceId_.empty() && !mayWriteRegistered_) {
        std::string f = "{\"action\":\"Register\",\"paths\":[";
        appendQuoted(f, "ScoreBoard.Clients.Device(" + deviceId_ + ").MayWrite");
        f += "]}";
        out_.push_back(f);
        mayWriteRegistered_ = true;
    }
}

// ---------- mirror ----------

void Client::apply(const std::string& key, const JsonValue& v, uint32_t now) {
    size_t rest;
    if (key == "WS.Device.Id") {
        deviceId_ = v.text();
        return;
    }
    if (key == "WS.Device.Name") {
        deviceName_ = v.text();
        return;
    }
    if (startsWith(key, "ScoreBoard.Clients.Device(", rest)) {
        if (key.size() > 9 && key.compare(key.size() - 9, 9, ".MayWrite") == 0) mayWrite_ = v.asBool();
        return;
    }
    if (key == kUsePBT) {
        g_.usePBT = v.asBool();
        return;
    }
    if (startsWith(key, kSettingPrefix, rest)) {
        for (int i = 0; i < 5; i++) {
            if (key.compare(rest, std::string::npos, std::string(kEjtNames[i]) + ")") == 0) {
                g_.ejt[i] = v.type == JsonValue::Null ? "" : v.text();
                g_.ejtSeen |= 1 << i;
            }
        }
        return;
    }
    if (key.compare(0, kGameLen, kGame) != 0) return;
    std::string gk = key.substr(kGameLen);
    // The Register reply is the first frame with game keys.
    synced_ = true;
    applyGame(gk, v, now);
    checkPending(gk, now);
}

static void setClock(ClockState& c, const std::string& prop, const JsonValue& v, uint32_t now) {
    if (prop == "Time") {
        c.timeMs = v.asInt();
        c.receivedAt = now;
    } else if (prop == "Running") {
        c.running = v.asBool();
        c.receivedAt = now; // the value was current when the state changed
    } else if (prop == "Direction") {
        c.countsDown = v.asBool();
    } else if (prop == "Number") {
        c.number = (int)v.asInt();
    } else if (prop == "MaximumTime") {
        c.maxMs = v.asInt();
    }
}

void Client::applyGame(const std::string& k, const JsonValue& v, uint32_t now) {
    size_t i = 0;
    std::string id;
    bool null = v.type == JsonValue::Null;
    if (takeId(k, i, "Clock", id)) {
        for (int c = 0; c < ClkCount; c++) {
            if (id == kClockNames[c]) setClock(g_.clock[c], k.substr(i), v, now);
        }
        return;
    }
    if (takeId(k, i, "Label", id)) {
        std::string t = v.text();
        if (id == "Start") g_.labelStart = t;
        else if (id == "Stop") g_.labelStop = t;
        else if (id == "Timeout") g_.labelTimeout = t;
        else if (id == "Undo") g_.labelUndo = t;
        else if (id == "Replaced") g_.labelReplaced = t;
        return;
    }
    if (takeId(k, i, "Rule", id)) {
        if (id == "Period.Number" && !null) g_.periods = (int)v.asInt();
        return;
    }
    int team = 0;
    if (takeId(k, i, "Team", id)) {
        if (id != "1" && id != "2") return;
        team = id[0] - '0';
    }
    if (takeId(k, i, "BoxTrip", id)) {
        std::string prop = k.substr(i);
        BoxTripState* t = tripFor(id, team, !null);
        if (!t) return;
        if (prop == "IsCurrent" && null) {
            // Trip deleted, or moved from the game to a team.
            for (size_t n = 0; n < g_.trips.size(); n++) {
                if (&g_.trips[n] == t) {
                    g_.trips.erase(g_.trips.begin() + n);
                    break;
                }
            }
            return;
        }
        if (prop.compare(0, 6, "Clock.") == 0) {
            if (null) {
                t->hasClock = false;
            } else {
                t->hasClock = true;
                setClock(t->clock, prop.substr(6), v, now);
            }
        } else if (prop == "IsCurrent") t->isCurrent = v.asBool();
        else if (prop == "Jammer") t->jammer = v.asBool();
        else if (prop == "TimingStopped") t->timingStopped = v.asBool();
        else if (prop == "Shortened") t->shortened = (int)v.asInt();
        else if (prop == "EndJamNumber") t->endJamNumber = (int)v.asInt();
        else if (prop == "RosterNumber") t->rosterNumber = v.text();
        else if (prop == "CurrentSkater") t->currentSkater = v.text();
        else if (prop == "CurrentFielding") t->currentFielding = v.text();
        else if (prop == "PenaltyCodes") t->penaltyCodes = v.text();
        else if (prop == "TotalPenalties") t->totalPenalties = (int)v.asInt();
        return;
    }
    if (team) {
        TeamState& ts = g_.team[team - 1];
        if (takeId(k, i, "Position", id)) {
            std::string prop = k.substr(i);
            for (int p = 0; p < PosCount; p++) {
                if (id != kPosNames[p]) continue;
                PositionState& ps = ts.pos[p];
                if (prop == "Skater") ps.skater = v.text();
                else if (prop == "RosterNumber") ps.rosterNumber = v.text();
                else if (prop == "CurrentFielding") ps.currentFielding = v.text();
                else if (prop == "PenaltyBox") ps.penaltyBox = v.asBool();
                else if (prop == "HasUnserved") ps.hasUnserved = v.asBool();
                else if (prop == "PenaltyTime") ps.penaltyTimeMs = v.asInt();
            }
            return;
        }
        if (takeId(k, i, "Color", id)) {
            if (id == "scoreboard.bg") ts.colourScoreboard = v.text();
            else if (id == "overlay.bg") ts.colourOverlay = v.text();
            return;
        }
        std::string prop = k.substr(i);
        if (prop == "Id") ts.id = v.text();
        else if (prop == "Name") ts.name = v.text();
        else if (prop == "Timeouts") ts.timeouts = (int)v.asInt();
        else if (prop == "OfficialReviews") ts.officialReviews = (int)v.asInt();
        else if (prop == "StarPass") ts.starPass = v.asBool();
        return;
    }
    if (k == "InJam") g_.inJam = v.asBool();
    else if (k == "InPeriod") g_.inPeriod = v.asBool();
    else if (k == "NoMoreJam") g_.noMoreJam = v.asBool();
    else if (k == "OfficialScore") g_.officialScore = v.asBool();
    else if (k == "OfficialReview") g_.officialReview = v.asBool();
    else if (k == "FiveSeconds") g_.fiveSeconds = v.asBool();
    else if (k == "TimeoutOwner") g_.timeoutOwner = v.text();
}

BoxTripState* Client::tripFor(const std::string& id, int team, bool create) {
    for (auto& t : g_.trips) {
        if (t.id == id && t.team == team) return &t;
    }
    if (!create) return nullptr;
    // Bound memory: finished trips are of no use to the device, drop the oldest.
    if (g_.trips.size() >= 48) {
        for (size_t n = 0; n < g_.trips.size(); n++) {
            if (!g_.trips[n].isCurrent) {
                g_.trips.erase(g_.trips.begin() + n);
                break;
            }
        }
    }
    g_.trips.push_back(BoxTripState());
    g_.trips.back().id = id;
    g_.trips.back().team = team;
    return &g_.trips.back();
}

std::vector<std::string> Client::ejtDifferences() const {
    std::vector<std::string> d;
    for (int i = 0; i < 5; i++) {
        if (!(g_.ejtSeen & (1 << i)) || g_.ejt[i] == kEjtValues[i]) continue;
        d.push_back(std::string(kEjtNames[i]) + " '" + g_.ejt[i] + "', eJT: " + kEjtValues[i]);
    }
    return d;
}

const BoxTripState* Client::trip(const std::string& id) const {
    const BoxTripState* found = nullptr;
    for (auto& t : g_.trips) {
        if (t.id == id && (!found || t.team > found->team)) found = &t;
    }
    return found;
}

long long Client::displayMs(const ClockState& c, uint32_t now) const {
    if (!c.running) return c.timeMs;
    long long d = (long long)(now - c.receivedAt);
    if (d > 999) d = 999; // the server sends a new value at least every displayed second
    return c.countsDown ? c.timeMs - d : c.timeMs + d;
}

// ---------- commands ----------

void Client::checkPending(const std::string& gk, uint32_t now) {
    for (size_t i = 0; i < pending_.size();) {
        bool hit = false;
        for (auto& w : pending_[i].watch) {
            if (watchMatches(w, gk)) {
                hit = true;
                break;
            }
        }
        if (hit) {
            finish(i, Outcome::Confirmed, now);
        } else {
            i++;
        }
    }
}

void Client::finish(size_t idx, Outcome o, uint32_t now) {
    PressResult r{pending_[idx].id, o, 0};
    if (o == Outcome::Confirmed) {
        r.latencyMs = now - pending_[idx].sentAt;
        lastLatency_ = r.latencyMs;
    }
    results_.push_back(r);
    pending_.erase(pending_.begin() + idx);
}

uint16_t Client::notSent() {
    uint16_t id = nextId_++;
    results_.push_back(PressResult{id, Outcome::NotSent, 0});
    return id;
}

uint16_t Client::set(const std::string& key, const std::string& jsonValue, const std::string& flag,
                     const std::vector<std::string>& watch, uint32_t now) {
    if (!connected_ || !synced_ || !mayWrite_) return notSent();
    std::string f = "{\"action\":\"Set\",\"key\":";
    appendQuoted(f, kGame + key);
    f += ",\"value\":" + jsonValue + ",\"flag\":";
    appendQuoted(f, flag);
    f += '}';
    out_.push_back(f);
    uint16_t id = nextId_++;
    pending_.push_back(Pending{id, now, watch});
    return id;
}

uint16_t Client::press(JtCommand c, uint32_t now) {
    static const std::vector<std::string> start = {"InJam", "Label("};
    static const std::vector<std::string> stop = {"InJam", "InPeriod", "Label("};
    static const std::vector<std::string> timeout = {"Label(", "TimeoutOwner", "OfficialReview", "Clock(Timeout).Number",
                                                     "InJam"};
    static const std::vector<std::string> undo = {"Label("};
    static const std::vector<std::string> typed = {"TimeoutOwner", "OfficialReview", "Label(", "Clock(Timeout).Number",
                                                   "Team(*).Timeouts", "Team(*).OfficialReviews"};
    switch (c) {
    case JtCommand::StartJam: return set("StartJam", "true", "", start, now);
    case JtCommand::StopJam: return set("StopJam", "true", "", stop, now);
    case JtCommand::Timeout: return set("Timeout", "true", "", timeout, now);
    case JtCommand::ClockReplace: return set("ClockReplace", "true", "", undo, now);
    case JtCommand::OfficialTimeout: return set("OfficialTimeout", "true", "", typed, now);
    case JtCommand::Team1Timeout: return set("Team(1).Timeout", "true", "", typed, now);
    case JtCommand::Team2Timeout: return set("Team(2).Timeout", "true", "", typed, now);
    case JtCommand::Team1Review: return set("Team(1).OfficialReview", "true", "", typed, now);
    case JtCommand::Team2Review: return set("Team(2).OfficialReview", "true", "", typed, now);
    }
    return notSent();
}

CrownAction Client::crownAction() const {
    const std::string& s = g_.labelStop;
    if (s == "Stop Jam" || s == "End Timeout" || s == "Overtime Lineup") return CrownAction::StopJam;
    if (g_.labelStart == "Start Jam") return CrownAction::StartJam;
    return CrownAction::None;
}

std::string Client::posKey(int team, PosId p) const { return teamPrefix(team) + ".Position(" + kPosNames[p] + ")"; }

PosId Client::jammerPos(int team) const {
    return (team == 1 || team == 2) && g_.team[team - 1].starPass ? PosPivot : PosJammer;
}

bool Client::tripPosition(const BoxTripState& t, PosId& out) const {
    if ((t.team != 1 && t.team != 2) || t.currentFielding.empty()) return false;
    for (int p = 0; p < PosCount; p++) {
        if (g_.team[t.team - 1].pos[p].currentFielding == t.currentFielding) {
            out = (PosId)p;
            return true;
        }
    }
    // A trip that runs on from an earlier jam points at the current fielding
    // too, but fall back to the position name at the end of the id.
    for (int p = 0; p < PosCount; p++) {
        std::string suffix = std::string("_") + kPosNames[p];
        const std::string& f = t.currentFielding;
        if (f.size() > suffix.size() && f.compare(f.size() - suffix.size(), suffix.size(), suffix) == 0) {
            out = (PosId)p;
            return true;
        }
    }
    return false;
}

uint16_t Client::startBoxClock(int team, PosId p, uint32_t now) {
    if (team != 1 && team != 2) return notSent();
    return set(posKey(team, p) + ".StartBoxClock", "true", "",
               {posKey(team, p) + ".PenaltyBox", teamPrefix(team) + ".BoxTrip(*).IsCurrent"}, now);
}

uint16_t Client::startBoxTrip(uint32_t now) { return set("StartBoxTrip", "true", "", {"BoxTrip(*).IsCurrent"}, now); }

uint16_t Client::assignTrip(const std::string& tripId, int team, PosId p, uint32_t now) {
    const BoxTripState* t = trip(tripId);
    if (!t || (team != 1 && team != 2)) return notSent();
    const std::string& f = g_.team[team - 1].pos[p].currentFielding;
    if (f.empty()) return notSent();
    std::string val;
    appendQuoted(val, f);
    return set(t->path() + ".CurrentFielding", val, "", {teamPrefix(team) + ".BoxTrip(" + tripId + ")"}, now);
}

uint16_t Client::addPenalty(const std::string& tripId, uint32_t now) {
    const BoxTripState* t = trip(tripId);
    if (!t || !t->team || t->currentSkater.empty()) return notSent();
    std::string key = teamPrefix(t->team) + ".Skater(" + t->currentSkater + ").Penalty(" +
                      std::to_string(t->totalPenalties + 1) + ").Code";
    return set(key, "\"?\"", "", {t->path() + ".PenaltyCodes", t->path() + ".Clock.MaximumTime"}, now);
}

uint16_t Client::release(const std::string& tripId, uint32_t now) {
    const BoxTripState* t = trip(tripId);
    PosId p;
    if (!t || !tripPosition(*t, p)) return notSent();
    return set(posKey(t->team, p) + ".PenaltyBox", "false", "", {posKey(t->team, p) + ".PenaltyBox", t->path() + ".IsCurrent"},
               now);
}

uint16_t Client::unendTrip(int team, PosId p, uint32_t now) {
    if (team != 1 && team != 2) return notSent();
    return set(posKey(team, p) + ".UnendBoxTrip", "true", "", {posKey(team, p) + ".PenaltyBox"}, now);
}

uint16_t Client::setTimingStopped(const std::string& tripId, bool stopped, uint32_t now) {
    const BoxTripState* t = trip(tripId);
    if (!t) return notSent();
    return set(t->path() + ".TimingStopped", stopped ? "true" : "false", "", {t->path() + ".TimingStopped"}, now);
}

uint16_t Client::cancelTrip(const std::string& tripId, uint32_t now) {
    const BoxTripState* t = trip(tripId);
    if (!t) return notSent();
    return set(t->path() + ".Delete", "true", "", {t->path() + ".IsCurrent"}, now);
}

} // namespace crg
