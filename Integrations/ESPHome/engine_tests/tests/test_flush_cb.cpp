// Host tests for the part of fast_display.h that talks to the tablet:
// flush_cb, setup and set_enabled. Run through engine_tests/run.sh.
//
//   test_flush_cb [name ...]
//
// test_fast_display.cpp builds only the rotation. This one builds the whole
// file, as the firmware does, against the stand-in headers in
// host/fast_display_sim, and plays LVGL, ESPHome and the panel for it. For 90
// and for 270 it checks:
//   - which way flush_cb turns the picture and which side of the panel is
//     which: the picture's pixel (x, y) is the panel's pixel (799 - y, x) at
//     90 and (y, 1279 - x) at 270;
//   - the bands: the area's place on the panel from its top down, each as
//     many rows as its part of the tile holds, in the two tile halves by
//     turns when the copies overlap and in the whole tile when they do not;
//   - the copies: esp_lcd_panel_draw_bitmap with a wait for each copy before
//     the next one starts and before LVGL is told the flush is done, the next
//     band rotated while the copy runs, or draw_pixels_at when the panel
//     handle or its semaphore is missing;
//   - what stays ESPHome's: rotation 0 and 180, another resolution, another
//     colour depth, no memory for the tile.
//
// Exit code 0 only when every check passed.
#include "fast_display.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------- test registry

struct TestCase {
  const char *name;
  void (*fn)();
};
static std::vector<TestCase> &registry() {
  static std::vector<TestCase> r;
  return r;
}
struct Reg {
  Reg(const char *n, void (*f)()) { registry().push_back({n, f}); }
};
#define TEST(name)                         \
  static void name();                      \
  static Reg reg_##name(#name, name);      \
  static void name()

static int g_checks = 0, g_failed = 0;
static const char *g_test = "";
static std::vector<std::string> g_failures;

static bool check(bool ok, int line, const std::string &what) {
  g_checks++;
  if (!ok) {
    g_failed++;
    char buf[64];
    snprintf(buf, sizeof buf, ":%d ", line);
    // The sweeps make many checks: keep and print the first failures only.
    if (g_failures.size() < 100) g_failures.push_back(std::string(g_test) + buf + what);
    if (g_failed <= 20) fprintf(stderr, "  FAIL %s%s%s\n", g_test, buf, what.c_str());
  }
  return ok;
}
#define CHECK(c) check((c), __LINE__, #c)
#define CHECK_STR(a, b) check(std::string(a) == std::string(b), __LINE__, std::string(#a) + " == \"" + std::string(b) + "\" (got \"" + std::string(a) + "\")")
// For the helpers that return what is wrong, or "" when nothing is.
#define CHECK_NONE(problem) check_none((problem), __LINE__)

static bool check_none(const std::string &problem, int line) { return check(problem.empty(), line, problem); }

static std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
static std::string fmt(const char *f, ...) {
  char buf[500];
  va_list ap;
  va_start(ap, f);
  vsnprintf(buf, sizeof buf, f, ap);
  va_end(ap);
  return buf;
}

struct Rng {
  uint32_t s;
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  int upto(int n) { return (int) (next() % (uint32_t) n); }  // 0 .. n-1
  void fill(std::vector<uint16_t> &v) {
    for (uint16_t &p : v) p = (uint16_t) (next() >> 8);
  }
};

// ---------------------------------------------------------------- the made-up tablet

static const int LW = 1280, LH = 800;  // what LVGL draws
static const int PW = 800, PH = 1280;  // the panel
static const size_t TILE = fast_display::TILE_BYTES;
// fast_display.h's two switches as the firmware starts with them, read before
// any test moves one: how much of the tile a flush uses, and overlapped copies.
static const size_t START_TILE_BYTES = fast_display::s_tile_bytes;
static const bool START_ASYNC = fast_display::s_async;
static const esphome::display::ColorBitness RGB565 = esphome::display::COLOR_BITNESS_565;

// sim.h only names these two; the handles point at one of each.
struct esp_lcd_panel_t {
  int unused;
};
struct QueueDefinition {
  int unused;
};

// One call a flush made.
struct Call {
  char kind;              // B esp_lcd_panel_draw_bitmap, P draw_pixels_at, T xSemaphoreTake,
                          // S ESPHome's own flush, R lv_display_flush_ready
  int x, y, width, rows;  // B, P: the place on the panel. S: the area
  const uint16_t *data;   // B, P, S: the pixels
};
static bool same(const Call &a, const Call &b) {
  return a.kind == b.kind && a.x == b.x && a.y == b.y && a.width == b.width && a.rows == b.rows && a.data == b.data;
}

struct Alloc {
  size_t alignment, size;
  uint32_t caps;
};

struct Tablet;
static Tablet *g_tablet = nullptr;         // the one the stand-in functions act on
static lv_display_t *g_default = nullptr;  // LVGL's default display
static std::vector<std::string> g_log;     // what fast_display.h logged, one line each
static std::vector<Alloc> g_allocs;        // what it asked heap_caps_aligned_alloc for
static uint16_t *g_tile = nullptr;         // the tile it got
static bool g_no_memory = false;

static void esphome_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px);

// A tablet at power-on: LVGL, ESPHome's LVGL component and the panel as
// ESPHome's own setup leaves them, before fast_display::setup().
struct Tablet {
  lv_display_t disp{};
  esphome::lvgl::LvglComponent comp;
  esphome::mipi_dsi::MipiDsi dsi;
  esp_lcd_panel_t panel{};
  QueueDefinition lock{};
  // LVGL's input devices: keys, the touch screen, and a pointer that is not polled.
  lv_timer_t keys_timer{16}, touch_timer{16};
  lv_indev_t bare{LV_INDEV_TYPE_POINTER, 50, nullptr, nullptr};
  lv_indev_t touch{LV_INDEV_TYPE_POINTER, 50, &touch_timer, &bare};
  lv_indev_t keys{LV_INDEV_TYPE_KEYPAD, 50, &keys_timer, &touch};

  // The panel's frame buffer, twice: with every copy done the moment it is
  // asked for, and with every copy reading its pixels as late as the tablet's
  // DMA copy may, when the flush waits for it. A rotation that wrote into a
  // tile half still being copied shows as wrong pixels in the second one.
  // `want` is what both must show.
  std::vector<uint16_t> at_once = std::vector<uint16_t>((size_t) PW * PH), late, want;
  std::vector<Call> calls;  // of the last flush, in order
  std::string error;        // the first thing the panel side saw wrong
  bool copying = false;     // a copy has started and was not waited for
  Call copy{};
  // The tile at the last wait. The band that goes out next is rotated before
  // that wait, while the copy runs: the tile is the same when it goes out.
  std::vector<uint16_t> tile_at_wait;
  bool waited = false;
  int fail_copies = 0;  // esp_lcd_panel_draw_bitmap fails: 0 never, 1 always, 2 every second time
  int copies = 0;       // esp_lcd_panel_draw_bitmap calls of the last flush

  int x1 = 0, y1 = 0, x2 = 0, y2 = 0;  // the last area
  std::vector<uint16_t> src;           // and its pixels, LVGL's buffer
  Rng rng{0x7ab1e7u};

  explicit Tablet(int rotation, bool with_handle = true, bool with_semaphore = true,
                  esphome::display::ColorBitness depth = RGB565, int panel_w = PW, int panel_h = PH)
      : dsi((size_t) panel_w, (size_t) panel_h, depth) {
    // fast_display.h keeps its state in file statics: as after power-on.
    free(fast_display::s_tile);
    fast_display::s_tile = nullptr;
    fast_display::s_tile_bytes = START_TILE_BYTES;
    fast_display::s_out = nullptr;
    fast_display::s_panel = nullptr;
    fast_display::s_done = nullptr;
    fast_display::s_stock = nullptr;
    fast_display::s_async = START_ASYNC;
    g_log.clear();
    g_allocs.clear();
    g_tile = nullptr;
    g_no_memory = false;
    g_tablet = this;
    g_default = &disp;
    dsi.sim_set(with_handle ? &panel : nullptr, with_semaphore ? &lock : nullptr);
    disp.flush_cb = esphome_flush;
    disp.user_data = &comp;
    turn(rotation);
    rng.fill(at_once);
    late = want = at_once;
  }
  Tablet(const Tablet &) = delete;
  ~Tablet() {
    g_tablet = nullptr;
    g_default = nullptr;
  }

  // What ESPHome's set_rotation leaves: the rotation, and LVGL's resolution
  // turned with it.
  void turn(int rotation) {
    comp.sim_set((esphome::display::DisplayRotation) rotation, comp.is_paused());
    const bool landscape = rotation == 90 || rotation == 270;
    disp.hor_res = landscape ? dsi.get_native_height() : dsi.get_native_width();
    disp.ver_res = landscape ? dsi.get_native_width() : dsi.get_native_height();
  }
  void pause(bool on) { comp.sim_set(comp.get_rotation(), on); }
  // The part of the tile a flush uses (fast_display.h's switch for comparisons).
  void use_tile(size_t bytes) { fast_display::s_tile_bytes = bytes; }
  void wrong(const std::string &what) {
    if (error.empty()) error = what;
  }

  // The panel takes pixels: they must be in the tile and fit the panel.
  bool takes(const Call &c, const char *who) {
    const size_t n = (size_t) c.width * (size_t) c.rows;
    if (c.x < 0 || c.y < 0 || c.width <= 0 || c.rows <= 0 || c.x + c.width > PW || c.y + c.rows > PH) {
      wrong(fmt("%s outside the panel: x %d, y %d, %d x %d", who, c.x, c.y, c.width, c.rows));
      return false;
    }
    if (g_tile == nullptr || c.data < g_tile || c.data + n > g_tile + TILE / sizeof(uint16_t)) {
      wrong(fmt("%s with pixels that are not in the tile", who));
      return false;
    }
    return true;
  }
  void blit(std::vector<uint16_t> &fb, const Call &c) {
    for (int r = 0; r < c.rows; r++)
      memcpy(&fb[(size_t) (c.y + r) * PW + c.x], c.data + (size_t) r * c.width, (size_t) c.width * sizeof(uint16_t));
  }

  // LVGL flushes one area of new pixels through the flush that is set.
  void draw(int ax1, int ay1, int ax2, int ay2) {
    x1 = ax1, y1 = ay1, x2 = ax2, y2 = ay2;
    src.resize((size_t) (x2 - x1 + 1) * (size_t) (y2 - y1 + 1));
    rng.fill(src);
    calls.clear();
    copies = 0;
    waited = false;
    lv_area_t area{x1, y1, x2, y2};
    disp.flush_cb(&disp, &area, reinterpret_cast<uint8_t *>(src.data()));
  }

  bool copy_starts(int n) const { return fail_copies == 0 || (fail_copies == 2 && n % 2 == 0); }

  // The calls a fast flush of the last area makes, in order.
  std::vector<Call> fast_calls(bool overlap) const {
    const int w = x2 - x1 + 1, h = y2 - y1 + 1;
    const int rnd = (int) comp.draw_rounding;
    const int width = (h + rnd - 1) / rnd * rnd;  // ESPHome sends whole multiples of its rounding
    const bool cw90 = comp.get_rotation() == esphome::display::DISPLAY_ROTATION_90_DEGREES;
    // The area on the panel: its rows are the panel's columns, and its left
    // edge is at the top at 90, its right edge at 270.
    const int px = cw90 ? PW - y1 - h : y1;
    const int py = cw90 ? x1 : PH - 1 - x2;
    // Without overlap a band has the whole tile, with it one half.
    const size_t part = overlap ? fast_display::s_tile_bytes / 2 : fast_display::s_tile_bytes;
    const int fit = std::max(1, (int) (part / ((size_t) width * sizeof(uint16_t))));
    std::vector<Call> out;
    bool pending = false;
    for (int done = 0, band = 0; done < w; done += fit, band++) {
      const int rows = std::min(fit, w - done);
      if (overlap) {
        if (pending) out.push_back({'T', 0, 0, 0, 0, nullptr});  // the copy before it is done
        out.push_back({'B', px, py + done, width, rows, g_tile + (size_t) (band & 1) * (part / sizeof(uint16_t))});
        pending = copy_starts(band);
      } else {
        out.push_back({'P', px, py + done, width, rows, g_tile});
      }
    }
    if (pending) out.push_back({'T', 0, 0, 0, 0, nullptr});  // the last copy, before LVGL goes on
    out.push_back({'R', 0, 0, 0, 0, nullptr});
    return out;
  }

  std::string describe(const Call &c) const {
    if (c.kind == 'T') return "xSemaphoreTake";
    if (c.kind == 'R') return "lv_display_flush_ready";
    const char *name = "ESPHome's flush";
    if (c.kind == 'B') name = "esp_lcd_panel_draw_bitmap";
    if (c.kind == 'P') name = "draw_pixels_at";
    std::string from = "somewhere else";
    if (c.data == src.data()) from = "LVGL's buffer";
    if (g_tile != nullptr && c.data >= g_tile && c.data < g_tile + TILE / sizeof(uint16_t))
      from = fmt("the tile + %d pixels", (int) (c.data - g_tile));
    return fmt("%s x %d, y %d, %d x %d from %s", name, c.x, c.y, c.width, c.rows, from.c_str());
  }
  std::string calls_problem(const std::vector<Call> &expected) const {
    for (size_t i = 0; i < calls.size() || i < expected.size(); i++) {
      if (i >= calls.size()) return fmt("call %zu is missing: %s", i + 1, describe(expected[i]).c_str());
      if (i >= expected.size()) return fmt("call %zu is one too many: %s", i + 1, describe(calls[i]).c_str());
      if (!same(calls[i], expected[i]))
        return fmt("call %zu is [%s], expected [%s]", i + 1, describe(calls[i]).c_str(), describe(expected[i]).c_str());
    }
    return "";
  }

  // The last area on `want`: the picture's pixel (x, y) is the panel's pixel
  // (PW - 1 - y, x) at 90 and (y, PH - 1 - x) at 270.
  void put_wanted() {
    const int w = x2 - x1 + 1, h = y2 - y1 + 1;
    const bool cw90 = comp.get_rotation() == esphome::display::DISPLAY_ROTATION_90_DEGREES;
    for (int r = 0; r < h; r++) {
      for (int c = 0; c < w; c++) {
        const int x = x1 + c, y = y1 + r;
        const int px = cw90 ? PW - 1 - y : y;
        const int py = cw90 ? x : PH - 1 - x;
        want[(size_t) py * PW + px] = src[(size_t) r * w + c];
      }
    }
    // A height that is no multiple of the rounding: the rectangle sent is
    // wider than the area, as ESPHome's own is, and its last columns hold
    // whatever the tile held. They count as drawn.
    const int rnd = (int) comp.draw_rounding;
    const int width = (h + rnd - 1) / rnd * rnd;
    const int px = cw90 ? PW - y1 - h : y1;
    const int py = cw90 ? x1 : PH - 1 - x2;
    for (int row = py; row < py + w && row < PH; row++)
      for (int col = px + h; col < px + width && col < PW; col++)
        want[(size_t) row * PW + col] = at_once[(size_t) row * PW + col];
  }
  std::string panel_problem() const {
    for (const std::vector<uint16_t> *fb : {&at_once, &late}) {
      if (*fb == want) continue;
      size_t at = 0;
      while ((*fb)[at] == want[at]) at++;
      return fmt("panel pixel x %zu, y %zu is %04x, not %04x%s", at % PW, at / PW, (*fb)[at], want[at],
                 fb == &late ? " (with each copy reading its tile half as late as it may)" : "");
    }
    return "";
  }

  // What is wrong with the last flush as a fast one, "" when nothing is.
  std::string fast_problem(bool overlap) {
    put_wanted();
    std::string problem = error;
    if (problem.empty()) problem = calls_problem(fast_calls(overlap));
    if (problem.empty()) problem = panel_problem();
    if (problem.empty()) return "";
    error.clear();
    copying = false;
    at_once = late = want;  // one failure is reported once
    return fmt("rotation %d, area (%d,%d)-(%d,%d), rounding %d, tile %zu bytes%s: %s", (int) comp.get_rotation(), x1,
               y1, x2, y2, (int) comp.draw_rounding, fast_display::s_tile_bytes, overlap ? " in two halves" : "",
               problem.c_str());
  }
  // One area through the fast flush. Returns what is wrong.
  std::string flush(int ax1, int ay1, int ax2, int ay2, bool overlap) {
    draw(ax1, ay1, ax2, ay2);
    return fast_problem(overlap);
  }
  // What is wrong with the last flush as one left to ESPHome: the same area
  // and buffer handed on, nothing sent to the panel by fast_display.h.
  std::string stock_problem() {
    std::string problem = error;
    const Call handed_on{'S', x1, y1, x2 - x1 + 1, y2 - y1 + 1, src.data()};
    if (problem.empty()) problem = calls_problem({handed_on, {'R', 0, 0, 0, 0, nullptr}});
    if (problem.empty() && (at_once != want || late != want)) problem = "the panel changed";
    if (problem.empty()) return "";
    error.clear();
    copying = false;
    at_once = late = want;
    return fmt("rotation %d, area (%d,%d)-(%d,%d): %s", (int) comp.get_rotation(), x1, y1, x2, y2, problem.c_str());
  }
  std::string kinds() const {
    std::string s;
    for (const Call &c : calls) s += c.kind;
    return s;
  }
};

// ---------------------------------------------------------------- what sim.h leaves to the test

void sim_log(char level, const char *tag, const char *format, ...) {
  char buf[300];
  va_list ap;
  va_start(ap, format);
  vsnprintf(buf, sizeof buf, format, ap);
  va_end(ap);
  g_log.push_back(std::string(1, level) + " " + tag + ": " + buf);
}

void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
  g_allocs.push_back({alignment, size, caps});
  if (g_no_memory) return nullptr;
  g_tile = static_cast<uint16_t *>(aligned_alloc(alignment, size));
  return g_tile;
}

lv_display_t *lv_display_get_default() { return g_default; }

lv_indev_t *lv_indev_get_next(lv_indev_t *indev) { return indev == nullptr ? &g_tablet->keys : indev->next; }

// The DMA copy of the tablet: it starts here, and the panel gives its
// semaphore when it is done.
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x_start, int y_start, int x_end, int y_end,
                                    const void *color_data) {
  Tablet &t = *g_tablet;
  const Call c{'B', x_start, y_start, x_end - x_start, y_end - y_start, static_cast<const uint16_t *>(color_data)};
  t.calls.push_back(c);
  if (panel != &t.panel) t.wrong("esp_lcd_panel_draw_bitmap without the panel's handle");
  if (t.copying) t.wrong("a copy starts while the one before it is still under way");
  if (t.waited && memcmp(t.tile_at_wait.data(), g_tile, TILE) != 0)
    t.wrong("a band was rotated after the wait for the copy before it, not while that copy ran");
  t.waited = false;
  if (!t.takes(c, "a copy") || !t.copy_starts(t.copies++)) return ESP_FAIL;
  t.blit(t.at_once, c);
  t.copying = true;
  t.copy = c;
  return ESP_OK;
}

int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
  Tablet &t = *g_tablet;
  t.calls.push_back({'T', 0, 0, 0, 0, nullptr});
  if (semaphore != &t.lock) t.wrong("xSemaphoreTake without the panel's semaphore");
  if (ticks != portMAX_DELAY) t.wrong("the wait for a copy has a time limit");
  if (!t.copying) {
    t.wrong("a wait with no copy under way: the tablet would hang");
    return 0;
  }
  t.blit(t.late, t.copy);  // the copy may read the tile as late as now
  t.copying = false;
  t.tile_at_wait.assign(g_tile, g_tile + TILE / sizeof(uint16_t));
  t.waited = true;
  return 1;
}

void esphome::mipi_dsi::MipiDsi::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                                                display::ColorOrder order, display::ColorBitness bitness,
                                                bool big_endian, int x_offset, int y_offset, int x_pad) {
  Tablet &t = *g_tablet;
  const Call c{'P', x_start, y_start, w, h, reinterpret_cast<const uint16_t *>(ptr)};
  t.calls.push_back(c);
  if (this != &t.dsi) t.wrong("draw_pixels_at on another display");
  if (order != display::COLOR_ORDER_RGB || bitness != display::COLOR_BITNESS_565 || big_endian || x_offset != 0 ||
      y_offset != 0 || x_pad != 0)
    t.wrong("draw_pixels_at with pixels that are not plain RGB565 (order, depth, byte order, offsets or padding)");
  if (t.copying) t.wrong("draw_pixels_at while a copy is under way");
  if (!t.takes(c, "draw_pixels_at")) return;
  t.blit(t.at_once, c);
  t.blit(t.late, c);
}

void lv_display_flush_ready(lv_display_t *disp) {
  Tablet &t = *g_tablet;
  t.calls.push_back({'R', 0, 0, 0, 0, nullptr});
  if (disp != &t.disp) t.wrong("lv_display_flush_ready for another display");
  if (t.copying) t.wrong("the flush is done while a copy is still under way");
}

// ESPHome's own flush (LvglComponent::flush_cb_), the one setup() replaces
// and keeps for the rotations it does not take.
static void esphome_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  Tablet &t = *g_tablet;
  t.calls.push_back({'S', area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area),
                     reinterpret_cast<const uint16_t *>(px)});
  if (disp != &t.disp) t.wrong("ESPHome's flush for another display");
  lv_display_flush_ready(disp);
}

static std::string last_log() { return g_log.empty() ? "" : g_log.back(); }

static std::string times(const char *s, int n) {
  std::string out;
  for (int i = 0; i < n; i++) out += s;
  return out;
}

// ---------------------------------------------------------------- setup()

// The line setup() logs when the fast flush is on.
static std::string active_log(int rotation, const char *copies, int gesture_px, int touch_read_ms) {
  return fmt("I fast_display: CPU-rotate flush active (%d degrees, %s DMA, gesture %d px, touch read %d ms)", rotation,
             copies, gesture_px, touch_read_ms);
}

// 90 and 270: the fast flush is on, with the tile and the touch settings.
TEST(setup_turns_the_fast_flush_on) {
  for (int rotation : {90, 270}) {
    Tablet t(rotation);
    CHECK(fast_display::setup(&t.dsi));
    CHECK(t.disp.flush_cb == fast_display::flush_cb);
    CHECK_STR(last_log(), active_log(rotation, "overlapped", 30, 5));
    // The tile: asked for once, whole, in internal RAM the DMA copy can read.
    CHECK(g_allocs.size() == 1 && g_allocs[0].alignment == 64 && g_allocs[0].size == TILE &&
          g_allocs[0].caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    // Swipes start sooner and touch is read more often; keys are left alone.
    CHECK(t.touch.gesture_min_distance == 30 && t.touch_timer.period == 5);
    CHECK(t.bare.gesture_min_distance == 30);
    CHECK(t.keys.gesture_min_distance == 50 && t.keys_timer.period == 16);
    CHECK(t.calls.empty() && t.error.empty());
    // A second setup() keeps the tile and still knows ESPHome's flush.
    CHECK(fast_display::setup(&t.dsi, 40, 8));
    CHECK_STR(last_log(), active_log(rotation, "overlapped", 40, 8));
    CHECK(g_allocs.size() == 1);
    CHECK(t.touch.gesture_min_distance == 40 && t.touch_timer.period == 8);
    CHECK(t.disp.flush_cb == fast_display::flush_cb && fast_display::s_stock == esphome_flush);
  }
  // Without the panel's handle or its semaphore the copies cannot overlap.
  for (int missing = 1; missing <= 3; missing++) {
    Tablet t(90, !(missing & 1), !(missing & 2));
    CHECK(fast_display::setup(&t.dsi));
    CHECK(t.disp.flush_cb == fast_display::flush_cb);
    CHECK_STR(last_log(), active_log(90, "blocking", 30, 5));
  }
}

// Anything else: setup() says why, ESPHome's flush stays, and set_enabled()
// cannot switch the fast one on.
TEST(setup_keeps_esphomes_flush) {
  const char *const not_ours = "not a 90- or 270-degree software-rotated LVGL display; keeping the stock flush";
  const char *const depth = "panel colour depth differs from LVGL's; keeping the stock flush";
  const char *const memory = "no internal RAM for the 32768-byte tile; keeping the stock flush";
  enum Odd { NONE, NO_MEMORY, NO_DISPLAY, NO_COMPONENT, NO_PANEL };
  struct Case {
    const char *what;
    int rotation;
    int hor, ver;  // LVGL's resolution, 0: as ESPHome sets it for the rotation
    esphome::display::ColorBitness depth;
    int panel_w, panel_h;
    Odd odd;
    const char *log;
  };
  const Case cases[] = {
      {"rotation 0", 0, 0, 0, RGB565, PW, PH, NONE, not_ours},
      {"rotation 180", 180, 0, 0, RGB565, PW, PH, NONE, not_ours},
      // On a square panel only the rotation tells the upright ways from the others.
      {"rotation 0, square panel", 0, 0, 0, RGB565, 800, 800, NONE, not_ours},
      {"rotation 180, square panel", 180, 0, 0, RGB565, 800, 800, NONE, not_ours},
      {"90, LVGL two pixels narrower", 90, PH - 2, PW, RGB565, PW, PH, NONE, not_ours},
      {"90, LVGL two pixels lower", 90, PH, PW - 2, RGB565, PW, PH, NONE, not_ours},
      {"270, LVGL two pixels narrower", 270, PH - 2, PW, RGB565, PW, PH, NONE, not_ours},
      {"270, LVGL two pixels lower", 270, PH, PW - 2, RGB565, PW, PH, NONE, not_ours},
      {"90, LVGL upright", 90, PW, PH, RGB565, PW, PH, NONE, not_ours},
      {"90, panel with 24-bit colour", 90, 0, 0, esphome::display::COLOR_BITNESS_888, PW, PH, NONE, depth},
      {"270, panel with 8-bit colour", 270, 0, 0, esphome::display::COLOR_BITNESS_332, PW, PH, NONE, depth},
      {"90, no memory for the tile", 90, 0, 0, RGB565, PW, PH, NO_MEMORY, memory},
      {"270, no memory for the tile", 270, 0, 0, RGB565, PW, PH, NO_MEMORY, memory},
      {"90, no LVGL display", 90, 0, 0, RGB565, PW, PH, NO_DISPLAY, not_ours},
      {"90, display without ESPHome's component", 90, 0, 0, RGB565, PW, PH, NO_COMPONENT, not_ours},
      {"90, no panel given", 90, 0, 0, RGB565, PW, PH, NO_PANEL, not_ours},
  };
  for (const Case &c : cases) {
    Tablet t(c.rotation, true, true, c.depth, c.panel_w, c.panel_h);
    if (c.hor != 0) t.disp.hor_res = c.hor, t.disp.ver_res = c.ver;
    g_no_memory = c.odd == NO_MEMORY;
    if (c.odd == NO_DISPLAY) g_default = nullptr;
    if (c.odd == NO_COMPONENT) t.disp.user_data = nullptr;
    const std::string what = std::string(c.what) + ": ";
    CHECK_NONE(fast_display::setup(c.odd == NO_PANEL ? nullptr : &t.dsi) ? what + "setup() returned true" : "");
    CHECK_NONE(t.disp.flush_cb == esphome_flush ? "" : what + "ESPHome's flush was replaced");
    CHECK_NONE(last_log() == fmt("W fast_display: %s", c.log) ? "" : what + "the log says \"" + last_log() + "\"");
    // The touch settings do not depend on the screen.
    CHECK_NONE(t.touch.gesture_min_distance == 30 && t.touch_timer.period == 5 ? "" : what + "no touch settings");
    fast_display::set_enabled(true);
    CHECK_NONE(t.disp.flush_cb == esphome_flush ? "" : what + "set_enabled(true) switched the fast flush on");
    if (t.disp.flush_cb != esphome_flush) continue;
    // What LVGL flushes goes to ESPHome's flush.
    t.disp.user_data = &t.comp;
    t.draw(10, 20, 109, 79);
    const std::string drawn = t.stock_problem();
    CHECK_NONE(drawn.empty() ? drawn : what + drawn);
  }
}

// ---------------------------------------------------------------- one flush

// The whole screen in one area, and in the eight 1280x100 strips LVGL flushes
// with the tablet's 12% buffer. Tile size and rounding as on the tablet.
TEST(flush_cb_whole_screen) {
  for (int rotation : {90, 270}) {
    Tablet t(rotation);
    if (!CHECK(fast_display::setup(&t.dsi))) continue;
    for (int overlap = 1; overlap >= 0; overlap--) {
      fast_display::s_async = overlap != 0;
      CHECK_NONE(t.flush(0, 0, LW - 1, LH - 1, overlap != 0));
      for (int y = 0; y < LH; y += 100) CHECK_NONE(t.flush(0, y, LW - 1, y + 99, overlap != 0));
    }
  }
}

// One strip, call by call, with the switches as the firmware starts with
// them: the whole 32 KB tile, overlapped copies. Half the tile holds 81 panel
// rows of 100 pixels, so the 1280 rows are 16 copies: each waited for before
// the next starts, the last before LVGL goes on. The stand-in for the copy
// also sees that the next band is rotated while a copy runs, not after the
// wait. The whole tile holds 163 rows: 8 draws, no waits.
TEST(flush_cb_waits_for_every_copy) {
  CHECK(START_TILE_BYTES == TILE && TILE == 32 * 1024 && START_ASYNC);
  for (int rotation : {90, 270}) {
    Tablet t(rotation);
    if (!CHECK(fast_display::setup(&t.dsi))) continue;
    t.draw(0, 300, LW - 1, 399);
    CHECK_STR(t.kinds(), "B" + times("TB", 15) + "TR");
    CHECK_NONE(t.fast_problem(true));
    fast_display::s_async = false;
    t.draw(0, 300, LW - 1, 399);
    CHECK_STR(t.kinds(), times("P", 8) + "R");
    CHECK_NONE(t.fast_problem(false));
  }
}

// A copy that did not start gives no semaphore: waiting for it would hang the
// tablet. The flush goes on and ends, and the next one is whole again.
TEST(flush_cb_does_not_wait_for_a_failed_copy) {
  for (int rotation : {90, 270}) {
    Tablet t(rotation);
    if (!CHECK(fast_display::setup(&t.dsi))) continue;
    t.fail_copies = 1;  // none starts
    t.draw(0, 300, LW - 1, 399);
    CHECK_STR(t.kinds(), times("B", 16) + "R");
    CHECK_STR(t.error, "");
    CHECK_NONE(t.calls_problem(t.fast_calls(true)));
    t.fail_copies = 2;  // every second one does not
    t.draw(0, 300, LW - 1, 399);
    CHECK_STR(t.kinds(), times("BTB", 8) + "R");
    CHECK_STR(t.error, "");
    CHECK_NONE(t.calls_problem(t.fast_calls(true)));
    t.fail_copies = 0;
    CHECK_NONE(t.flush(0, 0, LW - 1, LH - 1, true));
  }
}

// While ESPHome's LVGL is paused nothing goes to the panel, and LVGL is still
// told the flush is done.
TEST(flush_cb_draws_nothing_while_paused) {
  for (int rotation : {90, 270}) {
    Tablet t(rotation);
    if (!CHECK(fast_display::setup(&t.dsi))) continue;
    for (int overlap = 1; overlap >= 0; overlap--) {
      fast_display::s_async = overlap != 0;
      t.pause(true);
      t.draw(0, 300, LW - 1, 399);
      CHECK_STR(t.kinds(), "R");
      CHECK(t.error.empty() && t.at_once == t.want && t.late == t.want);
      t.pause(false);
      CHECK_NONE(t.flush(0, 300, LW - 1, 399, overlap != 0));
    }
  }
}

// No panel handle, or no semaphore to wait on: every band goes through
// ESPHome's draw_pixels_at, which returns when the pixels are there, from the
// whole tile.
TEST(flush_cb_blocking_without_handle_or_semaphore) {
  for (int rotation : {90, 270}) {
    for (int missing = 1; missing <= 3; missing++) {
      Tablet t(rotation, !(missing & 1), !(missing & 2));
      if (!CHECK(fast_display::setup(&t.dsi))) continue;
      CHECK_NONE(t.flush(0, 0, LW - 1, LH - 1, false));
      CHECK_NONE(t.flush(0, 300, LW - 1, 399, false));
      CHECK_NONE(t.flush(640, 400, 641, 401, false));
    }
  }
}

// The rotation is read at every flush. 0 and 180 go to ESPHome's flush with
// the same area and buffer; the other landscape way is turned the other way.
TEST(flush_cb_leaves_0_and_180_to_esphome) {
  for (int start : {90, 270}) {
    Tablet t(start);
    if (!CHECK(fast_display::setup(&t.dsi))) continue;
    for (int overlap = 1; overlap >= 0; overlap--) {
      fast_display::s_async = overlap != 0;
      CHECK_NONE(t.flush(100, 200, 299, 259, overlap != 0));
      for (int upright : {0, 180}) {
        t.turn(upright);
        t.draw(100, 200, 299, 259);
        CHECK_NONE(t.stock_problem());
        t.turn(360 - start);
        CHECK_NONE(t.flush(100, 200, 299, 259, overlap != 0));
        t.turn(start);
        CHECK_NONE(t.flush(100, 200, 299, 259, overlap != 0));
      }
    }
  }
}

// set_enabled() switches between the two flushes while the tablet runs.
TEST(set_enabled_switches) {
  for (int rotation : {90, 270}) {
    Tablet t(rotation);
    fast_display::set_enabled(true);  // before setup(): there is nothing to switch to
    if (!CHECK(t.disp.flush_cb == esphome_flush)) continue;
    if (!CHECK(fast_display::setup(&t.dsi))) continue;
    fast_display::set_enabled(false);
    if (!CHECK(t.disp.flush_cb == esphome_flush)) continue;
    t.draw(100, 200, 299, 259);
    CHECK_NONE(t.stock_problem());
    fast_display::set_enabled(true);
    if (!CHECK(t.disp.flush_cb == fast_display::flush_cb)) continue;
    CHECK_NONE(t.flush(100, 200, 299, 259, true));
    g_default = nullptr;  // no display: nothing happens
    fast_display::set_enabled(false);
    CHECK(t.disp.flush_cb == fast_display::flush_cb);
  }
}

// Single pixels, 2x2 blocks, and the first and last rows and columns.
TEST(flush_cb_edges) {
  struct A {
    int x1, y1, x2, y2, rnd;
  };
  const A areas[] = {
      // one pixel (rounding 1): the corners, the middle
      {0, 0, 0, 0, 1},
      {LW - 1, 0, LW - 1, 0, 1},
      {0, LH - 1, 0, LH - 1, 1},
      {LW - 1, LH - 1, LW - 1, LH - 1, 1},
      {640, 400, 640, 400, 1},
      // the smallest area with the tablet's rounding
      {0, 0, 1, 1, 2},
      {LW - 2, 0, LW - 1, 1, 2},
      {0, LH - 2, 1, LH - 1, 2},
      {LW - 2, LH - 2, LW - 1, LH - 1, 2},
      // first and last row and column, one and two pixels thick
      {0, 0, LW - 1, 0, 1},
      {0, LH - 1, LW - 1, LH - 1, 1},
      {0, 0, 0, LH - 1, 1},
      {LW - 1, 0, LW - 1, LH - 1, 1},
      {0, 0, LW - 1, 1, 2},
      {0, LH - 2, LW - 1, LH - 1, 2},
      {0, 0, 1, LH - 1, 2},
      {LW - 2, 0, LW - 1, LH - 1, 2},
      // odd sizes at odd places
      {1, 1, LW - 2, LH - 2, 1},
      {3, 5, 1001, 97, 1},
      {777, 333, 779, 799, 1},
  };
  for (int rotation : {90, 270}) {
    Tablet t(rotation);
    if (!CHECK(fast_display::setup(&t.dsi))) continue;
    for (const A &a : areas) {
      for (int overlap = 1; overlap >= 0; overlap--) {
        fast_display::s_async = overlap != 0;
        t.comp.draw_rounding = (size_t) a.rnd;
        CHECK_NONE(t.flush(a.x1, a.y1, a.x2, a.y2, overlap != 0));
      }
    }
  }
}

// Widths around what one part of the tile holds: one panel row less, exactly
// one part, one row more, two parts, three and a bit. Smaller tiles too, so an
// area takes many bands and both halves many times.
TEST(flush_cb_tile_halves) {
  const size_t tiles[] = {TILE, 4096, 1024};
  const int heights[] = {2, 6, 50, 100, 126, 400, 800};
  int n = 0;
  for (int rotation : {90, 270}) {
    Tablet t(rotation);
    if (!CHECK(fast_display::setup(&t.dsi))) continue;
    t.comp.draw_rounding = 1;
    for (size_t tile : tiles) {
      t.use_tile(tile);
      for (int h : heights) {
        for (int overlap = 1; overlap >= 0; overlap--) {
          fast_display::s_async = overlap != 0;
          const size_t part = overlap ? tile / 2 : tile;
          if ((size_t) h * 2 > part) continue;  // one panel row must fit
          const int fit = (int) (part / ((size_t) h * 2));
          for (int w : {fit - 1, fit, fit + 1, 2 * fit - 1, 2 * fit, 2 * fit + 1, 3 * fit + 2}) {
            if (w < 1 || w > LW) continue;
            // At the left edge, at the right edge, and somewhere odd.
            for (int x1 : {0, LW - w, (LW - w) / 3 | 1}) {
              if (x1 < 0 || x1 + w > LW) continue;
              const int y1 = h == LH ? 0 : (LH - h) / 2;
              CHECK_NONE(t.flush(x1, y1, x1 + w - 1, y1 + h - 1, overlap != 0));
              n++;
            }
          }
        }
      }
    }
  }
  printf("  %d areas\n", n);
}

// rounder_cb of ESPHome 2026.10.2 (components/lvgl/lvgl_esphome.cpp): how
// ESPHome grows every area before LVGL draws it.
static void esphome_rounder(int draw_rounding, int &x1, int &y1, int &x2, int &y2) {
  x1 = x1 / draw_rounding * draw_rounding;
  y1 = y1 / draw_rounding * draw_rounding;
  x2 = (x2 + draw_rounding) / draw_rounding * draw_rounding - 1;
  y2 = (y2 + draw_rounding) / draw_rounding * draw_rounding - 1;
}

// Areas at random, grown the way ESPHome grows them, with rounding 1, 2 (the
// tablet's) and 4. The way up changes now and then while the tablet runs.
TEST(flush_cb_random_areas) {
  const int cases = 4000;
  Rng rng{0xa11ea5u};
  Tablet t(90);
  if (!CHECK(fast_display::setup(&t.dsi))) return;
  for (int i = 0; i < cases; i++) {
    const int rnd = (i % 5 == 0) ? 1 : (i % 9 == 4) ? 4 : 2;
    int x1 = rng.upto(LW), x2 = rng.upto(LW), y1 = rng.upto(LH), y2 = rng.upto(LH);
    if (x1 > x2) std::swap(x1, x2);
    if (y1 > y2) std::swap(y1, y2);
    // Mostly small areas, as on a dashboard; some large.
    if (i % 4 != 0) {
      x2 = std::min(x2, x1 + rng.upto(200));
      y2 = std::min(y2, y1 + rng.upto(120));
    }
    esphome_rounder(rnd, x1, y1, x2, y2);
    if (i % 16 == 0) t.turn(i % 32 == 0 ? 90 : 270);
    t.comp.draw_rounding = (size_t) rnd;
    t.use_tile((i % 5 == 1) ? 4096 : TILE);  // a panel row of 800 pixels fits half of either
    const bool overlap = (i & 2) != 0;
    fast_display::s_async = overlap;
    CHECK_NONE(t.flush(x1, y1, x2, y2, overlap));
  }
  printf("  %d areas\n", cases);
}

// ESPHome grows every area to its rounding, so LVGL never flushes a height
// that is no multiple of it. If one came, ESPHome's own path would send a
// rectangle as wide as the next multiple, and so does the fast path: the
// component's draw_rounding reaches the rotation.
TEST(flush_cb_rounding_is_esphomes) {
  struct A {
    int rnd, x1, y1, x2, y2;
  };
  const A areas[] = {
      {4, 100, 200, 163, 205},      // 6 rows, sent 8 wide
      {8, 0, 300, LW - 1, 302},     // 3 rows, sent 8 wide
      {2, 640, 400, 640, 400},      // one pixel, sent 2 wide
      {16, 1000, 96, 1279, 196},    // 101 rows, sent 112 wide
      {4, 20, 500, 419, 598},       // 99 rows, sent 100 wide
  };
  for (int rotation : {90, 270}) {
    Tablet t(rotation);
    if (!CHECK(fast_display::setup(&t.dsi))) continue;
    for (const A &a : areas) {
      for (int overlap = 1; overlap >= 0; overlap--) {
        fast_display::s_async = overlap != 0;
        t.comp.draw_rounding = (size_t) a.rnd;
        t.draw(a.x1, a.y1, a.x2, a.y2);
        const int h = a.y2 - a.y1 + 1;
        const int sent = t.calls.empty() ? 0 : t.calls[0].width;
        CHECK(sent == (h + a.rnd - 1) / a.rnd * a.rnd && sent > h);
        CHECK_NONE(t.fast_problem(overlap != 0));
      }
    }
  }
}

// ---------------------------------------------------------------- main

int main(int argc, char **argv) {
  std::vector<std::string> only;
  for (int i = 1; i < argc; i++) {
    // --quick is the other test programs' flag; this one always runs in full.
    if (strcmp(argv[i], "--quick") != 0) only.push_back(argv[i]);
  }
  int ran = 0;
  for (const TestCase &t : registry()) {
    if (!only.empty() && std::find(only.begin(), only.end(), t.name) == only.end()) continue;
    g_test = t.name;
    const int before = g_failed;
    printf("- %s\n", t.name);
    fflush(stdout);
    t.fn();
    ran++;
    printf("  %s\n", g_failed == before ? "ok" : "FAILED");
  }
  free(fast_display::s_tile);
  fast_display::s_tile = nullptr;
  printf("\n%d tests, %d checks, %d failed\n", ran, g_checks, g_failed);
  for (size_t i = 0; i < g_failures.size() && i < 40; i++) printf("  %s\n", g_failures[i].c_str());
  if (g_failed > 40) printf("  ... and %d more\n", g_failed - 40);
  return g_failed == 0 ? 0 : 1;
}
