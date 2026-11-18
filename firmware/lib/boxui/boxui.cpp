#include "boxui.h"
#include "scale.h"

using ui::px;

namespace box {

namespace {

// CRG's PBT colours (docs/display.md)
const uint32_t kServing = 0xee3333, kStand = 0xffff00, kDone = 0x6fdf6f, kUnserved = 0x00eeee;
const uint32_t kWaiting = 0x7a2222, kPaused = 0x55555a, kEmpty = 0x1c1c1f, kButton = 0x2f2f33;
const uint32_t kRed = 0xff6a5e, kTrack = 0x2b2b2b, kGreenDot = 0x3ddc84, kBlue = 0x6fb6ff;

uint32_t fillOf(Phase p) {
    switch (p) {
    case Phase::Serving: return kServing;
    case Phase::Stand: return kStand;
    case Phase::Done: return kDone;
    case Phase::Waiting: return kWaiting;
    case Phase::Paused: return kPaused;
    }
    return kServing;
}
uint32_t textOn(Phase p) { return p == Phase::Stand || p == Phase::Done ? 0x000000 : 0xffffff; }
const char* wordOf(const Slot& s) {
    if (s.swapDone) return "DONE \xc2\xb7 swap";
    switch (s.phase) {
    case Phase::Serving: return "SERVING";
    case Phase::Stand: return "STAND";
    case Phase::Done: return "DONE";
    case Phase::Waiting: return "WAITING FOR JAM";
    case Phase::Paused: return "PAUSED";
    }
    return "";
}

lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, bool filled = true) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, px(x), px(y));
    lv_obj_set_size(o, px(w), px(h));
    if (filled) {
        lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    return o;
}

void setText(lv_obj_t* l, const std::string& s) {
    if (l && s != lv_label_get_text(l)) lv_label_set_text(l, s.c_str());
}

lv_obj_t* arc(lv_obj_t* parent, int d, int w) {
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_set_size(a, px(d), px(d));
    lv_obj_center(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_arc_set_rotation(a, 270);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_range(a, 0, 1000);
    lv_obj_set_style_arc_width(a, px(w), LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, px(w), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(kTrack), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(a, 0, 0);
    return a;
}

} // namespace

lv_obj_t* Ui::label(lv_obj_t* parent, const lv_font_t* f, uint32_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, "");
    return l;
}

Ui::Ui(lv_obj_t* parent, const jt::Fonts& f) : f_(f) {
    root_ = box(parent, 0, 0, 240, 240, 0x000000);
    lv_obj_center(root_);
    lv_obj_set_style_radius(root_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(root_, true, 0);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
}

std::string Ui::layoutKey(const View& v) const {
    std::string k = std::to_string((int)v.screen) + "/" + std::to_string((int)v.role);
    if (v.screen == Screen::Main && v.role == Role::Blockers) k += "/" + std::to_string(v.slots.size() > 3 ? 3 : v.slots.size());
    if (v.screen == Screen::NoConn) k += v.role == Role::Jammers ? "/j" : "/b";
    return k;
}

void Ui::build(const View& v) {
    lv_obj_clean(root_);
    for (auto& b : bands_) b = Band();
    status_ = hint_ = ring_ = hold_ = statusDot_ = pickTitle_ = nullptr;
    menuNumber_ = menuClock_ = menuPenalty_ = menuChange_ = nullptr;
    for (int i = 0; i < 4; i++) pick_[i] = pickLabel_[i] = pickNumber_[i] = nullptr;
    emptyText_[0] = emptyText_[1] = nullptr;
    switch (v.screen) {
    case Screen::Main:
        if (v.role == Role::Jammers) buildJammers();
        else buildBlockers(v.slots.size() > 3 ? 3 : (int)v.slots.size());
        break;
    case Screen::Picker: buildPicker(); break;
    case Screen::Menu: buildMenu(); break;
    case Screen::NoConn: buildNoConn(v); break;
    case Screen::SwapPrompt:
    case Screen::RejoinPrompt: {
        bool rj = v.screen == Screen::RejoinPrompt;
        lv_obj_t* q = label(root_, f_.b17, 0xffffff);
        lv_label_set_text(q, rj ? "Rejoin scoreboard?" : "Swap sides?");
        lv_obj_align(q, LV_ALIGN_TOP_MID, 0, px(rj ? 66 : 70));
        lv_obj_t* s = label(root_, f_.m12, 0xd6d6d6);
        lv_label_set_text(s, rj                         ? "Local clocks stop,\nthe scoreboard's show"
                             : v.role == Role::Jammers ? "Teams change ends"
                                                       : "Time the other team");
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s, LV_ALIGN_TOP_MID, 0, px(rj ? 92 : 98));
        lv_obj_t* yes = box(root_, 38, 130, 80, 48, kDone);
        lv_obj_set_style_radius(yes, px(10), 0);
        lv_obj_t* yl = label(yes, f_.b17, 0x000000);
        lv_label_set_text(yl, "Yes");
        lv_obj_center(yl);
        lv_obj_t* no = box(root_, 122, 130, 80, 48, kButton);
        lv_obj_set_style_radius(no, px(10), 0);
        lv_obj_t* nl = label(no, f_.b17, 0xffffff);
        lv_label_set_text(nl, "No");
        lv_obj_center(nl);
        break;
    }
    }
    // Notice pill over the middle, on every screen.
    notice_ = label(root_, f_.m13, 0xffffff);
    lv_obj_set_style_bg_color(notice_, lv_color_hex(0x202024), 0);
    lv_obj_set_style_bg_opa(notice_, LV_OPA_90, 0);
    lv_obj_set_style_pad_hor(notice_, px(10), 0);
    lv_obj_set_style_pad_ver(notice_, px(6), 0);
    lv_obj_set_style_radius(notice_, px(10), 0);
    lv_obj_set_style_border_color(notice_, lv_color_hex(0x6fb6ff), 0);
    lv_obj_set_style_border_width(notice_, px(1), 0);
    lv_obj_center(notice_);
    lv_obj_add_flag(notice_, LV_OBJ_FLAG_HIDDEN);
    // Hold progress: thin ring at the edge, on every screen.
    hold_ = arc(root_, 236, 3);
    lv_obj_set_style_arc_opa(hold_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(hold_, lv_color_white(), LV_PART_INDICATOR);
}

void Ui::buildBlockers(int n) {
    if (n <= 1) {
        // Jam-timer layout: status, roster number, clock, state word, hint; ring.
        ring_ = arc(root_, 234, 8);
        status_ = label(root_, f_.m12, 0xd6d6d6);
        lv_obj_align(status_, LV_ALIGN_TOP_MID, 0, px(30));
        Band& b = bands_[0];
        b.number = label(root_, f_.b29, 0xffffff);
        lv_obj_align(b.number, LV_ALIGN_TOP_MID, 0, px(48));
        b.clock = label(root_, f_.clock72, 0xffffff);
        lv_obj_align(b.clock, LV_ALIGN_CENTER, 0, px(4));
        b.state = label(root_, f_.b17, 0xffffff);
        lv_obj_set_style_text_letter_space(b.state, px(2), 0);
        lv_obj_align(b.state, LV_ALIGN_TOP_MID, 0, px(158));
        hint_ = label(root_, f_.m12, 0xb4b4b4);
        lv_obj_align(hint_, LV_ALIGN_TOP_MID, 0, px(182));
        return;
    }
    if (n == 2) {
        // Two halves of 107 px, 27 px status strip between them.
        const int ys[2] = {0, 134};
        for (int i = 0; i < 2; i++) {
            Band& b = bands_[i];
            b.fill = box(root_, 0, ys[i], 240, 107 - (i ? 1 : 0), kServing);
            b.number = label(b.fill, f_.b29, 0xffffff);
            b.clock = label(b.fill, f_.clock51, 0xffffff);
            b.state = label(b.fill, f_.b12, 0xffffff);
            // keep the text away from the curved edge: towards the strip
            int base = i == 0 ? 40 : 6;
            lv_obj_align(b.number, LV_ALIGN_TOP_MID, px(-72), px(base + 14));
            lv_obj_align(b.clock, LV_ALIGN_TOP_MID, px(34), px(base + 6));
            lv_obj_align(b.state, LV_ALIGN_TOP_MID, px(34), px(base + 50));
        }
    } else {
        // Three bands 75 / 85 / 75 with 3 px gaps.
        const int ys[3] = {0, 78, 166}, hs[3] = {75, 85, 74};
        for (int i = 0; i < 3; i++) {
            Band& b = bands_[i];
            bool small = i != 1;
            b.fill = box(root_, 0, ys[i], 240, hs[i], kServing);
            b.number = label(b.fill, small ? f_.b23 : f_.b29, 0xffffff);
            b.clock = label(b.fill, small ? f_.clock36 : f_.clock51, 0xffffff);
            if (!small) b.state = label(b.fill, f_.b12, 0xffffff);
            if (i == 0) {
                lv_obj_align(b.number, LV_ALIGN_BOTTOM_MID, px(-46), px(-12));
                lv_obj_align(b.clock, LV_ALIGN_BOTTOM_MID, px(26), px(-12));
            } else if (i == 2) {
                lv_obj_align(b.number, LV_ALIGN_TOP_MID, px(-46), px(12));
                lv_obj_align(b.clock, LV_ALIGN_TOP_MID, px(26), px(12));
            } else {
                lv_obj_align(b.number, LV_ALIGN_LEFT_MID, px(22), px(-4));
                lv_obj_align(b.clock, LV_ALIGN_CENTER, px(30), px(-8));
                lv_obj_align(b.state, LV_ALIGN_CENTER, px(30), px(26));
            }
        }
    }
    if (n == 2) {
        statusDot_ = box(root_, 0, 0, 8, 8, kGreenDot);
        lv_obj_set_style_radius(statusDot_, LV_RADIUS_CIRCLE, 0);
        status_ = label(root_, f_.m12, 0xffffff);
        lv_obj_align(status_, LV_ALIGN_CENTER, px(6), 0);
        lv_obj_align_to(statusDot_, status_, LV_ALIGN_OUT_LEFT_MID, px(-5), 0);
    }
}

void Ui::buildJammers() {
    const uint32_t chip[2] = {0x17406e, 0x7a4310};
    for (int i = 0; i < 2; i++) {
        Band& b = bands_[i];
        b.fill = box(root_, i ? 122 : 0, 0, 118, 240, kEmpty);
        int cx = i ? -8 : 8; // towards the centre line, away from the curved edge
        b.team = label(b.fill, f_.b12, 0xffffff);
        lv_obj_set_style_bg_color(b.team, lv_color_hex(chip[i]), 0);
        lv_obj_set_style_bg_opa(b.team, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(b.team, px(6), 0);
        lv_obj_set_style_pad_ver(b.team, px(2), 0);
        lv_obj_set_style_radius(b.team, px(4), 0);
        lv_obj_align(b.team, LV_ALIGN_TOP_MID, px(cx), px(46));
        b.number = label(b.fill, f_.b23, 0xffffff);
        lv_obj_align(b.number, LV_ALIGN_TOP_MID, px(cx), px(74));
        b.clock = label(b.fill, f_.clock43, 0xffffff);
        lv_obj_align(b.clock, LV_ALIGN_CENTER, px(cx), px(12));
        b.state = label(b.fill, f_.b12, 0xffffff);
        lv_obj_set_width(b.state, px(96));
        lv_label_set_long_mode(b.state, LV_LABEL_LONG_WRAP);
        lv_obj_align(b.state, LV_ALIGN_TOP_MID, px(cx), px(150));
        emptyText_[i] = label(b.fill, f_.m12, 0x9a9a9a);
        lv_obj_set_width(emptyText_[i], px(84));
        lv_label_set_long_mode(emptyText_[i], LV_LABEL_LONG_WRAP);
        lv_obj_align(emptyText_[i], LV_ALIGN_CENTER, px(cx), px(10));
    }
    // Status and hint sit over both halves: give them a dark backing.
    status_ = label(root_, f_.m12, 0xd6d6d6);
    hint_ = label(root_, f_.m12, 0xb4b4b4);
    for (lv_obj_t* l : {status_, hint_}) {
        lv_obj_set_style_bg_color(l, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_70, 0);
        lv_obj_set_style_pad_hor(l, px(6), 0);
        lv_obj_set_style_pad_ver(l, px(1), 0);
        lv_obj_set_style_radius(l, px(4), 0);
    }
    lv_obj_align(status_, LV_ALIGN_TOP_MID, 0, px(22));
    lv_obj_align(hint_, LV_ALIGN_TOP_MID, 0, px(202));
}

void Ui::buildPicker() {
    pickTitle_ = label(root_, f_.m13, 0xffffff);
    lv_obj_align(pickTitle_, LV_ALIGN_TOP_MID, 0, px(44));
    const int xs[2] = {42, 123}, ys[2] = {68, 123};
    for (int i = 0; i < 4; i++) {
        pick_[i] = box(root_, xs[i % 2], ys[i / 2], 75, 49, kButton);
        lv_obj_set_style_radius(pick_[i], px(8), 0);
        pickLabel_[i] = label(pick_[i], f_.m12, 0xd6d6d6);
        lv_obj_align(pickLabel_[i], LV_ALIGN_TOP_MID, 0, px(5));
        pickNumber_[i] = label(pick_[i], f_.b20, 0xffffff);
        lv_obj_align(pickNumber_[i], LV_ALIGN_BOTTOM_MID, 0, px(-4));
    }
    lv_obj_t* later = box(root_, 60, 182, 120, 28, kButton);
    lv_obj_set_style_radius(later, px(14), 0);
    lv_obj_t* t = label(later, f_.m12, 0xffffff);
    lv_label_set_text(t, "Decide later");
    lv_obj_center(t);
}

void Ui::buildMenu() {
    menuNumber_ = label(root_, f_.b23, 0xffffff);
    lv_obj_align(menuNumber_, LV_ALIGN_TOP_MID, px(-40), px(48));
    menuClock_ = label(root_, f_.clock36, 0xffffff);
    lv_obj_align(menuClock_, LV_ALIGN_TOP_MID, px(36), px(44));
    struct B {
        int x, y, w, h;
        uint32_t fill, text;
        const char* t;
    } bs[4] = {{40, 92, 160, 38, kDone, 0x000000, "Release"},
               {40, 134, 160, 38, kUnserved, 0x000000, "+ Penalty (30 s)"},
               {52, 176, 67, 30, kButton, 0xffffff, "Change"},
               {121, 176, 67, 30, kButton, 0xffffff, "Cancel"}};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* o = box(root_, bs[i].x, bs[i].y, bs[i].w, bs[i].h, bs[i].fill);
        lv_obj_set_style_radius(o, px(8), 0);
        lv_obj_t* t = label(o, i < 2 ? f_.b17 : f_.m13, bs[i].text);
        lv_label_set_text(t, bs[i].t);
        lv_obj_center(t);
        if (i == 1) menuPenalty_ = o;
        if (i == 2) menuChange_ = o;
    }
}

void Ui::buildNoConn(const View& v) {
    for (int i = 0; i < 24; i++) {
        lv_obj_t* a = arc(root_, 234, 6);
        lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_arc_set_rotation(a, i * 15);
        lv_arc_set_bg_angles(a, 0, 0);
        lv_arc_set_angles(a, 0, 8);
        lv_obj_set_style_arc_color(a, lv_color_hex(kRed), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    }
    lv_obj_t* t = label(root_, f_.b27, kRed);
    lv_label_set_text(t, "NO CONN");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, px(62));
    lv_obj_t* s = label(root_, f_.m12, 0xffffff);
    lv_label_set_text(s, "Buttons are not sent");
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, px(98));
    lv_obj_t* h = label(root_, f_.m12, 0xffffff);
    lv_label_set_text(h, v.role == Role::Jammers ? "Hold crown: run locally" : "Hold 10 o'clock: run locally");
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, px(150));
}

void Ui::updateBand(Band& b, const Slot& s, bool small, const View& v) {
    (void)small;
    bool empty = s.key.empty();
    uint32_t fill = empty ? kEmpty : fillOf(s.phase);
    uint32_t text = empty ? 0xffffff : textOn(s.phase);
    if (b.fill) lv_obj_set_style_bg_color(b.fill, lv_color_hex(fill), 0);
    auto col = [&](lv_obj_t* l) {
        if (l) lv_obj_set_style_text_color(l, lv_color_hex(text), 0);
    };
    col(b.number);
    col(b.clock);
    col(b.state);
    if (v.role == Role::Jammers) {
        int side = &b == &bands_[0] ? 0 : 1;
        setText(b.team, side == 0 ? v.teamLeft : v.teamRight);
        if (emptyText_[side]) {
            setText(emptyText_[side], side == 0 ? v.emptyLeft : v.emptyRight);
            if (empty) lv_obj_remove_flag(emptyText_[side], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(emptyText_[side], LV_OBJ_FLAG_HIDDEN);
        }
    }
    setText(b.number, empty ? "" : (s.number.empty() ? "" : "#" + s.number));
    setText(b.clock, empty ? "" : s.clock);
    std::string w = empty ? "" : wordOf(s);
    if (v.role == Role::Jammers && s.swapDone) w = "DONE \xc2\xb7 swap\ntap to release";
    setText(b.state, w);
}

void Ui::show(const View& v, uint32_t now) {
    (void)now;
    std::string k = layoutKey(v);
    if (k != key_) {
        build(v);
        key_ = k;
    }
    // Status line with press feedback
    std::string status = v.status;
    uint32_t statusColor = v.paused ? kBlue : 0xd6d6d6;
    if (v.feedback == Feedback::NotTaken) {
        status = "NOT TAKEN - press again";
        statusColor = kRed;
    } else if (v.feedback == Feedback::NotAllowed) {
        status = "NOT ALLOWED (no write access)";
        statusColor = kRed;
    } else if (v.feedback == Feedback::Waiting) {
        status = "sent\xe2\x80\xa6";
    }
    if (status_) {
        setText(status_, status);
        lv_obj_set_style_text_color(status_, lv_color_hex(statusColor), 0);
    }
    if (statusDot_) {
        lv_obj_set_style_bg_color(statusDot_, lv_color_hex(v.mode == Mode::Connected ? kGreenDot : kBlue), 0);
        lv_obj_update_layout(status_);
        lv_obj_align_to(statusDot_, status_, LV_ALIGN_OUT_LEFT_MID, px(-5), 0);
    }
    setText(hint_, v.hint);
    if (notice_) {
        setText(notice_, v.notice);
        if (v.notice.empty()) lv_obj_add_flag(notice_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(notice_, LV_OBJ_FLAG_HIDDEN);
    }
    if (hold_) {
        lv_arc_set_value(hold_, v.holdPermille);
        lv_obj_set_style_arc_color(hold_, lv_color_hex(v.holdStage ? kBlue : 0xffffff), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(hold_, v.holdPermille ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
    }

    switch (v.screen) {
    case Screen::Main:
        if (v.role == Role::Jammers) {
            for (int i = 0; i < 2 && i < (int)v.slots.size(); i++) updateBand(bands_[i], v.slots[i], false, v);
        } else if (v.slots.empty()) {
            Band& b = bands_[0];
            setText(b.number, "");
            setText(b.clock, "");
            setText(b.state, "BOX EMPTY");
            lv_obj_set_style_text_color(b.state, lv_color_hex(0x8f8f8f), 0);
            lv_arc_set_value(ring_, 0);
        } else {
            int n = v.slots.size() > 3 ? 3 : (int)v.slots.size();
            for (int i = 0; i < n; i++) updateBand(bands_[i], v.slots[i], n == 3 && i != 1, v);
            if (n == 1) {
                // single skater: text on black, the ring carries the state colour
                const Slot& s = v.slots[0];
                uint32_t c = fillOf(s.phase);
                lv_obj_set_style_text_color(bands_[0].number, lv_color_white(), 0);
                lv_obj_set_style_text_color(bands_[0].clock, lv_color_white(), 0);
                lv_obj_set_style_text_color(bands_[0].state, lv_color_hex(c == kWaiting ? 0xee6666 : c), 0);
                lv_arc_set_value(ring_, 1000);
                lv_obj_set_style_arc_color(ring_, lv_color_hex(c), LV_PART_INDICATOR);
            }
        }
        break;
    case Screen::Picker:
        setText(pickTitle_, v.pickTitle);
        for (int i = 0; i < 4; i++) {
            const PickItem& p = v.pick[i];
            lv_obj_set_style_bg_color(pick_[i], lv_color_hex(p.unserved ? kUnserved : kButton), 0);
            lv_obj_set_style_bg_opa(pick_[i], p.inBox ? LV_OPA_40 : LV_OPA_COVER, 0);
            setText(pickLabel_[i], p.label);
            setText(pickNumber_[i], p.number);
            uint32_t tc = p.unserved ? 0x000000 : 0xffffff;
            lv_obj_set_style_text_color(pickLabel_[i], lv_color_hex(p.unserved ? 0x000000 : 0xd6d6d6), 0);
            lv_obj_set_style_text_color(pickNumber_[i], lv_color_hex(tc), 0);
        }
        break;
    case Screen::Menu:
        setText(menuNumber_, v.menuNumber);
        setText(menuClock_, v.menuClock);
        // standalone clocks have no roster number: centre the clock
        lv_obj_align(menuClock_, LV_ALIGN_TOP_MID, px(v.menuNumber.empty() ? 0 : 36), px(44));
        lv_obj_set_style_bg_opa(menuPenalty_, v.menuCanPenalty ? LV_OPA_COVER : LV_OPA_30, 0);
        lv_obj_set_style_bg_opa(menuChange_, v.menuCanChange ? LV_OPA_COVER : LV_OPA_30, 0);
        break;
    case Screen::NoConn:
    case Screen::SwapPrompt:
    case Screen::RejoinPrompt: break;
    }
}

} // namespace box
