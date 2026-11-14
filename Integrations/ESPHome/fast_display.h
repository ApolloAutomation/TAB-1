#pragma once
// ============================================================================
//  fast_display.h — faster landscape rendering for the TAB-1
//
//  The panel is a portrait 800x1280 DSI display shown as a 1280x800 landscape
//  UI via `lvgl: rotation: 270` or, the other way up, `rotation: 90`. Stock
//  ESPHome rotates every LVGL band with the P4's PPA engine, which tops out
//  near 90 MB/s on this chip, about 33 ms per full-screen frame. This swaps
//  LVGL's flush callback for a CPU rotation into a 32 KB internal-SRAM tile
//  and DMA2D-copies each tile into the framebuffer through the panel's own
//  esp_lcd driver. The tile is split in two halves so the copy of one half
//  overlaps the rotation of the next. No ESPHome or IDF patches. If anything
//  doesn't match (rotation 0 or 180, no memory), setup() leaves the stock
//  path alone.
//
//  Also lowers LVGL's swipe-gesture threshold (default 50 px) so page swipes
//  trigger sooner, and polls touch every 5 ms instead of 16 ms. Neither
//  depends on the rotation: ESPHome turns each touch point the same way as
//  the picture before LVGL sees it.
//
//  Usage, in on_boot after LVGL is up:   fast_display::setup(id(dsi_display));
//
//  The rotation itself (the two kernels and rotate_area) is plain C++, so
//  engine_tests/tests/test_fast_display.cpp builds it on a computer with
//  FAST_DISPLAY_HOST_TEST defined and compares it with ESPHome's own loops.
// ============================================================================
#include <cstdint>
#include <cstddef>
#ifndef FAST_DISPLAY_HOST_TEST
#include "esphome/core/log.h"
#include "esphome/components/display/display.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/mipi_dsi/mipi_dsi.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"
#include "src/display/lv_display_private.h"  // read the stock flush_cb so it can be restored
#endif  // FAST_DISPLAY_HOST_TEST

namespace fast_display {

// Rotate rows [r0,r1) x columns [c0,c1) of `src` (row-major RGB565, row stride
// w px) 270 deg clockwise into `dst` (row stride dst_stride px):
//     dst[(c1 - 1 - c) * dst_stride + r] = src[r * w + c]
// Only those dst pixels are written. 32-bit loads only where provably aligned;
// 8-row x 32-column blocks keep each dst row's writes contiguous per visit.
static inline void rot270_rgb565(const uint16_t *src, int w, int r0, int r1, int c0, int c1,
                                 uint16_t *dst, int dst_stride)
{
    if (r0 >= r1 || c0 >= c1) return;
    const uint16_t *__restrict s = src;
    uint16_t *__restrict d = dst;
    typedef uint32_t __attribute__((__may_alias__)) u32a_t;
    const int spar = (int)((reinterpret_cast<std::uintptr_t>(s) >> 1) & 1u);
    const int dpar = (int)((reinterpret_cast<std::uintptr_t>(d) >> 1) & 1u);
    const bool src32 = (w & 1) == 0;
    const bool dst32 = (dst_stride & 1) == 0;
    const auto do_row = [&](int r, int ca, int cb) {
        const uint16_t *sp = s + (std::size_t)r * w + ca;
        uint16_t *dc = d + r;
        for (int c = ca; c < cb; ++c)
            dc[(std::size_t)(c1 - 1 - c) * dst_stride] = *sp++;
    };
    const auto do_col = [&](int c, int ra, int rb) {
        const uint16_t *sp = s + (std::size_t)ra * w + c;
        uint16_t *dp = d + (std::size_t)(c1 - 1 - c) * dst_stride + ra;
        for (int r = ra; r < rb; ++r) { *dp++ = *sp; sp += w; }
    };
    if (src32) {
        int cs = c0 + ((c0 ^ spar) & 1); if (cs > c1) cs = c1;
        int ce = cs + ((c1 - cs) & ~1);
        for (int c = c0; c < cs; ++c) do_col(c, r0, r1);
        for (int c = ce; c < c1; ++c) do_col(c, r0, r1);
        const int pairs = (ce - cs) >> 1;
        if (pairs > 0) {
            const std::size_t wu = (std::size_t)w >> 1;
            const std::size_t dstep = (std::size_t)2 * (std::size_t)dst_stride;
            const int rmain = r0 + ((r1 - r0) & ~7);
            const int drA0 = c1 - 1 - cs;
            const u32a_t *qbase = reinterpret_cast<const u32a_t *>(s + cs) + (std::size_t)r0 * wu;
            for (int p0 = 0; p0 < pairs; p0 += 16) {
                const int np = ((pairs - p0) < 16) ? (pairs - p0) : 16;
                const u32a_t *qb = qbase + p0;
                uint16_t *dAb = d + (std::size_t)(drA0 - 2 * p0) * (std::size_t)dst_stride + r0;
                for (int rb = r0; rb < rmain; rb += 8) {
                    const u32a_t *q0 = qb;      const u32a_t *q1 = q0 + wu;
                    const u32a_t *q2 = q1 + wu; const u32a_t *q3 = q2 + wu;
                    const u32a_t *q4 = q3 + wu; const u32a_t *q5 = q4 + wu;
                    const u32a_t *q6 = q5 + wu; const u32a_t *q7 = q6 + wu;
                    uint16_t *dA = dAb;
                    uint16_t *dB = dA - dst_stride;
                    const auto dopair = [&](const int k) {
                        const uint32_t x0 = q0[k], x1 = q1[k], x2 = q2[k], x3 = q3[k];
                        dA[0] = (uint16_t)x0; dA[1] = (uint16_t)x1;
                        dA[2] = (uint16_t)x2; dA[3] = (uint16_t)x3;
                        dB[0] = (uint16_t)(x0 >> 16); dB[1] = (uint16_t)(x1 >> 16);
                        dB[2] = (uint16_t)(x2 >> 16); dB[3] = (uint16_t)(x3 >> 16);
                        const uint32_t x4 = q4[k], x5 = q5[k], x6 = q6[k], x7 = q7[k];
                        dA[4] = (uint16_t)x4; dA[5] = (uint16_t)x5;
                        dA[6] = (uint16_t)x6; dA[7] = (uint16_t)x7;
                        dB[4] = (uint16_t)(x4 >> 16); dB[5] = (uint16_t)(x5 >> 16);
                        dB[6] = (uint16_t)(x6 >> 16); dB[7] = (uint16_t)(x7 >> 16);
                        dA -= dstep; dB -= dstep;
                    };
                    int i = 0;
                    for (; i + 3 < np; i += 4) {
                        dopair(0); dopair(1); dopair(2); dopair(3);
                        q0 += 4; q1 += 4; q2 += 4; q3 += 4;
                        q4 += 4; q5 += 4; q6 += 4; q7 += 4;
                    }
                    for (; i < np; ++i) {
                        dopair(0);
                        q0 += 1; q1 += 1; q2 += 1; q3 += 1;
                        q4 += 1; q5 += 1; q6 += 1; q7 += 1;
                    }
                    qb += 8 * wu;
                    dAb += 8;
                }
                int rr = rmain;
                for (; rr + 2 <= r1; rr += 2) {
                    const u32a_t *t0 = qb;
                    const u32a_t *t1 = qb + wu;
                    uint16_t *fA = dAb;
                    uint16_t *fB = fA - dst_stride;
                    for (int j = 0; j < np; ++j) {
                        const uint32_t a = t0[j], b = t1[j];
                        fA[0] = (uint16_t)a; fA[1] = (uint16_t)b;
                        fB[0] = (uint16_t)(a >> 16); fB[1] = (uint16_t)(b >> 16);
                        fA -= dstep; fB -= dstep;
                    }
                    qb += 2 * wu;
                    dAb += 2;
                }
                if (rr < r1) {
                    const u32a_t *t0 = qb;
                    uint16_t *fA = dAb;
                    uint16_t *fB = fA - dst_stride;
                    for (int j = 0; j < np; ++j) {
                        const uint32_t a = t0[j];
                        fA[0] = (uint16_t)a;
                        fB[0] = (uint16_t)(a >> 16);
                        fA -= dstep; fB -= dstep;
                    }
                }
            }
        }
        return;
    }
    if (dst32) {
        int rs = r0 + ((r0 ^ dpar) & 1); if (rs > r1) rs = r1;
        int re = rs + ((r1 - rs) & ~1);
        const int ds2 = dst_stride >> 1;
        for (int r = r0; r < rs; ++r) do_row(r, c0, c1);
        for (int r = re; r < r1; ++r) do_row(r, c0, c1);
        for (int r = rs; r < re; r += 2) {
            const uint16_t *sr0 = s + (std::size_t)r * w;
            const uint16_t *sr1 = sr0 + w;
            u32a_t *dcol = reinterpret_cast<u32a_t *>(d + r);
            int c = c0;
            int dr = c1 - 1 - c0;
            for (; c + 1 < c1; c += 2, dr -= 2) {
                const uint32_t x0 = sr0[c],     y0 = sr1[c];
                const uint32_t x1 = sr0[c + 1], y1 = sr1[c + 1];
                u32a_t *p = dcol + (std::size_t)dr * ds2;
                p[0]    = x0 | (y0 << 16);
                p[-ds2] = x1 | (y1 << 16);
            }
            if (c < c1) {
                const uint32_t x0 = sr0[c], y0 = sr1[c];
                dcol[(std::size_t)dr * ds2] = x0 | (y0 << 16);
            }
        }
        return;
    }
    for (int c = c0; c < c1; ++c) do_col(c, r0, r1);
}

// The same block 90 deg clockwise, the mirror image of the kernel above:
//     dst[(c - c0) * dst_stride + (r1 - 1 - r)] = src[r * w + c]
// Only those dst pixels are written. Same loads, same blocks; each 8-pixel run
// of a dst row is filled from its right end.
static inline void rot90_rgb565(const uint16_t *src, int w, int r0, int r1, int c0, int c1,
                                uint16_t *dst, int dst_stride)
{
    if (r0 >= r1 || c0 >= c1) return;
    const uint16_t *__restrict s = src;
    uint16_t *__restrict d = dst;
    typedef uint32_t __attribute__((__may_alias__)) u32a_t;
    const int spar = (int)((reinterpret_cast<std::uintptr_t>(s) >> 1) & 1u);
    const int dpar = (int)((reinterpret_cast<std::uintptr_t>(d) >> 1) & 1u);
    const bool src32 = (w & 1) == 0;
    const bool dst32 = (dst_stride & 1) == 0;
    const auto do_row = [&](int r, int ca, int cb) {
        const uint16_t *sp = s + (std::size_t)r * w + ca;
        uint16_t *dc = d + (r1 - 1 - r);
        for (int c = ca; c < cb; ++c)
            dc[(std::size_t)(c - c0) * dst_stride] = *sp++;
    };
    const auto do_col = [&](int c, int ra, int rb) {
        const uint16_t *sp = s + (std::size_t)ra * w + c;
        uint16_t *de = d + (std::size_t)(c - c0) * dst_stride + (r1 - ra);  // one past row ra's pixel
        for (int r = ra; r < rb; ++r) { *--de = *sp; sp += w; }
    };
    if (src32) {
        int cs = c0 + ((c0 ^ spar) & 1); if (cs > c1) cs = c1;
        int ce = cs + ((c1 - cs) & ~1);
        for (int c = c0; c < cs; ++c) do_col(c, r0, r1);
        for (int c = ce; c < c1; ++c) do_col(c, r0, r1);
        const int pairs = (ce - cs) >> 1;
        if (pairs > 0) {
            const std::size_t wu = (std::size_t)w >> 1;
            const std::size_t dstep = (std::size_t)2 * (std::size_t)dst_stride;
            const int rmain = r0 + ((r1 - r0) & ~7);
            const int drA0 = cs - c0;
            const u32a_t *qbase = reinterpret_cast<const u32a_t *>(s + cs) + (std::size_t)r0 * wu;
            for (int p0 = 0; p0 < pairs; p0 += 16) {
                const int np = ((pairs - p0) < 16) ? (pairs - p0) : 16;
                const u32a_t *qb = qbase + p0;
                // One past the pixel of the block's first row; rows go leftwards from here.
                uint16_t *dAe = d + (std::size_t)(drA0 + 2 * p0) * (std::size_t)dst_stride + (r1 - r0);
                for (int rb = r0; rb < rmain; rb += 8) {
                    const u32a_t *q0 = qb;      const u32a_t *q1 = q0 + wu;
                    const u32a_t *q2 = q1 + wu; const u32a_t *q3 = q2 + wu;
                    const u32a_t *q4 = q3 + wu; const u32a_t *q5 = q4 + wu;
                    const u32a_t *q6 = q5 + wu; const u32a_t *q7 = q6 + wu;
                    uint16_t *dA = dAe - 8;
                    uint16_t *dB = dA + dst_stride;
                    const auto dopair = [&](const int k) {
                        const uint32_t x0 = q0[k], x1 = q1[k], x2 = q2[k], x3 = q3[k];
                        dA[7] = (uint16_t)x0; dA[6] = (uint16_t)x1;
                        dA[5] = (uint16_t)x2; dA[4] = (uint16_t)x3;
                        dB[7] = (uint16_t)(x0 >> 16); dB[6] = (uint16_t)(x1 >> 16);
                        dB[5] = (uint16_t)(x2 >> 16); dB[4] = (uint16_t)(x3 >> 16);
                        const uint32_t x4 = q4[k], x5 = q5[k], x6 = q6[k], x7 = q7[k];
                        dA[3] = (uint16_t)x4; dA[2] = (uint16_t)x5;
                        dA[1] = (uint16_t)x6; dA[0] = (uint16_t)x7;
                        dB[3] = (uint16_t)(x4 >> 16); dB[2] = (uint16_t)(x5 >> 16);
                        dB[1] = (uint16_t)(x6 >> 16); dB[0] = (uint16_t)(x7 >> 16);
                        dA += dstep; dB += dstep;
                    };
                    int i = 0;
                    for (; i + 3 < np; i += 4) {
                        dopair(0); dopair(1); dopair(2); dopair(3);
                        q0 += 4; q1 += 4; q2 += 4; q3 += 4;
                        q4 += 4; q5 += 4; q6 += 4; q7 += 4;
                    }
                    for (; i < np; ++i) {
                        dopair(0);
                        q0 += 1; q1 += 1; q2 += 1; q3 += 1;
                        q4 += 1; q5 += 1; q6 += 1; q7 += 1;
                    }
                    qb += 8 * wu;
                    dAe -= 8;
                }
                int rr = rmain;
                for (; rr + 2 <= r1; rr += 2) {
                    const u32a_t *t0 = qb;
                    const u32a_t *t1 = qb + wu;
                    uint16_t *fA = dAe - 2;
                    uint16_t *fB = fA + dst_stride;
                    for (int j = 0; j < np; ++j) {
                        const uint32_t a = t0[j], b = t1[j];
                        fA[1] = (uint16_t)a; fA[0] = (uint16_t)b;
                        fB[1] = (uint16_t)(a >> 16); fB[0] = (uint16_t)(b >> 16);
                        fA += dstep; fB += dstep;
                    }
                    qb += 2 * wu;
                    dAe -= 2;
                }
                if (rr < r1) {
                    const u32a_t *t0 = qb;
                    uint16_t *fA = dAe - 1;
                    uint16_t *fB = fA + dst_stride;
                    for (int j = 0; j < np; ++j) {
                        const uint32_t a = t0[j];
                        fA[0] = (uint16_t)a;
                        fB[0] = (uint16_t)(a >> 16);
                        fA += dstep; fB += dstep;
                    }
                }
            }
        }
        return;
    }
    if (dst32) {
        // Rows r and r + 1 are neighbours in a dst row, r + 1 on the left.
        int rs = r0 + ((r0 ^ r1 ^ dpar) & 1); if (rs > r1) rs = r1;
        int re = rs + ((r1 - rs) & ~1);
        const int ds2 = dst_stride >> 1;
        for (int r = r0; r < rs; ++r) do_row(r, c0, c1);
        for (int r = re; r < r1; ++r) do_row(r, c0, c1);
        for (int r = rs; r < re; r += 2) {
            const uint16_t *sr0 = s + (std::size_t)r * w;
            const uint16_t *sr1 = sr0 + w;
            u32a_t *dcol = reinterpret_cast<u32a_t *>(d + (r1 - 2 - r));
            int c = c0;
            int dr = 0;
            for (; c + 1 < c1; c += 2, dr += 2) {
                const uint32_t x0 = sr0[c],     y0 = sr1[c];
                const uint32_t x1 = sr0[c + 1], y1 = sr1[c + 1];
                u32a_t *p = dcol + (std::size_t)dr * ds2;
                p[0]   = y0 | (x0 << 16);
                p[ds2] = y1 | (x1 << 16);
            }
            if (c < c1) {
                const uint32_t x0 = sr0[c], y0 = sr1[c];
                dcol[(std::size_t)dr * ds2] = y0 | (x0 << 16);
            }
        }
        return;
    }
    for (int c = c0; c < c1; ++c) do_col(c, r0, r1);
}

// Internal SRAM, split in two halves when async. Measured full redraw (1280x800):
// 64 KB 32.9 ms, 48 KB 33.8 ms, 32 KB 35.0 ms. 32 KB keeps ~90 KB of internal RAM
// free (this build's plain malloc() is internal-only); raise it once the RAM
// headroom with Wi-Fi + Home Assistant connected is known.
static constexpr std::size_t TILE_BYTES = 32 * 1024;

// One LVGL area without the panel: rotated tile by tile, each tile handed to
// put(x, y, width, rows, pixels) for the panel, top to bottom. The same pixels
// at the same places as ESPHome's software path (LvglComponent::draw_buffer_)
// for rotation 90 (cw90) and for 270.
//   src        the area's pixels, w x h, row-major RGB565
//   ax1, ay1   the area's top left corner in LVGL coordinates
//   rnd        ESPHome's draw_rounding
//   pw, ph     the panel's own width and height (800, 1280)
//   bufs       the two tile halves, buf_bytes each (twice the same one when
//              the copies do not overlap)
template<typename Put>
static inline void rotate_area(bool cw90, const uint16_t *src, int ax1, int ay1, int w, int h, int rnd, int pw, int ph,
                               uint16_t *const bufs[2], std::size_t buf_bytes, Put &&put) {
  const int hr = (h + rnd - 1) / rnd * rnd;  // output width, as ESPHome rounds it
  const int x_out = cw90 ? pw - ay1 - h : ay1;
  const int y_top = cw90 ? ax1 : ph - ax1 - w;
  int cols = (int) (buf_bytes / ((std::size_t) hr * sizeof(uint16_t)));
  if (cols < 1)
    cols = 1;
  if (cols > w)
    cols = w;
  int k = 0;
  for (int done = 0; done < w; done += cols, k ^= 1) {
    const int n = w - done < cols ? w - done : cols;
    // The top of the panel is the area's left edge at 90 and its right edge at 270.
    if (cw90) {
      rot90_rgb565(src, w, 0, h, done, done + n, bufs[k], hr);
    } else {
      rot270_rgb565(src, w, 0, h, w - done - n, w - done, bufs[k], hr);
    }
    put(x_out, y_top + done, hr, n, bufs[k]);
  }
}

#ifndef FAST_DISPLAY_HOST_TEST
namespace detail {
// MipiDsi (declared final) keeps its esp_lcd panel handle, DMA-done semaphore
// and colour depth protected. Explicit template instantiation is exempt from access checks, so
// this is standard C++; if ESPHome renames either member it fails to compile
// instead of misbehaving.
using Dsi = esphome::mipi_dsi::MipiDsi;
template<typename Tag, typename Tag::type M> struct Grab {
  friend typename Tag::type get(Tag) { return M; }
};
struct PanelTag {
  using type = esp_lcd_panel_handle_t Dsi::*;
  friend type get(PanelTag);
};
struct LockTag {
  using type = SemaphoreHandle_t Dsi::*;
  friend type get(LockTag);
};
template struct Grab<PanelTag, &Dsi::handle_>;
struct DepthTag {
  using type = esphome::display::ColorBitness Dsi::*;
  friend type get(DepthTag);
};
template struct Grab<LockTag, &Dsi::io_lock_>;
template struct Grab<DepthTag, &Dsi::color_depth_>;
}  // namespace detail

static const char *const TAG = "fast_display";
static size_t s_tile_bytes = TILE_BYTES;  // portion of the tile used per flush (<= TILE_BYTES; for A/B tests)
static uint16_t *s_tile = nullptr;
static esphome::mipi_dsi::MipiDsi *s_out = nullptr;
static esp_lcd_panel_handle_t s_panel = nullptr;
static SemaphoreHandle_t s_done = nullptr;  // given by the panel when a DMA2D copy finishes
static lv_display_flush_cb_t s_stock = nullptr;
static bool s_async = true;

// The two landscape rotations have the fast path. 0 and 180 are ESPHome's.
static inline bool fast_rotation(esphome::display::DisplayRotation rotation) {
  return rotation == esphome::display::DISPLAY_ROTATION_90_DEGREES ||
         rotation == esphome::display::DISPLAY_ROTATION_270_DEGREES;
}

// Same pixels as ESPHome's 90- and 270-degree software path
// (LvglComponent::draw_buffer_), tiled so the rotated data never leaves
// internal SRAM before the DMA copy.
static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px) {
  auto *comp = static_cast<esphome::lvgl::LvglComponent *>(lv_display_get_user_data(d));
  const esphome::display::DisplayRotation rotation = comp->get_rotation();
  if (!fast_rotation(rotation)) {
    s_stock(d, area, px);  // rotation changed at runtime: ESPHome's own path handles it
    return;
  }
  if (!comp->is_paused()) {
    const bool async = s_async && s_panel != nullptr && s_done != nullptr;
    const size_t buf_bytes = async ? s_tile_bytes / 2 : s_tile_bytes;
    uint16_t *const bufs[2] = {s_tile, s_tile + (async ? buf_bytes / sizeof(uint16_t) : 0)};
    bool pending = false;
    rotate_area(rotation == esphome::display::DISPLAY_ROTATION_90_DEGREES, reinterpret_cast<const uint16_t *>(px),
                area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area), (int) comp->draw_rounding,
                lv_display_get_vertical_resolution(d), lv_display_get_horizontal_resolution(d), bufs, buf_bytes,
                [&](int x, int y, int width, int rows, uint16_t *tile) {
                  if (async) {
                    if (pending)
                      xSemaphoreTake(s_done, portMAX_DELAY);  // previous half's copy is done
                    pending = esp_lcd_panel_draw_bitmap(s_panel, x, y, x + width, y + rows, tile) == ESP_OK;
                  } else {
                    s_out->draw_pixels_at(x, y, width, rows, reinterpret_cast<const uint8_t *>(tile),
                                          esphome::display::COLOR_ORDER_RGB, esphome::lvgl::LV_BITNESS, false, 0, 0, 0);
                  }
                });
    if (pending)
      xSemaphoreTake(s_done, portMAX_DELAY);  // LVGL may reuse its buffer once we return
  }
  lv_display_flush_ready(d);
}

// Switch between this path and the stock PPA path at runtime (for A/B checks).
static void set_enabled(bool on) {
  lv_display_t *d = lv_display_get_default();
  if (d == nullptr || s_stock == nullptr || s_tile == nullptr)
    return;
  lv_display_set_flush_cb(d, on ? flush_cb : s_stock);
}

static bool setup(esphome::mipi_dsi::MipiDsi *out, uint8_t gesture_px = 30, uint32_t touch_read_ms = 5) {
  for (lv_indev_t *i = lv_indev_get_next(nullptr); i != nullptr; i = lv_indev_get_next(i)) {
    if (lv_indev_get_type(i) != LV_INDEV_TYPE_POINTER)
      continue;
    lv_indev_set_gesture_min_distance(i, gesture_px);
    // LVGL polls touch on its own timer (default 16 ms) after ESPHome has read
    // the controller; the read is just a cached copy, so poll it more often.
    if (lv_timer_t *t = lv_indev_get_read_timer(i))
      lv_timer_set_period(t, touch_read_ms);
  }
  lv_display_t *d = lv_display_get_default();
  auto *comp = d ? static_cast<esphome::lvgl::LvglComponent *>(lv_display_get_user_data(d)) : nullptr;
  if (comp == nullptr || out == nullptr || !fast_rotation(comp->get_rotation()) ||
      lv_display_get_horizontal_resolution(d) != out->get_native_height() ||
      lv_display_get_vertical_resolution(d) != out->get_native_width()) {
    ESP_LOGW(TAG, "not a 90- or 270-degree software-rotated LVGL display; keeping the stock flush");
    return false;
  }
  // The tile holds LVGL's RGB565 pixels and goes to the panel unconverted.
  if (out->*get(detail::DepthTag{}) != esphome::lvgl::LV_BITNESS) {
    ESP_LOGW(TAG, "panel colour depth differs from LVGL's; keeping the stock flush");
    return false;
  }
  if (s_tile == nullptr)
    s_tile = static_cast<uint16_t *>(heap_caps_aligned_alloc(64, TILE_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
  if (s_tile == nullptr) {
    ESP_LOGW(TAG, "no internal RAM for the %u-byte tile; keeping the stock flush", (unsigned) TILE_BYTES);
    return false;
  }
  s_out = out;
  s_panel = out->*get(detail::PanelTag{});
  s_done = out->*get(detail::LockTag{});
  if (s_stock == nullptr)
    s_stock = d->flush_cb;
  lv_display_set_flush_cb(d, flush_cb);
  ESP_LOGI(TAG, "CPU-rotate flush active (%d degrees, %s DMA, gesture %u px, touch read %u ms)",
           (int) comp->get_rotation(), s_panel && s_done ? "overlapped" : "blocking", gesture_px,
           (unsigned) touch_read_ms);
  return true;
}
#endif  // FAST_DISPLAY_HOST_TEST

}  // namespace fast_display
