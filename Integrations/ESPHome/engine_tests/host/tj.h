#pragma once
// A small JSON reader for the test program, to read the contract fixtures and
// to check the engine's JSON answers. Independent of the engine's parser.
// It reads only inside [p, e): the text needs no NUL after it.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace tj {

struct Value {
  enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
  bool b = false;
  double n = 0;
  std::string s;  // also the raw text of a number
  std::vector<Value> a;
  std::vector<std::pair<std::string, Value>> o;

  bool has(const char *k) const {
    for (auto &kv : o)
      if (kv.first == k) return true;
    return false;
  }
  const Value &operator[](const char *k) const {
    static const Value none;
    for (auto &kv : o)
      if (kv.first == k) return kv.second;
    return none;
  }
  const Value &operator[](size_t i) const {
    static const Value none;
    return i < a.size() ? a[i] : none;
  }
  const Value &operator[](int i) const { return (*this)[(size_t) (i < 0 ? a.size() : (size_t) i)]; }
  size_t size() const { return t == ARR ? a.size() : o.size(); }
  bool is_null() const { return t == NUL; }
  int i() const { return (int) n; }
};

struct Parser {
  const char *p, *e;
  bool ok = true;
  void ws() {
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
  }
  static void put(std::string &out, uint32_t cp) {
    if (cp < 0x80) {
      out += (char) cp;
    } else if (cp < 0x800) {
      out += (char) (0xC0 | cp >> 6);
      out += (char) (0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += (char) (0xE0 | cp >> 12);
      out += (char) (0x80 | ((cp >> 6) & 0x3F));
      out += (char) (0x80 | (cp & 0x3F));
    } else {
      out += (char) (0xF0 | cp >> 18);
      out += (char) (0x80 | ((cp >> 12) & 0x3F));
      out += (char) (0x80 | ((cp >> 6) & 0x3F));
      out += (char) (0x80 | (cp & 0x3F));
    }
  }
  static bool digit(char c) { return c >= '0' && c <= '9'; }
  // The four hex digits of a \u escape. False when fewer than four are left
  // or one is not a hex digit.
  bool hex4(uint32_t *out) {
    if (e - p < 4) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++, p++) {
      const char c = *p;
      uint32_t d;
      if (digit(c))
        d = (uint32_t) (c - '0');
      else if (c >= 'a' && c <= 'f')
        d = (uint32_t) (c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        d = (uint32_t) (c - 'A' + 10);
      else
        return false;
      v = v * 16 + d;
    }
    *out = v;
    return true;
  }
  // The literal w (n bytes), only when all of it is left.
  bool lit(const char *w, size_t n) {
    if ((size_t) (e - p) < n || memcmp(p, w, n) != 0) return false;
    p += n;
    return true;
  }
  // A number in the JSON grammar: -? (0 | [1-9][0-9]*) (.[0-9]+)? ([eE][+-]?[0-9]+)?
  // strtod must then read the whole token.
  bool num(Value &v) {
    const char *s = p;
    if (p < e && *p == '-') p++;
    if (p >= e || !digit(*p)) return false;
    if (*p == '0') {
      p++;
    } else {
      while (p < e && digit(*p)) p++;
    }
    if (p < e && *p == '.') {
      p++;
      if (p >= e || !digit(*p)) return false;
      while (p < e && digit(*p)) p++;
    }
    if (p < e && (*p == 'e' || *p == 'E')) {
      p++;
      if (p < e && (*p == '+' || *p == '-')) p++;
      if (p >= e || !digit(*p)) return false;
      while (p < e && digit(*p)) p++;
    }
    v.t = Value::NUM;
    v.s.assign(s, p);
    char *end = nullptr;
    v.n = strtod(v.s.c_str(), &end);
    return end == v.s.c_str() + v.s.size();
  }
  bool str(std::string &out) {
    if (p >= e || *p != '"') return ok = false;
    p++;
    while (p < e && *p != '"') {
      if (*p != '\\') {
        out += *p++;
        continue;
      }
      p++;
      if (p >= e) return ok = false;  // the text ends after the backslash
      const char c = *p++;
      switch (c) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          uint32_t cp = 0;
          if (!hex4(&cp)) return ok = false;
          if (cp >= 0xD800 && cp <= 0xDBFF && e - p >= 2 && p[0] == '\\' && p[1] == 'u') {
            p += 2;
            uint32_t lo = 0;
            if (!hex4(&lo)) return ok = false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          put(out, cp);
          break;
        }
        default: out += c; break;
      }
    }
    if (p >= e) return ok = false;
    p++;
    return true;
  }
  Value val() {
    Value v;
    ws();
    if (p >= e) {
      ok = false;
      return v;
    }
    if (*p == '{') {
      v.t = Value::OBJ;
      p++;
      ws();
      if (p < e && *p == '}') {
        p++;
        return v;
      }
      while (ok) {
        ws();
        std::string k;
        if (!str(k)) break;
        ws();
        if (p >= e || *p != ':') {
          ok = false;
          break;
        }
        p++;
        v.o.emplace_back(k, val());
        ws();
        if (p < e && *p == ',') {
          p++;
          continue;
        }
        if (p < e && *p == '}') {
          p++;
          break;
        }
        ok = false;
      }
    } else if (*p == '[') {
      v.t = Value::ARR;
      p++;
      ws();
      if (p < e && *p == ']') {
        p++;
        return v;
      }
      while (ok) {
        v.a.push_back(val());
        ws();
        if (p < e && *p == ',') {
          p++;
          continue;
        }
        if (p < e && *p == ']') {
          p++;
          break;
        }
        ok = false;
      }
    } else if (*p == '"') {
      v.t = Value::STR;
      str(v.s);
    } else if (lit("true", 4)) {
      v.t = Value::BOOL;
      v.b = true;
    } else if (lit("false", 5)) {
      v.t = Value::BOOL;
    } else if (lit("null", 4)) {
    } else if (!num(v)) {
      ok = false;
    }
    return v;
  }
};

inline bool parse(const char *text, size_t n, Value *out) {
  Parser ps{text, text + n};
  *out = ps.val();
  ps.ws();
  return ps.ok && ps.p == ps.e;
}

inline bool parse(const std::string &s, Value *out) { return parse(s.data(), s.size(), out); }

inline Value load(const std::string &path) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) {
    fprintf(stderr, "cannot open %s\n", path.c_str());
    exit(2);
  }
  std::string s;
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  fclose(f);
  Value v;
  if (!parse(s, &v)) {
    fprintf(stderr, "bad JSON in %s\n", path.c_str());
    exit(2);
  }
  return v;
}

}  // namespace tj
