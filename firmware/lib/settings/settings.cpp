#include "settings.h"
#include <cstdio>
#include <cstdlib>
#include <map>

namespace cfg {

static const char* const kRoleKeys[kRoles] = {"jt", "blockers1", "blockers2", "jammers"};
static const char* const kRoleNames[kRoles] = {"Jam timer", "Team 1 blockers", "Team 2 blockers", "Jammers"};
static const char* const kPat[3] = {"nextjam", "timeoutend", "running"};

const char* roleKey(Role r) { return kRoleKeys[(int)r]; }

// ---------- text helpers ----------

static std::string escapeLine(const std::string& v) {
    std::string o;
    for (char c : v) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else o += c;
    }
    return o;
}

static std::string unescapeLine(const std::string& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char n = v[++i];
            o += n == 'n' ? '\n' : n == 'r' ? '\r' : n;
        } else {
            o += v[i];
        }
    }
    return o;
}

std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') {
            o += ' ';
        } else if (s[i] == '%' && i + 2 < s.size()) {
            o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            o += s[i];
        }
    }
    return o;
}

std::string htmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        case '\'': o += "&#39;"; break;
        default: o += c;
        }
    }
    return o;
}

std::string wifiQr(const std::string& ssid, const std::string& password) {
    auto esc = [](const std::string& v) {
        std::string o;
        for (char c : v) {
            if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') o += '\\';
            o += c;
        }
        return o;
    };
    return "WIFI:T:WPA;S:" + esc(ssid) + ";P:" + esc(password) + ";;";
}

// ---------- storage ----------

static std::string msToText(uint32_t ms) {
    char b[16];
    snprintf(b, sizeof b, "%u:%02u", (unsigned)(ms / 60000), (unsigned)(ms / 1000 % 60));
    return b;
}

// "2:00", "120" (seconds) or "1:30:00" are accepted; 0 on error.
static uint32_t textToMs(const std::string& t) {
    if (t.empty()) return 0;
    uint32_t total = 0, part = 0;
    bool digit = false;
    for (char c : t) {
        if (c >= '0' && c <= '9') {
            part = part * 10 + (c - '0');
            digit = true;
        } else if (c == ':' && digit) {
            total = (total + part) * 60;
            part = 0;
            digit = false;
        } else if (c != ' ') {
            return 0;
        }
    }
    if (!digit) return 0;
    return (total + part) * 1000;
}

std::string serialize(const Settings& s) {
    std::string o = "version=1\n";
    auto put = [&](const std::string& k, const std::string& v) { o += k + "=" + escapeLine(v) + "\n"; };
    put("role", roleKey(s.role));
    for (int r = 0; r < kRoles; r++) put(std::string("swap.") + kRoleKeys[r], s.swapButtons[r] ? "1" : "0");
    put("motor", s.motorFitted ? "1" : "0");
    put("brightness", std::to_string(s.brightness));
    put("sleepMinutes", std::to_string(s.sleepMinutes));
    put("updateUrl", s.updateUrl);
    put("jam", msToText(s.jamMs));
    put("lineup", msToText(s.lineupMs));
    put("timeout", msToText(s.timeoutMs));
    put("period", msToText(s.periodMs));
    put("periods", std::to_string(s.periods));
    put("jamAutoEnd", s.jamAutoEnd ? "1" : "0");
    put("periodAfterTimeout", kPat[(int)s.periodAfterTimeout]);
    put("undoLevels", std::to_string(s.undoLevels));
    for (size_t i = 0; i < s.networks.size(); i++) {
        std::string p = "net" + std::to_string(i) + ".";
        put(p + "ssid", s.networks[i].ssid);
        put(p + "password", s.networks[i].password);
        put(p + "host", s.networks[i].host);
        put(p + "port", std::to_string(s.networks[i].port));
    }
    return o;
}

static void applyValue(Settings& s, const std::string& k, const std::string& v) {
    auto num = [&](int lo, int hi, int& out) {
        char* end = nullptr;
        long n = strtol(v.c_str(), &end, 10);
        if (end && *end == 0 && !v.empty() && n >= lo && n <= hi) out = (int)n;
    };
    auto dur = [&](uint32_t lo, uint32_t hi, uint32_t& out) {
        uint32_t ms = textToMs(v);
        if (ms >= lo && ms <= hi) out = ms;
    };
    if (k == "role") {
        for (int r = 0; r < kRoles; r++)
            if (v == kRoleKeys[r]) s.role = (Role)r;
    } else if (k.compare(0, 5, "swap.") == 0) {
        for (int r = 0; r < kRoles; r++)
            if (k.substr(5) == kRoleKeys[r]) s.swapButtons[r] = v == "1";
    } else if (k == "motor") s.motorFitted = v == "1";
    else if (k == "brightness") num(10, 100, s.brightness);
    else if (k == "sleepMinutes") num(0, 120, s.sleepMinutes);
    // 0.1.0's default named the Waveshare's release file; now the site, so
    // each board finds its own firmware
    else if (k == "updateUrl") s.updateUrl = v == "https://clock.quagga.rocks/latest.json" ? Settings().updateUrl : v;
    else if (k == "jam") dur(10000, 600000, s.jamMs);
    else if (k == "lineup") dur(5000, 300000, s.lineupMs);
    else if (k == "timeout") dur(10000, 600000, s.timeoutMs);
    else if (k == "period") dur(60000, 7200000, s.periodMs);
    else if (k == "periods") num(1, 9, s.periods);
    else if (k == "jamAutoEnd") s.jamAutoEnd = v == "1";
    else if (k == "periodAfterTimeout") {
        for (int i = 0; i < 3; i++)
            if (v == kPat[i]) s.periodAfterTimeout = (PeriodAfterTimeout)i;
    } else if (k == "undoLevels") num(0, 8, s.undoLevels);
    else if (k.compare(0, 3, "net") == 0 && k.size() > 5 && k[4] == '.') {
        int i = k[3] - '0';
        if (i < 0 || i >= kMaxNetworks) return;
        if ((int)s.networks.size() <= i) s.networks.resize(i + 1);
        std::string f = k.substr(5);
        Network& n = s.networks[i];
        if (f == "ssid") n.ssid = v;
        else if (f == "password") n.password = v;
        else if (f == "host") n.host = v;
        else if (f == "port") num(1, 65535, n.port);
    }
}

Settings parse(const std::string& text) {
    Settings s;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        applyValue(s, line.substr(0, eq), unescapeLine(line.substr(eq + 1)));
    }
    // drop networks without a name
    std::vector<Network> nets;
    for (auto& n : s.networks)
        if (!n.ssid.empty()) nets.push_back(n);
    s.networks = nets;
    return s;
}

// ---------- setup page ----------

static std::map<std::string, std::string> formFields(const std::string& body) {
    std::map<std::string, std::string> f;
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t amp = body.find('&', pos);
        if (amp == std::string::npos) amp = body.size();
        std::string kv = body.substr(pos, amp - pos);
        size_t eq = kv.find('=');
        if (!kv.empty()) f[urlDecode(kv.substr(0, eq))] = eq == std::string::npos ? "" : urlDecode(kv.substr(eq + 1));
        pos = amp + 1;
    }
    return f;
}

std::string applyForm(const Settings& old, const std::string& body, Settings& out) {
    auto f = formFields(body);
    Settings s = old;
    std::string err;
    auto get = [&](const std::string& k) { return f.count(k) ? f[k] : std::string(); };
    auto dur = [&](const char* k, const char* what, uint32_t lo, uint32_t hi, uint32_t& dst) {
        uint32_t ms = textToMs(get(k));
        if (ms < lo || ms > hi) err += std::string(what) + " must be between " + msToText(lo) + " and " + msToText(hi) + ". ";
        else dst = ms;
    };
    auto num = [&](const char* k, const char* what, int lo, int hi, int& dst) {
        char* end = nullptr;
        std::string v = get(k);
        long n = strtol(v.c_str(), &end, 10);
        if (v.empty() || *end || n < lo || n > hi)
            err += std::string(what) + " must be " + std::to_string(lo) + " to " + std::to_string(hi) + ". ";
        else dst = (int)n;
    };

    bool roleOk = false;
    for (int r = 0; r < kRoles; r++)
        if (get("role") == kRoleKeys[r]) {
            s.role = (Role)r;
            roleOk = true;
        }
    if (!roleOk) err += "Choose a role. ";
    for (int r = 0; r < kRoles; r++) s.swapButtons[r] = f.count(std::string("swap.") + kRoleKeys[r]) > 0;
    s.motorFitted = f.count("motor") > 0;
    num("brightness", "Brightness", 10, 100, s.brightness);
    if (f.count("sleepMinutes")) num("sleepMinutes", "Sleep after", 0, 120, s.sleepMinutes);
    if (f.count("updateUrl")) {
        s.updateUrl = get("updateUrl");
        if (!s.updateUrl.empty() && s.updateUrl.compare(0, 7, "http://") != 0 && s.updateUrl.compare(0, 8, "https://") != 0)
            err += "The release address must start with http:// or https://. ";
    }
    dur("jam", "Jam length", 10000, 600000, s.jamMs);
    dur("lineup", "Lineup length", 5000, 300000, s.lineupMs);
    dur("timeout", "Timeout length", 10000, 600000, s.timeoutMs);
    dur("period", "Period length", 60000, 7200000, s.periodMs);
    num("periods", "Number of periods", 1, 9, s.periods);
    s.jamAutoEnd = f.count("jamAutoEnd") > 0;
    bool patOk = false;
    for (int i = 0; i < 3; i++)
        if (get("periodAfterTimeout") == kPat[i]) {
            s.periodAfterTimeout = (PeriodAfterTimeout)i;
            patOk = true;
        }
    if (!patOk) err += "Choose when the period clock runs after a timeout. ";
    num("undoLevels", "Undo levels", 0, 8, s.undoLevels);

    s.networks.clear();
    for (int i = 0; i < kMaxNetworks; i++) {
        std::string p = "net" + std::to_string(i) + ".";
        Network n;
        n.ssid = get(p + "ssid");
        if (n.ssid.empty()) continue;
        if (f.count(p + "delete")) continue;
        n.password = get(p + "password");
        // blank password: keep the saved one for that network name
        if (n.password.empty())
            for (auto& o : old.networks)
                if (o.ssid == n.ssid) n.password = o.password;
        n.host = get(p + "host");
        if (n.host.empty()) err += "Network " + n.ssid + ": the scoreboard address is missing. ";
        std::string port = get(p + "port");
        char* end = nullptr;
        long pn = strtol(port.c_str(), &end, 10);
        if (port.empty()) pn = 8000;
        else if (*end || pn < 1 || pn > 65535) err += "Network " + n.ssid + ": port must be 1 to 65535. ";
        n.port = (int)pn;
        s.networks.push_back(n);
    }
    if (!err.empty()) return err;
    out = s;
    return "";
}

std::string page(const Settings& s, const std::string& message, const std::string& firmware) {
    std::string h;
    h.reserve(8000);
    h += "<!doctype html><html><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         "<title>CLOCK setup</title><style>"
         "body{font-family:system-ui,sans-serif;margin:0 auto;max-width:560px;padding:12px 16px;background:#111;color:#eee}"
         "h1{font-size:1.3em}h2{font-size:1.05em;margin-top:1.6em;border-bottom:1px solid #444}"
         "label{display:block;margin:.5em 0}input[type=text],input[type=password],input[type=number],select"
         "{width:100%;box-sizing:border-box;padding:6px;background:#222;color:#eee;border:1px solid #555;border-radius:4px}"
         "fieldset{border:1px solid #444;border-radius:6px;margin:.6em 0}.row{display:flex;gap:8px}.row>*{flex:1}"
         ".msg{padding:8px;border-radius:4px;background:#244;margin:.6em 0}.err{background:#522}"
         "button{padding:10px 18px;font-size:1em;margin-top:1em}small{color:#aaa}</style></head><body>"
         "<h1>CLOCK setup</h1>";
    if (!message.empty()) {
        bool ok = message.compare(0, 5, "Saved") == 0;
        h += std::string("<div class=\"msg") + (ok ? "" : " err") + "\">" + htmlEscape(message) + "</div>";
    }
    h += "<form method=\"post\" action=\"/save\">";

    h += "<h2>Role</h2><label>This device is<select name=\"role\">";
    for (int r = 0; r < kRoles; r++)
        h += std::string("<option value=\"") + kRoleKeys[r] + "\"" + ((int)s.role == r ? " selected" : "") + ">" +
             kRoleNames[r] + "</option>";
    h += "</select></label><small>Swap the 10 and 2 o'clock buttons for:</small>";
    for (int r = 0; r < kRoles; r++)
        h += std::string("<label><input type=\"checkbox\" name=\"swap.") + kRoleKeys[r] + "\"" +
             (s.swapButtons[r] ? " checked" : "") + "> " + kRoleNames[r] + "</label>";

    h += "<h2>Wi-Fi and scoreboard</h2><small>Tried in this order. Leave the password empty to keep the saved "
         "one.</small>";
    for (int i = 0; i < kMaxNetworks; i++) {
        Network n = i < (int)s.networks.size() ? s.networks[i] : Network();
        std::string p = "net" + std::to_string(i) + ".";
        h += "<fieldset><legend>Network " + std::to_string(i + 1) + "</legend>";
        h += "<label>Wi-Fi name<input type=\"text\" name=\"" + p + "ssid\" value=\"" + htmlEscape(n.ssid) + "\"></label>";
        h += "<label>Password<input type=\"password\" name=\"" + p + "password\" placeholder=\"" +
             std::string(n.password.empty() ? "" : "(saved)") + "\"></label>";
        h += "<div class=\"row\"><label>Scoreboard address<input type=\"text\" name=\"" + p + "host\" value=\"" +
             htmlEscape(n.host) + "\" placeholder=\"192.168.1.10\"></label>";
        h += "<label>Port<input type=\"number\" name=\"" + p + "port\" value=\"" + std::to_string(n.port) +
             "\"></label></div>";
        if (!n.ssid.empty()) h += "<label><input type=\"checkbox\" name=\"" + p + "delete\"> Forget this network</label>";
        h += "</fieldset>";
    }

    h += "<h2>Standalone</h2><small>Used when the device runs without the scoreboard. m:ss</small>";
    auto txt = [&](const char* name, const char* label, const std::string& v) {
        h += std::string("<label>") + label + "<input type=\"text\" name=\"" + name + "\" value=\"" + htmlEscape(v) +
             "\"></label>";
    };
    h += "<div class=\"row\">";
    txt("jam", "Jam", msToText(s.jamMs));
    txt("lineup", "Lineup", msToText(s.lineupMs));
    txt("timeout", "Timeout", msToText(s.timeoutMs));
    h += "</div><div class=\"row\">";
    txt("period", "Period", msToText(s.periodMs));
    txt("periods", "Periods", std::to_string(s.periods));
    txt("undoLevels", "Undo levels (0-8)", std::to_string(s.undoLevels));
    h += "</div>";
    h += std::string("<label><input type=\"checkbox\" name=\"jamAutoEnd\"") + (s.jamAutoEnd ? " checked" : "") +
         "> The jam ends by itself when the jam clock runs out</label>";
    static const char* const patNames[3] = {"stopped until the next jam starts", "runs again when the timeout ends",
                                            "keeps running during a timeout"};
    h += "<label>After a timeout the period clock<select name=\"periodAfterTimeout\">";
    for (int i = 0; i < 3; i++)
        h += std::string("<option value=\"") + kPat[i] + "\"" + ((int)s.periodAfterTimeout == i ? " selected" : "") +
             ">" + patNames[i] + "</option>";
    h += "</select></label>";

    h += "<h2>Device</h2>";
    h += "<label>Brightness (%)<input type=\"number\" name=\"brightness\" min=\"10\" max=\"100\" value=\"" +
         std::to_string(s.brightness) + "\"></label>";
    h += "<label>Sleep after (minutes without a clock running or a press; 0 = never)"
         "<input type=\"number\" name=\"sleepMinutes\" min=\"0\" max=\"120\" value=\"" +
         std::to_string(s.sleepMinutes) + "\"></label>";
    h += std::string("<label><input type=\"checkbox\" name=\"motor\"") + (s.motorFitted ? " checked" : "") +
         "> Vibration motor fitted</label>";
    h += "<label>Release address (for Update in the device menu: the install site, or a latest.json)"
         "<input type=\"text\" name=\"updateUrl\" value=\"" +
         htmlEscape(s.updateUrl) + "\" placeholder=\"https://clock.quagga.rocks/\"></label>";
    h += "<button type=\"submit\">Save</button></form>";
    if (!firmware.empty()) {
        // Upload without internet: the raw file as the body (no multipart),
        // streamed into the update slot (lib/update).
        h += "<h2>Firmware</h2><p>Running: " + htmlEscape(firmware) +
             "</p><small>Install a firmware.bin file (from the install site). Not while a clock runs; with a low "
             "battery only on USB power. The device restarts when it is done.</small>"
             "<label><input type=\"file\" id=\"fw\" accept=\".bin\"></label>"
             "<button type=\"button\" id=\"up\" onclick=\"up()\">Install</button><p id=\"st\"></p>"
             "<script>function up(){var f=document.getElementById('fw').files[0],s=document.getElementById('st');"
             "if(!f){s.textContent='Choose firmware.bin first';return}"
             "var x=new XMLHttpRequest();x.open('POST','/update');"
             "x.upload.onprogress=function(e){if(e.lengthComputable)s.textContent='Sending '+"
             "Math.round(e.loaded*100/e.total)+' %'};"
             "x.onload=function(){s.textContent=x.responseText};"
             "x.onerror=function(){s.textContent='Upload failed: the connection was lost (see the device screen)'};"
             "x.setRequestHeader('Content-Type','application/octet-stream');s.textContent='Sending...';x.send(f)}"
             "</script>";
    }
    h += "</body></html>";
    return h;
}

} // namespace cfg
