// A small pull parser for the engine's JSON: device layouts and tab_set data.
// Works on a byte range that need not be NUL-terminated, never allocates and
// never writes into the input. Strict JSON (RFC 8259) with a nesting limit.
#ifndef TAB_JSON_H
#define TAB_JSON_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace tab {
namespace json {

// A raw string token: the bytes between the quotes, escapes not resolved.
struct Span {
  const char *p;
  size_t n;
  bool escaped;
};

enum Kind : char {
  K_ERROR = 0,
  K_OBJECT = 'o',
  K_ARRAY = 'a',
  K_STRING = 's',
  K_NUMBER = 'n',
  K_TRUE = 't',
  K_FALSE = 'f',
  K_NULL = 'z',
};

static const int MAX_DEPTH = 24;

inline int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

struct Reader {
  const char *p = nullptr;
  const char *end = nullptr;
  bool err = false;

  Reader() = default;
  Reader(const char *data, size_t n) : p(data), end(data + n) {}

  bool fail() {
    err = true;
    p = end;
    return false;
  }

  void ws() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  }

  bool at_end() {
    ws();
    return p >= end;
  }

  // The kind of the next value, without consuming it.
  Kind peek() {
    if (err) return K_ERROR;
    ws();
    if (p >= end) return K_ERROR;
    switch (*p) {
      case '{': return K_OBJECT;
      case '[': return K_ARRAY;
      case '"': return K_STRING;
      case 't': return K_TRUE;
      case 'f': return K_FALSE;
      case 'n': return K_NULL;
      default:
        if (*p == '-' || (*p >= '0' && *p <= '9')) return K_NUMBER;
        return K_ERROR;
    }
  }

  bool literal(const char *word) {
    const size_t n = strlen(word);
    if ((size_t) (end - p) < n || memcmp(p, word, n) != 0) return fail();
    p += n;
    return true;
  }

  // Reads a string token. Checks escapes and rejects raw control characters.
  bool string(Span *out) {
    if (peek() != K_STRING) return fail();
    p++;
    const char *start = p;
    bool escaped = false;
    while (p < end) {
      const unsigned char c = (unsigned char) *p;
      if (c == '"') {
        if (out) {
          out->p = start;
          out->n = (size_t) (p - start);
          out->escaped = escaped;
        }
        p++;
        return true;
      }
      if (c < 0x20) return fail();
      if (c == '\\') {
        escaped = true;
        if (++p >= end) return fail();
        switch (*p) {
          case '"': case '\\': case '/': case 'b': case 'f': case 'n': case 'r': case 't':
            p++;
            break;
          case 'u':
            if (end - p < 5) return fail();
            for (int i = 1; i <= 4; i++)
              if (hex_digit(p[i]) < 0) return fail();
            p += 5;
            break;
          default:
            return fail();
        }
        continue;
      }
      p++;
    }
    return fail();
  }

  // Reads a number token. Integers and decimals up to 15 significant digits
  // convert exactly as Python and JavaScript read them; longer ones go
  // through strtod. Fails on a value that is not finite and on a token of 64
  // characters or more. Skipping (out == nullptr) checks the syntax only.
  bool number(double *out) {
    if (peek() != K_NUMBER) return fail();
    const char *start = p;
    bool neg = false;
    if (*p == '-') {
      neg = true;
      p++;
    }
    if (p >= end) return fail();
    uint64_t mant = 0;
    int digits = 0, frac = 0;
    if (*p == '0') {
      p++;
    } else if (*p >= '1' && *p <= '9') {
      while (p < end && *p >= '0' && *p <= '9') {
        if (digits < 19) mant = mant * 10 + (uint64_t) (*p - '0');
        digits++;
        p++;
      }
    } else {
      return fail();
    }
    if (p < end && *p == '.') {
      p++;
      if (p >= end || *p < '0' || *p > '9') return fail();
      while (p < end && *p >= '0' && *p <= '9') {
        if (mant == 0 && *p == '0' && digits == 0) {
          frac++;  // leading zeros of a fraction do not count as digits
        } else {
          if (digits < 19) mant = mant * 10 + (uint64_t) (*p - '0');
          digits++;
          frac++;
        }
        p++;
      }
    }
    int exp = 0;
    bool has_exp = false;
    if (p < end && (*p == 'e' || *p == 'E')) {
      has_exp = true;
      p++;
      bool eneg = false;
      if (p < end && (*p == '+' || *p == '-')) {
        eneg = *p == '-';
        p++;
      }
      if (p >= end || *p < '0' || *p > '9') return fail();
      while (p < end && *p >= '0' && *p <= '9') {
        if (exp < 10000) exp = exp * 10 + (*p - '0');
        p++;
      }
      if (eneg) exp = -exp;
    }
    (void) has_exp;
    if (!out) return true;
    static const double P10[23] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                   1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    const int e10 = exp - frac;
    double v;
    if (digits <= 15 && e10 >= -22 && e10 <= 22) {
      v = (double) mant;
      v = e10 < 0 ? v / P10[-e10] : v * P10[e10];
      v = neg ? -v : v;
    } else {
      // A contract number is at most 15 characters (contract 4.1). A token
      // too long to convert here is refused, never read as some other value.
      char buf[64];
      const size_t n = (size_t) (p - start);
      if (n >= sizeof buf) return fail();
      memcpy(buf, start, n);
      buf[n] = 0;
      v = strtod(buf, nullptr);
    }
    // 1e400 reads as infinity: no finite value, so not a number here.
    if (!std::isfinite(v)) return fail();
    *out = v;
    return true;
  }

  // Object iteration. Call with first = true; it is cleared after the first
  // key. Returns false at the closing brace or on error.
  bool begin_object() {
    if (peek() != K_OBJECT) return fail();
    p++;
    return true;
  }

  bool next_key(bool &first, Span *key) {
    if (err) return false;
    ws();
    if (p >= end) return fail();
    if (*p == '}') {
      p++;
      return false;
    }
    if (!first) {
      if (*p != ',') return fail();
      p++;
    }
    first = false;
    if (!string(key)) return false;
    ws();
    if (p >= end || *p != ':') return fail();
    p++;
    return true;
  }

  bool begin_array() {
    if (peek() != K_ARRAY) return fail();
    p++;
    return true;
  }

  bool next_item(bool &first) {
    if (err) return false;
    ws();
    if (p >= end) return fail();
    if (*p == ']') {
      p++;
      return false;
    }
    if (!first) {
      if (*p != ',') return fail();
      p++;
    }
    first = false;
    ws();
    return true;
  }

  // Skips one value of any kind.
  bool skip(int depth = 0) {
    if (depth > MAX_DEPTH) return fail();
    switch (peek()) {
      case K_OBJECT: {
        begin_object();
        bool first = true;
        Span k;
        while (next_key(first, &k))
          if (!skip(depth + 1)) return false;
        return !err;
      }
      case K_ARRAY: {
        begin_array();
        bool first = true;
        while (next_item(first))
          if (!skip(depth + 1)) return false;
        return !err;
      }
      case K_STRING: return string(nullptr);
      case K_NUMBER: return number(nullptr);
      case K_TRUE: return literal("true");
      case K_FALSE: return literal("false");
      case K_NULL: return literal("null");
      default: return fail();
    }
  }
};

inline bool key_is(const Span &k, const char *lit) {
  const size_t n = strlen(lit);
  return !k.escaped && k.n == n && memcmp(k.p, lit, n) == 0;
}

inline size_t put_utf8(uint32_t cp, char *out) {
  if (cp < 0x80) {
    out[0] = (char) cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char) (0xC0 | (cp >> 6));
    out[1] = (char) (0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (char) (0xE0 | (cp >> 12));
    out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char) (0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char) (0xF0 | (cp >> 18));
  out[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char) (0x80 | (cp & 0x3F));
  return 4;
}

// Decodes the escape at p (p[0] is the backslash) of a string token checked
// by Reader::string. Writes 1 to 4 bytes to out and returns their count; *adv
// is the number of input bytes used. Never more bytes out than in.
inline size_t decode_escape(const char *p, size_t left, char out[4], size_t *adv) {
  const char e = p[1];
  *adv = 2;
  switch (e) {
    case '"': out[0] = '"'; return 1;
    case '\\': out[0] = '\\'; return 1;
    case '/': out[0] = '/'; return 1;
    case 'b': out[0] = '\b'; return 1;
    case 'f': out[0] = '\f'; return 1;
    case 'n': out[0] = '\n'; return 1;
    case 'r': out[0] = '\r'; return 1;
    case 't': out[0] = '\t'; return 1;
    default: break;
  }
  uint32_t cp = 0;
  for (int k = 0; k < 4; k++) cp = cp << 4 | (uint32_t) hex_digit(p[2 + k]);
  *adv = 6;
  if (cp >= 0xD800 && cp <= 0xDBFF && left >= 12 && p[6] == '\\' && p[7] == 'u') {
    uint32_t lo = 0;
    for (int k = 0; k < 4; k++) lo = lo << 4 | (uint32_t) hex_digit(p[8 + k]);
    if (lo >= 0xDC00 && lo <= 0xDFFF) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
      *adv = 12;
    } else {
      cp = 0xFFFD;
    }
  } else if ((cp >= 0xD800 && cp <= 0xDFFF) || cp == 0) {
    cp = 0xFFFD;  // a lone surrogate, or a NUL that would end the C string
  }
  return put_utf8(cp, out);
}

// Resolves the escapes of a string token checked by Reader::string. Writes at
// most s.n bytes (escapes never grow) and returns the count. No NUL is added.
// out may be nullptr to only count.
inline size_t unescape(const Span &s, char *out) {
  if (!s.escaped) {
    if (out) memcpy(out, s.p, s.n);
    return s.n;
  }
  size_t w = 0;
  char tmp[4];
  for (size_t i = 0; i < s.n;) {
    if (s.p[i] != '\\') {
      if (out) out[w] = s.p[i];
      w++;
      i++;
      continue;
    }
    size_t adv;
    const size_t n = decode_escape(s.p + i, s.n - i, tmp, &adv);
    if (out) memcpy(out + w, tmp, n);
    w += n;
    i += adv;
  }
  return w;
}

// Like unescape, but writes at most cap bytes and never cuts a UTF-8
// character in two. Returns the count. No NUL is added.
inline size_t unescape_max(const Span &s, char *out, size_t cap) {
  size_t w = 0;
  char tmp[4];
  for (size_t i = 0; i < s.n;) {
    const unsigned char c = (unsigned char) s.p[i];
    const char *src;
    size_t len, adv;
    if (c == '\\') {
      len = decode_escape(s.p + i, s.n - i, tmp, &adv);
      src = tmp;
    } else {
      len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
      if (i + len > s.n) len = s.n - i;
      adv = len;
      src = s.p + i;
    }
    if (w + len > cap) break;
    memcpy(out + w, src, len);
    w += len;
    i += adv;
  }
  return w;
}

}  // namespace json
}  // namespace tab

#endif  // TAB_JSON_H
