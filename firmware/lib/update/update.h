// Firmware updates (HANDOVER.md section 8), without Arduino: the device's
// "Update" (check a release address, download, install) and the setup page
// upload both end in a Writer that checks size, image header and SHA-256
// before the new slot is made the boot slot. What talks to the network and
// the flash is behind Fetch and Slot (board: esp_http_client and esp_ota;
// simulator: plain HTTP and a file).
#pragma once
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "sha256.h"

namespace upd {

// latest.json on the install site (web-installer/make_release.py).
struct Release {
    std::string version, app, sha256;
    long size = 0;
};
bool parseRelease(const std::string& json, Release& r, std::string& err);

// True when offered ("0.3.1", "v0.3.1") is a newer x.y.z than current. A
// current version that is not x.y.z (a dev build, "0.1-dev") counts as older
// than any release.
bool newer(const std::string& offered, const std::string& current);

// The app's address, relative to latest.json's.
std::string resolve(const std::string& base, const std::string& ref);

// The boards CLOCK runs on. Each has its own firmware: the chip differs, and
// so do the drivers. A firmware carries "CLOCK-BOARD=<id>;" somewhere in its
// image (kBoardMarkerPrefix, written by the board code), so an update or an
// upload for another board is refused even when the chip is the same.
struct Board {
    const char* id;   // release folder on the install site, e.g. "waveshare-c6"
    const char* name; // for messages
    uint16_t chip;    // ESP image header chip id: ESP32-C6 13, ESP32-S3 9
    bool legacy;      // also takes images without a marker (0.1.0 was built for this board only)
    // Button colours for the screens (lib/buttons): crown, 10 o'clock and
    // 2 o'clock button. nullptr = no colours, the screens say "Crown" etc.
    const char* crownName = nullptr;
    const char* leftName = nullptr;
    const char* rightName = nullptr;
};
extern const Board kWaveshareC6, kStopWatch;
const Board* findBoard(const std::string& id); // nullptr = unknown
#define CLOCK_BOARD_MARKER_PREFIX "CLOCK-BOARD="

// Where the device looks for updates. The setting is the install site
// ("https://clock.quagga.rocks/"): the device adds "<board id>/latest.json".
// A setting that names a .json file is used as it stands (a test server).
std::string releaseUrl(const std::string& setting, const Board& b);

// The start of an app image: magic 0xE9 and the board's chip id. "" = fine.
static const size_t kImageHeader = 16;
std::string checkImage(const uint8_t* p, size_t n, const Board& b);

// Finds the board markers in an image fed to it in pieces.
class MarkerScan {
  public:
    void reset();
    void feed(const uint8_t* p, size_t n);
    // "" = the image is for b, else why not
    std::string verdict(const Board& b) const;

  private:
    size_t matched_ = 0; // characters of the prefix matched so far
    bool inId_ = false;
    std::string id_;
    std::vector<std::string> found_; // board ids seen, in order
};

// The inactive app slot.
class Slot {
  public:
    virtual ~Slot() {}
    virtual std::string begin(size_t size) = 0; // "" = ready
    virtual bool write(const uint8_t* data, size_t len) = 0;
    virtual std::string finish() = 0;           // verify, boot from it next time; "" = done
    virtual void abort() = 0;                   // the running firmware stays the boot slot
};

// Size, header and SHA-256 around a Slot. Nothing becomes bootable unless
// all three are right.
class Writer {
  public:
    Writer(Slot& s, const Board& b) : slot_(s), board_(b) {}
    std::string begin(size_t size, const std::string& sha256hex); // sha "" = none (upload)
    std::string write(const uint8_t* data, size_t len);
    std::string finish();
    void abort();
    size_t written() const { return written_; }
    size_t size() const { return size_; }
    bool active() const { return active_; }

  private:
    Slot& slot_;
    const Board& board_;
    MarkerScan marks_;
    Sha256 sha_;
    std::string want_;
    uint8_t head_[kImageHeader];
    size_t size_ = 0, written_ = 0;
    bool active_ = false;
};

// When an update may run: no clock running and nobody in the box, and
// enough battery unless on USB power. Battery unknown counts as fine.
struct Conditions {
    bool idle = true;
    bool batteryKnown = false;
    int batteryPct = 0;
    bool usbPower = false;
};
static const int kMinBatteryPct = 30;
std::string refusal(const Conditions& c); // "" = allowed

// A download in progress.
class Fetch {
  public:
    virtual ~Fetch() {}
    // 1 while running (chunk may be empty: nothing new yet), 0 complete,
    // -1 failed (error()).
    virtual int poll(std::string& chunk) = 0;
    virtual long length() const = 0; // Content-Length, -1 unknown
    virtual std::string error() const = 0;
};

class Host {
  public:
    virtual ~Host() {}
    virtual std::unique_ptr<Fetch> fetch(const std::string& url) = 0;
    virtual Slot& slot() = 0;
    virtual Conditions conditions() = 0;
    virtual std::string version() = 0; // running firmware
    virtual const Board& board() = 0;  // the board this firmware runs on
};

enum class State : uint8_t { Idle, Checking, UpToDate, Available, Downloading, Receiving, Installed, Failed };

// Check -> offer -> download -> installed (the device restarts), or an
// upload from the setup page. One at a time.
class Flow {
  public:
    explicit Flow(Host& h) : h_(h), w_(h.slot(), h.board()) {}
    void check(const std::string& url, uint32_t now);
    std::string install(uint32_t now); // "" = started, else why not (state stays Available)
    void cancel();                     // stops a check or download; the old firmware stays
    void poll(uint32_t now);           // moves a check or download on (a budget per call)
    void reset();                      // back to Idle after a result was shown

    // Setup page upload (raw body): begin with the Content-Length.
    std::string beginUpload(size_t size, uint32_t now);
    std::string uploadChunk(const uint8_t* data, size_t len, uint32_t now);
    std::string endUpload(uint32_t now); // "" = installed
    void abortUpload(const std::string& why);

    State state() const { return state_; }
    const Release& release() const { return rel_; }
    int permille() const; // download / upload progress
    const std::string& message() const { return msg_; }
    bool busy() const { return state_ == State::Checking || state_ == State::Downloading || state_ == State::Receiving; }

    static const uint32_t kStallMs = 20000; // no data this long: give up
    static const size_t kBudget = 32768;    // bytes per poll, keeps the screen running
    static const size_t kMaxJson = 4096;

  private:
    void fail(const std::string& m);
    Host& h_;
    Writer w_;
    std::unique_ptr<Fetch> f_;
    State state_ = State::Idle;
    Release rel_;
    std::string url_, json_, msg_;
    uint32_t lastData_ = 0;
};

} // namespace upd
