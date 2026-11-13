// CRG scoreboard client core: session, registration, state mirror, commands
// with confirmation timeouts, keepalive and reconnect. No Arduino or socket
// code: the caller owns the WebSocket and feeds events in, with the time as
// an argument, and sends whatever outgoing() returns. See docs/protocol.md.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>


#include "crgjson.h"

namespace crg {

enum class Role : uint8_t { JamTimer, BoxBlockers, BoxJammers };

// ---------- mirror of the scoreboard state the device registers for ----------

enum ClockId : uint8_t { ClkPeriod, ClkJam, ClkLineup, ClkTimeout, ClkIntermission, ClkCount };

struct ClockState {
    long long timeMs = 0;
    long long maxMs = 0;
    bool running = false;
    bool countsDown = false;
    int number = 0;
    uint32_t receivedAt = 0; // local ms when timeMs arrived
};

// Positions in the order CRG lists them on the PBT screen.
enum PosId : uint8_t { PosJammer, PosPivot, PosBlocker1, PosBlocker2, PosBlocker3, PosCount };

struct PositionState {
    std::string skater; // skater id, "" when nobody is on the lineup
    std::string rosterNumber;
    std::string currentFielding; // value to set as a trip's CurrentFielding
    bool penaltyBox = false;
    bool hasUnserved = false;
    long long penaltyTimeMs = 0;
};

struct TeamState {
    std::string id; // "<game id>_<n>", compare with TimeoutOwner
    std::string name;
    int timeouts = 0;
    int officialReviews = 0;
    bool starPass = false; // after a star pass the jammer is the Pivot position
    PositionState pos[PosCount];
};

struct BoxTripState {
    std::string id;
    int team = 0; // 0 while the trip has no fielding (key G.BoxTrip(id)), else 1 or 2
    bool isCurrent = false;
    bool jammer = false;
    bool timingStopped = false;
    int shortened = 0;
    int endJamNumber = 0;
    std::string rosterNumber;
    std::string currentSkater;
    std::string penaltyCodes;
    std::string currentFielding;
    int totalPenalties = 0; // the skater's penalty count
    bool hasClock = false;
    ClockState clock;
    // Key prefix for commands on this trip, relative to ScoreBoard.CurrentGame.
    std::string path() const;
};

struct GameState {
    ClockState clock[ClkCount];
    bool inJam = false;
    bool inPeriod = false;
    bool noMoreJam = false;
    bool officialScore = false;
    bool officialReview = false;
    bool fiveSeconds = false;
    std::string timeoutOwner;
    std::string labelStart, labelStop, labelTimeout, labelUndo, labelReplaced;
    TeamState team[2];
    std::vector<BoxTripState> trips;
    bool usePBT = false;
    int periods = 2; // rule Period.Number
    // eJT settings (jam timer role): AutoStart, AutoStart5, Auto5, AutoEndJam, AutoEndTTO
    std::string ejt[5];
    uint8_t ejtSeen = 0;
};

// ---------- commands ----------

enum class Outcome : uint8_t {
    Pending,
    Confirmed, // a matching state change arrived
    NoChange,  // nothing arrived in time: ignored by the server, or slow
    Refused,   // server said not authorized
    NotSent,   // not connected, or not allowed to write
};

struct PressResult {
    uint16_t id;
    Outcome outcome;
    uint32_t latencyMs; // send to confirmation (Confirmed only)
};

enum class JtCommand : uint8_t {
    StartJam,
    StopJam,
    Timeout,
    ClockReplace, // the undo button
    OfficialTimeout,
    Team1Timeout,
    Team2Timeout,
    Team1Review,
    Team2Review,
};

// What the crown does now (protocol.md section 10).
enum class CrownAction : uint8_t { None, StartJam, StopJam };

struct ClientConfig {
    Role role = Role::JamTimer;
    std::string source = "jamtimer"; // shown in CRG Settings > Clients
    std::string platform = "ESP32-C6";
    uint32_t confirmMs = 1500;  // no confirming change within this -> NoChange
    uint32_t pingMs = 5000;     // ping after this long with nothing received (CRG answers Pong)
    uint32_t deadMs = 12000;    // nothing received for this long -> drop the connection (2 Pongs missed)
    uint32_t reconnectMinMs = 1000;
    uint32_t reconnectMaxMs = 8000;
};

class Client {
  public:
    explicit Client(const ClientConfig& cfg = ClientConfig());

    // --- session ---
    // Path and query for the WebSocket URL, e.g. "/WS/?source=jamtimer&platform=ESP32-C6".
    std::string urlPath() const;
    // Cookie header value to send on connect ("" when none yet).
    std::string cookieHeader() const;
    void setStoredCookie(const std::string& value) { cookie_ = value; }
    const std::string& cookie() const { return cookie_; }
    // True once after the cookie changed; persist cookie() then.
    bool takeCookieChanged();

    // --- transport events ---
    bool wantConnect(uint32_t now) const; // disconnected and the backoff has passed
    void onConnecting(uint32_t now);
    void onConnectFailed(uint32_t now);
    // setCookieHeader: the upgrade response's Set-Cookie value(s), may be "".
    void onOpen(const std::string& setCookieHeader, uint32_t now);
    void onFrame(const std::string& text, uint32_t now);
    void onClose(uint32_t now);
    // Pings, liveness and press timeouts. Returns true when the transport
    // should close the connection (it looks dead).
    bool tick(uint32_t now);

    // Frames to send, oldest first. The caller sends and clears them.
    std::vector<std::string>& outgoing() { return out_; }

    // --- status ---
    bool connected() const { return connected_; }
    bool synced() const { return synced_; } // first state frame after Register arrived
    bool mayWrite() const { return mayWrite_; }
    const std::string& deviceId() const { return deviceId_; }
    const std::string& deviceName() const { return deviceName_; }
    uint32_t lastLatencyMs() const { return lastLatency_; }
    const GameState& game() const { return g_; }

    // --- commands ---
    uint16_t press(JtCommand c, uint32_t now);
    CrownAction crownAction() const;
    // Box timer commands. team is 1 or 2.
    uint16_t startBoxClock(int team, PosId p, uint32_t now); // skater on that position sat down
    uint16_t startBoxTrip(uint32_t now);                     // blocker sat, who not known yet
    uint16_t assignTrip(const std::string& tripId, int team, PosId p, uint32_t now);
    uint16_t addPenalty(const std::string& tripId, uint32_t now); // "+ Penalty", code "?"
    uint16_t release(const std::string& tripId, uint32_t now);
    uint16_t unendTrip(int team, PosId p, uint32_t now);
    uint16_t setTimingStopped(const std::string& tripId, bool stopped, uint32_t now);
    uint16_t cancelTrip(const std::string& tripId, uint32_t now);
    // The jammer's position for a team: Pivot after a star pass.
    PosId jammerPos(int team) const;
    // The position a trip is on, from its CurrentFielding; false if unknown.
    bool tripPosition(const BoxTripState& t, PosId& out) const;

    // Generic Set. value is a JSON token (true, 12, "text"). watch: key
    // patterns below ScoreBoard.CurrentGame. whose change confirms it; a
    // pattern matches a key it is a prefix of, and "*" in it matches one id
    // (anything up to the next ")").
    uint16_t set(const std::string& key, const std::string& jsonValue, const std::string& flag,
                 const std::vector<std::string>& watch, uint32_t now);

    // Results of presses that are no longer pending, oldest first.
    std::vector<PressResult>& results() { return results_; }

    // Clock value for display: last received value, moved on locally while
    // running but never past the next whole-second update.
    long long displayMs(const ClockState& c, uint32_t now) const;

    const BoxTripState* trip(const std::string& id) const;
    // Global CRG settings that differ from what the eJT expects (protocol.md
    // section 8). Shown as a warning, never changed by the device.
    std::vector<std::string> ejtDifferences() const;

  private:
    struct Pending {
        uint16_t id;
        uint32_t sentAt;
        std::vector<std::string> watch;
    };

    void sendRegister();
    void apply(const std::string& key, const JsonValue& v, uint32_t now);
    void applyGame(const std::string& k, const JsonValue& v, uint32_t now);
    void checkPending(const std::string& gkey, uint32_t now);
    void finish(size_t idx, Outcome o, uint32_t now);
    uint16_t notSent();
    BoxTripState* tripFor(const std::string& id, int team, bool create);
    std::string posKey(int team, PosId p) const;

    static bool frameVisitor(void* ctx, const char* section, const std::string& key, const JsonValue& v);

    ClientConfig cfg_;
    std::string cookie_;
    bool cookieChanged_ = false;
    bool connected_ = false;
    bool connecting_ = false;
    bool synced_ = false;
    bool registered_ = false;
    bool mayWriteRegistered_ = false;
    bool mayWrite_ = true; // CRG's default for new devices; corrected once registered
    std::string deviceId_, deviceName_;
    uint32_t nextAttempt_ = 0;
    uint32_t backoff_ = 0;
    uint32_t lastRx_ = 0, lastPing_ = 0;
    uint32_t frameNow_ = 0;
    uint32_t lastLatency_ = 0;
    uint16_t nextId_ = 1;
    GameState g_;
    std::vector<Pending> pending_;
    std::vector<PressResult> results_;
    std::vector<std::string> out_;
};

const char* positionName(PosId p);

} // namespace crg
