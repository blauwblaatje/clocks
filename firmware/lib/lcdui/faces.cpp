// The faces after LCD (design canvas https://claude.ai/artifact/8k6rBAAvEcTUUSTjRkgqAA,
// Rogier 2026-11-20): Big, Rings, Chronograph, High contrast, Flip, Nixie,
// each for the jam timer and the box timer roles, and the face factory.
// Sizes are design pixels (240 x 240); the canvas is drawn at 466, so its
// numbers are divided by 1.94 here.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "buttons.h"
#include "lcdui.h"
#include "lvset.h"
#include "scale.h"
#include "src/misc/lv_area_private.h" // lv_area_intersect

using ui::px;

namespace lcd {

using namespace kit;

namespace {

// A thin line under the clock for the undo / menu hold.
class HoldLine {
  public:
    HoldLine(lv_obj_t* parent, int y, int width, lv_color_t track) : w_(width) {
        track_ = plain(parent);
        lv_obj_set_size(track_, px(width), px(3));
        lv_obj_set_style_radius(track_, px(2), 0);
        lv_obj_set_style_bg_color(track_, track, 0);
        lv_obj_set_style_bg_opa(track_, LV_OPA_COVER, 0);
        lv_obj_align(track_, LV_ALIGN_TOP_MID, 0, px(y));
        line_ = plain(track_);
        lv_obj_set_size(line_, 0, px(3));
        lv_obj_set_style_radius(line_, px(2), 0);
        lv_obj_set_style_bg_opa(line_, LV_OPA_COVER, 0);
        ui::visible(track_, false);
    }
    void set(int permille, lv_color_t c) {
        ui::visible(track_, permille > 0);
        int w = px(w_) * permille / 1000;
        if (lv_obj_get_width(line_) != w) lv_obj_set_width(line_, w);
        ui::bgColor(line_, c);
    }

  private:
    lv_obj_t *track_, *line_;
    int w_;
};

// CRG's team colours as a thin edge round the disc: one half per jammer, the
// whole edge for one team's blockers (the face's team-colour option).
class TeamEdge {
  public:
    explicit TeamEdge(lv_obj_t* parent) : l_(parent, 118, 3), r_(parent, 118, 3) {
        l_.show(false);
        r_.show(false);
    }
    void set(const box::View& v, bool on) {
        bool jm = v.role == box::Role::Jammers;
        int32_t cl = jm ? v.colourLeft : v.colourTeam, cr = jm ? v.colourRight : v.colourTeam;
        l_.show(on && cl >= 0);
        r_.show(on && cr >= 0);
        if (on && cl >= 0) l_.arc(jm ? 182 : 180, jm ? 358 : 360, lv_color_hex(cl));
        if (on && cr >= 0) r_.arc(jm ? 2 : 0, jm ? 178 : 180, lv_color_hex(cr));
    }

  private:
    Ring l_, r_;
};

lv_color_t teamText(const box::View& v, int side, bool on, lv_color_t fallback) {
    int32_t c = side < 0 ? v.colourTeam : side == 0 ? v.colourLeft : v.colourRight;
    return on && c >= 0 ? lv_color_hex(c) : fallback;
}

std::string shortStatus(const std::string& s) { return s.substr(0, s.find(" \xc2\xb7 ")); }

std::string nextLine(const box::View& v, const BoxInfo& b) {
    if (b.next < 0) return "BOX EMPTY";
    const box::Slot& s = v.slots[b.next];
    std::string n = s.number.empty() || s.number == "?" ? "" : "#" + s.number + " ";
    std::string w = s.phase == box::Phase::Stand ? "STAND" : s.phase == box::Phase::Done ? "DONE"
                    : s.phase == box::Phase::Paused ? "PAUSED" : s.phase == box::Phase::Waiting ? "WAITING" : "SERVING";
    std::string o = n + w;
    int others = b.used - b.done - (s.phase == box::Phase::Done ? 0 : 1);
    if (others > 0) o += " \xc2\xb7 +" + std::to_string(others);
    if (b.done && s.phase != box::Phase::Done) o += " \xc2\xb7 " + std::to_string(b.done) + " DONE";
    return o;
}

LegendStyle chips(const jt::Fonts& f, lv_color_t text, lv_color_t bg) {
    LegendStyle st;
    st.font = f.b12;
    st.text = text;
    st.chip = true;
    st.chipBg = bg;
    st.chipOpa = LV_OPA_90;
    st.dotSize = 7;
    st.rTop = 80;
    st.rBottom = 98;
    st.edge = 104;
    st.wrapTop = true;
    return st;
}

const box::Slot* sideSlot(const box::View& v, int side) {
    for (auto& s : v.slots)
        if (s.side == side && !s.key.empty()) return &s;
    return nullptr;
}

// ======================= Big =======================

const float kBigWeight = 0.15f;

class BigJt : public Face {
  public:
    BigJt(lv_obj_t* parent, const jt::Fonts& f, bool outline) : outline_(outline) {
        root_ = faceRoot(parent, lv_color_black());
        band_ = new Ring(root_, 112, 11);
        band_->show(outline);
        buttonDots(root_, 111, 7, lv_color_hex(0x0b0b0b));
        state_ = text(root_, f.b17, lv_color_black());
        lv_obj_align(state_, LV_ALIGN_TOP_MID, 0, px(34));
        digits_ = new Stroke(root_, 80, kBigWeight);
        lv_obj_align(digits_->obj(), LV_ALIGN_TOP_MID, 0, px(84));
        hold_ = new HoldLine(root_, 168, 104, lv_color_hex(0x555555));
        period_ = text(root_, f.b17, lv_color_black());
        lv_obj_align(period_, LV_ALIGN_TOP_MID, 0, px(180));
        legends_ = new Legends(root_, chips(f, lv_color_white(), lv_color_hex(0x0b0b0b)));
    }
    ~BigJt() override {
        delete band_;
        delete digits_;
        delete hold_;
        delete legends_;
        lv_obj_delete(root_);
    }
    void show(const jt::View& v, uint32_t now) override {
        ui::visible(root_, v.screen == jt::Screen::Main);
        if (v.screen != jt::Screen::Main) return;
        JtInfo in = jtInfo(v);
        uint32_t t = toneColour(v.tone);
        if (!t) t = 0x9a9a9a;
        if (v.feedbackRed) t = kRed;
        lv_color_t tone = lv_color_hex(t), ink = outline_ ? tone : lv_color_hex(0x0b0b0b);
        ui::bgColor(root_, outline_ ? lv_color_black() : tone);
        if (outline_) {
            band_->track(0, 360, tone);
        }
        std::string word = in.word;
        if (in.nextJam && !in.jamNo.empty() && !in.alert) word += "  NEXT " + in.jamNo;
        setText(state_, word);
        ui::textColor(state_, ink);
        int h = Stroke::fitHeight(v.clock, kBigWeight, 196, 80);
        digits_->setHeight(h);
        place(digits_->obj(), LV_ALIGN_TOP_MID, 0, px(124 - h / 2));
        digits_->set(v.clock, ink);
        std::string bottom = in.pNo + "  " + in.pClock;
        if (!in.connected) bottom += "  " + in.status;
        setText(period_, bottom);
        ui::textColor(period_, ink);
        hold_->set(v.holdPermille, v.holdStage ? (outline_ ? lv_color_white() : lv_color_hex(0x3b3b3b)) : ink);
        std::string c, l, r;
        jtActions(v, c, l, r);
        legends_->colours(outline_ ? tone : lv_color_white(), lv_color_hex(outline_ ? 0x1a1a1a : 0x0b0b0b));
        legends_->set(c, l, r);
        bool due = legendsDue(now, v.holdPermille);
        legends_->show(due);
        ui::visible(state_, !due || in.alert);
    }

  private:
    bool outline_;
    lv_obj_t *root_, *state_, *period_;
    Ring* band_;
    Stroke* digits_;
    HoldLine* hold_;
    Legends* legends_;
};

class BigBox : public Face {
  public:
    BigBox(lv_obj_t* parent, const jt::Fonts& f, bool outline, const Options& o) : outline_(outline), o_(o) {
        root_ = faceRoot(parent, lv_color_black());
        for (int i = 0; i < 2; i++) {
            half_[i] = plain(root_);
            lv_obj_set_size(half_[i], px(120), px(240));
            lv_obj_align(half_[i], i == 0 ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_set_style_bg_opa(half_[i], LV_OPA_COVER, 0);
            lv_obj_t* col = column(half_[i], 6);
            lv_obj_align(col, LV_ALIGN_CENTER, i == 0 ? px(6) : -px(6), px(4));
            hTeam_[i] = text(col, f.b12, lv_color_black());
            hTime_[i] = new Stroke(col, 34, kBigWeight);
            hSub_[i] = text(col, f.b12, lv_color_black());
            hBand_[i] = new Ring(root_, 112, 11);
        }
        band_ = new Ring(root_, 112, 11);
        edge_ = new TeamEdge(root_);
        buttonDots(root_, 111, 7, lv_color_hex(0x0b0b0b));
        status_ = text(root_, f.b12, lv_color_black());
        lv_obj_align(status_, LV_ALIGN_TOP_MID, 0, px(38));
        digits_ = new Stroke(root_, 80, kBigWeight);
        lv_obj_align(digits_->obj(), LV_ALIGN_TOP_MID, 0, px(84));
        hold_ = new HoldLine(root_, 168, 104, lv_color_hex(0x555555));
        sub_ = text(root_, f.b17, lv_color_black());
        lv_obj_align(sub_, LV_ALIGN_TOP_MID, 0, px(180));
        legends_ = new Legends(root_, chips(f, lv_color_white(), lv_color_hex(0x0b0b0b)));
    }
    ~BigBox() override {
        for (int i = 0; i < 2; i++) {
            delete hTime_[i];
            delete hBand_[i];
        }
        delete band_;
        delete edge_;
        delete digits_;
        delete hold_;
        delete legends_;
        lv_obj_delete(root_);
    }
    void show(const box::View& v, uint32_t now) override {
        ui::visible(root_, v.screen == box::Screen::Main);
        if (v.screen != box::Screen::Main) return;
        BoxInfo b = boxInfo(v);
        bool jm = v.role == box::Role::Jammers;
        lv_color_t dark = lv_color_hex(0x0b0b0b);
        for (int i = 0; i < 2; i++) {
            ui::visible(half_[i], jm);
            hBand_[i]->show(jm && outline_);
        }
        ui::visible(digits_->obj(), !jm);
        ui::visible(sub_, !jm);
        band_->show(!jm && outline_);
        lv_color_t ink = dark;
        if (jm) {
            ui::bgColor(root_, lv_color_black());
            for (int i = 0; i < 2; i++) {
                const box::Slot* s = sideSlot(v, i);
                lv_color_t tone = s ? lv_color_hex(phaseColour(s->phase)) : lv_color_hex(0x3a3a3a);
                lv_color_t hink = outline_ ? (s ? tone : lv_color_hex(0x777777)) : (s ? dark : lv_color_hex(0x9a9a9a));
                ui::bgColor(half_[i], outline_ ? lv_color_black() : tone);
                if (outline_) hBand_[i]->arc(i == 0 ? 182 : 2, i == 0 ? 358 : 178, tone);
                setText(hTeam_[i], upper(i == 0 ? v.teamLeft : v.teamRight));
                ui::textColor(hTeam_[i], teamText(v, i, o_.teamColours && outline_, hink));
                hTime_[i]->set(s ? s->clock : "-:--", hink);
                std::string sub = !s ? "EMPTY" : s->phase == box::Phase::Stand ? "STAND" : s->phase == box::Phase::Done ? "DONE"
                                                                                                                     : (s->number.empty() ? "" : "#" + s->number);
                setText(hSub_[i], sub);
                ui::textColor(hSub_[i], hink);
            }
            setText(status_, b.alert ? b.status : "");
            ui::textColor(status_, lv_color_white());
        } else {
            const box::Slot* s = b.next >= 0 ? &v.slots[b.next] : nullptr;
            lv_color_t tone = s ? lv_color_hex(phaseColour(s->phase)) : lv_color_hex(0x3a3a3a);
            ink = outline_ ? (s ? tone : lv_color_hex(0x9a9a9a)) : (s ? dark : lv_color_hex(0xbbbbbb));
            ui::bgColor(root_, outline_ ? lv_color_black() : tone);
            if (outline_) band_->track(0, 360, tone);
            std::string clock = s ? s->clock : "-:--";
            int h = Stroke::fitHeight(clock, kBigWeight, 196, 80);
            digits_->setHeight(h);
            place(digits_->obj(), LV_ALIGN_TOP_MID, 0, px(124 - h / 2));
            digits_->set(clock, ink);
            setText(sub_, nextLine(v, b));
            ui::textColor(sub_, ink);
            setText(status_, b.alert ? b.status : shortStatus(b.status));
            ui::textColor(status_, b.alert ? (outline_ ? lv_color_hex(kRed) : ink) : teamText(v, -1, o_.teamColours, ink));
        }
        edge_->set(v, o_.teamColours);
        hold_->set(v.holdPermille, lv_color_white());
        std::string c, l, r;
        boxActions(v, c, l, r);
        legends_->set(c, l, r);
        bool due = legendsDue(now, v.holdPermille);
        legends_->show(due);
        ui::visible(status_, !due || b.alert); // the top legends take its place
    }

  private:
    bool outline_;
    Options o_;
    lv_obj_t *root_, *status_, *sub_;
    lv_obj_t *half_[2], *hTeam_[2], *hSub_[2];
    Stroke* hTime_[2];
    Ring *hBand_[2], *band_;
    TeamEdge* edge_;
    Stroke* digits_;
    HoldLine* hold_;
    Legends* legends_;
};

// ======================= Rings =======================

const int kSpan = 324; // a full ring; the rest of the circle, left of 12, holds its label
const float kRingsWeight = 0.13f;

// A label at a clock angle and radius (centre-aligned).
void atAngle(lv_obj_t* o, int radius, float deg) {
    float a = deg * (float)M_PI / 180;
    lv_obj_align(o, LV_ALIGN_CENTER, (int)lroundf(px(radius) * sinf(a)), (int)lroundf(-px(radius) * cosf(a)));
}

lv_color_t trackOf(lv_color_t c) { return lv_color_mix(c, lv_color_black(), 44); }

class RingsJt : public Face {
  public:
    RingsJt(lv_obj_t* parent, const jt::Fonts& f) {
        root_ = faceRoot(parent, lv_color_black());
        outer_ = new Ring(root_, 106, 14);
        inner_ = new Ring(root_, 86, 6);
        jLabel_ = text(root_, f.b12, lv_color_white());
        atAngle(jLabel_, 106, 342);
        pLabel_ = text(root_, f.b12, lv_color_white());
        atAngle(pLabel_, 86, 342);
        buttonDots(root_, 118, 6, lv_color_black());
        lv_obj_t* col = column(root_, 3);
        lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
        head_ = text(col, f.b12, lv_color_hex(0x9a9a9a));
        digits_ = new Stroke(col, 46, kRingsWeight);
        sub_ = text(col, f.b12, lv_color_white());
        hold_ = new HoldLine(root_, 172, 80, lv_color_hex(0x333333));
        legends_ = new Legends(root_, chips(f, lv_color_white(), lv_color_hex(0x1a1a1a)));
    }
    ~RingsJt() override {
        delete outer_;
        delete inner_;
        delete digits_;
        delete hold_;
        delete legends_;
        lv_obj_delete(root_);
    }
    void show(const jt::View& v, uint32_t now) override {
        ui::visible(root_, v.screen == jt::Screen::Main);
        if (v.screen != jt::Screen::Main) return;
        JtInfo in = jtInfo(v);
        uint32_t t = toneColour(v.tone);
        if (!t) t = 0x9a9a9a;
        lv_color_t tone = lv_color_hex(v.feedbackRed ? kRed : t), per = lv_color_hex(0x8fb8ff);
        outer_->track(0, kSpan, trackOf(tone));
        outer_->arc(0, kSpan * (v.feedbackRed ? 1000 : in.leftPermille) / 1000, tone);
        inner_->show(v.periodPermille >= 0);
        inner_->track(0, kSpan, trackOf(per));
        inner_->arc(0, kSpan * (v.periodPermille < 0 ? 0 : v.periodPermille) / 1000, per);
        setText(jLabel_, in.jamNo.empty() ? "" : "J" + in.jamNo);
        ui::textColor(jLabel_, tone);
        setText(pLabel_, in.pNo);
        ui::textColor(pLabel_, per);
        std::string head = in.word;
        if (in.nextJam && !in.jamNo.empty() && !in.alert) head += "  NEXT " + in.jamNo;
        setText(head_, head);
        ui::textColor(head_, in.alert ? lv_color_hex(kRed) : lv_color_hex(0x9a9a9a));
        digits_->setHeight(Stroke::fitHeight(v.clock, kRingsWeight, 140, 46));
        digits_->set(v.clock, tone);
        setText(sub_, in.pNo + "  " + in.pClock + (in.connected ? "" : "  " + in.status));
        ui::textColor(sub_, tone);
        hold_->set(v.holdPermille, v.holdStage ? lv_color_hex(kInter) : tone);
        std::string c, l, r;
        jtActions(v, c, l, r);
        legends_->set(c, l, r);
        legends_->show(legendsDue(now, v.holdPermille));
    }

  private:
    lv_obj_t *root_, *jLabel_, *pLabel_, *head_, *sub_;
    Ring *outer_, *inner_;
    Stroke* digits_;
    HoldLine* hold_;
    Legends* legends_;
};

class RingsBox : public Face {
  public:
    RingsBox(lv_obj_t* parent, const jt::Fonts& f, const Options& o) : o_(o) {
        root_ = faceRoot(parent, lv_color_black());
        static const int R[3] = {107, 89, 71};
        for (int i = 0; i < 3; i++) {
            ring_[i] = new Ring(root_, R[i], 12);
            num_[i] = text(root_, f.b12, lv_color_white());
            atAngle(num_[i], R[i], 342);
        }
        for (int i = 0; i < 2; i++) half_[i] = new Ring(root_, 105, 15);
        edge_ = new TeamEdge(root_);
        buttonDots(root_, 118, 6, lv_color_black());
        one_ = column(root_, 3);
        lv_obj_align(one_, LV_ALIGN_CENTER, 0, 0);
        head_ = text(one_, f.b12, lv_color_hex(0x9a9a9a));
        digits_ = new Stroke(one_, 44, kRingsWeight);
        sub_ = text(one_, f.b12, lv_color_white());
        for (int i = 0; i < 2; i++) {
            side_[i] = column(root_, 5);
            lv_obj_align(side_[i], LV_ALIGN_CENTER, i == 0 ? -px(38) : px(38), 0);
            sTeam_[i] = text(side_[i], f.b12, lv_color_hex(0x9a9a9a));
            sTime_[i] = new Stroke(side_[i], 26, kRingsWeight);
            sSub_[i] = text(side_[i], f.b12, lv_color_white());
        }
        divider_ = plain(root_);
        lv_obj_set_size(divider_, px(1) > 0 ? px(1) : 1, px(86));
        lv_obj_set_style_bg_color(divider_, lv_color_hex(0x2a2a2a), 0);
        lv_obj_set_style_bg_opa(divider_, LV_OPA_COVER, 0);
        lv_obj_center(divider_);
        status_ = text(root_, f.b12, lv_color_hex(0x9a9a9a));
        lv_obj_align(status_, LV_ALIGN_TOP_MID, 0, px(62));
        hold_ = new HoldLine(root_, 172, 80, lv_color_hex(0x333333));
        legends_ = new Legends(root_, chips(f, lv_color_white(), lv_color_hex(0x1a1a1a)));
    }
    ~RingsBox() override {
        for (auto* r : ring_) delete r;
        for (auto* h : half_) delete h;
        for (auto* s : sTime_) delete s;
        delete edge_;
        delete digits_;
        delete hold_;
        delete legends_;
        lv_obj_delete(root_);
    }
    void show(const box::View& v, uint32_t now) override {
        ui::visible(root_, v.screen == box::Screen::Main);
        if (v.screen != box::Screen::Main) return;
        BoxInfo b = boxInfo(v);
        bool jm = v.role == box::Role::Jammers;
        lv_color_t grey = lv_color_hex(0x5a5a5a);
        for (int i = 0; i < 3; i++) {
            ring_[i]->show(!jm);
            ui::visible(num_[i], !jm);
        }
        for (int i = 0; i < 2; i++) {
            half_[i]->show(jm);
            ui::visible(side_[i], jm);
        }
        ui::visible(one_, !jm);
        ui::visible(divider_, jm);
        if (jm) {
            for (int i = 0; i < 2; i++) {
                const box::Slot* s = sideSlot(v, i);
                lv_color_t c = s ? lv_color_hex(phaseColour(s->phase)) : grey;
                int sec = s ? seconds(s->clock) : 0, span = 164 * (sec < 0 ? 0 : sec > 30 ? 30 : sec) / 30;
                half_[i]->track(i == 0 ? 188 : 8, i == 0 ? 352 : 172, trackOf(c));
                if (i == 0) half_[i]->arc(352 - span, 352, c);
                else half_[i]->arc(8, 8 + span, c);
                setText(sTeam_[i], upper(i == 0 ? v.teamLeft : v.teamRight));
                ui::textColor(sTeam_[i], teamText(v, i, o_.teamColours, lv_color_hex(0x9a9a9a)));
                sTime_[i]->set(s ? s->clock : "-", s ? c : lv_color_hex(0x6a6a6a));
                std::string sub = !s ? "EMPTY" : s->phase == box::Phase::Stand ? "STAND" : s->phase == box::Phase::Done ? "DONE"
                                                                                                                     : (s->number.empty() ? "SERVING" : "#" + s->number);
                setText(sSub_[i], sub);
                ui::textColor(sSub_[i], s ? c : lv_color_hex(0x6a6a6a));
            }
            setText(status_, b.alert ? b.status : shortStatus(b.status));
        } else {
            int n = v.slots.size() > 3 ? 3 : (int)v.slots.size();
            for (int i = 0; i < 3; i++) {
                const box::Slot* s = i < n ? &v.slots[i] : nullptr;
                lv_color_t c = s ? lv_color_hex(phaseColour(s->phase)) : grey;
                int sec = s ? seconds(s->clock) : 0;
                ring_[i]->track(0, kSpan, trackOf(c));
                ring_[i]->arc(0, kSpan * (sec < 0 ? 0 : sec > 30 ? 30 : sec) / 30, c);
                setText(num_[i], s ? (s->number.empty() || s->number == "?" ? "?" : s->number) : "-");
                ui::textColor(num_[i], c);
            }
            const box::Slot* s = b.next >= 0 ? &v.slots[b.next] : nullptr;
            lv_color_t c = s ? lv_color_hex(phaseColour(s->phase)) : lv_color_hex(0x6a6a6a);
            setText(head_, b.alert ? b.status : shortStatus(b.status));
            ui::textColor(head_, b.alert ? lv_color_hex(kRed) : teamText(v, -1, o_.teamColours, lv_color_hex(0x9a9a9a)));
            digits_->set(s ? s->clock : "-:--", c);
            setText(sub_, nextLine(v, b));
            ui::textColor(sub_, c);
        }
        edge_->set(v, o_.teamColours);
        hold_->set(v.holdPermille, lv_color_white());
        std::string c, l, r;
        boxActions(v, c, l, r);
        legends_->set(c, l, r);
        bool due = legendsDue(now, v.holdPermille);
        legends_->show(due);
        ui::visible(status_, jm && !due); // the top legends take its place
    }

  private:
    Options o_;
    lv_obj_t *root_, *num_[3], *one_, *head_, *sub_, *side_[2], *sTeam_[2], *sSub_[2], *divider_, *status_;
    Ring *ring_[3], *half_[2];
    Stroke *digits_, *sTime_[2];
    TeamEdge* edge_;
    HoldLine* hold_;
    Legends* legends_;
};

// ======================= Chronograph =======================

// The dial: ticks and numbers for one turn of the clock (T seconds), two
// subdials. Drawn once; redrawn only when T or a name changes.
class Dial {
  public:
    Dial(lv_obj_t* parent, const jt::Fonts& f) : f_(f) {
        obj_ = plain(parent);
        lv_obj_set_user_data(obj_, this);
        lv_obj_set_size(obj_, px(240), px(240));
        lv_obj_center(obj_);
        lv_obj_add_event_cb(obj_, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
    }
    void set(int totalS, bool countsDown, bool subs) {
        if (totalS == t_ && countsDown == down_ && subs == subs_) return;
        t_ = totalS;
        down_ = countsDown;
        subs_ = subs;
        lv_obj_invalidate(obj_);
    }

  private:
    static void onDraw(lv_event_t* e) {
        lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
        Dial* d = (Dial*)lv_obj_get_user_data(o);
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t c;
        lv_obj_get_coords(o, &c);
        float cx = (c.x1 + c.x2) / 2.0f, cy = (c.y1 + c.y2) / 2.0f;
        int T = d->t_ > 0 ? d->t_ : 120;
        auto line = [&](float x0, float y0, float r0, float r1, float deg, int w, lv_color_t col) {
            float a = deg * (float)M_PI / 180;
            lv_draw_line_dsc_t ld;
            lv_draw_line_dsc_init(&ld);
            ld.width = w;
            ld.color = col;
            ld.p1.x = x0 + r0 * sinf(a);
            ld.p1.y = y0 - r0 * cosf(a);
            ld.p2.x = x0 + r1 * sinf(a);
            ld.p2.y = y0 - r1 * cosf(a);
            lv_draw_line(layer, &ld);
        };
        // ticks: 120 for a jam (each second), 60 otherwise; a longer one every 10th
        int n = T == 120 ? 120 : 60;
        int wMinor = px(1) > 0 ? px(1) : 1, wMajor = px(2) > 1 ? px(2) : 2;
        for (int i = 0; i < n; i++) {
            bool major = i % 10 == 0;
            line(cx, cy, px(major ? 97 : 102), px(106), i * 360.0f / n, major ? wMajor : wMinor,
                 lv_color_hex(major ? 0xd8dde2 : 0x5d646b));
        }
        // numbers: the clock's own reading at that point of the turn
        int step = T <= 30 ? 5 : T <= 60 ? 10 : T <= 180 ? 30 : T / 4;
        lv_draw_label_dsc_t td;
        lv_draw_label_dsc_init(&td);
        td.font = T <= 60 ? d->f_.b17 : d->f_.b12;
        td.color = lv_color_hex(0xe8ecef);
        td.align = LV_TEXT_ALIGN_CENTER;
        char buf[16];
        for (int e2 = 0; e2 < T; e2 += step) {
            int val = d->down_ ? T - e2 : e2;
            if (T <= 60) snprintf(buf, sizeof buf, "%d", val);
            else if (T <= 180) snprintf(buf, sizeof buf, "%d:%02d", val / 60, val % 60);
            else snprintf(buf, sizeof buf, "%d", val / 60);
            float a = e2 * 2 * (float)M_PI / T, r = px(84);
            int x = (int)(cx + r * sinf(a)), y = (int)(cy - r * cosf(a)), hw = px(20), hh = px(8);
            lv_area_t q = {x - hw, y - hh, x + hw, y + hh};
            td.text = buf;
            td.text_local = 1;
            lv_draw_label(layer, &td, &q);
        }
        if (!d->subs_) return;
        for (int s = 0; s < 2; s++) {
            float sx = cx + (s == 0 ? -px(40) : px(40));
            lv_draw_arc_dsc_t ad;
            lv_draw_arc_dsc_init(&ad);
            ad.center.x = (int32_t)sx;
            ad.center.y = (int32_t)cy;
            ad.radius = (uint16_t)px(22);
            ad.width = px(22);
            ad.start_angle = 0;
            ad.end_angle = 360;
            ad.color = lv_color_hex(0x0a0b0c);
            lv_draw_arc(layer, &ad);
            ad.width = px(1) > 0 ? px(1) : 1;
            ad.color = lv_color_hex(0x3a3f45);
            lv_draw_arc(layer, &ad);
            for (int i = 0; i < 12; i++) line(sx, cy, px(i % 4 == 0 ? 16 : 18), px(21), i * 30.0f, wMinor, lv_color_hex(0x8a9198));
        }
    }
    lv_obj_t* obj_;
    jt::Fonts f_;
    int t_ = -1;
    bool down_ = true, subs_ = false;
};

// The hands: each from a centre (offset from the face's centre) at a clock
// angle. Only the area of a hand that moved is redrawn.
struct Hand {
    int ox = 0, oy = 0;    // centre offset, display px
    float deg = 0;
    int len = 0, tail = 0; // display px
    int width = 2, tip = 0; // tip: radius of a round end with a label
    lv_color_t c;
    std::string label;
    bool operator==(const Hand& o) const {
        return ox == o.ox && oy == o.oy && fabsf(deg - o.deg) < 0.05f && len == o.len && tail == o.tail && width == o.width &&
               tip == o.tip && lv_color_eq(c, o.c) && label == o.label;
    }
};

class Hands {
  public:
    Hands(lv_obj_t* parent, const jt::Fonts& f) : f_(f) {
        obj_ = plain(parent);
        lv_obj_set_user_data(obj_, this);
        lv_obj_set_size(obj_, px(240), px(240));
        lv_obj_center(obj_);
        lv_obj_add_event_cb(obj_, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
    }
    void set(const std::vector<Hand>& h) {
        size_t n = h.size() > hands_.size() ? h.size() : hands_.size();
        for (size_t i = 0; i < n; i++) {
            bool had = i < hands_.size(), has = i < h.size();
            if (had && has && hands_[i] == h[i]) continue;
            if (had) invalidate(hands_[i]);
            if (has) invalidate(h[i]);
        }
        hands_ = h;
    }

  private:
    void invalidate(const Hand& h) {
        lv_area_t c;
        lv_obj_get_coords(obj_, &c);
        float cx = (c.x1 + c.x2) / 2.0f + h.ox, cy = (c.y1 + c.y2) / 2.0f + h.oy, a = h.deg * (float)M_PI / 180;
        float x1 = cx + h.len * sinf(a), y1 = cy - h.len * cosf(a), x0 = cx - h.tail * sinf(a), y0 = cy + h.tail * cosf(a);
        int m = h.width + h.tip + px(4);
        lv_area_t q = {(int32_t)fminf(x0, x1) - m, (int32_t)fminf(y0, y1) - m, (int32_t)fmaxf(x0, x1) + m, (int32_t)fmaxf(y0, y1) + m};
        lv_obj_invalidate_area(obj_, &q);
    }
    static void onDraw(lv_event_t* e) {
        lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
        Hands* d = (Hands*)lv_obj_get_user_data(o);
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t c;
        lv_obj_get_coords(o, &c);
        for (const Hand& h : d->hands_) {
            float cx = (c.x1 + c.x2) / 2.0f + h.ox, cy = (c.y1 + c.y2) / 2.0f + h.oy, a = h.deg * (float)M_PI / 180;
            lv_draw_line_dsc_t ld;
            lv_draw_line_dsc_init(&ld);
            ld.width = h.width;
            ld.color = h.c;
            ld.round_start = 1;
            ld.round_end = 1;
            ld.p1.x = cx - h.tail * sinf(a);
            ld.p1.y = cy + h.tail * cosf(a);
            ld.p2.x = cx + h.len * sinf(a);
            ld.p2.y = cy - h.len * cosf(a);
            lv_draw_line(layer, &ld);
            if (h.tip > 0) {
                lv_draw_rect_dsc_t rd;
                lv_draw_rect_dsc_init(&rd);
                rd.bg_color = h.c;
                rd.radius = LV_RADIUS_CIRCLE;
                lv_area_t q = {(int32_t)(ld.p2.x - h.tip), (int32_t)(ld.p2.y - h.tip), (int32_t)(ld.p2.x + h.tip), (int32_t)(ld.p2.y + h.tip)};
                lv_draw_rect(layer, &rd, &q);
                lv_draw_label_dsc_t td;
                lv_draw_label_dsc_init(&td);
                td.font = d->f_.b12;
                td.color = lv_color_hex(0x0b0b0b);
                td.align = LV_TEXT_ALIGN_CENTER;
                td.text = h.label.c_str();
                td.text_local = 1;
                lv_area_t tq = {q.x1 - px(4), (int32_t)ld.p2.y - px(7), q.x2 + px(4), (int32_t)ld.p2.y + px(7)};
                lv_draw_label(layer, &td, &tq);
            }
            // cap
            lv_draw_rect_dsc_t cd;
            lv_draw_rect_dsc_init(&cd);
            cd.bg_color = h.tip ? lv_color_hex(0xd8dde2) : h.c;
            cd.radius = LV_RADIUS_CIRCLE;
            int r = h.ox == 0 ? px(5) : px(2);
            lv_area_t cq = {(int32_t)cx - r, (int32_t)cy - r, (int32_t)cx + r, (int32_t)cy + r};
            lv_draw_rect(layer, &cd, &cq);
        }
    }
    lv_obj_t* obj_;
    jt::Fonts f_;
    std::vector<Hand> hands_;
};

class ChronoBase : public Face {
  protected:
    ChronoBase(lv_obj_t* parent, const jt::Fonts& f) {
        root_ = faceRoot(parent, lv_color_hex(0x111416));
        lv_obj_set_style_bg_grad_color(root_, lv_color_hex(0x07080a), 0);
        lv_obj_set_style_bg_grad_dir(root_, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_border_color(root_, lv_color_hex(0x3a3f45), 0);
        lv_obj_set_style_border_width(root_, px(2), 0);
        sweep_ = new Ring(root_, 101, 5);
        sweep_->rounded(false);
        dial_ = new Dial(root_, f);
        name_[0] = text(root_, f.b12, lv_color_hex(0xaab1b8));
        name_[1] = text(root_, f.b12, lv_color_hex(0xaab1b8));
        lv_obj_align(name_[0], LV_ALIGN_CENTER, -px(40), -px(30));
        lv_obj_align(name_[1], LV_ALIGN_CENTER, px(40), -px(30));
        state_ = text(root_, f.b12, lv_color_white());
        lv_obj_align(state_, LV_ALIGN_TOP_MID, 0, px(56));
        window_ = row(root_, 4, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(window_, lv_color_hex(0x050606), 0);
        lv_obj_set_style_bg_opa(window_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(window_, lv_color_hex(0x3a3f45), 0);
        lv_obj_set_style_border_width(window_, px(1) > 0 ? px(1) : 1, 0);
        lv_obj_set_style_radius(window_, px(3), 0);
        lv_obj_set_style_pad_hor(window_, px(6), 0);
        lv_obj_set_style_pad_ver(window_, px(3), 0);
        lv_obj_align(window_, LV_ALIGN_TOP_MID, 0, px(156));
        win_ = new Stroke(window_, 20, 0.13f);
        winSub_ = text(window_, f.b12, lv_color_white());
        hands_ = new Hands(root_, f);
        hold_ = new HoldLine(root_, 186, 60, lv_color_hex(0x333333));
        buttonDots(root_, 113, 6, lv_color_black());
        legends_ = new Legends(root_, chips(f, lv_color_white(), lv_color_hex(0x1a1a1a)));
    }
    ~ChronoBase() override {
        delete sweep_;
        delete dial_;
        delete win_;
        delete hands_;
        delete hold_;
        delete legends_;
        lv_obj_delete(root_);
    }
    Hand hand(float deg, int len, lv_color_t c, int width = 2, int tail = 14) {
        Hand h;
        h.deg = deg;
        h.len = px(len);
        h.tail = px(tail);
        h.width = px(width) > 1 ? px(width) : 2;
        h.c = c;
        return h;
    }
    lv_obj_t *root_, *name_[2], *state_, *window_, *winSub_;
    Ring* sweep_;
    Dial* dial_;
    Stroke* win_;
    Hands* hands_;
    HoldLine* hold_;
    Legends* legends_;
};

class ChronoJt : public ChronoBase {
  public:
    ChronoJt(lv_obj_t* parent, const jt::Fonts& f) : ChronoBase(parent, f) {}
    void show(const jt::View& v, uint32_t now) override {
        ui::visible(root_, v.screen == jt::Screen::Main);
        if (v.screen != jt::Screen::Main) return;
        JtInfo in = jtInfo(v);
        uint32_t t = toneColour(v.tone);
        if (!t) t = 0x9a9a9a;
        lv_color_t tone = lv_color_hex(v.feedbackRed ? kRed : t);
        int T = v.ringTotalMs > 0 ? (int)(v.ringTotalMs / 1000) : 120;
        dial_->set(T, !v.ringCountsUp, true);
        // elapsed part of the turn: the hand sweeps clockwise from 12
        int used = 1000 - in.leftPermille;
        if (v.tone == jt::Tone::Idle) used = 0;
        float deg = used * 0.36f;
        sweep_->track(0, 0, tone);
        sweep_->arc(0, (int)deg, lv_color_mix(tone, lv_color_black(), 72));
        std::vector<Hand> hs;
        hs.push_back(hand(deg, 101, tone));
        Hand p = hand(v.periodPermille < 0 ? 0 : (1000 - v.periodPermille) * 0.36f, 17, lv_color_hex(0x8fb8ff), 2, 0);
        p.ox = -px(40);
        hs.push_back(p);
        int jam = atoi(in.jamNo.c_str());
        Hand j = hand(jam % 30 * 12.0f, 17, lv_color_hex(0xd8dde2), 2, 0);
        j.ox = px(40);
        hs.push_back(j);
        hands_->set(hs);
        setText(name_[0], in.pNo);
        setText(name_[1], in.jamNo.empty() ? "" : (in.nextJam ? "NEXT " : "JAM ") + in.jamNo);
        setText(state_, in.word);
        ui::textColor(state_, in.alert ? lv_color_hex(kRed) : tone);
        win_->set(v.clock, lv_color_hex(0xf2f4f6));
        setText(winSub_, in.connected ? "P " + in.pClock : in.status);
        ui::textColor(winSub_, tone);
        hold_->set(v.holdPermille, v.holdStage ? lv_color_hex(kInter) : tone);
        std::string c, l, r;
        jtActions(v, c, l, r);
        legends_->set(c, l, r);
        bool due = legendsDue(now, v.holdPermille);
        legends_->show(due);
        ui::visible(state_, !due || in.alert); // the top legends take its place
    }
};

class ChronoBox : public ChronoBase {
  public:
    ChronoBox(lv_obj_t* parent, const jt::Fonts& f, const Options& o) : ChronoBase(parent, f), o_(o) {
        edge_ = new TeamEdge(root_);
        lv_obj_align(state_, LV_ALIGN_TOP_MID, 0, px(66));
    }
    ~ChronoBox() override { delete edge_; }
    void show(const box::View& v, uint32_t now) override {
        ui::visible(root_, v.screen == box::Screen::Main);
        if (v.screen != box::Screen::Main) return;
        BoxInfo b = boxInfo(v);
        bool jm = v.role == box::Role::Jammers;
        dial_->set(30, true, false);
        sweep_->arc(0, 0, lv_color_black());
        std::vector<Hand> hs;
        static const int len[3] = {91, 75, 60};
        int k = 0;
        for (size_t i = 0; i < v.slots.size() && k < 3; i++) {
            const box::Slot& s = v.slots[i];
            if (s.key.empty()) continue;
            int sec = seconds(s.clock);
            if (sec < 0) sec = 0;
            if (sec > 30) sec = 30;
            lv_color_t c = lv_color_hex(phaseColour(s.phase));
            if (jm && o_.teamColours) c = teamText(v, s.side, true, c);
            Hand h = hand((30 - sec) * 12.0f, len[k], c, 2, 10);
            h.tip = px(8);
            h.label = s.number.empty() || s.number == "?" ? (jm ? (s.side == 0 ? "L" : "R") : "?") : s.number;
            hs.push_back(h);
            k++;
        }
        hands_->set(hs);
        setText(name_[0], "");
        setText(name_[1], "");
        setText(state_, shortStatus(b.status));
        ui::textColor(state_, b.alert ? lv_color_hex(kRed) : teamText(v, -1, o_.teamColours, lv_color_hex(0xaab1b8)));
        const box::Slot* s = b.next >= 0 ? &v.slots[b.next] : nullptr;
        lv_color_t c = s ? lv_color_hex(phaseColour(s->phase)) : lv_color_hex(0x6a6a6a);
        win_->set(s ? s->clock : "-:--", lv_color_hex(0xf2f4f6));
        std::string sub = nextLine(v, b);
        setText(winSub_, s ? sub.substr(0, sub.find(" \xc2\xb7 ")) : "");
        ui::textColor(winSub_, c);
        edge_->set(v, o_.teamColours);
        hold_->set(v.holdPermille, lv_color_white());
        std::string bc, bl, br;
        boxActions(v, bc, bl, br);
        legends_->set(bc, bl, br);
        bool due = legendsDue(now, v.holdPermille);
        legends_->show(due);
        ui::visible(state_, !due || b.alert); // the top legends take its place
    }

  private:
    Options o_;
    TeamEdge* edge_;
};

// ======================= High contrast =======================

const float kContrastWeight = 0.17f;

// White on black; states told apart by shape: jam = filled label and solid
// rim, lineup = outlined label and dashed rim, timeout = double outline and
// dotted rim. The button's initial sits in its dot.
LegendStyle contrastLegends(const jt::Fonts& f) {
    LegendStyle st;
    st.font = f.b12;
    st.text = lv_color_white();
    st.dotSize = 13;
    st.letter = true;
    st.dotBorder = lv_color_white();
    st.dotBorderWidth = 1;
    st.chipBg = lv_color_black();
    st.rTop = 72;
    st.rBottom = 92;
    st.edge = 104;
    st.wrapTop = true;
    return st;
}

lv_obj_t* pill(lv_obj_t* parent, const jt::Fonts& f) {
    lv_obj_t* p = text(parent, f.b17, lv_color_white());
    lv_obj_set_style_pad_hor(p, px(9), 0);
    lv_obj_set_style_pad_ver(p, px(2), 0);
    lv_obj_set_style_radius(p, px(11), 0);
    lv_obj_set_style_border_color(p, lv_color_white(), 0);
    lv_obj_set_style_border_width(p, px(2), 0);
    lv_obj_set_style_outline_color(p, lv_color_white(), 0);
    lv_obj_set_style_outline_pad(p, px(2), 0);
    lv_obj_set_style_bg_color(p, lv_color_white(), 0);
    return p;
}

enum class Shape : uint8_t { Filled, Outline, Double, Plain };
void shapeOf(lv_obj_t* o, Shape s) {
    ui::bgOpa(o, s == Shape::Filled ? LV_OPA_COVER : LV_OPA_TRANSP);
    ui::textColor(o, s == Shape::Filled ? lv_color_black() : lv_color_white());
    int bw = s == Shape::Plain ? 0 : px(2), ow = s == Shape::Double ? px(2) : 0;
    if (lv_obj_get_style_border_width(o, 0) != bw) lv_obj_set_style_border_width(o, bw, 0);
    if (lv_obj_get_style_outline_width(o, 0) != ow) lv_obj_set_style_outline_width(o, ow, 0);
}

class ContrastJt : public Face {
  public:
    ContrastJt(lv_obj_t* parent, const jt::Fonts& f) {
        root_ = faceRoot(parent, lv_color_black());
        rim_ = new Dashes(root_, 112, 8);
        pill_ = pill(root_, f);
        lv_obj_align(pill_, LV_ALIGN_TOP_MID, 0, px(20));
        digits_ = new Stroke(root_, 58, kContrastWeight);
        lv_obj_align(digits_->obj(), LV_ALIGN_TOP_MID, 0, px(80));
        hold_ = new HoldLine(root_, 144, 100, lv_color_hex(0x444444));
        period_ = text(root_, f.b17, lv_color_white());
        lv_obj_align(period_, LV_ALIGN_TOP_MID, 0, px(152));
        legends_ = new Legends(root_, contrastLegends(f));
    }
    ~ContrastJt() override {
        delete rim_;
        delete digits_;
        delete hold_;
        delete legends_;
        lv_obj_delete(root_);
    }
    void show(const jt::View& v, uint32_t now) override {
        ui::visible(root_, v.screen == jt::Screen::Main);
        if (v.screen != jt::Screen::Main) return;
        JtInfo in = jtInfo(v);
        Shape s = v.tone == jt::Tone::Jam ? Shape::Filled : v.tone == jt::Tone::Timeout ? Shape::Double : Shape::Outline;
        Dashes::Style st = v.tone == jt::Tone::Lineup ? Dashes::Style::Dash : v.tone == jt::Tone::Timeout ? Dashes::Style::Dot
                                                                                                        : Dashes::Style::Solid;
        if (in.alert) s = (v.feedbackRed ? Shape::Filled : Shape::Double);
        shapeOf(pill_, s);
        setText(pill_, in.word); // short: the top of the circle is narrow
        rim_->set(v.tone == jt::Tone::Idle ? 0 : in.leftPermille, st, lv_color_white(), lv_color_hex(0x3a3a3a));
        digits_->setHeight(Stroke::fitHeight(v.clock, kContrastWeight, 180, 58));
        digits_->set(v.clock, lv_color_white());
        setText(period_, in.pNo + "  " + in.pClock + (in.connected ? "" : "  " + in.status));
        hold_->set(v.holdPermille, lv_color_white());
        std::string c, l, r;
        jtActions(v, c, l, r);
        legends_->set(c, l, r);
        legends_->show(!crowdedButtons() || legendsDue(now, v.holdPermille));
    }

  private:
    lv_obj_t *root_, *pill_, *period_;
    Dashes* rim_;
    Stroke* digits_;
    HoldLine* hold_;
    Legends* legends_;
};

class ContrastBox : public Face {
  public:
    ContrastBox(lv_obj_t* parent, const jt::Fonts& f) {
        root_ = faceRoot(parent, lv_color_black());
        rim_ = new Dashes(root_, 112, 8);
        pill_ = pill(root_, f);
        lv_obj_align(pill_, LV_ALIGN_TOP_MID, 0, px(20));
        for (int i = 0; i < 3; i++) {
            Row& r = rows_[i];
            r.box = pill(root_, f);
            lv_label_set_text(r.box, "");
            lv_obj_set_size(r.box, px(166), px(27));
            lv_obj_set_style_pad_all(r.box, 0, 0);
            lv_obj_set_style_radius(r.box, px(5), 0);
            lv_obj_align(r.box, LV_ALIGN_TOP_MID, 0, px(80 + i * 31));
            r.num = text(r.box, f.b17, lv_color_white());
            lv_obj_align(r.num, LV_ALIGN_LEFT_MID, px(6), 0);
            r.time = new Stroke(r.box, 18, kContrastWeight);
            lv_obj_align(r.time->obj(), LV_ALIGN_CENTER, px(4), 0);
            r.tag = text(r.box, f.b12, lv_color_white());
            lv_obj_align(r.tag, LV_ALIGN_RIGHT_MID, -px(6), 0);
        }
        for (int i = 0; i < 2; i++) {
            Half& h = halves_[i];
            h.box = pill(root_, f);
            lv_label_set_text(h.box, "");
            lv_obj_set_size(h.box, px(88), px(96));
            lv_obj_set_style_pad_all(h.box, 0, 0);
            lv_obj_set_style_radius(h.box, px(8), 0);
            lv_obj_align(h.box, LV_ALIGN_TOP_MID, i == 0 ? -px(47) : px(47), px(80));
            h.team = text(h.box, f.b12, lv_color_white());
            lv_obj_align(h.team, LV_ALIGN_TOP_MID, 0, px(8));
            h.time = new Stroke(h.box, 32, kContrastWeight);
            lv_obj_align(h.time->obj(), LV_ALIGN_CENTER, 0, 0);
            h.tag = text(h.box, f.b12, lv_color_white());
            lv_obj_align(h.tag, LV_ALIGN_BOTTOM_MID, 0, -px(8));
        }
        empty_ = text(root_, f.b17, lv_color_white());
        lv_obj_align(empty_, LV_ALIGN_TOP_MID, 0, px(112));
        lv_label_set_text(empty_, "BOX EMPTY");
        hold_ = new HoldLine(root_, 180, 100, lv_color_hex(0x444444));
        legends_ = new Legends(root_, contrastLegends(f));
    }
    ~ContrastBox() override {
        delete rim_;
        for (auto& r : rows_) delete r.time;
        for (auto& h : halves_) delete h.time;
        delete hold_;
        delete legends_;
        lv_obj_delete(root_);
    }
    void show(const box::View& v, uint32_t now) override {
        ui::visible(root_, v.screen == box::Screen::Main);
        if (v.screen != box::Screen::Main) return;
        BoxInfo b = boxInfo(v);
        bool jm = v.role == box::Role::Jammers;
        shapeOf(pill_, b.alert ? Shape::Double : Shape::Filled);
        setText(pill_, b.alert ? b.status : shortStatus(b.status));
        auto slotShape = [](const box::Slot* s) {
            return !s ? Shape::Plain : s->phase == box::Phase::Stand ? Shape::Filled : s->phase == box::Phase::Done ? Shape::Outline : Shape::Plain;
        };
        auto tagOf = [](const box::Slot* s) {
            return !s ? std::string("EMPTY") : s->phase == box::Phase::Stand ? "STAND" : s->phase == box::Phase::Done ? "DONE"
                                                : s->phase == box::Phase::Paused ? "PAUSED" : "";
        };
        for (int i = 0; i < 2; i++) ui::visible(halves_[i].box, jm);
        int n = v.slots.size() > 3 ? 3 : (int)v.slots.size();
        for (int i = 0; i < 3; i++) ui::visible(rows_[i].box, !jm && i < n);
        ui::visible(empty_, !jm && b.used == 0);
        if (jm) {
            for (int i = 0; i < 2; i++) {
                Half& h = halves_[i];
                const box::Slot* s = sideSlot(v, i);
                Shape sh = slotShape(s);
                if (sh == Shape::Plain) sh = Shape::Outline; // the two halves keep a frame
                shapeOf(h.box, sh);
                lv_color_t ink = sh == Shape::Filled ? lv_color_black() : lv_color_white();
                setText(h.team, upper(i == 0 ? v.teamLeft : v.teamRight));
                ui::textColor(h.team, ink);
                h.time->set(s ? s->clock : "-", ink);
                setText(h.tag, tagOf(s));
                ui::textColor(h.tag, ink);
            }
        } else {
            for (int i = 0; i < n; i++) {
                Row& r = rows_[i];
                const box::Slot* s = &v.slots[i];
                Shape sh = slotShape(s);
                shapeOf(r.box, sh);
                lv_color_t ink = sh == Shape::Filled ? lv_color_black() : lv_color_white();
                setText(r.num, s->number.empty() ? "" : "#" + s->number);
                ui::textColor(r.num, ink);
                r.time->set(s->clock, ink);
                setText(r.tag, tagOf(s));
                ui::textColor(r.tag, ink);
            }
        }
        const box::Slot* s = b.next >= 0 ? &v.slots[b.next] : nullptr;
        int sec = s ? seconds(s->clock) : 0;
        rim_->set(sec <= 0 ? 0 : sec * 1000 / 30, Dashes::Style::Solid, lv_color_white(), lv_color_hex(0x3a3a3a));
        hold_->set(v.holdPermille, lv_color_white());
        std::string c, l, r;
        boxActions(v, c, l, r);
        legends_->set(c, l, r);
        legends_->show(!crowdedButtons() || legendsDue(now, v.holdPermille));
    }

  private:
    struct Row {
        lv_obj_t *box, *num, *tag;
        Stroke* time;
    };
    struct Half {
        lv_obj_t *box, *team, *tag;
        Stroke* time;
    };
    lv_obj_t *root_, *pill_, *empty_;
    Dashes* rim_;
    Row rows_[3];
    Half halves_[2];
    HoldLine* hold_;
    Legends* legends_;
};

// ======================= Flip and Nixie =======================

// A row of digit tiles: split-flap cards (Flip) or glass tubes (Nixie), one
// per digit, the colon between them printed on the face. A changed digit's
// card flips: its top half folds down over 150 ms.
struct TileStyle {
    bool tube = false;
    float weight = 0.13f;
    lv_color_t ink = lv_color_hex(0xefeadf);
};

class Tiles {
  public:
    Tiles(lv_obj_t* parent, int w, int h, int digitH, int gap, const TileStyle& st) : w_(w), h_(h), dh_(digitH), st_(st) {
        row_ = row(parent, gap, LV_FLEX_ALIGN_CENTER);
    }
    ~Tiles() {
        for (auto& t : tiles_) delete t.digit;
    }
    lv_obj_t* obj() const { return row_; }
    void set(const std::string& text, lv_color_t ink, bool lit = true) {
        std::string shape;
        for (char ch : text) shape += ch == ':' ? ':' : '#';
        if (shape != shape_) rebuild(shape);
        for (size_t i = 0; i < text.size() && i < tiles_.size(); i++) {
            Tile& t = tiles_[i];
            if (text[i] == ':') {
                for (lv_obj_t* d : {t.dotA, t.dotB}) {
                    ui::bgColor(d, ink);
                    if (st_.tube) {
                        int sw = lit ? px(5) : 0;
                        if (lv_obj_get_style_shadow_width(d, 0) != sw) lv_obj_set_style_shadow_width(d, sw, 0);
                    }
                }
                continue;
            }
            std::string ch(1, text[i] == ' ' ? ' ' : text[i]);
            bool changed = !t.first && ch != t.ch;
            t.first = false;
            t.ch = ch;
            t.digit->set(ch, ink);
            if (st_.tube) t.digit->glowOn(lit);
            if (changed && !st_.tube) flip(t);
        }
    }

  private:
    struct Tile {
        lv_obj_t *box = nullptr, *flap = nullptr, *dotA = nullptr, *dotB = nullptr;
        Stroke* digit = nullptr;
        std::string ch;
        bool first = true;
    };
    void rebuild(const std::string& shape) {
        for (auto& t : tiles_) delete t.digit;
        tiles_.clear();
        lv_obj_clean(row_);
        shape_ = shape;
        for (char k : shape) {
            Tile t;
            if (k == ':') {
                t.box = column(row_, h_ / 5);
                t.dotA = dot(t.box, st_.tube ? 4 : 5, st_.ink);
                t.dotB = dot(t.box, st_.tube ? 4 : 5, st_.ink);
                if (st_.tube)
                    for (lv_obj_t* d : {t.dotA, t.dotB}) {
                        lv_obj_set_style_shadow_color(d, lv_color_hex(0xff6a00), 0);
                        lv_obj_set_style_shadow_spread(d, px(1), 0);
                    }
                tiles_.push_back(t);
                continue;
            }
            t.box = plain(row_);
            lv_obj_set_size(t.box, px(w_), px(h_));
            lv_obj_set_style_bg_opa(t.box, LV_OPA_COVER, 0);
            if (st_.tube) {
                lv_obj_set_style_radius(t.box, px(w_) / 2, 0);
                lv_obj_set_style_bg_color(t.box, lv_color_hex(0x1e1611), 0);
                lv_obj_set_style_bg_grad_color(t.box, lv_color_hex(0x110c09), 0);
                lv_obj_set_style_bg_grad_dir(t.box, LV_GRAD_DIR_HOR, 0);
                lv_obj_set_style_border_color(t.box, lv_color_hex(0x5a4636), 0);
                lv_obj_set_style_border_opa(t.box, LV_OPA_60, 0);
                lv_obj_set_style_border_width(t.box, px(1) > 0 ? px(1) : 1, 0);
            } else {
                lv_obj_set_style_radius(t.box, px(w_) / 9 > 0 ? px(w_) / 9 : 1, 0);
                // two halves: a hard step in the gradient
                lv_obj_set_style_bg_color(t.box, lv_color_hex(0x2c2a27), 0);
                lv_obj_set_style_bg_grad_color(t.box, lv_color_hex(0x22201d), 0);
                lv_obj_set_style_bg_grad_dir(t.box, LV_GRAD_DIR_VER, 0);
                lv_obj_set_style_bg_main_stop(t.box, 127, 0);
                lv_obj_set_style_bg_grad_stop(t.box, 128, 0);
            }
            t.digit = new Stroke(t.box, dh_, st_.weight);
            lv_obj_center(t.digit->obj());
            if (st_.tube) {
                t.digit->setGhost(lv_color_hex(0x2c1f16));
                t.digit->setGlow(lv_color_hex(0xff5a00), lv_color_hex(0xff7a1a));
            } else {
                lv_obj_t* hinge = plain(t.box);
                lv_obj_set_size(hinge, LV_PCT(100), px(1) > 0 ? px(1) : 1);
                lv_obj_set_style_bg_color(hinge, lv_color_hex(0x0b0a09), 0);
                lv_obj_set_style_bg_opa(hinge, LV_OPA_COVER, 0);
                lv_obj_center(hinge);
                t.flap = plain(t.box);
                lv_obj_set_size(t.flap, LV_PCT(100), 0);
                lv_obj_set_style_bg_color(t.flap, lv_color_hex(0x34312d), 0);
                lv_obj_set_style_bg_opa(t.flap, LV_OPA_COVER, 0);
                lv_obj_align(t.flap, LV_ALIGN_BOTTOM_MID, 0, -px(h_) / 2);
                ui::visible(t.flap, false);
            }
            tiles_.push_back(t);
        }
    }
    static void flapExec(void* obj, int32_t v) {
        lv_obj_t* f = (lv_obj_t*)obj;
        lv_obj_set_height(f, v);
        ui::visible(f, v > 0);
    }
    void flip(Tile& t) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, t.flap);
        lv_anim_set_exec_cb(&a, flapExec);
        lv_anim_set_values(&a, px(h_) / 2, 0);
        lv_anim_set_duration(&a, 150);
        lv_anim_start(&a);
    }
    lv_obj_t* row_;
    int w_, h_, dh_;
    TileStyle st_;
    std::string shape_;
    std::vector<Tile> tiles_;
};

struct RetroStyle {
    bool nixie;
    lv_color_t bg, ink, dim, label, accentTrack;
};
RetroStyle retro(bool nixie) {
    if (nixie) return {true, lv_color_hex(0x0c0907), lv_color_hex(0xffa24a), lv_color_hex(0x5a4636), lv_color_hex(0xc9a98c), lv_color_hex(0x2a1a10)};
    return {false, lv_color_hex(0x141312), lv_color_hex(0xefeadf), lv_color_hex(0x6a655c), lv_color_hex(0xb9b3a6), lv_color_hex(0x2a2825)};
}

LegendStyle retroLegends(const jt::Fonts& f, const RetroStyle& r) {
    LegendStyle st;
    st.font = f.b12;
    st.text = r.label;
    st.chipBg = r.bg;
    st.wrapTop = true;
    st.rTop = 88;
    st.rBottom = 96;
    return st;
}

TileStyle tileStyle(const RetroStyle& r) {
    TileStyle t;
    t.tube = r.nixie;
    t.weight = r.nixie ? 0.06f : 0.13f;
    t.ink = r.ink;
    return t;
}

// The state at the top: Flip, a small flap card with a coloured dot; Nixie,
// three indicator lamps.
class StateTop {
  public:
    StateTop(lv_obj_t* parent, const jt::Fonts& f, const RetroStyle& r) : r_(r) {
        if (!r.nixie) {
            card_ = row(parent, 5, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_bg_color(card_, lv_color_hex(0x2a2825), 0);
            lv_obj_set_style_bg_grad_color(card_, lv_color_hex(0x201f1c), 0);
            lv_obj_set_style_bg_grad_dir(card_, LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_bg_main_stop(card_, 127, 0);
            lv_obj_set_style_bg_grad_stop(card_, 128, 0);
            lv_obj_set_style_bg_opa(card_, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(card_, px(3), 0);
            lv_obj_set_style_pad_hor(card_, px(7), 0);
            lv_obj_set_style_pad_ver(card_, px(3), 0);
            lv_obj_align(card_, LV_ALIGN_TOP_MID, 0, px(64));
            dot_ = dot(card_, 5, r.ink);
            word_ = text(card_, f.b17, r.ink);
        } else {
            card_ = row(parent, 9, LV_FLEX_ALIGN_START);
            lv_obj_align(card_, LV_ALIGN_TOP_MID, 0, px(62));
            for (int i = 0; i < 3; i++) {
                lv_obj_t* c = column(card_, 3);
                lampCol_[i] = c;
                lamp_[i] = dot(c, 7, lv_color_hex(0x2a1c12));
                lv_obj_set_style_shadow_color(lamp_[i], lv_color_hex(0xff6a00), 0);
                lampText_[i] = text(c, f.m12, r.dim);
            }
        }
    }
    // Nixie: the lamps' words and which one is lit (-1 none)
    void lamps(const char* const words[3], int lit) {
        for (int i = 0; i < 3; i++) {
            setText(lampText_[i], words[i]);
            ui::visible(lampCol_[i], words[i][0] != 0);
            bool on = i == lit;
            ui::bgColor(lamp_[i], on ? r_.ink : lv_color_hex(0x2a1c12));
            int sw = on ? px(8) : 0;
            if (lv_obj_get_style_shadow_width(lamp_[i], 0) != sw) lv_obj_set_style_shadow_width(lamp_[i], sw, 0);
            ui::textColor(lampText_[i], on ? r_.ink : r_.dim);
        }
    }
    void card(const std::string& word, lv_color_t dotC) {
        setText(word_, word);
        ui::bgColor(dot_, dotC);
    }

  private:
    RetroStyle r_;
    lv_obj_t *card_ = nullptr, *dot_ = nullptr, *word_ = nullptr;
    lv_obj_t *lamp_[3] = {}, *lampText_[3] = {}, *lampCol_[3] = {};
};

class RetroJt : public Face {
  public:
    RetroJt(lv_obj_t* parent, const jt::Fonts& f, bool nixie) : r_(retro(nixie)) {
        root_ = faceRoot(parent, r_.bg);
        if (nixie) dashes_ = new Dashes(root_, 113, 3);
        else ring_ = new Ring(root_, 114, 3);
        legends_ = new Legends(root_, retroLegends(f, r_));
        top_ = new StateTop(root_, f, r_);
        TileStyle ts = tileStyle(r_);
        main_ = nixie ? new Tiles(root_, 37, 64, 46, 3, ts) : new Tiles(root_, 42, 62, 44, 4, ts);
        lv_obj_align(main_->obj(), LV_ALIGN_TOP_MID, 0, px(nixie ? 90 : 88));
        lv_obj_t* pr = row(root_, 4, LV_FLEX_ALIGN_CENTER);
        lv_obj_align(pr, LV_ALIGN_TOP_MID, 0, px(162));
        pLabel_ = text(pr, nixie ? f.b12 : f.b17, nixie ? r_.ink : r_.label);
        period_ = nixie ? new Tiles(pr, 14, 22, 16, 2, ts) : new Tiles(pr, 15, 22, 15, 2, ts);
        status_ = text(pr, f.b12, r_.label);
        hold_ = new HoldLine(root_, 156, 100, r_.accentTrack);
    }
    ~RetroJt() override {
        delete dashes_;
        delete ring_;
        delete legends_;
        delete top_;
        delete main_;
        delete period_;
        delete hold_;
        lv_obj_delete(root_);
    }
    void show(const jt::View& v, uint32_t now) override {
        ui::visible(root_, v.screen == jt::Screen::Main);
        if (v.screen != jt::Screen::Main) return;
        JtInfo in = jtInfo(v);
        uint32_t t = toneColour(v.tone);
        lv_color_t tone = lv_color_hex(v.feedbackRed ? kRed : t ? t : 0x9a9a9a);
        int left = v.tone == jt::Tone::Idle ? 0 : in.leftPermille;
        if (r_.nixie) {
            static const char* const words[3] = {"LINEUP", "JAM", "TIMEOUT"};
            int lit = v.state == "LINEUP" ? 0 : v.state == "JAM" ? 1 : v.tone == jt::Tone::Timeout ? 2 : -1;
            if (in.alert || lit < 0) {
                const char* alt[3] = {"", in.word.c_str(), ""};
                top_->lamps(alt, 1);
            } else {
                top_->lamps(words, lit);
            }
            dashes_->set(left, Dashes::Style::Dot, r_.ink, r_.accentTrack);
        } else {
            std::string word = in.word;
            if (in.nextJam && !in.jamNo.empty() && !in.alert) word += "  NEXT " + in.jamNo;
            top_->card(word, tone);
            ring_->track(0, 360, r_.accentTrack);
            ring_->arc(0, left * 360 / 1000, tone);
        }
        main_->set(v.clock, v.feedbackRed && !r_.nixie ? lv_color_hex(kRed) : r_.ink);
        std::string pl = in.pNo;
        if (r_.nixie && !in.jamNo.empty()) pl += " \xc2\xb7 J" + in.jamNo;
        setText(pLabel_, pl);
        setText(status_, in.connected ? "" : in.status);
        period_->set(in.pClock, r_.ink);
        hold_->set(v.holdPermille, r_.nixie ? r_.ink : tone);
        std::string c, l, r;
        jtActions(v, c, l, r);
        legends_->set(c, l, r);
        legends_->show(!crowdedButtons() || legendsDue(now, v.holdPermille));
    }

  private:
    RetroStyle r_;
    lv_obj_t *root_, *pLabel_, *status_;
    Dashes* dashes_ = nullptr;
    Ring* ring_ = nullptr;
    Legends* legends_;
    StateTop* top_;
    Tiles *main_, *period_;
    HoldLine* hold_;
};

class RetroBox : public Face {
  public:
    RetroBox(lv_obj_t* parent, const jt::Fonts& f, bool nixie, const Options& o) : r_(retro(nixie)), o_(o) {
        root_ = faceRoot(parent, r_.bg);
        if (nixie) dashes_ = new Dashes(root_, 113, 3);
        else ring_ = new Ring(root_, 114, 3);
        edge_ = new TeamEdge(root_);
        legends_ = new Legends(root_, retroLegends(f, r_));
        top_ = new StateTop(root_, f, r_);
        TileStyle ts = tileStyle(r_);
        for (int i = 0; i < 3; i++) {
            Row& r = rows_[i];
            r.row = row(root_, 6, LV_FLEX_ALIGN_CENTER);
            lv_obj_align(r.row, LV_ALIGN_TOP_MID, 0, px(90 + i * 31));
            r.num = text(r.row, f.b17, r_.ink);
            lv_obj_set_width(r.num, px(32));
            lv_obj_set_style_text_align(r.num, LV_TEXT_ALIGN_RIGHT, 0);
            r.time = nixie ? new Tiles(r.row, 17, 27, 19, 2, ts) : new Tiles(r.row, 19, 27, 19, 2, ts);
            r.tag = text(r.row, f.b12, r_.ink);
            lv_obj_set_width(r.tag, px(36));
        }
        for (int i = 0; i < 2; i++) {
            Half& h = halves_[i];
            h.col = column(root_, 6);
            lv_obj_align(h.col, LV_ALIGN_TOP_MID, i == 0 ? -px(48) : px(48), px(92));
            h.team = text(h.col, f.b12, r_.label);
            h.time = nixie ? new Tiles(h.col, 19, 32, 23, 2, ts) : new Tiles(h.col, 20, 32, 23, 2, ts);
            h.tag = text(h.col, f.b12, r_.ink);
        }
        empty_ = text(root_, f.b17, r_.dim);
        lv_obj_align(empty_, LV_ALIGN_TOP_MID, 0, px(118));
        lv_label_set_text(empty_, "BOX EMPTY");
        hold_ = new HoldLine(root_, 186, 100, r_.accentTrack);
    }
    ~RetroBox() override {
        delete dashes_;
        delete ring_;
        delete edge_;
        delete legends_;
        delete top_;
        for (auto& r : rows_) delete r.time;
        for (auto& h : halves_) delete h.time;
        delete hold_;
        lv_obj_delete(root_);
    }
    void show(const box::View& v, uint32_t now) override {
        ui::visible(root_, v.screen == box::Screen::Main);
        if (v.screen != box::Screen::Main) return;
        BoxInfo b = boxInfo(v);
        bool jm = v.role == box::Role::Jammers;
        // Flip: the phase colours on the numbers; Nixie: one neon colour, a
        // done skater's tubes go dark and STAND glows.
        auto inkOf = [&](const box::Slot* s) {
            if (!s) return r_.dim;
            if (r_.nixie) return s->phase == box::Phase::Done ? r_.dim : r_.ink;
            return s->phase == box::Phase::Serving ? r_.ink : lv_color_hex(phaseColour(s->phase));
        };
        auto tagOf = [](const box::Slot* s) {
            return !s ? std::string("EMPTY") : s->phase == box::Phase::Stand ? "STAND" : s->phase == box::Phase::Done ? "DONE"
                                                : s->phase == box::Phase::Paused ? "PAUSED" : "";
        };
        std::string head = b.alert ? b.status : shortStatus(b.status);
        if (r_.nixie) {
            const char* words[3] = {"", head.c_str(), ""};
            top_->lamps(words, 1);
        } else {
            const box::Slot* s = b.next >= 0 ? &v.slots[b.next] : nullptr;
            top_->card(head, s ? lv_color_hex(phaseColour(s->phase)) : r_.dim);
        }
        int n = v.slots.size() > 3 ? 3 : (int)v.slots.size();
        for (int i = 0; i < 3; i++) ui::visible(rows_[i].row, !jm && i < n);
        for (int i = 0; i < 2; i++) ui::visible(halves_[i].col, jm);
        ui::visible(empty_, !jm && b.used == 0);
        if (jm) {
            for (int i = 0; i < 2; i++) {
                Half& h = halves_[i];
                const box::Slot* s = sideSlot(v, i);
                setText(h.team, upper(i == 0 ? v.teamLeft : v.teamRight));
                ui::textColor(h.team, teamText(v, i, o_.teamColours, r_.label));
                h.time->set(s ? s->clock : "-:--", inkOf(s), s && s->phase != box::Phase::Done);
                setText(h.tag, tagOf(s));
                ui::textColor(h.tag, inkOf(s));
            }
        } else {
            for (int i = 0; i < n; i++) {
                Row& r = rows_[i];
                const box::Slot* s = &v.slots[i];
                setText(r.num, s->number.empty() ? "" : "#" + s->number);
                ui::textColor(r.num, inkOf(s));
                r.time->set(s->clock, inkOf(s), s->phase != box::Phase::Done);
                setText(r.tag, tagOf(s));
                ui::textColor(r.tag, inkOf(s));
            }
        }
        const box::Slot* s = b.next >= 0 ? &v.slots[b.next] : nullptr;
        int sec = s ? seconds(s->clock) : 0, perm = sec <= 0 ? 0 : sec * 1000 / 30;
        if (r_.nixie) dashes_->set(perm, Dashes::Style::Dot, r_.ink, r_.accentTrack);
        else {
            ring_->track(0, 360, r_.accentTrack);
            ring_->arc(0, perm * 360 / 1000, s ? lv_color_hex(phaseColour(s->phase)) : r_.dim);
        }
        edge_->set(v, o_.teamColours);
        hold_->set(v.holdPermille, r_.ink);
        std::string c, l, r;
        boxActions(v, c, l, r);
        legends_->set(c, l, r);
        legends_->show(!crowdedButtons() || legendsDue(now, v.holdPermille));
    }

  private:
    struct Row {
        lv_obj_t *row, *num, *tag;
        Tiles* time;
    };
    struct Half {
        lv_obj_t *col, *team, *tag;
        Tiles* time;
    };
    RetroStyle r_;
    Options o_;
    lv_obj_t *root_, *empty_;
    Dashes* dashes_ = nullptr;
    Ring* ring_ = nullptr;
    TeamEdge* edge_;
    Legends* legends_;
    StateTop* top_;
    Row rows_[3];
    Half halves_[2];
    HoldLine* hold_;
};

} // namespace

// cfg::kFaceKeys: classic, lcd-day, lcd-night, lcd-colour, big, big-outline,
// rings, chrono, contrast, flip, nixie
std::unique_ptr<Face> makeJtFace(int face, lv_obj_t* parent, const jt::Fonts& f, const Options&) {
    switch (face) {
    case 1:
    case 2:
    case 3: return std::unique_ptr<Face>(new JtFace(parent, f, (Palette)face));
    case 4: return std::unique_ptr<Face>(new BigJt(parent, f, false));
    case 5: return std::unique_ptr<Face>(new BigJt(parent, f, true));
    case 6: return std::unique_ptr<Face>(new RingsJt(parent, f));
    case 7: return std::unique_ptr<Face>(new ChronoJt(parent, f));
    case 8: return std::unique_ptr<Face>(new ContrastJt(parent, f));
    case 9: return std::unique_ptr<Face>(new RetroJt(parent, f, false));
    case 10: return std::unique_ptr<Face>(new RetroJt(parent, f, true));
    }
    return nullptr;
}

std::unique_ptr<Face> makeBoxFace(int face, lv_obj_t* parent, const jt::Fonts& f, const Options& o) {
    switch (face) {
    case 1:
    case 2:
    case 3: return std::unique_ptr<Face>(new BoxFace(parent, f, (Palette)face, o));
    case 4: return std::unique_ptr<Face>(new BigBox(parent, f, false, o));
    case 5: return std::unique_ptr<Face>(new BigBox(parent, f, true, o));
    case 6: return std::unique_ptr<Face>(new RingsBox(parent, f, o));
    case 7: return std::unique_ptr<Face>(new ChronoBox(parent, f, o));
    case 8: return std::unique_ptr<Face>(new ContrastBox(parent, f));
    case 9: return std::unique_ptr<Face>(new RetroBox(parent, f, false, o));
    case 10: return std::unique_ptr<Face>(new RetroBox(parent, f, true, o));
    }
    return nullptr;
}

} // namespace lcd
