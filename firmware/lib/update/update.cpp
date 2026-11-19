#include "update.h"

#include <cstdlib>
#include <cstring>

#include "crgjson.h"

namespace upd {

// ---------- release file ----------

bool parseRelease(const std::string& json, Release& r, std::string& err) {
    r = Release();
    bool ok = crg::parseFrame(
        json,
        [](void* ctx, const char* section, const std::string& key, const crg::JsonValue& v) -> bool {
            Release& r = *(Release*)ctx;
            if (section[0]) return true;
            if (key == "version") r.version = v.text();
            else if (key == "app") r.app = v.text();
            else if (key == "sha256") r.sha256 = v.text();
            else if (key == "size") r.size = (long)v.asInt();
            return true;
        },
        &r);
    if (!ok) err = "The release file is not valid JSON";
    else if (r.version.empty() || r.app.empty() || r.size <= 0) err = "The release file misses version, app or size";
    else if (r.sha256.size() != 64 || r.sha256.find_first_not_of("0123456789abcdef") != std::string::npos)
        err = "The release file has no valid sha256";
    else return true;
    return false;
}

static bool triple(std::string v, long out[3]) {
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
    const char* p = v.c_str();
    for (int i = 0; i < 3; i++) {
        if (*p < '0' || *p > '9') return false;
        char* end = nullptr;
        out[i] = strtol(p, &end, 10);
        p = end;
        if (i < 2) {
            if (*p != '.') return false;
            p++;
        }
    }
    return *p == 0;
}

bool newer(const std::string& offered, const std::string& current) {
    long o[3], c[3];
    if (!triple(offered, o)) return false;
    if (!triple(current, c)) return true;
    for (int i = 0; i < 3; i++)
        if (o[i] != c[i]) return o[i] > c[i];
    return false;
}

std::string resolve(const std::string& base, const std::string& ref) {
    if (ref.find("://") != std::string::npos) return ref;
    size_t scheme = base.find("://");
    size_t hostEnd = base.find('/', scheme == std::string::npos ? 0 : scheme + 3);
    if (!ref.empty() && ref[0] == '/') return base.substr(0, hostEnd) + ref;
    size_t q = base.find_first_of("?#");
    std::string path = base.substr(0, q);
    size_t slash = path.rfind('/');
    if (slash == std::string::npos || slash < hostEnd) return path.substr(0, hostEnd) + "/" + ref;
    return path.substr(0, slash + 1) + ref;
}

// ---------- boards ----------

const Board kWaveshareC6 = {"waveshare-c6", "Waveshare ESP32-C6 1.28", 13, true};
// Yellow at 11 o'clock, blue at 1 o'clock, red (power) at 7 o'clock; the
// mapping is in board_m5_stopwatch.cpp (decided 2026-11-19).
const Board kStopWatch = {"m5-stopwatch", "M5Stack StopWatch", 9, false, "Yellow", "Red", "Blue"};
static const Board* const kBoards[] = {&kWaveshareC6, &kStopWatch};

const Board* findBoard(const std::string& id) {
    for (const Board* b : kBoards)
        if (id == b->id) return b;
    return nullptr;
}

std::string releaseUrl(const std::string& setting, const Board& b) {
    if (setting.empty()) return "";
    std::string path = setting.substr(0, setting.find_first_of("?#"));
    if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".json") == 0) return setting;
    std::string u = path;
    if (u.back() != '/') u += '/';
    return u + b.id + "/latest.json";
}

static std::string chipName(uint16_t chip) {
    switch (chip) {
    case 0: return "ESP32";
    case 9: return "ESP32-S3";
    case 13: return "ESP32-C6";
    }
    return "id " + std::to_string(chip);
}

std::string checkImage(const uint8_t* p, size_t n, const Board& b) {
    if (n < kImageHeader) return "The file is too short to be firmware";
    if (p[0] != 0xE9) return "This is not a firmware file (use firmware.bin)";
    uint16_t chip = (uint16_t)(p[12] | p[13] << 8);
    if (chip != b.chip) {
        for (const Board* o : kBoards)
            if (o->chip == chip) return std::string("This firmware is for the ") + o->name;
        return std::string("This firmware is for another chip (") + chipName(chip) + ")";
    }
    return "";
}

void MarkerScan::reset() {
    matched_ = 0;
    inId_ = false;
    id_.clear();
    found_.clear();
}

// The prefix in the scanner's own code is followed by a NUL, never by an id
// and ';', so it does not count as a marker.
void MarkerScan::feed(const uint8_t* p, size_t n) {
    static const char kPrefix[] = CLOCK_BOARD_MARKER_PREFIX;
    static const size_t kLen = sizeof kPrefix - 1;
    for (size_t i = 0; i < n; i++) {
        char c = (char)p[i];
        if (inId_) {
            if (c == ';' && !id_.empty()) {
                bool seen = false;
                for (auto& f : found_) seen |= f == id_;
                if (!seen && found_.size() < 4) found_.push_back(id_);
                inId_ = false;
                id_.clear();
                continue;
            }
            if (((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') && id_.size() < 32) {
                id_ += c;
                continue;
            }
            inId_ = false;
            id_.clear();
            matched_ = 0; // fall through: c may start a new prefix
        }
        if (c == kPrefix[matched_]) {
            if (++matched_ == kLen) {
                inId_ = true;
                matched_ = 0;
            }
        } else {
            matched_ = c == kPrefix[0] ? 1 : 0;
        }
    }
}

std::string MarkerScan::verdict(const Board& b) const {
    for (auto& f : found_)
        if (f == b.id) return "";
    if (!found_.empty()) {
        const Board* o = findBoard(found_[0]);
        return std::string("This firmware is for the ") + (o ? o->name : found_[0].c_str());
    }
    if (b.legacy) return ""; // built before the marker existed
    return std::string("This is not CLOCK firmware for the ") + b.name;
}

// ---------- writer ----------

std::string Writer::begin(size_t size, const std::string& sha256hex) {
    abort();
    if (size < kImageHeader) return "The file is too short to be firmware";
    std::string e = slot_.begin(size);
    if (!e.empty()) return e;
    sha_.reset();
    marks_.reset();
    want_ = sha256hex;
    size_ = size;
    written_ = 0;
    active_ = true;
    return "";
}

std::string Writer::write(const uint8_t* data, size_t len) {
    if (!active_) return "No update in progress";
    if (written_ + len > size_) {
        abort();
        return "More data than announced";
    }
    // The image header is checked before anything reaches the slot.
    size_t keep = 0;
    if (written_ < kImageHeader) {
        keep = kImageHeader - written_ < len ? kImageHeader - written_ : len;
        memcpy(head_ + written_, data, keep);
        if (written_ + keep == kImageHeader) {
            std::string e = checkImage(head_, kImageHeader, board_);
            if (!e.empty()) {
                abort();
                return e;
            }
            if (!slot_.write(head_, kImageHeader)) {
                abort();
                return "Writing the update failed";
            }
        }
    }
    sha_.update(data, len);
    marks_.feed(data, len);
    written_ += len;
    if (len > keep && !slot_.write(data + keep, len - keep)) {
        abort();
        return "Writing the update failed";
    }
    return "";
}

std::string Writer::finish() {
    if (!active_) return "No update in progress";
    if (written_ != size_) {
        abort();
        return "The update is incomplete";
    }
    if (!want_.empty() && sha_.hex() != want_) {
        abort();
        return "Checksum wrong: the download is damaged";
    }
    std::string m = marks_.verdict(board_);
    if (!m.empty()) {
        abort();
        return m;
    }
    active_ = false;
    std::string e = slot_.finish();
    return e;
}

void Writer::abort() {
    if (active_) slot_.abort();
    active_ = false;
}

// ---------- rules ----------

std::string refusal(const Conditions& c) {
    if (!c.idle) return "Not while a clock runs or someone is in the box";
    if (c.batteryKnown && c.batteryPct < kMinBatteryPct && !c.usbPower)
        return "Battery " + std::to_string(c.batteryPct) + " %: charge or connect USB first";
    return "";
}

// ---------- flow ----------

void Flow::fail(const std::string& m) {
    f_.reset();
    w_.abort();
    msg_ = m;
    state_ = State::Failed;
}

void Flow::check(const std::string& url, uint32_t now) {
    cancel();
    url_ = url;
    json_.clear();
    msg_.clear();
    if (url.empty()) {
        fail("No release address set (setup page)");
        return;
    }
    f_ = h_.fetch(url);
    if (!f_) {
        fail("Cannot reach " + url);
        return;
    }
    state_ = State::Checking;
    lastData_ = now;
}

std::string Flow::install(uint32_t now) {
    if (state_ != State::Available) return "Nothing to install";
    std::string r = refusal(h_.conditions());
    if (!r.empty()) return r;
    std::string e = w_.begin((size_t)rel_.size, rel_.sha256);
    if (!e.empty()) {
        fail(e);
        return e;
    }
    f_ = h_.fetch(resolve(url_, rel_.app));
    if (!f_) {
        fail("Cannot reach the release address");
        return msg_;
    }
    state_ = State::Downloading;
    lastData_ = now;
    return "";
}

void Flow::cancel() {
    f_.reset();
    w_.abort();
    if (busy()) state_ = State::Idle;
}

void Flow::reset() {
    cancel();
    state_ = State::Idle;
    msg_.clear();
}

void Flow::poll(uint32_t now) {
    if (!f_ || (state_ != State::Checking && state_ != State::Downloading)) return;
    size_t got = 0;
    while (got < kBudget) {
        std::string chunk;
        int r = f_->poll(chunk);
        if (r < 0) {
            fail((state_ == State::Checking ? "Check failed: " : "Download failed: ") + f_->error());
            return;
        }
        if (!chunk.empty()) {
            lastData_ = now;
            got += chunk.size();
            if (state_ == State::Checking) {
                json_ += chunk;
                if (json_.size() > kMaxJson) {
                    fail("The release file is too large");
                    return;
                }
            } else {
                std::string e = w_.write((const uint8_t*)chunk.data(), chunk.size());
                if (!e.empty()) {
                    fail(e);
                    return;
                }
            }
        }
        if (r == 0) {
            f_.reset();
            if (state_ == State::Checking) {
                std::string err;
                if (!parseRelease(json_, rel_, err)) {
                    fail(err);
                } else if (newer(rel_.version, h_.version())) {
                    state_ = State::Available;
                } else {
                    state_ = State::UpToDate;
                }
            } else {
                std::string e = w_.finish();
                if (!e.empty()) fail(e);
                else state_ = State::Installed;
            }
            return;
        }
        if (chunk.empty()) break; // nothing more for now
    }
    if (now - lastData_ >= kStallMs) fail(state_ == State::Checking ? "Check failed: no answer" : "Download stalled");
}

std::string Flow::beginUpload(size_t size, uint32_t now) {
    if (busy()) return "An update is already running";
    std::string r = refusal(h_.conditions());
    if (!r.empty()) {
        fail(r);
        return r;
    }
    msg_.clear();
    rel_ = Release();
    rel_.size = (long)size;
    std::string e = w_.begin(size, "");
    if (!e.empty()) return e;
    state_ = State::Receiving;
    lastData_ = now;
    return "";
}

std::string Flow::uploadChunk(const uint8_t* data, size_t len, uint32_t now) {
    if (state_ != State::Receiving) return "No upload in progress";
    std::string e = w_.write(data, len);
    if (!e.empty()) {
        fail(e);
        return e;
    }
    lastData_ = now;
    return "";
}

std::string Flow::endUpload(uint32_t) {
    if (state_ != State::Receiving) return "No upload in progress";
    std::string e = w_.finish();
    if (!e.empty()) {
        fail(e);
        return e;
    }
    state_ = State::Installed;
    return "";
}

void Flow::abortUpload(const std::string& why) {
    if (state_ == State::Receiving) fail(why);
}

int Flow::permille() const {
    if (w_.size() == 0) return 0;
    return (int)((uint64_t)w_.written() * 1000 / w_.size());
}

} // namespace upd
