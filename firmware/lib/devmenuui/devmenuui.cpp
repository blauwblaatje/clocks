#include "devmenuui.h"
#include "scale.h"

using ui::px;

namespace menu {

namespace {

const uint32_t kQuad[4] = {0x26323f, 0x2c2c33, 0x23362c, 0x3a2a2a};
const uint32_t kButton = 0x2f2f33, kGreen = 0x6fdf6f;

lv_obj_t* rect(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, int radius = 0) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, px(x), px(y));
    lv_obj_set_size(o, px(w), px(h));
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, px(radius), 0);
    return o;
}

} // namespace

lv_obj_t* Ui::label(lv_obj_t* parent, const lv_font_t* f, uint32_t color, const std::string& text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, text.c_str());
    return l;
}

Ui::Ui(lv_obj_t* parent, const jt::Fonts& f) : f_(f) {
    root_ = rect(parent, 0, 0, 240, 240, 0x000000, LV_RADIUS_CIRCLE);
    lv_obj_center(root_);
    lv_obj_set_style_clip_corner(root_, true, 0);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void Ui::build(const View& v) {
    lv_obj_clean(root_);
    switch (v.screen) {
    case Screen::Closed: break;
    case Screen::Top: {
        for (int q = 0; q < 4; q++) {
            bool right = q % 2, bottom = q >= 2;
            lv_obj_t* r = rect(root_, right ? 122 : 0, bottom ? 122 : 0, 118, 118, kQuad[q]);
            lv_obj_t* t = label(r, f_.b17, 0xffffff, v.quadrants[q]);
            lv_obj_align(t, LV_ALIGN_CENTER, px(right ? -6 : 6), px(bottom ? -8 : 8));
        }
        lv_obj_t* disc = rect(root_, 80, 80, 80, 80, 0xe9e9e9, LV_RADIUS_CIRCLE);
        lv_obj_t* c = label(disc, f_.b17, 0x111111, "Close");
        lv_obj_center(c);
        break;
    }
    case Screen::List: {
        lv_obj_t* t = label(root_, f_.b17, 0xffffff, v.title);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, px(30));
        for (size_t i = 0; i < v.rows.size() && i < 4; i++) {
            lv_obj_t* r = rect(root_, 30, 58 + (int)i * 34, 180, 30, kButton, 8);
            if (!v.rows[i].enabled) lv_obj_set_style_bg_opa(r, LV_OPA_40, 0);
            lv_obj_t* l = label(r, f_.m14, v.rows[i].enabled ? 0xffffff : 0x8f8f8f, v.rows[i].label);
            lv_obj_align(l, LV_ALIGN_LEFT_MID, px(12), 0);
            if (!v.rows[i].value.empty()) {
                lv_obj_t* val = label(r, f_.m12, 0xb4b4b4, v.rows[i].value);
                lv_obj_align(val, LV_ALIGN_RIGHT_MID, px(-10), 0);
            }
        }
        lv_obj_t* back = rect(root_, 76, 196, 88, 28, kButton, 14);
        lv_obj_t* bl = label(back, f_.m13, 0xffffff, "Back");
        lv_obj_center(bl);
        break;
    }
    case Screen::Lines: {
        lv_obj_t* t = label(root_, f_.b17, 0xffffff, v.title);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, px(30));
        for (size_t i = 0; i < v.lines.size() && i < 7; i++) {
            lv_obj_t* l = label(root_, f_.m13, 0xd6d6d6, v.lines[i]);
            lv_obj_align(l, LV_ALIGN_TOP_MID, 0, px(58 + (int)i * 19));
        }
        lv_obj_t* back = rect(root_, 76, 196, 88, 28, kButton, 14);
        lv_obj_t* bl = label(back, f_.m13, 0xffffff, "Back");
        lv_obj_center(bl);
        break;
    }
    case Screen::Message: {
        lv_obj_t* l = label(root_, f_.b17, 0xffffff, v.lines.empty() ? "" : v.lines[0]);
        lv_obj_set_width(l, px(180));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_center(l);
        break;
    }
    case Screen::Confirm: {
        lv_obj_t* q = label(root_, f_.b17, 0xffffff, v.lines.empty() ? "" : v.lines[0]);
        lv_obj_align(q, LV_ALIGN_TOP_MID, 0, px(88));
        lv_obj_t* yes = rect(root_, 38, 130, 80, 48, kGreen, 10);
        lv_obj_t* yl = label(yes, f_.b17, 0x000000, "Yes");
        lv_obj_center(yl);
        lv_obj_t* no = rect(root_, 122, 130, 80, 48, kButton, 10);
        lv_obj_t* nl = label(no, f_.b17, 0xffffff, "No");
        lv_obj_center(nl);
        break;
    }
    }
}

void Ui::show(const View& v) {
    // Rebuild when anything shown changes; menu pages are small and static.
    std::string k = std::to_string((int)v.screen) + "|" + v.title;
    for (auto& r : v.rows) k += "|" + r.label + "=" + r.value + (r.enabled ? "" : "-");
    for (auto& l : v.lines) k += "|" + l;
    if (k != key_) {
        build(v);
        key_ = k;
    }
    if (v.screen == Screen::Closed) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

} // namespace menu
