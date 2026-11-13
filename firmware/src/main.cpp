// Firmware entry: the shared device (lib/device) on the ESP32-C6 board.
// Compiles for the chip; not run on hardware yet. Board drivers: board.cpp.
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <driver/usb_serial_jtag.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_sleep.h>

#include "device.h"
#include "httpget.h"
#include "jtfonts.h"
#include "lvgl.h"
#include "board.h"
#include "pins.h"

#ifndef FW_VERSION
#define FW_VERSION "0.1-dev"
#endif


// ---- firmware updates (lib/update) ----

// Rollback (HANDOVER section 8): the Arduino core would mark a new firmware
// valid at once; this keeps it "pending verify" until it has run for a while
// (markValid). A reset before that boots the previous slot again.
extern "C" bool verifyRollbackLater() { return true; }

static bool appConfirmed = false;
static void markValid(const char* why) {
    if (appConfirmed) return;
    appConfirmed = true;
    esp_ota_img_states_t st;
    const esp_partition_t* run = esp_ota_get_running_partition();
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        Serial.printf("new firmware confirmed (%s)\n", why);
    }
}

// The inactive app slot. Sequential writes: the flash is erased as it is
// written, so the screen keeps running (a full erase up front blocks).
class OtaSlot : public upd::Slot {
  public:
    std::string begin(size_t size) override {
        abort();
        part_ = esp_ota_get_next_update_partition(nullptr);
        if (!part_) return "No update slot (partition table)";
        if (size > part_->size) {
            part_ = nullptr;
            return "Update too large for the app slot";
        }
        esp_err_t e = esp_ota_begin(part_, OTA_WITH_SEQUENTIAL_WRITES, &h_);
        if (e != ESP_OK) {
            part_ = nullptr;
            return std::string("Update slot: ") + esp_err_to_name(e);
        }
        return "";
    }
    bool write(const uint8_t* d, size_t n) override { return part_ && esp_ota_write(h_, d, n) == ESP_OK; }
    std::string finish() override {
        if (!part_) return "No update in progress";
        const esp_partition_t* p = part_;
        part_ = nullptr;
        esp_err_t e = esp_ota_end(h_); // checks the image (segments, appended SHA-256)
        if (e == ESP_ERR_OTA_VALIDATE_FAILED) return "The firmware image did not verify";
        if (e != ESP_OK) return std::string("Finishing the update: ") + esp_err_to_name(e);
        e = esp_ota_set_boot_partition(p);
        if (e != ESP_OK) return std::string("Boot slot: ") + esp_err_to_name(e);
        return "";
    }
    void abort() override {
        if (part_) esp_ota_abort(h_);
        part_ = nullptr;
    }

  private:
    const esp_partition_t* part_ = nullptr;
    esp_ota_handle_t h_ = 0;
};

// HTTPS with the certificate bundle in ESP-IDF: the release file and the
// firmware come from a server whose certificate checks out. Opening blocks
// for the name lookup and the TLS handshake (the update screen says
// "Checking" meanwhile); reads wait up to 5 s.
class EspFetch : public upd::Fetch {
  public:
    explicit EspFetch(const std::string& url) {
        esp_http_client_config_t cfg = {};
        cfg.url = url.c_str();
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.timeout_ms = 5000;
        cfg.buffer_size = 4096;
        c_ = esp_http_client_init(&cfg);
        if (!c_) err_ = "cannot start https";
    }
    ~EspFetch() override {
        if (c_) {
            esp_http_client_close(c_);
            esp_http_client_cleanup(c_);
        }
    }
    int poll(std::string& chunk) override {
        chunk.clear();
        if (!c_ || failed_) return -1;
        if (done_) return 0;
        if (!opened_) {
            esp_err_t e = esp_http_client_open(c_, 0);
            if (e != ESP_OK) return fail(std::string("cannot connect (") + esp_err_to_name(e) + ")");
            len_ = esp_http_client_fetch_headers(c_);
            int st = esp_http_client_get_status_code(c_);
            if (st != 200) return fail("server answered " + std::to_string(st));
            opened_ = true;
            return 1;
        }
        char buf[4096];
        int n = esp_http_client_read(c_, buf, sizeof buf);
        if (n < 0) return fail("read failed");
        if (n > 0) chunk.assign(buf, (size_t)n);
        if (esp_http_client_is_complete_data_received(c_)) {
            done_ = true;
            return 0;
        }
        return 1;
    }
    long length() const override { return len_; }
    std::string error() const override { return err_; }

  private:
    int fail(const std::string& e) {
        err_ = e;
        failed_ = true;
        return -1;
    }
    esp_http_client_handle_t c_ = nullptr;
    long len_ = -1;
    std::string err_;
    bool opened_ = false, done_ = false, failed_ = false;
};

class BoardPlatform : public dev::Platform {
  public:
    uint32_t now() override { return millis(); }
    std::string loadCookie(const char* role) override {
        prefs_.begin("crg", true);
        String v = prefs_.getString((std::string("ck_") + role).c_str(), "");
        prefs_.end();
        return v.c_str();
    }
    void saveCookie(const char* role, const std::string& c) override {
        prefs_.begin("crg", false);
        prefs_.putString((std::string("ck_") + role).c_str(), c.c_str());
        prefs_.end();
    }
    void setBrightness(int pct) override { board::setBrightness(pct); }
    void sleep() override {
        markValid("going to sleep"); // a reset while pending would roll back
        // Wake on the crown (IO1 is a low-power GPIO, so ext1 can wake on it).
        // TODO: require the 1 s hold after waking (check on wake, else sleep again).
        board::displayOff();
        esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_BTN_CROWN, ESP_EXT1_WAKEUP_ANY_LOW);
        esp_deep_sleep_start();
    }
    void log(const std::string& line) override { Serial.println(line.c_str()); }
    // ---- settings in NVS ----
    std::string loadSettings() override {
        prefs_.begin("crg", true);
        String v = prefs_.getString("settings", "");
        prefs_.end();
        return v.c_str();
    }
    void saveSettings(const std::string& text) override {
        prefs_.begin("crg", false);
        prefs_.putString("settings", text.c_str());
        prefs_.end();
    }

    // ---- Wi-Fi: saved networks, tried in order ----
    void scoreboard(const cfg::Settings& s, std::string& host, int& port) override {
        host.clear();
        if (s.networks.empty()) return;
        const cfg::Network* n = &s.networks[0];
        if (WiFi.status() == WL_CONNECTED) {
            for (auto& x : s.networks)
                if (x.ssid == WiFi.SSID().c_str()) n = &x;
        }
        host = n->host;
        port = n->port;
    }
    void networksChanged(const cfg::Settings& s) override {
        nets_ = s.networks;
        next_ = 0;
        WiFi.disconnect();
        tryNext();
    }
    int currentNetwork(const cfg::Settings& s) override {
        if (WiFi.status() != WL_CONNECTED) return -1;
        for (size_t i = 0; i < s.networks.size(); i++)
            if (s.networks[i].ssid == WiFi.SSID().c_str()) return (int)i;
        return -1;
    }
    void joinNetwork(const cfg::Settings& s, int i) override {
        nets_ = s.networks;
        next_ = (size_t)i;
        WiFi.disconnect();
        tryNext();
    }
    bool battery(int& pct, bool& charging) override {
        int mv = 0;
        charging = false; // the board has no charge-status pin (only the CHG LED)
        return board::battery(mv, pct);
    }
    // Call from loop(): move on to the next saved network after a while.
    void wifiLoop(uint32_t now) {
        if (nets_.empty() || WiFi.status() == WL_CONNECTED) {
            if (apOpen_ && apCloseWhenJoined_ && WiFi.status() == WL_CONNECTED) {
                WiFi.softAPdisconnect(true); // the access point closes only after a successful join
                apOpen_ = false;
            }
            return;
        }
        if (now - triedAt_ > 15000) tryNext();
    }
    void tryNext() {
        if (nets_.empty()) return;
        const cfg::Network& n = nets_[next_++ % nets_.size()];
        WiFi.begin(n.ssid.c_str(), n.password.c_str());
        triedAt_ = millis();
    }

    // ---- setup: access point next to the normal Wi-Fi ----
    SetupInfo startSetup() override {
        SetupInfo i;
        char ssid[24];
        snprintf(ssid, sizeof ssid, "CLOCK-%04X", (unsigned)(ESP.getEfuseMac() & 0xffff));
        char pw[9];
        for (int k = 0; k < 8; k++) pw[k] = '0' + (char)(esp_random() % 10);
        pw[8] = 0;
        WiFi.mode(WIFI_AP_STA);
        if (!WiFi.softAP(ssid, pw)) {
            i.error = "Access point did not start";
            return i;
        }
        apOpen_ = true;
        apCloseWhenJoined_ = false;
        i.httpPort = 80;
        i.qr = cfg::wifiQr(ssid, pw);
        i.lines = {std::string("Wi-Fi: ") + ssid, std::string("Password: ") + pw,
                   "Then open http://" + std::string(WiFi.softAPIP().toString().c_str())};
        return i;
    }
    void stopSetup() override { apCloseWhenJoined_ = true; }

    // ---- updates ----
    std::string firmwareVersion() override { return FW_VERSION; }
    upd::Slot& updateSlot() override { return slot_; }
    std::unique_ptr<upd::Fetch> fetch(const std::string& url) override {
        if (WiFi.status() != WL_CONNECTED) return nullptr;
        if (url.compare(0, 7, "http://") == 0)
            return std::unique_ptr<upd::Fetch>(new upd::HttpGet(url, [] { return (uint32_t)millis(); }));
        return std::unique_ptr<upd::Fetch>(new EspFetch(url));
    }
    // A computer on the USB port (the board has no pin for a plain charger).
    bool usbPower() override { return usb_serial_jtag_is_connected(); }
    void restart() override {
        delay(100);
        esp_restart();
    }

    std::vector<std::string> info() override {
        std::vector<std::string> l;
        l.push_back("Battery: not read yet");
        if (WiFi.status() == WL_CONNECTED)
            l.push_back(std::string("Wi-Fi: ") + WiFi.SSID().c_str() + ", " + std::to_string(WiFi.RSSI()) + " dBm");
        else l.push_back("Wi-Fi: not connected");
        const esp_partition_t* run = esp_ota_get_running_partition();
        l.push_back(std::string("Firmware: " FW_VERSION) + (run ? std::string(" (") + run->label + ")" : ""));
        if (esp_ota_get_last_invalid_partition()) l.push_back("Last update failed: rolled back");
        return l;
    }

  private:
    Preferences prefs_;
    std::vector<cfg::Network> nets_;
    size_t next_ = 0;
    uint32_t triedAt_ = 0;
    bool apOpen_ = false, apCloseWhenJoined_ = false;
    OtaSlot slot_;
};


struct Btn {
    uint8_t pin;
    dev::Phys id;
    bool down;
    uint32_t lastEdge;
};
// Crown at 12, timeout/pause at 2 (right), undo at 10 (left), seen from the front.
static Btn btns[] = {
    {PIN_BTN_CROWN, dev::Phys::Crown, false, 0},
    {PIN_BTN_TIMEOUT, dev::Phys::Right, false, 0},
    {PIN_BTN_UNDO, dev::Phys::Left, false, 0},
};
static const uint32_t kLockoutMs = 20;

static BoardPlatform platform;
static dev::Device* device = nullptr;

void setup() {
    Serial.begin(115200);
    for (auto& b : btns) pinMode(b.pin, INPUT_PULLUP);

    board::begin();
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    board::createDisplay();
    board::createTouch();
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // radio power saving off while in use (HANDOVER section 8)
    device = new dev::Device(platform, lv_screen_active(), jt::bitmapFonts());
    platform.networksChanged(device->settings());
    // No Wi-Fi saved yet: open setup at once (HANDOVER section 8).
    if (device->settings().networks.empty()) device->startSetup();
}

void loop() {
    uint32_t now = millis();
    // Act on the press edge; ignore further edges on that pin for kLockoutMs.
    for (auto& b : btns) {
        bool down = digitalRead(b.pin) == LOW;
        if (down != b.down && now - b.lastEdge >= kLockoutMs) {
            b.down = down;
            b.lastEdge = now;
            device->button(b.id, down);
        }
    }
    platform.wifiLoop(now);
    // A new firmware that has run 30 s with its main loop going stays.
    if (!appConfirmed && now > 30000) markValid("ran 30 s");
    // TODO: WsConn::open still blocks up to 1.5 s per attempt.
    device->loop();
    lv_timer_handler();
    delay(2);
}
