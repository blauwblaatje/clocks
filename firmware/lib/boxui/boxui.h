// LVGL screens for the box timer roles (docs/display.md, "Box timer").
// Draws a box::View; behaviour is in lib/boxctl. Layout coordinates match
// box::Controller's hit tests.
#pragma once
#include "boxctl.h"
#include "jtui.h" // jt::Fonts
#include "lvgl.h"

namespace box {

class Ui {
  public:
    Ui(lv_obj_t* parent, const jt::Fonts& f);
    void show(const View& v, uint32_t now);
    lv_obj_t* root() const { return root_; }

  private:
    struct Band {
        lv_obj_t *fill = nullptr, *number = nullptr, *clock = nullptr, *state = nullptr, *team = nullptr;
    };
    std::string layoutKey(const View& v) const;
    void build(const View& v);
    void buildBlockers(int n);
    void buildJammers();
    void buildPicker();
    void buildMenu();
    void buildNoConn(const View& v);
    void updateBand(Band& b, const Slot& s, bool small, const View& v, bool single = false);
    lv_obj_t* label(lv_obj_t* parent, const lv_font_t* f, uint32_t color);

    jt::Fonts f_;
    lv_obj_t* root_;
    std::string key_;
    Band bands_[3];
    lv_obj_t *status_ = nullptr, *hint_ = nullptr, *ring_ = nullptr, *hold_ = nullptr, *statusDot_ = nullptr;
    lv_obj_t *pick_[4] = {}, *pickLabel_[4] = {}, *pickNumber_[4] = {}, *pickTitle_ = nullptr;
    lv_obj_t *menuNumber_ = nullptr, *menuClock_ = nullptr, *menuPenalty_ = nullptr, *menuChange_ = nullptr;
    lv_obj_t *emptyText_[2] = {};
    lv_obj_t* notice_ = nullptr;
};

} // namespace box
