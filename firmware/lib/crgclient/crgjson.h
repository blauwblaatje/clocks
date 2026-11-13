// Minimal JSON reading and writing for CRG WebSocket frames. No Arduino
// dependencies. CRG frames are one object whose values are either scalars or,
// for "state", a flat object of key -> scalar; that is all this handles
// fully. Nested values elsewhere are skipped.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string>

namespace crg {

struct JsonValue {
    enum Type : uint8_t { Null, Bool, Number, String, Other } type = Null;
    bool b = false;
    double num = 0;
    std::string str; // decoded string, or the raw number text

    long long asInt() const { return type == Number ? (long long)num : (type == String ? atoll(str.c_str()) : 0); }
    bool asBool() const { return type == Bool ? b : (type == String && str == "true"); }
    // String form, as CRG's toString() would give it.
    std::string text() const;
};

// Called for each top-level member; for "state" it is called once per entry
// with section = "state". Return false to stop.
using JsonVisitor = bool (*)(void* ctx, const char* section, const std::string& key, const JsonValue& v);

// Parses one frame. Returns false on malformed JSON (members seen before the
// error have already been visited).
bool parseFrame(const std::string& text, JsonVisitor visit, void* ctx);

// Appends s as a JSON string literal (with quotes).
void appendQuoted(std::string& out, const std::string& s);

} // namespace crg
