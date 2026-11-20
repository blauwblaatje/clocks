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

// Clock faces (Rogier, 2026-11-19): all in the firmware, picked here, one
// per role (2026-11-20: "I only need to switch role, not face").
const int kFaces = 11;
extern const char* const kFaceKeys[kFaces];
extern const char* const kFaceNames[kFaces];

struct Settings {
    std::vector<Network> networks; // in the order to try
    Role role = Role::JamTimer;
    // Which button does what, per role (2026-11-20): for the role's three
    // actions in order (the crown's, the 10 o'clock button's: undo and the
    // menu hold, the 2 o'clock button's; jammers: undo, left, right) the
    // physical button: 'c' crown, 'l' left (10 o'clock; StopWatch red),
    // 'r' right (2 o'clock; StopWatch blue). "clr" = as printed. Undo and the
    // menu hold stay one action, so every mapping keeps the menu reachable.
    std::string buttons[kRoles] = {"clr", "clr", "clr", "clr"};
    bool motorFitted = false;
    int brightness = 100;
    int faces[kRoles] = {0, 0, 0, 0}; // kFaceKeys, per role
    bool teamColours = false;         // faces draw CRG's team colours (box timer roles)
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
const char* roleName(Role r); // "Jam timer", ...
bool validButtons(const std::string& m); // a permutation of "clr"
// The role's three actions, for the setup page and the menu.
const char* actionName(Role r, int slot);

// Names of the physical buttons on this board, for the setup page.
struct ButtonNames {
    std::string crown = "Crown", left = "10 o'clock", right = "2 o'clock";
};
std::string buttonsText(Role r, const std::string& m, const ButtonNames& n); // "Yellow: start/stop jam · Red: ..."

// Storage: one "key=value" line per setting, values escaped (\n, \\).
std::string serialize(const Settings& s);
Settings parse(const std::string& text); // unknown keys are ignored, bad values keep the default

// The setup page: a form with every setting, and its POST body
// (application/x-www-form-urlencoded). A blank password keeps the saved one.
// firmware: the running version; when set, the page also offers a
// firmware.bin upload (POST /update, raw body).
std::string page(const Settings& s, const std::string& message, const std::string& firmware = "",
                 const ButtonNames& names = ButtonNames());
// Returns "" when valid (out is then the new settings), else what is wrong.
std::string applyForm(const Settings& old, const std::string& body, Settings& out);

std::string urlDecode(const std::string& s);
std::string htmlEscape(const std::string& s);
// "WIFI:T:WPA;S:<ssid>;P:<password>;;" for the QR code
std::string wifiQr(const std::string& ssid, const std::string& password);

} // namespace cfg
