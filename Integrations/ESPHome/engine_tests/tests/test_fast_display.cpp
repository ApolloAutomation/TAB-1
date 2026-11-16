// Host tests for the screen rotation in fast_display.h: the two kernels
// (rot90_rgb565, rot270_rgb565) and rotate_area, which is one flush without
// the panel. Run through engine_tests/run.sh.
//
//   test_fast_display [--quick] [name ...]
//
// The kernels are compared with the plain meaning of their comment, pixel by
// pixel, for whole frames and for odd blocks. rotate_area is compared with
// ESPHome's own software rotation (LvglComponent::draw_buffer_ of ESPHome
// 2026.10.2, copied below) on a made-up 800x1280 panel: the same pixels at
// the same places, in bands from the top of the panel down. The touch test
// puts ESPHome's rotate_coordinates next to it: a touch on a drawn pixel is
// that pixel, at 90 and at 270.
//
// Exit code 0 only when every check passed.
#define FAST_DISPLAY_HOST_TEST 1
#include "fast_display.h"

#include <algorithm>
#include <chrono>
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
static bool QUICK = false;

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
#define CHECK_MSG(c, msg) check((c), __LINE__, std::string(#c) + "  " + (msg))
// For the helpers that return what is wrong, or "" when nothing is.
#define CHECK_NONE(problem) check_none((problem), __LINE__)

static bool check_none(const std::string &problem, int line) { return check(problem.empty(), line, problem); }

static std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
static std::string fmt(const char *f, ...) {
  char buf[400];
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
  int upto(int n) { return (int) (next() % (uint32_t) n); }           // 0 .. n-1
  int in(int lo, int hi) { return lo + upto(hi - lo + 1); }           // lo .. hi
  void fill(std::vector<uint16_t> &v) {
    for (uint16_t &p : v) p = (uint16_t) (next() >> 8);
  }
};

static const char *name_of(bool cw90) { return cw90 ? "90" : "270"; }

// ---------------------------------------------------------------- the kernels

// What the comments of the two kernels say, one pixel at a time.
static void plain_rot(bool cw90, const uint16_t *src, int w, int r0, int r1, int c0, int c1, uint16_t *dst,
                      int dst_stride) {
  for (int r = r0; r < r1; r++) {
    for (int c = c0; c < c1; c++) {
      if (cw90) {
        dst[(size_t) (c - c0) * dst_stride + (r1 - 1 - r)] = src[(size_t) r * w + c];
      } else {
        dst[(size_t) (c1 - 1 - c) * dst_stride + r] = src[(size_t) r * w + c];
      }
    }
  }
}

static void kernel(bool cw90, const uint16_t *src, int w, int r0, int r1, int c0, int c1, uint16_t *dst,
                   int dst_stride) {
  if (cw90) {
    fast_display::rot90_rgb565(src, w, r0, r1, c0, c1, dst, dst_stride);
  } else {
    fast_display::rot270_rgb565(src, w, r0, r1, c0, c1, dst, dst_stride);
  }
}

// Rows [r0,r1) x columns [c0,c1) of a w-wide source into a dst of c1 - c0 rows.
// soff and doff shift the buffers by one pixel, so both kinds of 32-bit
// alignment are met.
struct Block {
  int w, r0, r1, c0, c1, stride, soff, doff;
};

// Both buffers have exactly the size the kernel may touch, so the sanitizer
// build sees a read or write outside them. The dst starts as noise: every
// pixel the kernel must not write has to keep it. Returns what is wrong.
static std::string block_problem(bool cw90, const Block &b, uint32_t seed) {
  Rng rng{seed | 1u};
  std::vector<uint16_t> src((size_t) b.soff + (size_t) b.w * b.r1);
  std::vector<uint16_t> got((size_t) b.doff + (size_t) (b.c1 - b.c0) * b.stride);
  rng.fill(src);
  rng.fill(got);
  std::vector<uint16_t> want = got;
  plain_rot(cw90, src.data() + b.soff, b.w, b.r0, b.r1, b.c0, b.c1, want.data() + b.doff, b.stride);
  kernel(cw90, src.data() + b.soff, b.w, b.r0, b.r1, b.c0, b.c1, got.data() + b.doff, b.stride);
  if (got == want) return "";
  size_t at = 0;
  while (got[at] == want[at]) at++;
  return fmt("rot%s w=%d rows [%d,%d) columns [%d,%d) stride %d src+%d dst+%d: dst pixel %zu is %04x, not %04x",
             name_of(cw90), b.w, b.r0, b.r1, b.c0, b.c1, b.stride, b.soff, b.doff, at, got[at], want[at]);
}

// Every block of a small source: all the row counts around the 8-row and
// 2-row steps, all the column counts around the 2-column step, both
// alignments of both buffers, even and odd widths and strides.
TEST(kernels_every_small_block) {
  const int max_w = 10, max_r = QUICK ? 11 : 19;
  int n = 0;
  for (int cw90 = 0; cw90 < 2; cw90++)
    for (int w = 1; w <= max_w; w++)
      for (int c0 = 0; c0 < w; c0++)
        for (int c1 = c0 + 1; c1 <= w; c1++)
          for (int r1 = 1; r1 <= max_r; r1++)
            for (int r0 = 0; r0 < r1; r0++)
              for (int extra = 0; extra < 2; extra++)
                for (int off = 0; off < 4; off++) {
                  const Block b{w, r0, r1, c0, c1, r1 + extra, off & 1, off >> 1};
                  n++;
                  CHECK_NONE(block_problem(cw90 != 0, b, (uint32_t) n * 2654435761u));
                }
  printf("  %d blocks\n", n);
}

// Larger blocks at random: several 32-column blocks with a rest, many rows.
TEST(kernels_random_blocks) {
  const int cases = QUICK ? 3000 : 40000;
  Rng rng{0x7ab1d15u};
  for (int i = 0; i < cases; i++) {
    Block b;
    b.w = rng.in(1, 150);
    b.r1 = rng.in(1, 60);
    b.r0 = rng.upto(b.r1);
    b.c0 = rng.upto(b.w);
    b.c1 = rng.in(b.c0 + 1, b.w);
    b.stride = b.r1 + rng.upto(4);
    b.soff = rng.upto(2);
    b.doff = rng.upto(2);
    CHECK_NONE(block_problem((i & 1) != 0, b, rng.next()));
  }
  printf("  %d blocks\n", cases);
}

static const int LW = 1280, LH = 800;  // what LVGL draws
static const int PW = 800, PH = 1280;  // the panel

// The whole 1280x800 picture in one call, into a dst as large as the panel.
TEST(kernels_whole_frame) {
  for (int cw90 = 0; cw90 < 2; cw90++) {
    for (uint32_t seed : {0x1234567u, 0xbeef01u}) {
      const Block b{LW, 0, LH, 0, LW, LH, 0, 0};
      CHECK_NONE(block_problem(cw90 != 0, b, seed));
    }
  }
}

// Odd parts of the whole picture: the first and last row and column, single
// pixels in the corners and the middle, and blocks one pixel short on a side.
TEST(kernels_frame_edges) {
  struct Part {
    const char *what;
    int r0, r1, c0, c1;
  };
  const Part parts[] = {
      {"first row", 0, 1, 0, LW},
      {"last row", LH - 1, LH, 0, LW},
      {"first column", 0, LH, 0, 1},
      {"last column", 0, LH, LW - 1, LW},
      {"top left pixel", 0, 1, 0, 1},
      {"top right pixel", 0, 1, LW - 1, LW},
      {"bottom left pixel", LH - 1, LH, 0, 1},
      {"bottom right pixel", LH - 1, LH, LW - 1, LW},
      {"middle pixel", LH / 2, LH / 2 + 1, LW / 2, LW / 2 + 1},
      {"all but the first row and column", 1, LH, 1, LW},
      {"all but the last row and column", 0, LH - 1, 0, LW - 1},
      {"an odd block inside", 3, LH - 4, 5, LW - 2},
      {"two rows, three columns", 398, 400, 639, 642},
      {"nine rows, 33 columns at odd places", 101, 110, 77, 110},
  };
  uint32_t seed = 99;
  for (int cw90 = 0; cw90 < 2; cw90++) {
    for (const Part &p : parts) {
      for (int off = 0; off < 4; off++) {
        const Block b{LW, p.r0, p.r1, p.c0, p.c1, p.r1 + (off == 3), off & 1, off >> 1};
        const std::string problem = block_problem(cw90 != 0, b, seed += 7919);
        CHECK_NONE(problem.empty() ? problem : std::string(p.what) + ": " + problem);
      }
    }
  }
}

// ---------------------------------------------------------------- one flush

// The panel's frame buffer, and how it takes pixels (draw_pixels_at on the
// tablet, or the DMA copy of esp_lcd_panel_draw_bitmap).
struct Panel {
  std::vector<uint16_t> px = std::vector<uint16_t>((size_t) PW * PH);
  std::string error;
  void draw(int x, int y, int width, int rows, const uint16_t *p) {
    if (x < 0 || y < 0 || width <= 0 || rows <= 0 || x + width > PW || y + rows > PH) {
      if (error.empty()) error = fmt("draw outside the panel: x %d, y %d, %d x %d", x, y, width, rows);
      return;
    }
    for (int r = 0; r < rows; r++)
      memcpy(&px[(size_t) (y + r) * PW + x], p + (size_t) r * width, (size_t) width * sizeof(uint16_t));
  }
};

struct Rect {
  int x, y, width, rows;
};

// LvglComponent::draw_buffer_ of ESPHome 2026.10.2 (components/lvgl/
// lvgl_esphome.cpp) for software rotation without the PPA, its 90 and 270
// cases word for word. `width_` and `height_` are the panel's own size as its
// setup() reads them, `rotate_buf` is its rotate_buf_, and the last line is
// its draw_pixels_at. Returns where it drew.
static Rect esphome_draw_buffer(int rotation_, int width_, int height_, int draw_rounding, int area_x1, int area_y1,
                                int area_x2, int area_y2, const uint16_t *ptr, Panel &display) {
  auto width = area_x2 - area_x1 + 1;
  auto height = area_y2 - area_y1 + 1;
  auto height_rounded = (height + draw_rounding - 1) / draw_rounding * draw_rounding;
  auto x1 = area_x1;
  auto y1 = area_y1;
  std::vector<uint16_t> rotate_buf((size_t) width * height_rounded);
  uint16_t *dst = rotate_buf.data();
  switch (rotation_) {
    case 90:
      for (int32_t x = height; x-- != 0;) {
        for (int32_t y = 0; y != width; y++) {
          dst[y * height_rounded + x] = *ptr++;
        }
      }
      break;

    case 270:
      for (int32_t x = 0; x != height; x++) {
        for (int32_t y = width; y-- != 0;) {
          dst[y * height_rounded + x] = *ptr++;
        }
      }
      break;

    default:
      break;
  }
  switch (rotation_) {
    case 90:
      y1 = x1;
      x1 = width_ - area_y1 - height;
      height = width;
      width = height_rounded;
      break;

    case 270:
      x1 = y1;
      y1 = height_ - area_x1 - width;
      height = width;
      width = height_rounded;
      break;

    default:
      break;
  }
  display.draw(x1, y1, width, height, dst);
  return {x1, y1, width, height};
}

// rounder_cb of the same file: how ESPHome grows every area LVGL redraws.
static void esphome_rounder(int draw_rounding, int &x1, int &y1, int &x2, int &y2) {
  x1 = x1 / draw_rounding * draw_rounding;
  y1 = y1 / draw_rounding * draw_rounding;
  x2 = (x2 + draw_rounding) / draw_rounding * draw_rounding - 1;
  y2 = (y2 + draw_rounding) / draw_rounding * draw_rounding - 1;
}

struct Band {
  Rect at;
  int half;  // which tile half held it
};

// One flush as flush_cb runs it. With `overlap` the tile has two halves and
// the panel reads a half as late as the tablet's DMA copy may: when the next
// half is rotated and waits for it, or at the end. A rotation that wrote into
// the half still being copied would show as wrong pixels. Each half is its own
// allocation of exactly its size, so the sanitizer build sees an overrun.
static std::vector<Band> fast_flush(bool cw90, int x1, int y1, int x2, int y2, int rnd, const uint16_t *src,
                                    size_t tile_bytes, bool overlap, Panel &panel) {
  const size_t buf_bytes = overlap ? tile_bytes / 2 : tile_bytes;
  std::vector<uint16_t> half0(buf_bytes / sizeof(uint16_t)), half1(overlap ? buf_bytes / sizeof(uint16_t) : 0);
  uint16_t *const bufs[2] = {half0.data(), overlap ? half1.data() : half0.data()};
  std::vector<Band> bands;
  bool pending = false;
  Rect copy{};
  const uint16_t *copy_from = nullptr;
  fast_display::rotate_area(cw90, src, x1, y1, x2 - x1 + 1, y2 - y1 + 1, rnd, PW, PH, bufs, buf_bytes,
                            [&](int x, int y, int width, int rows, uint16_t *tile) {
                              if (!overlap) {
                                panel.draw(x, y, width, rows, tile);
                              } else {
                                if (pending) panel.draw(copy.x, copy.y, copy.width, copy.rows, copy_from);
                                copy = {x, y, width, rows};
                                copy_from = tile;
                                pending = true;
                              }
                              bands.push_back({{x, y, width, rows}, tile == bufs[0] ? 0 : 1});
                            });
  if (pending) panel.draw(copy.x, copy.y, copy.width, copy.rows, copy_from);
  return bands;
}

// Two panels that start with the same noise and must stay the same: ESPHome's
// path draws on one, the fast path on the other.
struct Pair {
  Panel stock, fast;
  Rng rng{0x51de5u};
  Pair() {
    rng.fill(stock.px);
    fast.px = stock.px;
  }

  // One area (x1,y1)-(x2,y2) of the LVGL screen with random pixels. Returns
  // what is wrong.
  std::string flush(bool cw90, int x1, int y1, int x2, int y2, int rnd, size_t tile_bytes, bool overlap) {
    const int w = x2 - x1 + 1, h = y2 - y1 + 1;
    std::vector<uint16_t> src((size_t) w * h);
    rng.fill(src);
    const Rect want = esphome_draw_buffer(cw90 ? 90 : 270, PW, PH, rnd, x1, y1, x2, y2, src.data(), stock);
    const std::vector<Band> bands = fast_flush(cw90, x1, y1, x2, y2, rnd, src.data(), tile_bytes, overlap, fast);
    std::string problem;
    if (!stock.error.empty()) problem = "ESPHome's path: " + stock.error;
    if (problem.empty() && !fast.error.empty()) problem = "fast path: " + fast.error;
    // The bands are ESPHome's rectangle cut into strips, from its top down,
    // with no gap and no overlap, each in the other tile half than the last.
    int y = want.y;
    for (size_t i = 0; problem.empty() && i < bands.size(); i++) {
      const Rect &b = bands[i].at;
      if (b.x != want.x || b.width != want.width || b.y != y || b.rows < 1)
        problem = fmt("band %zu is at x %d, y %d, %d x %d; expected x %d, y %d, width %d", i, b.x, b.y, b.width,
                      b.rows, want.x, y, want.width);
      else if ((size_t) b.width * b.rows * sizeof(uint16_t) > (overlap ? tile_bytes / 2 : tile_bytes))
        problem = fmt("band %zu (%d x %d) is larger than its tile", i, b.width, b.rows);
      else if (bands[i].half != (overlap ? (int) (i & 1) : 0))
        problem = fmt("band %zu is in tile half %d", i, bands[i].half);
      y += b.rows;
    }
    if (problem.empty() && y != want.y + want.rows)
      problem = fmt("the bands end at y %d, ESPHome's rectangle at y %d", y, want.y + want.rows);
    if (problem.empty() && fast.px != stock.px) {
      size_t at = 0;
      while (fast.px[at] == stock.px[at]) at++;
      problem = fmt("panel pixel x %zu, y %zu is %04x, ESPHome draws %04x", at % PW, at / PW, fast.px[at],
                    stock.px[at]);
    }
    if (problem.empty()) return "";
    stock.error.clear();
    fast.error.clear();
    fast.px = stock.px;  // one failure is reported once
    return fmt("rotation %s, area (%d,%d)-(%d,%d), rounding %d, tile %zu bytes%s: %s", name_of(cw90), x1, y1, x2, y2,
               rnd, tile_bytes, overlap ? " in two halves" : "", problem.c_str());
  }
};

static const size_t TILE = fast_display::TILE_BYTES;

// The whole screen in one area, and in the eight 1280x100 strips LVGL flushes
// with the tablet's 12% buffer. Tile size and rounding as on the tablet.
TEST(flush_whole_screen) {
  Pair p;
  for (int cw90 = 0; cw90 < 2; cw90++) {
    for (int overlap = 0; overlap < 2; overlap++) {
      CHECK_NONE(p.flush(cw90 != 0, 0, 0, LW - 1, LH - 1, 2, TILE, overlap != 0));
      for (int y = 0; y < LH; y += 100)
        CHECK_NONE(p.flush(cw90 != 0, 0, y, LW - 1, y + 99, 2, TILE, overlap != 0));
    }
  }
}

// Single pixels, 2x2 blocks, and the first and last rows and columns.
TEST(flush_edges) {
  Pair p;
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
  for (int cw90 = 0; cw90 < 2; cw90++)
    for (const A &a : areas)
      for (int overlap = 0; overlap < 2; overlap++)
        CHECK_NONE(p.flush(cw90 != 0, a.x1, a.y1, a.x2, a.y2, a.rnd, TILE, overlap != 0));
}

// Widths around what one tile half holds: one column less, exactly one half,
// one column more, two halves, three and a bit. Small tiles too, so an area
// takes many bands.
TEST(flush_tile_boundaries) {
  Pair p;
  const size_t tiles[] = {TILE, 4096, 1024};
  const int heights[] = {2, 6, 50, 100, 126, 400, 800};
  for (int cw90 = 0; cw90 < 2; cw90++) {
    for (size_t tile : tiles) {
      for (int h : heights) {
        for (int overlap = 0; overlap < 2; overlap++) {
          const size_t buf = overlap ? tile / 2 : tile;
          if ((size_t) h * 2 > buf) continue;  // one column must fit
          const int cols = (int) (buf / ((size_t) h * 2));
          for (int w : {cols - 1, cols, cols + 1, 2 * cols - 1, 2 * cols, 2 * cols + 1, 3 * cols + 2}) {
            if (w < 1 || w > LW) continue;
            // At the left edge, at the right edge, and somewhere odd.
            for (int x1 : {0, LW - w, (LW - w) / 3 | 1}) {
              if (x1 < 0 || x1 + w > LW) continue;
              const int y1 = h == LH ? 0 : (LH - h) / 2;
              CHECK_NONE(p.flush(cw90 != 0, x1, y1, x1 + w - 1, y1 + h - 1, 1, tile, overlap != 0));
            }
          }
        }
      }
    }
  }
}

// Areas at random: any rectangle with rounding 1, and with the tablet's
// rounding 2 the way ESPHome grows it before LVGL draws.
TEST(flush_random_areas) {
  Pair p;
  const int cases = QUICK ? 200 : 3000;
  Rng rng{0xa11ea5u};
  for (int i = 0; i < cases; i++) {
    const int rnd = 1 + (i & 1);
    int x1 = rng.upto(LW), x2 = rng.upto(LW), y1 = rng.upto(LH), y2 = rng.upto(LH);
    if (x1 > x2) std::swap(x1, x2);
    if (y1 > y2) std::swap(y1, y2);
    // Mostly small areas, as on a dashboard; some large.
    if (i % 4 != 0) {
      x2 = std::min(x2, x1 + rng.upto(200));
      y2 = std::min(y2, y1 + rng.upto(120));
    }
    esphome_rounder(rnd, x1, y1, x2, y2);
    const size_t tile = (i % 5 == 0) ? 4096 : TILE;  // a column of 800 pixels fits half of either
    CHECK_NONE(p.flush((i & 2) != 0, x1, y1, x2, y2, rnd, tile, (i & 4) != 0));
  }
  printf("  %d areas\n", cases);
}

// ---------------------------------------------------------------- touch

// LvglComponent::rotate_coordinates of ESPHome 2026.10.2, word for word:
// what it does to a touch point before LVGL reads it. `width_` and `height_`
// are the panel's own size.
static void esphome_rotate_coordinates(int rotation_, int32_t width_, int32_t height_, int32_t &x, int32_t &y) {
  switch (rotation_) {
    default:
      break;

    case 180: {
      x = width_ - x - 1;
      y = height_ - y - 1;
      break;
    }
    case 270: {
      auto tmp = x;
      x = height_ - y - 1;
      y = tmp;
      break;
    }
    case 90: {
      auto tmp = y;
      y = width_ - x - 1;
      x = tmp;
      break;
    }
  }
}

// The touch controller reports panel coordinates (the board file's axis
// settings; it knows nothing of the rotation). A finger on the panel pixel
// where the picture's pixel (x, y) is drawn must reach LVGL as (x, y), at 90
// and at 270: taps hit what they show, and a swipe to the right is one to the
// right.
TEST(touch_lands_on_the_picture) {
  Rng rng{0x70c4u};
  const uint16_t mark = 0xffff;
  for (int cw90 = 0; cw90 < 2; cw90++) {
    const int rotation = cw90 ? 90 : 270;
    Panel panel;
    for (int i = 0; i < 400; i++) {
      // The corners first, then anywhere.
      const int x = i < 4 ? (i & 1) * (LW - 1) : rng.upto(LW);
      const int y = i < 4 ? (i >> 1) * (LH - 1) : rng.upto(LH);
      const std::vector<Band> bands = fast_flush(cw90 != 0, x, y, x, y, 1, &mark, TILE, true, panel);
      if (!CHECK(bands.size() == 1 && panel.error.empty())) continue;
      int32_t tx = bands[0].at.x, ty = bands[0].at.y;  // the finger, on the panel
      uint16_t &drawn = panel.px[(size_t) ty * PW + tx];
      CHECK(drawn == mark);
      drawn = 0;  // the panel is black again for the next point
      esphome_rotate_coordinates(rotation, PW, PH, tx, ty);
      CHECK_MSG(tx == x && ty == y, fmt("rotation %d: a touch on the pixel drawn for (%d,%d) reaches LVGL as (%d,%d)",
                                        rotation, x, y, (int) tx, (int) ty));
    }
    // A finger moving right on the picture moves down the panel at 90 and up
    // it at 270; LVGL sees x grow both times.
    const int ax = 100, ay = 300, bx = 400, by = 300;  // on the picture
    Panel one;
    const Rect a = fast_flush(cw90 != 0, ax, ay, ax, ay, 1, &mark, TILE, false, one)[0].at;
    const Rect b = fast_flush(cw90 != 0, bx, by, bx, by, 1, &mark, TILE, false, one)[0].at;
    CHECK(a.x == b.x && (cw90 ? b.y > a.y : b.y < a.y));
    int32_t sx = a.x, sy = a.y, ex = b.x, ey = b.y;
    esphome_rotate_coordinates(rotation, PW, PH, sx, sy);
    esphome_rotate_coordinates(rotation, PW, PH, ex, ey);
    CHECK(ex - sx == 300 && ey == sy);
  }
  // The two ways up are half a turn apart: the same picture pixel is at
  // opposite ends of the panel.
  Panel p90, p270;
  const Rect a = fast_flush(true, 10, 20, 10, 20, 1, &mark, TILE, false, p90)[0].at;
  const Rect b = fast_flush(false, 10, 20, 10, 20, 1, &mark, TILE, false, p270)[0].at;
  CHECK(a.x == PW - 1 - b.x && a.y == PH - 1 - b.y);
}

// ---------------------------------------------------------------- main

// How long one whole picture takes on this computer, for comparing the two
// kernels with each other. Printed, not checked.
static void speed_note() {
  std::vector<uint16_t> src((size_t) LW * LH), dst((size_t) LW * LH);
  Rng rng{77};
  rng.fill(src);
  double ms[2] = {0, 0};
  for (int cw90 = 0; cw90 < 2; cw90++) {
    double best = 1e9;
    for (int run = 0; run < 30; run++) {
      const auto t0 = std::chrono::steady_clock::now();
      kernel(cw90 != 0, src.data(), LW, 0, LH, 0, LW, dst.data(), LH);
      const auto t1 = std::chrono::steady_clock::now();
      best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    ms[cw90] = best + (dst[12345] & 0);
  }
  printf("note: one 1280x800 picture on this computer: rot270 %.3f ms, rot90 %.3f ms (best of 30)\n", ms[0], ms[1]);
}

int main(int argc, char **argv) {
  std::vector<std::string> only;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--quick"))
      QUICK = true;
    else
      only.push_back(argv[i]);
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
  printf("\n%d tests, %d checks, %d failed\n", ran, g_checks, g_failed);
  for (size_t i = 0; i < g_failures.size() && i < 40; i++) printf("  %s\n", g_failures[i].c_str());
  if (g_failed > 40) printf("  ... and %d more\n", g_failed - 40);
  if (!QUICK && only.empty()) speed_note();
  return g_failed == 0 ? 0 : 1;
}
