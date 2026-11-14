#pragma once
// The characters of the host fonts, read from contract.json `fonts` as
// tools/gen_engine_fonts.py reads them for the tablet. Shared by test_engine
// and test_frozen, so both programs draw with the same glyphs.
#include <cstdint>
#include <string>
#include <vector>
#include "tj.h"

namespace hf {

// The code points of a UTF-8 text, character by character. A byte that does
// not start or continue a character as UTF-8 says makes the whole text
// invalid: then the list is empty.
static std::vector<uint32_t> utf8_codepoints(const std::string &s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    const uint8_t b = (uint8_t) s[i];
    int extra = b < 0x80 ? 0 : (b & 0xE0) == 0xC0 ? 1 : (b & 0xF0) == 0xE0 ? 2 : (b & 0xF8) == 0xF0 ? 3 : -1;
    if (extra < 0 || i + (size_t) extra >= s.size()) return {};
    uint32_t cp = extra == 0 ? b : extra == 1 ? (b & 0x1F) : extra == 2 ? (b & 0x0F) : (b & 0x07);
    for (int k = 1; k <= extra; k++) {
      const uint8_t c = (uint8_t) s[i + k];
      if ((c & 0xC0) != 0x80) return {};
      cp = cp << 6 | (c & 0x3F);
    }
    out.push_back(cp);
    i += (size_t) extra + 1;
  }
  return out;
}

// fonts.text_glyphs as pairs (first, last).
static std::vector<uint32_t> text_ranges(const tj::Value &contract) {
  std::vector<uint32_t> out;
  for (const tj::Value &r : contract["fonts"]["text_glyphs"].a) {
    out.push_back((uint32_t) r[0].i());
    out.push_back((uint32_t) r[1].i());
  }
  return out;
}

// fonts.digits_glyphs as pairs (c, c), one per character. The list is UTF-8:
// "°" is two bytes, and taken byte by byte it would put U+00C2 and U+00B0's
// second byte into the font instead of U+00B0.
static std::vector<uint32_t> digit_ranges(const tj::Value &contract) {
  std::vector<uint32_t> out;
  for (const uint32_t cp : utf8_codepoints(contract["fonts"]["digits_glyphs"].s)) {
    out.push_back(cp);
    out.push_back(cp);
  }
  return out;
}

}  // namespace hf
