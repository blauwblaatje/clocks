// Line protocol around crg::Client for the live test (live.py owns the socket).
// stdin, one per line, <now> in ms:
//   R <role>                 role: jt | blockers | jammers (before anything else)
//   K <cookie>               stored cookie
//   T <now>                  tick
//   N <now>                  connecting
//   O <now> <set-cookie>     socket open
//   F <now> <json>           frame received
//   C <now>                  socket closed
//   P <now> <command>        StartJam StopJam Timeout ClockReplace OfficialTimeout Team1Timeout ... Crown
//   B <now> <op> <args>      sbc T POS | sbt | assign TRIP T POS | pen TRIP | rel TRIP | unend T POS
//                            | stop TRIP 0/1 | cancel TRIP
//   Q                        dump state
//   X <now> <btn> <0|1>      box controller button (crown | left | right), up/down
//   Y <now> <x> <y>          box controller touch
//   V <now>                  box controller view
// stdout: SEND <json>, RES <id> <outcome> <latency>, ID <id>, DROP, COOKIE <v>, state lines, then OK.
#include "boxctl.h"
#include "crgclient.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

using namespace crg;

static const char* outcomeName(Outcome o) {
    switch (o) {
    case Outcome::Pending: return "Pending";
    case Outcome::Confirmed: return "Confirmed";
    case Outcome::NoChange: return "NoChange";
    case Outcome::Refused: return "Refused";
    case Outcome::NotSent: return "NotSent";
    }
    return "?";
}

static PosId pos(const std::string& s) {
    for (int p = 0; p < PosCount; p++) {
        if (s == positionName((PosId)p)) return (PosId)p;
    }
    return PosJammer;
}

int main() {
    std::ios::sync_with_stdio(false);
    Client* c = nullptr;
    boxcore::BoxTimer local;
    box::Controller* bc = nullptr;
    ClientConfig cfg;
    cfg.platform = "host-test";
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string op;
        in >> op;
        if (op == "R") {
            std::string r;
            in >> r;
            // bctl / jctl: a box::Controller drives the client (it consumes the press results)
            cfg.role = r == "blockers" || r == "bctl" ? Role::BoxBlockers
                       : r == "jammers" || r == "jctl" ? Role::BoxJammers
                                                       : Role::JamTimer;
            cfg.source = r == "jt" ? "jamtimer" : "boxtimer";
            delete c;
            c = new Client(cfg);
            delete bc;
            bc = nullptr;
            if (r == "bctl") bc = new box::Controller(box::Role::Blockers, 1, local, c, box::Mode::Connected);
            if (r == "jctl") bc = new box::Controller(box::Role::Jammers, 1, local, c, box::Mode::Connected);
            std::cout << "URL " << c->urlPath() << "\n";
        } else if (!c) {
            c = new Client(cfg);
        }
        uint32_t now = 0;
        if (op != "R" && op != "K" && op != "Q") in >> now;
        std::string rest;
        std::getline(in, rest);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
        if (op == "T" && bc) bc->tick(now);
        if (op == "X" && bc) {
            std::istringstream a(rest);
            std::string b;
            int down = 0;
            a >> b >> down;
            bc->button(b == "crown" ? box::Btn::Crown : b == "left" ? box::Btn::Left : box::Btn::Right, down, now);
            bc->tick(now);
        } else if (op == "Y" && bc) {
            std::istringstream a(rest);
            int x = 0, y = 0;
            a >> x >> y;
            bc->touch(x, y, now);
            bc->tick(now);
        } else if (op == "V" && bc) {
            box::View v = bc->view(now);
            std::cout << "V screen=" << (int)v.screen << " status='" << v.status << "' feedback=" << (int)v.feedback
                      << " slots=" << v.slots.size() << "\n";
            for (auto& s : v.slots)
                std::cout << "L key=" << s.key << " number=" << s.number << " clock=" << s.clock << " phase=" << (int)s.phase
                          << " side=" << s.side << "\n";
            if (v.screen == box::Screen::Picker)
                for (auto& pi : v.pick) std::cout << "K label='" << pi.label << "' number=" << pi.number << " unserved=" << pi.unserved << "\n";
        }
        if (op == "K") c->setStoredCookie(rest.substr(rest.find_first_not_of(' ')));
        else if (op == "T") {
            if (c->tick(now)) std::cout << "DROP\n";
        } else if (op == "N") c->onConnecting(now);
        else if (op == "O") c->onOpen(rest, now);
        else if (op == "F") c->onFrame(rest, now);
        else if (op == "C") c->onClose(now);
        else if (op == "P") {
            uint16_t id = 0;
            if (rest == "Crown") {
                CrownAction a = c->crownAction();
                id = a == CrownAction::StartJam  ? c->press(JtCommand::StartJam, now)
                     : a == CrownAction::StopJam ? c->press(JtCommand::StopJam, now)
                                                 : 0;
            } else {
                static const char* names[] = {"StartJam",     "StopJam",      "Timeout",     "ClockReplace", "OfficialTimeout",
                                              "Team1Timeout", "Team2Timeout", "Team1Review", "Team2Review"};
                for (int i = 0; i < 9; i++) {
                    if (rest == names[i]) id = c->press((JtCommand)i, now);
                }
            }
            std::cout << "ID " << id << "\n";
        } else if (op == "B") {
            std::istringstream a(rest);
            std::string b, x, y, z;
            a >> b >> x >> y >> z;
            uint16_t id = 0;
            if (b == "sbc") id = c->startBoxClock(std::stoi(x), pos(y), now);
            else if (b == "sbt") id = c->startBoxTrip(now);
            else if (b == "assign") id = c->assignTrip(x, std::stoi(y), pos(z), now);
            else if (b == "pen") id = c->addPenalty(x, now);
            else if (b == "rel") id = c->release(x, now);
            else if (b == "unend") id = c->unendTrip(std::stoi(x), pos(y), now);
            else if (b == "stop") id = c->setTimingStopped(x, y == "1", now);
            else if (b == "cancel") id = c->cancelTrip(x, now);
            std::cout << "ID " << id << "\n";
        } else if (op == "Q") {
            const GameState& g = c->game();
            CrownAction ca = c->crownAction();
            std::cout << "S connected=" << c->connected() << " synced=" << c->synced() << " mayWrite=" << c->mayWrite()
                      << " device=" << c->deviceId() << " inJam=" << g.inJam << " crown="
                      << (ca == CrownAction::StartJam ? "StartJam" : ca == CrownAction::StopJam ? "StopJam" : "None")
                      << " start='" << g.labelStart << "' stop='" << g.labelStop << "' timeout='" << g.labelTimeout
                      << "' undo='" << g.labelUndo << "' owner='" << g.timeoutOwner << "' to#=" << g.clock[ClkTimeout].number
                      << " jam#=" << g.clock[ClkJam].number << " jam=" << g.clock[ClkJam].timeMs
                      << " period=" << g.clock[ClkPeriod].timeMs << " t1TO=" << g.team[0].timeouts
                      << " usePBT=" << g.usePBT << " latency=" << c->lastLatencyMs() << "\n";
            for (int t = 0; t < 2; t++) {
                for (int p = 0; p < PosCount; p++) {
                    const PositionState& ps = g.team[t].pos[p];
                    if (ps.skater.empty() && !ps.penaltyBox) continue;
                    std::cout << "P " << t + 1 << " " << positionName((PosId)p) << " #" << ps.rosterNumber
                              << " box=" << ps.penaltyBox << " unserved=" << ps.hasUnserved
                              << " skater=" << ps.skater << " fielding=" << ps.currentFielding << "\n";
                }
            }
            for (auto& t : g.trips) {
                PosId p = PosJammer;
                bool hp = c->tripPosition(t, p);
                std::cout << "B " << t.id << " team=" << t.team << " cur=" << t.isCurrent << " #" << t.rosterNumber
                          << " pos=" << (hp ? positionName(p) : "-") << " jammer=" << t.jammer
                          << " codes='" << t.penaltyCodes << "' pens=" << t.totalPenalties << " clock=" << t.hasClock
                          << " time=" << t.clock.timeMs << " max=" << t.clock.maxMs << " run=" << t.clock.running
                          << " short=" << t.shortened << " stopped=" << t.timingStopped << " end=" << t.endJamNumber
                          << "\n";
            }
        }
        // As on the device: the controller runs after every event, before
        // anything else looks at the press results.
        if (bc && op != "R" && op != "K" && op != "Q") bc->tick(now);
        for (auto& f : c->outgoing()) std::cout << "SEND " << f << "\n";
        c->outgoing().clear();
        for (auto& r : c->results()) std::cout << "RES " << r.id << " " << outcomeName(r.outcome) << " " << r.latencyMs << "\n";
        c->results().clear();
        if (c->takeCookieChanged()) std::cout << "COOKIE " << c->cookie() << "\n";
        std::cout << "OK" << std::endl;
    }
    delete c;
    return 0;
}
