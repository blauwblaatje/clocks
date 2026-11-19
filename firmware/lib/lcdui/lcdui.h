// The LCD face (design: https://claude.ai/artifact/8k6rBAAvEcTUUSTjRkgqAA,
// decided with Rogier 2026-11-19): seven-segment digits drawn as polygons,
// fixed state words that light up, button legends where the buttons are, the
// ring as 60 rim segments and the undo/menu hold as a line under the clock.
//
// A face draws the main screen only. It lies over the classic screen
// (jt::Ui / box::Ui), is not clickable, and hides itself on the other
// screens (pickers, menus, prompts, no connection), so touch and every hit
// test stay the classic ones. Each part redraws only its own area when it
// changes (lvset.h).
#pragma once
#include <stdint.h>
#include <string>

#include "boxctl.h"
#include "jtctl.h"
#include "jtui.h"
#include "lvgl.h"

namespace lcd {

// Face setting values beyond 0 (classic); cfg::Settings::face.
enum class Palette : uint8_t { Day = 1, Night = 2, NightColour = 3 };

struct Colours {
    lv_color_t bg, lit, label, rule;
    bool tint; // lit segments take the state's colour
    lv_color_t ghostOf(lv_color_t c) const; // unlit segment
    lv_color_t offOf(lv_color_t c) const;   // unlit state word
};
Colours colours(Palette p);

// Seven-segment text: digits, '-', ' ', ':' (lit colon), ';' (unlit colon).
// Size in design pixels (240 x 240), drawn at display scale.
class Digits {
  public:
    Digits(lv_obj_t* parent, int height);
    void set(const std::string& text, lv_color_t lit, lv_color_t ghost);
    lv_obj_t* obj() const { return obj_; }
    static int width(const std::string& text, int heightPx);

  private:
    static void onDraw(lv_event_t* e);
    lv_obj_t* obj_;
    int h_; // display pixels
    std::string text_;
    lv_color_t lit_, ghost_;
    bool first_ = true;
};

// The rim: 60 segments, lit clockwise from 12 o'clock.
class Rim {
  public:
    explicit Rim(lv_obj_t* parent);
    void set(int permille, lv_color_t lit, lv_color_t ghost, bool on);

  private:
    static void onDraw(lv_event_t* e);
    void invalidateTicks(int from, int to);
    void invalidateBand();
    lv_obj_t* obj_;
    int lit_ = -1;
    lv_color_t litC_, ghostC_;
    bool on_ = true;
};

// The legends next to the buttons: a dot in the button's colour and what the
// button does now. Positions and colours come from the board (lib/buttons).
class Legends {
  public:
    Legends(lv_obj_t* parent, const jt::Fonts& f, const Colours& c);
    // crown, left (10 o'clock button), right (2 o'clock button)
    void set(const std::string& crown, const std::string& left, const std::string& right);

  private:
    lv_obj_t* label_[3];
};

// What the hint says each button does: "Yellow: Stop Jam · Red: Lineup".
void hintActions(const std::string& hint, std::string& crown, std::string& left, std::string& right);
std::string upper(const std::string& s);

class JtFace {
  public:
    JtFace(lv_obj_t* parent, const jt::Fonts& f, Palette p);
    ~JtFace();
    void show(const jt::View& v, uint32_t now);

  private:
    Colours c_;
    jt::Fonts f_;
    lv_obj_t* root_;
    Rim* rim_;
    Legends* legends_;
    lv_obj_t *words_[4], *single_, *pNo_, *pLabel_, *jamLabel_, *hold_;
    Digits *period_, *main_, *jamNo_;
};

class BoxFace {
  public:
    BoxFace(lv_obj_t* parent, const jt::Fonts& f, Palette p);
    ~BoxFace();
    void show(const box::View& v, uint32_t now);

  private:
    struct Row {
        lv_obj_t *box, *hash, *stand, *done;
        Digits *num, *time;
    };
    struct Half {
        lv_obj_t *team, *stand, *done, *note;
        Digits* time;
    };
    Colours c_;
    jt::Fonts f_;
    lv_obj_t *root_, *status_, *hold_, *divider_;
    Rim* rim_;
    Legends* legends_;
    Row rows_[3];
    Half halves_[2];
};

} // namespace lcd
