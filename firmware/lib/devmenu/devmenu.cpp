#include "devmenu.h"

namespace menu {

const char* roleName(RoleId r) {
    switch (r) {
    case RoleId::JamTimer: return "Jam timer";
    case RoleId::Blockers1: return "T1 blockers";
    case RoleId::Blockers2: return "T2 blockers";
    case RoleId::Jammers: return "Jammers";
    }
    return "?";
}

static const uint32_t kMessageMs = 2500;
static const uint32_t kIdleMs = 30000;

// ---------- hit tests (layout shared with lib/devmenuui) ----------

int Menu::topHit(int x, int y) {
    int dx = x - 120, dy = y - 120;
    if (dx * dx + dy * dy <= 40 * 40) return 4;
    if (dx * dx + dy * dy > 120 * 120) return -1;
    return (y < 120 ? 0 : 2) + (x < 120 ? 0 : 1);
}

int Menu::listHit(int x, int y) {
    for (int i = 0; i < 4; i++) {
        int top = 58 + i * 34;
        if (y >= top && y < top + 30 && x >= 30 && x < 210) return i;
    }
    if (y >= 196 && y < 224 && x >= 76 && x < 164) return 4;
    return -1;
}

int Menu::confirmHit(int x, int y) {
    if (y < 130 || y >= 178) return -1;
    if (x >= 38 && x < 118) return 1;
    if (x >= 122 && x < 202) return 0;
    return -1;
}

// ---------- navigation ----------

void Menu::open(uint32_t now) {
    stack_.assign(1, Page::Top);
    lastTouch_ = now;
}

void Menu::push(Page p, uint32_t now) {
    stack_.push_back(p);
    lastTouch_ = now;
}

void Menu::message(const std::string& m, uint32_t now) {
    message_ = m;
    messageAt_ = now;
    push(Page::Message, now);
}

void Menu::result(const Refusal& r, uint32_t now, const std::string& done) {
    if (!r.empty()) message(r, now);
    else if (!done.empty()) message(done, now);
}

std::vector<Row> Menu::rowsOf(Page p) const {
    std::vector<Row> r;
    switch (p) {
    case Page::Game: {
        bool jt = h_.role() == RoleId::JamTimer;
        r.push_back({"Role", roleName(h_.role())});
        if (!jt) r.push_back({"Swap sides", h_.sidesSwapped() ? "swapped" : "normal"});
        r.push_back({"Mode", h_.connectedMode() ? "connected" : "standalone"});
        if (jt) r.push_back({"Reset game", "", !h_.connectedMode()});
        else r.push_back({"Clear clocks", "", !h_.connectedMode()});
        break;
    }
    case Page::Device:
        r.push_back({"Brightness", std::to_string(h_.brightness()) + "%"});
        if (!h_.faceName().empty()) r.push_back({"Face", h_.faceName()});
        if (h_.motorFitted()) r.push_back({"Vibration", h_.vibration() ? "on" : "off"});
        break;
    case Page::Connection: {
        std::vector<std::string> nets = h_.wifiNetworks();
        int cur = h_.currentWifi();
        r.push_back({"Wi-Fi", cur >= 0 && cur < (int)nets.size() ? nets[cur] : "none"});
        r.push_back({"Setup", "access point"});
        r.push_back({"Info", ""});
        r.push_back({"Update", ""});
        break;
    }
    case Page::Power: r.push_back({"Sleep", "hold crown 1 s to wake"}); break;
    case Page::Role:
        for (RoleId id : {RoleId::JamTimer, RoleId::Blockers1, RoleId::Blockers2, RoleId::Jammers})
            r.push_back({roleName(id), h_.role() == id ? "current" : ""});
        break;
    case Page::Wifi: {
        std::vector<std::string> nets = h_.wifiNetworks();
        for (size_t i = 0; i < nets.size() && i < 4; i++)
            r.push_back({nets[i], (int)i == h_.currentWifi() ? "current" : ""});
        if (r.empty()) r.push_back({"No saved networks", "use Setup", false});
        break;
    }
    default: break;
    }
    return r;
}

void Menu::activate(Page p, int row, uint32_t now) {
    std::vector<Row> rows = rowsOf(p);
    if (row < 0 || row >= (int)rows.size() || !rows[row].enabled) return;
    const std::string& label = rows[row].label;
    switch (p) {
    case Page::Game:
        if (label == "Role") push(Page::Role, now);
        else if (label == "Swap sides") result(h_.swapSides(), now);
        else if (label == "Mode") result(h_.setConnectedMode(!h_.connectedMode()), now);
        else if (label == "Reset game") push(Page::ConfirmReset, now);
        else if (label == "Clear clocks") push(Page::ConfirmClear, now);
        break;
    case Page::Device:
        if (label == "Brightness") h_.setBrightness(h_.brightness() >= 100 ? 25 : h_.brightness() + 25);
        else if (label == "Face") h_.nextFace();
        else if (label == "Vibration") h_.setVibration(!h_.vibration());
        break;
    case Page::Connection:
        if (label == "Wi-Fi") push(Page::Wifi, now);
        else if (label == "Setup") result(h_.startSetup(), now);
        else if (label == "Info") push(Page::Info, now);
        else if (label == "Update") result(h_.startUpdate(), now);
        break;
    case Page::Power: push(Page::ConfirmSleep, now); break;
    case Page::Role: {
        Refusal r = h_.setRole((RoleId)row);
        if (r.empty()) close();
        else message(r, now);
        break;
    }
    case Page::Wifi: result(h_.selectWifi(row), now, "Joining " + label); break;
    default: break;
    }
}

void Menu::touch(int x, int y, uint32_t now) {
    if (stack_.empty()) return;
    lastTouch_ = now;
    Page p = stack_.back();
    switch (p) {
    case Page::Top: {
        int h = topHit(x, y);
        if (h == 4) close();
        else if (h >= 0) push((Page)((int)Page::Game + h), now);
        break;
    }
    case Page::Message: stack_.pop_back(); break;
    case Page::ConfirmReset:
    case Page::ConfirmClear:
    case Page::ConfirmSleep: {
        int h = confirmHit(x, y);
        if (h < 0) break;
        stack_.pop_back();
        if (h == 1) {
            if (p == Page::ConfirmReset) result(h_.resetGame(), now, "Game reset");
            else if (p == Page::ConfirmClear) result(h_.clearClocks(), now, "Clocks cleared");
            else {
                close();
                h_.sleep();
            }
        }
        break;
    }
    default: {
        int h = listHit(x, y);
        if (h == 4) stack_.pop_back();
        else if (p != Page::Info && h >= 0) activate(p, h, now);
        break;
    }
    }
}

void Menu::tick(uint32_t now) {
    if (stack_.empty()) return;
    if (stack_.back() == Page::Message && now - messageAt_ >= kMessageMs) stack_.pop_back();
    if (!stack_.empty() && now - lastTouch_ >= kIdleMs) close();
}

View Menu::view() const {
    View v;
    if (stack_.empty()) return v;
    Page p = stack_.back();
    static const char* const titles[] = {"", "Game", "Device", "Connection", "Power", "Role", "Wi-Fi", "Info"};
    switch (p) {
    case Page::Top: v.screen = Screen::Top; break;
    case Page::Message:
        v.screen = Screen::Message;
        v.lines.push_back(message_);
        break;
    case Page::ConfirmReset:
    case Page::ConfirmClear:
    case Page::ConfirmSleep:
        v.screen = Screen::Confirm;
        v.lines.push_back(p == Page::ConfirmReset ? "Reset the game?" : p == Page::ConfirmClear ? "Clear all clocks?" : "Sleep now?");
        break;
    case Page::Info:
        v.screen = Screen::Lines;
        v.title = titles[(int)p];
        v.lines = h_.info();
        break;
    default:
        v.screen = Screen::List;
        v.title = titles[(int)p];
        v.rows = rowsOf(p);
        break;
    }
    return v;
}

} // namespace menu
