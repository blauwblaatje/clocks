// Bitmap fonts for the jam timer screens, generated with lv_font_conv 1.5.2
// from Montserrat v7.222 (SIL OFL, fonts/OFL-Montserrat.txt), 4 bpp:
//   jt_m12/m13/m14 Medium, jt_b12/b17/b20/b23/b27/b29 Bold: ASCII + U+00B7 + U+2026
//   jt_sb72/sb51/sb43/sb36/sb27 SemiBold: "0123456789:.-" only
// Regenerate: see sim/README.md.
#pragma once
#include "jtui.h"

extern "C" {
LV_FONT_DECLARE(jt_m12)
LV_FONT_DECLARE(jt_m14)
LV_FONT_DECLARE(jt_b12)
LV_FONT_DECLARE(jt_b17)
LV_FONT_DECLARE(jt_b27)
LV_FONT_DECLARE(jt_sb72)
LV_FONT_DECLARE(jt_sb43)
LV_FONT_DECLARE(jt_sb27)
LV_FONT_DECLARE(jt_m13)
LV_FONT_DECLARE(jt_b20)
LV_FONT_DECLARE(jt_b23)
LV_FONT_DECLARE(jt_b29)
LV_FONT_DECLARE(jt_sb51)
LV_FONT_DECLARE(jt_sb36)
}

namespace jt {
inline Fonts bitmapFonts() {
    Fonts f;
    f.m12 = &jt_m12;
    f.m14 = &jt_m14;
    f.b12 = &jt_b12;
    f.b17 = &jt_b17;
    f.b27 = &jt_b27;
    f.clock72 = &jt_sb72;
    f.clock43 = &jt_sb43;
    f.clock27 = &jt_sb27;
    f.m13 = &jt_m13;
    f.b20 = &jt_b20;
    f.b23 = &jt_b23;
    f.b29 = &jt_b29;
    f.clock51 = &jt_sb51;
    f.clock36 = &jt_sb36;
    return f;
}
} // namespace jt
