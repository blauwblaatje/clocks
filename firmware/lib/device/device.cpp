#include "lvset.h"
#include "device.h"

#include "boxcore.h"
#include "jamcore.h"
#include "buttons.h"
#include "lcdui.h"
#include "scale.h"

using ui::px;

namespace dev {

namespace {

struct JtApp : App {
    jamcore::JamTimer local;
    jt::Controller ctl;
    jt::Ui ui;
    std::unique_ptr<lcd::JtFace> face; // over the classic screen (lcdui.h)
    jt::Fonts fonts;
    JtApp(lv_obj_t* scr, crg::Client* c, bool conn, const jt::Fonts& f, const jamcore::Config& cfg)
        : local(cfg), ctl(local, c, conn ? jt::Mode::Connected : jt::Mode::Standalone), ui(scr, f), fonts(f) {}
    ~JtApp() override {
        face.reset();
        lv_obj_delete(ui.root());
    }
    void setFace(int i) override {
        face.reset();
        // beside the classic screen, not in it (box::Ui rebuilds its children)
        if (i > 0) face.reset(new lcd::JtFace(lv_obj_get_parent(ui.root()), fonts, (lcd::Palette)i));
    }
    void button(Phys b, bool down, uint32_t now) override {
        ctl.button(b == Phys::Crown ? jt::Btn::Crown : b == Phys::Right ? jt::Btn::Timeout : jt::Btn::Undo, down, now);
    }
    void touch(int x, int y, uint32_t now) override { ctl.touch(x, y, now); }
    void tick(uint32_t now) override { ctl.tick(now); }
    void show(uint32_t now) override {
        jt::View v = ctl.view(now);
        ui.show(v, now);
        if (face) face->show(v, now);
    }
    lv_obj_t* root() override { return ui.root(); }
    bool takeMenuRequest() override { return ctl.takeMenuRequest(); }
    bool idle(uint32_t) override { return ctl.idle(); }
    bool connected() const override { return ctl.mode() == jt::Mode::Connected; }
    void setConnected(bool c) override { ctl.setMode(c ? jt::Mode::Connected : jt::Mode::Standalone); }
    void resetGame() override { ctl.resetLocalGame(); }
};

struct BoxApp : App {
    boxcore::BoxTimer local;
    box::Controller ctl;
    box::Ui ui;
    std::unique_ptr<lcd::BoxFace> face;
    jt::Fonts fonts;
    BoxApp(lv_obj_t* scr, box::Role r, int team, crg::Client* c, bool conn, const jt::Fonts& f)
        : ctl(r, team, local, c, conn ? box::Mode::Connected : box::Mode::Standalone), ui(scr, f), fonts(f) {}
    ~BoxApp() override {
        face.reset();
        lv_obj_delete(ui.root());
    }
    void setFace(int i) override {
        face.reset();
        if (i > 0) face.reset(new lcd::BoxFace(lv_obj_get_parent(ui.root()), fonts, (lcd::Palette)i));
    }
    void button(Phys b, bool down, uint32_t now) override {
        ctl.button(b == Phys::Crown ? box::Btn::Crown : b == Phys::Left ? box::Btn::Left : box::Btn::Right, down, now);
    }
    void touch(int x, int y, uint32_t now) override { ctl.touch(x, y, now); }
    void tick(uint32_t now) override { ctl.tick(now); }
    void show(uint32_t now) override {
        box::View v = ctl.view(now);
        ui.show(v, now);
        if (face) face->show(v, now);
    }
    lv_obj_t* root() override { return ui.root(); }
    bool takeMenuRequest() override { return ctl.takeMenuRequest(); }
    bool idle(uint32_t now) override { return ctl.canSwap(now); }
    bool connected() const override { return ctl.mode() == box::Mode::Connected; }
    void setConnected(bool c) override { ctl.setMode(c ? box::Mode::Connected : box::Mode::Standalone); }
    bool sidesSwapped() const override { return ctl.swapSides(); }
    bool requestSwap(uint32_t now) override { return ctl.requestSwap(now); }
    void clearClocks() override { ctl.clearClocks(); }
};

} // namespace

const char* roleArg(menu::RoleId r) {
    switch (r) {
    case menu::RoleId::JamTimer: return "jt";
    case menu::RoleId::Blockers1: return "blockers1";
    case menu::RoleId::Blockers2: return "blockers2";
    case menu::RoleId::Jammers: return "jammers";
    }
    return "jt";
}

Device::Device(Platform& p, lv_obj_t* screen, const jt::Fonts& f)
    : p_(p), screen_(screen), fonts_(f), port_(8000), menu_(*this) {
    // Screens are drawn for 240 x 240 and scaled to the display (scale.h).
    ui::gScreen = p_.displaySize();
    settings_ = cfg::parse(p_.loadSettings());
    role_ = (menu::RoleId)settings_.role;
    p_.scoreboard(settings_, host_, port_);
    p_.setBrightness(settings_.brightness);
    brightness_ = settings_.brightness;
    start(!host_.empty());
    menuUi_ = new menu::Ui(screen_, fonts_);
    lv_obj_add_event_cb(app_->root(), onTouch, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(menuUi_->root(), onTouch, LV_EVENT_PRESSED, this);
    // Battery: a small pill at the bottom edge, above every screen.
    battery_ = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(battery_, fonts_.m12, 0);
    lv_obj_set_style_bg_color(battery_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(battery_, LV_OPA_80, 0);
    lv_obj_set_style_pad_hor(battery_, px(6), 0);
    lv_obj_set_style_radius(battery_, px(6), 0);
    lv_label_set_text(battery_, "");
    // inside the ring (radius 113), below the hint line
    lv_obj_align_to(battery_, screen_, LV_ALIGN_BOTTOM_MID, 0, -((lv_obj_get_height(screen_) - px(240)) / 2) - px(14));
    lv_obj_add_flag(battery_, LV_OBJ_FLAG_HIDDEN);
}

void Device::updateBattery(uint32_t now) {
    if (batteryAt_ && now - batteryAt_ < 2000) return;
    batteryAt_ = now ? now : 1;
    int pct = 0;
    bool charging = false;
    if (!p_.battery(pct, charging)) {
        ui::visible(battery_, false);
        return;
    }
    std::string t = std::to_string(pct) + "%" + (charging ? " \xe2\x86\xaf" : ""); // ↯ while charging
    if (t != lv_label_get_text(battery_)) lv_label_set_text(battery_, t.c_str());
    ui::textColor(battery_, lv_color_hex(pct <= 15 && !charging ? 0xff6a5e : 0xb4b4b4));
    ui::visible(battery_, true);
}

// Automatic sleep (HANDOVER section 8): after sleepMinutes with no clock
// running, nobody in the box and no press; 10 s notice first.
void Device::sleepCheck(uint32_t now) {
    const uint32_t kWarnMs = 10000;
    if (!activityAt_) activityAt_ = now ? now : 1;
    bool idle = settings_.sleepMinutes > 0 && !menu_.isOpen() && !setupRoot_ && !updateRoot_ && !update_.busy() &&
                !sleeping_ && app_->idle(now);
    if (!idle) {
        activityAt_ = now;
        if (warnAt_) {
            warnAt_ = 0;
            lv_obj_add_flag(sleepNotice_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    uint32_t limit = (uint32_t)settings_.sleepMinutes * 60000u;
    if (!warnAt_ && now - activityAt_ >= limit - kWarnMs) {
        warnAt_ = now;
        if (!sleepNotice_) {
            sleepNotice_ = lv_label_create(lv_layer_top());
            lv_obj_set_style_text_font(sleepNotice_, fonts_.m14, 0);
            lv_obj_set_style_text_color(sleepNotice_, lv_color_white(), 0);
            lv_obj_set_style_text_align(sleepNotice_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_bg_color(sleepNotice_, lv_color_hex(0x202024), 0);
            lv_obj_set_style_bg_opa(sleepNotice_, LV_OPA_90, 0);
            lv_obj_set_style_border_color(sleepNotice_, lv_color_hex(0x6fb6ff), 0);
            lv_obj_set_style_border_width(sleepNotice_, px(1), 0);
            lv_obj_set_style_radius(sleepNotice_, px(10), 0);
            lv_obj_set_style_pad_all(sleepNotice_, px(10), 0);
        }
        lv_obj_remove_flag(sleepNotice_, LV_OBJ_FLAG_HIDDEN);
    }
    if (warnAt_) {
        uint32_t left = now - warnAt_ >= kWarnMs ? 0 : (kWarnMs - (now - warnAt_) + 999) / 1000;
        std::string t = "Sleeping in " + std::to_string(left) + " s\nPress any button to stay on";
        if (t != lv_label_get_text(sleepNotice_)) lv_label_set_text(sleepNotice_, t.c_str());
        lv_obj_align_to(sleepNotice_, screen_, LV_ALIGN_CENTER, 0, 0);
        if (left == 0) {
            warnAt_ = 0;
            lv_obj_add_flag(sleepNotice_, LV_OBJ_FLAG_HIDDEN);
            activityAt_ = now;
            p_.log("automatic sleep");
            sleep();
        }
    }
}

std::vector<std::string> Device::wifiNetworks() const {
    std::vector<std::string> n;
    for (auto& x : settings_.networks) n.push_back(x.ssid);
    return n;
}

menu::Refusal Device::selectWifi(int index) {
    if (index < 0 || index >= (int)settings_.networks.size()) return "No such network";
    if (!app_->idle(p_.now())) return "Not while a clock runs or someone is in the box";
    p_.joinNetwork(settings_, index);
    restartIfScoreboardChanged();
    return "";
}

// The joined network decides the scoreboard: reconnect there if it changed.
void Device::restartIfScoreboardChanged() {
    std::string host;
    int port = 8000;
    p_.scoreboard(settings_, host, port);
    if (host == host_ && port == port_) return;
    bool conn = app_->connected() || host_.empty();
    host_ = host;
    port_ = port;
    start(conn && !host_.empty());
    lv_obj_add_event_cb(app_->root(), onTouch, LV_EVENT_PRESSED, this);
    lv_obj_move_foreground(menuUi_->root());
    if (setupRoot_) lv_obj_move_foreground(setupRoot_);
    if (updateRoot_) lv_obj_move_foreground(updateRoot_);
}

Device::~Device() {
    http_.stop();
    delete app_;
    delete menuUi_;
}

jamcore::Config Device::standaloneConfig() const {
    jamcore::Config c;
    c.jamMs = settings_.jamMs;
    c.lineupMs = settings_.lineupMs;
    c.timeoutMs = settings_.timeoutMs;
    c.periodMs = settings_.periodMs;
    c.periods = (uint8_t)settings_.periods;
    c.jamAutoEnd = settings_.jamAutoEnd;
    c.periodAfterTimeout = (jamcore::PeriodAfterTimeout)settings_.periodAfterTimeout;
    c.undoLevels = (uint8_t)settings_.undoLevels;
    return c;
}

// ---------- setup ----------

menu::Refusal Device::startSetup() {
    Platform::SetupInfo info = p_.startSetup();
    if (!info.error.empty()) return info.error;
    std::string err;
    if (!http_.running() && !serveWeb(info.httpPort, err)) {
        p_.stopSetup();
        return "Setup page failed: " + err;
    }
    menu_.close();
    buildSetupScreen(info);
    return "";
}

// The setup page, with firmware.bin uploads streamed into the update slot.
bool Device::serveWeb(int port, std::string& err) {
    if (!http_.start(port, [this](const http::Request& r) { return handle(r); }, err)) return false;
    p_.log("setup page on port " + std::to_string(http_.port()));
    http::Upload u;
    u.path = "/update";
    u.begin = [this](size_t len) {
        openUpdateScreen();
        std::string e = update_.beginUpload(len, p_.now());
        showUpdate(p_.now());
        return e;
    };
    u.chunk = [this](const char* data, size_t len) { return update_.uploadChunk((const uint8_t*)data, len, p_.now()); };
    u.end = [this] {
        http::Response r;
        r.contentType = "text/plain; charset=utf-8";
        std::string e = update_.endUpload(p_.now());
        r.status = e.empty() ? 200 : 400;
        r.body = e.empty() ? "Installed. The device restarts now." : "Update failed: " + e;
        return r;
    };
    u.abort = [this](const std::string& why) { update_.abortUpload(why); };
    http_.setUpload(u);
    return true;
}

// The setup screen: the access point and the page on it.
void Device::buildSetupScreen(const Platform::SetupInfo& info) {
    // Setup screen: QR code and how to connect. Any button closes it.
    setupRoot_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(setupRoot_);
    lv_obj_set_size(setupRoot_, px(240), px(240));
    lv_obj_center(setupRoot_);
    lv_obj_set_style_radius(setupRoot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(setupRoot_, true, 0);
    lv_obj_set_style_bg_color(setupRoot_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(setupRoot_, LV_OPA_COVER, 0);
    lv_obj_t* t = lv_label_create(setupRoot_);
    lv_obj_set_style_text_font(t, fonts_.b17, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_label_set_text(t, "Setup");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, px(18));
    lv_obj_t* qr = lv_qrcode_create(setupRoot_);
    lv_qrcode_set_size(qr, px(96));
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, info.qr.data(), (uint32_t)info.qr.size());
    lv_obj_set_style_border_color(qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr, px(4), 0);
    lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, px(42));
    int y = 150;
    for (auto& l : info.lines) {
        lv_obj_t* lb = lv_label_create(setupRoot_);
        lv_obj_set_style_text_font(lb, fonts_.m12, 0);
        lv_obj_set_style_text_color(lb, lv_color_hex(0xd6d6d6), 0);
        lv_label_set_text(lb, l.c_str());
        lv_obj_align(lb, LV_ALIGN_TOP_MID, 0, px(y));
        y += 16;
    }
    lv_obj_t* hint = lv_label_create(setupRoot_);
    lv_obj_set_style_text_font(hint, fonts_.m12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8f8f8f), 0);
    lv_label_set_text(hint, "Any button: close");
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, px(y + 4));
}

void Device::stopSetupScreen() {
    if (!setupRoot_) return;
    lv_obj_delete(setupRoot_);
    setupRoot_ = nullptr;
    if (!p_.webPort()) http_.stop(); // always on where the platform serves it
    p_.stopSetup();
}

http::Response Device::handle(const http::Request& r) {
    http::Response resp;
    if (r.method == "POST" && r.path == "/save") {
        cfg::Settings next;
        std::string err = cfg::applyForm(settings_, r.body, next);
        if (!err.empty()) {
            resp.body = cfg::page(settings_, err);
            return resp;
        }
        std::string note;
        applySettings(next, note);
        setupMessage_ = "Saved." + note;
        resp.status = 303;
        resp.location = "/";
        return resp;
    }
    if (r.path == "/") {
        resp.body = cfg::page(settings_, setupMessage_, p_.firmwareVersion());
        setupMessage_.clear();
        return resp;
    }
    resp.status = 404;
    resp.contentType = "text/plain";
    resp.body = "not found";
    return resp;
}

// Store, then apply what can be applied now. A running clock or someone in
// the box keeps the role and the standalone rules until the next start.
void Device::applySettings(const cfg::Settings& s, std::string& note) {
    cfg::Settings old = settings_;
    settings_ = s;
    p_.saveSettings(cfg::serialize(s));
    setBrightness(s.brightness);
    if (old.face != s.face) {
        app_->setFace(s.face);
        raiseOverlays();
    }
    bool netsChanged = old.networks.size() != s.networks.size();
    for (size_t n = 0; !netsChanged && n < s.networks.size(); n++) {
        const cfg::Network &a = old.networks[n], &b = s.networks[n];
        netsChanged = a.ssid != b.ssid || a.password != b.password || a.host != b.host || a.port != b.port;
    }
    if (netsChanged) p_.networksChanged(s);
    std::string host;
    int port = 8000;
    p_.scoreboard(s, host, port);
    bool rulesChanged = old.jamMs != s.jamMs || old.lineupMs != s.lineupMs || old.timeoutMs != s.timeoutMs ||
                        old.periodMs != s.periodMs || old.periods != s.periods || old.jamAutoEnd != s.jamAutoEnd ||
                        old.periodAfterTimeout != s.periodAfterTimeout || old.undoLevels != s.undoLevels;
    bool restart = (menu::RoleId)s.role != role_ || host != host_ || port != port_ ||
                   (rulesChanged && role_ == menu::RoleId::JamTimer) ||
                   old.swapButtons[(int)role_] != s.swapButtons[(int)role_]; // the names on screen
    if (!restart) return;
    if (!app_->idle(p_.now())) {
        note = " Some changes apply when no clock runs and the box is empty.";
        pendingRestart_ = true;
        return;
    }
    bool conn = app_->connected() || (host_.empty() && !host.empty());
    host_ = host;
    port_ = port;
    role_ = (menu::RoleId)s.role;
    start(conn && !host_.empty());
    lv_obj_add_event_cb(app_->root(), onTouch, LV_EVENT_PRESSED, this);
    lv_obj_move_foreground(menuUi_->root());
    if (setupRoot_) lv_obj_move_foreground(setupRoot_);
    if (updateRoot_) lv_obj_move_foreground(updateRoot_);
}

void Device::onTouch(lv_event_t* e) {
    Device* d = (Device*)lv_event_get_user_data(e);
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    lv_area_t a;
    lv_obj_get_coords(d->app_->root(), &a);
    int x = ui::design(pt.x - a.x1), y = ui::design(pt.y - a.y1);
    uint32_t now = d->p_.now();
    d->activityAt_ = now;
    if (d->warnAt_) {
        d->warnAt_ = 0;
        lv_obj_add_flag(d->sleepNotice_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (d->menu_.isOpen()) d->menu_.touch(x, y, now);
    else d->app_->touch(x, y, now);
}

void Device::button(Phys b, bool down) {
    uint32_t now = p_.now();
    activityAt_ = now;
    if (warnAt_) { // the sleep notice: a press cancels it and does nothing else
        warnAt_ = 0;
        lv_obj_add_flag(sleepNotice_, LV_OBJ_FLAG_HIDDEN);
        if (down) {
            ignoring_ = true;
            ignoreBtn_ = b;
        }
        return;
    }
    // Per-role setting: swap the 10 and 2 o'clock buttons.
    if (settings_.swapButtons[(int)role_] && b != Phys::Crown) b = b == Phys::Left ? Phys::Right : Phys::Left;
    if (updateRoot_) {
        // Any button closes the update screen (and stops a check or a
        // download; the running firmware stays). Not once it is installed.
        if (down && update_.state() != upd::State::Installed) {
            closeUpdateScreen();
            ignoring_ = true;
            ignoreBtn_ = b;
        }
        return;
    }
    if (setupRoot_) {
        if (down) {
            stopSetupScreen();
            ignoring_ = true;
            ignoreBtn_ = b;
        }
        return;
    }
    if (sleeping_) {
        if (b == Phys::Crown && down) { // the board wakes on a 1 s crown hold
            sleeping_ = false;
            lv_obj_remove_flag(app_->root(), LV_OBJ_FLAG_HIDDEN);
            ignoring_ = true;
            ignoreBtn_ = b;
        }
        return;
    }
    if (menu_.isOpen()) {
        if (down) {
            // A button closes the menu and never acts on the game; its
            // release is swallowed too.
            menu_.close();
            ignoring_ = true;
            ignoreBtn_ = b;
        } else {
            app_->button(b, false, now); // e.g. the hold that opened the menu
        }
        return;
    }
    if (!down && ignoring_ && b == ignoreBtn_) {
        ignoring_ = false;
        return;
    }
    app_->button(b, down, now);
}

void Device::loop() {
    if (http_.running()) http_.poll(p_.now());
    if (updateRoot_ || update_.busy()) {
        uint32_t now = p_.now();
        update_.poll(now);
        if (update_.state() == upd::State::Installed) {
            if (!installedAt_) {
                installedAt_ = now ? now : 1;
            } else if (now - installedAt_ >= 1500) { // the upload's answer goes out first
                installedAt_ = 0;
                p_.log("update installed, restarting");
                p_.restart();
            }
        }
        if (updateRoot_) showUpdate(now);
    }
    if (pendingRestart_ && app_->idle(p_.now())) {
        pendingRestart_ = false;
        std::string host;
        int port = 8000;
        p_.scoreboard(settings_, host, port);
        bool conn = app_->connected();
        host_ = host;
        port_ = port;
        role_ = (menu::RoleId)settings_.role;
        start(conn && !host_.empty());
        lv_obj_add_event_cb(app_->root(), onTouch, LV_EVENT_PRESSED, this);
        lv_obj_move_foreground(menuUi_->root());
        if (setupRoot_) lv_obj_move_foreground(setupRoot_);
        if (updateRoot_) lv_obj_move_foreground(updateRoot_);
    }
    pump(p_.now());
    updateBattery(p_.now());
    sleepCheck(p_.now());
    // The board may fall back to another saved network: follow its scoreboard.
    if (p_.now() - netCheckAt_ >= 2000) {
        netCheckAt_ = p_.now();
        // The setup page, always on once the platform can serve it.
        if (!http_.running() && p_.webPort()) {
            std::string err;
            if (!serveWeb(p_.webPort(), err)) p_.log("setup page failed: " + err);
        }
        if (app_->idle(p_.now()) && !setupRoot_ && !updateRoot_) restartIfScoreboardChanged();
    }
    app_->tick(p_.now());
    if (app_->takeMenuRequest()) menu_.open(p_.now());
    menu_.tick(p_.now());
    flush();
    app_->show(p_.now());
    menuUi_->show(menu_.view());
}

// Button names on screen (lib/buttons), with this role's swap of the side
// buttons.
void Device::setButtonNames() {
    const upd::Board& bd = p_.board();
    bool sw = settings_.swapButtons[(int)role_];
    btn::gNames = {bd.crownName, sw ? bd.rightName : bd.leftName, sw ? bd.leftName : bd.rightName,
                   bd.crownAt,   sw ? bd.rightAt : bd.leftAt,     sw ? bd.leftAt : bd.rightAt};
}

void Device::start(bool connected) {
    setButtonNames();
    delete app_;
    app_ = nullptr;
    ws_.close();
    crg::ClientConfig cfg;
    cfg.platform = "ESP32-C6";
    cfg.role = role_ == menu::RoleId::JamTimer  ? crg::Role::JamTimer
               : role_ == menu::RoleId::Jammers ? crg::Role::BoxJammers
                                                : crg::Role::BoxBlockers;
    cfg.source = role_ == menu::RoleId::JamTimer ? "jamtimer" : std::string("boxtimer-") + roleArg(role_);
    client_.reset(new crg::Client(cfg));
    std::string c = p_.loadCookie(roleArg(role_));
    if (!c.empty()) client_->setStoredCookie(c);
    crg::Client* cp = host_.empty() ? nullptr : client_.get();
    switch (role_) {
    case menu::RoleId::JamTimer: app_ = new JtApp(screen_, cp, connected, fonts_, standaloneConfig()); break;
    case menu::RoleId::Jammers: app_ = new BoxApp(screen_, box::Role::Jammers, 1, cp, connected, fonts_); break;
    default:
        app_ = new BoxApp(screen_, box::Role::Blockers, role_ == menu::RoleId::Blockers2 ? 2 : 1, cp, connected, fonts_);
    }
    app_->setFace(settings_.face);
    raiseOverlays();
}

// Menu, setup and update screens stay above the app and its face.
void Device::raiseOverlays() {
    if (menuUi_) lv_obj_move_foreground(menuUi_->root());
    if (setupRoot_) lv_obj_move_foreground(setupRoot_);
    if (updateRoot_) lv_obj_move_foreground(updateRoot_);
}

void Device::pump(uint32_t now) {
    if (host_.empty()) return;
    crg::Client& c = *client_;
    // Connect step by step: the screen keeps running while it connects.
    if (c.wantConnect(now)) {
        c.onConnecting(now);
        std::string err;
        if (!ws_.startOpen(host_, port_, c.urlPath(), c.cookieHeader(), p_.now(), err)) {
            c.onConnectFailed(p_.now());
            p_.log("connect failed: " + err);
        }
    }
    if (ws_.opening()) {
        std::string setCookie, err;
        int r = ws_.pollOpen(p_.now(), setCookie, err);
        if (r == 1) {
            c.onOpen(setCookie, p_.now());
            p_.log(std::string("connected as ") + roleArg(role_));
        } else if (r < 0) {
            c.onConnectFailed(p_.now());
            p_.log("connect failed: " + err);
        }
    }
    if (ws_.isOpen()) {
        std::vector<std::string> msgs;
        bool open = ws_.poll(msgs);
        for (auto& m : msgs) c.onFrame(m, p_.now());
        if (!open) {
            c.onClose(p_.now());
            p_.log("connection closed");
        }
    }
    if (c.tick(p_.now())) {
        ws_.close();
        c.onClose(p_.now());
        p_.log("connection dead, closed");
    }
    flush();
    if (c.takeCookieChanged()) p_.saveCookie(roleArg(role_), c.cookie());
}

void Device::flush() {
    for (auto& f : client_->outgoing()) ws_.send(f);
    client_->outgoing().clear();
}

menu::Refusal Device::setRole(menu::RoleId r) {
    if (!app_->idle(p_.now())) return "Not while a clock runs or someone is in the box";
    if (r == role_) return "";
    role_ = r;
    settings_.role = (cfg::Role)r;
    p_.saveSettings(cfg::serialize(settings_));
    bool conn = app_->connected();
    start(conn);
    lv_obj_add_event_cb(app_->root(), onTouch, LV_EVENT_PRESSED, this);
    lv_obj_move_foreground(menuUi_->root());
    return "";
}

bool Device::connectedMode() const { return app_->connected(); }

menu::Refusal Device::setConnectedMode(bool c) {
    if (c && host_.empty()) return "No scoreboard set up";
    if (!c && !app_->idle(p_.now())) return "Not while a clock runs";
    app_->setConnected(c);
    return "";
}

bool Device::sidesSwapped() const { return app_->sidesSwapped(); }

menu::Refusal Device::swapSides() {
    return app_->requestSwap(p_.now()) ? "" : "Not while a clock runs or someone is in the box";
}

menu::Refusal Device::resetGame() {
    app_->resetGame();
    return "";
}

menu::Refusal Device::clearClocks() {
    app_->clearClocks();
    return "";
}

// Menu > Device > Face: the next face, at once (no restart).
void Device::nextFace() {
    settings_.face = (settings_.face + 1) % cfg::kFaces;
    p_.saveSettings(cfg::serialize(settings_));
    app_->setFace(settings_.face);
    raiseOverlays();
}

void Device::setBrightness(int pct) {
    brightness_ = pct;
    p_.setBrightness(pct);
    if (settings_.brightness != pct) {
        settings_.brightness = pct;
        p_.saveSettings(cfg::serialize(settings_));
    }
}

std::vector<std::string> Device::info() const {
    std::vector<std::string> l = p_.info();
    l.insert(l.begin(), std::string("Role: ") + menu::roleName(role_));
    l.push_back(host_.empty() ? "Scoreboard: none" : "Scoreboard: " + host_ + ":" + std::to_string(port_));
    l.push_back(std::string("Connected: ") + (client_->connected() ? "yes" : "no") +
                (client_->mayWrite() ? "" : ", no write access"));
    l.push_back("Press to confirm: " + std::to_string(client_->lastLatencyMs()) + " ms");
    if (role_ == menu::RoleId::JamTimer) {
        std::vector<std::string> d = client_->ejtDifferences();
        if (!d.empty()) {
            std::string names;
            for (auto& x : d) names += (names.empty() ? "" : ", ") + x.substr(0, x.find(' '));
            l.push_back("Not as eJT: " + names);
        }
    }
    return l;
}

// ---------- updates ----------

upd::Conditions Device::UpdHost::conditions() {
    upd::Conditions c;
    c.idle = d.app_->idle(d.p_.now());
    bool charging = false;
    c.batteryKnown = d.p_.battery(c.batteryPct, charging);
    c.usbPower = charging || d.p_.usbPower();
    return c;
}

menu::Refusal Device::startUpdate() {
    if (settings_.updateUrl.empty()) return "No release address (setup page)";
    menu_.close();
    openUpdateScreen();
    update_.check(upd::releaseUrl(settings_.updateUrl, p_.board()), p_.now());
    showUpdate(p_.now());
    return "";
}

static lv_obj_t* updLabel(lv_obj_t* parent, const lv_font_t* f, uint32_t color, int y) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, px(190));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, "");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, px(y));
    return l;
}

static lv_obj_t* updBox(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, px(x), px(y));
    lv_obj_set_size(o, px(w), px(h));
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, px(h > 20 ? 10 : h / 2), 0);
    return o;
}

// Update screen (docs/display.md): title, one line, a progress bar, a note,
// Install / Cancel when an update is offered, a hint at the bottom.
void Device::openUpdateScreen() {
    if (updateRoot_) return;
    updateRoot_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(updateRoot_);
    lv_obj_remove_flag(updateRoot_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(updateRoot_, px(240), px(240));
    lv_obj_center(updateRoot_);
    lv_obj_set_style_radius(updateRoot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(updateRoot_, true, 0);
    lv_obj_set_style_bg_color(updateRoot_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(updateRoot_, LV_OPA_COVER, 0);
    lv_obj_add_flag(updateRoot_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(updateRoot_, onUpdateTouch, LV_EVENT_PRESSED, this);
    updTitle_ = updLabel(updateRoot_, fonts_.b17, 0xffffff, 46);
    updLine_ = updLabel(updateRoot_, fonts_.m14, 0xd6d6d6, 76);
    updBar_ = updBox(updateRoot_, 40, 104, 160, 8, 0x2b2b2b);
    updFill_ = updBox(updBar_, 0, 0, 0, 8, 0x3ddc84);
    updNote_ = updLabel(updateRoot_, fonts_.m12, 0x8f8f8f, 118);
    updYes_ = updBox(updateRoot_, 38, 158, 80, 44, 0x3ddc84);
    lv_obj_t* yl = updLabel(updYes_, fonts_.b17, 0x000000, 0);
    lv_obj_set_width(yl, LV_SIZE_CONTENT);
    lv_label_set_text(yl, "Install");
    lv_obj_center(yl);
    updNo_ = updBox(updateRoot_, 122, 158, 80, 44, 0x2f2f33);
    lv_obj_t* nl = updLabel(updNo_, fonts_.b17, 0xffffff, 0);
    lv_obj_set_width(nl, LV_SIZE_CONTENT);
    lv_label_set_text(nl, "Cancel");
    lv_obj_center(nl);
    updHint_ = updLabel(updateRoot_, fonts_.m12, 0x8f8f8f, 208);
    updNote_s.clear();
    updKey_.clear();
    installedAt_ = 0;
}

void Device::closeUpdateScreen() {
    if (update_.state() == upd::State::Receiving) update_.abortUpload("Cancelled on the device");
    update_.reset();
    if (updateRoot_) lv_obj_delete(updateRoot_);
    updateRoot_ = nullptr;
    updTitle_ = updLine_ = updBar_ = updFill_ = updNote_ = updHint_ = updYes_ = updNo_ = nullptr;
}

static std::string hostOf(const std::string& url) {
    size_t s = url.find("://");
    s = s == std::string::npos ? 0 : s + 3;
    return url.substr(s, url.find('/', s) - s);
}

static std::string megabytes(long bytes) {
    char b[16];
    snprintf(b, sizeof b, "%.1f MB", bytes / 1048576.0);
    return b;
}

void Device::showUpdate(uint32_t) {
    if (!updateRoot_) return;
    using upd::State;
    State s = update_.state();
    const upd::Release& r = update_.release();
    std::string title, line, note = updNote_s, hint;
    uint32_t noteColor = 0xff6a5e;
    bool bar = false, buttons = false;
    int pm = update_.permille();
    switch (s) {
    case State::Idle:
    case State::Checking:
        title = "Update";
        line = "Checking " + hostOf(settings_.updateUrl);
        hint = "Any button: cancel";
        break;
    case State::UpToDate:
        title = "Up to date";
        line = "Firmware " + p_.firmwareVersion();
        hint = "Any button: close";
        break;
    case State::Available:
        title = "Update to " + r.version + "?";
        line = "Now " + p_.firmwareVersion() + " \xc2\xb7 " + megabytes(r.size);
        buttons = true;
        hint = "Any button: cancel";
        break;
    case State::Downloading:
    case State::Receiving:
        title = s == State::Downloading ? "Updating" : "Receiving update";
        line = std::to_string(pm / 10) + " %";
        bar = true;
        note = "Do not power off";
        noteColor = 0xd6d6d6;
        hint = s == State::Downloading ? "Any button: cancel" : "From the setup page";
        break;
    case State::Installed:
        title = "Update installed";
        line = "Restarting\xe2\x80\xa6";
        note.clear();
        break;
    case State::Failed:
        title = "Update failed";
        note = update_.message();
        hint = "Any button: close";
        break;
    }
    std::string key = title + "|" + line + "|" + note + "|" + hint + (buttons ? "b" : "") + (bar ? "p" : "");
    if (key == updKey_) return;
    updKey_ = key;
    lv_label_set_text(updTitle_, title.c_str());
    lv_label_set_text(updLine_, line.c_str());
    lv_label_set_text(updNote_, note.c_str());
    ui::textColor(updNote_, lv_color_hex(noteColor));
    lv_label_set_text(updHint_, hint.c_str());
    auto vis = [](lv_obj_t* o, bool on) {
        if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    };
    vis(updBar_, bar);
    lv_obj_set_width(updFill_, px(160 * pm / 1000));
    vis(updYes_, buttons);
    vis(updNo_, buttons);
}

void Device::onUpdateTouch(lv_event_t* e) {
    Device* d = (Device*)lv_event_get_user_data(e);
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    lv_area_t a;
    lv_obj_get_coords(d->updateRoot_, &a);
    d->activityAt_ = d->p_.now();
    d->updateTouch(ui::design(pt.x - a.x1), ui::design(pt.y - a.y1), d->p_.now());
}

void Device::updateTouch(int x, int y, uint32_t now) {
    if (update_.state() != upd::State::Available || y < 158 || y >= 202) return;
    if (x >= 38 && x < 118) {
        std::string e = update_.install(now);
        updNote_s = e; // a refusal stays on screen, the offer too
        showUpdate(now);
    } else if (x >= 122 && x < 202) {
        closeUpdateScreen();
    }
}

void Device::sleep() {
    sleeping_ = true;
    lv_obj_add_flag(app_->root(), LV_OBJ_FLAG_HIDDEN);
    p_.sleep(); // the board: deep sleep, wakes as a restart
}

} // namespace dev
