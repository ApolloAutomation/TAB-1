// Card colours (contract 4.7): mix, lum, contrast, readable, legible and
// blend, the nine colours of a card and the colours of an image. Plain C++
// with no LVGL, so the host test runs maths.json against it directly.
//
// No colour is worked out in floating point: mix is whole numbers, and lum
// reads the table of tab_linear.h instead of calling pow (the tablet has no
// double-precision FPU). lum and blend are doubles in a fixed order, each
// step on its own; the host tests build with -ffp-contract=off so the Mac
// gives what the tablet gives. These run when a card's state changes, never
// while it is painted (cards spec B.3).
#ifndef TAB_COLOUR_H
#define TAB_COLOUR_H

#include <cmath>
#include <cstdint>
#include "tab_linear.h"

namespace tab {
namespace detail {

// Host tests count the contrasts worked out (TAB_TEST_COUNTERS); the
// tablet's build has none of this.
#ifdef TAB_TEST_COUNTERS
inline uint64_t &test_contrasts() {
  static uint64_t n = 0;
  return n;
}
#define TAB_COUNT_CONTRAST() (++::tab::detail::test_contrasts())
#else
#define TAB_COUNT_CONTRAST() ((void) 0)
#endif

static const uint32_t COLOUR_WHITE = 0xFFFFFF, COLOUR_INK = 0x15181D;

// Per channel (a * (100 - p) + b * p + 50) div 100, p from 0 to 100.
inline uint32_t mix(uint32_t a, uint32_t b, uint32_t p) {
  if (p > 100) p = 100;
  uint32_t out = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const uint32_t x = a >> shift & 255, y = b >> shift & 255;
    out |= ((x * (100 - p) + y * p + 50) / 100) << shift;
  }
  return out;
}

// (0.2126 * L[r] + 0.7152 * L[g]) + 0.0722 * L[b].
inline double lum(uint32_t c) {
  const double first = 0.2126 * LINEAR[c >> 16 & 255];
  const double second = 0.7152 * LINEAR[c >> 8 & 255];
  const double third = 0.0722 * LINEAR[c & 255];
  return (first + second) + third;
}

inline double contrast(uint32_t a, uint32_t b) {
  TAB_COUNT_CONTRAST();
  const double x = lum(a), y = lum(b);
  const double hi = x >= y ? x : y, lo = x >= y ? y : x;
  return (hi + 0.05) / (lo + 0.05);
}

// White or 0x15181D, the one with the higher contrast on c; white on a tie.
inline uint32_t readable(uint32_t c) {
  return contrast(COLOUR_WHITE, c) >= contrast(COLOUR_INK, c) ? COLOUR_WHITE : COLOUR_INK;
}

// c when it stands out on b; else c mixed half with t if that does; else t.
inline uint32_t legible(uint32_t c, uint32_t b, uint32_t t) {
  if (contrast(c, b) >= 3) return c;
  const uint32_t m = mix(c, t, 50);
  if (contrast(m, b) >= 3) return m;
  return t;
}

// Version 1's blend, kept for the one rule that uses it (0.82): per channel
// floor(a * (1 - t) + b * t + 0.5). t = 0 gives a.
inline uint32_t blend(uint32_t a, uint32_t b, double t) {
  uint32_t out = 0;
  for (int shift = 16; shift >= 0; shift -= 8) {
    const double first = (a >> shift & 255) * (1 - t);
    const double second = (b >> shift & 255) * t;
    out |= (uint32_t) (int) floor((first + second) + 0.5) << shift;
  }
  return out;
}

// The colours one card is drawn with: the page's theme with the card's style
// on top (render.js cardTokens).
struct Tok {
  uint32_t bg, bg2, tx, tx2, acc, acc2, acct, accf, accf_pr, card2, card2_pr, line, onbg, onbg_pr, ontx, ion, card_pr;
  int16_t r, r2, bw;
  int8_t gd;
  bool has_bg2;
};

// The nine colours of a card (contract 4.7) and what the card shows now.
struct CardColours {
  uint32_t A, AF, AT, AP, F, FT, FS, FI, FP;
  uint32_t background;   // the card's background now
  uint32_t text;         // the card's text colour now
  uint32_t border;       // its border colour
  uint32_t header_icon;  // the header icon
  int16_t border_width;  // lit 2: at least 2 (drawn without changing the object's border width)
  uint8_t dark;          // readable(bg) is white
  uint8_t filled;        // on 1 and lit 0: every "on:" colour applies
  uint8_t lit_on;        // on 1 and lit 1 or 2
  uint8_t sel_border;    // contrast(AF, background) < 1.3: a selected button gets a border
};

inline CardColours card_colours(const Tok &t, bool on, int lit, bool has_tint, uint32_t tint) {
  CardColours k{};
  const bool dark = readable(t.bg) == COLOUR_WHITE;
  if (!has_tint) {
    k.A = t.acc;
    k.AF = t.accf;
    k.AT = t.acct;
    k.AP = t.accf_pr;
    k.F = t.onbg;
    k.FT = t.ontx;
    k.FS = t.ontx;
    k.FI = t.ion;
    k.FP = t.onbg_pr;
  } else {
    const uint32_t a = legible(tint, t.bg, t.tx);
    const uint32_t f = mix(tint, t.bg, dark ? 78 : 84);
    k.A = a;
    k.AF = a;
    k.AT = readable(a);
    k.AP = mix(0x000000, a, 88);
    k.F = f;
    k.FT = t.tx;
    k.FS = contrast(t.tx2, f) >= 3 ? t.tx2 : t.tx;
    k.FI = legible(tint, f, t.tx);
    k.FP = mix(t.tx, f, 90);
  }
  const bool filled = on && lit == 0;
  const bool lit_on = on && (lit == 1 || lit == 2);
  k.dark = dark ? 1 : 0;
  k.filled = filled ? 1 : 0;
  k.lit_on = lit_on ? 1 : 0;
  k.background = filled ? k.F : t.bg;
  k.text = filled ? k.FT : t.tx;
  k.border = (filled || (on && lit == 2)) ? k.A : t.line;
  k.border_width = (int16_t) ((on && lit == 2) ? (t.bw > 2 ? t.bw : 2) : t.bw);
  k.header_icon = filled ? k.FI : lit_on ? k.A : t.tx2;
  k.sel_border = contrast(k.AF, k.background) < 1.3 ? 1 : 0;
  return k;
}

// An image widget's two colours (contract 4.7, "Image" and "Image, badge and
// bare"). look 0 box, 1 badge, 2 bare. boxed: the box or circle is drawn in
// fill; plain: the badge or bare image without a colour of its own.
struct ImageColours {
  uint32_t fill, icon;
  uint8_t boxed, plain;
};

inline ImageColours image_colours(const Tok &t, const CardColours &k, int look, bool has_c, uint32_t c) {
  ImageColours out{};
  if (look == 0) {
    out.boxed = 1;
    out.fill = t.card2;
    out.icon = has_c ? legible(c, t.card2, t.tx) : t.acc2;
    return out;
  }
  const bool plain = !has_c && !k.filled && !k.lit_on;
  const uint32_t K = has_c ? c : k.filled ? k.FI : k.lit_on ? k.A : t.tx2;
  out.plain = plain ? 1 : 0;
  if (look == 1) {
    out.boxed = 1;
    if (plain) {
      out.fill = t.card2;
      out.icon = t.tx2;
    } else {
      out.fill = mix(K, k.background, 80);
      out.icon = legible(K, out.fill, k.text);
    }
    return out;
  }
  out.icon = legible(K, k.background, k.text);
  return out;
}

}  // namespace detail
}  // namespace tab

#endif  // TAB_COLOUR_H
