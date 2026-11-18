// Device settings (HANDOVER.md section 8, "Setup"): saved Wi-Fi networks,
// each with its own scoreboard; role; per-role button sides; standalone
// durations and rules; motor fitted. Stored as "key=value" lines; edited on
// the setup page (an HTML form). No Arduino dependencies.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace cfg {

constexpr int kMaxNetworks = 4;
enum class Role : uint8_t { JamTimer, Blockers1, Blockers2, Jammers };
constexpr int kRoles = 4;

struct Network {
    std::string ssid, password;
    std::string host; // scoreboard IP or name
    int port = 8000;
};

enum class PeriodAfterTimeout : uint8_t { NextJam, TimeoutEnd, KeepsRunning };

struct Settings {
    std::vector<Network> networks; // in the order to try
    Role role = Role::JamTimer;
    bool swapButtons[kRoles] = {false, false, false, false}; // swap the 10 and 2 o'clock buttons, per role
    bool motorFitted = false;
    int brightness = 100;
    int sleepMinutes = 10; // sleep after this long idle (no clock, no press); 0 = never
    // Install site for the menu's Update: the device adds "<board>/latest.json"
    // (upd::releaseUrl); a .json address is used as it stands; "" = none
    std::string updateUrl = "https://clock.quagga.rocks/";
    // standalone (jam timer)
    uint32_t jamMs = 120000, lineupMs = 30000, timeoutMs = 60000, periodMs = 1800000;
    int periods = 2;
    bool jamAutoEnd = true;
    PeriodAfterTimeout periodAfterTimeout = PeriodAfterTimeout::NextJam;
    int undoLevels = 1;
};

const char* roleKey(Role r); // "jt", "blockers1", "blockers2", "jammers"

// Storage: one "key=value" line per setting, values escaped (\n, \\).
std::string serialize(const Settings& s);
Settings parse(const std::string& text); // unknown keys are ignored, bad values keep the default

// The setup page: a form with every setting, and its POST body
// (application/x-www-form-urlencoded). A blank password keeps the saved one.
// firmware: the running version; when set, the page also offers a
// firmware.bin upload (POST /update, raw body).
std::string page(const Settings& s, const std::string& message, const std::string& firmware = "");
// Returns "" when valid (out is then the new settings), else what is wrong.
std::string applyForm(const Settings& old, const std::string& body, Settings& out);

std::string urlDecode(const std::string& s);
std::string htmlEscape(const std::string& s);
// "WIFI:T:WPA;S:<ssid>;P:<password>;;" for the QR code
std::string wifiQr(const std::string& ssid, const std::string& password);

} // namespace cfg
