// LVGL screen for the device menu (lib/devmenu). Drawn as an overlay over
// the role screen; layout matches menu::Menu's hit tests.
#pragma once
#include "devmenu.h"
#include "jtui.h" // jt::Fonts
#include "lvgl.h"

namespace menu {

class Ui {
  public:
    Ui(lv_obj_t* parent, const jt::Fonts& f);
    void show(const View& v);
    lv_obj_t* root() const { return root_; }

  private:
    void build(const View& v);
    lv_obj_t* label(lv_obj_t* parent, const lv_font_t* f, uint32_t color, const std::string& text);
    jt::Fonts f_;
    lv_obj_t* root_;
    std::string key_;
};

} // namespace menu
