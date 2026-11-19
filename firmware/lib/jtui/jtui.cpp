#include "lvset.h"
#include "jtui.h"
#include "buttons.h"
#include "scale.h"

using ui::px;

int ui::gScreen = ui::kDesign;

namespace jt {

namespace {

const uint32_t kGreen = 0x3ddc84, kAmber = 0xffb020, kPurple = 0xd9a0ff, kBlue = 0x6fb6ff, kGrey = 0x8f8f8f;
const uint32_t kRed = 0xff6a5e, kTrack = 0x2b2b2b, kTextSoft = 0xd6d6d6, kHint = 0xb4b4b4, kHintDim = 0x5a5a5a;

uint32_t toneColor(Tone t) {
    switch (t) {
    case Tone::Jam: return kGreen;
    case Tone::Lineup: return kAmber;
    case Tone::Timeout: return kPurple;
    case Tone::Intermission: return kBlue;
    case Tone::Idle: return kGrey;
    }
    return kGrey;
}

lv_obj_t* box(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* f, uint32_t color, int y) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, "");
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, px(y));
    return l;
}

// A ring: arc of the given outer diameter and stroke, starting at 12 o'clock.
lv_obj_t* ring(lv_obj_t* parent, int diameter, int width, uint32_t track) {
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_set_size(a, px(diameter), px(diameter));
    lv_obj_center(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_arc_set_rotation(a, 270);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, 0);
    lv_obj_set_style_arc_width(a, px(width), LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, px(width), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(track), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, track ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(a, 0, 0);
    return a;
}

} // namespace

void Ui::setText(lv_obj_t* l, const std::string& s) {
    if (s != lv_label_get_text(l)) lv_label_set_text(l, s.c_str());
}

Ui::Ui(lv_obj_t* parent, const Fonts& f) : f_(f) {
    root_ = box(parent);
    lv_obj_set_size(root_, px(240), px(240));
    lv_obj_center(root_);
    lv_obj_set_style_radius(root_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(root_, true, 0);
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    buildMain();
    buildPicker();
    buildNoConn();
    buildRejoin();
}

void Ui::buildMain() {
    main_ = box(root_);
    lv_obj_set_size(main_, px(240), px(240));
    // Ring: radius 113 to the middle of the 8 px stroke.
    ring_ = ring(main_, 234, 8, kTrack);
    // Thin inner ring: hold progress, "sent..." pulse, "not taken" flash.
    inner_ = ring(main_, 210, 3, 0);
    lv_obj_set_style_arc_rounded(inner_, false, LV_PART_INDICATOR);

    lv_obj_t* status = box(main_);
    lv_obj_set_size(status, px(120), px(18));
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, px(28));
    statusDot_ = box(status);
    lv_obj_set_size(statusDot_, px(8), px(8));
    lv_obj_set_style_radius(statusDot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(statusDot_, lv_color_hex(kGreen), 0);
    lv_obj_set_style_bg_opa(statusDot_, LV_OPA_COVER, 0);
    lv_obj_align(statusDot_, LV_ALIGN_CENTER, px(-18), 0);
    statusText_ = label(status, f_.b12, 0xffffff, 0);
    lv_label_set_text(statusText_, "CRG");
    lv_obj_align(statusText_, LV_ALIGN_CENTER, px(6), 0);
    statusPill_ = box(status);
    lv_obj_set_size(statusPill_, px(92), px(18));
    lv_obj_center(statusPill_);
    lv_obj_set_style_radius(statusPill_, px(9), 0);
    lv_obj_set_style_bg_color(statusPill_, lv_color_hex(kBlue), 0);
    lv_obj_set_style_bg_opa(statusPill_, LV_OPA_COVER, 0);
    pillText_ = label(statusPill_, f_.b12, 0x0b1a2a, 0);
    lv_label_set_text(pillText_, "STANDALONE");
    lv_obj_center(pillText_);

    context_ = label(main_, f_.m14, kTextSoft, 50);
    // Clock fonts hold only digits, so their line height is the digit
    // height: place clocks by their centre, not their top.
    clock_ = label(main_, f_.clock72, 0xffffff, 0);
    lv_obj_align(clock_, LV_ALIGN_CENTER, 0, px(-14));
    state_ = label(main_, f_.b17, kGreen, 146);
    lv_obj_set_style_text_letter_space(state_, px(2), 0);
    period_ = label(main_, f_.m14, kTextSoft, 170);
    hint_ = label(main_, f_.m12, kHint, 192);
}

void Ui::buildPicker() {
    picker_ = box(root_);
    lv_obj_set_size(picker_, px(240), px(240));
    static const uint32_t fill[4] = {0x17406e, 0x0f2c4d, 0x7a4310, 0x552e0a};
    static const char* const what[4] = {"TTO", "OR", "TTO", "OR"};
    for (int q = 0; q < 4; q++) {
        bool right = q >= 2, bottom = q % 2 == 1;
        lv_obj_t* r = box(picker_);
        lv_obj_set_size(r, px(118), px(118));
        lv_obj_set_pos(r, px(right ? 122 : 0), px(bottom ? 122 : 0));
        lv_obj_set_style_bg_color(r, lv_color_hex(fill[q]), 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        // Text slightly towards the centre: the outer corners are off the round screen.
        int dx = right ? -4 : 4, dy = bottom ? -2 : 2;
        lv_obj_t* t = label(r, f_.m12, 0xffffff, 0);
        lv_label_set_text(t, right ? "TEAM 2" : "TEAM 1");
        lv_obj_align(t, LV_ALIGN_CENTER, px(dx), px(dy - 10));
        lv_obj_t* w = label(r, f_.b17, 0xffffff, 0);
        lv_label_set_text(w, what[q]);
        lv_obj_align(w, LV_ALIGN_CENTER, px(dx), px(dy + 8));
    }
    lv_obj_t* disc = box(picker_);
    lv_obj_set_size(disc, px(90), px(90));
    lv_obj_center(disc);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(0xe9e9e9), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    pickClock_ = label(disc, f_.clock27, 0x111111, 0);
    lv_obj_align(pickClock_, LV_ALIGN_CENTER, 0, px(-10));
    lv_obj_t* oto = label(disc, f_.b17, 0x111111, 0);
    lv_label_set_text(oto, "OTO");
    lv_obj_align(oto, LV_ALIGN_CENTER, 0, px(16));
}

void Ui::buildNoConn() {
    noconn_ = box(root_);
    lv_obj_set_size(noconn_, px(240), px(240));
    // Dashed red ring: 24 short arcs.
    for (int i = 0; i < 24; i++) {
        lv_obj_t* a = ring(noconn_, 234, 6, 0);
        lv_arc_set_bg_angles(a, 0, 0);
        lv_arc_set_rotation(a, i * 15);
        lv_arc_set_angles(a, 0, 8);
        lv_obj_set_style_arc_color(a, lv_color_hex(kRed), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
        dashes_[i] = a;
    }
    lv_obj_t* t = label(noconn_, f_.b27, kRed, 44);
    lv_label_set_text(t, "NO CONN");
    lv_obj_t* s = label(noconn_, f_.m12, 0xffffff, 76);
    lv_label_set_text(s, "Buttons are not sent");
    lastClock_ = label(noconn_, f_.clock43, kGrey, 0);
    lv_obj_align(lastClock_, LV_ALIGN_CENTER, 0, px(2));
    lastWhat_ = label(noconn_, f_.m12, kGrey, 146);
    noconnHint_ = label(noconn_, f_.m12, 0xffffff, 176);
    lv_label_set_text(noconnHint_, ("Hold " + btn::left("Undo") + ": run locally").c_str());
    // hold progress on the inner ring
}

// Same layout as the box timer's prompts (jt::Controller::promptHit).
void Ui::buildRejoin() {
    rejoin_ = box(root_);
    lv_obj_set_size(rejoin_, px(240), px(240));
    lv_obj_t* q = label(rejoin_, f_.b17, 0xffffff, 66);
    lv_label_set_text(q, "Rejoin scoreboard?");
    lv_obj_t* s = label(rejoin_, f_.m12, kTextSoft, 94);
    lv_label_set_text(s, "Local clocks stop,\nthe scoreboard's show");
    const struct {
        int x;
        uint32_t bg, fg;
        const char* t;
    } b[2] = {{38, kGreen, 0x000000, "Yes"}, {122, 0x2f2f33, 0xffffff, "No"}};
    for (auto& i : b) {
        lv_obj_t* o = box(rejoin_);
        lv_obj_set_pos(o, px(i.x), px(130));
        lv_obj_set_size(o, px(80), px(48));
        lv_obj_set_style_radius(o, px(10), 0);
        lv_obj_set_style_bg_color(o, lv_color_hex(i.bg), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_t* l = label(o, f_.b17, i.fg, 0);
        lv_label_set_text(l, i.t);
        lv_obj_center(l);
    }
    lv_obj_add_flag(rejoin_, LV_OBJ_FLAG_HIDDEN);
}

void Ui::show(const View& v, uint32_t now) {
    auto vis = [](lv_obj_t* o, bool on) {
        if (on == lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
            if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
    };
    vis(main_, v.screen == Screen::Main);
    vis(picker_, v.screen == Screen::Picker);
    vis(noconn_, v.screen == Screen::NoConn);
    vis(rejoin_, v.screen == Screen::Rejoin);
    if (v.screen == Screen::Rejoin) return;

    if (v.screen == Screen::Picker) {
        setText(pickClock_, v.pickerClock);
        return;
    }
    if (v.screen == Screen::NoConn) {
        setText(lastClock_, v.lastKnownClock);
        setText(lastWhat_, v.lastKnownWhat);
        // The hold reuses the hint line: show progress as text brightness.
        ui::textColor(noconnHint_, lv_color_hex(v.holdPermille ? kAmber : 0xffffff));
        return;
    }

    bool connected = v.mode == Mode::Connected;
    vis(statusDot_, connected);
    vis(statusText_, connected);
    vis(statusPill_, !connected);
    if (!connected) {
        setText(pillText_, v.scoreboardBack ? "CRG BACK" : "STANDALONE");
    }
    if (connected) {
        // CRG's global eJT settings differ: say so, never change them.
        setText(statusText_, v.settingsWarning ? "CRG \xc2\xb7 eJT settings?" : "CRG");
        uint32_t c = v.settingsWarning ? kAmber : kGreen;
        ui::textColor(statusText_, lv_color_hex(v.settingsWarning ? kAmber : 0xffffff));
        ui::bgColor(statusDot_, lv_color_hex(c));
        lv_obj_update_layout(statusText_);
        lv_obj_align_to(statusDot_, statusText_, LV_ALIGN_OUT_LEFT_MID, px(-6), 0);
    }

    uint32_t tone = toneColor(v.tone);
    setText(context_, v.context);
    setText(clock_, v.clock);
    setText(period_, v.period);

    std::string stateText = v.state, hintText = v.hint;
    uint32_t stateColor = tone;
    if (v.feedback == Feedback::NotTaken) {
        stateText = "NOT TAKEN";
        hintText = "press again";
        stateColor = kRed;
    } else if (v.feedback == Feedback::NotAllowed) {
        stateText = "NOT ALLOWED";
        hintText = "no write access on the scoreboard";
        stateColor = kRed;
    } else if (v.feedback == Feedback::Waiting) {
        hintText = "sent\xe2\x80\xa6";
    }
    setText(state_, stateText);
    ui::textColor(state_, lv_color_hex(stateColor));
    setText(hint_, hintText);
    ui::textColor(hint_, lv_color_hex(v.hintDim ? kHintDim : kHint));

    // The red flash also takes over the outer ring (set once, not tone then red).
    lv_arc_set_value(ring_, v.feedbackRed ? 1000 : v.ringPermille);
    ui::arcColor(ring_, lv_color_hex(v.feedbackRed ? kRed : tone));
    ui::arcOpa(ring_, v.ringOn ? LV_OPA_COVER : LV_OPA_TRANSP);

    // Inner ring: hold progress, pulsing while waiting, red flashes.
    if (v.holdPermille > 0) {
        lv_arc_set_value(inner_, v.holdPermille);
        ui::arcColor(inner_, v.holdStage ? lv_color_hex(kBlue) : lv_color_white());
        ui::arcOpa(inner_, LV_OPA_COVER);
    } else if (v.feedback == Feedback::Waiting) {
        lv_arc_set_value(inner_, 1000);
        ui::arcColor(inner_, lv_color_white());
        uint32_t ph = now % 600;
        uint32_t opa = ph < 300 ? 60 + ph * 195 / 300 : 255 - (ph - 300) * 195 / 300;
        ui::arcOpa(inner_, (lv_opa_t)opa);
    } else if (v.feedbackRed) {
        lv_arc_set_value(inner_, 1000);
        ui::arcColor(inner_, lv_color_hex(kRed));
        ui::arcOpa(inner_, LV_OPA_COVER);
    } else {
        ui::arcOpa(inner_, LV_OPA_TRANSP);
    }
}

} // namespace jt
