// Host test for the device menu.
// Build: g++ -std=c++17 -Wall -Ilib/devmenu test/native/test_devmenu.cpp lib/devmenu/devmenu.cpp -o /tmp/tm && /tmp/tm
#include "devmenu.h"
#include <cstdio>

using namespace menu;

static int failures = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);          \
            failures++;                                                       \
        }                                                                     \
    } while (0)

struct FakeHost : Host {
    RoleId r = RoleId::JamTimer;
    bool conn = true, swapped = false, busy = false, slept = false, reset = false, vib = false, motor = false;
    int bright = 75, wifi = 0;
    RoleId role() const override { return r; }
    Refusal setRole(RoleId n) override {
        if (busy) return "Not while a clock runs";
        r = n;
        return "";
    }
    bool connectedMode() const override { return conn; }
    Refusal setConnectedMode(bool c) override {
        conn = c;
        return "";
    }
    bool sidesSwapped() const override { return swapped; }
    Refusal swapSides() override {
        if (busy) return "Not while someone is in the box";
        swapped = !swapped;
        return "";
    }
    Refusal resetGame() override {
        reset = true;
        return "";
    }
    Refusal clearClocks() override { return ""; }
    int brightness() const override { return bright; }
    void setBrightness(int p) override { bright = p; }
    bool motorFitted() const override { return motor; }
    bool vibration() const override { return vib; }
    void setVibration(bool on) override { vib = on; }
    std::vector<std::string> wifiNetworks() const override { return {"Sporthal", "Home"}; }
    int currentWifi() const override { return wifi; }
    Refusal selectWifi(int i) override {
        wifi = i;
        return "";
    }
    Refusal startSetup() override { return ""; }
    std::vector<std::string> info() const override { return {"Battery 80%", "Firmware 0.1"}; }
    menu::Refusal startUpdate() override { return "No release address set"; }
    void sleep() override { slept = true; }
};

// Centre points of the layout
static void tapQuadrant(Menu& m, int q, uint32_t t) { m.touch(q % 2 ? 180 : 60, q < 2 ? 60 : 180, t); }
static void tapRow(Menu& m, int i, uint32_t t) { m.touch(120, 58 + i * 34 + 15, t); }
static void tapBack(Menu& m, uint32_t t) { m.touch(120, 210, t); }

static void navigation() {
    FakeHost h;
    Menu m(h);
    CHECK(!m.isOpen() && m.view().screen == Screen::Closed);
    m.open(0);
    CHECK(m.view().screen == Screen::Top);
    CHECK(Menu::topHit(120, 120) == 4 && Menu::topHit(60, 60) == 0 && Menu::topHit(180, 180) == 3);
    tapQuadrant(m, 0, 100); // Game
    View v = m.view();
    CHECK(v.screen == Screen::List && v.title == "Game" && v.rows.size() == 3);
    CHECK(v.rows[0].label == "Role" && v.rows[0].value == "Jam timer");
    CHECK(v.rows[2].label == "Reset game" && !v.rows[2].enabled); // connected
    tapRow(m, 1, 200); // Mode -> standalone
    CHECK(!h.conn && m.view().rows[2].enabled);
    tapRow(m, 2, 300); // Reset game -> confirm
    CHECK(m.view().screen == Screen::Confirm);
    m.touch(80, 150, 400); // yes
    CHECK(h.reset && m.view().screen == Screen::Message && m.view().lines[0] == "Game reset");
    m.tick(3000);
    CHECK(m.view().screen == Screen::List);
    tapRow(m, 0, 3100); // Role
    CHECK(m.view().title == "Role" && m.view().rows[0].value == "current");
    tapRow(m, 1, 3200); // T1 blockers: menu closes
    CHECK(h.r == RoleId::Blockers1 && !m.isOpen());
    m.open(4000);
    tapQuadrant(m, 0, 4100);
    v = m.view();
    CHECK(v.rows[1].label == "Swap sides" && v.rows[3].label == "Clear clocks");
    h.busy = true;
    tapRow(m, 1, 4200);
    CHECK(m.view().screen == Screen::Message && m.view().lines[0] == "Not while someone is in the box" && !h.swapped);
    m.touch(120, 120, 4300); // a tap dismisses the message
    tapBack(m, 4400);
    CHECK(m.view().screen == Screen::Top);
    m.touch(120, 120, 4500); // centre: close
    CHECK(!m.isOpen());
}

static void deviceConnectionPower() {
    FakeHost h;
    Menu m(h);
    m.open(0);
    tapQuadrant(m, 1, 10);
    CHECK(m.view().rows.size() == 1); // no motor: no Vibration row
    tapRow(m, 0, 20);
    CHECK(h.bright == 100);
    tapRow(m, 0, 30);
    CHECK(h.bright == 25);
    tapBack(m, 40);
    tapQuadrant(m, 2, 50);
    View v = m.view();
    CHECK(v.rows[0].value == "Sporthal" && v.rows.size() == 4);
    tapRow(m, 2, 60); // Info
    CHECK(m.view().screen == Screen::Lines && m.view().lines[0] == "Battery 80%");
    tapBack(m, 70);
    tapRow(m, 0, 80); // Wi-Fi list
    tapRow(m, 1, 90);
    CHECK(h.wifi == 1 && m.view().lines[0] == "Joining Home");
    m.tick(5000);
    tapBack(m, 5010);
    tapBack(m, 5020);
    tapQuadrant(m, 3, 5030); // Power
    tapRow(m, 0, 5040);
    m.touch(80, 150, 5050);
    CHECK(h.slept && !m.isOpen());
    // idle: closes by itself
    m.open(6000);
    m.tick(36000);
    CHECK(!m.isOpen());
}

int main() {
    navigation();
    deviceConnectionPower();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all devmenu tests passed\n");
    return 0;
}
