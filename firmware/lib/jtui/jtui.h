// LVGL screens for the jam timer role (docs/display.md). Draws a jt::View;
// all behaviour is in lib/jtctl. Fonts come from the platform: bitmap fonts
// on the device, TTF-rendered in the desktop simulator.
#pragma once
#include "jtctl.h"
#include "lvgl.h"

namespace jt {

struct Fonts {
    const lv_font_t* m12;     // Montserrat 500, 12 px
    const lv_font_t* m14;     // 500, 14 px
    const lv_font_t* b12;     // 700, 12 px
    const lv_font_t* b17;     // 700, 17 px
    const lv_font_t* b27;     // 700, 27 px
    const lv_font_t* clock72; // 600, 72 px (digits)
    const lv_font_t* clock43; // 600, 43 px (digits)
    const lv_font_t* clock27; // 600, 27 px (digits)
    // box timer screens
    const lv_font_t* m13;     // 500, 13 px
    const lv_font_t* b20;     // 700, 20 px
    const lv_font_t* b23;     // 700, 23 px
    const lv_font_t* b29;     // 700, 29 px
    const lv_font_t* clock51; // 600, 51 px (digits)
    const lv_font_t* clock36; // 600, 36 px (digits)
};

class Ui {
  public:
    // parent: the screen. The UI is a 240 x 240 round area centred in it.
    Ui(lv_obj_t* parent, const Fonts& f);
    void show(const View& v, uint32_t now);
    lv_obj_t* root() const { return root_; }

  private:
    void buildMain();
    void buildPicker();
    void buildNoConn();
    void buildRejoin();
    static void setText(lv_obj_t* label, const std::string& s);

    Fonts f_;
    lv_obj_t* root_;
    // main
    lv_obj_t *main_, *ring_, *inner_, *statusDot_, *statusText_, *statusPill_, *pillText_;
    lv_obj_t *context_, *clock_, *state_, *period_, *hint_;
    // picker
    lv_obj_t *picker_, *pickClock_;
    // no connection
    lv_obj_t *noconn_, *lastClock_, *lastWhat_, *noconnHint_;
    lv_obj_t* dashes_[24];
    // "Rejoin scoreboard?"
    lv_obj_t* rejoin_;
};

} // namespace jt
