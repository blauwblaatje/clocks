// Host test for the settings model, storage and setup page.
// Build: g++ -std=c++17 -Wall -Ilib/settings test/native/test_settings.cpp lib/settings/settings.cpp -o /tmp/ts && /tmp/ts
#include "settings.h"
#include <cstdio>

using namespace cfg;

static int failures = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);          \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static bool has(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }

static void roundTrip() {
    Settings s;
    s.role = Role::Jammers;
    s.buttons[1] = "rcl";
    s.faces[3] = 6;
    s.teamColours = true;
    s.jamMs = 90000;
    s.periodAfterTimeout = PeriodAfterTimeout::TimeoutEnd;
    s.undoLevels = 3;
    s.sleepMinutes = 0;
    s.networks.push_back({"Sporthal = Zuid", "p@ss\nword\\", "192.168.1.10", 8000});
    s.networks.push_back({"Home", "x", "crg.local", 8080});
    Settings t = parse(serialize(s));
    CHECK(t.role == Role::Jammers && t.buttons[1] == "rcl" && t.buttons[0] == "clr");
    CHECK(t.faces[3] == 6 && t.faces[0] == 0 && t.teamColours && !Settings().teamColours);
    CHECK(t.jamMs == 90000 && t.periodAfterTimeout == PeriodAfterTimeout::TimeoutEnd && t.undoLevels == 3);
    CHECK(t.sleepMinutes == 0 && Settings().sleepMinutes == 10);
    CHECK(t.networks.size() == 2 && t.networks[0].ssid == "Sporthal = Zuid" && t.networks[0].password == "p@ss\nword\\");
    CHECK(t.networks[1].host == "crg.local" && t.networks[1].port == 8080);
    // defaults for anything missing or broken
    Settings d = parse("role=bogus\njam=abc\nperiods=99\nunknown=1\n");
    CHECK(d.role == Role::JamTimer && d.jamMs == 120000 && d.periods == 2);
    // clock face: stored by key, unknown keys keep the default (classic)
    Settings f;
    f.faces[0] = 2;
    f.faces[2] = 10;
    Settings g = parse(serialize(f));
    CHECK(g.faces[0] == 2 && g.faces[2] == 10 && g.faces[1] == 0 && Settings().faces[0] == 0);
    CHECK(parse("face.jt=sundial\n").faces[0] == 0 && parse("buttons.jt=ccc\n").buttons[0] == "clr");
    // up to 0.2: one face for every role, and a swap of the side buttons
    Settings old = parse("face=lcd-night\nswap.blockers1=1\nswap.jt=0\n");
    CHECK(old.faces[0] == 2 && old.faces[3] == 2 && old.buttons[1] == "crl" && old.buttons[0] == "clr");
    // a per-role key after the old one wins
    CHECK(parse("face=big\nface.jammers=nixie\n").faces[3] == 10 && parse("face=big\nface.jammers=nixie\n").faces[1] == 4);
    CHECK(validButtons("lrc") && !validButtons("lr") && !validButtons("llr"));
    ButtonNames n;
    n.crown = "Yellow";
    CHECK(buttonsText(Role::JamTimer, "lcr", n) == "10 o'clock: start/stop jam \xc2\xb7 Yellow: undo, hold: menu \xc2\xb7 2 o'clock: timeout");
}

static void form() {
    Settings old;
    old.networks.push_back({"Home", "secret", "192.168.1.5", 8000});
    std::string body = "role=blockers2&buttons.blockers2=crl&brightness=60&sleepMinutes=5&jam=1%3A30&lineup=30&timeout=1%3A00"
                       "&period=15%3A00&periods=2&periodAfterTimeout=running&undoLevels=2"
                       "&net0.ssid=Home&net0.password=&net0.host=192.168.1.5&net0.port=8000"
                       "&net1.ssid=Sporthal+Zuid&net1.password=a%26b&net1.host=10.0.0.2&net1.port="
                       "&net2.ssid=&net2.host=";
    Settings out;
    CHECK(applyForm(old, body, out) == "");
    CHECK(out.role == Role::Blockers2 && out.buttons[2] == "crl" && out.buttons[0] == "clr");
    CHECK(out.sleepMinutes == 5);
    CHECK(out.faces[0] == 0 && !out.teamColours); // no face field: unchanged; unchecked box
    Settings lcd;
    CHECK(applyForm(old, body + "&face.jammers=lcd-colour&face.jt=rings&teamColours=on&buttons.jt=bad", lcd) == "");
    CHECK(lcd.faces[3] == 3 && lcd.faces[0] == 6 && lcd.teamColours && lcd.buttons[0] == "clr");
    CHECK(out.brightness == 60 && out.jamMs == 90000 && out.lineupMs == 30000 && out.periodMs == 900000);
    CHECK(!out.jamAutoEnd); // unchecked box
    CHECK(out.periodAfterTimeout == PeriodAfterTimeout::KeepsRunning && out.undoLevels == 2);
    CHECK(out.networks.size() == 2);
    CHECK(out.networks[0].password == "secret"); // blank keeps the saved password
    CHECK(out.networks[1].ssid == "Sporthal Zuid" && out.networks[1].password == "a&b" && out.networks[1].port == 8000);

    // forget a network
    std::string body2 = "role=jt&brightness=100&jam=2%3A00&lineup=0%3A30&timeout=1%3A00&period=30%3A00&periods=2"
                        "&periodAfterTimeout=nextjam&undoLevels=1&net0.ssid=Home&net0.host=x&net0.delete=on";
    Settings out2;
    CHECK(applyForm(out, body2, out2) == "" && out2.networks.empty());

    // errors: nothing changes
    std::string bad = "role=jt&brightness=5&jam=0%3A02&lineup=0%3A30&timeout=1%3A00&period=30%3A00&periods=2"
                      "&periodAfterTimeout=nextjam&undoLevels=1&net0.ssid=Gym&net0.host=&net0.port=99999";
    Settings out3 = old;
    std::string err = applyForm(old, bad, out3);
    CHECK(has(err, "Brightness") && has(err, "Jam length") && has(err, "address is missing") && has(err, "port"));
    CHECK(out3.networks.size() == 1 && out3.networks[0].ssid == "Home");
}

static void pageAndQr() {
    Settings s;
    s.networks.push_back({"<script>", "pw", "h\"x", 8000});
    std::string p = page(s, "Saved.");
    CHECK(has(p, "&lt;script&gt;") && !has(p, "<script>"));
    CHECK(has(p, "value=\"h&quot;x\""));
    CHECK(!has(p, "value=\"pw\"")); // passwords are never sent back
    CHECK(has(p, "placeholder=\"(saved)\""));
    CHECK(has(p, "<option value=\"jt\" selected>"));
    CHECK(has(page(s, "Brightness must be"), "class=\"msg err\""));
    CHECK(wifiQr("CRG;timer", "a:b") == "WIFI:T:WPA;S:CRG\\;timer;P:a\\:b;;");
    CHECK(urlDecode("a+b%20c%3D") == "a b c=");

    // release address and the firmware upload
    CHECK(has(p, "name=\"updateUrl\" value=\"https://clock.quagga.rocks/\""));
    CHECK(!has(p, "/update")); // no upload without a firmware version
    std::string f = page(s, "", "0.3.0");
    CHECK(has(f, "Running: 0.3.0") && has(f, "x.open('POST','/update')"));
    Settings u;
    u.updateUrl = "http://192.168.1.5:8080/latest.json";
    Settings back = parse(serialize(u));
    CHECK(back.updateUrl == u.updateUrl);
    // 0.1.0's default (the Waveshare's latest.json) becomes the site
    Settings old = parse("updateUrl=https://clock.quagga.rocks/latest.json\n");
    CHECK(old.updateUrl == "https://clock.quagga.rocks/");
    std::string base = "role=jt&brightness=100&jam=2%3A00&lineup=0%3A30&timeout=1%3A00&period=30%3A00&periods=2"
                       "&periodAfterTimeout=nextjam&undoLevels=1";
    Settings o;
    CHECK(applyForm(u, base + "&updateUrl=ftp%3A%2F%2Fx", o).find("release address") != std::string::npos);
    CHECK(applyForm(u, base + "&updateUrl=", o) == "" && o.updateUrl.empty());
    CHECK(applyForm(u, base, o) == "" && o.updateUrl == u.updateUrl); // field absent: kept
}

int main() {
    roundTrip();
    form();
    pageAndQr();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all settings tests passed\n");
    return 0;
}
