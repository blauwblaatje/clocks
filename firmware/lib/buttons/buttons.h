// Button names on screen. A board whose buttons have colours (M5Stack
// StopWatch: yellow, blue, red) names them by colour (Rogier, 2026-11-19);
// a board without (Waveshare) keeps the texts written for it: "Crown",
// "10 o'clock", "2 o'clock", "Undo". dev::Device sets the names for the role
// in use, so a per-role swap of the side buttons is followed.
#pragma once
#include <string>

namespace btn {

struct Names {
    const char* crown = nullptr; // nullptr: no colour, use the text as written
    const char* left = nullptr;  // the 10 o'clock button (undo, menu)
    const char* right = nullptr; // the 2 o'clock button
};
inline Names gNames;

inline std::string pick(const char* colour, const char* fallback) { return colour ? colour : fallback; }
inline std::string crown(const char* fallback = "Crown") { return pick(gNames.crown, fallback); }
inline std::string left(const char* fallback = "10 o'clock") { return pick(gNames.left, fallback); }
inline std::string right(const char* fallback = "2 o'clock") { return pick(gNames.right, fallback); }

} // namespace btn
