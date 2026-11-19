// Button names on screen. A board whose buttons have colours (M5Stack
// StopWatch: yellow, blue, red) names them by colour (Rogier, 2026-11-19);
// a board without (Waveshare) keeps the texts written for it: "Crown",
// "10 o'clock", "2 o'clock", "Undo". dev::Device sets the names for the role
// in use, so a per-role swap of the side buttons is followed.
#pragma once
#include <stdint.h>
#include <string>

namespace btn {

struct Names {
    const char* crown = nullptr; // nullptr: no colour, use the text as written
    const char* left = nullptr;  // the 10 o'clock button (undo, menu)
    const char* right = nullptr; // the 2 o'clock button
    // Where each button is, in clock hours (the LCD face puts its legends
    // there): the Waveshare's 12, 10 and 2; the StopWatch's 11, 7 and 1.
    int crownAt = 12, leftAt = 10, rightAt = 2;
};
inline Names gNames;

// A button's colour for a legend dot; 0 = no colour (Waveshare).
inline uint32_t colourOf(const char* name) {
    std::string n = name ? name : "";
    return n == "Yellow" ? 0xf2c200 : n == "Blue" ? 0x3b82f6 : n == "Red" ? 0xe5383b : 0;
}

inline std::string pick(const char* colour, const char* fallback) { return colour ? colour : fallback; }
inline std::string crown(const char* fallback = "Crown") { return pick(gNames.crown, fallback); }
inline std::string left(const char* fallback = "10 o'clock") { return pick(gNames.left, fallback); }
inline std::string right(const char* fallback = "2 o'clock") { return pick(gNames.right, fallback); }

} // namespace btn
