#include "facekit.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "buttons.h"
#include "lvset.h"
#include "scale.h"
#include "src/misc/lv_area_private.h" // lv_area_intersect

using ui::px;

namespace lcd {
namespace kit {

// ---------- LVGL helpers ----------

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
    lv_label_set_text(l, "");
    return l;
}

void setText(lv_obj_t* l, const std::string& s) {
    if (!l) return;
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

lv_obj_t* faceRoot(lv_obj_t* parent, lv_color_t bg) {
    lv_obj_t* r = plain(parent);
    lv_obj_set_size(r, px(240), px(240));
    lv_obj_center(r);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(r, true, 0);
    lv_obj_set_style_bg_color(r, bg, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    return r;
}

lv_obj_t* dot(lv_obj_t* parent, int size, lv_color_t c) {
    lv_obj_t* d = plain(parent);
    lv_obj_set_size(d, px(size), px(size));
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, c, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    return d;
}

void place(lv_obj_t* o, lv_align_t a, int x, int y) {
    // lv_obj_align always marks the layout dirty; skip a move to the same place
    if (lv_obj_get_style_align(o, 0) == a && lv_obj_get_style_x(o, 0) == x && lv_obj_get_style_y(o, 0) == y) return;
    lv_obj_align(o, a, x, y);
}

void setWidth(lv_obj_t* o, int w) {
    if (lv_obj_get_style_width(o, 0) != w) lv_obj_set_width(o, w);
}

int seconds(const std::string& clock) {
    int m = 0, s = 0;
    if (sscanf(clock.c_str(), "%d:%d", &m, &s) != 2) return -1;
    return m * 60 + s;
}

std::string upper(const std::string& s) {
    std::string o = s;
    for (char& ch : o)
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    return o;
}

uint32_t toneColour(jt::Tone t) {
    switch (t) {
    case jt::Tone::Jam: return kJam;
    case jt::Tone::Lineup: return kLineup;
    case jt::Tone::Timeout: return kTimeout;
    case jt::Tone::Intermission: return kInter;
    case jt::Tone::Idle: return 0;
    }
    return 0;
}

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

// ---------- reading the views ----------

JtInfo jtInfo(const jt::View& v) {
    JtInfo i;
    // "Period 24:10" (" stopped"), "P1 · Jam 7" / "P1 · Next: Jam 8"
    size_t colon = v.period.find(':');
    if (colon != std::string::npos) {
        size_t sp = v.period.rfind(' ', colon);
        i.pClock = v.period.substr(sp == std::string::npos ? 0 : sp + 1);
        i.pClock = i.pClock.substr(0, i.pClock.find(' '));
    }
    if (v.context.size() >= 2 && v.context[0] == 'P') i.pNo = v.context.substr(0, v.context.find(' '));
    size_t j = v.context.find("Jam ");
    if (j != std::string::npos) i.jamNo = v.context.substr(j + 4);
    i.nextJam = v.context.find("Next") != std::string::npos;
    i.connected = v.mode == jt::Mode::Connected;
    i.status = i.connected ? "CRG" : v.scoreboardBack ? "CRG BACK" : "LOCAL";
    if (v.feedback == jt::Feedback::NotTaken) {
        i.word = "NOT TAKEN";
        i.alert = true;
    } else if (v.feedback == jt::Feedback::NotAllowed) {
        i.word = "NO WRITE ACCESS";
        i.alert = true;
    } else if (v.state == "JAM" && !i.jamNo.empty()) {
        i.word = "JAM " + i.jamNo;
    } else {
        i.word = v.state;
    }
    i.leftPermille = v.ringCountsUp ? 1000 - v.ringPermille : v.ringPermille;
    return i;
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
        size_t held = p.find(" held: ");
        if (c == std::string::npos && held == std::string::npos) continue;
        std::string who = c != std::string::npos && (held == std::string::npos || c < held) ? p.substr(0, c) : p.substr(0, held);
        std::string what = upper(c != std::string::npos && (held == std::string::npos || c < held) ? p.substr(c + 2) : "HOLD: " + p.substr(held + 7));
        if (who == btn::crown()) crown = what;
        else if (who == btn::left() || who == btn::left("Undo")) left = what;
        else if (who == btn::right()) right = what;
        else if (who == "Hold") crown = upper(p); // "Hold: Extend period 1" (crown, intermission)
    }
}

void jtActions(const jt::View& v, std::string& crown, std::string& left, std::string& right) {
    crown.clear();
    left = "UNDO";
    right = "TIMEOUT";
    hintActions(v.hint, crown, left, right);
}

void boxActions(const box::View& v, std::string& crown, std::string& left, std::string& right) {
    if (v.role == box::Role::Jammers) {
        crown = "HOLD: UNDO";
        left = "SAT DOWN";
        right = "SAT DOWN";
    } else {
        crown = "SAT DOWN";
        left = "UNDO";
        right = v.mode == box::Mode::Connected ? "RELEASE" : "PAUSE";
    }
    hintActions(v.hint, crown, left, right);
}

int slotSeconds(const box::Slot& s) { return s.key.empty() ? -1 : seconds(s.clock); }

BoxInfo boxInfo(const box::View& v) {
    BoxInfo b;
    b.status = upper(v.status);
    if (!v.notice.empty()) b.status = upper(v.notice), b.alert = true;
    else if (v.feedback == box::Feedback::NotTaken) b.status = "NOT TAKEN - PRESS AGAIN", b.alert = true;
    else if (v.feedback == box::Feedback::NotAllowed) b.status = "NO WRITE ACCESS", b.alert = true;
    else if (v.feedback == box::Feedback::Waiting) b.status = "SENT\xe2\x80\xa6";
    else if (v.paused) b.status += "  PAUSED";
    int best = 1 << 30;
    for (size_t i = 0; i < v.slots.size(); i++) {
        const box::Slot& s = v.slots[i];
        if (s.key.empty()) continue;
        b.used++;
        if (s.phase == box::Phase::Done) {
            b.done++;
            continue;
        }
        int sec = seconds(s.clock);
        if (sec >= 0 && sec < best) best = sec, b.next = (int)i;
    }
    if (b.next < 0)
        for (size_t i = 0; i < v.slots.size(); i++)
            if (!v.slots[i].key.empty()) {
                b.next = (int)i;
                break;
            }
    return b;
}

// ---------- stroke digits ----------

namespace {

// Glyphs on a grid of the bowl radius r: x 0..2, y 0..4 (two bowls stacked),
// measured on the stroke's middle line.
struct Op {
    char kind; // 'l' line x1 y1 x2 y2, 'a' arc cx cy from to (LVGL degrees, clockwise from 3 o'clock)
    float a, b, c, d;
};
struct Glyph {
    char ch;
    std::vector<Op> ops;
};
const std::vector<Glyph>& glyphs() {
    static const std::vector<Glyph> g = {
        {'0', {{'a', 1, 1, 180, 360}, {'a', 1, 3, 0, 180}, {'l', 0, 1, 0, 3}, {'l', 2, 1, 2, 3}}},
        {'1', {{'l', 1.25f, 0, 1.25f, 4}, {'l', 0.35f, 0.85f, 1.25f, 0}}},
        {'2', {{'a', 1, 1, 195, 30}, {'l', 1.866f, 1.5f, 0, 4}, {'l', 0, 4, 2, 4}}},
        {'3', {{'a', 1, 1, 200, 90}, {'a', 1, 3, 270, 160}}},
        {'4', {{'l', 1.5f, 0, 1.5f, 4}, {'l', 1.5f, 0, 0, 2.8f}, {'l', 0, 2.8f, 2, 2.8f}}},
        {'5', {{'l', 1.9f, 0, 0.12f, 0}, {'l', 0.12f, 0, 0.12f, 2.05f}, {'l', 0.12f, 2.05f, 0.21f, 2.45f}, {'a', 1, 3, 218, 150}}},
        {'6', {{'a', 1, 3, 0, 360}, {'l', 0, 3, 0, 1}, {'a', 1, 1, 180, 325}}},
        {'7', {{'l', 0, 0, 2, 0}, {'l', 2, 0, 0.55f, 4}}},
        {'8', {{'a', 1, 1, 0, 360}, {'a', 1, 3, 0, 360}}},
        {'9', {{'a', 1, 1, 0, 360}, {'l', 2, 1, 2, 3}, {'a', 1, 3, 0, 145}}},
        {'-', {{'l', 0.3f, 2, 1.7f, 2}}},
    };
    return g;
}
const Glyph* glyphOf(char ch) {
    for (auto& g : glyphs())
        if (g.ch == ch) return &g;
    return nullptr;
}

struct SGeo {
    float H, t, r, W, gap, cw;
    SGeo(float h, float weight, float gapPx) {
        H = h;
        t = fmaxf(1.0f, h * weight);
        r = (h - t) / 4;
        W = 2 * r + t;
        gap = gapPx >= 0 ? gapPx : h * 0.14f;
        cw = t * 2.4f;
    }
};

} // namespace

Stroke::Stroke(lv_obj_t* parent, int height, float weight) : h_(px(height)), weight_(weight) {
    obj_ = plain(parent);
    lv_obj_set_user_data(obj_, this);
    lv_obj_set_size(obj_, 1, h_);
    lv_obj_add_event_cb(obj_, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(obj_, onExt, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
    c_ = ghost_ = glowOuter_ = glowInner_ = lv_color_white();
}

int Stroke::widthOf(const std::string& s) const {
    SGeo g((float)h_, weight_, gap_ >= 0 ? (float)px(gap_) : -1.0f);
    float x = 0;
    for (size_t i = 0; i < s.size(); i++) {
        x += s[i] == ':' ? g.cw : g.W;
        if (i + 1 < s.size()) x += s[i] == ':' || s[i + 1] == ':' ? g.gap * 0.5f : g.gap;
    }
    return (int)ceilf(x) + 2;
}

int Stroke::fitHeight(const std::string& s, float weight, int maxWidth, int maxHeight) {
    // width grows linearly with the height
    float per = 0;
    SGeo g(100, weight, -1);
    for (size_t i = 0; i < s.size(); i++) {
        per += s[i] == ':' ? g.cw : g.W;
        if (i + 1 < s.size()) per += s[i] == ':' || s[i + 1] == ':' ? g.gap * 0.5f : g.gap;
    }
    if (per <= 0) return maxHeight;
    int h = (int)(maxWidth * 100 / per);
    return h < maxHeight ? h : maxHeight;
}

void Stroke::glowOn(bool on) {
    if (on == glow_) return;
    lv_obj_invalidate(obj_); // the old glow's area
    glow_ = on;
    lv_obj_refresh_ext_draw_size(obj_);
    lv_obj_invalidate(obj_);
}

void Stroke::resize() {
    int w = widthOf(text_);
    if (lv_obj_get_width(obj_) != w || lv_obj_get_height(obj_) != h_) lv_obj_set_size(obj_, w, h_);
}

void Stroke::setHeight(int height) {
    if (px(height) == h_) return;
    h_ = px(height);
    resize();
    lv_obj_invalidate(obj_);
}

void Stroke::set(const std::string& s, lv_color_t c) {
    if (!first_ && s == text_ && lv_color_eq(c, c_)) return;
    first_ = false;
    bool sameWidth = s.size() == text_.size() && s.find(':') == text_.find(':');
    text_ = s;
    c_ = c;
    if (!sameWidth) resize();
    lv_obj_invalidate(obj_);
}

// The glow reaches past the box.
void Stroke::onExt(lv_event_t* e) {
    Stroke* d = (Stroke*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
    if (d->glow_) lv_event_set_ext_draw_size(e, (int32_t)(d->h_ * d->weight_ * 2.2f) + 2);
}

void Stroke::onDraw(lv_event_t* e) {
    lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
    Stroke* d = (Stroke*)lv_obj_get_user_data(o);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    SGeo g((float)d->h_, d->weight_, d->gap_ >= 0 ? (float)px(d->gap_) : -1.0f);

    auto drawOps = [&](const Glyph& gl, float ox, float width, lv_color_t col, lv_opa_t opa) {
        float x0 = a.x1 + ox + g.t / 2, y0 = a.y1 + g.t / 2;
        for (const Op& op : gl.ops) {
            if (op.kind == 'l') {
                lv_draw_line_dsc_t ld;
                lv_draw_line_dsc_init(&ld);
                ld.width = (int32_t)lroundf(width);
                ld.color = col;
                ld.opa = opa;
                ld.round_start = 1;
                ld.round_end = 1;
                ld.p1.x = x0 + op.a * g.r;
                ld.p1.y = y0 + op.b * g.r;
                ld.p2.x = x0 + op.c * g.r;
                ld.p2.y = y0 + op.d * g.r;
                lv_draw_line(layer, &ld);
            } else {
                lv_draw_arc_dsc_t ad;
                lv_draw_arc_dsc_init(&ad);
                ad.width = (int32_t)lroundf(width);
                ad.color = col;
                ad.opa = opa;
                ad.rounded = 1;
                ad.center.x = (int32_t)lroundf(x0 + op.a * g.r);
                ad.center.y = (int32_t)lroundf(y0 + op.b * g.r);
                ad.radius = (uint16_t)lroundf(g.r + width / 2);
                ad.start_angle = op.c;
                ad.end_angle = op.d;
                lv_draw_arc(layer, &ad);
            }
        }
    };
    auto drawColon = [&](float ox, float width, lv_color_t col, lv_opa_t opa) {
        lv_draw_rect_dsc_t rd;
        lv_draw_rect_dsc_init(&rd);
        rd.bg_color = col;
        rd.bg_opa = opa;
        rd.radius = LV_RADIUS_CIRCLE;
        float s = width * 1.15f, cx = a.x1 + ox + g.cw / 2;
        for (float cy : {g.t / 2 + g.r * 1.3f, g.t / 2 + g.r * 2.75f}) {
            lv_area_t q;
            q.x1 = (int32_t)lroundf(cx - s / 2);
            q.x2 = q.x1 + (int32_t)lroundf(s) - 1;
            q.y1 = a.y1 + (int32_t)lroundf(cy - s / 2);
            q.y2 = q.y1 + (int32_t)lroundf(s) - 1;
            lv_draw_rect(layer, &rd, &q);
        }
    };

    float x = 0;
    const std::string& s = d->text_;
    for (size_t i = 0; i < s.size(); i++) {
        char ch = s[i];
        if (ch == ':') {
            if (d->glow_) {
                drawColon(x, g.t * 3.2f, d->glowOuter_, LV_OPA_20);
                drawColon(x, g.t * 1.9f, d->glowInner_, LV_OPA_50);
            }
            drawColon(x, g.t, d->c_, LV_OPA_COVER);
            x += g.cw;
        } else {
            if (d->ghostOn_) {
                drawOps(*glyphOf('8'), x, g.t, d->ghost_, LV_OPA_COVER);
                drawOps(*glyphOf('0'), x, g.t, d->ghost_, LV_OPA_COVER);
            }
            const Glyph* gl = glyphOf(ch);
            if (gl) {
                if (d->glow_) {
                    drawOps(*gl, x, g.t * 4.0f, d->glowOuter_, LV_OPA_20);
                    drawOps(*gl, x, g.t * 2.2f, d->glowInner_, LV_OPA_40);
                }
                drawOps(*gl, x, g.t, d->c_, LV_OPA_COVER);
            }
            x += g.W;
        }
        if (i + 1 < s.size()) x += ch == ':' || s[i + 1] == ':' ? g.gap * 0.5f : g.gap;
    }
}

// ---------- rings ----------

Ring::Ring(lv_obj_t* parent, int radius, int width) {
    obj_ = lv_arc_create(parent);
    lv_obj_remove_style_all(obj_);
    lv_obj_remove_flag(obj_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj_, LV_OBJ_FLAG_SCROLLABLE);
    int w = px(width) > 0 ? px(width) : 1, d = 2 * px(radius) + w;
    lv_obj_set_size(obj_, d, d);
    lv_obj_center(obj_);
    lv_arc_set_rotation(obj_, 270); // 0 = 12 o'clock
    lv_arc_set_mode(obj_, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(obj_, 0, 1000);
    for (lv_part_t p : {LV_PART_MAIN, LV_PART_INDICATOR}) {
        lv_obj_set_style_arc_width(obj_, w, p);
        lv_obj_set_style_arc_rounded(obj_, true, p);
        lv_obj_set_style_arc_opa(obj_, LV_OPA_COVER, p);
    }
    lv_arc_set_bg_angles(obj_, 0, 0);
    lv_arc_set_angles(obj_, 0, 0);
    tf_ = tt_ = af_ = at_ = 0;
}

void Ring::track(int from, int to, lv_color_t c, lv_opa_t opa) {
    ui::arcColor(obj_, c, LV_PART_MAIN);
    ui::arcOpa(obj_, opa, LV_PART_MAIN);
    if (from == tf_ && to == tt_) return;
    tf_ = from;
    tt_ = to;
    lv_arc_set_bg_angles(obj_, from, to);
}

void Ring::arc(int from, int to, lv_color_t c) {
    ui::arcColor(obj_, c, LV_PART_INDICATOR);
    if (to < from) to = from;
    if (from == af_ && to == at_) return;
    af_ = from;
    at_ = to;
    lv_arc_set_angles(obj_, from, to);
}

void Ring::rounded(bool r) {
    for (lv_part_t p : {LV_PART_MAIN, LV_PART_INDICATOR}) lv_obj_set_style_arc_rounded(obj_, r, p);
}

void Ring::show(bool on) { ui::visible(obj_, on); }

Dashes::Dashes(lv_obj_t* parent, int radius, int width) : r_(px(radius)), w_(px(width) > 0 ? px(width) : 1) {
    obj_ = plain(parent);
    lv_obj_set_user_data(obj_, this);
    lv_obj_set_size(obj_, px(240), px(240));
    lv_obj_center(obj_);
    lv_obj_add_event_cb(obj_, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
    litC_ = trackC_ = lv_color_white();
}

void Dashes::invalidateSeg(int from, int to) {
    lv_area_t c;
    lv_obj_get_coords(obj_, &c);
    float cx = (c.x1 + c.x2) / 2.0f, cy = (c.y1 + c.y2) / 2.0f;
    int n = count(), m = w_ / 2 + 3;
    for (int i = from; i < to; i++) {
        float a0 = i * 2 * (float)M_PI / n, a1 = (i + 1) * 2 * (float)M_PI / n;
        float xs[2] = {cx + r_ * sinf(a0), cx + r_ * sinf(a1)}, ys[2] = {cy - r_ * cosf(a0), cy - r_ * cosf(a1)};
        lv_area_t a = {(int32_t)fminf(xs[0], xs[1]) - m, (int32_t)fminf(ys[0], ys[1]) - m,
                       (int32_t)fmaxf(xs[0], xs[1]) + m, (int32_t)fmaxf(ys[0], ys[1]) + m};
        lv_obj_invalidate_area(obj_, &a);
    }
}

void Dashes::set(int permille, Style s, lv_color_t lit, lv_color_t track) {
    bool full = lit_ < 0 || s != style_ || !lv_color_eq(lit, litC_) || !lv_color_eq(track, trackC_);
    style_ = s;
    int n = permille <= 0 ? 0 : permille >= 1000 ? count() : (permille * count() + 500) / 1000;
    if (full) {
        litC_ = lit;
        trackC_ = track;
        lit_ = n;
        lv_obj_invalidate(obj_);
    } else if (n != lit_) {
        invalidateSeg(n < lit_ ? n : lit_, n < lit_ ? lit_ : n);
        lit_ = n;
    }
}

void Dashes::show(bool on) { ui::visible(obj_, on); }

void Dashes::onDraw(lv_event_t* e) {
    lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
    Dashes* d = (Dashes*)lv_obj_get_user_data(o);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t c;
    lv_obj_get_coords(o, &c);
    int cx = (c.x1 + c.x2) / 2, cy = (c.y1 + c.y2) / 2, n = d->count();
    for (int i = 0; i < n; i++) {
        bool lit = i < d->lit_;
        float deg0 = i * 360.0f / n, deg1 = (i + 1) * 360.0f / n;
        if (d->style_ == Style::Dot) {
            float a = (deg0 + 0.5f * (deg1 - deg0)) * (float)M_PI / 180;
            lv_draw_rect_dsc_t rd;
            lv_draw_rect_dsc_init(&rd);
            rd.bg_color = lit ? d->litC_ : d->trackC_;
            rd.radius = LV_RADIUS_CIRCLE;
            int s = lit ? d->w_ : d->w_ * 2 / 3;
            lv_area_t q;
            q.x1 = cx + (int32_t)lroundf(d->r_ * sinf(a)) - s / 2;
            q.y1 = cy - (int32_t)lroundf(d->r_ * cosf(a)) - s / 2;
            q.x2 = q.x1 + s - 1;
            q.y2 = q.y1 + s - 1;
            lv_area_t clip;
            if (!lv_area_intersect(&clip, &q, &layer->_clip_area)) continue;
            lv_draw_rect(layer, &rd, &q);
            continue;
        }
        float pad = d->style_ == Style::Dash ? (deg1 - deg0) * 0.2f : 0.0f;
        lv_draw_arc_dsc_t ad;
        lv_draw_arc_dsc_init(&ad);
        ad.color = lit ? d->litC_ : d->trackC_;
        ad.width = lit ? d->w_ : (d->w_ / 3 > 0 ? d->w_ / 3 : 1);
        ad.center.x = cx;
        ad.center.y = cy;
        ad.radius = (uint16_t)(d->r_ + ad.width / 2);
        ad.start_angle = deg0 + pad - 90;
        ad.end_angle = deg1 - pad - 90 + (d->style_ == Style::Solid ? 0.6f : 0.0f);
        lv_draw_arc(layer, &ad);
    }
}

// ---------- legends ----------

Legends::Legends(lv_obj_t* parent, const LegendStyle& st) : wrap_(st.wrapTop) {
    const char* names[3] = {btn::gNames.crown, btn::gNames.left, btn::gNames.right};
    int at[3] = {btn::gNames.crownAt, btn::gNames.leftAt, btn::gNames.rightAt};
    const char* fallback[3] = {"C", "L", "R"};
    for (int i = 0; i < 3; i++) {
        float a = at[i] * 30.0f * (float)M_PI / 180.0f;
        float ca = cosf(a);
        bool atTop = ca > 0.95f, atSide = ca > -0.7f && ca < 0.7f; // 12 o'clock; 10, 2, 4, 8 o'clock
        top_[i] = ca > 0 && !atTop && !atSide;
        // at 12 o'clock up at the rim, at the sides near it; one line each
        float rr = atTop ? px(107) : atSide ? px(102) : top_[i] ? px(st.rTop) : px(st.rBottom);
        int dx = (int)lroundf(rr * sinf(a)), dy = (int)lroundf(-rr * cosf(a));
        bool centre = dx >= -px(10) && dx <= px(10), rightSide = !centre && dx > 0;
        bool plate = st.chip || atTop || atSide;
        lv_obj_t* r = row(parent, plate ? 5 : 4, LV_FLEX_ALIGN_CENTER);
        row_[i] = r;
        if (rightSide) lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_REVERSE);
        if (plate) {
            lv_obj_set_style_bg_color(r, st.chipBg, 0);
            lv_obj_set_style_bg_opa(r, st.chip ? st.chipOpa : LV_OPA_90, 0);
            lv_obj_set_style_radius(r, px(9), 0);
            lv_obj_set_style_pad_hor(r, px(6), 0);
            lv_obj_set_style_pad_ver(r, px(3), 0);
        }
        uint32_t col = btn::colourOf(names[i]);
        lv_obj_t* d = dot(r, st.dotSize, col ? lv_color_hex(col) : st.text);
        lv_obj_set_style_border_color(d, st.dotBorder, 0);
        lv_obj_set_style_border_width(d, st.dotBorderWidth, 0);
        if (st.letter) {
            lv_obj_t* l = text(d, st.font, lv_color_black());
            lv_label_set_text(l, names[i] ? std::string(names[i], 1).c_str() : fallback[i]);
            lv_obj_center(l);
        }
        label_[i] = text(r, st.font, st.text);
        lv_obj_set_style_text_align(label_[i], centre ? LV_TEXT_ALIGN_CENTER : rightSide ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
        int half = px(120), y = half + dy - px(8);
        int e = px(atSide ? 112 : st.edge), edge = (int)sqrtf((float)(e * e - dy * dy));
        if (centre) lv_obj_align(r, LV_ALIGN_TOP_MID, 0, y);
        else if (rightSide) lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -(half - edge) - px(2), y);
        else lv_obj_align(r, LV_ALIGN_TOP_LEFT, half - edge + px(2), y);
    }
}

// One short line each: what comes before a comma ("PAUSE, HELD: UNDO").
void Legends::set(const std::string& crown, const std::string& left, const std::string& right) {
    const std::string* t[3] = {&crown, &left, &right};
    for (int i = 0; i < 3; i++) {
        std::string l = t[i]->substr(0, t[i]->find(','));
        size_t sp = l.find(' ');
        if (wrap_ && top_[i] && sp != std::string::npos) l[sp] = '\n';
        setText(label_[i], l);
        ui::visible(row_[i], on_ && !t[i]->empty());
    }
}

void Legends::colours(lv_color_t text, lv_color_t chipBg) {
    for (int i = 0; i < 3; i++) {
        ui::textColor(label_[i], text);
        if (lv_obj_get_style_bg_opa(row_[i], 0) != LV_OPA_TRANSP) ui::bgColor(row_[i], chipBg);
    }
}

void Legends::show(bool on) {
    on_ = on;
    for (int i = 0; i < 3; i++) ui::visible(row_[i], on && lv_label_get_text(label_[i])[0]);
}

bool crowdedButtons() {
    for (int at : {btn::gNames.crownAt, btn::gNames.leftAt, btn::gNames.rightAt}) {
        float c = cosf(at * 30.0f * (float)M_PI / 180.0f);
        if (c > 0.95f || (c > -0.7f && c < 0.7f)) return true;
    }
    return false;
}

void buttonDots(lv_obj_t* parent, int radius, int size, lv_color_t border) {
    const char* names[3] = {btn::gNames.crown, btn::gNames.left, btn::gNames.right};
    int at[3] = {btn::gNames.crownAt, btn::gNames.leftAt, btn::gNames.rightAt};
    for (int i = 0; i < 3; i++) {
        uint32_t col = btn::colourOf(names[i]);
        lv_obj_t* d = dot(parent, size, col ? lv_color_hex(col) : lv_color_hex(0xbbbbbb));
        lv_obj_set_style_border_color(d, border, 0);
        lv_obj_set_style_border_width(d, px(1) > 0 ? px(1) : 1, 0);
        float a = at[i] * 30.0f * (float)M_PI / 180.0f;
        lv_obj_align(d, LV_ALIGN_CENTER, (int)lroundf(px(radius) * sinf(a)), (int)lroundf(-px(radius) * cosf(a)));
    }
}

} // namespace kit
} // namespace lcd
