// The parsed device layout (contract section 4) and its two-pass loader.
//
// Pass 1 reads the received bytes, checks syntax, shape, version and limits,
// and counts what the model needs. The caller then allocates one block of
// exactly model_size() bytes. Pass 2 reads the bytes again and fills that
// block: records, a string pool with unescaped copies, 256 bytes per text
// slot and 120 numbers per list slot. The received bytes are never written.
#ifndef TAB_MODEL_H
#define TAB_MODEL_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "lvgl.h"
#include "tab_json.h"

namespace tab {
namespace detail {

// The screen every rectangle lies on (contract 4.2 `screen`, 4.3).
static const int SCREEN_W = 1280, SCREEN_H = 800;
static const int MAX_PAGES = 20;
static const int MAX_NAV = 8;
static const int MAX_SLOTS = 1024;
static const int MAX_WIDGETS = 2048;
static const int MAX_LIST = 120;
static const int TEXT_CAP = 256;  // 255 UTF-8 bytes and the NUL
static const uint32_t MAX_LAYOUT = 262144;
static const double MAX_NUMBER = 1e9;  // contract 4.1: numbers are within +-1e9

// A finite number held to the contract range, as HA's num() does.
inline double in_range(double v) { return v > MAX_NUMBER ? MAX_NUMBER : v < -MAX_NUMBER ? -MAX_NUMBER : v; }

enum WidgetKind : uint8_t {
  W_NONE = 0,
  W_HEADING,
  W_CARD,
  W_LABEL,
  W_BUTTON,
  W_SWITCH,
  W_SLIDER,
  W_THERMO,
  W_CHART,
  W_IMAGE,
};

enum BoundHow : uint8_t { B_ABSENT = 0, B_NULL, B_CONST, B_SLOT };

// A bound field: absent, a constant of the field's type, null, or a slot.
struct Bound {
  uint8_t how;
  char type;        // 't' 'b' 'n' 'l' 'i'
  uint16_t slot;    // 1-based, for B_SLOT
  uint16_t list_n;  // constant list length
  union {
    const char *text;
    const double *list;
    double num;
  } c;
};

// Theme colour roles, in contract order.
enum Colour : uint8_t {
  C_WALL, C_CARD, C_CARD_PR, C_CARD2, C_CARD2_PR, C_LINE, C_TX, C_TX2, C_ACC,
  C_ACCF, C_ACCF_PR, C_ACCT, C_ACC2, C_ONBG, C_ONBG_PR, C_ONTX, C_ION, C_COUNT,
};

static const char *const THEME_KEYS[C_COUNT] = {
    "wall", "card", "card_pr", "card2", "card2_pr", "line", "tx", "tx2", "acc",
    "accf", "accf_pr", "acct", "acc2", "onbg", "onbg_pr", "ontx", "ion"};

// Card style colours and the theme role each one replaces ("bg" replaces card).
enum StyleColour : uint8_t { S_BG, S_BG2, S_TX, S_TX2, S_ACC, S_ACC2, S_ACCT, S_CARD2, S_LINE, S_ONBG, S_ONTX, S_COUNT };
static const char *const STYLE_KEYS[S_COUNT] = {"bg", "bg2", "tx", "tx2", "acc", "acc2", "acct", "card2", "line", "onbg", "ontx"};

struct Theme {
  uint32_t c[C_COUNT];
  int16_t r, r2, bw;
  uint8_t dark;
};

struct CardStyle {
  uint16_t mask;  // bit per StyleColour present
  uint8_t has_r, has_r2, has_bw;
  int8_t gd;      // -1 none, 0 top to bottom, 1 left to right
  int16_t r, r2, bw;
  uint32_t c[S_COUNT];
};

struct Widget {
  uint32_t id;
  int16_t r[4];
  int16_t hr[4];
  uint8_t kind;
  uint8_t size;      // label 0/1/2
  uint8_t chart;     // 0 line, 1 bars
  uint8_t cf;        // ask before sending
  uint8_t tap;       // 0 none, 1 policy, 2 go to page
  uint8_t tap_v;     // policy or page
  uint8_t change;    // 0 none, 1 policy
  uint8_t change_v;  // policy
  uint8_t has_hr;
  uint8_t cut;       // a text of this widget was cut with an ellipsis (diagnostics)
  uint16_t children; // cards: number of widget records that follow
  int16_t card;      // index of the card this widget is in, -1 for page items
  int16_t style;     // cards: index into styles, -1 none
  uint16_t page;
  uint32_t icon;     // fixed icon code point, 0 none
  Bound text, value, vmin, vmax, step, on, confirm;
  // LVGL objects, set while built.
  lv_obj_t *obj, *o1, *o2, *o3, *o4;
  // Touch feedback (contract 7.4).
  uint8_t fb;        // 0 none, 1 waiting outline, 2 pulse
  uint8_t fbp;       // policy of the input that set fb and ovr
  uint8_t ovr;       // 0 none, 1 a sent value is shown (no ack yet), 2 acked
  uint8_t dragging;
  uint8_t pressed;   // a finger is on it (pressed look)
  int8_t pressed_part;  // thermostat: -1 minus, 1 plus, 0 none
  uint8_t faded;     // a control without a value yet, drawn at 40% (set by paint_widget)
  double ovr_value;
  double drag_value;
  uint32_t fb_until;
  uint32_t ovr_until;
};

struct Page {
  const char *title;
  uint16_t first, count;  // widget records of this page
  lv_obj_t *obj;
};

struct Nav {
  uint16_t page;
  uint8_t pressed;
  uint32_t icon;  // 0: the default icon
  lv_obj_t *obj, *icon_obj;
};

struct Slot {
  char type;
  uint8_t null;
  uint16_t list_n;
  double num;
  char *text;    // TEXT_CAP bytes
  double *list;  // MAX_LIST numbers
};

struct Model {
  size_t block_size;
  Theme theme;
  Page *pages;
  Widget *w;
  Slot *slots;  // slots[0] is slot 1
  CardStyle *styles;
  uint16_t npages, nw, nslots, nstyles;
  uint8_t nnav;
  uint32_t objects;  // estimate of LVGL objects, for the memory check
  Nav nav[MAX_NAV];
};

struct Counts {
  uint32_t pages, widgets, ids, slots, text_slots, list_slots, pool, styles, nums, objects;
};

inline size_t align8(size_t n) { return (n + 7) & ~(size_t) 7; }

// Field tables. A scan records where each known key's value starts.
enum TopKey { T_V, T_SLOTS, T_THEME, T_NAV, T_PAGES, T_COUNT };
static const char *const TOP_KEYS[T_COUNT] = {"v", "slots", "theme", "nav", "pages"};
enum PageKey { P_TITLE, P_W, P_COUNT };
static const char *const PAGE_KEYS[P_COUNT] = {"title", "w"};
enum ItemKey {
  I_ID, I_T, I_R, I_TEXT, I_ICON, I_SIZE, I_VALUE, I_MIN, I_MAX, I_STEP, I_KIND, I_TAP,
  I_CHANGE, I_CF, I_CONFIRM, I_HR, I_TITLE, I_ON, I_STYLE, I_W, I_COUNT,
};
static const char *const ITEM_KEYS[I_COUNT] = {"id",   "t",      "r",  "text",    "icon", "size",  "value",
                                               "min",  "max",    "step", "kind",  "tap",  "change", "cf",
                                               "confirm", "hr", "title", "on",    "style", "w"};
enum NavKey { N_P, N_ICON, N_COUNT };
static const char *const NAV_KEYS[N_COUNT] = {"p", "icon"};
enum TapKey { TP_P, TP_GO, TP_COUNT };
static const char *const TAP_KEYS[TP_COUNT] = {"p", "go"};
enum StyleExtra { SX_R = S_COUNT, SX_R2, SX_BW, SX_GD, SX_COUNT };

// Scans one object. at[i] is the start of the value of names[i], or nullptr.
inline bool scan_object(json::Reader &r, const char *const *names, int count, const char **at) {
  for (int i = 0; i < count; i++) at[i] = nullptr;
  if (!r.begin_object()) return false;
  bool first = true;
  json::Span k;
  while (r.next_key(first, &k)) {
    r.ws();
    for (int i = 0; i < count; i++) {
      if (json::key_is(k, names[i])) {
        at[i] = r.p;
        break;
      }
    }
    if (!r.skip()) return false;
  }
  return !r.err;
}

// The decoded types of an escaped slots text, 3 bits a slot (384 bytes of
// internal RAM, so the engine's fixed state stays under 2 KB). One buffer
// serves every Loader: layouts are read one at a time, and each pass decodes
// its slots text into it before it reads a slot type.
static const char SLOT_LETTERS[] = "tbnli";
inline uint8_t *slot_buffer() {
  static uint8_t types[(MAX_SLOTS * 3 + 7) / 8 + 1];
  return types;
}
inline void slot_buffer_set(uint32_t i, char type) {
  const char *at = strchr(SLOT_LETTERS, type);
  const uint32_t code = at && type ? (uint32_t) (at - SLOT_LETTERS) + 1 : 0;
  uint8_t *b = slot_buffer() + (i * 3) / 8;
  const uint32_t shift = (i * 3) % 8;
  uint32_t two = (uint32_t) b[0] | ((uint32_t) b[1] << 8);
  two = (two & ~(7u << shift)) | (code << shift);
  b[0] = (uint8_t) two;
  b[1] = (uint8_t) (two >> 8);
}
inline char slot_buffer_get(uint32_t i) {
  const uint8_t *b = slot_buffer() + (i * 3) / 8;
  const uint32_t code = (((uint32_t) b[0] | ((uint32_t) b[1] << 8)) >> ((i * 3) % 8)) & 7u;
  return code ? SLOT_LETTERS[code - 1] : 0;
}

struct Loader {
  const char *data = nullptr;
  size_t n = 0;
  bool fill = false;
  Model *m = nullptr;
  Counts c{};
  const char *code = "";  // "" or "parse", "version", "limits"
  const char *at_top[T_COUNT]{};
  json::Span slots_raw{};
  uint32_t nslots = 0;
  uint32_t npages = 0;
  // Fill cursors.
  uint32_t wi = 0, si = 0;
  char *pool = nullptr;
  double *nums = nullptr;

  bool bad(const char *why = "parse") {
    if (!*code) code = why;
    return false;
  }

  json::Reader at(const char *p) const { return json::Reader(p, (size_t) (data + n - p)); }

  // The type character of slot i (1-based). A slots text with escapes
  // (canonical JSON never has any) was decoded once by read_slots, up to
  // MAX_SLOTS; past that the layout is refused with `limits` anyway.
  char slot_type_of(uint32_t i) const {
    if (i < 1 || i > nslots) return 0;
    if (!slots_raw.escaped) return slots_raw.p[i - 1];
    return i <= (uint32_t) MAX_SLOTS ? slot_buffer_get(i - 1) : 0;
  }

  // Whether slot i exists with this type. Past MAX_SLOTS in an escaped text
  // any type is taken, so such a layout answers `limits` like any other
  // layout with too many slots (contract 5: parse, version, then limits).
  bool slot_is(uint32_t i, char type) const {
    if (slots_raw.escaped && i > (uint32_t) MAX_SLOTS && i <= nslots) return true;
    return slot_type_of(i) == type;
  }

  bool read_int(const char *p, int64_t lo, int64_t hi, int64_t *out) {
    if (!p) return bad();
    json::Reader r = at(p);
    double v;
    if (r.peek() != json::K_NUMBER || !r.number(&v)) return bad();
    // Range first: casting a value outside int64_t (1e300) is undefined.
    if (!(v >= (double) lo && v <= (double) hi) || v != floor(v)) return bad();
    *out = (int64_t) v;
    return true;
  }

  // [x, y, w, h] in screen pixels. A number outside -4000..4000 is refused.
  // The rectangle is then cut to the 1280x800 screen (contract 4.3); a
  // negative width or height counts as 0. So no widget is ever larger than
  // the screen, whatever the layout says.
  bool read_rect(const char *p, int16_t out[4]) {
    if (!p) return bad();
    json::Reader r = at(p);
    if (!r.begin_array()) return bad();
    bool first = true;
    int k = 0;
    int v4[4] = {0, 0, 0, 0};
    while (r.next_item(first)) {
      double v;
      if (k >= 4 || !r.number(&v)) return bad();
      if (v < -4000 || v > 4000) return bad();
      v4[k++] = (int) (v < 0 ? v - 0.5 : v + 0.5);
    }
    if (r.err || k != 4) return bad();
    auto cut = [](int a, int lim) { return a < 0 ? 0 : a > lim ? lim : a; };
    const int x0 = cut(v4[0], SCREEN_W), y0 = cut(v4[1], SCREEN_H);
    const int x1 = cut(v4[0] + v4[2], SCREEN_W), y1 = cut(v4[1] + v4[3], SCREEN_H);
    out[0] = (int16_t) x0;
    out[1] = (int16_t) y0;
    out[2] = (int16_t) (x1 > x0 ? x1 - x0 : 0);
    out[3] = (int16_t) (y1 > y0 ? y1 - y0 : 0);
    return true;
  }

  // Stores an unescaped copy in the pool (fill) or counts it (pass 1).
  const char *store_text(const json::Span &s) {
    const size_t len = json::unescape(s, nullptr);
    if (!fill) {
      c.pool += (uint32_t) len + 1;
      return "";
    }
    char *dst = pool;
    json::unescape(s, dst);
    dst[len] = 0;
    pool += len + 1;
    return dst;
  }

  // A bound field of the given type at p (or absent when p is nullptr).
  bool read_bound(const char *p, char type, Bound *out) {
    Bound b{};
    b.type = type;
    if (!p) {
      b.how = B_ABSENT;
      if (fill) *out = b;
      return true;
    }
    json::Reader r = at(p);
    const json::Kind k = r.peek();
    if (k == json::K_NULL) {
      b.how = B_NULL;
    } else if (k == json::K_OBJECT) {
      const char *ref[1];
      static const char *const S_KEY[1] = {"s"};
      if (!scan_object(r, S_KEY, 1, ref) || !ref[0]) return bad();
      int64_t slot;
      if (!read_int(ref[0], 1, MAX_SLOTS * 4, &slot)) return false;
      if (!slot_is((uint32_t) slot, type)) return bad();
      b.how = B_SLOT;
      b.slot = (uint16_t) slot;
    } else if (type == 't') {
      json::Span s;
      if (!r.string(&s)) return bad();
      b.how = B_CONST;
      b.c.text = store_text(s);
    } else if (type == 'l') {
      if (!r.begin_array()) return bad();
      bool first = true;
      uint32_t cnt = 0;
      double *dst = fill ? nums : nullptr;
      while (r.next_item(first)) {
        double v;
        if (!r.number(&v)) return bad();
        if (cnt < (uint32_t) MAX_LIST) {
          if (dst) dst[cnt] = in_range(v);
          cnt++;
        }
      }
      if (r.err) return bad();
      b.how = B_CONST;
      b.list_n = (uint16_t) cnt;
      if (fill) {
        b.c.list = nums;
        nums += cnt;
      } else {
        c.nums += cnt;
      }
    } else if (type == 'b' && (k == json::K_TRUE || k == json::K_FALSE)) {
      b.how = B_CONST;
      b.c.num = k == json::K_TRUE ? 1 : 0;
    } else {
      double v;
      if (k != json::K_NUMBER || !r.number(&v)) return bad();
      b.how = B_CONST;
      b.c.num = type == 'b' ? (v != 0 ? 1 : 0) : in_range(v);
    }
    if (fill) *out = b;
    return true;
  }

  // tap/change: {"p": 0..3} or {"go": page}. Sets kind 0 when absent.
  bool read_action(const char *p, bool allow_go, uint8_t *kind, uint8_t *v) {
    *kind = 0;
    *v = 0;
    if (!p) return true;
    json::Reader r = at(p);
    if (r.peek() == json::K_NULL) return true;
    const char *f[TP_COUNT];
    if (!scan_object(r, TAP_KEYS, TP_COUNT, f)) return bad();
    int64_t x;
    if (f[TP_P]) {
      if (!read_int(f[TP_P], 0, 3, &x)) return false;
      *kind = 1;
      *v = (uint8_t) x;
    } else if (f[TP_GO] && allow_go) {
      if (!read_int(f[TP_GO], 0, 1000, &x)) return false;
      if ((uint32_t) x < npages) {  // a page that does not exist: display only
        *kind = 2;
        *v = (uint8_t) x;
      }
    }
    return true;
  }

  static uint8_t kind_of(const json::Span &t) {
    static const char *const NAMES[] = {"heading", "card",       "label", "button", "switch",
                                        "slider",  "thermostat", "chart", "image"};
    for (int i = 0; i < 9; i++)
      if (json::key_is(t, NAMES[i])) return (uint8_t) (W_HEADING + i);
    return W_NONE;
  }

  static uint32_t objects_of(uint8_t kind) {
    switch (kind) {
      case W_HEADING: return 3;  // with an icon; without one it makes 2
      case W_CARD: return 3;
      case W_LABEL: return 1;
      case W_BUTTON: return 3;
      case W_SWITCH: return 3;
      case W_SLIDER: return 4;
      case W_THERMO: return 6;
      case W_CHART: return 1;
      case W_IMAGE: return 2;
      default: return 0;
    }
  }

  bool read_style(const char *p, int16_t *index) {
    *index = -1;
    if (!p) return true;
    json::Reader r = at(p);
    if (r.peek() == json::K_NULL) return true;
    static const char *const KEYS[SX_COUNT] = {"bg", "bg2", "tx", "tx2", "acc", "acc2", "acct", "card2",
                                               "line", "onbg", "ontx", "r", "r2", "bw", "gd"};
    const char *f[SX_COUNT];
    if (!scan_object(r, KEYS, SX_COUNT, f)) return bad();
    CardStyle st{};
    st.gd = -1;
    int64_t x;
    for (int i = 0; i < S_COUNT; i++) {
      if (!f[i]) continue;
      if (!read_int(f[i], 0, 0xFFFFFF, &x)) return false;
      st.c[i] = (uint32_t) x;
      st.mask |= (uint16_t) (1u << i);
    }
    if (f[SX_R]) {
      if (!read_int(f[SX_R], 0, 400, &x)) return false;
      st.r = (int16_t) x;
      st.has_r = 1;
    }
    if (f[SX_R2]) {
      if (!read_int(f[SX_R2], 0, 400, &x)) return false;
      st.r2 = (int16_t) x;
      st.has_r2 = 1;
    }
    if (f[SX_BW]) {
      if (!read_int(f[SX_BW], 0, 100, &x)) return false;
      st.bw = (int16_t) x;
      st.has_bw = 1;
    }
    if (f[SX_GD]) {
      if (!read_int(f[SX_GD], 0, 1, &x)) return false;
      st.gd = (int8_t) x;
    }
    if (fill) m->styles[si] = st;
    *index = (int16_t) si;
    si++;
    if (!fill) c.styles++;
    return true;
  }

  // One heading, card, or widget inside a card.
  bool read_widget(const char *p, int16_t card, uint16_t page) {
    json::Reader r = at(p);
    const char *f[I_COUNT];
    if (r.peek() != json::K_OBJECT || !scan_object(r, ITEM_KEYS, I_COUNT, f)) return bad();
    if (f[I_ID]) c.ids += fill ? 0 : 1;
    if (!f[I_T]) return bad();
    json::Reader tr = at(f[I_T]);
    json::Span t;
    if (!tr.string(&t)) return bad();
    uint8_t kind = kind_of(t);
    const bool item = card < 0;
    if (item && kind != W_HEADING && kind != W_CARD) kind = W_NONE;
    if (!item && (kind == W_HEADING || kind == W_CARD)) kind = W_NONE;
    if (kind == W_NONE) return true;  // a kind this engine does not know: skipped
    Widget w{};
    int64_t x;
    if (!read_int(f[I_ID], 0, 0xFFFFFFFFll, &x)) return false;
    w.id = (uint32_t) x;
    w.kind = kind;
    w.card = card;
    w.style = -1;
    w.page = page;
    if (!read_rect(f[I_R], w.r)) return false;
    if (f[I_ICON]) {
      json::Reader ir = at(f[I_ICON]);
      if (ir.peek() != json::K_NULL) {
        if (!read_int(f[I_ICON], 0, 0x10FFFF, &x)) return false;
        w.icon = (uint32_t) x;
      }
    }
    if (!read_action(f[I_TAP], true, &w.tap, &w.tap_v)) return false;
    if (!read_action(f[I_CHANGE], false, &w.change, &w.change_v)) return false;
    if (f[I_CF]) {
      if (!read_int(f[I_CF], 0, 1, &x)) return false;
      w.cf = (uint8_t) x;
    }
    if (!read_bound(f[I_CONFIRM], 't', &w.confirm)) return false;
    switch (kind) {
      case W_HEADING:
        if (!read_bound(f[I_TEXT], 't', &w.text)) return false;
        break;
      case W_CARD:
        if (f[I_HR]) {
          if (!read_rect(f[I_HR], w.hr)) return false;
          w.has_hr = 1;
        }
        if (!read_bound(f[I_TITLE], 't', &w.text)) return false;
        if (!read_bound(f[I_ON], 'b', &w.on)) return false;
        if (!read_style(f[I_STYLE], &w.style)) return false;
        break;
      case W_LABEL:
        if (!read_bound(f[I_TEXT], 't', &w.text)) return false;
        if (f[I_SIZE]) {
          if (!read_int(f[I_SIZE], 0, 2, &x)) return false;
          w.size = (uint8_t) x;
        } else {
          w.size = 1;
        }
        break;
      case W_BUTTON:
        if (!read_bound(f[I_TEXT], 't', &w.text)) return false;
        break;
      case W_SWITCH:
        if (!read_bound(f[I_VALUE], 'b', &w.value)) return false;
        break;
      case W_SLIDER:
      case W_THERMO:
        if (!read_bound(f[I_VALUE], 'n', &w.value)) return false;
        if (!read_bound(f[I_MIN], 'n', &w.vmin)) return false;
        if (!read_bound(f[I_MAX], 'n', &w.vmax)) return false;
        if (!read_bound(f[I_STEP], 'n', &w.step)) return false;
        break;
      case W_CHART:
        if (!read_bound(f[I_VALUE], 'l', &w.value)) return false;
        if (f[I_KIND]) {
          if (!read_int(f[I_KIND], 0, 1, &x)) return false;
          w.chart = (uint8_t) x;
        }
        break;
      case W_IMAGE:
        if (!read_bound(f[I_VALUE], 'i', &w.value)) return false;
        break;
      default:
        break;
    }
    const uint32_t index = wi++;
    if (!fill) {
      c.widgets++;
      c.objects += objects_of(kind);
    }
    if (kind == W_CARD) {
      uint16_t children = 0;
      if (f[I_W]) {
        json::Reader cr = at(f[I_W]);
        if (cr.peek() != json::K_NULL) {
          if (!cr.begin_array()) return bad();
          bool first = true;
          while (cr.next_item(first)) {
            const uint32_t before = wi;
            if (!read_widget(cr.p, (int16_t) index, page)) return false;
            children = (uint16_t) (children + (wi - before));
            if (!cr.skip()) return bad();
          }
          if (cr.err) return bad();
        }
      }
      w.children = children;
    }
    if (fill) m->w[index] = w;
    return true;
  }

  bool read_pages() {
    if (!at_top[T_PAGES]) return bad();
    json::Reader r = at(at_top[T_PAGES]);
    if (!r.begin_array()) return bad();
    // Count first, so "go" can be checked against the page count.
    {
      json::Reader count = r;
      bool first = true;
      npages = 0;
      while (count.next_item(first)) {
        npages++;
        if (!count.skip()) return bad();
      }
      if (count.err) return bad();
    }
    bool first = true;
    uint32_t page = 0;
    while (r.next_item(first)) {
      const char *f[P_COUNT];
      json::Reader pr = at(r.p);
      if (pr.peek() != json::K_OBJECT || !scan_object(pr, PAGE_KEYS, P_COUNT, f)) return bad();
      const uint32_t first_w = wi;
      const char *title = "";
      if (f[P_TITLE]) {
        json::Reader tr = at(f[P_TITLE]);
        json::Span s;
        if (tr.peek() == json::K_NULL) {
        } else if (!tr.string(&s)) {
          return bad();
        } else {
          title = store_text(s);
        }
      }
      if (f[P_W]) {
        json::Reader wr = at(f[P_W]);
        if (wr.peek() != json::K_NULL) {
          if (!wr.begin_array()) return bad();
          bool wfirst = true;
          while (wr.next_item(wfirst)) {
            if (page < (uint32_t) MAX_PAGES * 4 && !read_widget(wr.p, -1, (uint16_t) page)) return false;
            if (!wr.skip()) return bad();
          }
          if (wr.err) return bad();
        }
      }
      if (fill) {
        Page &pg = m->pages[page];
        pg.title = title;
        pg.first = (uint16_t) first_w;
        pg.count = (uint16_t) (wi - first_w);
      }
      page++;
      if (!r.skip()) return bad();
    }
    if (r.err) return bad();
    if (!fill) c.pages = page;
    return true;
  }

  bool read_nav() {
    if (!at_top[T_NAV]) return true;
    json::Reader r = at(at_top[T_NAV]);
    if (r.peek() == json::K_NULL) return true;
    if (!r.begin_array()) return bad();
    bool first = true;
    uint32_t count = 0;
    while (r.next_item(first)) {
      const char *f[N_COUNT];
      json::Reader nr = at(r.p);
      if (nr.peek() != json::K_OBJECT || !scan_object(nr, NAV_KEYS, N_COUNT, f)) return bad();
      int64_t page, icon = 0;
      if (!read_int(f[N_P], 0, 1000, &page)) return false;
      if (f[N_ICON]) {
        json::Reader ir = at(f[N_ICON]);
        if (ir.peek() != json::K_NULL && !read_int(f[N_ICON], 0, 0x10FFFF, &icon)) return false;
      }
      if (count < (uint32_t) MAX_NAV) {
        if (fill) {
          m->nav[count].page = (uint16_t) page;
          m->nav[count].icon = (uint32_t) icon;
        } else {
          c.objects += 2;
        }
        count++;
      }
      if (!r.skip()) return bad();
    }
    if (r.err) return bad();
    if (fill) m->nnav = (uint8_t) count;
    return true;
  }

  bool read_theme() {
    if (fill) {
      memset(&m->theme, 0, sizeof m->theme);
    }
    if (!at_top[T_THEME]) return true;
    json::Reader r = at(at_top[T_THEME]);
    if (r.peek() == json::K_NULL) return true;
    static const char *const KEYS[C_COUNT + 4] = {
        "wall", "card", "card_pr", "card2", "card2_pr", "line", "tx", "tx2", "acc", "accf", "accf_pr",
        "acct", "acc2", "onbg", "onbg_pr", "ontx", "ion", "dark", "r", "r2", "bw"};
    const char *f[C_COUNT + 4];
    if (!scan_object(r, KEYS, C_COUNT + 4, f)) return bad();
    int64_t x;
    for (int i = 0; i < C_COUNT; i++) {
      if (!f[i]) continue;
      if (!read_int(f[i], 0, 0xFFFFFF, &x)) return false;
      if (fill) m->theme.c[i] = (uint32_t) x;
    }
    const int16_t limits[4] = {1, 400, 400, 100};
    int16_t vals[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++) {
      if (!f[C_COUNT + i]) continue;
      if (!read_int(f[C_COUNT + i], 0, limits[i], &x)) return false;
      vals[i] = (int16_t) x;
    }
    if (fill) {
      m->theme.dark = (uint8_t) vals[0];
      m->theme.r = vals[1];
      m->theme.r2 = vals[2];
      m->theme.bw = vals[3];
    }
    return true;
  }

  bool read_slots() {
    nslots = 0;
    slots_raw = json::Span{"", 0, false};
    if (!at_top[T_SLOTS]) return true;
    json::Reader r = at(at_top[T_SLOTS]);
    if (!r.string(&slots_raw)) return bad();
    // One walk: each character is read (an escape decoded) exactly once and
    // checked to be a slot type. The types of an escaped text are kept for
    // slot_type_of. A text over the limit costs one walk, then `limits`.
    uint32_t count = 0;
    for (size_t k = 0; k < slots_raw.n;) {
      size_t len = 1;
      char t = slots_raw.p[k];
      if (t == '\\') {
        // A surrogate pair is two escapes; neither decodes to a type letter.
        len = slots_raw.p[k + 1] == 'u' ? 6 : 2;
        char tmp[8];
        json::Span one{slots_raw.p + k, len, true};
        json::unescape(one, tmp);
        t = tmp[0];
      }
      // A raw UTF-8 character starts with a byte that is never a type letter.
      if (t != 't' && t != 'b' && t != 'n' && t != 'l' && t != 'i') return bad();
      if (slots_raw.escaped && count < (uint32_t) MAX_SLOTS) slot_buffer_set(count, t);
      if (!fill) {
        if (t == 't') c.text_slots++;
        if (t == 'l') c.list_slots++;
      }
      k += len;
      count++;
    }
    nslots = count;
    if (!fill) c.slots = nslots;
    return true;
  }

  // The whole walk; same code for both passes.
  bool walk() {
    wi = 0;
    si = 0;
    if (!read_slots() || !read_theme() || !read_pages() || !read_nav()) return false;
    return true;
  }
};

// Pass 1. Returns "" or "parse", "version", "limits". Fills counts.
inline const char *model_check(const char *data, size_t n, Loader &ld, uint32_t max_slots = MAX_SLOTS,
                               uint32_t max_widgets = MAX_WIDGETS) {
  ld = Loader();
  ld.data = data;
  ld.n = n;
  json::Reader r(data, n);
  if (r.peek() != json::K_OBJECT) return "parse";
  if (!scan_object(r, TOP_KEYS, T_COUNT, ld.at_top)) return "parse";
  if (!r.at_end()) return "parse";
  // Version first, so a newer layout says "version" even if its shape differs.
  if (!ld.at_top[T_V]) return "parse";
  {
    json::Reader vr = ld.at(ld.at_top[T_V]);
    double v;
    if (vr.peek() != json::K_NUMBER || !vr.number(&v)) return "parse";
    if (v != 1) return "version";
  }
  if (!ld.walk()) return *ld.code ? ld.code : "parse";
  if (ld.c.pages > (uint32_t) MAX_PAGES || ld.c.slots > max_slots || ld.c.ids > max_widgets) return "limits";
  return "";
}

// Byte offsets of the parts of the model block.
struct BlockPlan {
  size_t pages, w, slots, styles, nums, text, lists, pool, total;
};

inline BlockPlan model_plan(const Counts &c) {
  BlockPlan b{};
  size_t at = align8(sizeof(Model));
  b.pages = at;
  at = align8(at + c.pages * sizeof(Page));
  b.w = at;
  at = align8(at + c.widgets * sizeof(Widget));
  b.slots = at;
  at = align8(at + c.slots * sizeof(Slot));
  b.styles = at;
  at = align8(at + c.styles * sizeof(CardStyle));
  b.nums = at;
  at = align8(at + c.nums * sizeof(double));
  b.text = at;
  at = align8(at + (size_t) c.text_slots * TEXT_CAP);
  b.lists = at;
  at = align8(at + (size_t) c.list_slots * MAX_LIST * sizeof(double));
  b.pool = at;
  at = align8(at + c.pool + 8);
  b.total = at;
  return b;
}

inline size_t model_size(const Counts &c) { return model_plan(c).total; }

// Pass 2. block has model_size() bytes. Returns the model or nullptr.
inline Model *model_fill(const char *data, size_t n, const Counts &counts, void *block) {
  const BlockPlan plan = model_plan(counts);
  uint8_t *base = static_cast<uint8_t *>(block);
  memset(base, 0, plan.total);
  Model *m = reinterpret_cast<Model *>(base);
  m->block_size = plan.total;
  m->pages = reinterpret_cast<Page *>(base + plan.pages);
  m->w = reinterpret_cast<Widget *>(base + plan.w);
  m->slots = reinterpret_cast<Slot *>(base + plan.slots);
  m->styles = reinterpret_cast<CardStyle *>(base + plan.styles);
  m->npages = (uint16_t) counts.pages;
  m->nw = (uint16_t) counts.widgets;
  m->nslots = (uint16_t) counts.slots;
  m->nstyles = (uint16_t) counts.styles;
  m->objects = counts.objects + counts.pages + 2;
  Loader ld;
  ld.data = data;
  ld.n = n;
  ld.fill = true;
  ld.m = m;
  ld.pool = reinterpret_cast<char *>(base + plan.pool);
  ld.nums = reinterpret_cast<double *>(base + plan.nums);
  json::Reader r(data, n);
  if (!scan_object(r, TOP_KEYS, T_COUNT, ld.at_top)) return nullptr;
  if (!ld.read_slots()) return nullptr;
  char *text = reinterpret_cast<char *>(base + plan.text);
  double *lists = reinterpret_cast<double *>(base + plan.lists);
  for (uint32_t i = 0; i < ld.nslots; i++) {
    Slot &s = m->slots[i];
    s.type = ld.slot_type_of(i + 1);
    s.null = 1;
    if (s.type == 't') {
      s.text = text;
      text += TEXT_CAP;
    } else if (s.type == 'l') {
      s.list = lists;
      lists += MAX_LIST;
    }
  }
  if (!ld.walk()) return nullptr;
  if (ld.wi != counts.widgets || ld.si != counts.styles) return nullptr;
  return m;
}

}  // namespace detail
}  // namespace tab

#endif  // TAB_MODEL_H
