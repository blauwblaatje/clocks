#include "lcdui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "buttons.h"
#include "lvset.h"
#include "src/misc/lv_area_private.h" // lv_area_intersect
#include "scale.h"

using ui::px;

namespace lcd {

using namespace kit;

namespace {

const float kSlant = 0.06f; // digits lean right, as on a stopwatch LCD

// Only what the segments can show; anything else becomes a blank digit.
std::string segText(const std::string& s) {
    std::string out;
    for (char ch : s) out += (ch >= '0' && ch <= '9') || ch == ':' || ch == '-' ? ch : ' ';
    return out;
}

LegendStyle lcdLegends(const jt::Fonts& f, const Colours& c) {
    LegendStyle st;
    st.font = f.b12;
    st.text = c.label;
    st.dotBorder = c.lit;
    st.dotBorderWidth = c.tint || c.lit.red > 128 ? 0 : 1;
    st.chipBg = c.bg;
    return st;
}

} // namespace

lv_color_t Colours::ghostOf(lv_color_t c) const { return lv_color_mix(c, bg, tint || lit.red > 128 ? 30 : 20); }
lv_color_t Colours::offOf(lv_color_t c) const { return lv_color_mix(c, bg, lit.red > 128 ? 55 : 40); }

Colours colours(Palette p) {
    Colours c;
    if (p == Palette::Day) {
        c.bg = lv_color_hex(0xb3bda4);
        c.lit = lv_color_hex(0x1a1f14);
        c.label = lv_color_hex(0x3a442f);
        c.rule = lv_color_hex(0x8d977f);
        c.tint = false;
    } else {
        c.bg = lv_color_hex(0x000000);
        c.lit = lv_color_hex(0xffb347);
        c.label = lv_color_hex(0xa39580);
        c.rule = lv_color_hex(0x3a2a14);
        c.tint = p == Palette::NightColour;
    }
    return c;
}

// ---------- digits ----------

namespace {
const char* segsOf(char ch) {
    switch (ch) {
    case '0': return "abcdef";
    case '1': return "bc";
    case '2': return "abdeg";
    case '3': return "abcdg";
    case '4': return "bcfg";
    case '5': return "acdfg";
    case '6': return "acdefg";
    case '7': return "abc";
    case '8': return "abcdefg";
    case '9': return "abcdfg";
    case '-': return "g";
    }
    return "";
}
struct Geo {
    float H, W, t, gap, cw;
    explicit Geo(float h) : H(h), W(h * 0.5f), t(h * 0.13f), gap(h * 0.13f), cw(h * 0.2f) {}
};
} // namespace

int Digits::width(const std::string& s, int h) {
    Geo g((float)h);
    float x = 2;
    for (char ch : s) x += ch == ':' || ch == ';' ? g.cw : g.W + g.gap;
    if (!s.empty() && s.back() != ':' && s.back() != ';') x -= g.gap;
    return (int)ceilf(x + g.H * kSlant + 3);
}

Digits::Digits(lv_obj_t* parent, int height) : h_(px(height)) {
    obj_ = plain(parent);
    lv_obj_set_user_data(obj_, this);
    lv_obj_set_size(obj_, 1, h_);
    lv_obj_add_event_cb(obj_, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
}

void Digits::set(const std::string& s, lv_color_t lit, lv_color_t ghost) {
    std::string t = segText(s);
    for (size_t i = 0; i < s.size() && i < t.size(); i++)
        if (s[i] == ';') t[i] = ';';
    if (!first_ && t == text_ && lv_color_eq(lit, lit_) && lv_color_eq(ghost, ghost_)) return;
    first_ = false;
    bool resize = t.size() != text_.size() || Digits::width(t, h_) != lv_obj_get_width(obj_);
    text_ = t;
    lit_ = lit;
    ghost_ = ghost;
    if (resize) lv_obj_set_width(obj_, Digits::width(t, h_)); // invalidates old and new area
    lv_obj_invalidate(obj_);
}

// Each segment is one thick line with round ends: a pill, the LCD look, one
// primitive per segment (triangles left seams between them).
void Digits::onDraw(lv_event_t* e) {
    lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
    Digits* d = (Digits*)lv_obj_get_user_data(o);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    Geo g((float)d->h_);
    auto sx = [&](float x, float y) { return a.x1 + x + (g.H - y) * kSlant; };
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.width = (int32_t)lroundf(g.t);
    ld.round_start = 1;
    ld.round_end = 1;
    auto seg = [&](float x1, float y1, float x2, float y2, bool on) {
        ld.color = on ? d->lit_ : d->ghost_;
        ld.p1.x = sx(x1, y1);
        ld.p1.y = a.y1 + y1;
        ld.p2.x = sx(x2, y2);
        ld.p2.y = a.y1 + y2;
        lv_draw_line(layer, &ld);
    };
    float x = 2, r = g.t / 2, gp = g.t * 0.55f;
    for (char ch : d->text_) {
        if (ch == ':' || ch == ';') {
            lv_draw_rect_dsc_t rd;
            lv_draw_rect_dsc_init(&rd);
            rd.bg_color = ch == ':' ? d->lit_ : d->ghost_;
            rd.radius = (int32_t)(g.t * 0.2f);
            float s = g.t * 0.95f, cx = x + g.cw / 2;
            for (float cy : {g.H * 0.3f, g.H * 0.7f}) {
                lv_area_t q;
                q.x1 = (int32_t)lroundf(sx(cx - s / 2, cy));
                q.x2 = q.x1 + (int32_t)lroundf(s) - 1;
                q.y1 = a.y1 + (int32_t)lroundf(cy - s / 2);
                q.y2 = q.y1 + (int32_t)lroundf(s) - 1;
                lv_draw_rect(layer, &rd, &q);
            }
            x += g.cw;
            continue;
        }
        const char* on = segsOf(ch);
        auto has = [&](char k) { return strchr(on, k) != nullptr; };
        float xl = x + r, xr = x + g.W - r, yt = r, ym = g.H / 2, yb = g.H - r;
        seg(xl + gp, yt, xr - gp, yt, has('a'));
        seg(xl + gp, ym, xr - gp, ym, has('g'));
        seg(xl + gp, yb, xr - gp, yb, has('d'));
        seg(xl, yt + gp, xl, ym - gp, has('f'));
        seg(xr, yt + gp, xr, ym - gp, has('b'));
        seg(xl, ym + gp, xl, yb - gp, has('e'));
        seg(xr, ym + gp, xr, yb - gp, has('c'));
        x += g.W + g.gap;
    }
}

// ---------- rim ----------

namespace {
const int kTicks = 60;
struct Tick {
    float x1, y1, x2, y2;
};
// A tick as a chord between its two ends at the middle radius; the line's
// width makes it the band. Coordinates relative to the face's centre.
Tick tickAt(int i) {
    float rm = px(111), a0 = (i + 0.18f) * 2 * (float)M_PI / kTicks, a1 = (i + 0.82f) * 2 * (float)M_PI / kTicks;
    return {rm * sinf(a0), -rm * cosf(a0), rm * sinf(a1), -rm * cosf(a1)};
}
} // namespace

Rim::Rim(lv_obj_t* parent) {
    obj_ = plain(parent);
    lv_obj_set_user_data(obj_, this);
    lv_obj_set_size(obj_, px(240), px(240));
    lv_obj_center(obj_);
    lv_obj_add_event_cb(obj_, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
}

void Rim::invalidateTicks(int from, int to) {
    lv_area_t c;
    lv_obj_get_coords(obj_, &c);
    int cx = (c.x1 + c.x2) / 2, cy = (c.y1 + c.y2) / 2, w = px(9) / 2 + 2;
    for (int i = from; i < to; i++) {
        Tick t = tickAt(i);
        lv_area_t a;
        a.x1 = cx + (int32_t)floorf(fminf(t.x1, t.x2)) - w;
        a.x2 = cx + (int32_t)ceilf(fmaxf(t.x1, t.x2)) + w;
        a.y1 = cy + (int32_t)floorf(fminf(t.y1, t.y2)) - w;
        a.y2 = cy + (int32_t)ceilf(fmaxf(t.y1, t.y2)) + w;
        lv_obj_invalidate_area(obj_, &a);
    }
}

// The whole rim as four bands, not the face inside it.
void Rim::invalidateBand() {
    lv_area_t c;
    lv_obj_get_coords(obj_, &c);
    int b = px(17);
    lv_area_t top = {c.x1, c.y1, c.x2, c.y1 + b}, bottom = {c.x1, c.y2 - b, c.x2, c.y2};
    lv_area_t left = {c.x1, c.y1 + b, c.x1 + b, c.y2 - b}, right = {c.x2 - b, c.y1 + b, c.x2, c.y2 - b};
    for (lv_area_t* a : {&top, &bottom, &left, &right}) lv_obj_invalidate_area(obj_, a);
}

void Rim::set(int permille, lv_color_t lit, lv_color_t ghost, bool on) {
    int n = permille <= 0 ? 0 : permille >= 1000 ? kTicks : (permille * kTicks + 500) / 1000;
    bool colours = lit_ < 0 || !lv_color_eq(lit, litC_) || !lv_color_eq(ghost, ghostC_) || on != on_;
    if (colours) {
        litC_ = lit;
        ghostC_ = ghost;
        on_ = on;
        lit_ = n;
        invalidateBand();
    } else if (n != lit_) {
        invalidateTicks(n < lit_ ? n : lit_, n < lit_ ? lit_ : n);
        lit_ = n;
    }
}

void Rim::onDraw(lv_event_t* e) {
    lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
    Rim* r = (Rim*)lv_obj_get_user_data(o);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(o, &c);
    float cx = (c.x1 + c.x2) / 2.0f, cy = (c.y1 + c.y2) / 2.0f;
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.width = px(9);
    for (int i = 0; i < kTicks; i++) {
        Tick t = tickAt(i);
        lv_area_t a;
        int w = ld.width / 2 + 1;
        a.x1 = (int32_t)(cx + fminf(t.x1, t.x2)) - w;
        a.x2 = (int32_t)(cx + fmaxf(t.x1, t.x2)) + w;
        a.y1 = (int32_t)(cy + fminf(t.y1, t.y2)) - w;
        a.y2 = (int32_t)(cy + fmaxf(t.y1, t.y2)) + w;
        lv_area_t clip;
        if (!lv_area_intersect(&clip, &a, &layer->_clip_area)) continue; // only the ticks being redrawn
        ld.color = i < r->lit_ && r->on_ ? r->litC_ : r->ghostC_;
        ld.p1.x = cx + t.x1;
        ld.p1.y = cy + t.y1;
        ld.p2.x = cx + t.x2;
        ld.p2.y = cy + t.y2;
        lv_draw_line(layer, &ld);
    }
}

// ---------- jam timer ----------

JtFace::JtFace(lv_obj_t* parent, const jt::Fonts& f, Palette p) : c_(colours(p)), f_(f) {
    root_ = faceRoot(parent, c_.bg);
    rim_ = new Rim(root_);
    legends_ = new Legends(root_, lcdLegends(f, c_));

    lv_obj_t* wr = row(root_, 6, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(wr, LV_ALIGN_TOP_MID, 0, px(66));
    for (auto& w : words_) w = text(wr, f.b12, c_.offOf(c_.lit));
    lv_label_set_text(words_[0], "LINEUP");
    lv_label_set_text(words_[1], "JAM");
    lv_label_set_text(words_[2], "TIMEOUT");
    lv_label_set_text(words_[3], "");
    single_ = text(root_, f.b12, c_.lit);
    lv_obj_align(single_, LV_ALIGN_TOP_MID, 0, px(66));
    lv_label_set_text(single_, "");

    lv_obj_t* pr = row(root_, 6, LV_FLEX_ALIGN_END);
    lv_obj_align(pr, LV_ALIGN_TOP_MID, 0, px(82));
    lv_obj_t* pc = column(pr, 0);
    lv_obj_set_flex_align(pc, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    pNo_ = text(pc, f.b12, c_.lit);
    pLabel_ = text(pc, f.m12, c_.label);
    lv_label_set_text(pNo_, "");
    lv_label_set_text(pLabel_, "PERIOD");
    period_ = new Digits(pr, 19);

    lv_obj_t* rule = plain(root_);
    lv_obj_set_size(rule, px(140), px(1) > 0 ? px(1) : 1);
    lv_obj_set_style_bg_color(rule, c_.rule, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, px(106));

    main_ = new Digits(root_, 57);
    lv_obj_align(main_->obj(), LV_ALIGN_TOP_MID, 0, px(111));

    hold_ = plain(root_);
    lv_obj_set_size(hold_, 0, px(3));
    lv_obj_set_style_bg_opa(hold_, LV_OPA_COVER, 0);
    lv_obj_align(hold_, LV_ALIGN_TOP_MID, 0, px(173));

    lv_obj_t* jr = row(root_, 5, LV_FLEX_ALIGN_END);
    lv_obj_align(jr, LV_ALIGN_TOP_MID, 0, px(180));
    jamLabel_ = text(jr, f.m12, c_.label);
    lv_label_set_text(jamLabel_, "");
    jamNo_ = new Digits(jr, 14);
}

JtFace::~JtFace() {
    delete rim_;
    delete legends_;
    delete period_;
    delete main_;
    delete jamNo_;
    lv_obj_delete(root_);
}

void JtFace::show(const jt::View& v, uint32_t now) {
    ui::visible(root_, v.screen == jt::Screen::Main);
    if (v.screen != jt::Screen::Main) return;
    uint32_t tone = v.tone == jt::Tone::Jam ? kJam : v.tone == jt::Tone::Lineup ? kLineup
                    : v.tone == jt::Tone::Timeout ? kTimeout : v.tone == jt::Tone::Intermission ? kInter : 0;
    lv_color_t lit = c_.tint && tone ? lv_color_hex(tone) : c_.lit;
    lv_color_t ghost = c_.ghostOf(lit), off = c_.offOf(c_.lit);

    // State words; another state or press feedback replaces the row.
    bool connected = v.mode == jt::Mode::Connected;
    std::string status = connected ? "CRG" : v.scoreboardBack ? "CRG BACK" : "LOCAL";
    std::string single;
    if (v.feedback == jt::Feedback::NotTaken) single = "NOT TAKEN";
    else if (v.feedback == jt::Feedback::NotAllowed) single = "NO WRITE ACCESS";
    else if (v.state != "LINEUP" && v.state != "JAM" && v.state != "TIMEOUT") single = v.state + "   " + status;
    ui::visible(single_, !single.empty());
    for (int i = 0; i < 4; i++) ui::visible(words_[i], single.empty());
    setText(single_, single);
    ui::textColor(single_, v.feedback == jt::Feedback::NotTaken || v.feedback == jt::Feedback::NotAllowed
                               ? (c_.tint ? lv_color_hex(kRed) : c_.lit) : lit);
    const char* names[3] = {"LINEUP", "JAM", "TIMEOUT"};
    for (int i = 0; i < 3; i++) ui::textColor(words_[i], v.state == names[i] ? lit : off);
    setText(words_[3], status);
    ui::textColor(words_[3], c_.lit);

    // Period and jam number, from "Period 24:10" and "P1 · Jam 7" / "P1 · Next: Jam 1".
    std::string pclock;
    size_t sp = v.period.rfind(' ', v.period.find(':'));
    if (v.period.find(':') != std::string::npos) pclock = v.period.substr(sp == std::string::npos ? 0 : sp + 1);
    pclock = pclock.substr(0, pclock.find(' '));
    period_->set(pclock, lit, ghost);
    std::string pno = v.context.size() >= 2 && v.context[0] == 'P' ? v.context.substr(0, v.context.find(' ')) : "";
    setText(pNo_, pno);
    ui::textColor(pNo_, lit);
    size_t j = v.context.find("Jam ");
    std::string jno = j == std::string::npos ? "" : v.context.substr(j + 4);
    if (jno.size() == 1) jno = "0" + jno;
    setText(jamLabel_, jno.empty() ? "" : v.context.find("Next") != std::string::npos ? "NEXT JAM" : "JAM");
    jamNo_->set(jno, lit, ghost);

    main_->set(v.clock, v.feedbackRed ? (c_.tint ? lv_color_hex(kRed) : c_.lit) : lit, ghost);
    rim_->set(v.feedbackRed ? 1000 : v.ringPermille, v.feedbackRed && c_.tint ? lv_color_hex(kRed) : lit, ghost, v.ringOn || v.feedbackRed);

    // The undo/menu hold: a line under the clock.
    lv_obj_set_width(hold_, px(140) * v.holdPermille / 1000);
    ui::bgColor(hold_, v.holdStage ? lv_color_hex(c_.tint ? kInter : c_.lit.red > 128 ? 0xffb347 : 0x1a1f14) : lit);

    std::string crown, left = "UNDO", right = "TIMEOUT";
    hintActions(v.hint, crown, left, right);
    legends_->set(crown, left, right);
    legends_->show(!crowdedButtons() || legendsDue(now, v.holdPermille));
}

// ---------- box timer ----------

BoxFace::BoxFace(lv_obj_t* parent, const jt::Fonts& f, Palette p, const Options& o) : c_(colours(p)), f_(f), o_(o) {
    root_ = faceRoot(parent, c_.bg);
    // team colours: a thin edge outside the rim, one half per jammer
    edgeL_ = new Ring(root_, 118, 3);
    edgeR_ = new Ring(root_, 118, 3);
    edgeL_->show(false);
    edgeR_->show(false);
    rim_ = new Rim(root_);
    legends_ = new Legends(root_, lcdLegends(f, c_));
    status_ = text(root_, f.b12, c_.lit);
    lv_obj_align(status_, LV_ALIGN_TOP_MID, 0, px(66));
    lv_label_set_text(status_, "");

    lv_color_t off = c_.offOf(c_.lit);
    for (int i = 0; i < 3; i++) {
        Row& r = rows_[i];
        r.box = plain(root_);
        lv_obj_set_size(r.box, px(166), px(30));
        lv_obj_align(r.box, LV_ALIGN_TOP_MID, 0, px(80 + i * 30));
        lv_obj_set_style_border_side(r.box, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(r.box, c_.rule, 0);
        lv_obj_set_style_border_width(r.box, i < 2 ? 1 : 0, 0);
        r.hash = text(r.box, f.m12, c_.label);
        lv_label_set_text(r.hash, "#");
        lv_obj_align(r.hash, LV_ALIGN_LEFT_MID, 0, px(2));
        r.num = new Digits(r.box, 17);
        lv_obj_align(r.num->obj(), LV_ALIGN_LEFT_MID, px(9), 0);
        r.time = new Digits(r.box, 24);
        lv_obj_align(r.time->obj(), LV_ALIGN_LEFT_MID, px(50), 0);
        r.stand = text(r.box, f.b12, off);
        lv_label_set_text(r.stand, "STAND");
        lv_obj_align(r.stand, LV_ALIGN_TOP_RIGHT, 0, px(2));
        r.done = text(r.box, f.b12, off);
        lv_label_set_text(r.done, "DONE");
        lv_obj_align(r.done, LV_ALIGN_BOTTOM_RIGHT, 0, -px(4));
    }

    divider_ = plain(root_);
    lv_obj_set_size(divider_, 1, px(96));
    lv_obj_set_style_bg_color(divider_, c_.rule, 0);
    lv_obj_set_style_bg_opa(divider_, LV_OPA_COVER, 0);
    lv_obj_align(divider_, LV_ALIGN_TOP_MID, 0, px(84));
    for (int i = 0; i < 2; i++) {
        Half& h = halves_[i];
        lv_obj_t* col = column(root_, 5);
        lv_obj_set_width(col, px(96));
        lv_obj_align(col, LV_ALIGN_TOP_MID, i == 0 ? -px(50) : px(50), px(84));
        h.team = text(col, f.b12, c_.label);
        lv_label_set_text(h.team, "");
        h.time = new Digits(col, 34);
        lv_obj_t* sd = row(col, 8, LV_FLEX_ALIGN_CENTER);
        h.stand = text(sd, f.b12, off);
        lv_label_set_text(h.stand, "STAND");
        h.done = text(sd, f.b12, off);
        lv_label_set_text(h.done, "DONE");
        h.note = text(col, f.m12, c_.label);
        lv_obj_set_width(h.note, px(92));
        lv_label_set_long_mode(h.note, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(h.note, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(h.note, "");
    }

    hold_ = plain(root_);
    lv_obj_set_size(hold_, 0, px(3));
    lv_obj_set_style_bg_opa(hold_, LV_OPA_COVER, 0);
    lv_obj_align(hold_, LV_ALIGN_TOP_MID, 0, px(172));
}

BoxFace::~BoxFace() {
    delete edgeL_;
    delete edgeR_;
    delete rim_;
    delete legends_;
    for (auto& r : rows_) {
        delete r.num;
        delete r.time;
    }
    for (auto& h : halves_) delete h.time;
    lv_obj_delete(root_);
}

void BoxFace::show(const box::View& v, uint32_t now) {
    ui::visible(root_, v.screen == box::Screen::Main);
    if (v.screen != box::Screen::Main) return;
    lv_color_t off = c_.offOf(c_.lit);
    auto litOf = [&](box::Phase p) { return c_.tint ? lv_color_hex(phaseColour(p)) : c_.lit; };

    std::string status = upper(v.status);
    if (!v.notice.empty()) status = upper(v.notice);
    else if (v.feedback == box::Feedback::NotTaken) status = "NOT TAKEN - PRESS AGAIN";
    else if (v.feedback == box::Feedback::NotAllowed) status = "NO WRITE ACCESS";
    else if (v.feedback == box::Feedback::Waiting) status = "SENT\xe2\x80\xa6";
    else if (v.paused) status += "  PAUSED";
    setText(status_, status);

    bool jammers = v.role == box::Role::Jammers;
    ui::visible(divider_, jammers);
    for (int i = 0; i < 2; i++) ui::visible(lv_obj_get_parent(halves_[i].team), jammers);
    int first = -1; // the slot the rim follows
    if (jammers) {
        for (int i = 0; i < 3; i++) ui::visible(rows_[i].box, false);
        for (int i = 0; i < 2; i++) {
            Half& h = halves_[i];
            const box::Slot* s = nullptr;
            for (auto& sl : v.slots)
                if (sl.side == i && !sl.key.empty()) s = &sl;
            setText(h.team, upper(i == 0 ? v.teamLeft : v.teamRight));
            int32_t tc = i == 0 ? v.colourLeft : v.colourRight;
            ui::textColor(h.team, o_.teamColours && tc >= 0 ? lv_color_hex(tc) : c_.label);
            lv_color_t lit = s ? litOf(s->phase) : c_.lit;
            h.time->set(s ? s->clock : " ;  ", lit, c_.ghostOf(lit));
            ui::textColor(h.stand, s && s->phase == box::Phase::Stand ? lit : off);
            ui::textColor(h.done, s && s->phase == box::Phase::Done ? lit : off);
            setText(h.note, s ? (s->swapDone ? "SWAP - TAP TO RELEASE" : "") : upper(i == 0 ? v.emptyLeft : v.emptyRight));
            if (s && first < 0) first = (int)(s - &v.slots[0]);
        }
    } else {
        int n = v.slots.size() > 3 ? 3 : (int)v.slots.size();
        for (int i = 0; i < 3; i++) {
            Row& r = rows_[i];
            bool used = i < n || (i == 0 && n == 0);
            ui::visible(r.box, used);
            if (!used) continue;
            const box::Slot* s = i < n ? &v.slots[i] : nullptr;
            lv_color_t lit = s ? litOf(s->phase) : c_.lit;
            std::string num = s ? (s->number == "?" ? "-" : s->number) : "";
            ui::visible(r.hash, !num.empty());
            r.num->set(num, lit, c_.ghostOf(lit));
            r.time->set(s ? s->clock : " ;  ", lit, c_.ghostOf(lit));
            ui::textColor(r.stand, s && s->phase == box::Phase::Stand ? lit : off);
            ui::textColor(r.done, s && s->phase == box::Phase::Done ? lit : off);
        }
        if (n > 0) first = 0;
    }

    // Rim: the first skater's time left of 30 s, in their colour.
    if (first >= 0) {
        const box::Slot& s = v.slots[first];
        int sec = seconds(s.clock);
        lv_color_t lit = litOf(s.phase);
        rim_->set(sec < 0 ? 0 : sec * 1000 / 30, lit, c_.ghostOf(lit), true);
    } else {
        rim_->set(0, c_.lit, c_.ghostOf(c_.lit), true);
    }

    lv_obj_set_width(hold_, px(140) * v.holdPermille / 1000);
    ui::bgColor(hold_, v.holdStage ? lv_color_hex(c_.tint ? kInter : c_.lit.red > 128 ? 0xffb347 : 0x1a1f14) : c_.lit);

    // Team colours: the jammers' halves, or the whole edge for one team.
    int32_t cl = jammers ? v.colourLeft : v.colourTeam, cr = jammers ? v.colourRight : v.colourTeam;
    edgeL_->show(o_.teamColours && cl >= 0);
    edgeR_->show(o_.teamColours && cr >= 0);
    if (cl >= 0) {
        edgeL_->track(0, 0, lv_color_hex(cl));
        edgeL_->arc(jammers ? 182 : 180, jammers ? 358 : 360, lv_color_hex(cl));
    }
    if (cr >= 0) {
        edgeR_->track(0, 0, lv_color_hex(cr));
        edgeR_->arc(jammers ? 2 : 0, jammers ? 178 : 180, lv_color_hex(cr));
    }

    std::string crown, left, right;
    if (jammers) {
        crown = "HOLD: UNDO";
        left = "SAT DOWN";
        right = "SAT DOWN";
    } else {
        crown = "SAT DOWN";
        left = "UNDO";
        right = v.mode == box::Mode::Connected ? "RELEASE" : "PAUSE";
    }
    hintActions(v.hint, crown, left, right);
    legends_->set(crown, left, right);
    legends_->show(!crowdedButtons() || legendsDue(now, v.holdPermille));
}

} // namespace lcd
