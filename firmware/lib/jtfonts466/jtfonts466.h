// Bitmap fonts for 466 x 466 displays (M5Stack StopWatch): the sizes of
// jtfonts.h times 466/240, so the screens scale as a whole (scale.h).
// Generated with lv_font_conv 1.5.2 from Montserrat v7.222 (SIL OFL,
// OFL-Montserrat.txt), 4 bpp, same character ranges as jtfonts.h. The name
// keeps the design size: jt466_b17 is the 17 px design font, drawn at 33 px.
//   design 12 13 14 17 20 23 27 29 36 43 51 72
//   real   23 25 27 33 39 45 52 56 70 83 99 140
// Regenerate: see sim/README.md.
#pragma once
#include "jtui.h"

extern "C" {
LV_FONT_DECLARE(jt466_m12)
LV_FONT_DECLARE(jt466_m13)
LV_FONT_DECLARE(jt466_m14)
LV_FONT_DECLARE(jt466_b12)
LV_FONT_DECLARE(jt466_b17)
LV_FONT_DECLARE(jt466_b20)
LV_FONT_DECLARE(jt466_b23)
LV_FONT_DECLARE(jt466_b27)
LV_FONT_DECLARE(jt466_b29)
LV_FONT_DECLARE(jt466_sb27)
LV_FONT_DECLARE(jt466_sb36)
LV_FONT_DECLARE(jt466_sb43)
LV_FONT_DECLARE(jt466_sb51)
LV_FONT_DECLARE(jt466_sb72)
}

namespace jt {
inline Fonts bitmapFonts466() {
    Fonts f;
    f.m12 = &jt466_m12;
    f.m14 = &jt466_m14;
    f.b12 = &jt466_b12;
    f.b17 = &jt466_b17;
    f.b27 = &jt466_b27;
    f.clock72 = &jt466_sb72;
    f.clock43 = &jt466_sb43;
    f.clock27 = &jt466_sb27;
    f.m13 = &jt466_m13;
    f.b20 = &jt466_b20;
    f.b23 = &jt466_b23;
    f.b29 = &jt466_b29;
    f.clock51 = &jt466_sb51;
    f.clock36 = &jt466_sb36;
    return f;
}
} // namespace jt
