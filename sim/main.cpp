// Desktop simulator for the device screens: LVGL in an SDL window, driven by
// the same controllers and client core as the firmware.
//
//   sim [--role jt|blockers1|blockers2|jammers]   standalone (default role jt)
//   sim --crg HOST:PORT [--role ...]               connected to a CRG scoreboard
//   sim --shots DIR                                no window: every screen state as DIR/*.ppm
//   sim --ctl [--crg ...] [--role ...]             no window: scripted on stdin (see runCtl)
//   sim --board stopwatch ...                      the M5Stack StopWatch (466 x 466) instead of the
//                                                  Waveshare board (240 x 240), with any of the above
//
// Keys: Space or C = crown (12 o'clock), U or Left = 10 o'clock button,
// T or Right = 2 o'clock button. Hold a key as you would hold the button.
// Mouse = touch. Window is zoomed x2 (the StopWatch's x1).
#include <SDL2/SDL.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <ctime>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "crgclient.h"
#include "jamcore.h"
#include "jtctl.h"
#include "jtfonts.h"
#include "jtfonts466.h"
#include "scale.h"
#include "jtui.h"
#include "boxcore.h"
#include "boxctl.h"
#include "boxui.h"
#include "devmenu.h"
#include "devmenuui.h"
#include "lvgl.h"
#include "device.h"
#include "httpget.h"

#ifndef SIM_FONT_DIR
#define SIM_FONT_DIR "deps/montserrat"
#endif

static bool useTtf = false;
// --board: which board the simulator stands in for (display size, fonts, updates)
static const upd::Board* simBoard = &upd::kWaveshareC6;
static int simSize = 240;

static jt::Fonts loadFonts() {
    if (!useTtf) return simSize > 240 ? jt::bitmapFonts466() : jt::bitmapFonts();
    // The TTF data is kept in memory: tiny_ttf on a file seeks for every glyph.
    // No kerning, as with the bitmap fonts the device will use.
    auto ttf = [](const char* weight, int size) {
        static std::vector<std::string> data;
        std::string p = std::string(SIM_FONT_DIR) + "/Montserrat-" + weight + ".ttf";
        std::ifstream in(p, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "cannot load %s\n", p.c_str());
            std::exit(1);
        }
        data.reserve(16);
        data.emplace_back(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return (const lv_font_t*)lv_tiny_ttf_create_data_ex(data.back().data(), data.back().size(), ui::px(size),
                                                            LV_FONT_KERNING_NONE, 256);
    };
    jt::Fonts f;
    f.m12 = ttf("Medium", 12);
    f.m14 = ttf("Medium", 14);
    f.b12 = ttf("Bold", 12);
    f.b17 = ttf("Bold", 17);
    f.b27 = ttf("Bold", 27);
    f.clock72 = ttf("SemiBold", 72);
    f.clock43 = ttf("SemiBold", 43);
    f.clock27 = ttf("SemiBold", 27);
    f.m13 = ttf("Medium", 13);
    f.b20 = ttf("Bold", 20);
    f.b23 = ttf("Bold", 23);
    f.b29 = ttf("Bold", 29);
    f.clock51 = ttf("SemiBold", 51);
    f.clock36 = ttf("SemiBold", 36);
    return f;
}

// ---------------- interactive ----------------

enum class Phys : uint8_t { Crown, Left, Right }; // 12, 10 and 2 o'clock

struct KeyEv {
    dev::Phys b;
    bool down;
};
static std::deque<KeyEv> keys;

static int keyWatch(void*, SDL_Event* e) {
    if ((e->type != SDL_KEYDOWN && e->type != SDL_KEYUP) || e->key.repeat) return 0;
    dev::Phys b;
    switch (e->key.keysym.sym) {
    case SDLK_SPACE:
    case SDLK_c: b = dev::Phys::Crown; break;
    case SDLK_t:
    case SDLK_RIGHT: b = dev::Phys::Right; break;
    case SDLK_u:
    case SDLK_LEFT: b = dev::Phys::Left; break;
    default: return 0;
    }
    keys.push_back({b, e->type == SDL_KEYDOWN});
    return 0;
}

struct SimPlatform : dev::Platform {
    uint32_t now() override { return SDL_GetTicks(); }
    static std::string file(const char* role) {
        const char* h = getenv("HOME");
        return std::string(h ? h : ".") + "/.cache/crg-sim-" + role + ".cookie";
    }
    std::string loadCookie(const char* role) override {
        std::ifstream in(file(role));
        std::string c;
        std::getline(in, c);
        return c;
    }
    void saveCookie(const char* role, const std::string& c) override { std::ofstream(file(role)) << c << "\n"; }
    void setBrightness(int p) override { lv_obj_set_style_opa(lv_screen_active(), (lv_opa_t)(p * 255 / 100), 0); }
    void sleep() override { std::printf("sleeping: press the crown to wake\n"); }
    void log(const std::string& l) override { std::printf("%s\n", l.c_str()); }
    std::vector<std::string> info() override {
        return {"Battery: n/a (simulator)", std::string("Firmware: simulator, ") + simBoard->name};
    }
    const upd::Board& board() override { return *simBoard; }
    int displaySize() override { return simSize; }

    // Settings in ~/.cache; --role and --crg override what is saved.
    std::string roleOverride, hostOverride;
    int portOverride = 8000;
    static std::string settingsFile() {
        const char* h = getenv("HOME");
        return std::string(h ? h : ".") + "/.cache/crg-sim-settings.txt";
    }
    std::string loadSettings() override {
        std::ifstream in(settingsFile());
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!roleOverride.empty()) text += "\nrole=" + roleOverride + "\n"; // later lines win
        return text;
    }
    void saveSettings(const std::string& text) override {
        std::ofstream(settingsFile()) << text;
        std::printf("settings saved to %s\n", settingsFile().c_str());
    }
    void scoreboard(const cfg::Settings& s, std::string& host, int& port) override {
        if (!hostOverride.empty()) {
            host = hostOverride;
            port = portOverride;
        } else if (!s.networks.empty()) { // the network picked in the menu (first by default)
            const cfg::Network& n = s.networks[chosen < (int)s.networks.size() ? chosen : 0];
            host = n.host;
            port = n.port;
        } else {
            host.clear();
        }
    }
    void networksChanged(const cfg::Settings&) override { chosen = 0; }
    // The simulator "joins" whichever saved network is picked in the menu.
    int chosen = 0;
    int currentNetwork(const cfg::Settings& s) override {
        return s.networks.empty() || !hostOverride.empty() ? -1 : (chosen < (int)s.networks.size() ? chosen : 0);
    }
    void joinNetwork(const cfg::Settings&, int i) override { chosen = i; }
    bool battery(int& pct, bool& charging) override { // no battery in the simulator, unless CLOCK_SIM_BATTERY
        const char* b = getenv("CLOCK_SIM_BATTERY");
        if (!b) return false;
        pct = atoi(b);
        charging = false;
        return true;
    }
    // The first free port from 8090 up (the board uses 80 on its access point).
    static int freePort() {
        for (int p = 8090; p < 8110; p++) {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(p);
            a.sin_addr.s_addr = htonl(INADDR_ANY);
            bool ok = bind(fd, (sockaddr*)&a, sizeof a) == 0;
            close(fd);
            if (ok) return p;
        }
        return 0;
    }
    SetupInfo startSetup() override {
        SetupInfo i;
        i.httpPort = freePort();
        if (!i.httpPort) {
            i.error = "No free port 8090-8109";
            return i;
        }
        std::string url = "http://localhost:" + std::to_string(i.httpPort) + "/";
        i.qr = url;
        i.lines = {"Simulator: open in a browser", url};
        std::printf("setup page: %s\n", url.c_str());
        return i;
    }
    void stopSetup() override {}

    // Updates: the "slot" is a file; finish() keeps it as the installed one.
    // CLOCK_SIM_VERSION, CLOCK_SIM_BATTERY (percent) and CLOCK_SIM_USB=1
    // stand in for the board's version, battery and USB power.
    struct FileSlot : upd::Slot {
        std::string path() const {
            const char* h = getenv("HOME");
            return std::string(h ? h : ".") + "/.cache/crg-sim-update";
        }
        FILE* f = nullptr;
        std::string begin(size_t size) override {
            if (size > 6553600) return "Update too large for the app slot";
            f = std::fopen((path() + ".part").c_str(), "wb");
            return f ? "" : "Cannot open " + path() + ".part";
        }
        bool write(const uint8_t* d, size_t n) override { return f && std::fwrite(d, 1, n, f) == n; }
        std::string finish() override {
            if (!f) return "No update in progress";
            std::fclose(f);
            f = nullptr;
            std::rename((path() + ".part").c_str(), (path() + ".bin").c_str());
            std::printf("update written to %s.bin\n", path().c_str());
            return "";
        }
        void abort() override {
            if (f) std::fclose(f);
            f = nullptr;
            std::remove((path() + ".part").c_str());
        }
    } slot;
    std::string firmwareVersion() override {
        const char* v = getenv("CLOCK_SIM_VERSION");
        return v ? v : "0.1-dev";
    }
    upd::Slot& updateSlot() override { return slot; }
    std::unique_ptr<upd::Fetch> fetch(const std::string& url) override {
        return std::unique_ptr<upd::Fetch>(new upd::HttpGet(url, [] { return (uint32_t)SDL_GetTicks(); }));
    }
    bool usbPower() override {
        const char* u = getenv("CLOCK_SIM_USB");
        return u && *u == '1';
    }
    void restart() override {
        std::printf("restart: the simulator exits\n");
        std::fflush(stdout);
        std::exit(0);
    }
};

static bool setupAtStart = false;

static int runWindow(const std::string& host, int port, const std::string& role, bool roleSet) {
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return SDL_GetTicks(); });
    lv_display_t* disp = lv_sdl_window_create(simSize + 20, simSize + 20);
    lv_sdl_window_set_zoom(disp, simSize > 240 ? 1 : 2);
    lv_sdl_mouse_create();
    SDL_AddEventWatch(keyWatch, nullptr);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x303030), 0);
    jt::Fonts fonts = loadFonts();
    menu::RoleId r = role == "blockers1"   ? menu::RoleId::Blockers1
                     : role == "blockers2" ? menu::RoleId::Blockers2
                     : role == "jammers"   ? menu::RoleId::Jammers
                                           : menu::RoleId::JamTimer;
    SimPlatform plat;
    plat.roleOverride = roleSet ? role : "";
    plat.hostOverride = host;
    plat.portOverride = port;
    (void)r;
    dev::Device dev(plat, lv_screen_active(), fonts);
    if (setupAtStart) {
        std::string e = dev.startSetup();
        if (!e.empty()) std::printf("setup: %s\n", e.c_str());
    }
    std::printf("role %s. Keys: Space/C crown (12), U or Left arrow = 10 o'clock, T or Right arrow = 2 o'clock; "
                "hold as on the device (undo: release after 1 s; menu: hold 3 s)%s\n",
                role.c_str(), host.empty() ? " - standalone" : (" - scoreboard " + host + ":" + std::to_string(port)).c_str());
    for (;;) {
        while (!keys.empty()) {
            dev.button(keys.front().b, keys.front().down);
            keys.pop_front();
        }
        dev.loop();
        lv_timer_handler();
        SDL_Delay(5);
    }
}

// ---------------- scripted (--ctl) ----------------
// No window: a display as on the board (240 x 240, or 466 x 466 with --board
// stopwatch), real time, commands on stdin (one per line, each answered with
// "OK" after it is done):
//   down crown|left|right    up crown|left|right    button down / up
//   touch X Y                tap at X,Y in design pixels (240 x 240, as in docs/display.md;
//                            scaled to the display): 80 ms down, 60 ms up
//   text                     visible labels, one "TEXT ..." line, " | " between them
//   shot FILE                screen as a PPM
//   flushed                  pixels sent to the panel since the last "flushed"
//   fb FILE                  what the panel shows (partial redraws as on the board) as a PPM
// Used by tools/game-test to run several devices against one scoreboard.

static void writePpm(lv_obj_t* obj, const std::string& path);
static std::vector<uint16_t> ctlPanel; // --ctl: the panel's memory, RGB565
static uint64_t ctlFlushed = 0;        // --ctl: pixels sent to the panel ("flushed" reads and resets)

static uint32_t monoMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void labels(lv_obj_t* o, std::string& out) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(o, &lv_label_class)) {
        std::string t = lv_label_get_text(o);
        for (char& c : t) {
            if (c == '\n') c = ' ';
        }
        if (!t.empty()) out += (out.empty() ? "" : " | ") + t;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) labels(lv_obj_get_child(o, i), out);
}

static struct {
    bool pressed = false;
    int x = 0, y = 0;
} ctlTouch;

static int runCtl(const std::string& host, int port, const std::string& role, bool roleSet) {
    static uint32_t t0 = monoMs();
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return monoMs() - t0; });
    // Drawn as on the board: 40-line partial buffers, flushed into a panel
    // memory that keeps what is not redrawn ("fb FILE" saves it), and on the
    // StopWatch areas rounded to even rows and columns (board_m5_stopwatch.cpp).
    static std::vector<uint8_t> buf1((size_t)simSize * 40 * 2), buf2((size_t)simSize * 40 * 2);
    ctlPanel.assign((size_t)simSize * simSize, 0);
    lv_display_t* d = lv_display_create(simSize, simSize);
    lv_display_set_buffers(d, buf1.data(), buf2.data(), buf1.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, [](lv_display_t* disp, const lv_area_t* a, uint8_t* px) {
        const uint16_t* p = (const uint16_t*)px;
        ctlFlushed += (uint64_t)(a->x2 - a->x1 + 1) * (a->y2 - a->y1 + 1);
        if (getenv("CLOCK_SIM_FLUSHLOG")) std::fprintf(stderr, "flush %d,%d %dx%d\n", (int)a->x1, (int)a->y1, (int)(a->x2 - a->x1 + 1), (int)(a->y2 - a->y1 + 1));
        for (int y = a->y1; y <= a->y2; y++)
            for (int x = a->x1; x <= a->x2; x++) ctlPanel[(size_t)y * simSize + x] = *p++;
        lv_display_flush_ready(disp);
    });
    if (simSize == 466)
        lv_display_add_event_cb(
            d,
            [](lv_event_t* e) {
                lv_area_t* a = (lv_area_t*)lv_event_get_param(e);
                a->x1 &= ~1;
                a->y1 &= ~1;
                a->x2 |= 1;
                a->y2 |= 1;
            },
            LV_EVENT_INVALIDATE_AREA, nullptr);
    lv_indev_t* in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, [](lv_indev_t*, lv_indev_data_t* data) {
        data->point.x = ctlTouch.x;
        data->point.y = ctlTouch.y;
        data->state = ctlTouch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    });
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), 0);
    jt::Fonts fonts = loadFonts();
    struct CtlPlatform : SimPlatform {
        uint32_t now() override { return monoMs() - t0; }
    } plat;
    plat.roleOverride = roleSet ? role : "";
    plat.hostOverride = host;
    plat.portOverride = port;
    dev::Device dev(plat, lv_screen_active(), fonts);
    std::printf("READY %s\n", role.c_str());
    std::string pending;
    uint32_t touchUntil = 0;
    for (;;) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(0, &fds);
        timeval tv{0, 5000};
        if (select(1, &fds, nullptr, nullptr, &tv) > 0) {
            char b[512];
            ssize_t n = read(0, b, sizeof b);
            if (n <= 0) return 0;
            pending.append(b, n);
        }
        size_t nl;
        while (touchUntil == 0 && (nl = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            char a[32] = "", c[256] = "";
            int x = 0, y = 0;
            sscanf(line.c_str(), "%31s", a);
            std::string cmd = a;
            if (cmd == "down" || cmd == "up") {
                sscanf(line.c_str(), "%*s %255s", c);
                std::string w = c;
                dev::Phys p = w == "left" ? dev::Phys::Left : w == "right" ? dev::Phys::Right : dev::Phys::Crown;
                dev.button(p, cmd == "down");
                dev.loop();
                lv_timer_handler();
                std::printf("OK\n");
            } else if (cmd == "touch" && sscanf(line.c_str(), "%*s %d %d", &x, &y) == 2) {
                ctlTouch = {true, ui::px(x), ui::px(y)};
                touchUntil = monoMs() + 80; // OK once released
            } else if (cmd == "text") {
                dev.loop();
                lv_timer_handler();
                std::string out;
                labels(lv_screen_active(), out);
                std::string top;
                labels(lv_layer_top(), top);
                if (!top.empty()) out += " || " + top;
                std::printf("TEXT %s\nOK\n", out.c_str());
            } else if (cmd == "flushed") {
                std::printf("FLUSHED %llu\nOK\n", (unsigned long long)ctlFlushed);
                ctlFlushed = 0;
            } else if (cmd == "fb" && sscanf(line.c_str(), "%*s %255s", c) == 1) {
                lv_timer_handler();
                FILE* f = std::fopen(c, "wb");
                if (f) {
                    std::fprintf(f, "P6\n%d %d\n255\n", simSize, simSize);
                    for (uint16_t v : ctlPanel) {
                        unsigned char rgb[3] = {(unsigned char)((v >> 11) << 3), (unsigned char)(((v >> 5) & 63) << 2),
                                                (unsigned char)((v & 31) << 3)};
                        std::fwrite(rgb, 1, 3, f);
                    }
                    std::fclose(f);
                }
                std::printf("OK\n");
            } else if (cmd == "shot" && sscanf(line.c_str(), "%*s %255s", c) == 1) {
                lv_timer_handler();
                writePpm(lv_screen_active(), c);
                std::printf("OK\n");
            } else {
                std::printf("ERR %s\nOK\n", line.c_str());
            }
        }
        // Held for 80 ms, then released for 60 ms before the OK: LVGL reads
        // the pointer every 30 ms and must see the release, or the next tap
        // continues this press.
        if (touchUntil && ctlTouch.pressed && (int32_t)(monoMs() - touchUntil) >= 0) {
            ctlTouch.pressed = false;
            touchUntil = monoMs() + 60;
        } else if (touchUntil && !ctlTouch.pressed && (int32_t)(monoMs() - touchUntil) >= 0) {
            touchUntil = 0;
            std::printf("OK\n");
        }
        dev.loop();
        lv_timer_handler();
    }
}

// ---------------- screenshots ----------------

static uint32_t shotTick = 0;

static void memLine(const char* name) {
#ifdef SIM_MEMCHECK
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    std::printf("MEM %-36s used %6u  max %6u\n", name, (unsigned)(m.total_size - m.free_size), (unsigned)m.max_used);
#else
    (void)name;
#endif
}

static void writePpm(lv_obj_t* obj, const std::string& path) {
    lv_draw_buf_t* b = lv_snapshot_take(obj, LV_COLOR_FORMAT_RGB888);
    if (!b) {
        std::fprintf(stderr, "snapshot failed\n");
        return;
    }
    int w = b->header.w, h = b->header.h, stride = b->header.stride;
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        const uint8_t* row = b->data + y * stride;
        for (int x = 0; x < w; x++) {
            uint8_t px[3] = {row[x * 3 + 2], row[x * 3 + 1], row[x * 3]}; // stored as B, G, R
            std::fwrite(px, 1, 3, f);
        }
    }
    std::fclose(f);
    lv_draw_buf_destroy(b);
}

static const std::string G = "ScoreBoard.CurrentGame.";
static std::string kv(const std::string& k, const std::string& v) { return "\"" + G + k + "\":" + v; }
static std::string st(std::initializer_list<std::string> parts) {
    std::string s = "{\"state\":{";
    bool first = true;
    for (auto& p : parts) {
        s += (first ? "" : ",") + p;
        first = false;
    }
    return s + "}}";
}

static int runShots(const std::string& dir) {
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return shotTick; });
    static std::vector<uint8_t> buf((size_t)simSize * simSize * 2);
    lv_display_t* d = lv_display_create(simSize, simSize);
    lv_display_set_buffers(d, buf.data(), nullptr, buf.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, [](lv_display_t* disp, const lv_area_t*, uint8_t*) { lv_display_flush_ready(disp); });
    jt::Fonts fonts = loadFonts();
    jt::Ui ui(lv_screen_active(), fonts);

    int n = 0;
    auto shot = [&](jt::Controller& c, uint32_t now, const char* name) {
        shotTick = now;
        ui.show(c.view(now), now);
        lv_timer_handler();
        char file[256];
        std::snprintf(file, sizeof file, "%s/%02d-%s.ppm", dir.c_str(), ++n, name);
        writePpm(ui.root(), file);
        std::printf("%s\n", file);
        memLine(name);
    };
    auto press = [](jt::Controller& c, jt::Btn b, uint32_t t) {
        c.button(b, true, t);
        c.tick(t);
        c.button(b, false, t + 50);
    };

    // Standalone
    {
        jamcore::JamTimer jt;
        jt::Controller c(jt, nullptr, jt::Mode::Standalone);
        shot(c, 0, "standalone-ready");
        press(c, jt::Btn::Crown, 1000);
        c.tick(31000);
        shot(c, 31000, "standalone-jam");
        press(c, jt::Btn::Crown, 61000);
        c.tick(73000);
        shot(c, 73000, "standalone-lineup");
        c.tick(87000);
        shot(c, 87000, "standalone-lineup-flash-on");
        shot(c, 87250, "standalone-lineup-flash-off");
        press(c, jt::Btn::Timeout, 88000);
        c.tick(110000);
        shot(c, 110000, "standalone-timeout");
        c.button(jt::Btn::Undo, true, 111000);
        c.tick(111600);
        shot(c, 111600, "standalone-undo-hold");
    }
    // Connected
    {
        jamcore::JamTimer jt;
        crg::Client cl;
        jt::Controller c(jt, &cl, jt::Mode::Connected);
        cl.onConnecting(0);
        cl.onOpen("CRG_SCOREBOARD=x", 0);
        cl.onFrame(R"({"state":{"WS.Device.Id":"d1"}})", 0);
        cl.onFrame(st({kv("InJam", "false"), kv("InPeriod", "true"), kv("Label(Start)", "\"Start Jam\""),
                       kv("Label(Stop)", "\"---\""), kv("Clock(Jam).Number", "6"), kv("Clock(Jam).MaximumTime", "120000"),
                       kv("Clock(Jam).Direction", "true"), kv("Clock(Period).Number", "1"),
                       kv("Clock(Period).Time", "1452000"), kv("Clock(Period).Direction", "true"),
                       kv("Clock(Lineup).Running", "true"), kv("Clock(Lineup).Time", "18000"),
                       kv("Team(1).Id", "\"g_1\""), kv("Team(2).Id", "\"g_2\"")}),
                   0);
        shot(c, 100, "crg-lineup");
        press(c, jt::Btn::Crown, 1000);
        shot(c, 1100, "crg-sent-waiting");
        cl.onFrame(st({kv("InJam", "true"), kv("Label(Start)", "\"---\""), kv("Label(Stop)", "\"Stop Jam\""),
                       kv("Clock(Jam).Number", "7"), kv("Clock(Jam).Running", "true"), kv("Clock(Jam).Time", "120000"),
                       kv("Clock(Period).Running", "true"), kv("Clock(Lineup).Running", "false")}),
                   1130);
        c.tick(1130);
        shot(c, 1140, "crg-jam-crown-blocked");
        cl.onFrame(st({kv("Clock(Jam).Time", "102000"), kv("Clock(Period).Time", "1434000")}), 19000);
        shot(c, 19000, "crg-jam");
        press(c, jt::Btn::Crown, 20000);
        cl.tick(21500);
        c.tick(21500);
        shot(c, 21600, "crg-not-taken-red");
        shot(c, 21750, "crg-not-taken");
        press(c, jt::Btn::Timeout, 25000);
        cl.onFrame(st({kv("InJam", "false"), kv("Label(Stop)", "\"End Timeout\""), kv("Label(Timeout)", "\"New Timeout\""),
                       kv("Clock(Timeout).Running", "true"), kv("Clock(Timeout).Time", "4000"),
                       kv("Clock(Timeout).Number", "1"), kv("Clock(Period).Running", "false")}),
                   25020);
        c.tick(25020);
        shot(c, 25100, "crg-picker");
        c.touch(60, 60, 27000);
        cl.onFrame(st({kv("TimeoutOwner", "\"g_1\""), kv("Clock(Timeout).Time", "21000")}), 27020);
        c.tick(27020);
        shot(c, 29000, "crg-team-timeout");
        cl.onFrame(st({kv("InJam", "false"), kv("InPeriod", "false"), kv("Label(Start)", "\"Start Jam\""),
                       kv("Label(Stop)", "\"Lineup\""), kv("TimeoutOwner", "\"\""), kv("Clock(Timeout).Running", "false"),
                       kv("Clock(Intermission).Running", "true"), kv("Clock(Intermission).Direction", "true"),
                       kv("Clock(Intermission).MaximumTime", "900000"), kv("Clock(Intermission).Time", "372000"),
                       kv("Rule(Period.Number)", "2")}),
                   40000);
        c.tick(40000);
        c.button(jt::Btn::Crown, true, 40000);
        c.tick(40400);
        shot(c, 40400, "crg-intermission-hold");
        c.button(jt::Btn::Crown, false, 40500);
        cl.onClose(50000);
        c.tick(50000);
        shot(c, 50000, "crg-no-connection");
    }
    // ---- box timer roles ----
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_screen_load(scr);
    box::Ui bui(scr, fonts);
    auto bshot = [&](box::Controller& c, uint32_t now, const char* name) {
        shotTick = now;
        bui.show(c.view(now), now);
        lv_timer_handler();
        char file[256];
        std::snprintf(file, sizeof file, "%s/%02d-%s.ppm", dir.c_str(), ++n, name);
        writePpm(bui.root(), file);
        std::printf("%s\n", file);
        memLine(name);
    };
    auto bpress = [](box::Controller& c, box::Btn b, uint32_t t) {
        c.button(b, true, t);
        c.tick(t);
        c.button(b, false, t + 50);
    };
    {
        boxcore::BoxTimer bt;
        box::Controller c(box::Role::Blockers, 1, bt, nullptr, box::Mode::Standalone);
        bshot(c, 0, "box-empty");
        bpress(c, box::Btn::Crown, 1000);
        bshot(c, 11000, "box-one-serving");
        bpress(c, box::Btn::Crown, 12000);
        bshot(c, 23000, "box-two-stand");
        bpress(c, box::Btn::Crown, 24000);
        bshot(c, 32000, "box-three-done");
        bpress(c, box::Btn::Right, 33000);
        bshot(c, 40000, "box-three-paused");
        bpress(c, box::Btn::Right, 41000);
        c.touch(120, 120, 42000);
        bshot(c, 42000, "box-menu");
    }
    {
        boxcore::BoxTimer bt;
        box::Controller c(box::Role::Jammers, 1, bt, nullptr, box::Mode::Standalone);
        bshot(c, 0, "jammers-empty");
        bpress(c, box::Btn::Left, 1000);
        bshot(c, 9000, "jammers-left-serving");
        bpress(c, box::Btn::Right, 10000);
        bshot(c, 11000, "jammers-swap");
    }
    {
        boxcore::BoxTimer bt;
        crg::ClientConfig cfg;
        cfg.role = crg::Role::BoxBlockers;
        crg::Client cl(cfg);
        box::Controller c(box::Role::Blockers, 1, bt, &cl, box::Mode::Connected);
        cl.onConnecting(0);
        cl.onOpen("", 0);
        cl.onFrame(R"({"state":{"WS.Device.Id":"d1"}})", 0);
        cl.onFrame(R"x({"state":{"ScoreBoard.Settings.Setting(ScoreBoard.Penalties.UsePBT)":true}})x", 0);
        cl.onFrame(st({kv("InJam", "true"), kv("Team(1).Position(Pivot).RosterNumber", "\"21\""),
                       kv("Team(1).Position(Blocker1).RosterNumber", "\"88\""),
                       kv("Team(1).Position(Blocker1).HasUnserved", "true"),
                       kv("Team(1).Position(Blocker2).RosterNumber", "\"4\""),
                       kv("Team(1).Position(Blocker3).RosterNumber", "\"\""),
                       kv("Team(1).Position(Blocker3).HasUnserved", "true")}),
                   0);
        bpress(c, box::Btn::Crown, 1000);
        bshot(c, 1100, "crg-box-picker");
        c.touch(120, 196, 1200); // decide later
        auto tripf = [&](const char* id, const char* num, long long ms, bool run) {
            std::string pfx = std::string("Team(1).BoxTrip(") + id + ").";
            cl.onFrame(st({kv(pfx + "IsCurrent", "true"), kv(pfx + "RosterNumber", std::string("\"") + num + "\""),
                           kv(pfx + "Clock.Time", std::to_string(ms)), kv(pfx + "Clock.Running", run ? "true" : "false"),
                           kv(pfx + "Clock.Direction", "true")}),
                       2000);
        };
        tripf("a", "88", 17000, false);
        cl.onFrame(st({kv("InJam", "false")}), 2000);
        bshot(c, 2000, "crg-box-waiting");
        cl.onClose(3000);
        bshot(c, 3000, "crg-box-no-connection");
    }
    // ---- 2 o'clock release with nobody done ----
    {
        boxcore::BoxTimer bt;
        crg::ClientConfig cfg;
        cfg.role = crg::Role::BoxBlockers;
        crg::Client cl(cfg);
        box::Controller c(box::Role::Blockers, 1, bt, &cl, box::Mode::Connected);
        cl.onConnecting(0);
        cl.onOpen("", 0);
        cl.onFrame(R"({"state":{"WS.Device.Id":"d1"}})", 0);
        cl.onFrame(R"x({"state":{"ScoreBoard.Settings.Setting(ScoreBoard.Penalties.UsePBT)":true}})x", 0);
        cl.onFrame(st({kv("InJam", "true")}), 0);
        for (auto t : {std::make_pair("a", "21"), std::make_pair("b", "88")}) {
            std::string pfx = std::string("Team(1).BoxTrip(") + t.first + ").";
            cl.onFrame(st({kv(pfx + "IsCurrent", "true"), kv(pfx + "RosterNumber", std::string("\"") + t.second + "\""),
                           kv(pfx + "Clock.Time", t.first[0] == 'a' ? "14000" : "6000"), kv(pfx + "Clock.Running", "true"),
                           kv(pfx + "Clock.Direction", "true")}),
                       0);
        }
        bpress(c, box::Btn::Right, 1000);
        bshot(c, 1100, "crg-box-release-nobody-done");
    }
    // ---- swap prompt and the second hold stage ----
    {
        boxcore::BoxTimer bt;
        crg::ClientConfig cfg;
        cfg.role = crg::Role::BoxBlockers;
        crg::Client cl(cfg);
        box::Controller c(box::Role::Blockers, 1, bt, &cl, box::Mode::Connected);
        cl.onConnecting(0);
        cl.onOpen("", 0);
        cl.onFrame(R"({"state":{"WS.Device.Id":"d1"}})", 0);
        cl.onFrame(st({kv("InJam", "false"), kv("InPeriod", "false"), kv("Clock(Jam).Number", "14"),
                       kv("Clock(Period).Number", "1"), kv("Rule(Period.Number)", "2")}),
                   0);
        c.tick(100);
        bshot(c, 100, "crg-box-swap-prompt");
    }
    {
        jamcore::JamTimer jt2;
        jt::Controller c(jt2, nullptr, jt::Mode::Standalone);
        lv_screen_load(lv_obj_get_parent(ui.root()));
        c.button(jt::Btn::Undo, true, 1000);
        c.tick(2700);
        shot(c, 2700, "standalone-hold-towards-menu");
        lv_screen_load(scr);
    }
    // ---- device menu ----
    struct ShotHost : menu::Host {
        menu::RoleId role() const override { return menu::RoleId::Blockers1; }
        menu::Refusal setRole(menu::RoleId) override { return ""; }
        bool connectedMode() const override { return true; }
        menu::Refusal setConnectedMode(bool) override { return ""; }
        bool sidesSwapped() const override { return false; }
        menu::Refusal swapSides() override { return "Not while someone is in the box"; }
        menu::Refusal resetGame() override { return ""; }
        menu::Refusal clearClocks() override { return ""; }
        int brightness() const override { return 75; }
        void setBrightness(int) override {}
        bool motorFitted() const override { return false; }
        bool vibration() const override { return false; }
        void setVibration(bool) override {}
        std::vector<std::string> wifiNetworks() const override { return {"Sporthal Zuid", "Home"}; }
        int currentWifi() const override { return 0; }
        menu::Refusal selectWifi(int) override { return ""; }
        menu::Refusal startSetup() override { return ""; }
        std::vector<std::string> info() const override {
            return {"Battery 82%", "Wi-Fi Sporthal Zuid, -61 dBm", "Scoreboard 192.168.1.10:8000", "Write access: yes",
                    "Press to confirm: 14 ms", "Firmware 0.1"};
        }
        menu::Refusal startUpdate() override { return ""; }
        void sleep() override {}
    } host;
    lv_obj_t* mscr = lv_obj_create(nullptr);
    lv_screen_load(mscr);
    menu::Ui mui(mscr, fonts);
    menu::Menu m(host);
    auto mshot = [&](const char* name) {
        mui.show(m.view());
        lv_timer_handler();
        char file[256];
        std::snprintf(file, sizeof file, "%s/%02d-%s.ppm", dir.c_str(), ++n, name);
        writePpm(mui.root(), file);
        std::printf("%s\n", file);
        memLine(name);
    };
    m.open(0);
    mshot("menu-top");
    m.touch(60, 60, 10);
    mshot("menu-game");
    m.touch(120, 58 + 34 + 15, 20);
    mshot("menu-refused");
    m.touch(120, 120, 30);
    m.touch(120, 73, 40);
    mshot("menu-role");
    m.touch(120, 210, 50);
    m.touch(120, 210, 60);
    m.touch(60, 180, 70);
    mshot("menu-connection");
    m.touch(120, 58 + 68 + 15, 80);
    mshot("menu-info");
    m.touch(120, 210, 90);
    m.touch(120, 210, 100);
    m.touch(180, 180, 110);
    m.touch(120, 73, 120);
    mshot("menu-confirm-sleep");
    // ---- setup screen (through the device, with a stand-in platform) ----
    {
        struct ShotPlatform : dev::Platform {
            uint32_t now() override { return shotTick; }
            std::string loadCookie(const char*) override { return ""; }
            void saveCookie(const char*, const std::string&) override {}
            void setBrightness(int) override {}
            void sleep() override {}
            void log(const std::string&) override {}
            std::vector<std::string> info() override { return {}; }
            std::string loadSettings() override { return ""; }
            void saveSettings(const std::string&) override {}
            void scoreboard(const cfg::Settings&, std::string& h, int&) override { h.clear(); }
            void networksChanged(const cfg::Settings&) override {}
            int currentNetwork(const cfg::Settings&) override { return -1; }
            void joinNetwork(const cfg::Settings&, int) override {}
            bool battery(int& p, bool& c) override {
                p = 12;
                c = false;
                return true;
            }
            SetupInfo startSetup() override {
                SetupInfo i;
                i.httpPort = 0; // any free port
                i.qr = cfg::wifiQr("CLOCK-1A2B", "73914862");
                i.lines = {"Wi-Fi: CLOCK-1A2B", "Password: 73914862", "Then open http://192.168.4.1"};
                return i;
            }
            void stopSetup() override {}
            struct NoSlot : upd::Slot {
                std::string begin(size_t) override { return "no slot"; }
                bool write(const uint8_t*, size_t) override { return false; }
                std::string finish() override { return "no slot"; }
                void abort() override {}
            } slot;
            std::string firmwareVersion() override { return "0.1-dev"; }
            const upd::Board& board() override { return *simBoard; }
            int displaySize() override { return simSize; }
            upd::Slot& updateSlot() override { return slot; }
            std::unique_ptr<upd::Fetch> fetch(const std::string&) override { return nullptr; }
            bool usbPower() override { return false; }
            void restart() override {}
        } sp;
        lv_obj_t* dscr = lv_obj_create(nullptr);
        lv_screen_load(dscr);
        lv_obj_set_style_bg_color(dscr, lv_color_black(), 0);
        dev::Device d(sp, dscr, fonts);
        shotTick = 5000;
        d.loop(); // draws the role screen and the battery pill (top layer)
        lv_timer_handler();
        char file[256];
        // the role screen with the low-battery pill: screen and top layer drawn together
        lv_obj_t* top = lv_layer_top();
        lv_obj_set_parent(lv_obj_get_child(top, 0), dscr);
        lv_timer_handler();
        std::snprintf(file, sizeof file, "%s/%02d-%s.ppm", dir.c_str(), ++n, "battery-low");
        writePpm(dscr, file);
        std::printf("%s\n", file);
        d.startSetup();
        lv_timer_handler();
        std::snprintf(file, sizeof file, "%s/%02d-%s.ppm", dir.c_str(), ++n, "setup-qr");
        writePpm(dscr, file);
        std::printf("%s\n", file);
    }
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0); // log lines show up at once, also in a file
    std::string host, shots, role = "jt";
    bool roleSet = false, ctl = false;
    int port = 8000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--crg") && i + 1 < argc) {
            std::string hp = argv[++i];
            size_t c = hp.rfind(':');
            host = hp.substr(0, c);
            if (c != std::string::npos) port = atoi(hp.c_str() + c + 1);
        } else if (!strcmp(argv[i], "--role") && i + 1 < argc) {
            role = argv[++i];
            roleSet = true;
            if (role != "jt" && role != "blockers1" && role != "blockers2" && role != "jammers") {
                std::fprintf(stderr, "role: jt, blockers1, blockers2 or jammers\n");
                return 2;
            }
        } else if (!strcmp(argv[i], "--setup")) {
            setupAtStart = true;
        } else if (!strcmp(argv[i], "--ctl")) {
            ctl = true;
        } else if (!strcmp(argv[i], "--ttf")) {
            useTtf = true;
        } else if (!strcmp(argv[i], "--shots") && i + 1 < argc) {
            shots = argv[++i];
        } else if (!strcmp(argv[i], "--board") && i + 1 < argc) {
            std::string b = argv[++i];
            if (b == "waveshare" || b == "waveshare-c6") simBoard = &upd::kWaveshareC6, simSize = 240;
            else if (b == "stopwatch" || b == "m5-stopwatch") simBoard = &upd::kStopWatch, simSize = 466;
            else {
                std::fprintf(stderr, "board: waveshare or stopwatch\n");
                return 2;
            }
        } else {
            std::fprintf(stderr, "usage: sim [--board waveshare|stopwatch] [--role jt|blockers1|blockers2|jammers] "
                                 "[--crg HOST:PORT] [--setup] [--shots DIR] [--ctl] [--ttf]\n");
            return 2;
        }
    }
    ui::gScreen = simSize; // the shots draw screens without a Device
    if (!shots.empty()) return runShots(shots);
    if (ctl) return runCtl(host, port, role, roleSet);
    return runWindow(host, port, role, roleSet);
}
