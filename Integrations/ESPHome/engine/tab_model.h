// The parsed device layout (contract section 4) and its two-pass loader.
//
// Pass 1 reads the received bytes, checks syntax, shape, version and limits,
// and counts what the model needs. The caller then allocates one block of
// exactly model_size() bytes. Pass 2 reads the bytes again and fills that
// block: records, a string pool with unescaped copies, 256 bytes per text
// slot and 120 numbers per list slot. The received bytes are never written.
//
// Both passes run the same functions (Loader::walk), so they cannot disagree
// about what a layout holds. Version 1 and version 2 layouts are read by the
// same code: `v` changes only the page limits and the per-page draw counts
// (contract 4.2). Every key of version 2 defaults to the version 1 look.
#ifndef TAB_MODEL_H
#define TAB_MODEL_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "lvgl.h"
#include "tab_colour.h"
#include "tab_json.h"

namespace tab {
namespace detail {

// Host tests count what the engine does (TAB_TEST_COUNTERS); the tablet's
// build has none of this.
#ifdef TAB_TEST_COUNTERS
struct TestCounts {
  uint64_t keys;          // object keys looked up
  uint64_t key_compares;  // names compared with them
  uint64_t style_sets;    // styles, flags, positions and texts the engine changed on an object
  uint64_t look_updates;  // card_look_update runs
  uint64_t colour_updates;  // widget_colour_update runs
};
inline TestCounts &test_counts() {
  static TestCounts c;
  return c;
}
#define TAB_COUNT(field) (++::tab::detail::test_counts().field)
#else
#define TAB_COUNT(field) ((void) 0)
#endif

// The screen every rectangle lies on (contract 4.2 `screen`, 4.3).
static const int SCREEN_W = 1280, SCREEN_H = 800;
static const int MAX_PAGES = 64;         // (2) caps.max_pages
static const int MAX_PAGES_V1 = 20;      // a layout with "v": 1
static const int MAX_NORMAL_PAGES = 20;  // (2) pages that are not detail pages
static const int MAX_THEMES = MAX_PAGES + 1;  // the layout's, and one per page
static const int MAX_NAV = 8;
static const int MAX_SLOTS = 1024;
static const int MAX_WIDGETS = 2048;
static const int MAX_LIST = 120;
static const int TEXT_CAP = 256;  // 255 UTF-8 bytes and the NUL
static const uint32_t MAX_LAYOUT = 262144;
static const double MAX_NUMBER = 1e9;  // contract 4.1: numbers are within +-1e9
static const double MAX_COLOUR = 16777215;  // (2) 0xFFFFFF
static const double MAX_CODE_POINT = 0x10FFFF;
static const int MAX_OPTIONS = 16;        // (2) options of a choice
static const int OPTION_TEXT_BYTES = 40;  // (2) a choice text is cut to this
static const int MAX_TRACK = 8;           // (2) colours of a track
// (2) Drawn on one page at most (contract 4.2): a further chart or
// thermostat is skipped, a further track is dropped from its slider.
static const int PAGE_CHARTS = 8, PAGE_ARCS = 8, PAGE_TRACKS = 12;
static const int TTL_DEFAULT = 60, TTL_LEAST = 5, TTL_MOST = 3600;  // (2) detail pages, seconds

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
  W_CHOICE,  // (2)
};

enum BoundHow : uint8_t { B_ABSENT = 0, B_NULL, B_CONST, B_SLOT };

// A bound field: absent, a constant of the field's type, null, or a slot.
struct Bound {
  uint8_t how;
  char type;        // 't' 'b' 'n' 'l' 'i', and 'c' for a colour (a number slot)
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

// Card style colours and the theme role each one replaces ("bg" replaces
// card). The last six are (2): with them a card can carry another theme whole.
enum StyleColour : uint8_t {
  S_BG, S_BG2, S_TX, S_TX2, S_ACC, S_ACC2, S_ACCT, S_CARD2, S_LINE, S_ONBG, S_ONTX,
  S_CARD_PR, S_CARD2_PR, S_ACCF, S_ACCF_PR, S_ONBG_PR, S_ION, S_COUNT,
};
static const char *const STYLE_KEYS[S_COUNT] = {"bg",      "bg2",      "tx",   "tx2",     "acc",     "acc2",
                                                "acct",    "card2",    "line", "onbg",    "ontx",    "card_pr",
                                                "card2_pr", "accf",    "accf_pr", "onbg_pr", "ion"};

struct Theme {
  uint32_t c[C_COUNT];
  int16_t r, r2, bw;
  uint8_t dark;
};

struct CardStyle {
  uint32_t mask;  // bit per StyleColour present
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
  uint8_t tap;       // 0 none, 1 policy, 2 go to page, 3 (2) back
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
  // (2) The keys of version 2 (contract 4.3). Each one absent gives the
  // look of version 1.
  Bound vis;   // a widget: drawn while 1. A card: `un`, the device is not available
  Bound c;     // a widget: its own colour. A card: `tint`
  Bound ic;    // card, button: an icon that replaces `icon`
  Bound mark;  // thermostat, chart: `mark`. Slider: `rate`
  const double *track;  // slider: 2 to 8 colours, or nullptr
  const double *icons;  // choice: code points, 0 for none
  const double *cols;   // choice: colours
  const char *texts;    // choice: the option texts, one after another, each ended by a NUL
  uint8_t track_n, icons_n, cols_n, texts_n;
  uint8_t options;  // choice: the longest of its lists, at most 16; 0: nothing to draw
  uint8_t hold;     // 0 none, 2 go to page
  uint8_t hold_v;   // page
  uint8_t px;       // label: text size wanted, 14 to 255; 0 none
  uint8_t al;       // label: 0 left, 1 centre, 2 right
  uint8_t clk;      // label: 0 text, 1 24 hour clock, 2 12 hour clock
  uint8_t shape;    // button: 0 box, 1 round, 2 bare
  uint8_t isz;      // button: 0 icon 24, 1 large icon
  uint8_t look;     // slider 0 knob, 1 bar; image 0 box, 1 badge, 2 bare; choice 0 cells, 1 dots
  uint8_t dir;      // slider: 0 left to right, 1 bottom to top, 2 top to bottom
  uint8_t nv;       // slider: 1 not faded without a value
  uint8_t rows;     // choice: 1 to 4
  uint8_t st;       // chart: 1 steps
  uint8_t bs;       // thermostat: side of its buttons, 36 to 64
  uint8_t lit;      // card: 0 fill, 1 icon, 2 outline, 3 no change
  // (2) Colours worked out from the state, never while painting (cards spec
  // B.3). A card's are in Model.looks[cl]; a widget keeps those of its own
  // that need a contrast: c_now is its colour (a label's text, a switch's
  // track at 1, a slider's fill, an image's box), c_on what goes on it (the
  // switch's knob, the bar's handle and icon, the image's icon).
  int16_t cl;        // cards: index into Model.looks; -1 for other widgets
  uint8_t c_has;     // c_now holds a colour of the widget's own `c` (or, for a slider and an image, always)
  uint8_t stale;     // a value changed while its page was not on screen: paint before the page shows
  uint32_t c_now, c_on;
};

// (2) One card's colours: its tokens, fixed at the build (a card is on one
// page and a page has one theme), and the nine colours of its state, worked
// out again only when its `on`, `tint` or `un` changes (card_look_update).
struct CardLook {
  Tok t;
  CardColours k;
  uint8_t ready;  // t and k were worked out (at the build)
  uint8_t on, unavailable, has_tint;
  uint32_t tint;
};

struct Page {
  const char *title;
  uint16_t first, count;  // widget records of this page
  lv_obj_t *obj;
  uint8_t sub;    // (2) 1: a detail page (never page 0)
  uint8_t theme;  // (2) index into Model.themes; 0 is the layout's theme
  uint16_t ttl;   // (2) seconds without a touch on a detail page; 0 never
  uint16_t stale; // widgets of this page waiting for their paint (Widget.stale)
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
  Theme theme;    // the layout's theme (also themes[0])
  Theme *themes;  // (2) themes[0] the layout's, then one per page with a `theme`
  Page *pages;
  Widget *w;
  Slot *slots;  // slots[0] is slot 1
  CardStyle *styles;
  CardLook *looks;  // (2) one per card
  uint16_t npages, nw, nslots, nstyles, nlooks;
  uint8_t nnav, nthemes;
  uint32_t objects;  // estimate of LVGL objects, for the memory check
  Nav nav[MAX_NAV];
};

struct Counts {
  uint32_t pages, widgets, ids, slots, text_slots, list_slots, pool, styles, nums, objects;
  uint32_t themes;  // (2) the layout's theme and the page themes
  uint32_t normal;  // (2) pages that are not detail pages
  uint32_t cards;   // (2) one CardLook each
  uint8_t version;  // `v`: 1 or 2
  // (2) Parts the per-page draw counts left out (contract 4.2).
  uint32_t drop_charts, drop_arcs, drop_tracks;
};

inline size_t align8(size_t n) { return (n + 7) & ~(size_t) 7; }

// A table of key names that finds a key with about one comparison, however
// many names it has (cards spec B.2: an item knows 44 keys, and both passes
// look up every key of every item). Built when the program is compiled.
constexpr size_t name_length(const char *s) {
  size_t n = 0;
  while (s[n]) n++;
  return n;
}
constexpr uint32_t key_hash(const char *p, size_t n) {
  return n == 0 ? 0u : (uint32_t) (uint8_t) p[0] * 31u + (uint32_t) (uint8_t) p[n - 1] * 7u + (uint32_t) n * 13u;
}
struct KeyIndex {
  static const int SIZE = 128;  // more than twice the longest table, so a probe is short
  const char *const *names;
  int count;
  uint8_t at[SIZE];  // name index + 1, 0 for an empty place
  constexpr KeyIndex(const char *const *list, int n) : names(list), count(n), at{} {
    for (int i = 0; i < n; i++) {
      uint32_t h = key_hash(list[i], name_length(list[i])) % SIZE;
      while (at[h]) h = (h + 1) % SIZE;
      at[h] = (uint8_t) (i + 1);
    }
  }
  // The index of the name k is, or -1. An escaped key is never a name.
  int find(const json::Span &k) const {
    TAB_COUNT(keys);
    if (k.escaped) return -1;
    for (uint32_t h = key_hash(k.p, k.n) % SIZE; at[h]; h = (h + 1) % SIZE) {
      TAB_COUNT(key_compares);
      const int i = at[h] - 1;
      if (json::key_is(k, names[i])) return i;
    }
    return -1;
  }
};

// Field tables. A scan records where each known key's value starts.
enum TopKey { T_V, T_SLOTS, T_THEME, T_NAV, T_PAGES, T_COUNT };
static constexpr const char *TOP_KEYS[T_COUNT] = {"v", "slots", "theme", "nav", "pages"};
static constexpr KeyIndex TOP_INDEX(TOP_KEYS, T_COUNT);
enum PageKey { P_TITLE, P_W, P_SUB, P_TTL, P_THEME, P_COUNT };
static constexpr const char *PAGE_KEYS[P_COUNT] = {"title", "w", "sub", "ttl", "theme"};
static constexpr KeyIndex PAGE_INDEX(PAGE_KEYS, P_COUNT);
enum ItemKey {
  I_ID, I_T, I_R, I_TEXT, I_ICON, I_SIZE, I_VALUE, I_MIN, I_MAX, I_STEP, I_KIND, I_TAP,
  I_CHANGE, I_CF, I_CONFIRM, I_HR, I_TITLE, I_ON, I_STYLE, I_W,
  // (2)
  I_VIS, I_HOLD, I_C, I_IC, I_TINT, I_LIT, I_UN, I_PX, I_AL, I_CLK, I_SHAPE, I_ISZ,
  I_LOOK, I_DIR, I_TRACK, I_NV, I_RATE, I_MARK, I_BS, I_ST, I_TEXTS, I_ICONS, I_COLS, I_ROWS,
  I_COUNT,
};
static constexpr const char *ITEM_KEYS[I_COUNT] = {
    "id",   "t",     "r",    "text",  "icon",  "size", "value", "min",   "max",   "step",  "kind",
    "tap",  "change", "cf",  "confirm", "hr",  "title", "on",   "style", "w",
    "vis",  "hold",  "c",    "ic",    "tint",  "lit",  "un",    "px",    "al",    "clk",   "shape",
    "isz",  "look",  "dir",  "track", "nv",    "rate", "mark",  "bs",    "st",    "texts", "icons",
    "cols", "rows"};
static constexpr KeyIndex ITEM_INDEX(ITEM_KEYS, I_COUNT);
enum NavKey { N_P, N_ICON, N_COUNT };
static constexpr const char *NAV_KEYS[N_COUNT] = {"p", "icon"};
static constexpr KeyIndex NAV_INDEX(NAV_KEYS, N_COUNT);
enum TapKey { TP_P, TP_GO, TP_BACK, TP_COUNT };
static constexpr const char *TAP_KEYS[TP_COUNT] = {"p", "go", "back"};
static constexpr KeyIndex TAP_INDEX(TAP_KEYS, TP_COUNT);
static constexpr const char *SLOT_REF_KEYS[1] = {"s"};
static constexpr KeyIndex SLOT_REF_INDEX(SLOT_REF_KEYS, 1);
// A theme: the colour roles in contract order, then dark, r, r2, bw.
enum ThemeExtra { TH_DARK = C_COUNT, TH_R, TH_R2, TH_BW, TH_COUNT };
static constexpr const char *THEME_READ_KEYS[TH_COUNT] = {
    "wall", "card", "card_pr", "card2", "card2_pr", "line", "tx", "tx2", "acc", "accf", "accf_pr",
    "acct", "acc2", "onbg", "onbg_pr", "ontx", "ion", "dark", "r", "r2", "bw"};
static constexpr KeyIndex THEME_INDEX(THEME_READ_KEYS, TH_COUNT);
// A card style: the colours of StyleColour, then r, r2, bw, gd.
enum StyleExtra { SX_R = S_COUNT, SX_R2, SX_BW, SX_GD, SX_COUNT };
static constexpr const char *STYLE_READ_KEYS[SX_COUNT] = {
    "bg",      "bg2",      "tx",   "tx2",     "acc",     "acc2", "acct", "card2", "line", "onbg", "ontx",
    "card_pr", "card2_pr", "accf", "accf_pr", "onbg_pr", "ion",  "r",    "r2",    "bw",   "gd"};
static constexpr KeyIndex STYLE_INDEX(STYLE_READ_KEYS, SX_COUNT);

// Scans one object. at[i] is the start of the value of the i-th name, or
// nullptr. A key given twice: the last one counts (in both passes alike).
inline bool scan_object(json::Reader &r, const KeyIndex &names, const char **at) {
  for (int i = 0; i < names.count; i++) at[i] = nullptr;
  if (!r.begin_object()) return false;
  bool first = true;
  json::Span k;
  while (r.next_key(first, &k)) {
    r.ws();
    const int i = names.find(k);
    if (i >= 0) at[i] = r.p;
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
  uint32_t wi = 0, si = 0, ti = 0, ci = 0;
  char *pool = nullptr;
  double *nums = nullptr;
  // (2) What the page being read holds so far, for the per-page draw counts.
  uint32_t page_charts = 0, page_arcs = 0, page_tracks = 0;

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

  // (2) A key that takes a whole number (contract 4.3). Absent or null:
  // *given is false. Not a number, or a fraction: parse.
  bool read_whole(const char *p, bool *given, double *out) {
    *given = false;
    if (!p) return true;
    json::Reader r = at(p);
    if (r.peek() == json::K_NULL) return true;
    double v;
    if (r.peek() != json::K_NUMBER || !r.number(&v) || v != floor(v)) return bad();
    *given = true;
    *out = v;
    return true;
  }

  // (2) A key that only changes the look: a whole number held to lo..hi, so a
  // later contract can add a value without a new key (contract 4.3). def
  // when absent.
  bool read_look(const char *p, int lo, int hi, int def, uint8_t *out) {
    bool given;
    double v = 0;
    if (!read_whole(p, &given, &v)) return false;
    *out = (uint8_t) (!given ? def : v < lo ? lo : v > hi ? hi : (int) v);
    return true;
  }

  // (2) `nv` and `sub` count only when they are exactly 1.
  bool read_one(const char *p, uint8_t *out) {
    bool given;
    double v = 0;
    if (!read_whole(p, &given, &v)) return false;
    *out = given && v == 1 ? 1 : 0;
    return true;
  }

  // (2) A page's `ttl`: below 0 is 0, 1 to 4 is 5, above 3600 is 3600.
  bool read_ttl(const char *p, uint16_t *out) {
    bool given;
    double v = 0;
    if (!read_whole(p, &given, &v)) return false;
    *out = (uint16_t) (!given ? TTL_DEFAULT : v <= 0 ? 0 : v < TTL_LEAST ? TTL_LEAST : v > TTL_MOST ? TTL_MOST : (int) v);
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

  // A bound field of the given type at p (or absent when p is nullptr). A
  // colour ('c', version 2) is a whole number 0 to 0xFFFFFF, or a number slot.
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
      if (!scan_object(r, SLOT_REF_INDEX, ref) || !ref[0]) return bad();
      int64_t slot;
      if (!read_int(ref[0], 1, MAX_SLOTS * 4, &slot)) return false;
      if (!slot_is((uint32_t) slot, type == 'c' ? 'n' : type)) return bad();
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
    } else if (type == 'c') {
      double v;
      if (k != json::K_NUMBER || !r.number(&v) || !(v >= 0 && v <= MAX_COLOUR) || v != floor(v)) return bad();
      b.how = B_CONST;
      b.c.num = v;
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

  // (2) A constant list of whole numbers 0 to most (`track` and `cols`:
  // colours; `icons`: code points). Every entry is checked, also those past
  // the ones kept. Absent or null: 0 entries. *count is the list's length.
  bool check_list(const char *p, double most, uint32_t *count) {
    *count = 0;
    if (!p) return true;
    json::Reader r = at(p);
    if (r.peek() == json::K_NULL) return true;
    if (!r.begin_array()) return bad();
    bool first = true;
    uint32_t cnt = 0;
    while (r.next_item(first)) {
      double v;
      if (!r.number(&v) || !(v >= 0 && v <= most) || v != floor(v)) return bad();
      cnt++;
    }
    if (r.err) return bad();
    *count = cnt;
    return true;
  }

  // (2) Keeps the first `keep` entries of a list check_list passed: in the
  // number pool (fill) or counted (pass 1).
  void store_list(const char *p, uint32_t keep, const double **out, uint8_t *n) {
    *out = nullptr;
    *n = (uint8_t) keep;
    if (!keep) return;
    if (!fill) {
      c.nums += keep;
      return;
    }
    json::Reader r = at(p);
    r.begin_array();
    bool first = true;
    *out = nums;
    for (uint32_t i = 0; i < keep && r.next_item(first); i++) r.number(nums++);
  }

  // (2) A list the way a key reads it: checked, then at most `most_kept`
  // entries kept, none when fewer than `least`.
  bool read_list(const char *p, double most, uint32_t most_kept, uint32_t least, const double **out, uint8_t *n) {
    uint32_t count;
    if (!check_list(p, most, &count)) return false;
    const uint32_t keep = count < least ? 0 : count > most_kept ? most_kept : count;
    store_list(p, keep, out, n);
    return true;
  }

  // (2) The option texts of a choice: the first 16 entries, each cut to 40
  // bytes at a character border, one after another in the pool. An entry
  // that is not text is parse, wherever it stands (contract 4.3).
  bool read_texts(const char *p, const char **out, uint8_t *n) {
    *out = nullptr;
    *n = 0;
    if (!p) return true;
    json::Reader r = at(p);
    if (r.peek() == json::K_NULL) return true;
    if (!r.begin_array()) return bad();
    const char *start = fill ? pool : nullptr;
    bool first = true;
    uint32_t cnt = 0;
    while (r.next_item(first)) {
      json::Span s;
      if (!r.string(&s)) return bad();
      if (cnt >= (uint32_t) MAX_OPTIONS) continue;
      if (fill) {
        const size_t len = json::unescape_max(s, pool, OPTION_TEXT_BYTES);
        pool[len] = 0;
        pool += len + 1;
      } else {
        char tmp[OPTION_TEXT_BYTES];
        c.pool += (uint32_t) json::unescape_max(s, tmp, OPTION_TEXT_BYTES) + 1;
      }
      cnt++;
    }
    if (r.err) return bad();
    *out = start;
    *n = (uint8_t) cnt;
    return true;
  }

  // tap/change/hold: {"p": 0..3}, {"go": page}, or (2) {"back": 1}. Sets kind
  // 0 when absent, and for an object of none of these forms. tap_forms: go
  // and back count (a tap or a hold, not a change).
  bool read_action(const char *p, bool tap_forms, uint8_t *kind, uint8_t *v) {
    *kind = 0;
    *v = 0;
    if (!p) return true;
    json::Reader r = at(p);
    if (r.peek() == json::K_NULL) return true;
    const char *f[TP_COUNT];
    if (!scan_object(r, TAP_INDEX, f)) return bad();
    int64_t x;
    if (f[TP_P]) {
      if (!read_int(f[TP_P], 0, 3, &x)) return false;
      *kind = 1;
      *v = (uint8_t) x;
    } else if (f[TP_GO] && tap_forms) {
      if (!read_int(f[TP_GO], 0, 1000, &x)) return false;
      if ((uint32_t) x < npages) {  // a page that does not exist: display only
        *kind = 2;
        *v = (uint8_t) x;
      }
    } else if (f[TP_BACK] && tap_forms) {
      uint8_t one;
      if (!read_one(f[TP_BACK], &one)) return false;
      if (one) *kind = 3;
    }
    return true;
  }

  static uint8_t kind_of(const json::Span &t) {
    static const char *const NAMES[] = {"heading", "card",  "label", "button", "switch",
                                        "slider",  "thermostat", "chart", "image",  "choice"};
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
      if (json::key_is(t, NAMES[i])) return (uint8_t) (W_HEADING + i);
    return W_NONE;
  }

  // The LVGL objects a widget makes, counted from the keys read, so the
  // memory check and the build slices hold for what is really built.
  static uint32_t objects_of(const Widget &w) {
    switch (w.kind) {
      case W_HEADING: return 3;  // with an icon; without one it makes 2
      case W_CARD: return 3;
      case W_LABEL: return 1;
      case W_BUTTON: return 3;
      case W_SWITCH: return 3;
      case W_SLIDER: return 4;  // every look is drawn as the knob of version 1 so far
      case W_THERMO: return 6;
      case W_CHART: return 1;
      case W_IMAGE: return 2;
      case W_CHOICE: return w.options ? 1 : 0;  // one drawn object; none without an option
      default: return 0;
    }
  }

  bool read_style(const char *p, int16_t *index) {
    *index = -1;
    if (!p) return true;
    json::Reader r = at(p);
    if (r.peek() == json::K_NULL) return true;
    const char *f[SX_COUNT];
    if (!scan_object(r, STYLE_INDEX, f)) return bad();
    CardStyle st{};
    st.gd = -1;
    int64_t x;
    for (int i = 0; i < S_COUNT; i++) {
      if (!f[i]) continue;
      if (!read_int(f[i], 0, 0xFFFFFF, &x)) return false;
      st.c[i] = (uint32_t) x;
      st.mask |= 1u << i;
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

  // (2) A choice's lists, value, rows and look (contract 4.3). Its options
  // are as many as its longest list, at most 16.
  bool read_choice(const char *const *f, Widget &w) {
    if (!read_texts(f[I_TEXTS], &w.texts, &w.texts_n)) return false;
    if (!read_list(f[I_ICONS], MAX_CODE_POINT, MAX_OPTIONS, 1, &w.icons, &w.icons_n)) return false;
    if (!read_list(f[I_COLS], MAX_COLOUR, MAX_OPTIONS, 1, &w.cols, &w.cols_n)) return false;
    uint8_t most = w.texts_n > w.icons_n ? w.texts_n : w.icons_n;
    w.options = most > w.cols_n ? most : w.cols_n;
    if (!read_bound(f[I_VALUE], 'n', &w.value)) return false;
    if (!read_look(f[I_ROWS], 1, 4, 1, &w.rows)) return false;
    return read_look(f[I_LOOK], 0, 1, 0, &w.look);
  }

  // (2) A slider's track: 2 to 8 colours kept, fewer is no track. On a page
  // of a version 2 layout that already has 12 tracks, it is dropped.
  bool read_track(const char *p, Widget &w) {
    uint32_t count;
    if (!check_list(p, MAX_COLOUR, &count)) return false;
    uint32_t keep = count < 2 ? 0 : count > (uint32_t) MAX_TRACK ? (uint32_t) MAX_TRACK : count;
    if (keep && c.version == 2 && ++page_tracks > (uint32_t) PAGE_TRACKS) {
      keep = 0;
      if (!fill) c.drop_tracks++;
    }
    store_list(p, keep, &w.track, &w.track_n);
    return true;
  }

  // One heading, card, or widget inside a card.
  bool read_widget(const char *p, int16_t card, uint16_t page) {
    json::Reader r = at(p);
    const char *f[I_COUNT];
    if (r.peek() != json::K_OBJECT || !scan_object(r, ITEM_INDEX, f)) return bad();
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
    // (2) A further chart or thermostat on a page is skipped like a kind
    // this engine does not know; its id still counts (contract 4.2).
    if (c.version == 2 && kind == W_CHART && ++page_charts > (uint32_t) PAGE_CHARTS) {
      if (!fill) c.drop_charts++;
      return true;
    }
    if (c.version == 2 && kind == W_THERMO && ++page_arcs > (uint32_t) PAGE_ARCS) {
      if (!fill) c.drop_arcs++;
      return true;
    }
    Widget w{};
    int64_t x;
    if (!read_int(f[I_ID], 0, 0xFFFFFFFFll, &x)) return false;
    w.id = (uint32_t) x;
    w.kind = kind;
    w.card = card;
    w.style = -1;
    w.cl = -1;
    w.page = page;
    w.bs = 36;
    w.rows = 1;
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
    // (2) Keys most kinds share: `hold` (a card's, and a widget's but not on
    // a slider, thermostat or choice, contract 4.3), `vis` on a widget of a
    // card, and `c`.
    if (kind != W_HEADING && kind != W_SLIDER && kind != W_THERMO && kind != W_CHOICE) {
      uint8_t hk, hv;
      if (!read_action(f[I_HOLD], true, &hk, &hv)) return false;
      if (hk == 2) {  // a hold keeps only `go`
        w.hold = 2;
        w.hold_v = hv;
      }
    }
    if (!item && !read_bound(f[I_VIS], 'b', &w.vis)) return false;
    if (kind != W_CARD && kind != W_CHOICE && !read_bound(f[I_C], 'c', &w.c)) return false;
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
        if (!read_bound(f[I_IC], 'i', &w.ic)) return false;
        if (!read_bound(f[I_TINT], 'c', &w.c)) return false;
        if (!read_bound(f[I_UN], 'b', &w.vis)) return false;
        if (!read_look(f[I_LIT], 0, 3, 0, &w.lit)) return false;
        w.cl = (int16_t) ci++;
        if (!fill) c.cards++;
        break;
      case W_LABEL: {
        if (!read_bound(f[I_TEXT], 't', &w.text)) return false;
        if (f[I_SIZE]) {
          if (!read_int(f[I_SIZE], 0, 2, &x)) return false;
          w.size = (uint8_t) x;
        } else {
          w.size = 1;
        }
        // `px` wins over `size`; the size rule takes 14 for anything smaller.
        bool given;
        double v = 0;
        if (!read_whole(f[I_PX], &given, &v)) return false;
        w.px = (uint8_t) (!given ? 0 : v < 14 ? 14 : v > 255 ? 255 : (int) v);
        if (!read_look(f[I_AL], 0, 2, 0, &w.al)) return false;
        if (!read_whole(f[I_CLK], &given, &v)) return false;
        w.clk = (uint8_t) (given && (v == 1 || v == 2) ? v : 0);
        // A clock shows `value`; a label without `clk` has no value key.
        if (given && !read_bound(f[I_VALUE], 'n', &w.value)) return false;
        break;
      }
      case W_BUTTON:
        if (!read_bound(f[I_TEXT], 't', &w.text)) return false;
        if (!read_bound(f[I_VALUE], 'b', &w.value)) return false;
        if (!read_bound(f[I_IC], 'i', &w.ic)) return false;
        if (!read_look(f[I_SHAPE], 0, 2, 0, &w.shape)) return false;
        if (!read_look(f[I_ISZ], 0, 1, 0, &w.isz)) return false;
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
        if (kind == W_SLIDER) {
          if (!read_look(f[I_LOOK], 0, 1, 0, &w.look)) return false;
          if (!read_look(f[I_DIR], 0, 2, 0, &w.dir)) return false;
          if (!read_one(f[I_NV], &w.nv)) return false;
          if (!read_bound(f[I_RATE], 'n', &w.mark)) return false;
          if (!read_track(f[I_TRACK], w)) return false;
        } else {
          if (!read_bound(f[I_MARK], 'n', &w.mark)) return false;
          if (!read_look(f[I_BS], 36, 64, 36, &w.bs)) return false;
        }
        break;
      case W_CHART:
        if (!read_bound(f[I_VALUE], 'l', &w.value)) return false;
        if (f[I_KIND]) {
          if (!read_int(f[I_KIND], 0, 1, &x)) return false;
          w.chart = (uint8_t) x;
        }
        if (!read_bound(f[I_MARK], 'n', &w.mark)) return false;
        if (!read_look(f[I_ST], 0, 1, 0, &w.st)) return false;
        break;
      case W_IMAGE:
        if (!read_bound(f[I_VALUE], 'i', &w.value)) return false;
        if (!read_look(f[I_LOOK], 0, 2, 0, &w.look)) return false;
        break;
      case W_CHOICE:
        if (!read_choice(f, w)) return false;
        break;
      default:
        break;
    }
    const uint32_t index = wi++;
    if (!fill) {
      c.widgets++;
      c.objects += objects_of(w);
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

  // Theme keys onto *t, or only checked when t is nullptr. A key that is
  // missing keeps what *t holds (contract 4.2, and 4.8 for a page theme).
  bool read_theme_keys(const char *p, Theme *t) {
    json::Reader r = at(p);
    const char *f[TH_COUNT];
    if (!scan_object(r, THEME_INDEX, f)) return bad();
    int64_t x;
    for (int i = 0; i < C_COUNT; i++) {
      if (!f[i]) continue;
      if (!read_int(f[i], 0, 0xFFFFFF, &x)) return false;
      if (t) t->c[i] = (uint32_t) x;
    }
    static const int16_t limits[4] = {1, 400, 400, 100};
    for (int i = 0; i < 4; i++) {
      if (!f[TH_DARK + i]) continue;
      if (!read_int(f[TH_DARK + i], 0, limits[i], &x)) return false;
      if (!t) continue;
      if (i == 0) t->dark = (uint8_t) x;
      if (i == 1) t->r = (int16_t) x;
      if (i == 2) t->r2 = (int16_t) x;
      if (i == 3) t->bw = (int16_t) x;
    }
    return true;
  }

  bool read_theme() {
    if (fill) memset(&m->theme, 0, sizeof m->theme);
    ti = 1;
    if (!fill) c.themes = 1;
    bool ok = true;
    if (at_top[T_THEME]) {
      json::Reader r = at(at_top[T_THEME]);
      if (r.peek() != json::K_NULL) ok = read_theme_keys(at_top[T_THEME], fill ? &m->theme : nullptr);
    }
    if (ok && fill) m->themes[0] = m->theme;
    return ok;
  }

  // (2) A page's `theme`: a copy of the layout's theme with the page's keys
  // on top. Returns the index in Model.themes, 0 without one. Only pages the
  // limits allow get a place; the keys of the others are still checked.
  bool read_page_theme(const char *p, uint32_t page, uint8_t *index) {
    *index = 0;
    if (!p) return true;
    json::Reader r = at(p);
    if (r.peek() == json::K_NULL) return true;
    if (page >= (uint32_t) MAX_PAGES) return read_theme_keys(p, nullptr);
    Theme *t = nullptr;
    if (fill) {
      t = &m->themes[ti];
      *t = m->themes[0];
    } else {
      c.themes++;
    }
    if (!read_theme_keys(p, t)) return false;
    *index = (uint8_t) ti++;
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
    uint32_t page = 0, normal = 0;
    while (r.next_item(first)) {
      const char *f[P_COUNT];
      json::Reader pr = at(r.p);
      if (pr.peek() != json::K_OBJECT || !scan_object(pr, PAGE_INDEX, f)) return bad();
      const uint32_t first_w = wi;
      page_charts = page_arcs = page_tracks = 0;
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
      // (2) A detail page has `sub` 1 and is not page 0 (contract 4.8).
      uint8_t sub = 0, theme = 0;
      uint16_t ttl = 0;
      if (!read_one(f[P_SUB], &sub) || !read_ttl(f[P_TTL], &ttl)) return false;
      if (page == 0) sub = 0;
      if (!sub) normal++;
      if (!read_page_theme(f[P_THEME], page, &theme)) return false;
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
        pg.sub = sub;
        pg.ttl = ttl;
        pg.theme = theme;
      }
      page++;
      if (!r.skip()) return bad();
    }
    if (r.err) return bad();
    if (!fill) {
      c.pages = page;
      c.normal = normal;
    }
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
      if (nr.peek() != json::K_OBJECT || !scan_object(nr, NAV_INDEX, f)) return bad();
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
    ci = 0;
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
  if (!scan_object(r, TOP_INDEX, ld.at_top)) return "parse";
  if (!r.at_end()) return "parse";
  // Version first, so a newer layout says "version" even if its shape differs.
  if (!ld.at_top[T_V]) return "parse";
  {
    json::Reader vr = ld.at(ld.at_top[T_V]);
    double v;
    if (vr.peek() != json::K_NUMBER || !vr.number(&v)) return "parse";
    if (v != 1 && v != 2) return "version";
    ld.c.version = (uint8_t) v;
  }
  if (!ld.walk()) return *ld.code ? ld.code : "parse";
  // `v` changes the page limits only (contract 4.2): version 1 counts every
  // page, version 2 all pages and the normal ones apart.
  const bool pages_over = ld.c.version == 1
                              ? ld.c.pages > (uint32_t) MAX_PAGES_V1
                              : ld.c.pages > (uint32_t) MAX_PAGES || ld.c.normal > (uint32_t) MAX_NORMAL_PAGES;
  if (pages_over || ld.c.slots > max_slots || ld.c.ids > max_widgets) return "limits";
  return "";
}

// Byte offsets of the parts of the model block.
struct BlockPlan {
  size_t themes, pages, w, slots, styles, looks, nums, text, lists, pool, total;
};

inline BlockPlan model_plan(const Counts &c) {
  BlockPlan b{};
  size_t at = align8(sizeof(Model));
  b.themes = at;
  at = align8(at + c.themes * sizeof(Theme));
  b.pages = at;
  at = align8(at + c.pages * sizeof(Page));
  b.w = at;
  at = align8(at + c.widgets * sizeof(Widget));
  b.slots = at;
  at = align8(at + c.slots * sizeof(Slot));
  b.styles = at;
  at = align8(at + c.styles * sizeof(CardStyle));
  b.looks = at;
  at = align8(at + c.cards * sizeof(CardLook));
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
  m->themes = reinterpret_cast<Theme *>(base + plan.themes);
  m->pages = reinterpret_cast<Page *>(base + plan.pages);
  m->w = reinterpret_cast<Widget *>(base + plan.w);
  m->slots = reinterpret_cast<Slot *>(base + plan.slots);
  m->styles = reinterpret_cast<CardStyle *>(base + plan.styles);
  m->looks = reinterpret_cast<CardLook *>(base + plan.looks);
  m->npages = (uint16_t) counts.pages;
  m->nw = (uint16_t) counts.widgets;
  m->nslots = (uint16_t) counts.slots;
  m->nstyles = (uint16_t) counts.styles;
  m->nlooks = (uint16_t) counts.cards;
  m->nthemes = (uint8_t) counts.themes;
  m->objects = counts.objects + counts.pages + 2;
  Loader ld;
  ld.data = data;
  ld.n = n;
  ld.fill = true;
  ld.m = m;
  ld.c.version = counts.version;
  ld.pool = reinterpret_cast<char *>(base + plan.pool);
  ld.nums = reinterpret_cast<double *>(base + plan.nums);
  json::Reader r(data, n);
  if (!scan_object(r, TOP_INDEX, ld.at_top)) return nullptr;
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
  if (ld.wi != counts.widgets || ld.si != counts.styles || ld.ti != counts.themes || ld.ci != counts.cards)
    return nullptr;
  return m;
}

// (2) The text of option i of a choice, "" when it has none.
inline const char *choice_text(const Widget &w, uint32_t i) {
  if (!w.texts || i >= w.texts_n) return "";
  const char *s = w.texts;
  for (uint32_t k = 0; k < i; k++) s += strlen(s) + 1;
  return s;
}

}  // namespace detail
}  // namespace tab

#endif  // TAB_MODEL_H
