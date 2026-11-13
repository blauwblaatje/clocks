// Board drivers: GC9A01 display (through Arduino_GFX), CST816S touch,
// backlight and battery. Compiles; not run on the board yet.
#pragma once
#include <stdint.h>

#include "lvgl.h"

namespace board {

// The board sits in the case a quarter turn round (USB at 3 o'clock seen from
// the front, the board's own "down" is its USB side). Picture and touch are
// turned by the same amount. If the picture comes out upside down on the
// first run, change kRotation from 1 to 3; touch follows automatically.
constexpr uint8_t kRotation = 1;

void begin();                   // display, touch, backlight
lv_display_t* createDisplay();  // LVGL display with partial buffers and flush
lv_indev_t* createTouch();      // LVGL pointer input from the CST816S
void setBrightness(int percent); // 0..100
void displayOff();              // backlight off, panel to sleep (before deep sleep)
// Battery voltage in mV and an estimated charge; false if the reading is implausible.
bool battery(int& millivolts, int& percent);

// Touch coordinates from the panel's own frame to the screen's (rotation).
void rotateTouch(uint8_t rotation, int nx, int ny, int& lx, int& ly);

} // namespace board
