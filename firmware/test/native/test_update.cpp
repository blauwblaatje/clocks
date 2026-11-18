// Host tests for lib/update: SHA-256, release file, versions, image check,
// writer, rules and the check/download/upload flow.
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "sha256.h"
#include "update.h"

using namespace upd;

static int failures = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);          \
            failures++;                                                       \
        }                                                                     \
    } while (0)

static std::string sha(const std::string& s) {
    Sha256 h;
    h.update((const uint8_t*)s.data(), s.size());
    return h.hex();
}

static void sha256() {
    CHECK(sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // one million 'a', fed in odd pieces
    Sha256 h;
    std::string a(997, 'a');
    size_t left = 1000000;
    while (left) {
        size_t n = left < a.size() ? left : a.size();
        h.update((const uint8_t*)a.data(), n);
        left -= n;
    }
    CHECK(h.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

static void releaseAndVersions() {
    Release r;
    std::string err;
    std::string good = "{\"version\": \"0.3.1\", \"app\": \"firmware.bin\", \"size\": 1585292, \"sha256\": \"" +
                       sha("x") + "\"}";
    CHECK(parseRelease(good, r, err) && r.version == "0.3.1" && r.app == "firmware.bin" && r.size == 1585292);
    CHECK(!parseRelease("{\"version\":\"1.0.0\",\"app\":\"f.bin\",\"size\":10,\"sha256\":\"abc\"}", r, err));
    CHECK(!parseRelease("{\"version\":\"1.0.0\"}", r, err));
    CHECK(!parseRelease("<html>", r, err));

    CHECK(newer("0.3.1", "0.3.0") && newer("v0.10.0", "0.9.9") && newer("1.0.0", "0.99.99"));
    CHECK(!newer("0.3.0", "0.3.0") && !newer("0.2.9", "0.3.0") && !newer("garbage", "0.3.0"));
    CHECK(newer("0.1.0", "0.1-dev")); // a dev build takes any release

    CHECK(resolve("https://clock.quagga.rocks/latest.json", "firmware.bin") == "https://clock.quagga.rocks/firmware.bin");
    CHECK(resolve("http://h:8080/rel/latest.json?x=1", "firmware.bin") == "http://h:8080/rel/firmware.bin");
    CHECK(resolve("http://h/rel/latest.json", "/fw/a.bin") == "http://h/fw/a.bin");
    CHECK(resolve("http://h", "a.bin") == "http://h/a.bin");
    CHECK(resolve("http://h/latest.json", "https://o/a.bin") == "https://o/a.bin");
}

// A fake firmware image: header for the chip, bytes, and the board marker
// near the middle ("" = none, as in 0.1.0).
static std::string image(size_t n, uint16_t chip = 13, const std::string& board = "waveshare-c6") {
    std::string s(n, '\0');
    s[0] = (char)0xE9;
    s[1] = 3;
    s[12] = (char)(chip & 0xff);
    s[13] = (char)(chip >> 8);
    for (size_t i = 16; i < n; i++) s[i] = (char)(i * 7);
    if (!board.empty()) {
        std::string m = "CLOCK-BOARD=" + board + ";";
        if (n > 64 + m.size()) s.replace(n / 2, m.size(), m);
    }
    return s;
}

struct FakeSlot : Slot {
    std::string data, failBegin;
    bool begun = false, finished = false, aborted = false, failWrite = false;
    std::string begin(size_t) override {
        if (!failBegin.empty()) return failBegin;
        data.clear();
        begun = true;
        finished = aborted = false;
        return "";
    }
    bool write(const uint8_t* d, size_t n) override {
        if (failWrite) return false;
        data.append((const char*)d, n);
        return true;
    }
    std::string finish() override {
        finished = true;
        return "";
    }
    void abort() override { aborted = true; }
};

static void writer() {
    FakeSlot s;
    Writer w(s, kWaveshareC6);
    std::string img = image(5000);
    CHECK(w.begin(img.size(), sha(img)) == "");
    // pieces smaller than the header first
    CHECK(w.write((const uint8_t*)img.data(), 5) == "" && s.data.empty());
    CHECK(w.write((const uint8_t*)img.data() + 5, 20) == "" && s.data.size() == 25);
    CHECK(w.write((const uint8_t*)img.data() + 25, img.size() - 25) == "");
    CHECK(w.finish() == "" && s.finished && s.data == img);

    // wrong checksum: never finished
    CHECK(w.begin(img.size(), sha("other")) == "");
    w.write((const uint8_t*)img.data(), img.size());
    CHECK(w.finish().find("Checksum") == 0 && s.aborted && !s.finished);
    // short
    CHECK(w.begin(img.size(), "") == "");
    w.write((const uint8_t*)img.data(), 100);
    CHECK(w.finish() == "The update is incomplete" && s.aborted);
    // too long
    CHECK(w.begin(100, "") == "");
    CHECK(w.write((const uint8_t*)img.data(), 101) == "More data than announced");
    // wrong file: nothing reaches the slot
    std::string png = "\x89PNG\r\n\x1a\n" + std::string(100, 'x');
    CHECK(w.begin(png.size(), "") == "");
    s.data.clear();
    CHECK(w.write((const uint8_t*)png.data(), png.size()).find("not a firmware file") != std::string::npos);
    CHECK(s.data.empty() && s.aborted);
    std::string esp32 = image(200, 0); // plain ESP32
    CHECK(w.begin(esp32.size(), "") == "");
    CHECK(w.write((const uint8_t*)esp32.data(), esp32.size()) ==
          "This firmware is for another chip (ESP32)");
    // slot refuses
    s.failBegin = "Update slot too small";
    CHECK(w.begin(100, "") == "Update slot too small" && !w.active());
}

static void writeAll(Writer& w, const std::string& img, size_t piece) {
    for (size_t i = 0; i < img.size(); i += piece)
        w.write((const uint8_t*)img.data() + i, img.size() - i < piece ? img.size() - i : piece);
}

static void boards() {
    CHECK(findBoard("waveshare-c6") == &kWaveshareC6 && findBoard("m5-stopwatch") == &kStopWatch);
    CHECK(findBoard("c6") == nullptr);
    // release address: the site plus the board's folder, or a .json as it stands
    CHECK(releaseUrl("https://clock.quagga.rocks/", kStopWatch) == "https://clock.quagga.rocks/m5-stopwatch/latest.json");
    CHECK(releaseUrl("https://clock.quagga.rocks", kWaveshareC6) == "https://clock.quagga.rocks/waveshare-c6/latest.json");
    CHECK(releaseUrl("http://10.0.0.5:8080/rel/latest.json", kStopWatch) == "http://10.0.0.5:8080/rel/latest.json");
    CHECK(releaseUrl("http://h/x.json?t=1", kStopWatch) == "http://h/x.json?t=1");
    CHECK(releaseUrl("", kStopWatch) == "");

    // marker found across piece boundaries
    FakeSlot s;
    Writer sw(s, kStopWatch);
    std::string img = image(5000, 9, "m5-stopwatch");
    for (size_t piece : {1, 3, 7, 1460}) {
        CHECK(sw.begin(img.size(), sha(img)) == "");
        writeAll(sw, img, piece);
        CHECK(sw.finish() == "" && s.finished);
    }
    // the same chip with another board's marker
    std::string other = image(5000, 9, "waveshare-c6");
    CHECK(sw.begin(other.size(), "") == "");
    writeAll(sw, other, 1000);
    CHECK(sw.finish() == "This firmware is for the Waveshare ESP32-C6 1.28" &&
          s.aborted);
    // C6 firmware on the StopWatch: refused at the header already
    std::string c6 = image(5000, 13, "waveshare-c6");
    CHECK(sw.begin(c6.size(), "") == "");
    CHECK(sw.write((const uint8_t*)c6.data(), c6.size()) ==
          "This firmware is for the Waveshare ESP32-C6 1.28");
    // no marker: refused on the StopWatch, taken on the Waveshare (0.1.0)
    std::string bare = image(5000, 9, "");
    CHECK(sw.begin(bare.size(), "") == "");
    writeAll(sw, bare, 1000);
    CHECK(sw.finish() == "This is not CLOCK firmware for the M5Stack StopWatch");
    Writer cw(s, kWaveshareC6);
    std::string old = image(5000, 13, "");
    CHECK(cw.begin(old.size(), "") == "");
    writeAll(cw, old, 1000);
    CHECK(cw.finish() == "");
    // the prefix alone (as in the scanner's own code), or a broken marker, is no marker
    std::string fake = image(5000, 9, "");
    fake.replace(100, 13, std::string("CLOCK-BOARD=\0", 13));
    fake.replace(200, 25, "CLOCK-CLOCK-BOARD=m5-stop");
    fake.replace(300, 26, "CLOCK-BOARD=m5-stopwatch;;");
    CHECK(sw.begin(fake.size(), "") == "");
    writeAll(sw, fake, 50);
    CHECK(sw.finish() == ""); // the one at 300 counts; 200 is cut off by the next bytes
    std::string broken = image(5000, 9, "");
    broken.replace(100, 25, "CLOCK-BOARD=m5-stopwatch!");
    CHECK(sw.begin(broken.size(), "") == "");
    writeAll(sw, broken, 50);
    CHECK(sw.finish() == "This is not CLOCK firmware for the M5Stack StopWatch");
}

static void rules() {
    Conditions c;
    CHECK(refusal(c) == "");
    c.idle = false;
    CHECK(refusal(c).find("Not while") == 0);
    c.idle = true;
    c.batteryKnown = true;
    c.batteryPct = 29;
    CHECK(refusal(c) == "Battery 29 %: charge or connect USB first");
    c.usbPower = true;
    CHECK(refusal(c) == "");
    c.usbPower = false;
    c.batteryPct = 30;
    CHECK(refusal(c) == "");
}

// ---------- flow ----------

struct FakeFetch : Fetch {
    std::deque<std::string> parts; // "" = nothing this poll; "!err" = fail
    long len = -1;
    std::string err;
    int poll(std::string& chunk) override {
        if (parts.empty()) return 0;
        std::string p = parts.front();
        parts.pop_front();
        if (!p.empty() && p[0] == '!') {
            err = p.substr(1);
            return -1;
        }
        chunk = p;
        return parts.empty() ? 0 : 1;
    }
    long length() const override { return len; }
    std::string error() const override { return err; }
};

struct FakeHost : Host {
    FakeSlot s;
    Conditions c;
    std::string ver = "0.3.0";
    std::vector<std::string> urls;
    std::deque<std::deque<std::string>> answers;
    std::unique_ptr<Fetch> fetch(const std::string& url) override {
        urls.push_back(url);
        if (answers.empty()) return nullptr;
        auto f = std::unique_ptr<FakeFetch>(new FakeFetch);
        f->parts = answers.front();
        answers.pop_front();
        return f;
    }
    Slot& slot() override { return s; }
    Conditions conditions() override { return c; }
    std::string version() override { return ver; }
    const Board& board() override { return kWaveshareC6; }
};

static std::string latest(const std::string& v, const std::string& img) {
    return "{\"version\":\"" + v + "\",\"app\":\"firmware.bin\",\"size\":" + std::to_string(img.size()) +
           ",\"sha256\":\"" + sha(img) + "\"}";
}

static void flow() {
    std::string img = image(100000);
    FakeHost h;
    Flow f(h);
    // up to date
    h.answers.push_back({"", latest("0.3.0", img)});
    f.check("http://h/latest.json", 0);
    CHECK(f.state() == State::Checking);
    f.poll(10);
    CHECK(f.state() == State::Checking); // nothing yet
    f.poll(20);
    CHECK(f.state() == State::UpToDate);
    // newer: offer, then install in pieces
    std::string j = latest("0.3.1", img);
    h.answers.push_back({j.substr(0, 20), j.substr(20)});
    f.check("http://h/rel/latest.json", 100);
    f.poll(110);
    CHECK(f.state() == State::Available && f.release().version == "0.3.1");
    h.c.idle = false;
    CHECK(f.install(120).find("Not while") == 0 && f.state() == State::Available);
    h.c.idle = true;
    std::deque<std::string> parts;
    for (size_t i = 0; i < img.size(); i += 7000) parts.push_back(img.substr(i, 7000));
    h.answers.push_back(parts);
    CHECK(f.install(130) == "" && f.state() == State::Downloading);
    CHECK(h.urls.back() == "http://h/rel/firmware.bin");
    f.poll(140); // budget: not all at once
    CHECK(f.state() == State::Downloading && f.permille() > 0 && f.permille() < 1000);
    for (int i = 0; i < 10 && f.state() == State::Downloading; i++) f.poll(150 + i);
    CHECK(f.state() == State::Installed && h.s.finished && h.s.data == img);

    // damaged download
    f.reset();
    h.answers.push_back({latest("0.3.1", img)});
    f.check("http://h/latest.json", 1000);
    f.poll(1000);
    std::string bad = img;
    bad[5000] ^= 1;
    h.answers.push_back({bad});
    f.install(1010);
    f.poll(1020);
    CHECK(f.state() == State::Failed && f.message().find("Checksum") == 0 && !h.s.finished);
    // network error, stall, cancel
    h.answers.push_back({"!connection refused"});
    f.check("http://h/latest.json", 2000);
    f.poll(2000);
    CHECK(f.state() == State::Failed && f.message() == "Check failed: connection refused");
    h.answers.push_back({"", "", ""});
    f.check("http://h/latest.json", 3000);
    f.poll(3000);
    f.poll(3000 + Flow::kStallMs);
    CHECK(f.state() == State::Failed && f.message() == "Check failed: no answer");
    h.answers.push_back({latest("0.3.1", img)});
    f.check("http://h/latest.json", 4000);
    f.poll(4000);
    std::deque<std::string> ten;
    for (size_t i = 0; i < img.size(); i += 10000) ten.push_back(img.substr(i, 10000));
    h.answers.push_back(ten);
    f.install(4010);
    f.poll(4020);
    f.cancel();
    CHECK(f.state() == State::Idle && h.s.aborted && !h.s.finished);
    f.check("", 5000);
    CHECK(f.state() == State::Failed);
    CHECK(f.install(5000) == "Nothing to install");

    // upload: low battery refused, then a good one in pieces
    f.reset();
    h.c.batteryKnown = true;
    h.c.batteryPct = 10;
    CHECK(f.beginUpload(img.size(), 6000).find("Battery 10 %") == 0);
    h.c.usbPower = true;
    CHECK(f.beginUpload(img.size(), 6000) == "" && f.state() == State::Receiving && f.busy());
    CHECK(f.beginUpload(img.size(), 6000) == "An update is already running");
    for (size_t i = 0; i < img.size(); i += 1460) {
        size_t n = img.size() - i < 1460 ? img.size() - i : 1460;
        CHECK(f.uploadChunk((const uint8_t*)img.data() + i, n, 6001) == "");
    }
    CHECK(f.endUpload(6100) == "" && f.state() == State::Installed && h.s.data == img);
    // upload cut off
    f.reset();
    f.beginUpload(img.size(), 7000);
    f.uploadChunk((const uint8_t*)img.data(), 3000, 7001);
    f.abortUpload("Upload interrupted");
    CHECK(f.state() == State::Failed && f.message() == "Upload interrupted" && h.s.aborted);
}

int main() {
    sha256();
    releaseAndVersions();
    writer();
    boards();
    rules();
    flow();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all update tests passed\n");
    return 0;
}
