// Style setters that only touch the object when the value changes. LVGL
// redraws the whole object on every lv_obj_set_style_*, also with the value
// it already has; the screens set their colours on every update, and the
// rings are as big as the display, so the whole screen was redrawn all the
// time. On the StopWatch a full redraw takes about 300 ms and showed as a
// ring filling in the wrong order (Rogier, 2026-11-19). Use these in update
// paths; building a screen can use the plain calls.
#pragma once
#include "lvgl.h"

namespace ui {

inline void textColor(lv_obj_t* o, lv_color_t c, lv_part_t part = LV_PART_MAIN) {
    if (!lv_color_eq(lv_obj_get_style_text_color(o, part), c)) lv_obj_set_style_text_color(o, c, part);
}
inline void bgColor(lv_obj_t* o, lv_color_t c, lv_part_t part = LV_PART_MAIN) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(o, part), c)) lv_obj_set_style_bg_color(o, c, part);
}
inline void bgOpa(lv_obj_t* o, lv_opa_t v, lv_part_t part = LV_PART_MAIN) {
    if (lv_obj_get_style_bg_opa(o, part) != v) lv_obj_set_style_bg_opa(o, v, part);
}
inline void arcColor(lv_obj_t* o, lv_color_t c, lv_part_t part = LV_PART_INDICATOR) {
    if (!lv_color_eq(lv_obj_get_style_arc_color(o, part), c)) lv_obj_set_style_arc_color(o, c, part);
}
inline void arcOpa(lv_obj_t* o, lv_opa_t v, lv_part_t part = LV_PART_INDICATOR) {
    if (lv_obj_get_style_arc_opa(o, part) != v) lv_obj_set_style_arc_opa(o, v, part);
}

// Hidden flag: LVGL redraws the object on every add/remove, also when the
// flag is already as asked.
inline void visible(lv_obj_t* o, bool on) {
    if (on == lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace ui
