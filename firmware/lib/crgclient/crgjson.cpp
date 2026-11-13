#include "crgjson.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace crg {

std::string JsonValue::text() const {
    switch (type) {
    case Bool: return b ? "true" : "false";
    case Number:
    case String: return str;
    default: return "";
    }
}

namespace {

struct Parser {
    const std::string& s;
    size_t i = 0;

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
    }
    bool eat(char c) {
        ws();
        if (i < s.size() && s[i] == c) {
            i++;
            return true;
        }
        return false;
    }
    static void putUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(uint32_t& v) {
        if (i + 4 > s.size()) return false;
        v = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        ws();
        if (i >= s.size() || s[i] != '"') return false;
        i++;
        out.clear();
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    i += 2;
                    uint32_t lo;
                    if (!hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                putUtf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool literal(const char* w) {
        size_t n = 0;
        while (w[n]) n++;
        if (s.compare(i, n, w) != 0) return false;
        i += n;
        return true;
    }
    // Scalar or (skipped) compound value.
    bool value(JsonValue& v) {
        ws();
        if (i >= s.size()) return false;
        char c = s[i];
        v = JsonValue();
        if (c == '"') {
            v.type = JsonValue::String;
            return string(v.str);
        }
        if (c == 't') {
            v.type = JsonValue::Bool;
            v.b = true;
            return literal("true");
        }
        if (c == 'f') {
            v.type = JsonValue::Bool;
            return literal("false");
        }
        if (c == 'n') return literal("null");
        if (c == '{' || c == '[') {
            v.type = JsonValue::Other;
            return skip();
        }
        size_t st = i;
        while (i < s.size() && (strchr("+-0123456789.eE", s[i]) != nullptr)) i++;
        if (i == st) return false;
        v.type = JsonValue::Number;
        v.str = s.substr(st, i - st);
        v.num = strtod(v.str.c_str(), nullptr);
        return true;
    }
    bool skip() {
        int depth = 0;
        std::string tmp;
        while (i < s.size()) {
            char c = s[i];
            if (c == '"') {
                if (!string(tmp)) return false;
                continue;
            }
            i++;
            if (c == '{' || c == '[') depth++;
            else if (c == '}' || c == ']') {
                if (--depth == 0) return true;
            }
        }
        return false;
    }
};

} // namespace

bool parseFrame(const std::string& text, JsonVisitor visit, void* ctx) {
    Parser p{text};
    if (!p.eat('{')) return false;
    if (p.eat('}')) return true;
    std::string key, inner;
    JsonValue v;
    do {
        if (!p.string(key) || !p.eat(':')) return false;
        p.ws();
        if (key == "state" && p.i < text.size() && text[p.i] == '{') {
            p.i++;
            if (!p.eat('}')) {
                do {
                    if (!p.string(inner) || !p.eat(':') || !p.value(v)) return false;
                    if (!visit(ctx, "state", inner, v)) return true;
                } while (p.eat(','));
                if (!p.eat('}')) return false;
            }
        } else {
            if (!p.value(v)) return false;
            if (!visit(ctx, "", key, v)) return true;
        }
    } while (p.eat(','));
    return p.eat('}');
}

void appendQuoted(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += (char)c;
            }
        }
    }
    out += '"';
}

} // namespace crg
