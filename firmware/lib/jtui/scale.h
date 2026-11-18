// Screen scale. Every screen is designed in 240 x 240 pixels (docs/display.md,
// the Waveshare board). On a bigger round display (M5Stack StopWatch: 466 x
// 466) the screen code passes every size through px(), and the fonts come
// in matching sizes (jtfonts.h). Touch arrives in display pixels and goes
// back to design pixels with design() before the controllers see it, so all
// hit tests stay in 240 x 240.
#pragma once

namespace ui {

static const int kDesign = 240;
extern int gScreen; // display width and height in pixels; set by dev::Device

inline int px(int v) {
    if (gScreen == kDesign) return v;
    long n = (long)v * gScreen;
    return (int)(n >= 0 ? (n + kDesign / 2) / kDesign : -((-n + kDesign / 2) / kDesign));
}
inline int design(int v) { return gScreen == kDesign ? v : (int)((long)v * kDesign / gScreen); }

} // namespace ui
