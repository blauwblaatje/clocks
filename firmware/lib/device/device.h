// The device: one role at a time (jam timer or a box timer role), the CRG
// connection and the menu. Shared by the firmware and the desktop simulator;
// what differs between them is behind Platform.
#pragma once
#include <memory>
#include <stdint.h>
#include <string>

#include "boxctl.h"
#include "boxui.h"
#include "crgclient.h"
#include "devmenu.h"
#include "devmenuui.h"
#include "httpmini.h"
#include "settings.h"
#include "jtctl.h"
#include "jtui.h"
#include "lvgl.h"
#include "jamcore.h"
#include "update.h"
#include "wsposix.h"

namespace dev {

enum class Phys : uint8_t { Crown, Left, Right }; // 12, 10 and 2 o'clock

class Platform {
  public:
    virtual ~Platform() {}
    virtual uint32_t now() = 0; // ms
    virtual std::string loadCookie(const char* role) = 0;
    virtual void saveCookie(const char* role, const std::string& cookie) = 0;
    virtual void setBrightness(int percent) = 0;
    virtual void sleep() = 0; // board: deep sleep (does not return); simulator: no-op
    virtual void log(const std::string& line) = 0;
    virtual std::vector<std::string> info() = 0; // battery, Wi-Fi, firmware ...
    // Settings, stored as text (cfg::serialize)
    virtual std::string loadSettings() = 0;
    virtual void saveSettings(const std::string& text) = 0;
    // Scoreboard to use: from the Wi-Fi network joined. "" = none.
    virtual void scoreboard(const cfg::Settings& s, std::string& host, int& port) = 0;
    virtual void networksChanged(const cfg::Settings& s) = 0; // rejoin if needed
    virtual int currentNetwork(const cfg::Settings& s) = 0;     // index of the joined network, -1 none
    virtual void joinNetwork(const cfg::Settings& s, int index) = 0;
    // Battery: false while unknown (no reading yet).
    virtual bool battery(int& percent, bool& charging) = 0;
    // Setup: open the access point (board) and say how to reach the page.
    struct SetupInfo {
        std::string qr;                 // text for the QR code
        std::vector<std::string> lines; // shown under it
        int httpPort = 80;
        std::string error;              // could not start
    };
    virtual SetupInfo startSetup() = 0;
    virtual void stopSetup() = 0;
    // Firmware updates (lib/update, HANDOVER section 8)
    virtual std::string firmwareVersion() = 0;
    virtual upd::Slot& updateSlot() = 0;                                    // the inactive app slot
    virtual std::unique_ptr<upd::Fetch> fetch(const std::string& url) = 0; // nullptr = cannot start
    virtual bool usbPower() = 0;                                            // powered over USB (rules for a low battery)
    virtual void restart() = 0;                                             // boot the new firmware
};

// One role: the jam timer or a box timer role.
struct App {
    virtual ~App() {}
    virtual void button(Phys b, bool down, uint32_t now) = 0;
    virtual void touch(int x, int y, uint32_t now) = 0;
    virtual void tick(uint32_t now) = 0;
    virtual void show(uint32_t now) = 0;
    virtual lv_obj_t* root() = 0;
    virtual bool takeMenuRequest() = 0;
    virtual bool idle(uint32_t now) = 0; // no clock running, nobody in the box
    virtual bool connected() const = 0;
    virtual void setConnected(bool c) = 0;
    // Menu actions; the firmware has no RTTI, so no dynamic_cast.
    virtual bool sidesSwapped() const { return false; }
    virtual bool requestSwap(uint32_t) { return true; }
    virtual void resetGame() {}
    virtual void clearClocks() {}
};

const char* roleArg(menu::RoleId r); // "jt", "blockers1", "blockers2", "jammers"

class Device : public menu::Host {
  public:
    // host "" = no scoreboard configured (standalone only).
    // Role, scoreboard and the rest come from the stored settings.
    Device(Platform& p, lv_obj_t* screen, const jt::Fonts& f);
    ~Device() override;

    void button(Phys b, bool down);
    void loop(); // call often: network, controllers, screens (not lv_timer_handler)

    // menu::Host
    menu::RoleId role() const override { return role_; }
    menu::Refusal setRole(menu::RoleId r) override;
    bool connectedMode() const override;
    menu::Refusal setConnectedMode(bool c) override;
    bool sidesSwapped() const override;
    menu::Refusal swapSides() override;
    menu::Refusal resetGame() override;
    menu::Refusal clearClocks() override;
    int brightness() const override { return brightness_; }
    void setBrightness(int p) override;
    bool motorFitted() const override { return settings_.motorFitted; }
    bool vibration() const override { return false; }
    void setVibration(bool) override {}
    std::vector<std::string> wifiNetworks() const override;
    int currentWifi() const override { return p_.currentNetwork(settings_); }
    menu::Refusal selectWifi(int index) override;
    menu::Refusal startSetup() override;
    bool inSetup() const { return setupRoot_ != nullptr; }
    const cfg::Settings& settings() const { return settings_; }
    std::vector<std::string> info() const override;
    menu::Refusal startUpdate() override;
    bool inUpdate() const { return updateRoot_ != nullptr; }
    void sleep() override;

  private:
    static void onTouch(lv_event_t* e);
    static void onUpdateTouch(lv_event_t* e);
    void openUpdateScreen();
    void closeUpdateScreen();
    void showUpdate(uint32_t now);
    void updateTouch(int x, int y, uint32_t now);
    void start(bool connected);
    void pump(uint32_t now);
    void flush();
    void stopSetupScreen();
    void restartIfScoreboardChanged();
    void updateBattery(uint32_t now);
    void sleepCheck(uint32_t now);
    http::Response handle(const http::Request& r);
    void applySettings(const cfg::Settings& s, std::string& note);
    jamcore::Config standaloneConfig() const;

    Platform& p_;
    lv_obj_t* screen_;
    jt::Fonts fonts_;
    std::string host_;
    int port_;
    menu::RoleId role_;
    std::unique_ptr<crg::Client> client_;
    WsConn ws_;
    App* app_ = nullptr;
    menu::Menu menu_;
    menu::Ui* menuUi_ = nullptr;
    cfg::Settings settings_;
    http::Server http_;
    lv_obj_t* setupRoot_ = nullptr;
    lv_obj_t* battery_ = nullptr;
    uint32_t batteryAt_ = 0, netCheckAt_ = 0;
    // automatic sleep
    uint32_t activityAt_ = 0, warnAt_ = 0;
    lv_obj_t* sleepNotice_ = nullptr;
    std::string setupMessage_;
    int brightness_ = 100;
    bool ignoring_ = false, sleeping_ = false, pendingRestart_ = false;
    Phys ignoreBtn_ = Phys::Crown;

    // Updates
    struct UpdHost : upd::Host {
        Device& d;
        explicit UpdHost(Device& dd) : d(dd) {}
        std::unique_ptr<upd::Fetch> fetch(const std::string& url) override { return d.p_.fetch(url); }
        upd::Slot& slot() override { return d.p_.updateSlot(); }
        upd::Conditions conditions() override;
        std::string version() override { return d.p_.firmwareVersion(); }
    } updHost_{*this};
    upd::Flow update_{updHost_};
    lv_obj_t* updateRoot_ = nullptr;
    lv_obj_t *updTitle_ = nullptr, *updLine_ = nullptr, *updBar_ = nullptr, *updFill_ = nullptr, *updNote_ = nullptr,
             *updHint_ = nullptr, *updYes_ = nullptr, *updNo_ = nullptr;
    std::string updNote_s;      // refusal or error under the title
    std::string updKey_;        // what the screen shows now, to redraw only on change
    uint32_t installedAt_ = 0;  // restart a moment after "Installed"
};

} // namespace dev
