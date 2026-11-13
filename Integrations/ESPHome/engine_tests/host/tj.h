#pragma once
// A small JSON reader for the test program, to read the contract fixtures and
// to check the engine's JSON answers. Independent of the engine's parser.
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
  uint32_t hex4() {
    uint32_t v = 0;
    for (int i = 0; i < 4 && p < e; i++, p++) {
      const char c = *p;
      v = v * 16 + (uint32_t) (c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
    }
    return v;
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
      const char c = *p++;
      switch (c) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          uint32_t cp = hex4();
          if (cp >= 0xD800 && cp <= 0xDBFF && p + 1 < e && p[0] == '\\' && p[1] == 'u') {
            p += 2;
            const uint32_t lo = hex4();
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
    } else if (!strncmp(p, "true", 4)) {
      v.t = Value::BOOL;
      v.b = true;
      p += 4;
    } else if (!strncmp(p, "false", 5)) {
      v.t = Value::BOOL;
      p += 5;
    } else if (!strncmp(p, "null", 4)) {
      p += 4;
    } else {
      v.t = Value::NUM;
      const char *s = p;
      while (p < e && (strchr("+-0123456789.eE", *p) != nullptr)) p++;
      v.s.assign(s, p);
      v.n = strtod(v.s.c_str(), nullptr);
      if (p == s) ok = false;
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
