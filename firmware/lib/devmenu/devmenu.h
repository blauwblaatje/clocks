// Device menu (HANDOVER.md section 8): four touch quadrants at the top level,
// then short lists. No LVGL or Arduino; what each item does is up to the
// Host (the firmware, or the simulator). The screen is drawn by lib/devmenuui.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace menu {

enum class RoleId : uint8_t { JamTimer, Blockers1, Blockers2, Jammers };
const char* roleName(RoleId r);

// Answer of an action that may be refused ("" = done).
using Refusal = std::string;

class Host {
  public:
    virtual ~Host() {}
    // Game
    virtual RoleId role() const = 0;
    virtual Refusal setRole(RoleId r) = 0; // refused while a clock runs or someone is in the box
    virtual bool connectedMode() const = 0;
    virtual Refusal setConnectedMode(bool connected) = 0;
    virtual bool sidesSwapped() const = 0;
    virtual Refusal swapSides() = 0;
    virtual Refusal resetGame() = 0;   // jam timer, standalone
    virtual Refusal clearClocks() = 0; // box roles, standalone
    // Device
    virtual int brightness() const = 0; // percent
    virtual void setBrightness(int percent) = 0;
    virtual bool motorFitted() const = 0;
    virtual bool vibration() const = 0;
    virtual void setVibration(bool on) = 0;
    // Connection
    virtual std::vector<std::string> wifiNetworks() const = 0; // saved networks
    virtual int currentWifi() const = 0;                        // index, -1 none
    virtual Refusal selectWifi(int index) = 0;
    virtual Refusal startSetup() = 0;
    virtual std::vector<std::string> info() const = 0; // battery, Wi-Fi, scoreboard, write, firmware, latency
    virtual Refusal startUpdate() = 0;                 // opens the update screen (closes the menu)
    // Power
    virtual void sleep() = 0;
};

enum class Screen : uint8_t { Closed, Top, List, Lines, Message, Confirm };

struct Row {
    std::string label, value;
    bool enabled = true;
};

struct View {
    Screen screen = Screen::Closed;
    std::string title;
    std::vector<Row> rows;           // List: at most 4
    std::vector<std::string> lines;  // Lines (info), Message, Confirm question
    const char* quadrants[4] = {"Game", "Device", "Connection", "Power"};
};

class Menu {
  public:
    explicit Menu(Host& h) : h_(h) {}
    void open(uint32_t now);
    void close() { stack_.clear(); }
    bool isOpen() const { return !stack_.empty(); }
    void touch(int x, int y, uint32_t now);
    void tick(uint32_t now); // messages time out, the menu closes when idle
    View view() const;

    // Hit tests shared with lib/devmenuui.
    static int topHit(int x, int y);  // 0..3 quadrant, 4 close (centre), -1
    static int listHit(int x, int y); // 0..3 row, 4 back, -1
    static int confirmHit(int x, int y); // 1 yes, 0 no, -1

  private:
    enum class Page : uint8_t { Top, Game, Device, Connection, Power, Role, Wifi, Info, Message, ConfirmReset,
                                ConfirmClear, ConfirmSleep };
    std::vector<Row> rowsOf(Page p) const;
    void activate(Page p, int row, uint32_t now);
    void push(Page p, uint32_t now);
    void message(const std::string& m, uint32_t now);
    void result(const Refusal& r, uint32_t now, const std::string& done = "");

    Host& h_;
    std::vector<Page> stack_;
    std::string message_;
    uint32_t messageAt_ = 0, lastTouch_ = 0;
};

} // namespace menu
