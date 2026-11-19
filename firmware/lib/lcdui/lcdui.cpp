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

namespace {

const float kSlant = 0.06f; // digits lean right, as on a stopwatch LCD

// State colours of the classic screens (jtui.cpp, boxui.cpp), for the night
// palette with colour.
const uint32_t kJam = 0x3ddc84, kLineup = 0xffb020, kTimeout = 0xd9a0ff, kInter = 0x6fb6ff, kRed = 0xff6a5e;
const uint32_t kServing = 0xff5a4e, kStand = 0xffff00, kDone = 0x6fdf6f, kWaitingC = 0xb05050, kPausedC = 0x8f8f8f;

lv_obj_t* plain(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t* text(lv_obj_t* parent, const lv_font_t* f, lv_color_t c) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

void setText(lv_obj_t* l, const std::string& s) {
    const char* t = lv_label_get_text(l);
    if (!t || s != t) lv_label_set_text(l, s.c_str());
}

lv_obj_t* row(lv_obj_t* parent, int gap, lv_flex_align_t cross) {
    lv_obj_t* r = plain(parent);
    lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, cross, cross);
    lv_obj_set_style_pad_column(r, px(gap), 0);
    return r;
}

lv_obj_t* column(lv_obj_t* parent, int gap) {
    lv_obj_t* r = plain(parent);
    lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(r, px(gap), 0);
    return r;
}

lv_obj_t* faceRoot(lv_obj_t* parent, const Colours& c) {
    lv_obj_t* r = plain(parent);
    lv_obj_set_size(r, px(240), px(240));
    lv_obj_center(r);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(r, true, 0);
    lv_obj_set_style_bg_color(r, c.bg, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    return r;
}

// "1:42" -> seconds; -1 when it is not a time.
int seconds(const std::string& clock) {
    int m = 0, s = 0;
    if (sscanf(clock.c_str(), "%d:%d", &m, &s) != 2) return -1;
    return m * 60 + s;
}

// Only what the segments can show; anything else becomes a blank digit.
std::string segText(const std::string& s) {
    std::string out;
    for (char ch : s) out += (ch >= '0' && ch <= '9') || ch == ':' || ch == '-' ? ch : ' ';
    return out;
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

std::string upper(const std::string& s) {
    std::string o = s;
    for (char& ch : o)
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    return o;
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

// ---------- legends ----------

Legends::Legends(lv_obj_t* parent, const jt::Fonts& f, const Colours& c) {
    const char* names[3] = {btn::gNames.crown, btn::gNames.left, btn::gNames.right};
    int at[3] = {btn::gNames.crownAt, btn::gNames.leftAt, btn::gNames.rightAt};
    for (int i = 0; i < 3; i++) {
        float a = at[i] * 30.0f * (float)M_PI / 180.0f;
        // top half closer in, where the circle is wider: two legends fit side by side
        float rr = cosf(a) > 0 ? px(83) : px(92);
        int dx = (int)lroundf(rr * sinf(a)), dy = (int)lroundf(-rr * cosf(a));
        // At 12 or 6 o'clock: centred. Elsewhere one line that starts at the
        // rim's inner edge on the button's side and runs inward.
        bool centre = dx >= -px(10) && dx <= px(10), rightSide = !centre && dx > 0;
        lv_obj_t* r = row(parent, 4, LV_FLEX_ALIGN_CENTER);
        if (rightSide) lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_REVERSE);
        lv_obj_t* dot = plain(r);
        lv_obj_set_size(dot, px(7), px(7));
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        uint32_t col = btn::colourOf(names[i]);
        lv_obj_set_style_bg_color(dot, col ? lv_color_hex(col) : c.lit, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(dot, c.lit, 0);
        lv_obj_set_style_border_width(dot, c.tint || c.lit.red > 128 ? 0 : 1, 0);
        label_[i] = text(r, f.b12, c.label);
        lv_obj_set_style_text_align(label_[i], centre ? LV_TEXT_ALIGN_CENTER : rightSide ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);

        lv_label_set_text(label_[i], "");
        // Anchored at the button, the text runs inward: left-side legends
        // start at the dot, right-side ones end at it.
        int half = px(120), y = half + dy - px(8);
        int edge = (int)sqrtf((float)(px(104) * px(104) - dy * dy)); // rim's inner edge from the centre
        if (centre) lv_obj_align(r, LV_ALIGN_TOP_MID, 0, y);
        else if (rightSide) lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -(half - edge) - px(2), y);
        else lv_obj_align(r, LV_ALIGN_TOP_LEFT, half - edge + px(2), y);
    }
}

// One short line each: what comes before a comma ("PAUSE, HELD: UNDO").
void Legends::set(const std::string& crown, const std::string& left, const std::string& right) {
    const std::string* t[3] = {&crown, &left, &right};
    for (int i = 0; i < 3; i++) setText(label_[i], t[i]->substr(0, t[i]->find(',')));
}

void hintActions(const std::string& hint, std::string& crown, std::string& left, std::string& right) {
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i < hint.size(); i++) {
        if (hint[i] == '\n' || hint.compare(i, 4, " \xc2\xb7 ") == 0) {
            parts.push_back(cur);
            cur.clear();
            if (hint[i] != '\n') i += 3;
            continue;
        }
        cur += hint[i];
    }
    parts.push_back(cur);
    for (auto& p : parts) {
        size_t c = p.find(": ");
        if (c == std::string::npos) continue;
        std::string who = p.substr(0, c), what = upper(p.substr(c + 2));
        if (who == btn::crown()) crown = what;
        else if (who == btn::left() || who == btn::left("Undo")) left = what;
        else if (who == btn::right()) right = what;
        else if (who == "Hold") crown = upper(p); // "Hold: Extend period 1" (crown, intermission)
    }
}

// ---------- jam timer ----------

JtFace::JtFace(lv_obj_t* parent, const jt::Fonts& f, Palette p) : c_(colours(p)), f_(f) {
    root_ = faceRoot(parent, c_);
    rim_ = new Rim(root_);
    legends_ = new Legends(root_, f, c_);

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
    (void)now;
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
}

// ---------- box timer ----------

namespace {
uint32_t phaseColour(box::Phase p) {
    switch (p) {
    case box::Phase::Serving: return kServing;
    case box::Phase::Stand: return kStand;
    case box::Phase::Done: return kDone;
    case box::Phase::Waiting: return kWaitingC;
    case box::Phase::Paused: return kPausedC;
    }
    return kServing;
}
} // namespace

BoxFace::BoxFace(lv_obj_t* parent, const jt::Fonts& f, Palette p) : c_(colours(p)), f_(f) {
    root_ = faceRoot(parent, c_);
    rim_ = new Rim(root_);
    legends_ = new Legends(root_, f, c_);
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
    (void)now;
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
}

} // namespace lcd
