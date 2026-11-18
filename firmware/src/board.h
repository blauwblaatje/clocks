// What differs between the boards CLOCK runs on. One implementation per
// board, chosen by the PlatformIO environment (build_src_filter):
//   env c6         board_waveshare_c6.cpp   Waveshare ESP32-C6-Touch-LCD-1.28, 240 x 240
//   env stopwatch  board_m5_stopwatch.cpp   M5Stack StopWatch (ESP32-S3), 466 x 466
// main.cpp and everything in lib/ are the same for every board.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

#include "lvgl.h"
#include "update.h"

namespace board {

extern const upd::Board& kBoard; // which firmware this is: updates fetch and accept only this board's
extern const char kMarker[];     // "CLOCK-BOARD=<id>;" in the image (lib/update MarkerScan)
extern const int kSize;          // round display, pixels across

void begin();                    // power, display, touch, buttons
lv_display_t* createDisplay();   // LVGL display with partial buffers and flush
lv_indev_t* createTouch();       // LVGL pointer input
void setBrightness(int percent); // 0..100
void displayOff();               // before sleep

// The three buttons as the device sees them, seen from the front: crown
// (12 o'clock), left (10) and right (2). Bit 0 crown, bit 1 left, bit 2 right.
enum : uint8_t { kCrown = 1, kLeft = 2, kRight = 4 };
uint8_t buttons();

// Battery: false while unknown or implausible.
bool battery(int& millivolts, int& percent, bool& charging);
// Powered over USB, for the update battery rule.
bool usbPower();
// Power off / deep sleep; does not return.
[[noreturn]] void sleep();
// Board lines for the Info screen (battery and the like).
std::vector<std::string> info();

// Shared by the boards: single-cell LiPo, rough voltage to charge.
int lipoPercent(int millivolts);

} // namespace board
