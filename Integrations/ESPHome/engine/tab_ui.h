// Drawing: the nine widget kinds, the side bar and the engine's own screens,
// to contract section 4.7. Every widget is built at load time with fixed
// boxes, so a value change only repaints its own rectangle and a page switch
// only hides one object and shows another.
#include "tab_engine.h"  // first, outside the guard (see tab_state.h)

#ifndef TAB_UI_H
#define TAB_UI_H

#include "tab_state.h"

namespace tab {
namespace detail {

// ---------------------------------------------------------------- fonts

inline const lv_font_t *text_font(int px) {
  switch (px) {
    case 14: return g.fonts.text[0];
    case 15: return g.fonts.text[1];
    case 16: return g.fonts.text[2];
    case 18: return g.fonts.text[3];
    case 20: return g.fonts.text[4];
    case 28: return g.fonts.text[5];
    default: return g.fonts.text[6];
  }
}

// The digits fonts hold only contract.json fonts.digits_glyphs.
inline const lv_font_t *digits_font(int px) {
  switch (px) {
    case 96: return g.fonts.digits[1];
    case 192: return g.fonts.digits[2];
    default: return g.fonts.digits[0];
  }
}

inline const lv_font_t *icon_font(int px) {
  switch (px) {
    case 24: return g.fonts.icon[0];
    case 28: return g.fonts.icon[1];
    case 48: return g.fonts.icon[2];
    default: return g.fonts.icon[3];
  }
}

inline int line_height(const lv_font_t *f) { return f ? (int) lv_font_get_line_height(f) : 0; }

// Decodes one UTF-8 character at s[i]; returns the index after it.
inline size_t utf8_next(const char *s, size_t n, size_t i, uint32_t *cp) {
  const uint8_t c = (uint8_t) s[i];
  int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
  if (i + (size_t) len > n) len = 1;
  uint32_t v = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
  for (int k = 1; k < len; k++) {
    const uint8_t x = (uint8_t) s[i + k];
    if ((x & 0xC0) != 0x80) {
      len = 1;
      v = c;
      break;
    }
    v = v << 6 | (x & 0x3F);
  }
  *cp = v;
  return i + (size_t) len;
}

// Width of text in pixels as LVGL lays it out (glyph advance plus kerning).
inline int text_width(const lv_font_t *font, const char *s, size_t n) {
  if (!font) return 0;
  int w = 0;
  size_t i = 0;
  uint32_t cp = 0;
  if (n) i = utf8_next(s, n, 0, &cp);
  while (cp) {
    uint32_t next = 0;
    size_t j = i;
    if (i < n) j = utf8_next(s, n, i, &next);
    w += lv_font_get_glyph_width(font, cp, next);
    cp = i < n ? next : 0;
    i = j;
  }
  return w;
}

inline bool has_glyph(const lv_font_t *font, uint32_t cp) {
  if (!font) return false;
  lv_font_glyph_dsc_t d;
  return lv_font_get_glyph_dsc(font, &d, cp, 0);
}

inline size_t icon_utf8(uint32_t cp, char out[5]) {
  const size_t n = json::put_utf8(cp, out);
  out[n] = 0;
  return n;
}

// The code point to draw for an icon: the fallback when the font lacks it.
inline uint32_t icon_or_fallback(const lv_font_t *font, uint32_t cp) {
  if (cp && has_glyph(font, cp)) return cp;
  return ICON_FALLBACK;
}

// ---------------------------------------------------------------- objects

// Host tests set TAB_TEST_OBJECT_LIMIT to make LVGL "run out" of objects.
#ifdef TAB_TEST_OBJECT_LIMIT
inline int32_t test_objects_left = -1;  // below 0: no limit
inline bool test_object_refused() {
  if (test_objects_left < 0) return false;
  if (test_objects_left == 0) return true;
  test_objects_left--;
  return false;
}
#else
inline bool test_object_refused() { return false; }
#endif

// The builders below go on after a failed create (plain and text_box set
// obj_fail and return nullptr), so every helper takes a null object.
inline lv_obj_t *plain(lv_obj_t *parent) {
  lv_obj_t *o = parent && !test_object_refused() ? lv_obj_create(parent) : nullptr;
  if (!o) {
    g.obj_fail = true;
    return nullptr;
  }
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
                                         LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_SCROLL_CHAIN));
  return o;
}

inline void place(lv_obj_t *o, int x, int y, int w, int h) {
  if (!o) return;
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
}

// Every style the engine sets after the build goes through these, and each
// compares first (cards spec B.4, rules 2 and 3): LVGL sets a style again
// even when it holds that value, invalidates the object twice for it, and
// for a position, size, padding, border width or font walks every object of
// the screen before the next frame. All of them set the main part's local
// style, the only one the engine uses (plain() removes the theme's).
inline void style_num(lv_obj_t *o, lv_style_prop_t prop, int32_t v) {
  if (!o) return;
  lv_style_value_t cur;
  if (lv_obj_get_local_style_prop(o, prop, &cur, 0) == LV_STYLE_RES_FOUND && cur.num == v) return;
  TAB_COUNT(style_sets);
  lv_style_value_t nv;
  nv.num = v;
  lv_obj_set_local_style_prop(o, prop, nv, 0);
}

inline void style_colour(lv_obj_t *o, lv_style_prop_t prop, uint32_t c) {
  if (!o) return;
  const lv_color_t want = lv_color_hex(c);
  lv_style_value_t cur;
  if (lv_obj_get_local_style_prop(o, prop, &cur, 0) == LV_STYLE_RES_FOUND && lv_color_eq(cur.color, want)) return;
  TAB_COUNT(style_sets);
  lv_style_value_t nv;
  nv.color = want;
  lv_obj_set_local_style_prop(o, prop, nv, 0);
}

inline void style_ptr(lv_obj_t *o, lv_style_prop_t prop, const void *p) {
  if (!o) return;
  lv_style_value_t cur;
  if (lv_obj_get_local_style_prop(o, prop, &cur, 0) == LV_STYLE_RES_FOUND && cur.ptr == p) return;
  TAB_COUNT(style_sets);
  lv_style_value_t nv;
  nv.ptr = p;
  lv_obj_set_local_style_prop(o, prop, nv, 0);
}

// lv_obj_set_pos and lv_obj_set_width compare too; these count what changes.
inline void move(lv_obj_t *o, int32_t x, int32_t y) {
  if (!o) return;
  lv_style_value_t cx, cy;
  if (lv_obj_get_local_style_prop(o, LV_STYLE_X, &cx, 0) == LV_STYLE_RES_FOUND && cx.num == x &&
      lv_obj_get_local_style_prop(o, LV_STYLE_Y, &cy, 0) == LV_STYLE_RES_FOUND && cy.num == y)
    return;
  TAB_COUNT(style_sets);
  lv_obj_set_pos(o, x, y);
}

inline void set_width(lv_obj_t *o, int32_t w) {
  if (!o) return;
  lv_style_value_t cur;
  if (lv_obj_get_local_style_prop(o, LV_STYLE_WIDTH, &cur, 0) == LV_STYLE_RES_FOUND && cur.num == w) return;
  TAB_COUNT(style_sets);
  lv_obj_set_width(o, w);
}

inline void set_hidden(lv_obj_t *o, bool hide) {
  if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == hide) return;
  TAB_COUNT(style_sets);
  if (hide)
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

inline void set_opa(lv_obj_t *o, lv_opa_t opa) { style_num(o, LV_STYLE_OPA, opa); }

// 40% or full opacity (contract 4.7, "No value" and (2) "Card, not
// available"). Full is LVGL's default, so an object that was never faded
// gets no style for it.
inline void fade(lv_obj_t *o, bool faded) {
  if (!o) return;
  const int32_t want = faded ? LV_OPA_40 : LV_OPA_COVER;
  lv_style_value_t cur;
  const int32_t now_opa = lv_obj_get_local_style_prop(o, LV_STYLE_OPA, &cur, 0) == LV_STYLE_RES_FOUND
                              ? cur.num
                              : (int32_t) LV_OPA_COVER;
  if (now_opa == want) return;
  TAB_COUNT(style_sets);
  lv_style_value_t nv;
  nv.num = want;
  lv_obj_set_local_style_prop(o, LV_STYLE_OPA, nv, 0);
}

inline void set_bg(lv_obj_t *o, uint32_t c) {
  style_colour(o, LV_STYLE_BG_COLOR, c);
  style_num(o, LV_STYLE_BG_OPA, LV_OPA_COVER);
}

inline void set_radius(lv_obj_t *o, int r) { style_num(o, LV_STYLE_RADIUS, r); }

inline void set_clickable(lv_obj_t *o, bool on) {
  if (!o) return;
  if (on)
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  else
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

inline void set_outline(lv_obj_t *o, bool on, uint32_t c) {
  if (!o) return;
  style_num(o, LV_STYLE_OUTLINE_WIDTH, on ? 2 : 0);
  if (on) {
    style_colour(o, LV_STYLE_OUTLINE_COLOR, c);
    style_num(o, LV_STYLE_OUTLINE_OPA, LV_OPA_COVER);
    style_num(o, LV_STYLE_OUTLINE_PAD, 0);
  }
}

// A one-line text box with a fixed size. The text is centred vertically.
inline lv_obj_t *text_box(lv_obj_t *parent, int x, int y, int w, int h, const lv_font_t *font, bool centre) {
  lv_obj_t *l = parent && !test_object_refused() ? lv_label_create(parent) : nullptr;
  if (!l) {
    g.obj_fail = true;
    return nullptr;
  }
  lv_obj_remove_style_all(l);
  lv_obj_remove_flag(l, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_style_text_font(l, font, 0);
  if (centre) lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  place(l, x, y, w, h);
  const int pad = (h - line_height(font)) / 2;
  lv_obj_set_style_pad_top(l, pad > 0 ? pad : 0, 0);
  lv_label_set_text(l, "");
  return l;
}

// An icon glyph centred in a box.
inline lv_obj_t *icon_box(lv_obj_t *parent, int x, int y, int w, int h, int px) {
  lv_obj_t *l = text_box(parent, x, y, w, h, icon_font(px), true);
  return l;
}

inline void set_icon(lv_obj_t *l, int px, uint32_t cp) {
  if (!l) return;
  char buf[5];
  icon_utf8(icon_or_fallback(icon_font(px), cp), buf);
  if (strcmp(lv_label_get_text(l), buf) == 0) return;
  TAB_COUNT(style_sets);
  lv_label_set_text(l, buf);
}

inline void set_text_colour(lv_obj_t *o, uint32_t c) { style_colour(o, LV_STYLE_TEXT_COLOR, c); }

inline void set_font(lv_obj_t *o, const lv_font_t *f) { style_ptr(o, LV_STYLE_TEXT_FONT, f); }

// Sets one line of text, cut with an ellipsis when wider than width. Control
// characters show as spaces. Returns true when the text was cut.
inline bool set_text(lv_obj_t *l, const char *text, int width) {
  if (!l) return false;
  const lv_font_t *font = lv_obj_get_style_text_font(l, LV_PART_MAIN);
  char buf[TEXT_CAP + 8];
  size_t n = 0;
  for (; text && text[n] && n < (size_t) TEXT_CAP - 1; n++) buf[n] = (uint8_t) text[n] < 0x20 ? ' ' : text[n];
  buf[n] = 0;
  bool cut = false;
  if (width > 0 && text_width(font, buf, n) > width) {
    cut = true;
    const char *ell = has_glyph(font, 0x2026) ? "\xE2\x80\xA6" : "...";
    const size_t ell_n = strlen(ell);
    // Longest prefix that fits with the ellipsis.
    size_t keep = 0;
    size_t i = 0;
    while (i < n) {
      uint32_t cp;
      const size_t j = utf8_next(buf, n, i, &cp);
      char trial[TEXT_CAP + 8];
      memcpy(trial, buf, j);
      memcpy(trial + j, ell, ell_n);
      if (text_width(font, trial, j + ell_n) > width) break;
      keep = j;
      i = j;
    }
    while (keep > 0 && buf[keep - 1] == ' ') keep--;
    memcpy(buf + keep, ell, ell_n);
    n = keep + ell_n;
    buf[n] = 0;
  }
  if (strcmp(lv_label_get_text(l), buf) != 0) {
    TAB_COUNT(style_sets);
    lv_label_set_text(l, buf);
  }
  return cut;
}

// ---------------------------------------------------------------- numbers

// JavaScript Math.round.
inline double js_round(double v) { return floor(v + 0.5); }

// logic.js clampStep.
inline double clamp_step(double value, double low, double high, double step) {
  double r = value < low ? low : value;
  if (r > high) r = high;
  if (step > 0) {
    r = low + js_round((r - low) / step) * step;
    if (r < low) r = low;
    if (r > high) r = high;
  }
  return js_round(r * 1000000.0) / 1000000.0;
}

// logic.js fraction. NaN (never expected: every number is within +-1e9) is 0.
inline double fraction(double value, double low, double high) {
  if (!(high > low)) return 0;
  double f = (value - low) / (high - low);
  return !(f > 0) ? 0 : f > 1 ? 1 : f;
}

// A drawing coordinate: NaN is 0, and the value fits int32_t before the cast.
inline int32_t px_round(double v) {
  if (!(v == v)) return 0;
  v = js_round(v);
  return (int32_t) (v > 1e6 ? 1e6 : v < -1e6 ? -1e6 : v);
}

inline void limits_of(Model &m, Widget &w, double *lo, double *hi, double *step) {
  if (!bound_num(m, w.vmin, lo)) *lo = 0;
  if (!bound_num(m, w.vmax, hi)) *hi = 100;
  if (!bound_num(m, w.step, step)) *step = 1;
}

// render.js thermostatText: no decimals from 1000 up, else at most one.
inline void thermostat_text(double v, char *out, size_t cap) {
  double r = fabs(v) >= 1000 ? js_round(v) : js_round(v * 10) / 10;
  if (r == 0) r = 0;  // no "-0"
  if (r == floor(r))
    snprintf(out, cap, "%.0f", r);
  else
    snprintf(out, cap, "%.1f", r);
}

inline int thermostat_text_width(int s);

// The size thermostatFit picks (below), before the drawn width is checked.
inline const lv_font_t *thermostat_fit_font(const char *text, int s) {
  const double stroke = fmax(8.0, s / 14.0);
  const double inner = 2 * (72 - stroke / 2) * s / 200.0;
  const double max_w = 0.8 * inner;
  double em = 0;
  bool digits = true;
  size_t n = strlen(text);
  for (size_t i = 0; i < n;) {
    uint32_t cp;
    i = utf8_next(text, n, i, &cp);
    em += (cp == '.' || cp == ',' || cp == ':' || cp == ' ') ? 0.32 : 0.62;
    if (!((cp >= '0' && cp <= '9') || cp == '.' || cp == ',' || cp == '-')) digits = false;
  }
  const double fit = fmin(s / 5.0, em > 0 ? max_w / em : s / 5.0);
  if (digits && digits_font(48) && fit >= 48) return digits_font(48);
  static const int SIZES[4] = {34, 20, 18, 14};
  for (int px : SIZES)
    if (fit >= px) return text_font(px);
  return text_font(14);
}

// The thermostat value font (contract 4.7): the largest size not above
// thermostatFit; 48 only for text of 0123456789.,- (its own list: the
// digits font holds a few more characters).
// Anything else, such as the no-value "–" (U+2013), takes the largest text
// size that fits: 34 at most. Every text size holds U+2013. thermostatFit
// counts 0.62 em a digit, but Montserrat's digits are up to 0.67 em, so the
// text drawn at that size can still be wider than thermostat_text_width(s):
// then the next smaller size (48, 34, 20, 18, 14). Only 14 px is ever cut.
inline const lv_font_t *thermostat_font(const char *text, int s) {
  const lv_font_t *font = thermostat_fit_font(text, s);
  static const int SIZES[4] = {34, 20, 18, 14};
  const int room = thermostat_text_width(s);
  const size_t n = strlen(text);
  int next = 0;
  if (font != digits_font(48)) {
    while (next < 4 && text_font(SIZES[next]) != font) next++;
    next++;  // the size after the one picked
  }
  while (next < 4 && text_width(font, text, n) > room) font = text_font(SIZES[next++]);
  return font;
}

// The thermostat arc (contract 4.7): radius 0.36 * s plus half the stroke,
// stroke max(8, s / 14) * s / 200. A rectangle is cut to the screen
// (read_rect), so s is at most 800 and the radius at most 402. The limit is
// kept here too: LVGL's radius mask writes past its buffer for some radii
// above 900 px.
static const int THERMO_MAX_SIDE = SCREEN_H;
inline void thermo_arc(int s, int32_t *width, int32_t *radius) {
  if (s > THERMO_MAX_SIDE) s = THERMO_MAX_SIDE;
  if (s < 0) s = 0;
  const double stroke = fmax(8.0, s / 14.0) * s / 200.0;
  int32_t r = (int32_t) js_round(0.36 * s + stroke / 2);
  int32_t w = (int32_t) js_round(stroke);
  if (r < 1) r = 1;
  if (w < 1) w = 1;
  if (w > r) w = r;
  *width = w;
  *radius = r;
}

inline int thermostat_text_width(int s) {
  const double stroke = fmax(8.0, s / 14.0);
  return (int) (0.8 * 2 * (72 - stroke / 2) * s / 200.0);
}

// The image widget icon size: the largest of 72, 48, 28, 24 not above min(w, h) / 2.
inline int image_icon_px(int w, int h) {
  const int half = (w < h ? w : h) / 2;
  static const int SIZES[4] = {72, 48, 28, 24};
  for (int px : SIZES)
    if (px <= half) return px;
  return 24;
}

// ---------------------------------------------------------------- events

inline Layout *layout_of_ptr(const void *p) {
  for (int i = 0; i < LAYOUTS; i++) {
    Layout &l = g.lay[i];
    if (!l.model) continue;
    const uint8_t *b = reinterpret_cast<const uint8_t *>(l.model);
    const uint8_t *q = static_cast<const uint8_t *>(p);
    if (q >= b && q < b + l.model->block_size) return &l;
  }
  return nullptr;
}

inline void after_press() {
  // Draw the pressed look now instead of at the next refresh period.
  if (lv_display_get_default()) lv_refr_now(nullptr);
}

inline void slider_value_at(Layout &l, Widget &w, lv_indev_t *indev) {
  if (!indev) return;
  lv_point_t p;
  lv_indev_get_point(indev, &p);
  lv_area_t a;
  lv_obj_get_coords(w.obj, &a);
  const int width = lv_area_get_width(&a);
  double amount = width > 0 ? (double) (p.x - a.x1) / width : 0;
  amount = amount < 0 ? 0 : amount > 1 ? 1 : amount;
  double lo, hi, step;
  limits_of(*l.model, w, &lo, &hi, &step);
  w.drag_value = clamp_step(lo + amount * (hi - lo), lo, hi, step);
}

inline void on_knob_margin(lv_event_t *e) { lv_event_set_ext_draw_size(e, 14); }

// A control without a value yet is drawn faded (Widget.faded, contract 7.4).
// There is nothing to change on it yet: while the tablet can send, it takes
// no input. While it cannot, it stays clickable so a tap says why (the strip,
// and a knock without a session), as on any other control. A tap that only
// switches pages (tap 2) needs no value and always works. Both tests read
// one flag and, only on a faded control, what can_send() reads anyway.
// (2) The same holds for every widget of a card that is not available, and
// for that card itself (`un` 1, contract 4.3); a `back` works as a `go`.
inline bool quiet_now(Model &m, const Widget &w) { return w.faded || card_unavailable(m, w); }

inline bool faded_and_quiet(Model &m, const Widget &w) {
  return w.tap != 2 && w.tap != 3 && quiet_now(m, w) && can_send();
}

inline bool faded_tap(Model &m, const Widget &w) {
  if (w.tap == 2 || w.tap == 3 || !quiet_now(m, w)) return false;
  if (!can_send()) refuse_touch();
  return true;
}

// ---------------------------------------------------------------- (2) the press now

// The press now (cards spec B.5). It begins at the first sight of the finger,
// in a card's PRESSED or in the timer, whichever comes first, at that point.
inline void press_begin(lv_point_t p) {
  g.tracking = true;
  g.swallowed = false;
  g.start = p;
  g.press_at = now();
  g.moved = false;
  g.held = false;
  g.press_in_watch = g.erase_timer != nullptr;
}

// The 12 px test, one for the card tap, the pressed look and the hold: once
// passed it stays passed until the next press (contract 4.3).
inline void press_track(lv_point_t p) {
  if (!g.tracking || g.moved) return;
  const int32_t dx = p.x - g.start.x, dy = p.y - g.start.y;
  if (dx * dx + dy * dy >= MOVED_PX2) g.moved = true;
}

// The card that has the press, or nullptr.
inline Widget *pressed_card(Layout **out = nullptr) {
  Layout *l = layout_at(g.cp_layout);
  if (!l || l->state != L_ACTIVE || l->gen != g.cp_gen || !l->model || g.cp_widget >= l->model->nw) return nullptr;
  if (out) *out = l;
  return &l->model->w[g.cp_widget];
}

// A widget that has the press: the card that has it, a pressed widget, a
// dragged slider, a pressed thermostat button.
inline bool press_held(Widget &w) {
  if (w.kind == W_CARD) return pressed_card() == &w;
  return w.pressed || w.dragging || w.pressed_part;
}

// The press is lost (contract 7.4): LVGL sends PRESS_LOST at the release and
// no CLICKED, also when the widget shows again before the finger lifts. The
// press opens no hold page any more either.
inline void lose_press(Widget &w) {
  if (w.kind == W_CARD) g.cp_layout = -1;
  w.pressed = 0;
  w.dragging = 0;
  w.pressed_part = 0;
  g.held = true;
  if (g.indev) lv_indev_wait_release(g.indev);
}

// (2) `vis` (contract 4.3): a widget of a card is drawn while its value is
// not 0; no value hides it, and without the key it always shows.
inline bool vis_shows(Model &m, const Widget &w) {
  if (w.card < 0 || w.vis.how == B_ABSENT) return true;
  double v;
  return bound_num(m, w.vis, &v) && v != 0;
}

inline void on_widget_event(lv_event_t *e) {
  Widget *w = static_cast<Widget *>(lv_event_get_user_data(e));
  Layout *l = layout_of(w);
  if (!l || l->state != L_ACTIVE) return;
  const lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *indev = lv_indev_active();
  switch (code) {
    case LV_EVENT_PRESSED:
      if (faded_and_quiet(*l->model, *w)) break;
      w->pressed = 1;
      if (w->kind == W_SLIDER && w->change && !w->faded) {
        g.press_on_slider = true;  // this press is the slider's, not a page swipe
        w->dragging = 1;
        slider_value_at(*l, *w, indev);
      }
      paint_widget(*l, *w);
      after_press();
      break;
    case LV_EVENT_PRESSING:
      if (w->dragging) {
        const double before = w->drag_value;
        slider_value_at(*l, *w, indev);
        if (w->drag_value != before) paint_widget(*l, *w);
      }
      break;
    case LV_EVENT_RELEASED:
      w->pressed = 0;
      if (w->dragging) {
        slider_value_at(*l, *w, indev);
        w->dragging = 0;
        // A lost press never gets here; the state is read once more anyway.
        if (vis_shows(*l->model, *w) && !faded_and_quiet(*l->model, *w)) slider_released(*l, *w, w->drag_value);
      }
      paint_widget(*l, *w);
      break;
    case LV_EVENT_PRESS_LOST:
      w->pressed = 0;
      w->dragging = 0;
      paint_widget(*l, *w);
      break;
    case LV_EVENT_CLICKED:
      if (!vis_shows(*l->model, *w) || faded_tap(*l->model, *w)) break;
      if (w->kind != W_SLIDER) widget_tapped(*l, *w);
      break;
    default:
      break;
  }
}

inline void on_thermo_button(lv_event_t *e) {
  Widget *w = static_cast<Widget *>(lv_event_get_user_data(e));
  Layout *l = layout_of(w);
  if (!l || l->state != L_ACTIVE) return;
  lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  const int dir = target == w->o2 ? -1 : 1;
  switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
      if (faded_and_quiet(*l->model, *w)) break;
      w->pressed_part = (int8_t) dir;
      paint_widget(*l, *w);
      after_press();
      break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
      w->pressed_part = 0;
      paint_widget(*l, *w);
      break;
    case LV_EVENT_CLICKED:
      if (!vis_shows(*l->model, *w) || faded_tap(*l->model, *w)) break;
      thermostat_step(*l, *w, dir);
      break;
    default:
      break;
  }
}

inline void paint_nav(Layout &l);

inline void on_nav_event(lv_event_t *e) {
  Nav *n = static_cast<Nav *>(lv_event_get_user_data(e));
  Layout *l = layout_of_ptr(n);
  if (!l || l->state != L_ACTIVE) return;
  switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
      n->pressed = 1;
      paint_nav(*l);
      after_press();
      break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
      n->pressed = 0;
      paint_nav(*l);
      break;
    case LV_EVENT_CLICKED:
      if (n->page < l->model->npages) show_page(*l, n->page);
      break;
    default:
      break;
  }
}

// (2) A card with `tap` or `hold` (contract 4.3, cards spec B.5). The press
// is the card's when it begins on the card outside its widgets, or on a
// widget that is not clickable (one that only shows, also while faded). The
// card taps on release when the press moved less than 12 px; the pressed look
// (after 80 ms) and the hold come from the timer.
inline void card_press_end(Layout &l, Widget &w) {
  if (pressed_card() == &w) g.cp_layout = -1;
  if (!w.pressed) return;
  w.pressed = 0;
  paint_widget(l, w);
}

inline void on_card_event(lv_event_t *e) {
  Widget *w = static_cast<Widget *>(lv_event_get_user_data(e));
  Layout *l = layout_of(w);
  if (!l || l->state != L_ACTIVE) return;
  lv_indev_t *indev = lv_indev_active();
  lv_point_t p = {0, 0};
  if (indev) lv_indev_get_point(indev, &p);
  switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
      if (!g.tracking) press_begin(p);
      g.cp_layout = (int8_t) layout_index(l);
      g.cp_gen = l->gen;
      g.cp_widget = (uint16_t) (w - l->model->w);
      break;
    case LV_EVENT_PRESSING:
      press_track(p);
      break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
      card_press_end(*l, *w);
      break;
    case LV_EVENT_CLICKED:
      press_track(p);
      if (g.moved || g.swallowed || faded_tap(*l->model, *w)) break;
      widget_tapped(*l, *w);
      break;
    default:
      break;
  }
}

inline void add_events(lv_obj_t *o, lv_event_cb_t cb, void *user) {
  if (!o) return;
  lv_obj_add_event_cb(o, cb, LV_EVENT_PRESSED, user);
  lv_obj_add_event_cb(o, cb, LV_EVENT_PRESSING, user);
  lv_obj_add_event_cb(o, cb, LV_EVENT_RELEASED, user);
  lv_obj_add_event_cb(o, cb, LV_EVENT_PRESS_LOST, user);
  lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, user);
}

// ---------------------------------------------------------------- colours of the parts

// The colours a widget is painted with: its card's record (cards spec B.3),
// worked out at the build. A widget outside a card has none: the record of
// no card, all zero, which nothing paints with.
inline const CardLook &look_for(Model &m, Widget *card) {
  static const CardLook none{};
  if (!card) return none;
  if (const CardLook *k = card_look(m, card)) return *k;
  card_look_update(m, *card);  // not reached: the card's shell works it out first
  const CardLook *k = card_look(m, card);
  return k ? *k : none;
}

// ---------------------------------------------------------------- custom drawing

// logic.js linePoints and barRects, drawn with the LVGL draw API.
inline void on_chart_draw(lv_event_t *e) {
  Widget *w = static_cast<Widget *>(lv_event_get_user_data(e));
  Layout *l = layout_of(w);
  if (!l || !l->model) return;
  Model &m = *l->model;
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_obj_t *obj = w->obj;
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  const int W = lv_area_get_width(&a), H = lv_area_get_height(&a);
  const Tok &t = look_for(m, card_of(m, *w)).t;
  const uint32_t colour = w->c_has ? w->c_now : t.acc2;
  const lv_opa_t opa = lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN);
  lv_draw_rect_dsc_t base;
  lv_draw_rect_dsc_init(&base);
  base.bg_color = lv_color_hex(t.line);
  base.bg_opa = opa;
  lv_area_t line_area = {a.x1, a.y2 - 1, a.x2, a.y2 - 1};
  lv_draw_rect(layer, &base, &line_area);
  const double *v = nullptr;
  const int n = bound_list(m, w->value, &v);
  if (n <= 0) return;
  double low = v[0], high = v[0];
  for (int i = 1; i < n; i++) {
    low = fmin(low, v[i]);
    high = fmax(high, v[i]);
  }
  if (w->chart == 1) {
    const double h2 = H - 2, gap = 4;
    const double lo = fmin(0.0, low);
    const double span = (high - lo) != 0 ? high - lo : 1;
    const double bw = fmax(0.0, (W - gap * (n - 1)) / n);
    lv_draw_rect_dsc_t bar;
    lv_draw_rect_dsc_init(&bar);
    bar.bg_color = lv_color_hex(colour);
    bar.bg_opa = opa;
    bar.radius = (int32_t) fmin(4.0, bw / 2);
    for (int i = 0; i < n; i++) {
      const double scaled = (v[i] - lo) / span * h2;
      const double bh = fmin(h2, fmax(2.0, scaled));
      const double x = i * (bw + gap), y = h2 - bh;
      lv_area_t r = {a.x1 + px_round(x), a.y1 + px_round(y), a.x1 + px_round(x + bw) - 1, a.y1 + px_round(y + bh) - 1};
      if (r.x2 >= r.x1 && r.y2 >= r.y1) lv_draw_rect(layer, &bar, &r);
    }
    return;
  }
  const double pad = 4;
  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = lv_color_hex(colour);
  ld.width = 3;
  ld.opa = opa;
  ld.round_start = 1;
  ld.round_end = 1;
  auto point = [&](int i, double *x, double *y) {
    if (n == 1) {
      *x = i == 0 ? pad : W - pad;
      *y = H / 2.0;
      return;
    }
    *x = pad + (double) i / (n - 1) * (W - pad * 2);
    *y = high == low ? H / 2.0 : H - pad - (v[i] - low) / (high - low) * (H - pad * 2);
  };
  const int count = n == 1 ? 2 : n;
  double px, py;
  point(0, &px, &py);
  for (int i = 1; i < count; i++) {
    double x, y;
    point(i, &x, &y);
    ld.p1.x = px_round(a.x1 + px);
    ld.p1.y = px_round(a.y1 + py);
    ld.p2.x = px_round(a.x1 + x);
    ld.p2.y = px_round(a.y1 + y);
    lv_draw_line(layer, &ld);
    px = x;
    py = y;
  }
}

inline bool thermostat_value(Model &m, Widget &w, double *v) {
  if (w.ovr) {
    *v = w.ovr_value;
    return true;
  }
  return bound_num(m, w.value, v);
}

inline void on_thermo_draw(lv_event_t *e) {
  Widget *w = static_cast<Widget *>(lv_event_get_user_data(e));
  Layout *l = layout_of(w);
  if (!l || !l->model) return;
  Model &m = *l->model;
  const CardLook &k = look_for(m, card_of(m, *w));
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(w->obj, &a);
  const int W = lv_area_get_width(&a), H = lv_area_get_height(&a);
  const int s = W < H ? W : H;
  const lv_opa_t opa = lv_obj_get_style_opa_recursive(w->obj, LV_PART_MAIN);
  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.center.x = a.x1 + W / 2;
  arc.center.y = a.y1 + H / 2;
  int32_t width, radius;
  thermo_arc(s, &width, &radius);
  arc.width = width;
  arc.radius = (uint16_t) radius;
  arc.rounded = 1;
  arc.opa = opa;
  arc.start_angle = 135;
  arc.end_angle = 45;
  arc.color = lv_color_hex(k.k.filled ? k.k.FP : k.t.card2);
  lv_draw_arc(layer, &arc);
  double v;
  if (!thermostat_value(m, *w, &v)) return;
  double lo, hi, step;
  limits_of(m, *w, &lo, &hi, &step);
  const double amount = fraction(v, lo, hi);
  int end = (int) px_round(135 + 270 * amount);
  if (end <= 135) return;
  if (end >= 360) end -= 360;
  arc.end_angle = end;
  arc.color = lv_color_hex(w->c_has ? w->c_now : k.t.acc2);
  lv_draw_arc(layer, &arc);
}

// ---------------------------------------------------------------- painting

// (2) The card of a widget pulses (contract 4.7, "Card, pulse").
inline bool card_pulses(Model &m, const Widget &w) {
  const Widget *card = card_of(m, w);
  return card && card->fb == 2;
}

inline void paint_label(Layout &l, Widget &w) {
  Model &m = *l.model;
  const CardLook &k = look_for(m, card_of(m, w));
  uint32_t colour = w.size == 0 ? (k.k.filled ? k.k.FS : k.t.tx2) : k.k.text;
  if (w.c_has) colour = w.c_now;
  if (card_pulses(m, w)) colour = k.k.AT;
  set_text_colour(w.obj, colour);
  fade(w.obj, k.unavailable);
  const char *text = "";
  bound_text(m, w.text, &text);
  w.cut = set_text(w.obj, text, w.r[2]);
}

inline void layout_button(Layout &l, Widget &w, const char *text) {
  (void) l;
  const int W = w.r[2], H = w.r[3];
  const lv_font_t *tf = text_font(15);
  const lv_font_t *ifont = icon_font(24);
  const int ih = line_height(ifont), th = line_height(tf);
  const bool has_icon = w.o1 != nullptr, has_text = w.o2 != nullptr;
  const int iw = has_icon ? lv_font_get_glyph_width(ifont, icon_or_fallback(ifont, w.icon), 0) : 0;
  if (has_icon && has_text && H >= 64) {
    const int group = ih + 6 + th;
    const int y0 = (H - group) / 2;
    move(w.o1, (W - iw) / 2, y0);
    move(w.o2, 6, y0 + ih + 6);
    w.cut = set_text(w.o2, text, W - 12);
  } else if (has_icon && has_text) {
    const int avail = W - 12 - iw - 6;
    w.cut = set_text(w.o2, text, avail);
    const char *shown = lv_label_get_text(w.o2);
    const int tw = text_width(tf, shown, strlen(shown));
    const int group = iw + 6 + (tw < avail ? tw : avail);
    const int x0 = (W - group) / 2;
    move(w.o1, x0, (H - ih) / 2);
    move(w.o2, x0 + iw + 6, 0);
  } else if (has_icon) {
    move(w.o1, (W - iw) / 2, (H - ih) / 2);
  } else if (has_text) {
    w.cut = set_text(w.o2, text, W - 12);
  }
}

inline void paint_button(Layout &l, Widget &w) {
  Model &m = *l.model;
  const CardLook &k = look_for(m, card_of(m, w));
  const bool filled = k.k.filled;
  const bool pulse = w.fb == 2;
  uint32_t bg = k.t.card2;
  if (pulse)
    bg = k.k.AF;
  else if (w.pressed)
    bg = filled ? k.k.FP : k.t.card2_pr;
  set_bg(w.obj, bg);
  fade(w.obj, k.unavailable);
  const uint32_t fg = pulse ? k.k.AT : k.t.tx;
  set_text_colour(w.o1, fg);
  set_text_colour(w.o2, fg);
  set_outline(w.obj, w.fb == 1, filled ? k.k.FI : k.k.A);
  const char *text = "";
  bound_text(m, w.text, &text);
  layout_button(l, w, text);
}

// A bool of a switch or a button now: the value the widget's own tap shows,
// else the one its card shows for the same slot (contract 7.4), else its own.
inline bool bool_value(Model &m, Widget &w, const Bound &b, bool *v) {
  double x;
  if (w.ovr)
    x = w.ovr_value;
  else if (!slot_override(m, card_of(m, w), b, &x) && !bound_num(m, b, &x))
    return false;
  *v = x != 0;
  return true;
}

inline bool switch_value(Model &m, Widget &w, bool *v) { return bool_value(m, w, w.value, v); }

// (2) A button with `value` is selected while it is 1; no value is 0 (4.4).
inline bool button_selected(Model &m, Widget &w) {
  bool v = false;
  return w.value.how != B_ABSENT && bool_value(m, w, w.value, &v) && v;
}

inline void paint_switch(Layout &l, Widget &w) {
  Model &m = *l.model;
  const CardLook &k = look_for(m, card_of(m, w));
  const bool filled = k.k.filled;
  bool v = false;
  const bool has = switch_value(m, w, &v);
  fade(w.obj, !has || k.unavailable);
  w.faded = has ? 0 : 1;
  uint32_t track = v ? (w.c_has ? w.c_now : filled ? k.k.FI : k.k.A) : k.t.card2;
  if (w.pressed) track = v ? k.k.AP : k.t.card2_pr;
  set_bg(w.o1, track);
  set_bg(w.o2, v ? (w.c_has ? w.c_on : filled ? k.k.F : k.k.AT) : k.t.tx2);
  const int tw = w.r[2] < 64 ? w.r[2] : 64;
  const int th = w.r[3] < 36 ? w.r[3] : 36;
  move(w.o2, v ? tw - 32 : 4, (th - 28) / 2);
  set_outline(w.o1, w.fb == 1, filled ? k.k.FI : k.k.A);
}

inline bool slider_value(Model &m, Widget &w, double *v) {
  if (w.dragging) {
    *v = w.drag_value;
    return true;
  }
  if (w.ovr) {
    *v = w.ovr_value;
    return true;
  }
  return bound_num(m, w.value, v);
}

inline void paint_slider(Layout &l, Widget &w) {
  Model &m = *l.model;
  const CardLook &k = look_for(m, card_of(m, w));
  const bool filled = k.k.filled;
  double v = 0, lo, hi, step;
  const bool has = slider_value(m, w, &v);
  limits_of(m, w, &lo, &hi, &step);
  // Faded when the value went away, also under a dragging finger: the press
  // is then lost (paint_widget).
  double real;
  w.faded = w.ovr || bound_num(m, w.value, &real) ? 0 : 1;
  fade(w.obj, !has || w.faded || k.unavailable);
  const double amount = has ? fraction(v, lo, hi) : 0;
  const int W = w.r[2], H = w.r[3];
  const int fill = (int) js_round(amount * W);
  set_bg(w.o1, filled ? k.k.FP : k.t.card2);
  set_bg(w.o2, w.c_has ? w.c_now : filled ? k.k.FI : k.k.A);
  set_width(w.o2, fill);
  set_hidden(w.o2, fill <= 0);
  set_bg(w.o3, filled ? k.k.FT : k.t.tx);
  style_num(w.o3, LV_STYLE_OUTLINE_WIDTH, filled ? 2 : 0);
  if (filled) {
    style_colour(w.o3, LV_STYLE_OUTLINE_COLOR, k.k.F);
    style_num(w.o3, LV_STYLE_OUTLINE_OPA, LV_OPA_COVER);
  }
  move(w.o3, fill - 11, (H - 22) / 2);
  set_outline(w.o1, w.fb == 1, filled ? k.k.FI : k.k.A);
}

inline void paint_thermostat(Layout &l, Widget &w) {
  Model &m = *l.model;
  const CardLook &k = look_for(m, card_of(m, w));
  const bool filled = k.k.filled;
  double v;
  const bool has = thermostat_value(m, w, &v);
  fade(w.obj, !has || k.unavailable);
  w.faded = has ? 0 : 1;
  char text[24];
  if (has)
    thermostat_text(v, text, sizeof text);
  else
    snprintf(text, sizeof text, "\xE2\x80\x93");  // en dash
  const int s = w.r[2] < w.r[3] ? w.r[2] : w.r[3];
  const lv_font_t *font = thermostat_font(text, s);
  set_font(w.o1, font);
  const int pad = (s - line_height(font)) / 2;
  style_num(w.o1, LV_STYLE_PAD_TOP, pad > 0 ? pad : 0);  // only when the font changed (rule 3)
  set_text_colour(w.o1, k.k.text);
  w.cut = set_text(w.o1, text, thermostat_text_width(s));
  for (int b_i = 0; b_i < 2; b_i++) {
    lv_obj_t *b = b_i == 0 ? w.o2 : w.o3;
    if (!b) continue;
    const int dir = b_i == 0 ? -1 : 1;
    set_bg(b, w.pressed_part == dir ? k.t.card2_pr : k.t.card2);
    set_outline(b, w.fb == 1, filled ? k.k.FI : k.k.A);
    lv_obj_t *icon = lv_obj_get_child(b, 0);
    set_text_colour(icon, k.t.tx);
  }
  lv_obj_invalidate(w.obj);
}

inline void paint_chart(Layout &l, Widget &w) {
  Model &m = *l.model;
  const double *v;
  const bool has = bound_list(m, w.value, &v) >= 0;
  fade(w.obj, !has || card_unavailable(m, w));
  lv_obj_invalidate(w.obj);
}

inline void paint_image(Layout &l, Widget &w) {
  Model &m = *l.model;
  const CardLook &k = look_for(m, card_of(m, w));
  double v = 0;
  const bool has = bound_num(m, w.value, &v);
  fade(w.obj, !has || k.unavailable);
  w.faded = has ? 0 : 1;
  // (2) While its card pulses an image has no box and its icon is acct.
  const bool pulse = card_pulses(m, w);
  if (pulse)
    style_num(w.obj, LV_STYLE_BG_OPA, LV_OPA_TRANSP);
  else
    set_bg(w.obj, w.pressed ? k.t.card2_pr : k.t.card2);
  const int px = image_icon_px(w.r[2], w.r[3]);
  // The box of look 0; badge and bare looks come with their own drawing.
  set_text_colour(w.o1, pulse ? k.k.AT : w.look == 0 && w.c_has ? w.c_on : k.t.acc2);
  set_icon(w.o1, px, has && v > 0 ? (uint32_t) v : ICON_PLACEHOLDER);
  set_outline(w.obj, w.fb == 1, k.k.A);
}

inline void paint_heading(Layout &l, Widget &w) {
  Model &m = *l.model;
  const uint32_t colour = w.c_has ? w.c_now : page_theme(m, w.page).c[C_TX];
  set_text_colour(w.o1, colour);
  set_text_colour(w.o2, colour);
  const char *text = "";
  bound_text(m, w.text, &text);
  w.cut = set_text(w.o2, text, w.r[2] - (w.o1 ? 32 : 0));
}

// (2) The border of `lit` 2 (contract 4.7): at least 2 wide in `acc`, drawn
// over the card's own border. The object's border width stays: LVGL places
// the card's children inside it, so a new width would move every widget of
// the card and lay the screen out again (cards spec B.4).
inline void on_card_ring(lv_event_t *e) {
  Widget *w = static_cast<Widget *>(lv_event_get_user_data(e));
  Layout *l = layout_of(w);
  if (!l || !l->model || !w->ring) return;
  const CardLook &k = look_for(*l->model, w);
  lv_area_t a;
  lv_obj_get_coords(w->obj, &a);
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_opa = LV_OPA_TRANSP;
  d.border_width = w->ring;
  d.border_color = lv_color_hex(k.k.border);
  d.border_opa = LV_OPA_COVER;
  d.radius = k.t.r;
  lv_draw_rect(lv_event_get_layer(e), &d, &a);
}

inline void paint_card(Layout &l, Widget &w) {
  Model &m = *l.model;
  const CardLook &k = look_for(m, &w);
  const Tok &t = k.t;
  // (2) Pulse, then pressed (contract 4.7): one colour, no gradient.
  const bool pulse = w.fb == 2;
  if (pulse || w.pressed) {
    set_bg(w.obj, pulse ? k.k.AF : k.k.filled ? k.k.FP : k.pr);
    style_num(w.obj, LV_STYLE_BG_GRAD_DIR, LV_GRAD_DIR_NONE);
  } else if (k.k.filled) {
    set_bg(w.obj, k.k.F);
    style_num(w.obj, LV_STYLE_BG_GRAD_DIR, LV_GRAD_DIR_NONE);
  } else {
    set_bg(w.obj, t.bg);
    if (t.has_bg2 && t.gd >= 0) {
      style_colour(w.obj, LV_STYLE_BG_GRAD_COLOR, t.bg2);
      style_num(w.obj, LV_STYLE_BG_GRAD_DIR, t.gd == 1 ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER);
    } else {
      style_num(w.obj, LV_STYLE_BG_GRAD_DIR, LV_GRAD_DIR_NONE);
    }
  }
  style_colour(w.obj, LV_STYLE_BORDER_COLOR, k.k.border);
  const uint8_t ring = k.k.border_width > t.bw ? (uint8_t) k.k.border_width : 0;
  if (ring != w.ring) {
    w.ring = ring;
    lv_obj_invalidate(w.obj);  // drawn by on_card_ring
  }
  // The waiting outline: 2 px in acc (on: ion).
  if (w.tap || w.fb) set_outline(w.obj, w.fb == 1, k.k.filled ? k.k.FI : k.k.A);
  set_text_colour(w.o1, pulse ? k.k.AT : k.k.header_icon);
  set_text_colour(w.o2, pulse ? k.k.AT : k.k.text);
  fade(w.o1, k.unavailable);
  fade(w.o2, k.unavailable);
  if (w.o2) {
    const char *text = "";
    bound_text(m, w.text, &text);
    w.cut = set_text(w.o2, text, w.hr[2] - (w.o1 ? 32 : 0));
  }
}

inline void paint_kind(Layout &l, Widget &w) {
  switch (w.kind) {
    case W_HEADING: paint_heading(l, w); break;
    case W_CARD: paint_card(l, w); break;
    case W_LABEL: paint_label(l, w); break;
    case W_BUTTON: paint_button(l, w); break;
    case W_SWITCH: paint_switch(l, w); break;
    case W_SLIDER: paint_slider(l, w); break;
    case W_THERMO: paint_thermostat(l, w); break;
    case W_CHART: paint_chart(l, w); break;
    case W_IMAGE: paint_image(l, w); break;
    default: break;
  }
}

inline void paint_widget(Layout &l, Widget &w) {
  if (!w.obj || !l.model) return;
  Model &m = *l.model;
  if (w.stale) {
    w.stale = 0;
    Page &p = m.pages[w.page];
    if (p.stale) p.stale--;
    if (l.stale) l.stale--;
  }
  // (2) `vis`: LVGL's search for the object under a new press skips it.
  const bool shows = vis_shows(m, w);
  if (w.card >= 0) set_hidden(w.obj, !shows);
  paint_kind(l, w);
  // (2) A widget that has the press and becomes hidden, faded or unavailable
  // loses it; a card loses it when it turns unavailable (contract 7.4). Not
  // a widget that already was: a faded control keeps saying why it cannot
  // send while the tablet cannot (faded_tap).
  const uint8_t quiet = !shows || w.faded || card_unavailable(m, w) ? 1 : 0;
  if (quiet && !w.quiet && press_held(w)) {
    lose_press(w);
    paint_kind(l, w);
  }
  w.quiet = quiet;
}

inline bool widget_dirty(const Widget &w) {
  return bound_dirty(w.text) || bound_dirty(w.value) || bound_dirty(w.vmin) || bound_dirty(w.vmax) ||
         bound_dirty(w.step) || bound_dirty(w.on) || bound_dirty(w.confirm) || bound_dirty(w.vis) ||
         bound_dirty(w.c) || bound_dirty(w.ic) || bound_dirty(w.mark);
}

// Rule 1 of "What a repaint may cost" (cards spec B.4): a widget of the page
// on screen is painted now. One of another page of the shown layout gets a
// stale mark instead; the timer's slices paint it (paint_stale), and
// show_page paints what is still stale on a page before it shows. A layout
// being built is painted as before: at its build, with the values it holds.
inline void repaint(Layout &l, Widget &w) {
  if (!w.obj || !l.model) return;
  if (l.state != L_ACTIVE || w.page == g.page) {
    paint_widget(l, w);
    return;
  }
  if (w.stale) return;
  w.stale = 1;
  l.model->pages[w.page].stale++;
  l.stale++;
}

// Repaints the widgets whose values changed (g.dirty). A card whose `on`,
// `tint` or `un` changed gets its colours worked out again, and then it and
// every widget in it are repainted; a widget whose `c` changed gets its own.
inline void paint_dirty(Layout &l) {
  if (!l.model || !g.dirty_any) return;
  Model &m = *l.model;
  for (uint32_t i = 0; i < m.nw; i++) {
    Widget &w = m.w[i];
    if (!w.obj) continue;
    if (w.kind == W_CARD && (bound_dirty(w.on) || bound_dirty(w.c) || bound_dirty(w.vis)) && card_look_update(m, w)) {
      repaint(l, w);
      for (uint32_t k = 1; k <= w.children && i + k < m.nw; k++) {
        Widget &c = m.w[i + k];
        widget_colour_update(m, c);
        repaint(l, c);
      }
      i += w.children;
      continue;
    }
    if (!widget_dirty(w)) continue;
    if (w.kind != W_CARD && bound_dirty(w.c)) widget_colour_update(m, w);
    repaint(l, w);
  }
}

// Paints the stale widgets of one page now (before it shows).
inline void paint_stale_page(Layout &l, int page) {
  if (!l.model || !l.stale || page < 0 || page >= l.model->npages) return;
  Model &m = *l.model;
  Page &p = m.pages[page];
  for (uint32_t i = p.first; i < (uint32_t) p.first + p.count && i < m.nw && p.stale; i++)
    if (m.w[i].stale) paint_widget(l, m.w[i]);
}

// One slice of stale painting: page by page from where the last slice
// stopped, at most STALE_WIDGETS widgets and while the slice's time lasts.
inline void paint_stale(Layout &l, uint32_t start) {
  if (!l.model || !l.stale) return;
  Model &m = *l.model;
  uint32_t painted = 0;
  for (uint32_t n = 0; n <= m.npages && l.stale; n++) {
    if (l.s_page >= m.npages) l.s_page = 0;
    Page &p = m.pages[l.s_page];
    for (uint32_t i = p.first; i < (uint32_t) p.first + p.count && i < m.nw && p.stale; i++) {
      if (!m.w[i].stale) continue;
      if (painted >= STALE_WIDGETS || now() - start >= SLICE_MS) return;
      paint_widget(l, m.w[i]);
      painted++;
    }
    l.s_page++;
  }
}

// ---------------------------------------------------------------- building

inline lv_obj_t *page_obj(Layout &l, int page) {
  return (l.model && page >= 0 && page < l.model->npages) ? l.model->pages[page].obj : nullptr;
}

inline void paint_nav(Layout &l) {
  if (!l.model) return;
  Model &m = *l.model;
  const Tok t = theme_tokens(m);
  const int page = l.state == L_ACTIVE ? g.page : -1;
  for (int i = 0; i < m.nnav; i++) {
    Nav &n = m.nav[i];
    if (!n.obj) continue;
    const bool act = n.page == page;
    if (n.pressed) {
      set_bg(n.obj, t.card_pr);
    } else if (act) {
      set_bg(n.obj, t.onbg);
    } else {
      style_num(n.obj, LV_STYLE_BG_OPA, LV_OPA_TRANSP);
    }
    set_text_colour(n.icon_obj, act ? t.acc : t.tx2);
  }
}

inline void build_root(Layout &l) {
  Model &m = *l.model;
  const Tok t = theme_tokens(m);
  l.root = plain(g.base);
  if (!l.root) return;
  place(l.root, 0, 0, SCREEN_W, SCREEN_H);
  set_bg(l.root, m.theme.c[C_WALL]);
  lv_obj_add_flag(l.root, LV_OBJ_FLAG_HIDDEN);
  // The side bar comes first, so cards are drawn above it as in the editor.
  l.bar = plain(l.root);
  if (!l.bar) return;
  place(l.bar, 0, 0, 96, SCREEN_H);
  set_bg(l.bar, m.theme.c[C_CARD]);
  for (int i = 0; i < m.nnav; i++) {
    Nav &n = m.nav[i];
    n.obj = plain(l.bar);
    if (!n.obj) return;
    place(n.obj, 16, 16 + 72 * i, 64, 64);
    set_radius(n.obj, t.r2);
    set_clickable(n.obj, true);
    add_events(n.obj, on_nav_event, &n);
    n.icon_obj = icon_box(n.obj, 0, 0, 64, 64, 28);
    set_icon(n.icon_obj, 28, n.icon ? n.icon : ICON_VIEW_DASHBOARD);
  }
  const int target = g.page < m.npages ? g.page : 0;
  for (int p = 0; p < m.npages; p++) {
    lv_obj_t *po = plain(l.root);
    if (!po) return;
    place(po, 0, 0, SCREEN_W, SCREEN_H);
    if (p != target) lv_obj_add_flag(po, LV_OBJ_FLAG_HIDDEN);
    m.pages[p].obj = po;
  }
}

inline void build_widget(Layout &l, Widget &w, lv_obj_t *parent, int ox, int oy) {
  const int x = w.r[0] - ox, y = w.r[1] - oy, W = w.r[2], H = w.r[3];
  switch (w.kind) {
    case W_LABEL: {
      const int px = w.size == 0 ? 14 : w.size == 2 ? 34 : 18;
      w.obj = text_box(parent, x, y, W, H, text_font(px), false);
      break;
    }
    case W_BUTTON: {
      w.obj = plain(parent);
      if (!w.obj) return;
      place(w.obj, x, y, W, H);
      set_radius(w.obj, look_for(*l.model, card_of(*l.model, w)).t.r2);
      const lv_font_t *ifont = icon_font(24);
      // The glyph that is drawn: the fallback when the font lacks w.icon. The
      // text room beside it uses the same width, as layout_button does.
      const int iw = w.icon ? lv_font_get_glyph_width(ifont, icon_or_fallback(ifont, w.icon), 0) : 0;
      if (w.icon) {
        w.o1 = icon_box(w.obj, 0, 0, iw > 0 ? iw : 24, line_height(ifont), 24);
        set_icon(w.o1, 24, w.icon);
      }
      if (w.text.how != B_ABSENT) {
        const bool stacked = w.icon && H >= 64;
        const int tw = W - 12 - (w.icon && !stacked ? iw + 6 : 0);
        w.o2 = text_box(w.obj, 6, 0, tw > 0 ? tw : 1, stacked ? line_height(text_font(15)) : H, text_font(15),
                        !w.icon || stacked);
      }
      break;
    }
    case W_SWITCH: {
      w.obj = plain(parent);
      if (!w.obj) return;
      place(w.obj, x, y, W, H);
      const int tw = W < 64 ? W : 64, th = H < 36 ? H : 36;
      w.o1 = plain(w.obj);
      place(w.o1, 0, (H - th) / 2, tw, th);
      set_radius(w.o1, LV_RADIUS_CIRCLE);
      w.o2 = plain(w.o1);
      place(w.o2, 4, (th - 28) / 2, 28, 28);
      set_radius(w.o2, LV_RADIUS_CIRCLE);
      break;
    }
    case W_SLIDER: {
      w.obj = plain(parent);
      if (!w.obj) return;
      place(w.obj, x, y, W, H);
      lv_obj_add_flag(w.obj, (lv_obj_flag_t) (LV_OBJ_FLAG_OVERFLOW_VISIBLE | LV_OBJ_FLAG_USER_1));
      lv_obj_remove_flag(w.obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
      // The knob reaches 11 px (and a 2 px ring) past the ends. At 40% opacity
      // LVGL draws the slider through a layer of its own area plus this margin.
      lv_obj_add_event_cb(w.obj, on_knob_margin, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
      lv_obj_refresh_ext_draw_size(w.obj);
      w.o1 = plain(w.obj);
      place(w.o1, 0, (H - 14) / 2, W, 14);
      set_radius(w.o1, 7);
      w.o2 = plain(w.o1);
      place(w.o2, 0, 0, 0, 14);
      set_radius(w.o2, 7);
      w.o3 = plain(w.obj);
      place(w.o3, -11, (H - 22) / 2, 22, 22);
      set_radius(w.o3, LV_RADIUS_CIRCLE);
      break;
    }
    case W_THERMO: {
      w.obj = plain(parent);
      if (!w.obj) return;
      place(w.obj, x, y, W, H);
      lv_obj_add_event_cb(w.obj, on_thermo_draw, LV_EVENT_DRAW_MAIN, &w);
      const int s = W < H ? W : H;
      const int tw = thermostat_text_width(s);
      w.o1 = text_box(w.obj, (W - tw) / 2, (H - s) / 2, tw, s, text_font(34), true);
      if (w.change) {
        for (int k = 0; k < 2; k++) {
          lv_obj_t *b = plain(w.obj);
          if (!b) return;
          place(b, k == 0 ? 0 : W - 36, H - 36, 36, 36);
          set_radius(b, LV_RADIUS_CIRCLE);
          lv_obj_t *icon = icon_box(b, 0, 0, 36, 36, 24);
          set_icon(icon, 24, k == 0 ? ICON_MINUS : ICON_PLUS);
          set_clickable(b, true);  // also while faded (on_thermo_button)
          lv_obj_add_event_cb(b, on_thermo_button, LV_EVENT_PRESSED, &w);
          lv_obj_add_event_cb(b, on_thermo_button, LV_EVENT_RELEASED, &w);
          lv_obj_add_event_cb(b, on_thermo_button, LV_EVENT_PRESS_LOST, &w);
          lv_obj_add_event_cb(b, on_thermo_button, LV_EVENT_CLICKED, &w);
          if (k == 0)
            w.o2 = b;
          else
            w.o3 = b;
        }
      }
      break;
    }
    case W_CHART: {
      w.obj = plain(parent);
      if (!w.obj) return;
      place(w.obj, x, y, W, H);
      lv_obj_add_event_cb(w.obj, on_chart_draw, LV_EVENT_DRAW_MAIN, &w);
      break;
    }
    case W_IMAGE: {
      w.obj = plain(parent);
      if (!w.obj) return;
      place(w.obj, x, y, W, H);
      set_radius(w.obj, look_for(*l.model, card_of(*l.model, w)).t.r2);
      const int px = image_icon_px(W, H);
      w.o1 = icon_box(w.obj, 0, 0, W, H, px);
      break;
    }
    default:
      return;
  }
  if (!w.obj || g.obj_fail) return;  // a part is missing: the build fails, nothing is painted
  // Clickable for good, also while faded (on_widget_event).
  const bool acts = w.tap || (w.kind == W_SLIDER && w.change);
  if (acts && w.kind != W_THERMO) {
    set_clickable(w.obj, true);
    add_events(w.obj, on_widget_event, &w);
  }
  widget_colour_update(*l.model, w);
  paint_widget(l, w);
}

// The shell of a page item: a heading whole, or a card without its widgets.
inline void build_shell(Layout &l, Widget &w, lv_obj_t *parent) {
  Model &m = *l.model;
  if (w.kind == W_HEADING) {
    w.obj = plain(parent);
    if (!w.obj) return;
    place(w.obj, w.r[0], w.r[1], w.r[2], w.r[3]);
    const lv_font_t *ifont = icon_font(24);
    if (w.icon) {
      w.o1 = icon_box(w.obj, 0, (w.r[3] - line_height(ifont)) / 2, 24, line_height(ifont), 24);
      set_icon(w.o1, 24, w.icon);
    }
    const int tx = w.o1 ? 32 : 0;
    w.o2 = text_box(w.obj, tx, 0, w.r[2] - tx, w.r[3], text_font(20), false);
    if (g.obj_fail) return;
    widget_colour_update(m, w);
    paint_widget(l, w);
    return;
  }
  if (w.kind != W_CARD) return;
  // The card's colours, once at its build (cards spec B.3).
  card_look_update(m, w);
  const Tok &t = look_for(m, &w).t;
  w.obj = plain(parent);
  if (!w.obj) return;
  place(w.obj, w.r[0], w.r[1], w.r[2], w.r[3]);
  set_radius(w.obj, t.r);
  lv_obj_set_style_border_width(w.obj, t.bw, 0);
  lv_obj_set_style_border_opa(w.obj, LV_OPA_COVER, 0);
  // (2) A card with `tap` or `hold` is a button (cards spec B.4, B.5).
  if (w.tap || w.hold) {
    set_clickable(w.obj, true);
    add_events(w.obj, on_card_event, &w);
  }
  if (w.lit == 2) lv_obj_add_event_cb(w.obj, on_card_ring, LV_EVENT_DRAW_MAIN_END, &w);
  if (w.has_hr) {
    const lv_font_t *ifont = icon_font(24);
    const int hx = w.hr[0] - w.r[0] - t.bw, hy = w.hr[1] - w.r[1] - t.bw;
    if (w.icon) {
      w.o1 = icon_box(w.obj, hx, hy + (w.hr[3] - line_height(ifont)) / 2, 24, line_height(ifont), 24);
      set_icon(w.o1, 24, w.icon);
    }
    const int tx = w.o1 ? 32 : 0;
    w.o2 = text_box(w.obj, hx + tx, hy, w.hr[2] - tx, w.hr[3], text_font(16), false);
  }
  if (g.obj_fail) return;
  paint_widget(l, w);
}

// One build slice. Returns true when the layout is fully built. A card's
// widgets are built a few at a time like the items, so one big card spans
// several slices; the layout stays hidden until it is whole (design 7.5).
inline bool build_step(Layout &l, uint32_t budget_ms) {
  if (!l.model) return true;
  Model &m = *l.model;
  const uint32_t start = now();
  uint32_t objects = 0;
  // At least one step per slice, then stop at the object or time budget.
  auto spent = [&](uint32_t cost) {
    return objects > 0 && (objects + cost > SLICE_OBJECTS || now() - start >= budget_ms);
  };
  if (l.b_stage == 0) {
    build_root(l);
    if (g.obj_fail) return false;
    objects += 2 + m.npages + 2u * m.nnav;
    l.b_stage = 1;
    l.b_item = 0;
    l.b_child = 0;
  }
  while (l.b_stage == 1) {
    if (l.b_item >= m.nw) {
      l.b_stage = 2;
      break;
    }
    Widget &w = m.w[l.b_item];
    const uint32_t left = m.nw - 1u - l.b_item;
    const uint32_t kids = w.kind == W_CARD ? (w.children < left ? w.children : left) : 0;
    lv_obj_t *parent = page_obj(l, w.page);
    if (l.b_child == 0) {
      const uint32_t cost = parent ? Loader::objects_of(w) : 0;
      if (spent(cost)) return false;
      if (parent) build_shell(l, w, parent);
      if (g.obj_fail) return false;
      objects += cost;
      if (!w.obj || kids == 0) {
        l.b_item = (uint16_t) (l.b_item + 1 + kids);
        continue;
      }
      l.b_child = 1;
    }
    // Widget rectangles are absolute (contract 4.3); LVGL places children
    // inside the card's border, so that is taken off.
    const int bw = look_for(m, &w).t.bw;
    const int ox = w.r[0] + bw, oy = w.r[1] + bw;
    while (l.b_child <= kids) {
      Widget &c = m.w[l.b_item + l.b_child];
      const uint32_t cost = Loader::objects_of(c);
      if (spent(cost)) return false;
      build_widget(l, c, w.obj, ox, oy);
      if (g.obj_fail) return false;
      objects += cost;
      l.b_child++;
    }
    l.b_item = (uint16_t) (l.b_item + 1 + kids);
    l.b_child = 0;
  }
  if (l.b_stage == 2) {
    paint_nav(l);
    l.b_stage = 3;
  }
  return true;
}

// Stops a build: the objects made so far are deleted with the layout.
inline void build_abort(Layout &l) { l.b_stage = 3; }

// Deletes the objects below o, the last child first, while the budget lasts.
// True when o has no children left. Pages and cards of any size go over
// several slices this way.
inline bool delete_below(lv_obj_t *o, Budget &b) {
  for (uint32_t n = lv_obj_get_child_count(o); n > 0; n = lv_obj_get_child_count(o)) {
    lv_obj_t *c = lv_obj_get_child(o, (int32_t) n - 1);
    if (!delete_below(c, b) || !b.left()) return false;
    lv_obj_delete(c);
    b.objects--;
  }
  return true;
}

// One deletion slice: pages first (a page never built costs nothing), then
// the side bar and the root. Returns true when every object is gone.
inline bool delete_step(Layout &l, Budget &b) {
  if (!l.root) return true;
  Model *m = l.model;
  while (m && l.d_page < m->npages) {
    lv_obj_t *po = m->pages[l.d_page].obj;
    if (po) {
      if (!delete_below(po, b) || !b.left()) return false;
      lv_obj_delete(po);
      b.objects--;
      m->pages[l.d_page].obj = nullptr;
    }
    l.d_page++;
  }
  if (!delete_below(l.root, b) || !b.left()) return false;
  lv_obj_delete(l.root);
  b.objects--;
  l.root = nullptr;
  l.bar = nullptr;
  return true;
}

inline void show_page(Layout &l, int page) {
  if (!l.model || page < 0 || page >= l.model->npages) return;
  if (l.state == L_ACTIVE) {
    paint_stale_page(l, page);  // what changed while it was not on screen
    for (int p = 0; p < l.model->npages; p++) {
      lv_obj_t *po = l.model->pages[p].obj;
      if (!po || p == page) continue;
      lv_obj_add_flag(po, LV_OBJ_FLAG_HIDDEN);
    }
    if (l.model->pages[page].obj) lv_obj_remove_flag(l.model->pages[page].obj, LV_OBJ_FLAG_HIDDEN);
    g.page = page;
    paint_nav(l);
  }
}

// ---------------------------------------------------------------- engine screens

inline uint32_t ui_colour(int role) {
  Layout *a = active_layout();
  if (a && a->model) return a->model->theme.c[role];
  switch (role) {
    case C_WALL: return DEF_WALL;
    case C_CARD: return DEF_CARD;
    case C_CARD2: case C_CARD2_PR: return DEF_CARD2;
    case C_TX: return DEF_TX;
    case C_TX2: return DEF_TX2;
    case C_ACCF: case C_ACCF_PR: return DEF_ACC;
    case C_ACCT: return DEF_WALL;
    default: return DEF_TX;
  }
}

inline int ui_radius(bool inner) {
  Layout *a = active_layout();
  if (a && a->model) return inner ? a->model->theme.r2 : a->model->theme.r;
  return inner ? 10 : 16;
}

inline void on_question_event(lv_event_t *e) {
  lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    if (target == g.q_yes) set_bg(g.q_yes, ui_colour(C_ACCF_PR));
    if (target == g.q_no) set_bg(g.q_no, ui_colour(C_CARD2_PR));
    after_press();
    return;
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    restyle_overlay();
    return;
  }
  if (code != LV_EVENT_CLICKED) return;
  if (target == g.q_box) return;
  question_answer(target == g.q_yes);
}

inline void restyle_overlay() {
  if (!g.overlay) return;
  set_text_colour(g.mark, ui_colour(C_TX2));
  set_bg(g.strip, ui_colour(C_CARD2));
  set_radius(g.strip, ui_radius(true));
  set_text_colour(g.strip_label, ui_colour(C_TX));
  set_bg(g.q_box, ui_colour(C_CARD));
  set_radius(g.q_box, ui_radius(false));
  set_text_colour(g.q_text, ui_colour(C_TX));
  set_bg(g.q_no, ui_colour(C_CARD2));
  set_bg(g.q_yes, ui_colour(C_ACCF));
  set_radius(g.q_no, ui_radius(true));
  set_radius(g.q_yes, ui_radius(true));
  set_text_colour(lv_obj_get_child(g.q_no, 0), ui_colour(C_TX));
  set_text_colour(lv_obj_get_child(g.q_yes, 0), ui_colour(C_ACCT));
  style_colour(g.question, LV_STYLE_BG_COLOR, ui_colour(C_WALL));
  style_num(g.question, LV_STYLE_BG_OPA, LV_OPA_60);
}

inline void ui_begin() {
  g.screen = lv_screen_active();
  lv_obj_remove_flag(g.screen, LV_OBJ_FLAG_SCROLLABLE);
  g.base = plain(g.screen);
  place(g.base, 0, 0, SCREEN_W, SCREEN_H);

  // Waiting screen: the tablet's name (the panel asks to check it), a
  // headline that says where the tablet is, and the next step, the largest
  // line after the headline and as bright, with a second line under it for
  // the one state that has one (HintText.more). The texts come from
  // hint_show() (tab_core.h), right after this.
  g.waiting = plain(g.base);
  place(g.waiting, 0, 0, SCREEN_W, SCREEN_H);
  set_bg(g.waiting, DEF_WALL);
  lv_obj_t *icon = icon_box(g.waiting, 0, 232, SCREEN_W, 96, 72);
  set_icon(icon, 72, ICON_TABLET);
  set_text_colour(icon, DEF_TX2);
  lv_obj_t *name = text_box(g.waiting, 96, 336, SCREEN_W - 192, 24, text_font(16), true);
  set_text_colour(name, DEF_TX2);
  set_text(name, g.pf.name ? g.pf.name : "", SCREEN_W - 192);
  g.headline = text_box(g.waiting, 96, 372, SCREEN_W - 192, 48, text_font(34), true);
  set_text_colour(g.headline, DEF_TX);
  g.hint = text_box(g.waiting, 96, 432, SCREEN_W - 192, 32, text_font(20), true);
  set_text_colour(g.hint, DEF_TX);
  g.hint2 = text_box(g.waiting, 96, 468, SCREEN_W - 192, 32, text_font(20), true);
  set_text_colour(g.hint2, DEF_TX);

  // Overlay: the "not connected" mark, the message strip, the question box.
  g.overlay = plain(g.screen);
  place(g.overlay, 0, 0, SCREEN_W, SCREEN_H);
  g.mark = icon_box(g.overlay, 32, 736 + (32 - line_height(icon_font(28))) / 2, 32, line_height(icon_font(28)), 28);
  set_icon(g.mark, 28, ICON_LAN_DISCONNECT);
  lv_obj_add_flag(g.mark, LV_OBJ_FLAG_HIDDEN);

  g.strip = plain(g.overlay);
  g.strip_w = STRIP_MIN_W;
  place(g.strip, 96 + (SCREEN_W - 96 - STRIP_MIN_W) / 2, SCREEN_H - 24 - 52, STRIP_MIN_W, 52);
  g.strip_label = text_box(g.strip, 16, 0, STRIP_MIN_W - 32, 52, text_font(15), true);
  lv_obj_add_flag(g.strip, LV_OBJ_FLAG_HIDDEN);

  g.question = plain(g.overlay);
  place(g.question, 0, 0, SCREEN_W, SCREEN_H);
  set_clickable(g.question, true);
  lv_obj_add_event_cb(g.question, on_question_event, LV_EVENT_CLICKED, nullptr);
  g.q_box = plain(g.question);
  place(g.q_box, (SCREEN_W - 560) / 2, (SCREEN_H - 232) / 2, 560, 232);
  set_clickable(g.q_box, true);  // a tap inside the box but not on a button does nothing
  g.q_text = text_box(g.q_box, 24, 32, 512, 64, text_font(18), true);
  for (int k = 0; k < 2; k++) {
    lv_obj_t *b = plain(g.q_box);
    place(b, k == 0 ? 24 : 296, 136, 240, 72);
    set_clickable(b, true);
    lv_obj_t *t = text_box(b, 8, 0, 224, 72, text_font(18), true);
    set_text(t, k == 0 ? "No" : "Yes", 224);
    lv_obj_add_event_cb(b, on_question_event, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(b, on_question_event, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(b, on_question_event, LV_EVENT_PRESS_LOST, nullptr);
    lv_obj_add_event_cb(b, on_question_event, LV_EVENT_CLICKED, nullptr);
    if (k == 0)
      g.q_no = b;
    else
      g.q_yes = b;
  }
  lv_obj_add_flag(g.question, LV_OBJ_FLAG_HIDDEN);
  restyle_overlay();
}

inline void ui_teardown() {
  if (g.overlay) lv_obj_delete(g.overlay);
  if (g.base) lv_obj_delete(g.base);
  g.overlay = g.base = g.waiting = g.mark = g.headline = g.hint = g.hint2 = g.strip = g.strip_label = nullptr;
  g.question = g.q_box = g.q_text = g.q_no = g.q_yes = nullptr;
  g.erase_screen = g.e_title = g.e_text = g.e_erase = g.e_cancel = nullptr;  // children of the overlay
}

inline void show_waiting(bool on) {
  if (!g.waiting) return;
  if (on)
    lv_obj_remove_flag(g.waiting, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(g.waiting, LV_OBJ_FLAG_HIDDEN);
}

inline void show_layout(Layout &l) {
  if (l.root) lv_obj_remove_flag(l.root, LV_OBJ_FLAG_HIDDEN);
  show_waiting(false);
  restyle_overlay();
}

// The strip's text, the strip as wide as it needs (STRIP_MIN_W to
// STRIP_MAX_W), centred over the dashboard right of the mark. It moves only
// when its width changes.
inline void strip_text(const char *text) {
  const lv_font_t *font = lv_obj_get_style_text_font(g.strip_label, LV_PART_MAIN);
  int w = text_width(font, text, strlen(text)) + 32;
  if (w < STRIP_MIN_W) w = STRIP_MIN_W;
  if (w > STRIP_MAX_W) w = STRIP_MAX_W;
  if (w != g.strip_w) {
    g.strip_w = (int16_t) w;
    place(g.strip, 96 + (SCREEN_W - 96 - w) / 2, SCREEN_H - 24 - 52, w, 52);
    lv_obj_set_width(g.strip_label, w - 32);
  }
  set_text(g.strip_label, text, w - 32);
  lv_obj_remove_flag(g.strip, LV_OBJ_FLAG_HIDDEN);
}

// A passing message, STRIP_MS long (over a Wi-Fi note, which comes back after it).
inline void show_strip(const char *text) {
  if (!g.strip) return;
  strip_text(text);
  g.strip_until = now() + STRIP_MS;
  if (!g.strip_until) g.strip_until = 1;
}

// A Wi-Fi note: stays until hide_strip().
inline void show_note(const char *text) {
  if (!g.strip) return;
  strip_text(text);
}

inline void hide_strip() {
  if (g.strip) lv_obj_add_flag(g.strip, LV_OBJ_FLAG_HIDDEN);
}

inline void update_mark() {
  if (!g.mark) return;
  const bool show = active_layout() && !g.connected && now() - g.away_since >= AWAY_MARK_MS;
  const bool hidden = lv_obj_has_flag(g.mark, LV_OBJ_FLAG_HIDDEN);
  if (show && hidden) lv_obj_remove_flag(g.mark, LV_OBJ_FLAG_HIDDEN);
  if (!show && !hidden) lv_obj_add_flag(g.mark, LV_OBJ_FLAG_HIDDEN);
}

inline void question_open(Layout &l, Widget &w, char event, bool has_value, double value) {
  if (!g.question) return;
  g.q_open = true;
  g.q_layout = (int8_t) layout_index(&l);
  g.q_gen = l.gen;
  g.q_widget = (uint16_t) (&w - l.model->w);
  g.q_event = event;
  g.q_has_value = has_value;
  g.q_value = value;
  const char *text = nullptr;
  if (!bound_text(*l.model, w.confirm, &text) || !text || !*text) text = "Are you sure?";
  set_text(g.q_text, text, 512);
  restyle_overlay();
  lv_obj_remove_flag(g.question, LV_OBJ_FLAG_HIDDEN);
}

inline void question_close() {
  g.q_open = false;
  if (g.question) lv_obj_add_flag(g.question, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------- erase question

inline void erase_buttons_idle() {
  set_bg(g.e_cancel, ui_colour(C_ACCF));
  set_bg(g.e_erase, ui_colour(C_CARD2));
}

inline void on_erase_event(lv_event_t *e) {
  lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    if (target == g.e_cancel) set_bg(g.e_cancel, ui_colour(C_ACCF_PR));
    if (target == g.e_erase) set_bg(g.e_erase, ui_colour(C_CARD2_PR));
    after_press();
    return;
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    erase_buttons_idle();
    return;
  }
  if (code == LV_EVENT_CLICKED && (target == g.e_cancel || target == g.e_erase)) erase_answer(target == g.e_erase);
}

inline lv_obj_t *erase_button(lv_obj_t *parent, int x, const char *text, uint32_t text_colour) {
  lv_obj_t *b = plain(parent);
  place(b, x, 432, 240, 72);
  set_clickable(b, true);
  set_radius(b, ui_radius(true));
  lv_obj_t *t = text_box(b, 8, 0, 224, 72, text_font(18), true);
  set_text_colour(t, text_colour);
  set_text(t, text, 224);
  lv_obj_add_event_cb(b, on_erase_event, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(b, on_erase_event, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(b, on_erase_event, LV_EVENT_PRESS_LOST, nullptr);
  lv_obj_add_event_cb(b, on_erase_event, LV_EVENT_CLICKED, nullptr);
  return b;
}

// A full screen above everything else, made only when the question opens.
// Cancel is the highlighted button; a tap anywhere else does nothing.
inline void erase_screen_open() {
  if (!g.overlay || g.erase_screen) return;
  lv_obj_t *s = plain(g.overlay);
  if (!s) return;
  g.erase_screen = s;
  place(s, 0, 0, SCREEN_W, SCREEN_H);
  set_bg(s, ui_colour(C_WALL));
  set_clickable(s, true);
  g.e_title = text_box(s, 96, 268, SCREEN_W - 192, 48, text_font(34), true);
  set_text_colour(g.e_title, ui_colour(C_TX));
  set_text(g.e_title, ERASE_TITLE, SCREEN_W - 192);
  g.e_text = text_box(s, 96, 336, SCREEN_W - 192, 32, text_font(18), true);
  set_text_colour(g.e_text, ui_colour(C_TX2));
  set_text(g.e_text, ERASE_TEXT, SCREEN_W - 192);
  g.e_erase = erase_button(s, SCREEN_W / 2 - 12 - 240, "Erase", ui_colour(C_TX));
  g.e_cancel = erase_button(s, SCREEN_W / 2 + 12, "Cancel", ui_colour(C_ACCT));
  erase_buttons_idle();
}

inline void erase_screen_busy() {
  if (!g.erase_screen) return;
  set_text(g.e_title, ERASE_BUSY, SCREEN_W - 192);
  lv_obj_t *gone[3] = {g.e_text, g.e_erase, g.e_cancel};
  for (lv_obj_t *o : gone)
    if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// Also runs inside a button's own click event, so the delete waits for the
// next LVGL pass.
inline void erase_screen_close() {
  if (g.erase_screen) lv_obj_delete_async(g.erase_screen);
  g.erase_screen = g.e_title = g.e_text = g.e_erase = g.e_cancel = nullptr;
}

}  // namespace detail
}  // namespace tab

#endif  // TAB_UI_H
