// Shared parts of the clock faces (lcdui.cpp, faces.cpp): small LVGL
// helpers, the View parsing every face needs, stroke digits, rings and the
// button legends. Sizes are design pixels (240 x 240), drawn at display
// scale (scale.h).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

#include "boxctl.h"
#include "jtctl.h"
#include "jtui.h"
#include "lvgl.h"

namespace lcd {
namespace kit {

lv_obj_t* plain(lv_obj_t* parent);
lv_obj_t* text(lv_obj_t* parent, const lv_font_t* f, lv_color_t c);
void setText(lv_obj_t* l, const std::string& s);
lv_obj_t* row(lv_obj_t* parent, int gap, lv_flex_align_t cross);
lv_obj_t* column(lv_obj_t* parent, int gap);
lv_obj_t* faceRoot(lv_obj_t* parent, lv_color_t bg);
lv_obj_t* dot(lv_obj_t* parent, int size, lv_color_t c);
// Move an object only when the place changed (an align invalidates).
void place(lv_obj_t* o, lv_align_t a, int x, int y);
void setWidth(lv_obj_t* o, int w);

int seconds(const std::string& clock); // "1:42" -> 102; -1 = not a time
std::string upper(const std::string& s);
inline lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

// State colours of the classic screens (jtui.cpp, boxui.cpp).
const uint32_t kJam = 0x3ddc84, kLineup = 0xffb020, kTimeout = 0xd9a0ff, kInter = 0x6fb6ff, kRed = 0xff6a5e;
const uint32_t kServing = 0xff5a4e, kStand = 0xffff00, kDone = 0x6fdf6f, kWaitingC = 0xb05050, kPausedC = 0x8f8f8f;
uint32_t toneColour(jt::Tone t); // 0 = idle
uint32_t phaseColour(box::Phase p);

// What a face takes out of the jam timer's view.
struct JtInfo {
    std::string pNo;      // "P1"
    std::string pClock;   // "24:10"
    std::string jamNo;    // "7"
    bool nextJam = false; // the jam number is the next jam's
    std::string word;     // "JAM 7", "LINEUP", "TEAM 1 TO", "NOT TAKEN", ...
    bool alert = false;   // word is press feedback
    bool connected = false;
    std::string status;   // "CRG", "LOCAL", "CRG BACK"
    int leftPermille = 0; // time left of the ring clock, 0..1000
};
JtInfo jtInfo(const jt::View& v);

// What each button does now, from the hint ("Yellow: Stop Jam · ...").
void hintActions(const std::string& hint, std::string& crown, std::string& left, std::string& right);
void jtActions(const jt::View& v, std::string& crown, std::string& left, std::string& right);
void boxActions(const box::View& v, std::string& crown, std::string& left, std::string& right);

// The box screen: whose clock comes next (lowest time left, still serving
// or standing), how many are done.
struct BoxInfo {
    std::string status; // "T1 BLOCKERS · JAM ON", or feedback / notice
    bool alert = false;
    int next = -1;      // index into v.slots
    int done = 0;
    int used = 0;       // slots with a skater
};
BoxInfo boxInfo(const box::View& v);
int slotSeconds(const box::Slot& s); // time left, -1 = none

// ---------- stroke digits ----------

// Digits drawn as strokes (lines and round arcs, round ends): any size, no
// font, tabular. weight = stroke / height. Characters: 0-9 : - space.
class Stroke {
  public:
    Stroke(lv_obj_t* parent, int height, float weight);
    void set(const std::string& text, lv_color_t c);
    void setHeight(int height); // design px
    // Nixie: unlit cathodes ('8' and '0') behind the digit, and a glow of
    // wider, fainter strokes under it.
    void setGhost(lv_color_t c) { ghost_ = c; ghostOn_ = true; }
    void setGlow(lv_color_t outer, lv_color_t inner) {
        glowOuter_ = outer;
        glowInner_ = inner;
        glow_ = true;
        lv_obj_refresh_ext_draw_size(obj_);
    }
    void setGap(int gap) { gap_ = gap; } // design px between digits (default 0.14 height)
    void glowOn(bool on); // after setGlow: the glow on or off (a done skater's tubes go dark)
    lv_obj_t* obj() const { return obj_; }
    int widthOf(const std::string& text) const; // display px
    static int fitHeight(const std::string& text, float weight, int maxWidth, int maxHeight);

  private:
    static void onDraw(lv_event_t* e);
    static void onExt(lv_event_t* e);
    void resize();
    lv_obj_t* obj_;
    int h_; // display px
    float weight_;
    int gap_ = -1;
    std::string text_;
    lv_color_t c_, ghost_, glowOuter_, glowInner_;
    bool ghostOn_ = false, glow_ = false, first_ = true;
};

// ---------- rings ----------

// A round arc (lv_arc), angles in clock degrees (0 = 12 o'clock, clockwise).
// set() only moves what changed; LVGL redraws the changed part.
class Ring {
  public:
    Ring(lv_obj_t* parent, int radius, int width); // radius to the middle of the band
    void track(int from, int to, lv_color_t c, lv_opa_t opa = LV_OPA_COVER);
    void arc(int from, int to, lv_color_t c);
    void rounded(bool r);
    void show(bool on);
    lv_obj_t* obj() const { return obj_; }

  private:
    lv_obj_t* obj_;
    int tf_ = -1, tt_ = -1, af_ = -1, at_ = -1;
    bool on_ = true;
};

// A dashed or dotted band (High contrast lineup / timeout, Nixie): n
// segments, the first lit ones drawn in the colour, the rest in the track
// colour. Only changed segments are redrawn.
class Dashes {
  public:
    enum class Style : uint8_t { Solid, Dash, Dot };
    Dashes(lv_obj_t* parent, int radius, int width);
    void set(int permille, Style s, lv_color_t lit, lv_color_t track);
    void show(bool on);

  private:
    static void onDraw(lv_event_t* e);
    void invalidateSeg(int from, int to);
    int count() const { return style_ == Style::Dot ? 60 : 40; }
    lv_obj_t* obj_;
    int r_, w_;
    int lit_ = -1;
    Style style_ = Style::Solid;
    lv_color_t litC_, trackC_;
};

// ---------- legends ----------

// What each button does, next to the button: a dot in the button's colour
// and a short text. Positions and colours from the board (lib/buttons).
struct LegendStyle {
    const lv_font_t* font = nullptr;
    lv_color_t text = lv_color_white();
    lv_color_t dotBorder = lv_color_black();
    int dotBorderWidth = 0;
    int dotSize = 7;
    bool chip = false; // text on a rounded plate
    lv_color_t chipBg = lv_color_black(); // also the plate on a crowded board
    lv_opa_t chipOpa = LV_OPA_80;
    bool letter = false; // the button's initial in a bigger dot (High contrast)
    int rTop = 83, rBottom = 92; // where the legends sit
    bool wrapTop = false;        // top-half legends in two lines (side by side chips)
    int edge = 104;              // the inner edge they start from
};
class Legends {
  public:
    Legends(lv_obj_t* parent, const LegendStyle& s);
    void set(const std::string& crown, const std::string& left, const std::string& right);
    void show(bool on);
    void colours(lv_color_t text, lv_color_t chipBg);
    bool shown() const { return on_; }

  private:
    lv_obj_t* row_[3];
    lv_obj_t* label_[3];
    bool top_[3] = {};
    bool wrap_ = false;
    bool on_ = true;
};

// A board with buttons at the sides or at 12 o'clock (Waveshare: 12, 10, 2):
// the legends there would sit on the faces' top rows, so they show on
// plates and only after a press.
bool crowdedButtons();

// The three small button dots at the rim (faces that show their legends
// only after a press).
void buttonDots(lv_obj_t* parent, int radius, int size, lv_color_t border);

} // namespace kit
} // namespace lcd
