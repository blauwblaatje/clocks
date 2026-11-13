// Host test for the CRG client core with canned frames (no server).
// Build: g++ -std=c++17 -Wall -Ilib/crgclient test/native/test_crgclient.cpp lib/crgclient/*.cpp -o /tmp/tc && /tmp/tc
// The live test against a real CRG is tools/crgclient-live/.
#include "crgclient.h"
#include <cstdio>
#include <string>

using namespace crg;

static int failures = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);          \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static const std::string G = "ScoreBoard.CurrentGame.";

static std::string st(const std::string& body) { return "{\"state\":{" + body + "}}"; }
static std::string kv(const std::string& k, const std::string& v) { return "\"" + G + k + "\":" + v; }

static bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

static Outcome last(Client& c, uint16_t id) {
    for (auto& r : c.results()) {
        if (r.id == id) return r.outcome;
    }
    return Outcome::Pending;
}

// Connected and synced jam timer client at t=1000.
static void connect(Client& c, uint32_t now = 1000) {
    c.onConnecting(now);
    c.onOpen("CRG_SCOREBOARD=node0abc123.node0; Path=/; HttpOnly", now);
    c.onFrame(R"({"state":{"WS.Device.Id":"dev-1","WS.Device.Name":"Brave Owl","WS.Client.Id":"c1"}})", now);
    c.onFrame(st(kv("InJam", "false") + "," + kv("Label(Start)", "\"Start Jam\"") + "," + kv("Label(Stop)", "\"Lineup\"") +
                 "," + kv("Team(1).Id", "\"g_1\"")),
              now + 5);
    c.outgoing().clear();
}

struct Seen {
    int n = 0;
    std::string last, lastSection;
    JsonValue v;
};
static bool visitor(void* ctx, const char* section, const std::string& key, const JsonValue& v) {
    Seen* s = (Seen*)ctx;
    s->n++;
    s->last = key;
    s->lastSection = section;
    s->v = v;
    return true;
}

static void jsonParsing() {
    Seen s;
    CHECK(parseFrame(R"({"state":{"a":"x\"y\\zé","b":-1000,"c":true,"d":null,"e":{"x":[1,{"y":2}]},"f":"😀"}})",
                     visitor, &s));
    CHECK(s.n == 6);
    s = Seen();
    parseFrame(R"({"state":{"a":"x\"y\\zé"}})", visitor, &s);
    CHECK(s.v.str == "x\"y\\z\xc3\xa9");
    s = Seen();
    parseFrame(R"({"state":{"b":-1000}})", visitor, &s);
    CHECK(s.v.type == JsonValue::Number && s.v.asInt() == -1000);
    s = Seen();
    parseFrame(R"({"state":{"f":"😀"}})", visitor, &s);
    CHECK(s.v.str == "\xf0\x9f\x98\x80");
    s = Seen();
    CHECK(parseFrame(R"({"Pong":""})", visitor, &s) && s.lastSection == "" && s.last == "Pong");
    s = Seen();
    CHECK(parseFrame(R"({"authorization":"Not authorized for Set"})", visitor, &s) && s.last == "authorization");
    CHECK(!parseFrame(R"({"state":{"a":)", visitor, &s));
    CHECK(parseFrame("{}", visitor, &s));
    std::string q;
    appendQuoted(q, "a\"b\n\x01");
    CHECK(q == "\"a\\\"b\\n\\u0001\"");
}

static void sessionAndRegister() {
    ClientConfig cfg;
    cfg.source = "jamtimer";
    cfg.platform = "ESP32-C6 fw0.1";
    Client c(cfg);
    CHECK(c.urlPath() == "/WS/?source=jamtimer&platform=ESP32-C6%20fw0.1");
    CHECK(c.cookieHeader() == "");
    CHECK(c.wantConnect(0));
    c.onConnecting(0);
    CHECK(!c.wantConnect(0));
    c.onOpen("CRG_SCOREBOARD=node0abc123.node0; Path=/; HttpOnly", 0);
    CHECK(c.cookie() == "node0abc123.node0");
    CHECK(c.takeCookieChanged() && !c.takeCookieChanged());
    CHECK(c.cookieHeader() == "CRG_SCOREBOARD=node0abc123.node0");
    CHECK(c.outgoing().size() == 1);
    const std::string& reg = c.outgoing()[0];
    CHECK(contains(reg, "\"action\":\"Register\""));
    CHECK(contains(reg, "\"ScoreBoard.CurrentGame.Label(*)\""));
    CHECK(!contains(reg, "BoxTrip")); // jam timer does not register box keys
    c.outgoing().clear();
    CHECK(!c.synced());
    c.onFrame(R"({"state":{"WS.Device.Id":"dev-1","WS.Device.Name":"Brave Owl"}})", 10);
    CHECK(c.deviceId() == "dev-1" && c.deviceName() == "Brave Owl");
    CHECK(!c.synced());
    c.onFrame(st(kv("InJam", "false")), 20);
    CHECK(c.synced());
    // MayWrite is registered once the device id is known
    CHECK(c.outgoing().size() == 1 && contains(c.outgoing()[0], "ScoreBoard.Clients.Device(dev-1).MayWrite"));
    c.outgoing().clear();
    c.onFrame(R"({"state":{"ScoreBoard.Clients.Device(dev-1).MayWrite":false}})", 30);
    CHECK(!c.mayWrite());

    // Same cookie again: not reported as changed
    Client d(cfg);
    d.setStoredCookie("node0abc123.node0");
    d.onOpen("CRG_SCOREBOARD=node0abc123.node0; Path=/", 0);
    CHECK(!d.takeCookieChanged());

    ClientConfig box;
    box.role = Role::BoxBlockers;
    Client b(box);
    b.onOpen("", 0);
    CHECK(contains(b.outgoing()[0], "\"ScoreBoard.CurrentGame.Team(*).BoxTrip(*).Clock.Time\""));
    CHECK(contains(b.outgoing()[0], "\"ScoreBoard.CurrentGame.BoxTrip(*).IsCurrent\""));
    CHECK(contains(b.outgoing()[0], "UsePBT"));
}

static void pressConfirmed() {
    Client c;
    uint16_t early = c.press(JtCommand::StartJam, 0);
    CHECK(last(c, early) == Outcome::NotSent);
    CHECK(c.outgoing().empty());
    connect(c);
    CHECK(c.crownAction() == CrownAction::StartJam);
    uint16_t id = c.press(JtCommand::StartJam, 2000);
    CHECK(c.outgoing().size() == 1);
    CHECK(c.outgoing()[0] == R"({"action":"Set","key":"ScoreBoard.CurrentGame.StartJam","value":true,"flag":""})");
    // a clock tick does not confirm
    c.onFrame(st(kv("Clock(Period).Time", "1799000")), 2010);
    CHECK(last(c, id) == Outcome::Pending);
    c.onFrame(st(kv("InJam", "true") + "," + kv("Label(Start)", "\"---\"") + "," + kv("Label(Stop)", "\"Stop Jam\"")),
              2030);
    CHECK(last(c, id) == Outcome::Confirmed);
    CHECK(c.lastLatencyMs() == 30);
    CHECK(c.game().inJam);
    CHECK(c.crownAction() == CrownAction::StopJam);
}

static void pressNoChangeAndRefused() {
    Client c;
    connect(c);
    uint16_t id = c.press(JtCommand::StopJam, 2000);
    c.tick(3499);
    CHECK(last(c, id) == Outcome::Pending);
    c.tick(3500);
    CHECK(last(c, id) == Outcome::NoChange);

    uint16_t r = c.press(JtCommand::StartJam, 4000);
    c.onFrame(R"({"authorization":"Not authorized for Set"})", 4010);
    CHECK(last(c, r) == Outcome::Refused);
    CHECK(!c.mayWrite());
    uint16_t n = c.press(JtCommand::StartJam, 4100);
    CHECK(last(c, n) == Outcome::NotSent);
    // granted again from Settings > Clients
    c.onFrame(R"({"state":{"ScoreBoard.Clients.Device(dev-1).MayWrite":true}})", 4200);
    CHECK(c.mayWrite());
}

static void crownLabels() {
    Client c;
    connect(c);
    auto labels = [&](const char* start, const char* stop) {
        c.onFrame(st(kv("Label(Start)", std::string("\"") + start + "\"") + "," +
                     kv("Label(Stop)", std::string("\"") + stop + "\"")),
                  5000);
        return c.crownAction();
    };
    CHECK(labels("Start Jam", "Lineup") == CrownAction::StartJam);
    CHECK(labels("Start Jam", "---") == CrownAction::StartJam);
    CHECK(labels("---", "Stop Jam") == CrownAction::StopJam);
    CHECK(labels("---", "End Timeout") == CrownAction::StopJam);
    CHECK(labels("Start Jam", "Overtime Lineup") == CrownAction::StopJam);
    CHECK(labels("---", "---") == CrownAction::None);
}

static void keepaliveAndReconnect() {
    ClientConfig cfg;
    Client c(cfg);
    connect(c, 1000);
    CHECK(!c.tick(5999) && c.outgoing().empty());
    CHECK(!c.tick(6005));
    CHECK(c.outgoing().size() == 1 && c.outgoing()[0] == R"({"action":"Ping"})");
    c.outgoing().clear();
    c.onFrame(R"({"Pong":""})", 6020);
    c.onFrame(R"({"state":{}})", 9000); // traffic: no ping needed
    CHECK(!c.tick(13999) && c.outgoing().empty());
    CHECK(!c.tick(14000) && c.outgoing().size() == 1); // 5 s quiet: ping
    c.outgoing().clear();
    CHECK(!c.tick(19005) && c.outgoing().size() == 1); // no Pong: ping again
    c.outgoing().clear();
    CHECK(!c.tick(20999));
    CHECK(c.tick(21000)); // 12 s without anything: drop
    uint16_t id = c.press(JtCommand::StartJam, 76020);
    c.onClose(76030);
    CHECK(last(c, id) == Outcome::NoChange); // never resent after a reconnect
    CHECK(!c.connected() && !c.wantConnect(76500));
    CHECK(c.wantConnect(77030));
    c.onConnecting(77030);
    c.onConnectFailed(77100);
    CHECK(!c.wantConnect(78000) && c.wantConnect(79100)); // backoff doubled to 2 s
    c.onConnecting(79100);
    c.onConnectFailed(79200);
    CHECK(c.wantConnect(83200));
    c.onConnecting(83200);
    c.onConnectFailed(83300);
    c.onConnecting(91300);
    c.onConnectFailed(91400);
    CHECK(!c.wantConnect(99399) && c.wantConnect(99400)); // capped at 8 s
    // the mirror is rebuilt after a reconnect
    c.onConnecting(99400);
    c.onOpen("", 99400);
    CHECK(!c.synced() && !c.game().inJam);
    CHECK(c.cookie() == "node0abc123.node0"); // no Set-Cookie: keep the stored one
}

static void clockDisplay() {
    Client c;
    connect(c);
    c.onFrame(st(kv("Clock(Jam).Direction", "true") + "," + kv("Clock(Jam).Running", "true") + "," +
                 kv("Clock(Jam).Time", "119000")),
              10000);
    const ClockState& j = c.game().clock[ClkJam];
    CHECK(c.displayMs(j, 10000) == 119000);
    CHECK(c.displayMs(j, 10400) == 118600);
    CHECK(c.displayMs(j, 12000) == 118001); // late update: hold within the second
    c.onFrame(st(kv("Clock(Jam).Running", "false")), 12000);
    CHECK(c.displayMs(c.game().clock[ClkJam], 15000) == 119000);
}

static void boxTrips() {
    ClientConfig cfg;
    cfg.role = Role::BoxBlockers;
    Client c(cfg);
    connect(c);
    c.onFrame(st(kv("Team(1).Position(Blocker2).Skater", "\"sk12\"") + "," +
                 kv("Team(1).Position(Blocker2).CurrentFielding", "\"tj_1_Blocker2\"") + "," +
                 kv("Team(1).Position(Blocker2).HasUnserved", "true") + "," + kv("Team(1).StarPass", "false") + "," +
                 kv("Team(2).StarPass", "true")),
              2000);
    CHECK(c.game().team[0].pos[PosBlocker2].hasUnserved);
    CHECK(c.jammerPos(1) == PosJammer && c.jammerPos(2) == PosPivot);

    uint16_t s = c.startBoxTrip(3000);
    CHECK(contains(c.outgoing().back(), "\"ScoreBoard.CurrentGame.StartBoxTrip\""));
    // an existing trip's clock tick must not confirm it
    c.onFrame(st(kv("Team(2).BoxTrip(old).Clock.Time", "12000")), 3005);
    CHECK(last(c, s) == Outcome::Pending);
    c.onFrame(st(kv("BoxTrip(t1).IsCurrent", "true") + "," + kv("BoxTrip(t1).RosterNumber", "\"\"") + "," +
                 kv("BoxTrip(t1).Clock.Time", "30000") + "," + kv("BoxTrip(t1).Clock.Running", "true")),
              3020);
    CHECK(last(c, s) == Outcome::Confirmed);
    const BoxTripState* t = c.trip("t1");
    CHECK(t && t->team == 0 && t->path() == "BoxTrip(t1)" && t->hasClock);

    uint16_t a = c.assignTrip("t1", 1, PosBlocker2, 4000);
    CHECK(contains(c.outgoing().back(),
                   R"("key":"ScoreBoard.CurrentGame.BoxTrip(t1).CurrentFielding","value":"tj_1_Blocker2")"));
    // the trip moves to the team: old keys removed, new keys added (in any order)
    c.onFrame(st(kv("Team(1).BoxTrip(t1).IsCurrent", "true") + "," + kv("Team(1).BoxTrip(t1).RosterNumber", "\"12\"") +
                 "," + kv("Team(1).BoxTrip(t1).CurrentSkater", "\"sk12\"") + "," +
                 kv("Team(1).BoxTrip(t1).CurrentFielding", "\"tj_1_Blocker2\"") + "," +
                 kv("Team(1).BoxTrip(t1).TotalPenalties", "1") + "," + kv("Team(1).BoxTrip(t1).Clock.Time", "29000") +
                 "," + kv("BoxTrip(t1).IsCurrent", "null") + "," + kv("BoxTrip(t1).Clock.Time", "null")),
              4030);
    CHECK(last(c, a) == Outcome::Confirmed);
    t = c.trip("t1");
    CHECK(t && t->team == 1 && t->path() == "Team(1).BoxTrip(t1)" && t->rosterNumber == "12");
    int copies = 0;
    for (auto& x : c.game().trips) copies += x.id == "t1";
    CHECK(copies == 1); // the game-level copy is gone
    PosId p;
    CHECK(c.tripPosition(*t, p) && p == PosBlocker2);

    uint16_t pen = c.addPenalty("t1", 5000);
    CHECK(contains(c.outgoing().back(),
                   R"("key":"ScoreBoard.CurrentGame.Team(1).Skater(sk12).Penalty(2).Code","value":"?")"));
    c.onFrame(st(kv("Team(1).BoxTrip(t1).PenaltyCodes", "\"B ?\"")), 5040);
    CHECK(last(c, pen) == Outcome::Confirmed);

    uint16_t ts = c.setTimingStopped("t1", true, 6000);
    CHECK(contains(c.outgoing().back(), R"("key":"ScoreBoard.CurrentGame.Team(1).BoxTrip(t1).TimingStopped","value":true)"));
    c.onFrame(st(kv("Team(1).BoxTrip(t1).TimingStopped", "true")), 6010);
    CHECK(last(c, ts) == Outcome::Confirmed && c.trip("t1")->timingStopped);

    uint16_t rel = c.release("t1", 7000);
    CHECK(contains(c.outgoing().back(), R"("key":"ScoreBoard.CurrentGame.Team(1).Position(Blocker2).PenaltyBox","value":false)"));
    c.onFrame(st(kv("Team(1).Position(Blocker2).PenaltyBox", "false") + "," + kv("Team(1).BoxTrip(t1).IsCurrent", "false") +
                 "," + kv("Team(1).BoxTrip(t1).Clock.Time", "null")),
              7020);
    CHECK(last(c, rel) == Outcome::Confirmed);
    CHECK(c.trip("t1") && !c.trip("t1")->isCurrent && !c.trip("t1")->hasClock);

    uint16_t un = c.unendTrip(1, PosBlocker2, 8000);
    CHECK(contains(c.outgoing().back(), "Team(1).Position(Blocker2).UnendBoxTrip"));
    c.onFrame(st(kv("Team(1).Position(Blocker2).PenaltyBox", "true")), 8010);
    CHECK(last(c, un) == Outcome::Confirmed);

    uint16_t del = c.cancelTrip("t1", 9000);
    CHECK(contains(c.outgoing().back(), "Team(1).BoxTrip(t1).Delete"));
    c.onFrame(st(kv("Team(1).BoxTrip(t1).IsCurrent", "null")), 9010);
    CHECK(last(c, del) == Outcome::Confirmed && c.trip("t1") == nullptr);

    CHECK(last(c, c.addPenalty("missing", 9100)) == Outcome::NotSent);
    uint16_t sb = c.startBoxClock(2, c.jammerPos(2), 9200);
    CHECK(contains(c.outgoing().back(), "Team(2).Position(Pivot).StartBoxClock"));
    c.onFrame(st(kv("Team(2).Position(Pivot).PenaltyBox", "true")), 9220);
    CHECK(last(c, sb) == Outcome::Confirmed);
}

static void ejtSettings() {
    Client c;
    c.onOpen("", 0);
    CHECK(contains(c.outgoing()[0], "ScoreBoard.Settings.Setting(ScoreBoard.AutoStart5)"));
    CHECK(c.ejtDifferences().empty()); // nothing known yet: no warning
    // a fresh server: AutoStart "", AutoStart5 "", Auto5 false, AutoEndJam false, AutoEndTTO true
    c.onFrame(R"x({"state":{"ScoreBoard.Settings.Setting(ScoreBoard.AutoStart)":"","ScoreBoard.Settings.Setting(ScoreBoard.AutoStart5)":"",)x"
              R"x("ScoreBoard.Settings.Setting(ScoreBoard.Auto5)":false,"ScoreBoard.Settings.Setting(ScoreBoard.AutoEndJam)":false,)x"
              R"x("ScoreBoard.Settings.Setting(ScoreBoard.AutoEndTTO)":true}})x",
              10);
    auto d = c.ejtDifferences();
    CHECK(d.size() == 3 && d[0] == "AutoStart '', eJT: Timeout" && d[2] == "Auto5 'false', eJT: true");
    c.onFrame(R"x({"state":{"ScoreBoard.Settings.Setting(ScoreBoard.AutoStart)":"Timeout","ScoreBoard.Settings.Setting(ScoreBoard.AutoStart5)":"Jam",)x"
              R"x("ScoreBoard.Settings.Setting(ScoreBoard.Auto5)":"true"}})x",
              20);
    CHECK(c.ejtDifferences().empty());
}

int main() {
    ejtSettings();
    jsonParsing();
    sessionAndRegister();
    pressConfirmed();
    pressNoChangeAndRefused();
    crownLabels();
    keepaliveAndReconnect();
    clockDisplay();
    boxTrips();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all crgclient tests passed\n");
    return 0;
}
