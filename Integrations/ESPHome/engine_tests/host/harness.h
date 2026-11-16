#pragma once
// Host harness for the engine: a 1280x800 RGB565 LVGL display that renders
// into a memory framebuffer through the same partial 1/8-screen buffer as the
// tablet, a simulated clock, a scripted touch pointer and PNG screenshots.
// Ported from the showcase harness.
//
//   hh::init();                       display and pointer
//   hh::advance(ms);                  run LVGL for ms simulated milliseconds
//   hh::tap(x, y);                    press, hold 60 ms, release
//   hh::drag(x0, y0, x1, y1, ms);     press, move in 10 ms steps, release
//   hh::press / move / release        the same in separate steps
//   hh::screenshot("out.png");        refresh now and write the framebuffer
//   hh::load_file(path, &size)        read a whole file
#include "lvgl.h"
#include <zlib.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace hh {

static const int W = 1280, H = 800;
static uint32_t g_ms = 0;          // simulated milliseconds since start
static uint16_t *g_fb = nullptr;   // flushed pixels, W*H RGB565
static lv_display_t *g_disp = nullptr;
static lv_indev_t *g_indev = nullptr;
static bool g_down = false;
static int g_x = 0, g_y = 0;
static void (*g_each_ms)() = nullptr;  // called once per simulated millisecond

static uint32_t tick_cb() { return g_ms; }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  const int w = lv_area_get_width(a), h = lv_area_get_height(a);
  const uint16_t *src = reinterpret_cast<const uint16_t *>(px);
  for (int y = 0; y < h; y++)
    memcpy(g_fb + (size_t) (a->y1 + y) * W + a->x1, src + (size_t) y * w, (size_t) w * 2);
  lv_display_flush_ready(d);
}

static void read_cb(lv_indev_t *, lv_indev_data_t *data) {
  data->point.x = g_x;
  data->point.y = g_y;
  data->state = g_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void init() {
  lv_init();
  lv_tick_set_cb(tick_cb);
  g_fb = static_cast<uint16_t *>(calloc((size_t) W * H, 2));
  g_disp = lv_display_create(W, H);
  lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
  const size_t bsz = (size_t) W * H / 8 * 2;
  lv_display_set_buffers(g_disp, aligned_alloc(64, bsz), nullptr, bsz, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(g_disp, flush_cb);
  g_indev = lv_indev_create();
  lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(g_indev, read_cb);
  // The tablet's tuning (Engine.yaml long_press_time, fast_display.h read period).
  lv_indev_set_long_press_time(g_indev, 900);
  lv_timer_set_period(lv_indev_get_read_timer(g_indev), 5);
}

static void advance(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) {
    g_ms++;
    if (g_each_ms) g_each_ms();
    lv_timer_handler();
  }
}

static void press(int x, int y) {
  g_x = x;
  g_y = y;
  g_down = true;
  advance(10);
}
static void move(int x, int y) {
  g_x = x;
  g_y = y;
  advance(10);
}
static void release() {
  g_down = false;
  advance(30);
}
static void tap(int x, int y) {
  press(x, y);
  advance(50);
  release();
}
static void drag(int x0, int y0, int x1, int y1, uint32_t ms) {
  press(x0, y0);
  const int steps = ms / 10 > 0 ? (int) (ms / 10) : 1;
  for (int i = 1; i <= steps; i++) move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
  release();
}

static void png_chunk(FILE *f, const char *tag, const uint8_t *d, uint32_t n) {
  uint8_t len[4] = {uint8_t(n >> 24), uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
  fwrite(len, 1, 4, f);
  fwrite(tag, 1, 4, f);
  if (n) fwrite(d, 1, n, f);
  uLong c = crc32(0, reinterpret_cast<const Bytef *>(tag), 4);
  if (n) c = crc32(c, d, n);
  uint8_t crc[4] = {uint8_t(c >> 24), uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c)};
  fwrite(crc, 1, 4, f);
}

static void refresh() { lv_refr_now(g_disp); }

// Writes the framebuffer as an 8-bit RGB PNG (RGB565 expanded by bit replication).
static bool screenshot(const char *path) {
  refresh();
  std::vector<uint8_t> raw((size_t) H * (1 + W * 3));
  for (int y = 0; y < H; y++) {
    uint8_t *row = &raw[(size_t) y * (1 + W * 3)];
    *row++ = 0;
    for (int x = 0; x < W; x++) {
      const uint16_t p = g_fb[(size_t) y * W + x];
      const uint8_t r = p >> 11, g = (p >> 5) & 63, b = p & 31;
      *row++ = uint8_t(r << 3 | r >> 2);
      *row++ = uint8_t(g << 2 | g >> 4);
      *row++ = uint8_t(b << 3 | b >> 2);
    }
  }
  uLongf zn = compressBound(raw.size());
  std::vector<uint8_t> z(zn);
  if (compress2(z.data(), &zn, raw.data(), raw.size(), 6) != Z_OK) return false;
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  const uint8_t ihdr[13] = {uint8_t(W >> 24), uint8_t(W >> 16), uint8_t(W >> 8), uint8_t(W),
                            uint8_t(H >> 24), uint8_t(H >> 16), uint8_t(H >> 8), uint8_t(H),
                            8, 2, 0, 0, 0};
  png_chunk(f, "IHDR", ihdr, 13);
  png_chunk(f, "IDAT", z.data(), (uint32_t) zn);
  png_chunk(f, "IEND", nullptr, 0);
  fclose(f);
  return true;
}

static uint16_t pixel(int x, int y) { return g_fb[(size_t) y * W + x]; }

// RGB565 of a 0xRRGGBB colour, as LVGL converts it.
static uint16_t rgb565(uint32_t c) {
  return (uint16_t) (((c >> 19) & 0x1F) << 11 | ((c >> 10) & 0x3F) << 5 | ((c >> 3) & 0x1F));
}

static uint8_t *load_file(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "cannot open %s\n", path);
    exit(2);
  }
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *d = static_cast<uint8_t *>(malloc((size_t) n + 1));
  if (fread(d, 1, (size_t) n, f) != (size_t) n) {
    fprintf(stderr, "short read %s\n", path);
    exit(2);
  }
  d[n] = 0;
  fclose(f);
  if (size) *size = (size_t) n;
  return d;
}

// The tablet's fonts as ESPHome's font component makes them: no kerning,
// line_height = the font's height and base_line = height - ascender, both in
// whole pixels (generated main.cpp, Font(glyphs, count, ascender, height, ...)).
// In the order of the fonts in Engine.yaml. tools/check_engine_package.py
// --compile compares this table with a build, and its static mode checks that
// no digits font's line is higher than contract.json fonts.row_px.
struct DeviceMetric {
  const char *family;  // "text", "digits" or "icons"
  int px, line_height, base_line;
};
static const DeviceMetric DEVICE_METRICS[] = {
    {"text", 14, 17, 3},       {"text", 15, 18, 3},         {"text", 16, 20, 4},   {"text", 18, 22, 4},
    {"text", 20, 24, 4},       {"text", 28, 34, 6},         {"text", 34, 41, 8},   {"digits", 48, 59, 12},
    {"digits", 96, 117, 24},   {"digits", 192, 234, 48},    {"icons", 24, 24, 3},  {"icons", 28, 28, 3},
    {"icons", 48, 48, 6},      {"icons", 72, 72, 9},
};

static const DeviceMetric *device_metric(const char *family, int px) {
  for (const DeviceMetric &m : DEVICE_METRICS)
    if (m.px == px && strcmp(m.family, family) == 0) return &m;
  return nullptr;
}

// A font from a TTF, standing in for the ESPHome font of the same family and
// size: kerning off and the tablet's line metrics. The metrics are set before
// the first glyph is drawn (tiny_ttf keys its glyph cache on line_height) and
// before limited_font copies the font.
static const lv_font_t *ttf_font(const uint8_t *data, size_t n, int px, const char *family) {
  lv_font_t *f = lv_tiny_ttf_create_data_ex(data, n, px, LV_FONT_KERNING_NONE, LV_TINY_TTF_CACHE_GLYPH_CNT);
  const DeviceMetric *m = device_metric(family, px);
  if (!f || !m) {
    fprintf(stderr, "no %s font of %d px\n", family, px);
    exit(2);
  }
  f->line_height = m->line_height;
  f->base_line = m->base_line;
  return f;
}

// The same font holding only the code points in `ranges` (pairs: first, last),
// as the tablet's bitmap fonts do: they are made from contract.json
// fonts.text_glyphs. Any other character has no glyph here either, so LVGL
// draws its placeholder box and screenshots match the tablet.
struct LimitedFont {
  lv_font_t font;
  const lv_font_t *base;
  std::vector<uint32_t> ranges;
};
static std::vector<LimitedFont *> &limited_fonts() {
  static std::vector<LimitedFont *> all;
  return all;
}
static bool limited_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t letter, uint32_t next) {
  for (const LimitedFont *f : limited_fonts()) {
    if (&f->font != font) continue;
    bool held = false, next_held = next == 0;
    for (size_t i = 0; i + 1 < f->ranges.size(); i += 2) {
      if (letter >= f->ranges[i] && letter <= f->ranges[i + 1]) held = true;
      if (next >= f->ranges[i] && next <= f->ranges[i + 1]) next_held = true;
    }
    if (!held) return false;
    return f->base->get_glyph_dsc(f->base, dsc, letter, next_held ? next : 0);
  }
  return false;
}
static const lv_font_t *limited_font(const lv_font_t *base, const std::vector<uint32_t> &ranges) {
  if (!base) return nullptr;
  LimitedFont *f = new LimitedFont{*base, base, ranges};
  f->font.get_glyph_dsc = limited_glyph_dsc;
  limited_fonts().push_back(f);
  return &f->font;
}

}  // namespace hh
