// Host tests for the TAB-1 dashboard engine (design 7.9), against the
// contract fixtures in engine_tests/contract. Run through engine_tests/run.sh.
//
//   test_engine <contract dir> <text ttf> <icon ttf> <out dir> [--quick] [name ...]
//
// Screenshots go to <out dir>/shots, the report of cut texts to
// <out dir>/cut_texts.txt. Exit code 0 only when every check passed.
#define TAB_TEST_OBJECT_LIMIT 1  // tab_ui.h: lets a test make object creation fail
#define TAB_TEST_COUNTERS 1      // tab_model.h: counts what the engine does
#include "harness.h"
#include "fake_platform.h"
#include "tj.h"
#include "tab_engine.h"
#include "frames.h"
#include "host_fonts.h"
#include "src/core/lv_obj_private.h"  // repaint_cost reads whether a layout pass is due
#include "src/display/lv_display_private.h"  // and which areas are drawn again

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace D = tab::detail;

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
    g_failures.push_back(std::string(g_test) + buf + what);
    fprintf(stderr, "  FAIL %s%s%s\n", g_test, buf, what.c_str());
  }
  return ok;
}
#define CHECK(c) check((c), __LINE__, #c)
#define CHECK_MSG(c, msg) check((c), __LINE__, std::string(#c) + "  " + (msg))
#define CHECK_STR(a, b) check(std::string(a) == std::string(b), __LINE__, std::string(#a) + " == \"" + std::string(b) + "\" (got \"" + std::string(a) + "\")")

// ---------------------------------------------------------------- environment

static std::string CDIR, OUT, SHOTS;
static bool QUICK = false;
static tab::Fonts g_fonts;

static const char *NONCE = "0123456789abcdef0123456789abcdef";
static const char *SECRET_HEX = "6465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f80818283";
static const char *PSK_HEX = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

static std::string read_file(const std::string &path) {
  size_t n = 0;
  uint8_t *d = hh::load_file(path.c_str(), &n);
  std::string s(reinterpret_cast<char *>(d), n);
  free(d);
  return s;
}

static std::string fixture_path(const char *name) { return CDIR + "/fixtures/" + name; }
static tj::Value fixture(const char *name) { return tj::load(fixture_path(name)); }

static std::string hex(const uint8_t *d, size_t n) {
  std::string s(n * 2, '0');
  tab::crypto::to_hex(d, n, &s[0]);
  return s;
}

static void unhex(const std::string &h, uint8_t *out) { tab::crypto::from_hex(h.data(), h.size() / 2, out); }

static std::string hmac_hex(const uint8_t *key, size_t key_len, const std::string &msg, size_t bytes) {
  uint8_t d[32];
  tab::crypto::hmac_sha256(key, key_len, msg.data(), msg.size(), d);
  return hex(d, bytes);
}

static std::string sha_hex(const std::string &s) {
  uint8_t d[32];
  tab::crypto::sha256(s.data(), s.size(), d);
  return hex(d, 32);
}

static std::string call_auth(const uint8_t *secret, const std::string &nonce, uint32_t n, const char *action,
                             const std::string &body) {
  const std::string msg = std::string("apollo-tab1 call v1|") + fake::MAC + "|" + nonce + "|" + std::to_string(n) +
                          "|" + action + "|" + body;
  return std::to_string(n) + "." + hmac_hex(secret, 32, msg, 16);
}

// ---------------------------------------------------------------- engine control

static void shutdown() {
  if (D::g.started) {
    D::teardown();
    D::g.started = false;
  }
  hh::g_down = false;
  hh::advance(40);
}

// A fresh fake platform. The pairing key (psk) is the fixtures'.
static void reset_platform(bool keep_flash = false) {
  const bool had = fake::st.flash_has;
  std::vector<uint8_t> flash = fake::st.flash;
  fake::st = fake::State();
  unhex(PSK_HEX, fake::st.key);
  if (keep_flash) {
    fake::st.flash_has = had;
    fake::st.flash = flash;
  }
}

static void start() {
  tab::begin(g_fonts, fake::platform());
  hh::advance(20);
}

static D::Layout *active() { return D::active_layout(); }

static D::Widget *widget(uint32_t id, D::Layout *l = nullptr) {
  if (!l) l = active();
  if (!l || !l->model) return nullptr;
  for (uint32_t i = 0; i < l->model->nw; i++)
    if (l->model->w[i].id == id) return &l->model->w[i];
  return nullptr;
}

static bool advance_until(const std::function<bool()> &done, uint32_t max_ms) {
  for (uint32_t t = 0; t < max_ms; t++) {
    if (done()) return true;
    hh::advance(1);
  }
  return done();
}

// Chunks of at most n bytes that never end inside a UTF-8 character (contract 5).
static std::vector<std::string> chunks(const std::string &bytes, size_t n) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < bytes.size()) {
    size_t end = i + n < bytes.size() ? i + n : bytes.size();
    while (end < bytes.size() && end > i && ((uint8_t) bytes[end] & 0xC0) == 0x80) end--;
    out.push_back(bytes.substr(i, end - i));
    i = end;
  }
  return out;
}

// The tablet as HA sees it: pairing, a session, signed calls.
struct Ha {
  uint8_t secret[32];
  bool paired = false;
  std::string nonce = NONCE;
  uint32_t n = 0;
  int hellos = 0;  // this boot; a nonce starts one session per boot (contract 3)

  Ha() { unhex(SECRET_HEX, secret); }

  // A fresh engine. paired: the fixture secret is stored.
  void boot(bool paired_tablet, bool keep_flash = false) {
    shutdown();
    reset_platform(keep_flash);
    paired = paired_tablet;
    if (paired) {
      memcpy(fake::st.secret, secret, 32);
      fake::st.have_secret = true;
    }
    hellos = 0;
    nonce = NONCE;
    start();
  }

  std::string auth(const char *action, const std::string &body) {
    if (!paired) return "";
    return call_auth(secret, nonce, ++n, action, body);
  }

  // The first hello of a boot uses NONCE, later ones a fresh nonce, as HA does.
  bool hello() {
    if (hellos++ > 0) {
      char b[33];
      snprintf(b, sizeof b, "%016x%016x", 0xfeedu, (unsigned) hellos);
      nonce = b;
    }
    const std::string a = paired ? call_auth(secret, nonce, 0, "tab_hello", "") : "";
    char out[768];
    const size_t len = tab::on_hello(nonce.data(), nonce.size(), a.data(), a.size(), out, sizeof out);
    n = 0;
    tj::Value v;
    return len > 0 && tj::parse(out, len, &v) && v["session"].n == 1;
  }

  std::string send(const std::string &bytes, size_t chunk = 3000) {
    char hash[17];
    D::hash_of(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), hash);
    const auto parts = chunks(bytes, chunk);
    for (size_t i = 0; i < parts.size(); i++) {
      const char *r = tab::on_layout(hash, 16, (int) i, (int) parts.size(), (int) bytes.size(), parts[i].data(),
                                     parts[i].size());
      if (*r) return r;
    }
    return "";
  }

  std::string commit(const std::string &hash) {
    const std::string a = auth("tab_commit", hash);
    return tab::on_commit(hash.data(), hash.size(), a.data(), a.size());
  }

  std::string set(const std::string &data) {
    const std::string a = auth("tab_set", data);
    return tab::on_set(data.data(), data.size(), a.data(), a.size());
  }

  void ack(uint32_t counter, bool ok, const std::string &code) {
    const std::string body = std::to_string(counter) + "|" + (ok ? "1" : "0") + "|" + code;
    const std::string a = auth("tab_ack", body);
    tab::on_ack((int) counter, ok, code.data(), code.size(), a.data(), a.size());
  }

  std::string clear() {
    const std::string a = auth("tab_clear", "");
    return tab::on_clear(a.data(), a.size());
  }

  // Sends, commits and waits until the layout is on screen.
  bool show(const std::string &bytes) {
    const std::string hash = sha_hex(bytes).substr(0, 16);
    if (!send(bytes).empty()) return false;
    if (!commit(hash).empty()) return false;
    return advance_until([&] { return hash == tab::active_hash(); }, 3000);
  }
};

static std::string layout_bytes(const char *name) { return read_file(fixture_path(name)); }

static std::string state_payload(int step, int part = 0) {
  return fixture("state_stream.json")["steps"][step]["payloads"][part].s;
}

static int centre_x(const int16_t r[4]) { return r[0] + r[2] / 2; }
static int centre_y(const int16_t r[4]) { return r[1] + r[3] / 2; }

static bool obj_at(lv_obj_t *o, int x, int y, int w, int h) {
  if (!o) return false;
  lv_obj_update_layout(o);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  return a.x1 == x && a.y1 == y && lv_area_get_width(&a) == w && lv_area_get_height(&a) == h;
}

static std::string rect_text(lv_obj_t *o) {
  if (!o) return "(none)";
  lv_obj_update_layout(o);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  char b[64];
  snprintf(b, sizeof b, "[%d,%d,%d,%d]", (int) a.x1, (int) a.y1, (int) lv_area_get_width(&a),
           (int) lv_area_get_height(&a));
  return b;
}

static bool hidden(lv_obj_t *o) { return !o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }

static std::string label_text(lv_obj_t *o) { return o ? lv_label_get_text(o) : ""; }

static const lv_font_t *font_of(lv_obj_t *o) { return o ? lv_obj_get_style_text_font(o, LV_PART_MAIN) : nullptr; }

// The text a label shows for a slot value: control characters as spaces.
static std::string shown(const std::string &s) {
  std::string out = s;
  for (char &c : out)
    if ((uint8_t) c < 0x20) c = ' ';
  return out;
}

static bool shows_text(lv_obj_t *o, const std::string &value, bool cut) {
  const std::string got = label_text(o), want = shown(value);
  if (!cut) return got == want;
  const std::string ell = "\xE2\x80\xA6";
  if (got.size() < ell.size() || got.compare(got.size() - ell.size(), ell.size(), ell) != 0) return false;
  const std::string head = got.substr(0, got.size() - ell.size());
  return want.compare(0, head.size(), head) == 0 && head.size() < want.size();
}

static int screen_colours(int x0) {
  std::map<uint16_t, int> seen;
  for (int y = 0; y < hh::H; y += 2)
    for (int x = x0; x < hh::W; x += 2) seen[hh::pixel(x, y)]++;
  return (int) seen.size();
}

static double share_not(uint16_t colour, int x0) {
  long other = 0, all = 0;
  for (int y = 0; y < hh::H; y += 2)
    for (int x = x0; x < hh::W; x += 2) {
      all++;
      if (hh::pixel(x, y) != colour) other++;
    }
  return (double) other / (double) all;
}

static void shot(const std::string &name) {
  const std::string path = SHOTS + "/" + name + ".png";
  CHECK_MSG(hh::screenshot(path.c_str()), path);
}

// The waiting screen's headline and next step for each state, and the Wi-Fi
// notes over a dashboard, from the contract's screens.json (CONTRACT.md 11),
// which the panel's tests read too: load_screens() fills these before the
// tests run, with {hotspot} the fake platform's hotspot name (the YAML's
// `wifi: ap: ssid:`; tools/check_engine_package.py checks that fake_platform.h
// uses the same, and that screens.json names it) and {name} the fake tablet's
// name. screens_match_contract compares tab_state.h's own words with them.
static tj::Value SCREENS;
static std::string WAIT_HA, WAIT_HOTSPOT, WAIT_CONNECTING, WAIT_ADD, WAIT_ERASED, WAIT_ERASED2, WAIT_ERASED_LONG,
    WAIT_STARTING, WAIT_RUNNING, WAIT_LOADING, WAIT_PICK, WAIT_RESTART, WAIT_OFF_ON;
static std::string T_HOTSPOT, T_CONNECTING, T_ADD, T_ERASED, T_STARTING, T_TURN_ON, T_WAITING, T_LOADING, T_PICK,
    T_CLOSED, T_NO_WIFI;
static std::string NOTE_HOTSPOT, NOTE_CONNECTING, NO_WIFI;

static std::string fill_in(std::string s, const char *key, const std::string &value) {
  for (size_t at = s.find(key); at != std::string::npos; at = s.find(key, at + value.size()))
    s.replace(at, strlen(key), value);
  return s;
}

// A screens.json text with its placeholders filled in.
static std::string screen_words(const tj::Value &v, const char *hotspot = fake::State().hotspot_name,
                                const char *name = fake::NAME) {
  return fill_in(fill_in(v.s, "{hotspot}", hotspot ? hotspot : ""), "{name}", name ? name : "");
}

static bool load_screens() {
  SCREENS = tj::load(CDIR + "/screens.json");
  const tj::Value &w = SCREENS["waiting"], &n = SCREENS["notes"];
  if (w.size() != 11 || n.size() != 3) return false;
  std::string *steps[] = {&WAIT_HOTSPOT, &WAIT_CONNECTING, &WAIT_ADD, &WAIT_ERASED, &WAIT_STARTING, &WAIT_HA,
                          &WAIT_RUNNING, &WAIT_LOADING, &WAIT_PICK, &WAIT_RESTART, &WAIT_OFF_ON};
  std::string *titles[] = {&T_HOTSPOT, &T_CONNECTING, &T_ADD, &T_ERASED, &T_STARTING, &T_TURN_ON,
                           &T_WAITING, &T_LOADING, &T_PICK, &T_CLOSED, &T_NO_WIFI};
  const char *states[] = {"hotspot", "connecting", "add", "erased", "starting", "turn_on",
                          "wait_ha", "loading", "pick", "closed", "no_wifi"};
  for (int i = 0; i < 11; i++) {
    *titles[i] = screen_words(w[states[i]]["headline"]);
    *steps[i] = screen_words(w[states[i]]["step"]);
    if (titles[i]->empty() || steps[i]->empty()) return false;
  }
  WAIT_ERASED2 = screen_words(w["erased"]["step2"]);
  WAIT_ERASED_LONG = screen_words(w["erased"]["step_long_name"]);
  NOTE_HOTSPOT = screen_words(n["hotspot"]["text"]);
  NOTE_CONNECTING = screen_words(n["connecting"]["text"]);
  NO_WIFI = screen_words(n["off_on"]["text"]);
  return !WAIT_ERASED2.empty() && !WAIT_ERASED_LONG.empty() && !NOTE_HOTSPOT.empty() && !NOTE_CONNECTING.empty() &&
         !NO_WIFI.empty();
}

// ================================================================ crypto and JSON

TEST(crypto_vectors) {
  CHECK_STR(sha_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK_STR(sha_hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK_STR(sha_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  {
    std::string m(1000000, 'a');
    tab::crypto::Sha256 s;
    tab::crypto::sha256_init(s);
    for (size_t i = 0; i < m.size(); i += 777) tab::crypto::sha256_update(s, m.data() + i, std::min<size_t>(777, m.size() - i));
    uint8_t d[32];
    tab::crypto::sha256_final(s, d);
    CHECK_STR(hex(d, 32), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  }
  // RFC 4231 cases 1, 2 and 6 (key longer than a block).
  uint8_t k1[20];
  memset(k1, 0x0b, sizeof k1);
  CHECK_STR(hmac_hex(k1, 20, "Hi There", 32), "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
  CHECK_STR(hmac_hex(reinterpret_cast<const uint8_t *>("Jefe"), 4, "what do ya want for nothing?", 32),
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
  uint8_t k6[131];
  memset(k6, 0xaa, sizeof k6);
  CHECK_STR(hmac_hex(k6, 131, "Test Using Larger Than Block-Size Key - Hash Key First", 32),
            "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
  // Contract values: layout hashes, the key id of the fixture secret.
  for (const char *name : {"layout_small", "layout_all_cards", "layout_all_cards.nochart"}) {
    const std::string bytes = layout_bytes((std::string(name) + ".dl.json").c_str());
    const tj::Value meta = fixture((std::string(name) + ".meta.json").c_str());
    CHECK_STR(sha_hex(bytes).substr(0, 16), meta["hash"].s);
    CHECK(bytes.size() == (size_t) meta["size"].n);
  }
  uint8_t secret[32];
  unhex(SECRET_HEX, secret);
  char kid[17];
  D::kid_of(secret, kid);
  CHECK_STR(kid, "d9ccb8a994337e34");
  CHECK(tab::crypto::is_hex("00ff", 4, 4) && !tab::crypto::is_hex("00FF", 4, 4) && !tab::crypto::is_hex("00f", 3, 4));
  // Wire numbers as the reference num()/str() writes them.
  const std::pair<double, const char *> nums[] = {{42, "42"},       {42.5, "42.5"},  {20.125, "20.125"},
                                                  {0.1 + 0.2, "0.3"}, {-0.0001, "0"},  {41.9999, "42"},
                                                  {1e12, "1000000000"}, {-4.25, "-4.25"}, {0.0625, "0.062"},
                                                  {123456789.5, "123456789.5"}};
  for (auto &p : nums) {
    char b[48];
    D::wire_number(p.first, b, sizeof b);
    CHECK_STR(b, p.second);
  }
}

static bool json_num(const char *text, double *v) {
  tab::json::Reader r(text, strlen(text));
  return r.number(v) && r.at_end();
}

TEST(json_parser) {
  double v = 0;
  CHECK(json_num("0", &v) && v == 0);
  CHECK(json_num("-0", &v) && v == 0);
  CHECK(json_num("20.125", &v) && v == 20.125);
  CHECK(json_num("-4.25", &v) && v == -4.25);
  CHECK(json_num("19.36", &v) && v == 19.36);
  CHECK(json_num("0.001", &v) && v == 0.001);
  CHECK(json_num("1e3", &v) && v == 1000);
  CHECK(json_num("2.5E-1", &v) && v == 0.25);
  CHECK(json_num("1000000000", &v) && v == 1e9);
  CHECK(json_num("123456789.123", &v) && v == 123456789.123);
  CHECK(json_num("0.30000000000000004", &v) && v == 0.1 + 0.2);
  for (const char *bad : {"01", "1.", ".5", "+1", "1e", "--1", "-", "1.e3", "0x10"}) CHECK_MSG(!json_num(bad, &v), bad);
  // Strings: escapes, surrogate pairs, control characters.
  {
    const char *t = "\"K\\u00fcche \\\"A\\\"\\\\B \\u00b7 21\\u00b0\\n2 \\ud83d\\ude00 \\udc00x\"";
    tab::json::Reader r(t, strlen(t));
    tab::json::Span s;
    CHECK(r.string(&s) && s.escaped);
    char out[128];
    const size_t n = tab::json::unescape(s, out);
    CHECK_STR(std::string(out, n), "K\xC3\xBC" "che \"A\"\\B \xC2\xB7 21\xC2\xB0\n2 \xF0\x9F\x98\x80 \xEF\xBF\xBDx");
    CHECK(n <= s.n);
    // Bounded copy never cuts a character.
    const size_t m = tab::json::unescape_max(s, out, 2);
    CHECK(m == 1 && out[0] == 'K');
  }
  {
    const char raw[] = "\"a\x01\"";
    tab::json::Reader r(raw, sizeof raw - 1);
    CHECK(!r.string(nullptr));
  }
  for (const char *bad : {"\"\\x\"", "\"\\u12\"", "\"abc", "{\"a\":1,}", "[1,]", "[1 2]", "{\"a\" 1}", "nul", "tru"}) {
    tab::json::Reader r(bad, strlen(bad));
    CHECK_MSG(!r.skip() || !r.at_end(), bad);
  }
  // Nesting limit.
  std::string deep(200, '[');
  deep += std::string(200, ']');
  tab::json::Reader dr(deep.data(), deep.size());
  CHECK(!dr.skip());
  // Unescaped and escaped "slots" texts give the same types.
  D::Loader ld;
  const char *esc = "{\"pages\":[{\"title\":\"\",\"w\":[]}],\"slots\":\"t\\u0062n\",\"v\":1}";
  CHECK_STR(D::model_check(esc, strlen(esc), ld), "");
  CHECK(ld.c.slots == 3 && ld.slot_type_of(2) == 'b' && ld.slot_type_of(3) == 'n');
}

// The test program's own JSON reader (host/tj.h) checks the engine's answers,
// so it must read only the bytes it is given, and only JSON. tj_exact copies
// the text to a heap block of exactly its size with no NUL after it: a read
// past the end is an error in the AddressSanitizer build.
static bool tj_exact(const std::string &text, tj::Value *v) {
  char *b = static_cast<char *>(malloc(text.empty() ? 1 : text.size()));
  if (!text.empty()) memcpy(b, text.data(), text.size());
  const bool ok = tj::parse(b, text.size(), v);
  free(b);
  return ok;
}

TEST(tj_reader_bounds) {
  tj::Value v;
  // Cut off in an escape, a \u escape or a literal: refused, nothing read past the end.
  for (const char *bad : {"\"\\", "\"a\\", "[\"\\", "{\"k\\", "\"\\u", "\"\\u0", "\"\\u00", "\"\\u00f",
                          "\"\\u00fc", "\"\\ud83d\\", "\"\\ud83d\\u", "\"\\ud83d\\ude0", "t", "tr", "tru", "f",
                          "fa", "fal", "fals", "n", "nu", "nul", "[tru", "{\"a\":fals", "[1,nul"})
    CHECK_MSG(!tj_exact(bad, &v), bad);
  // A \u escape needs four hex digits.
  for (const char *bad : {"\"\\u00zz\"", "\"\\u0g00\"", "\"\\u12\"", "\"\\u\"", "\"\\ud83d\\uzzzz\""})
    CHECK_MSG(!tj_exact(bad, &v), bad);
  // Whole ones still read, also with no NUL after them.
  CHECK(tj_exact("true", &v) && v.t == tj::Value::BOOL && v.b);
  CHECK(tj_exact("false", &v) && v.t == tj::Value::BOOL && !v.b);
  CHECK(tj_exact("null", &v) && v.is_null());
  CHECK(tj_exact("[true,false,null]", &v) && v.size() == 3 && v[0].b && !v[1].b && v[2].is_null());
  CHECK(tj_exact("\"a\\n\\\"b\\\\\"", &v) && v.s == "a\n\"b\\");
  CHECK(tj_exact("\"\\u00fc\\u00B0\"", &v) && v.s == "\xC3\xBC\xC2\xB0");
  CHECK(tj_exact("\"\\ud83d\\ude00\"", &v) && v.s == "\xF0\x9F\x98\x80");
  CHECK(tj_exact("{\"k\":\"\\u0041\"}", &v) && v["k"].s == "A");
}

TEST(tj_reader_numbers) {
  tj::Value v;
  // Only the JSON number grammar: -? (0 | [1-9][0-9]*) (.[0-9]+)? ([eE][+-]?[0-9]+)?
  const std::vector<std::string> bad = {
      "+", "1e", "1.2.3", "01", ".5", "1.", "--1", "1e+", "[1,+]", std::string("\0", 1),
      std::string("1\0", 2), std::string("[1,\0]", 5), "-", "+1", "1e-", "1E", "1.e3", "-.5",
      "00", "-01", "0x10", "1e2.5", "[1,-]", "{\"a\":1.}", "[.5]", "Infinity", "NaN", "1 2"};
  for (const std::string &b : bad) {
    std::string shown;
    for (const char c : b) shown += c ? std::string(1, c) : std::string("\\0");
    CHECK_MSG(!tj_exact(b, &v), shown);
  }
  const std::pair<const char *, double> good[] = {
      {"0", 0},        {"-0", 0},         {"7", 7},        {"-12", -12},       {"10", 10},
      {"1.5", 1.5},    {"-0.25", -0.25},  {"1e3", 1000},   {"1E3", 1000},      {"1e+3", 1000},
      {"1e-3", 0.001}, {"2.5E-1", 0.25},  {"0.5e1", 5},    {"0e0", 0},         {" 42 ", 42},
      {"123456789.125", 123456789.125}};
  for (auto &g : good) {
    const bool read = tj_exact(g.first, &v);
    CHECK_MSG(read && v.t == tj::Value::NUM && v.n == g.second, g.first);
  }
  CHECK(tj_exact("1e+3", &v) && v.s == "1e+3");  // the raw text stays
  CHECK(tj_exact("[1,-2,3.5e2]", &v) && v.size() == 3 && v[0].n == 1 && v[1].n == -2 && v[2].n == 350);
  CHECK(tj_exact("{\"a\":-0.5,\"b\":0}", &v) && v["a"].n == -0.5 && v["b"].n == 0);
}

TEST(layout_shapes) {
  // Any key order, unknown keys and unknown widget kinds are fine (contract 4.1, 4.6).
  const char *dl =
      "{\"v\":1,\"zzz\":{\"x\":[1,2,{\"y\":null}]},\"theme\":{\"wall\":1053720,\"card\":1580067,\"tx\":15264493,"
      "\"r\":16,\"r2\":10,\"bw\":1},\"slots\":\"tn\",\"pages\":[{\"w\":[{\"t\":\"card\",\"r\":[100,84,376,184],"
      "\"id\":7,\"w\":[{\"t\":\"gauge\",\"id\":8,\"r\":[1,2,3,4],\"value\":{\"s\":2}},{\"value\":{\"s\":2},\"r\":[116,"
      "100,344,40],\"t\":\"slider\",\"id\":9,\"change\":{\"p\":0},\"min\":0,\"max\":10,\"step\":0.5},{\"text\":{\"s\":1},"
      "\"t\":\"label\",\"id\":10,\"r\":[116,150,344,28]}]},{\"t\":\"newthing\",\"id\":11,\"r\":[0,0,1,1]}],\"title\":\"P\","
      "\"extra\":true}],\"nav\":[{\"title\":\"P\",\"p\":0}],\"screen\":[1280,800]}";
  D::Loader ld;
  CHECK_STR(D::model_check(dl, strlen(dl), ld), "");
  CHECK(ld.c.widgets == 3);  // card, slider, label; the gauge and newthing are skipped
  CHECK(ld.c.ids == 5);      // but every object with an id counts toward the limit
  // Wrong slot type in a reference, bad rectangle, missing v, newer v.
  const std::pair<const char *, const char *> bad[] = {
      {"{\"v\":1,\"slots\":\"t\",\"pages\":[{\"w\":[{\"t\":\"card\",\"id\":1,\"r\":[0,0,1,1],\"on\":{\"s\":1}}]}]}",
       "parse"},
      {"{\"v\":1,\"slots\":\"\",\"pages\":[{\"w\":[{\"t\":\"card\",\"id\":1,\"r\":[0,0,1]}]}]}", "parse"},
      {"{\"slots\":\"\",\"pages\":[]}", "parse"},
      {"{\"v\":3,\"pages\":7}", "version"},
      {"{\"v\":1,\"slots\":\"tx\",\"pages\":[]}", "parse"},
      {"[1]", "parse"},
      {"{\"v\":1,\"pages\":[]} x", "parse"}};
  for (auto &b : bad) {
    D::Loader l2;
    CHECK_STR(D::model_check(b.first, strlen(b.first), l2), b.second);
  }
  // Loads and draws on a tablet.
  Ha ha;
  ha.boot(false);
  CHECK(ha.show(dl));
  D::Widget *s = widget(9);
  CHECK(s && s->kind == D::W_SLIDER);
  if (s) CHECK_MSG(obj_at(s->obj, 116, 100, 344, 40), rect_text(s->obj));
  CHECK(widget(8) == nullptr && widget(11) == nullptr);
  CHECK_STR(ha.set("{\"h\":\"" + std::string(tab::active_hash()) + "\",\"v\":[[2,4.5],[1,\"ok\"]]}"), "");
  double num = 0;
  CHECK(tab::slot_number(2, &num) && num == 4.5);
  CHECK_STR(label_text(widget(10)->obj), "ok");
}

// ================================================================ protocol fixtures

static std::string status_build() {
  char out[768];
  tj::Value v;
  if (!tj::parse(out, tab::on_status(out, sizeof out), &v)) return "?";
  return v["build"].s;
}

TEST(chunks_fixture) {
  const tj::Value f = fixture("chunks.json");
  CHECK(f["cases"].size() == 24);
  Ha ha;
  for (const tj::Value &c : f["cases"].a) {
    g_test = c["name"].s.c_str();
    ha.boot(false);
    fake::st.alloc_fails = c["platform"]["alloc_fails"].b;
    const std::string build_fails = c["platform"]["build_fails"].s;
    auto run_calls = [&](const tj::Value &calls, const tj::Value &expect) {
      for (size_t i = 0; i < calls.size(); i++) {
        const tj::Value &k = calls[i];
        const char *r = tab::on_layout(k["hash"].s.data(), k["hash"].s.size(), k["seq"].i(), k["total"].i(),
                                       k["size"].i(), k["data"].s.data(), k["data"].s.size());
        CHECK_STR(r, expect[i].s == "ok" ? "" : expect[i].s);
      }
    };
    CHECK(c["calls"].size() >= 1 && c["calls"].size() == c["expect"].size());
    run_calls(c["calls"], c["expect"]);
    const std::string hash = c.has("commit_hash") ? c["commit_hash"].s : c["calls"][0]["hash"].s;
    const char *got = tab::on_commit(hash.data(), hash.size(), "", 0);
    CHECK_STR(got, c["commit"].s == "ok" ? "" : c["commit"].s);
    if (!build_fails.empty()) {
      // The build that follows runs out of memory: too small a block left.
      CHECK_STR(build_fails, "memory");
      CHECK(tab::building());
      fake::st.largest = 1000000;
      fake::st.free = 1000000;
      hh::advance(20);
      fake::st.largest = 16000000;
      fake::st.free = 28000000;
      CHECK(!tab::building());
      CHECK_STR(status_build(), build_fails);
    }
    if (c.has("after_commit")) {
      const tj::Value &ac = c["after_commit"];
      CHECK(tab::building() == build_fails.empty());
      CHECK(ac["calls"].size() == ac["expect"].size());
      run_calls(ac["calls"], ac["expect"]);
      CHECK(tab::building() == ac["building"].b);  // a seq 0 call drops the build
      const std::string h2 = ac.has("commit_hash") ? ac["commit_hash"].s : ac["calls"][0]["hash"].s;
      CHECK_STR(tab::on_commit(h2.data(), h2.size(), "", 0), ac["commit"].s == "ok" ? "" : ac["commit"].s);
    }
    const std::string shown = c.has("shown") ? c["shown"].s : c["commit"].s == "ok" ? hash : "";
    if (!shown.empty()) {
      CHECK(advance_until([&] { return shown == tab::active_hash(); }, 3000));
      hh::advance(100);
      // A dropped build released its two blocks; the shown layout holds two.
      CHECK(fake::st.live.size() == 2);
    } else {
      hh::advance(100);
      CHECK_STR(tab::active_hash(), "");
      CHECK(!tab::building());
      CHECK(fake::st.live.size() <= 1);  // at most an unfinished transfer
    }
    CHECK(!fake::st.bad_release);
  }
  g_test = "chunks_fixture";
}

// ================================================================ contract readings
//
// Three cases the contract once left open, now contract rules (CONTRACT.md
// 4.7 and 5, cases in fixtures/chunks.json).

// Reading 1: the no-value "–" (U+2013) is not in the 48 px digits font, so it
// takes the largest text size that fits (34 at most), never the digits font.
TEST(reading_thermostat_dash) {
  static const char *DASH = "\xE2\x80\x93";
  const std::string dl =
      "{\"nav\":[],\"pages\":[{\"title\":\"P\",\"w\":[{\"id\":1,\"r\":[100,40,1100,700],\"t\":\"card\",\"w\":["
      "{\"id\":2,\"max\":35,\"min\":5,\"r\":[120,60,320,320],\"step\":0.5,\"t\":\"thermostat\",\"value\":{\"s\":1}},"
      "{\"id\":3,\"max\":35,\"min\":5,\"r\":[500,60,120,120],\"step\":0.5,\"t\":\"thermostat\",\"value\":{\"s\":2}}"
      "]}]}],\"screen\":[1280,800],\"slots\":\"nn\",\"theme\":{\"bw\":1,\"card\":1580067,\"r\":16,\"r2\":10,"
      "\"tx\":15264493,\"wall\":1053720},\"v\":1}";
  Ha ha;
  ha.boot(false);
  if (!CHECK(ha.show(dl))) return;
  const std::string h = tab::active_hash();
  D::Widget *big = widget(2), *small = widget(3);
  if (!CHECK(big && small && big->o1 && small->o1)) return;
  // No value yet: the dash, in text fonts only.
  CHECK_STR(label_text(big->o1), DASH);
  CHECK_STR(label_text(small->o1), DASH);
  CHECK(font_of(big->o1) == g_fonts.text[6]);    // 34: fit is 64, 48 would be digits only
  CHECK(font_of(small->o1) == g_fonts.text[4]);  // 20: fit is 24
  CHECK(font_of(big->o1) != g_fonts.digits[0] && font_of(small->o1) != g_fonts.digits[0]);
  // A value: digits at 48 where it fits.
  CHECK_STR(ha.set("{\"h\":\"" + h + "\",\"v\":[[1,21.5],[2,21.5]]}"), "");
  CHECK_STR(label_text(big->o1), "21.5");
  CHECK(font_of(big->o1) == g_fonts.digits[0]);
  CHECK(font_of(small->o1) == g_fonts.text[4]);
  // A negative value uses the ASCII hyphen, which the digits font holds.
  CHECK_STR(ha.set("{\"h\":\"" + h + "\",\"v\":[[1,-4]]}"), "");
  CHECK_STR(label_text(big->o1), "-4");
  CHECK(font_of(big->o1) == g_fonts.digits[0]);
  // Back to no value: the dash in 34 again.
  CHECK_STR(ha.set("{\"f\":1,\"h\":\"" + h + "\",\"v\":[]}"), "");
  CHECK_STR(label_text(big->o1), DASH);
  CHECK(font_of(big->o1) == g_fonts.text[6]);
  // The rule itself, at any size.
  for (int s : {60, 100, 120, 200, 320, 800}) {
    const lv_font_t *f = D::thermostat_font(DASH, s);
    CHECK_MSG(f != g_fonts.digits[0], std::to_string(s));
    const double fit = s / 5.0;  // the dash is narrow; s / 5 always decides
    const int want = fit >= 34 ? 34 : fit >= 20 ? 20 : fit >= 18 ? 18 : 14;
    CHECK_MSG(f == D::text_font(want), std::to_string(s));
  }
}

// Contract 4.7: thermostatFit counts 0.62 em a digit, but Montserrat's digits
// are up to 0.67 em. A size whose drawn text is wider than the box steps down
// to the next one (48, 34, 20, 18, 14); only 14 px is ever cut.
TEST(thermostat_value_steps_down_when_too_wide) {
  static const char *DASH = "\xE2\x80\x93";
  Ha ha;
  ha.boot(false);
  const char *texts[] = {"4040", "4000", "4400", "4444", "8000", "40000", "40404", "21.5", "-4", "999.9", "12346", DASH};
  static const int SIZES[4] = {34, 20, 18, 14};
  int stepped = 0;
  for (int s = 40; s <= 800; s += 2) {
    const int room = D::thermostat_text_width(s);
    for (const char *t : texts) {
      const size_t n = strlen(t);
      const lv_font_t *fit = D::thermostat_fit_font(t, s);
      // The sizes in order from the one thermostatFit picks: the first that fits.
      std::vector<const lv_font_t *> order{fit};
      bool after = fit == g_fonts.digits[0];
      for (int px : SIZES) {
        if (after) order.push_back(D::text_font(px));
        if (D::text_font(px) == fit) after = true;
      }
      const lv_font_t *want = order.back();
      for (const lv_font_t *f : order)
        if (D::text_width(f, t, n) <= room) {
          want = f;
          break;
        }
      const lv_font_t *got = D::thermostat_font(t, s);
      const std::string what = std::string(t) + " in " + std::to_string(s);
      CHECK_MSG(got == want, what);
      CHECK_MSG(D::text_width(got, t, n) <= room || got == D::text_font(14), what);
      if (got != fit) stepped++;
    }
  }
  CHECK_MSG(stepped > 0, std::to_string(stepped));
  // On screen: 4040 in a 248 px gauge is 34 px and whole (48 px would cut it),
  // 21.5 keeps 48, and 40000 in a 76 px gauge is 14 px and cut.
  const std::string dl =
      "{\"nav\":[],\"pages\":[{\"title\":\"P\",\"w\":[{\"id\":1,\"r\":[100,40,1100,700],\"t\":\"card\",\"w\":["
      "{\"id\":2,\"max\":10000,\"min\":0,\"r\":[120,60,248,248],\"step\":1,\"t\":\"thermostat\",\"value\":{\"s\":1}},"
      "{\"id\":3,\"max\":35,\"min\":5,\"r\":[400,60,320,320],\"step\":0.5,\"t\":\"thermostat\",\"value\":{\"s\":2}},"
      "{\"id\":4,\"max\":50000,\"min\":0,\"r\":[760,60,76,76],\"step\":1,\"t\":\"thermostat\",\"value\":{\"s\":3}}"
      "]}]}],\"screen\":[1280,800],\"slots\":\"nnn\",\"theme\":{\"bw\":1,\"card\":1580067,\"r\":16,\"r2\":10,"
      "\"tx\":15264493,\"wall\":1053720},\"v\":1}";
  if (!CHECK(ha.show(dl))) return;
  const std::string h = tab::active_hash();
  CHECK_STR(ha.set("{\"h\":\"" + h + "\",\"v\":[[1,4040],[2,21.5],[3,40000]]}"), "");
  D::Widget *a = widget(2), *b = widget(3), *c = widget(4);
  if (!CHECK(a && b && c && a->o1 && b->o1 && c->o1)) return;
  CHECK(D::thermostat_fit_font("4040", 248) == g_fonts.digits[0]);
  CHECK(font_of(a->o1) == g_fonts.text[6]);
  CHECK_STR(label_text(a->o1), "4040");
  CHECK(!a->cut);
  CHECK(font_of(b->o1) == g_fonts.digits[0]);
  CHECK_STR(label_text(b->o1), "21.5");
  CHECK(!b->cut);
  CHECK(font_of(c->o1) == g_fonts.text[0]);
  CHECK(c->cut);
  shot("thermostat_steps_down");
}

// Reading 2: tab_layout seq 0 checks its header before it allocates, in the
// order too_big, order (hash not 16 lower-case hex, total < 1), size (size < 1,
// or more data than size), memory. A seq 0 call always drops the unfinished
// transfer and any build in progress first, even when it then answers an error.
TEST(reading_seq0_header) {
  const std::string all = layout_bytes("layout_all_cards.dl.json");
  const std::string hash = sha_hex(all).substr(0, 16);
  const std::string upper = "4384F665E4DD9F62";
  const int size = (int) all.size();
  struct Case {
    const char *name;
    std::string hash;
    int total, size;
    size_t data;
    bool alloc_fails;
    const char *want;
  };
  const Case cases[] = {
      {"too_big wins over a bad hash", "xyz", 0, 262145, 10, false, "too_big"},
      {"too_big wins over no memory", hash, 3, 262145, 10, true, "too_big"},
      {"hash too short", hash.substr(0, 15), 3, size, 3000, false, "order"},
      {"hash in upper case", upper, 3, size, 3000, false, "order"},
      {"hash not hex", "zzzzzzzzzzzzzzzz", 3, size, 3000, false, "order"},
      {"total 0", hash, 0, size, 3000, false, "order"},
      {"total -1", hash, -1, size, 3000, false, "order"},
      {"order wins over size", "xyz", 3, 0, 3000, false, "order"},
      {"size 0", hash, 3, 0, 0, false, "size"},
      {"size -5", hash, 3, -5, 0, false, "size"},
      {"more data than size", hash, 1, 10, 11, false, "size"},
      {"size wins over no memory", hash, 1, 10, 11, true, "size"},
      {"no memory", hash, 3, size, 3000, true, "memory"},
  };
  Ha ha;
  for (const Case &c : cases) {
    g_test = c.name;
    ha.boot(false);
    const uint64_t allocs = fake::st.allocs;
    fake::st.alloc_fails = c.alloc_fails;
    CHECK_STR(tab::on_layout(c.hash.data(), c.hash.size(), 0, c.total, c.size, all.data(), c.data), c.want);
    fake::st.alloc_fails = false;
    CHECK(fake::st.allocs == allocs);  // a refused header allocates nothing
    CHECK(fake::st.live.empty());
    CHECK_STR(tab::on_commit(c.hash.data(), c.hash.size(), "", 0), "incomplete");
  }
  g_test = "reading_seq0_header";
  // A good header still works after a refused one.
  ha.boot(false);
  CHECK_STR(tab::on_layout(upper.data(), 16, 0, 1, size, all.data(), all.size()), "order");
  CHECK(ha.show(all));
  // A bad seq 0 drops the build in progress; the layout on screen stays.
  const std::string small = layout_bytes("layout_small.dl.json");
  CHECK_STR(ha.send(small), "");
  CHECK_STR(ha.commit("a9326c033e45f2f4"), "");
  CHECK_STR(tab::staged_hash(), "a9326c033e45f2f4");
  CHECK_STR(tab::on_layout("bad", 3, 0, 1, 10, "0123456789", 10), "order");
  CHECK_STR(tab::staged_hash(), "");
  CHECK(!tab::building());
  CHECK_STR(tab::active_hash(), hash);
  hh::advance(200);
  CHECK_STR(tab::active_hash(), hash);
  char out[768];
  tj::Value v;
  CHECK(tj::parse(out, tab::on_status(out, sizeof out), &v));
  CHECK_STR(v["build"].s, "");  // dropped, not failed
  CHECK(fake::st.live.size() == 2 && !fake::st.bad_release);
}

// Reading 3: a repeated commit of the hash that is already staged (HA lost
// the first answer and asks again, with a new auth) answers success and
// changes nothing: no new allocation, the same build carries on.
TEST(reading_repeated_commit) {
  const std::string small = layout_bytes("layout_small.dl.json");
  const std::string all = layout_bytes("layout_all_cards.dl.json");
  const std::string hs = "a9326c033e45f2f4", ha_all = "4384f665e4dd9f62";
  Ha ha;
  ha.boot(true);
  CHECK(ha.hello());
  CHECK(ha.show(small));
  CHECK_STR(ha.send(all), "");
  CHECK_STR(ha.commit(ha_all), "");
  D::Layout *staged = D::staged_layout();
  CHECK(staged && tab::building());
  const uint64_t allocs = fake::st.allocs;
  const uint8_t gen = staged ? staged->gen : 0;
  CHECK_STR(ha.commit(ha_all), "");  // the retry
  CHECK_STR(ha.commit(ha_all), "");  // and once more
  CHECK(fake::st.allocs == allocs);
  CHECK(D::staged_layout() == staged && staged && staged->gen == gen);
  CHECK_STR(tab::staged_hash(), ha_all);
  CHECK_STR(tab::active_hash(), hs);
  // A replayed auth is still refused.
  const std::string old = call_auth(ha.secret, ha.nonce, ha.n, "tab_commit", ha_all);
  CHECK_STR(tab::on_commit(ha_all.data(), 16, old.data(), old.size()), "auth");
  // The build ends once, with the layout on screen; a commit then is the
  // "already on screen" case.
  CHECK(advance_until([&] { return tab::active_hash() == ha_all; }, 3000));
  CHECK_STR(ha.commit(ha_all), "");
  hh::advance(200);
  CHECK(fake::st.live.size() == 2 && !fake::st.bad_release);
  // After a build that failed, the staged layout is gone: a repeated commit
  // has nothing to refer to and answers incomplete.
  CHECK_STR(ha.send(small), "");
  CHECK_STR(ha.commit(hs), "");
  fake::st.largest = 1000000;
  fake::st.free = 1000000;
  hh::advance(200);
  fake::st.largest = 16000000;
  fake::st.free = 28000000;
  char out[768];
  tj::Value v;
  CHECK(tj::parse(out, tab::on_status(out, sizeof out), &v));
  CHECK_STR(v["build"].s, "memory");
  CHECK_STR(ha.commit(hs), "incomplete");
  CHECK_STR(tab::active_hash(), ha_all);
}

static void same_shape(const tj::Value &got, const tj::Value &want, const std::string &path) {
  CHECK_MSG(got.t == want.t, path);
  if (got.t != want.t) return;
  if (want.t == tj::Value::OBJ) {
    CHECK_MSG(got.o.size() == want.o.size(), path + " key count");
    for (size_t i = 0; i < want.o.size() && i < got.o.size(); i++) {
      CHECK_MSG(got.o[i].first == want.o[i].first, path + "." + want.o[i].first);
      same_shape(got.o[i].second, want.o[i].second, path + "." + want.o[i].first);
    }
  }
}

// The answer has the shape and the values of the first case of
// hello_v2.json (engine 0.2.0, proto [1, 2]: the host build sets
// TAB_PROTO_HIGH 2), and its caps are contract.json's engine_caps
// (contract 3, cards spec A.5).
static void same_value(const tj::Value &got, const tj::Value &want, const std::string &path) {
  CHECK_MSG(got.t == want.t && got.n == want.n && got.s == want.s && got.size() == want.size(), path);
  if (got.t != want.t) return;
  if (want.t == tj::Value::ARR)
    for (size_t i = 0; i < want.a.size() && i < got.a.size(); i++)
      same_value(got.a[i], want.a[i], path + "[" + std::to_string(i) + "]");
  if (want.t == tj::Value::OBJ)
    for (size_t i = 0; i < want.o.size() && i < got.o.size(); i++)
      same_value(got.o[i].second, want.o[i].second, path + "." + want.o[i].first);
}

TEST(hello_fixture) {
  const tj::Value f = fixture("hello_v2.json");
  const tj::Value &want = f["cases"][0]["response"];  // speaks_1_and_2
  CHECK_STR(f["cases"][0]["name"].s, "speaks_1_and_2");
  Ha ha;
  // Paired with the fixture secret: the fixture's auth starts a session, any other does not.
  ha.boot(true);
  char out[768];
  const std::string nonce = f["call"]["nonce"].s, auth = f["call"]["auth"].s;
  size_t n = tab::on_hello(nonce.data(), nonce.size(), auth.data(), auth.size(), out, sizeof out);
  tj::Value v;
  CHECK(n > 0 && tj::parse(out, n, &v));
  same_shape(v, want, "hello");
  CHECK(v["session"].n == want["session"].n && v["enc"].n == want["enc"].n && v["boot"].n == want["boot"].n);
  CHECK_STR(v["kid"].s, want["kid"].s);
  CHECK_STR(v["engine"].s, want["engine"].s);
  same_value(v["proto"], want["proto"], "proto");
  CHECK_STR(v["layout"].s, "");  // nothing on screen here
  same_value(v["caps"], want["caps"], "caps");
  // The caps are those of the newest engine, both ways round: every cap the
  // engine answers is in engine_caps with the same value, and every one of
  // engine_caps is answered.
  const tj::Value contract = tj::load(CDIR + "/contract.json");
  const tj::Value &caps = contract["engine_caps"];
  CHECK(v["caps"].t == tj::Value::OBJ && v["caps"].size() == caps.size());
  for (auto &kv : v["caps"].o) {
    if (!CHECK_MSG(caps.has(kv.first.c_str()), kv.first + " is not in engine_caps")) continue;
    same_value(kv.second, caps[kv.first.c_str()], "engine_caps." + kv.first);
  }
  for (auto &kv : caps.o) CHECK_MSG(v["caps"].has(kv.first.c_str()), kv.first + " is not answered");
  // What a missing cap means to HA (caps_defaults) never claims more than
  // the engine has: every name there is answered.
  for (auto &kv : contract["caps_defaults"].o)
    CHECK_MSG(v["caps"].has(kv.first.c_str()), kv.first + " of caps_defaults is not answered");
  CHECK(D::g.session);
  // auth.json "hello": the good auth starts a session, once per boot; the
  // same nonce again starts none (contract 3), and the bad auth none either.
  const tj::Value ah = fixture("auth.json")["hello"];
  CHECK_STR(ah["nonce"].s, nonce);
  CHECK_STR(ah["auth"].s, auth);
  CHECK(ah["expect_session"].n == 1 && ah["repeat_expect_session"].n == 0);
  n = tab::on_hello(nonce.data(), nonce.size(), ah["auth"].s.data(), ah["auth"].s.size(), out, sizeof out);
  CHECK(n > 0 && tj::parse(out, n, &v) && v["session"].n == ah["repeat_expect_session"].n);
  shutdown();  // a reboot forgets the nonces: the same hello works again
  start();
  n = tab::on_hello(nonce.data(), nonce.size(), ah["auth"].s.data(), ah["auth"].s.size(), out, sizeof out);
  CHECK(n > 0 && tj::parse(out, n, &v) && v["session"].n == ah["expect_session"].n);
  const std::string bad = ah["bad_auth"].s;
  n = tab::on_hello(nonce.data(), nonce.size(), bad.data(), bad.size(), out, sizeof out);
  CHECK(n > 0 && tj::parse(out, n, &v) && v["session"].n == ah["bad_expect_session"].n);
  for (const std::string &a : {bad, std::string(""), std::string("1.63cb060e0ce562e88abfe4982d1bc6d8"),
                               std::string("00.63cb060e0ce562e88abfe4982d1bc6d8"), auth + "0"}) {
    n = tab::on_hello(nonce.data(), nonce.size(), a.data(), a.size(), out, sizeof out);
    CHECK(n > 0 && tj::parse(out, n, &v) && v["session"].n == 0);
  }
  // A refused hello keeps the old session.
  CHECK(D::g.session && memcmp(D::g.nonce, nonce.data(), 32) == 0);
  // A nonce that is not 32 lower-case hex never starts a session.
  ha.boot(false);
  n = tab::on_hello("ABCDEF", 6, "", 0, out, sizeof out);
  CHECK(n > 0 && tj::parse(out, n, &v) && v["session"].n == 0 && v["kid"].s.empty());
  // Not paired: any auth starts one.
  n = tab::on_hello(nonce.data(), nonce.size(), "zz", 2, out, sizeof out);
  CHECK(n > 0 && tj::parse(out, n, &v) && v["session"].n == 1);
  // No API key: enc 0.
  fake::st.have_key = false;
  n = tab::on_hello(nonce.data(), nonce.size(), "", 0, out, sizeof out);
  CHECK(n > 0 && tj::parse(out, n, &v) && v["enc"].n == 0);
  // Too small a buffer: 0 and an empty string.
  CHECK(tab::on_hello(nonce.data(), nonce.size(), "", 0, out, 40) == 0 && out[0] == 0);
  CHECK(n < 768);
}

TEST(status_fixture) {
  const tj::Value f = fixture("status.json");
  const tj::Value &want = f["cases"][0]["response"];
  Ha ha;
  ha.boot(false);
  char out[768];
  size_t n = tab::on_status(out, sizeof out);
  tj::Value v;
  CHECK(n > 0 && tj::parse(out, n, &v));
  same_shape(v, want, "status");
  CHECK(v["layout"].s.empty() && v["staged"].s.empty() && v["build"].s.empty());
  CHECK(v["mem"]["block"].n == 16000000 && v["mem"]["int"].n == 61000 && v["mem"]["psram"].n == 28000000);
  // While building: staged; afterwards: layout.
  const std::string small = layout_bytes("layout_small.dl.json");
  CHECK_STR(ha.send(small), "");
  CHECK_STR(ha.commit("a9326c033e45f2f4"), "");
  n = tab::on_status(out, sizeof out);
  CHECK(tj::parse(out, n, &v) && v["staged"].s == "a9326c033e45f2f4" && v["layout"].s.empty());
  CHECK(advance_until([] { return std::string(tab::active_hash()) == "a9326c033e45f2f4"; }, 2000));
  n = tab::on_status(out, sizeof out);
  CHECK(tj::parse(out, n, &v) && v["staged"].s.empty() && v["layout"].s == "a9326c033e45f2f4" && v["build"].s.empty());
  same_shape(v, want, "status");
  CHECK(tab::on_status(out, 20) == 0);
}

TEST(auth_fixture) {
  const tj::Value f = fixture("auth.json");
  const std::string small = layout_bytes("layout_small.dl.json");
  Ha ha;
  CHECK(f["paired"].size() == 14 && f["unpaired"].size() == 2);
  // Paired: feed every case in order.
  ha.boot(true);
  CHECK(ha.hello());
  CHECK_STR(ha.send(small), "");
  for (const tj::Value &c : f["paired"].a) {
    g_test = c["name"].s.c_str();
    const std::string &a = c["auth"].s, &body = c["body"].s, &action = c["action"].s;
    const std::string want = c["expect"].s == "ok" ? "" : c["expect"].s;
    const uint32_t before = D::g.last_n;
    if (action == "tab_commit") {
      CHECK_STR(tab::on_commit(body.data(), body.size(), a.data(), a.size()), want);
    } else if (action == "tab_set") {
      CHECK_STR(tab::on_set(body.data(), body.size(), a.data(), a.size()), want);
    } else if (action == "tab_clear") {
      CHECK_STR(tab::on_clear(a.data(), a.size()), want);
    } else if (action == "tab_ack") {
      // The body is "n|1or0|code"; the effect shows in last_n below.
      const size_t p1 = body.find('|'), p2 = body.find('|', p1 + 1);
      const std::string code = body.substr(p2 + 1);
      tab::on_ack(atoi(body.c_str()), body[p1 + 1] == '1', code.data(), code.size(), a.data(), a.size());
    }
    if (want.empty())
      CHECK(D::g.last_n == (uint32_t) atoi(a.c_str()));
    else
      CHECK(D::g.last_n == before);  // a refused call changes nothing
  }
  g_test = "auth_fixture";
  CHECK(D::g.last_n == 10);
  CHECK_STR(tab::active_hash(), "");  // the last case cleared the layout
  // Paired without a session: signed calls are refused even with a valid signature.
  ha.boot(true);
  const std::string a1 = call_auth(ha.secret, NONCE, 1, "tab_clear", "");
  CHECK_STR(tab::on_clear(a1.data(), a1.size()), "auth");
  // tab_layout and tab_status stay open.
  CHECK_STR(ha.send(small), "");
  char out[768];
  CHECK(tab::on_status(out, sizeof out) > 0);
  // Unpaired: any auth.
  ha.boot(false);
  CHECK_STR(ha.send(small), "");
  for (const tj::Value &c : f["unpaired"].a) {
    const std::string &a = c["auth"].s, &body = c["body"].s;
    if (c["action"].s == "tab_commit")
      CHECK_STR(tab::on_commit(body.data(), body.size(), a.data(), a.size()), "");
    else
      CHECK_STR(tab::on_clear(a.data(), a.size()), "");
  }
}

TEST(pair_fixture) {
  const tj::Value f = fixture("pair.json");
  CHECK(f["cases"].size() == 4);
  Ha ha;
  for (const tj::Value &c : f["cases"].a) {
    g_test = c["name"].s.c_str();
    ha.boot(false);
    if (c.has("tablet_psk_hex")) {
      if (c["tablet_psk_hex"].is_null())
        fake::st.have_key = false;
      else
        unhex(c["tablet_psk_hex"].s, fake::st.key);
    }
    const tj::Value &k = c["call"];
    const char *r = tab::on_pair(k["nonce"].s.data(), k["nonce"].s.size(), k["wrapped"].s.data(), k["wrapped"].s.size(),
                                 k["kid"].s.data(), k["kid"].s.size());
    const std::string want = c["expect"].s == "ok" ? "" : c["expect"].s;
    CHECK_STR(r, want);
    if (want.empty()) {
      CHECK(fake::st.secret_saves == 1);
      CHECK_STR(hex(fake::st.secret, 32), c["stored_secret_hex"].s);
      CHECK(D::g.paired && !D::g.session);
      CHECK_STR(D::g.kid, k["kid"].s);
      // The new secret signs the next hello.
      CHECK(ha.hello() == false);  // ha is still "unpaired" and sends no auth
      ha.paired = true;
      CHECK(ha.hello());
    } else {
      CHECK(fake::st.secret_saves == 0 && !D::g.paired);
    }
  }
  g_test = "pair_fixture";
  // Pairing ends the session.
  ha.boot(true);
  CHECK(ha.hello() && D::g.session);
  const tj::Value &k = f["cases"][0]["call"];
  CHECK_STR(tab::on_pair(k["nonce"].s.data(), 32, k["wrapped"].s.data(), 64, k["kid"].s.data(), 16), "");
  CHECK(!D::g.session);
  // The session nonce is never accepted as a pairing nonce.
  CHECK(ha.hello());
  CHECK_STR(tab::on_pair(ha.nonce.data(), 32, k["wrapped"].s.data(), 64, k["kid"].s.data(), 16), "bad_key");
}

// Contract 3: a nonce starts at most one session per boot. The tablet keeps
// the last 16 session nonces; a hello that started no session is not kept.
TEST(hello_nonce_ring) {
  Ha ha;
  ha.boot(true);
  char out[768];
  tj::Value v;
  auto nonce_of = [](int i) {
    char b[33];
    snprintf(b, sizeof b, "%016x%016x", 0xabcu, (unsigned) i);
    return std::string(b);
  };
  auto hello_with = [&](const std::string &nonce, bool signed_ = true) {
    const std::string a = signed_ ? call_auth(ha.secret, nonce, 0, "tab_hello", "") : "";
    const size_t n = tab::on_hello(nonce.data(), nonce.size(), a.data(), a.size(), out, sizeof out);
    return n > 0 && tj::parse(out, n, &v) ? (int) v["session"].n : -1;
  };
  CHECK(hello_with(nonce_of(0)) == 1);
  CHECK(hello_with(nonce_of(0)) == 0);
  CHECK(D::g.session && memcmp(D::g.nonce, nonce_of(0).data(), 32) == 0);  // the session stays
  // A recorded hello cannot bring an old session back, nor its recorded calls.
  const std::string recorded_clear = call_auth(ha.secret, nonce_of(0), 1, "tab_clear", "");
  CHECK(hello_with(nonce_of(1)) == 1);
  CHECK(hello_with(nonce_of(0)) == 0);
  CHECK(memcmp(D::g.nonce, nonce_of(1).data(), 32) == 0);
  CHECK_STR(tab::on_clear(recorded_clear.data(), recorded_clear.size()), "auth");
  // A refused hello (bad auth) does not use up its nonce.
  CHECK(hello_with(nonce_of(2), false) == 0);
  CHECK(hello_with(nonce_of(2)) == 1);
  // 16 are kept: after 16 newer sessions the oldest is forgotten (the limit
  // the contract states).
  for (int i = 3; i <= 16; i++) CHECK(hello_with(nonce_of(i)) == 1);
  CHECK(hello_with(nonce_of(1)) == 0);
  CHECK(hello_with(nonce_of(0)) == 1);
  // A reboot forgets them all.
  ha.boot(true);
  CHECK(hello_with(nonce_of(1)) == 1);
  // Not paired: the same rule, with any auth.
  ha.boot(false);
  CHECK(hello_with(nonce_of(5), false) == 1);
  CHECK(hello_with(nonce_of(5), false) == 0);
  CHECK(hello_with(nonce_of(6), false) == 1);
}

static int session_in_status() {
  char out[768];
  tj::Value v;
  if (!tj::parse(out, tab::on_status(out, sizeof out), &v)) return -1;
  return (int) v["session"].n;
}

// Contract 2.2 and 3: tab_status says whether a session is live on this
// connection, so HA can check a knock without resetting a session.
TEST(status_session) {
  const tj::Value f = fixture("status.json");
  Ha ha;
  ha.boot(true);
  CHECK(session_in_status() == 0);
  CHECK(ha.hello());
  CHECK(session_in_status() == 1);
  CHECK(session_in_status() == 1);  // reading it changes nothing
  // A reconnect that HA did not see: the session is gone, and says so.
  fake::st.connected = false;
  hh::advance(20);
  fake::st.connected = true;
  hh::advance(20);
  CHECK(session_in_status() == 0);
  CHECK(ha.hello());
  CHECK(session_in_status() == 1);
  // Pairing ends the session too.
  const tj::Value pair = fixture("pair.json");
  const tj::Value &k = pair["cases"][0]["call"];
  CHECK_STR(tab::on_pair(k["nonce"].s.data(), 32, k["wrapped"].s.data(), 64, k["kid"].s.data(), 16), "");
  CHECK(session_in_status() == 0);
  // The knock cases read responses of this shape.
  CHECK(f["knock"].size() == 4);
  char out[768];
  tj::Value v;
  CHECK(tj::parse(out, tab::on_status(out, sizeof out), &v));
  for (const tj::Value &c : f["knock"].a)
    if (c["status"].t == tj::Value::OBJ) same_shape(v, c["status"], "knock " + c["name"].s);
}

// Contract 6.1 (auth.json "no_api_key"): a tablet that holds a secret but has
// no API key ignores the secret until a key is back.
TEST(no_api_key_fixture) {
  const tj::Value nk = fixture("auth.json")["no_api_key"];
  const std::string small = layout_bytes("layout_small.dl.json");
  const std::string all = layout_bytes("layout_all_cards.dl.json");
  Ha ha;
  ha.boot(true);
  fake::st.have_key = false;
  ha.paired = false;  // HA signs nothing for this tablet
  char out[768];
  tj::Value v;
  const tj::Value &h = nk["hello"];
  size_t n = tab::on_hello(h["nonce"].s.data(), h["nonce"].s.size(), h["auth"].s.data(), h["auth"].s.size(), out,
                           sizeof out);
  ha.hellos = 1;
  CHECK(n > 0 && tj::parse(out, n, &v));
  CHECK(v["session"].n == h["expect_session"].n && v["enc"].n == h["expect_enc"].n);
  CHECK_STR(v["kid"].s, h["expect_kid"].s);
  // Unsigned calls work.
  CHECK_STR(ha.send(small), "");
  CHECK(nk["calls"].size() == 2);
  for (const tj::Value &c : nk["calls"].a) {
    g_test = c["name"].s.c_str();
    const std::string &a = c["auth"].s, &body = c["body"].s;
    const std::string want = c["expect"].s == "ok" ? "" : c["expect"].s;
    if (c["action"].s == "tab_commit")
      CHECK_STR(tab::on_commit(body.data(), body.size(), a.data(), a.size()), want);
    else
      CHECK_STR(tab::on_clear(a.data(), a.size()), want);
  }
  g_test = "no_api_key_fixture";
  // No input is sent: a tap only shows the strip.
  CHECK(ha.show(all));
  CHECK_STR(ha.set(state_payload(0)), "");
  hh::advance(20);
  fake::st.events.clear();
  D::Widget *sw = widget(3);
  if (CHECK(sw)) hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(fake::events("tab1_input").empty() && !hidden(D::g.strip));
  // Pairing asks for encryption first.
  const tj::Value pair = fixture("pair.json");
  const tj::Value &k = pair["cases"][0]["call"];
  CHECK_STR(tab::on_pair(k["nonce"].s.data(), 32, k["wrapped"].s.data(), 64, k["kid"].s.data(), 16),
            "needs_encryption");
  // The key is back: the stored secret counts again.
  fake::st.have_key = true;
  CHECK_STR(tab::on_clear("", 0), "auth");
  ha.paired = true;
  CHECK(ha.hello());
  n = tab::on_hello(NONCE, 32, "", 0, out, sizeof out);
  CHECK(n > 0 && tj::parse(out, n, &v) && v["enc"].n == 1);
  CHECK_STR(v["kid"].s, "d9ccb8a994337e34");
  hh::advance(3000);
  fake::st.events.clear();
  if (sw) hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(fake::events("tab1_input").size() == 1);
}

// ================================================================ layouts and drawing

static int text_px(const D::Widget &w) { return w.size == 0 ? 14 : w.size == 2 ? 34 : 18; }

static std::string g_cut_report;

static void check_layout_geometry(const char *label) {
  D::Layout *l = active();
  if (!CHECK(l && l->model)) return;
  lv_obj_update_layout(lv_screen_active());
  D::Model &m = *l->model;
  // Side bar (contract 4.7).
  CHECK(obj_at(l->bar, 0, 0, 96, 800));
  CHECK(lv_color_to_u32(lv_obj_get_style_bg_color(l->bar, LV_PART_MAIN)) == (0xFF000000u | m.theme.c[D::C_CARD]));
  for (int i = 0; i < m.nnav; i++) {
    CHECK_MSG(obj_at(m.nav[i].obj, 16, 16 + 72 * i, 64, 64), rect_text(m.nav[i].obj));
    CHECK(font_of(m.nav[i].icon_obj) == g_fonts.icon[1]);
    CHECK(lv_obj_get_style_radius(m.nav[i].obj, LV_PART_MAIN) == m.theme.r2);
  }
  // Every widget's object at its rectangle, with the contract's font sizes.
  int checked = 0;
  for (uint32_t i = 0; i < m.nw; i++) {
    D::Widget &w = m.w[i];
    char id[48];
    snprintf(id, sizeof id, "%s widget %u", label, (unsigned) w.id);
    if (!CHECK_MSG(w.obj != nullptr, id)) continue;
    CHECK_MSG(obj_at(w.obj, w.r[0], w.r[1], w.r[2], w.r[3]), std::string(id) + " at " + rect_text(w.obj));
    checked++;
    switch (w.kind) {
      case D::W_LABEL: CHECK_MSG(font_of(w.obj) == D::text_font(text_px(w)), id); break;
      case D::W_HEADING:
        CHECK_MSG(font_of(w.o2) == g_fonts.text[4], id);
        if (w.icon) CHECK_MSG(font_of(w.o1) == g_fonts.icon[0], id);
        CHECK_MSG(obj_at(w.o2, w.r[0] + (w.icon ? 32 : 0), w.r[1], w.r[2] - (w.icon ? 32 : 0), w.r[3]), id);
        break;
      case D::W_CARD:
        if (w.has_hr) {
          const int tx = w.icon ? 32 : 0;
          CHECK_MSG(obj_at(w.o2, w.hr[0] + tx, w.hr[1], w.hr[2] - tx, w.hr[3]), std::string(id) + " header " + rect_text(w.o2));
          CHECK_MSG(font_of(w.o2) == g_fonts.text[2], id);
          if (w.icon) CHECK_MSG(font_of(w.o1) == g_fonts.icon[0], id);
        }
        break;
      case D::W_BUTTON:
        if (w.o2) CHECK_MSG(font_of(w.o2) == g_fonts.text[1], id);
        if (w.o1) CHECK_MSG(font_of(w.o1) == g_fonts.icon[0], id);
        break;
      case D::W_SWITCH: {
        const int tw = w.r[2] < 64 ? w.r[2] : 64, th = w.r[3] < 36 ? w.r[3] : 36;
        CHECK_MSG(obj_at(w.o1, w.r[0], w.r[1] + (w.r[3] - th) / 2, tw, th), id);
        break;
      }
      case D::W_SLIDER: CHECK_MSG(obj_at(w.o1, w.r[0], w.r[1] + (w.r[3] - 14) / 2, w.r[2], 14), id); break;
      case D::W_THERMO:
        if (w.change) {
          CHECK_MSG(obj_at(w.o2, w.r[0], w.r[1] + w.r[3] - 36, 36, 36), id);
          CHECK_MSG(obj_at(w.o3, w.r[0] + w.r[2] - 36, w.r[1] + w.r[3] - 36, 36, 36), id);
          CHECK_MSG(font_of(lv_obj_get_child(w.o2, 0)) == g_fonts.icon[0], id);
        }
        break;
      case D::W_IMAGE: CHECK_MSG(font_of(w.o1) == D::icon_font(D::image_icon_px(w.r[2], w.r[3])), id); break;
      default: break;
    }
  }
  CHECK(checked == (int) m.nw);
}

static void report_cuts(const char *label) {
  D::Layout *l = active();
  if (!l) return;
  D::Model &m = *l->model;
  for (uint32_t i = 0; i < m.nw; i++) {
    D::Widget &w = m.w[i];
    if (!w.cut) continue;
    const char *full = "";
    D::bound_text(m, w.text, &full);
    lv_obj_t *o = w.kind == D::W_LABEL ? w.obj : w.kind == D::W_THERMO ? w.o1 : w.o2;
    static const char *const KINDS[] = {"", "heading", "card", "label", "button", "switch", "slider", "thermostat",
                                        "chart", "image", "choice"};
    char line[600];
    snprintf(line, sizeof line, "%s: widget %u (%s, box %d px): \"%s\" shows as \"%s\"\n", label, (unsigned) w.id,
             KINDS[w.kind], o ? (int) lv_obj_get_content_width(o) : 0, full, label_text(o).c_str());
    g_cut_report += line;
  }
}

static void shots_of_pages(const char *label) {
  const int n = tab::page_count();
  CHECK(n >= 1);
  D::Layout *l = active();
  const uint16_t wall = hh::rgb565(l->model->theme.c[D::C_WALL]);
  for (int p = 0; p < n; p++) {
    tab::go_to(p);
    hh::advance(40);
    CHECK(tab::current_page() == p);
    shot(std::string(label) + "_page" + std::to_string(p));
    // Not blank: the page area holds cards and text, not only the wall colour.
    CHECK_MSG(share_not(wall, 96) > 0.05, label);
    CHECK_MSG(screen_colours(96) >= 6, label);
    // The side bar: card colour, the entry of this page in onbg.
    CHECK(hh::pixel(4, 700) == hh::rgb565(l->model->theme.c[D::C_CARD]));
    for (int i = 0; i < l->model->nnav; i++)
      if (l->model->nav[i].page == p) CHECK(hh::pixel(20, 16 + 72 * i + 32) == hh::rgb565(l->model->theme.c[D::C_ONBG]));
  }
  tab::go_to(0);
  hh::advance(20);
}

TEST(layouts_draw) {
  Ha ha;
  ha.boot(false);
  // Small layout, with values.
  CHECK(ha.show(layout_bytes("layout_small.dl.json")));
  check_layout_geometry("layout_small");
  CHECK_STR(ha.set("{\"f\":1,\"h\":\"a9326c033e45f2f4\",\"v\":[[1,\"Ceiling light\"],[2,1],[3,\"80%\"],[4,"
                   "\"Temperature\"],[5,\"21.5\xC2\xB0\"]]}"),
            "");
  hh::advance(40);
  shots_of_pages("layout_small");
  report_cuts("layout_small");
  // All cards: first without values (the unavailable look), then with them.
  CHECK(ha.show(layout_bytes("layout_all_cards.dl.json")));
  check_layout_geometry("layout_all_cards");
  hh::advance(40);
  shot("layout_all_cards_page0_no_values");
  // The dimmed slider still draws the whole knob, also the half left of the track.
  {
    D::Widget *s10 = widget(10);
    const int ky = centre_y(s10->r);
    CHECK(hh::pixel(s10->r[0] - 8, ky) != hh::pixel(s10->r[0] - 13, ky));
  }
  // Faded without a value; it stays clickable, so a tap can say why nothing
  // is sent (faded_controls_say_why).
  D::Widget *sw = widget(3);
  CHECK(sw && lv_obj_get_style_opa(sw->obj, LV_PART_MAIN) == LV_OPA_40 && sw->faded &&
        lv_obj_has_flag(sw->obj, LV_OBJ_FLAG_CLICKABLE));
  CHECK_STR(ha.set(state_payload(0)), "");
  hh::advance(40);
  CHECK(lv_obj_get_style_opa(sw->obj, LV_PART_MAIN) == LV_OPA_COVER && !sw->faded &&
        lv_obj_has_flag(sw->obj, LV_OBJ_FLAG_CLICKABLE));
  check_layout_geometry("layout_all_cards");
  shots_of_pages("layout_all_cards");
  report_cuts("layout_all_cards");
  // The down-levelled layout.
  CHECK(ha.show(layout_bytes("layout_all_cards.nochart.dl.json")));
  check_layout_geometry("layout_all_cards.nochart");
  const tj::Value meta = fixture("layout_all_cards.nochart.meta.json");
  CHECK(tab::slot_count() == (int) meta["slots"].size());
  // Values in the nochart slot numbering, from the plan (device slot to source slot).
  {
    const tj::Value wire = fixture("state_stream.json")["wire_values_by_device_slot"];
    const tj::Value full = fixture("layout_all_cards.meta.json");
    std::map<int, int> src_to_full;
    for (auto &s : full["slots"].a) src_to_full[s["src"].i()] = s["d"].i();
    std::string data = "{\"f\":1,\"h\":\"" + meta["hash"].s + "\",\"v\":[";
    bool first = true;
    for (auto &s : meta["slots"].a) {
      const std::string key = std::to_string(src_to_full[s["src"].i()]);
      const tj::Value &v = wire[key.c_str()];
      std::string val;
      if (v.t == tj::Value::STR) {
        val = "\"";
        for (char c : v.s) {
          if (c == '"' || c == '\\') val += '\\';
          val += c;
        }
        val += "\"";
      } else if (v.t == tj::Value::ARR) {
        val = "[";
        for (size_t i = 0; i < v.a.size(); i++) val += (i ? "," : "") + v.a[i].s;
        val += "]";
      } else {
        val = v.t == tj::Value::NUL ? "null" : v.s;
      }
      data += std::string(first ? "" : ",") + "[" + std::to_string(s["d"].i()) + "," + val + "]";
      first = false;
    }
    data += "]}";
    CHECK_STR(ha.set(data), "");
  }
  hh::advance(40);
  shots_of_pages("layout_all_cards_nochart");
  report_cuts("layout_all_cards.nochart");
  // The report of cut texts (design 7.3).
  const std::string path = OUT + "/cut_texts.txt";
  FILE *f = fopen(path.c_str(), "w");
  if (f) {
    fputs(g_cut_report.empty() ? "No text is cut.\n" : g_cut_report.c_str(), f);
    fclose(f);
  }
  printf("  cut texts (%s):\n%s", path.c_str(), g_cut_report.empty() ? "    none\n" : g_cut_report.c_str());
}

TEST(engine_screens) {
  Ha ha;
  ha.boot(true);
  fake::st.network = true;  // on Wi-Fi, turned on, Home Assistant not there yet
  fake::st.connected = false;
  hh::advance(D::HINT_POLL_MS + 40);
  // The waiting screen: name, headline, next step (all states: waiting_screen_states).
  CHECK(!hidden(D::g.waiting));
  bool found_name = false, found_wait = false, found_hint = false;
  for (uint32_t i = 0; i < lv_obj_get_child_count(D::g.waiting); i++) {
    const std::string t = label_text(lv_obj_get_child(D::g.waiting, (int32_t) i));
    found_name |= t == fake::NAME;
    found_wait |= t == T_WAITING;
    found_hint |= t == WAIT_RUNNING;
  }
  CHECK(found_name && found_wait && found_hint);
  // Whole (set_text cuts with "…"), without Home Assistant jargon.
  CHECK_STR(label_text(D::g.headline), T_WAITING);
  CHECK_STR(label_text(D::g.hint), WAIT_RUNNING);
  CHECK(label_text(D::g.hint).find("integration") == std::string::npos);
  shot("waiting_screen");
  fake::st.connected = true;
  // Question box and message strip on the all-cards layout.
  CHECK(ha.hello());
  CHECK(ha.show(layout_bytes("layout_all_cards.dl.json")));
  CHECK_STR(ha.set(state_payload(0)), "");
  CHECK(hidden(D::g.waiting));
  tab::go_to(2);
  hh::advance(20);
  D::Widget *lock = widget(53);
  hh::tap(centre_x(lock->r), centre_y(lock->r));
  CHECK(!hidden(D::g.question));
  CHECK_STR(label_text(D::g.q_text), "Lock Front door?");
  shot("question_box");
  hh::tap(504, 456);  // No
  CHECK(hidden(D::g.question));
  fake::st.network = true;  // Home Assistant away, Wi-Fi up
  fake::st.connected = false;
  hh::advance(20);
  hh::tap(centre_x(lock->r), centre_y(lock->r));
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), "Home Assistant is not connected");
  // The "not connected" mark after 10 s without HA.
  CHECK(hidden(D::g.mark));
  hh::advance(10100);
  CHECK(!hidden(D::g.mark));
  CHECK(hidden(D::g.strip));  // the strip went after 2.5 s
  shot("not_connected_mark");
  fake::st.connected = true;
  hh::advance(20);
  CHECK(hidden(D::g.mark));
}

TEST(text_measure) {
  // The engine's width measure equals LVGL's own, so "…" cuts exactly at the box.
  const char *texts[] = {"Ceiling light", "Climate \xC2\xB7 W\xC3\xA4rme", "21.5\xC2\xB0 \xC2\xB7 46% \xC2\xB7 2 lights on",
                         "AVAWAy To", "Soft focus \xC2\xB7 Mara Lind", "x"};
  for (const lv_font_t *f : {g_fonts.text[0], g_fonts.text[3], g_fonts.text[6]}) {
    for (const char *t : texts) {
      lv_point_t p;
      lv_text_get_size(&p, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      CHECK_MSG(D::text_width(f, t, strlen(t)) == p.x, t);
    }
  }
  CHECK(D::has_glyph(g_fonts.text[3], 0x2026) && D::has_glyph(g_fonts.text[3], 0xB7) && D::has_glyph(g_fonts.text[3], 0xB0));
  // An icon the font lacks draws the fallback glyph.
  CHECK(D::icon_or_fallback(g_fonts.icon[0], 0x10FFFD) == D::ICON_FALLBACK);
  CHECK(D::icon_or_fallback(g_fonts.icon[0], D::ICON_PLUS) == D::ICON_PLUS);
  // Cutting: the shown text fits and ends in "…".
  Ha ha;
  ha.boot(false);
  lv_obj_t *l = D::text_box(lv_screen_active(), 0, 0, 120, 30, g_fonts.text[3], false);
  CHECK(D::set_text(l, "A rather long room name for a small box", 120));
  const std::string s = label_text(l);
  CHECK(s.size() > 3 && s.substr(s.size() - 3) == "\xE2\x80\xA6");
  CHECK(D::text_width(g_fonts.text[3], s.c_str(), s.size()) <= 120);
  CHECK(!D::set_text(l, "Short", 120) && label_text(l) == "Short");
  lv_obj_delete(l);
}

TEST(icons_match_contract) {
  // The engine's own icons are the code points icons.json gives those names.
  const tj::Value ic = tj::load(CDIR + "/icons.json");
  const std::pair<const char *, uint32_t> mine[] = {
      {"mdi:view-dashboard", D::ICON_VIEW_DASHBOARD}, {"mdi:lan-disconnect", D::ICON_LAN_DISCONNECT},
      {"mdi:minus", D::ICON_MINUS},                   {"mdi:plus", D::ICON_PLUS},
      {"mdi:tablet-dashboard", D::ICON_TABLET},       {ic["placeholder"].s.c_str(), D::ICON_PLACEHOLDER},
      {ic["fallback"].s.c_str(), D::ICON_FALLBACK}};
  for (auto &p : mine) {
    CHECK_MSG(ic["icons"].has(p.first), p.first);
    CHECK_MSG((uint32_t) ic["icons"][p.first][0].n == p.second, p.first);
    CHECK_MSG(D::has_glyph(g_fonts.icon[0], p.second), p.first);
  }
  // Every level-1 icon is in the icon font the tests use.
  int missing = 0, level1 = 0;
  for (auto &kv : ic["icons"].o)
    if (kv.second[1].n == 1) {
      level1++;
      if (!D::has_glyph(g_fonts.icon[1], (uint32_t) kv.second[0].n)) missing++;
    }
  CHECK(level1 > 50);
  CHECK_MSG(missing == 0, std::to_string(missing) + " missing");
}

// ================================================================ state

TEST(state_stream_fixture) {
  const tj::Value f = fixture("state_stream.json");
  CHECK(f["steps"].size() == 9);
  Ha ha;
  ha.boot(false);
  CHECK(ha.show(layout_bytes("layout_all_cards.dl.json")));
  D::Layout *l = active();
  D::Model &m = *l->model;
  int step_no = 0;
  for (const tj::Value &step : f["steps"].a) {
    char name[32];
    snprintf(name, sizeof name, "state step %d", step_no++);
    g_test = name;
    for (const tj::Value &p : step["payloads"].a) CHECK_STR(ha.set(p.s), step["expect"].s == "ok" ? "" : step["expect"].s);
    hh::advance(20);
    for (auto &kv : step["after"].o) {
      const int slot = atoi(kv.first.c_str());
      const tj::Value &want = kv.second;
      const char type = tab::slot_type(slot);
      if (want.is_null()) {
        CHECK_MSG(tab::slot_null(slot), kv.first);
        continue;
      }
      CHECK_MSG(!tab::slot_null(slot), kv.first);
      switch (type) {
        case 't': {
          char buf[300];
          CHECK_MSG(tab::slot_text(slot, buf, sizeof buf) && want.s == buf, kv.first);
          break;
        }
        case 'b': {
          bool b;
          CHECK_MSG(tab::slot_bool(slot, &b) && b == (want.n != 0), kv.first);
          break;
        }
        case 'n': {
          double d;
          CHECK_MSG(tab::slot_number(slot, &d) && d == want.n, kv.first);
          break;
        }
        case 'i': {
          uint32_t ic;
          CHECK_MSG(tab::slot_icon(slot, &ic) && ic == (uint32_t) want.n, kv.first);
          break;
        }
        case 'l': {
          double list[130];
          const int n = tab::slot_list(slot, list, 130);
          bool same = n == (int) want.size();
          for (int i = 0; same && i < n; i++) same = list[i] == want[(size_t) i].n;
          CHECK_MSG(same, kv.first);
          break;
        }
        default: CHECK_MSG(false, "slot type of " + kv.first);
      }
    }
    // Labels, switches and sliders show the values.
    for (uint32_t i = 0; i < m.nw; i++) {
      D::Widget &w = m.w[i];
      char id[40];
      snprintf(id, sizeof id, "widget %u", (unsigned) w.id);
      if (w.kind == D::W_LABEL && w.text.how == D::B_SLOT) {
        const tj::Value &v = step["after"][std::to_string(w.text.slot).c_str()];
        CHECK_MSG(shows_text(w.obj, v.is_null() ? "" : v.s, w.cut), std::string(id) + " shows \"" + label_text(w.obj) + "\"");
      }
      if (w.kind == D::W_SWITCH && w.value.how == D::B_SLOT) {
        const tj::Value &v = step["after"][std::to_string(w.value.slot).c_str()];
        const int tw = w.r[2] < 64 ? w.r[2] : 64;
        if (v.is_null()) {
          CHECK_MSG(lv_obj_get_style_opa(w.obj, LV_PART_MAIN) == LV_OPA_40, id);
        } else {
          CHECK_MSG(lv_obj_get_x(w.o2) == (v.n != 0 ? tw - 32 : 4), id);
          const uint32_t track = lv_color_to_u32(lv_obj_get_style_bg_color(w.o1, LV_PART_MAIN)) & 0xFFFFFF;
          const D::Tok t = D::card_tokens(m, D::card_of(m, w));
          const bool on = D::card_is_on(m, D::card_of(m, w));
          CHECK_MSG(track == (v.n != 0 ? (on ? t.ion : t.acc) : t.card2), id);
        }
      }
      if (w.kind == D::W_SLIDER && w.value.how == D::B_SLOT) {
        const tj::Value &v = step["after"][std::to_string(w.value.slot).c_str()];
        double lo, hi, st;
        D::limits_of(m, w, &lo, &hi, &st);
        const int fill = v.is_null() ? 0 : (int) std::floor(D::fraction(v.n, lo, hi) * w.r[2] + 0.5);
        CHECK_MSG(lv_obj_get_width(w.o2) == fill || (fill == 0 && hidden(w.o2)), id);
      }
    }
  }
  g_test = "state_stream_fixture";
  // tab_set rules: bad JSON applies nothing; a wrong type or slot is skipped.
  char before[300];
  CHECK(tab::slot_text(1, before, sizeof before));
  CHECK_STR(ha.set("{\"h\":\"4384f665e4dd9f62\",\"v\":[[1,\"x\"],"), "parse");
  CHECK_STR(ha.set("[1]"), "parse");
  CHECK_STR(ha.set("{\"v\":[]}"), "parse");
  char after[300];
  CHECK(tab::slot_text(1, after, sizeof after) && std::string(before) == after);
  CHECK_STR(ha.set("{\"h\":\"4384f665e4dd9f62\",\"v\":[[1,5],[2,\"x\"],[999,1],[0,1],[1.5,1],[3,\"ok\",7],\"bad\",[11,61]]}"), "");
  CHECK(tab::slot_text(1, after, sizeof after) && std::string(before) == after);
  double num;
  CHECK(tab::slot_number(11, &num) && num == 61);
  // Text over 255 bytes is cut on a character boundary.
  std::string longtext;
  for (int i = 0; i < 200; i++) longtext += "\xC3\xBC";  // 400 bytes
  CHECK_STR(ha.set("{\"h\":\"4384f665e4dd9f62\",\"v\":[[1,\"" + longtext + "\"]]}"), "");
  CHECK(tab::slot_text(1, after, sizeof after) && strlen(after) == 254);
  // A list over 120 numbers keeps the first 120.
  std::string list = "[";
  for (int i = 0; i < 130; i++) list += (i ? "," : "") + std::to_string(i);
  list += "]";
  CHECK_STR(ha.set("{\"h\":\"4384f665e4dd9f62\",\"v\":[[22," + list + "]]}"), "");
  double lv[200];
  CHECK(tab::slot_list(22, lv, 200) == 120 && lv[119] == 119);
}

// ================================================================ input

TEST(input_events_fixture) {
  const tj::Value f = fixture("input_events.json");
  std::map<std::string, const tj::Value *> cases;
  for (const tj::Value &c : f["cases"].a) cases[c["name"].s] = &c;
  for (const char *k : {"tap_switch", "change_slider", "change_fraction", "confirmed_lock"})
    if (!CHECK_MSG(cases.count(k) == 1, k)) return;
  // Every "ok" case with a rectangle is pressed below.
  int rects = 0;
  for (const tj::Value &c : f["cases"].a) rects += c["expect"].s == "ok" && c.has("widget_rect");
  CHECK(rects == 3);
  Ha ha;
  // Pair through tab_pair with the fixture key, then greet with the fixture call.
  ha.boot(false);
  const tj::Value pair = fixture("pair.json")["cases"][0]["call"];
  CHECK_STR(tab::on_pair(pair["nonce"].s.data(), 32, pair["wrapped"].s.data(), 64, pair["kid"].s.data(), 16), "");
  ha.paired = true;
  const tj::Value hello = fixture("hello.json")["call"];
  char out[768];
  tj::Value v;
  const size_t n = tab::on_hello(hello["nonce"].s.data(), 32, hello["auth"].s.data(), hello["auth"].s.size(), out, sizeof out);
  CHECK(tj::parse(out, n, &v) && v["session"].n == 1);
  CHECK(ha.show(layout_bytes("layout_all_cards.dl.json")));
  CHECK_STR(ha.set(state_payload(0)), "");
  hh::advance(20);
  fake::st.events.clear();
  auto last_input = [] {
    auto ev = fake::events("tab1_input");
    return ev.empty() ? fake::Event{} : ev.back();
  };
  // Counter 1: the switch.
  const tj::Value &tap = (*cases["tap_switch"]);
  const tj::Value &r1 = tap["widget_rect"];
  hh::tap(r1[0].i() + r1[2].i() / 2, r1[1].i() + r1[3].i() / 2);
  CHECK(fake::events("tab1_input").size() == 1);
  CHECK_STR(last_input().d, tap["data"]["d"].s);
  CHECK_STR(last_input().s, tap["data"]["s"].s);
  // Counter 2: the slider, dragged to 42 and released.
  const tj::Value &chg = (*cases["change_slider"]);
  const tj::Value &r2 = chg["widget_rect"];
  const int y2 = r2[1].i() + r2[3].i() / 2;
  hh::drag(r2[0].i() + 80, y2, 260, y2, 200);
  CHECK(tab::current_page() == 0);  // a drag on a slider is not a swipe
  CHECK(fake::events("tab1_input").size() == 2);
  CHECK_STR(last_input().d, chg["data"]["d"].s);
  CHECK_STR(last_input().s, chg["data"]["s"].s);
  // Counter 3: a fractional value through the same payload builder (the
  // slider's step of 1 cannot make 42.5 by touch).
  const tj::Value &frac = (*cases["change_fraction"]);
  CHECK(D::send_input(*active(), *widget(10), 'c', false, true, 42.5));
  CHECK(fake::events("tab1_input").size() == 3);
  CHECK_STR(last_input().d, frac["data"]["d"].s);
  CHECK_STR(last_input().s, frac["data"]["s"].s);
  // Counter 4: the lock asks first, then sends with confirmed 1.
  const tj::Value &lock = (*cases["confirmed_lock"]);
  hh::tap(16 + 32, 16 + 72 + 32);  // side bar entry 2 shows page 2
  CHECK(tab::current_page() == 2);
  const tj::Value &r4 = lock["widget_rect"];
  hh::tap(r4[0].i() + r4[2].i() / 2, r4[1].i() + r4[3].i() / 2);
  CHECK(fake::events("tab1_input").size() == 3);  // nothing before the answer
  CHECK(!hidden(D::g.question));
  hh::tap(776, 456);  // Yes
  CHECK(hidden(D::g.question));
  CHECK(fake::events("tab1_input").size() == 4);
  CHECK_STR(last_input().d, lock["data"]["d"].s);
  CHECK_STR(last_input().s, lock["data"]["s"].s);
  // Every input event is ASCII, nine fields, at most 200 characters.
  for (auto &e : fake::events("tab1_input")) {
    CHECK(e.d.size() <= 200 && std::count(e.d.begin(), e.d.end(), '|') == 8);
    CHECK(e.has_s && tab::crypto::is_hex(e.s.data(), e.s.size(), 32));
  }
}

// ================================================================ build, swap, memory

TEST(bytes_stay_pristine) {
  Ha ha;
  ha.boot(false);
  const std::string bytes = layout_bytes("layout_all_cards.dl.json");
  CHECK(ha.show(bytes));
  D::Layout *l = active();
  for (int i = 0; i < 100; i++) {
    const std::string data = "{\"h\":\"4384f665e4dd9f62\",\"v\":[[1,\"K\\u00fcche \\\"" + std::to_string(i) +
                             "\\\"\\n\"],[2," + std::to_string(i % 2) + "],[8," + std::to_string(i) +
                             ".5],[22,[1,2,3]],[25," + std::to_string(984469 + i % 3) + "],[4,null]]}";
    CHECK(ha.set(data).empty());
    hh::advance(2);
  }
  char h[17];
  D::hash_of(l->bytes, l->size, h);
  CHECK_STR(h, "4384f665e4dd9f62");
  CHECK(memcmp(l->bytes, bytes.data(), bytes.size()) == 0 && l->bytes[l->size] == 0);
}

TEST(swap_safety) {
  Ha ha;
  ha.boot(false);
  const std::string small = layout_bytes("layout_small.dl.json");
  const std::string all = layout_bytes("layout_all_cards.dl.json");
  CHECK(ha.show(small));
  CHECK_STR(ha.set("{\"h\":\"a9326c033e45f2f4\",\"v\":[[2,1]]}"), "");
  // Fails at commit: no room. The old layout stays on screen and live.
  CHECK_STR(ha.send(all), "");
  fake::st.largest = 1000000;
  CHECK_STR(ha.commit("4384f665e4dd9f62"), "memory");
  CHECK_STR(tab::active_hash(), "a9326c033e45f2f4");
  CHECK_STR(tab::staged_hash(), "");
  CHECK(fake::st.live.size() == 2);  // the transfer was released
  CHECK_STR(ha.set("{\"h\":\"a9326c033e45f2f4\",\"v\":[[2,0]]}"), "");
  bool b = true;
  CHECK(tab::slot_bool(2, &b) && !b);
  // Fails mid-build: the platform runs out of room after the commit.
  fake::st.largest = 16000000;
  CHECK_STR(ha.send(all), "");
  CHECK_STR(ha.commit("4384f665e4dd9f62"), "");
  CHECK_STR(tab::staged_hash(), "4384f665e4dd9f62");
  fake::st.largest = 1000000;
  fake::st.free = 1000000;
  hh::advance(200);
  char out[768];
  tj::Value v;
  tj::parse(out, tab::on_status(out, sizeof out), &v);
  CHECK_STR(v["build"].s, "memory");
  CHECK_STR(v["staged"].s, "");
  CHECK_STR(v["layout"].s, "a9326c033e45f2f4");
  CHECK(!hidden(active()->root));
  hh::advance(100);
  CHECK(fake::st.live.size() == 2 && !fake::st.bad_release);
  // Still live: values and pages work.
  CHECK_STR(ha.set("{\"h\":\"a9326c033e45f2f4\",\"v\":[[3,\"40%\"]]}"), "");
  CHECK_STR(label_text(widget(4)->obj), "40%");
  // A new seq 0 clears the build code.
  fake::st.largest = 16000000;
  fake::st.free = 28000000;
  CHECK_STR(ha.send(all), "");
  tj::parse(out, tab::on_status(out, sizeof out), &v);
  CHECK_STR(v["build"].s, "");
  // The commit of the hash on screen answers success and changes nothing.
  const D::Layout *before = active();
  CHECK_STR(ha.commit("a9326c033e45f2f4"), "");
  CHECK(active() == before && !tab::building());
  // Bad layouts at commit change nothing either.
  const std::string broken = "{\"v\":1,\"pages\":[{\"title\":\"x\",\"w\":[}";
  CHECK_STR(ha.send(broken), "");
  CHECK_STR(ha.commit(sha_hex(broken).substr(0, 16)), "parse");
  CHECK_STR(tab::active_hash(), "a9326c033e45f2f4");
  hh::advance(50);
  CHECK(fake::st.live.size() == 2);
}

TEST(swap_waits_for_finger) {
  Ha ha;
  ha.boot(true);
  CHECK(ha.hello());
  CHECK(ha.show(layout_bytes("layout_all_cards.dl.json")));
  CHECK_STR(ha.set(state_payload(0)), "");
  hh::advance(20);
  fake::st.events.clear();
  // A slider drag is under way when the new layout becomes ready.
  D::Widget *s = widget(10);
  const int y = centre_y(s->r);
  hh::press(s->r[0] + 40, y);
  hh::move(s->r[0] + 100, y);
  const std::string small = layout_bytes("layout_small.dl.json");
  CHECK_STR(ha.send(small), "");
  CHECK_STR(ha.commit("a9326c033e45f2f4"), "");
  hh::advance(500);
  D::Layout *st = D::staged_layout();
  CHECK(st && st->state == D::L_READY);
  CHECK_STR(tab::active_hash(), "4384f665e4dd9f62");  // waits for the release
  hh::move(260, y);
  hh::release();
  // The drag ended with the right value, on the layout it started on.
  auto ev = fake::events("tab1_input");
  CHECK(ev.size() == 1);
  if (!ev.empty()) {
    CHECK(ev[0].d.find("|4384f665e4dd9f62|10|c|0|42") != std::string::npos);
  }
  hh::advance(20);
  CHECK_STR(tab::active_hash(), "a9326c033e45f2f4");
  // A finger that stays down longer than 3 s does not hold the swap forever.
  hh::press(700, 600);
  CHECK_STR(ha.send(layout_bytes("layout_all_cards.dl.json")), "");
  CHECK_STR(ha.commit("4384f665e4dd9f62"), "");
  hh::advance(1000);
  CHECK_STR(tab::active_hash(), "a9326c033e45f2f4");
  hh::advance(3000);
  CHECK_STR(tab::active_hash(), "4384f665e4dd9f62");
  hh::release();
}

TEST(values_during_build) {
  // While a layout is built, tab_set reaches the old one (on screen) and the
  // staged one (by hash); any other hash is refused.
  Ha ha;
  ha.boot(false);
  CHECK(ha.show(layout_bytes("layout_small.dl.json")));
  CHECK_STR(ha.send(layout_bytes("layout_all_cards.dl.json")), "");
  CHECK_STR(ha.commit("4384f665e4dd9f62"), "");
  CHECK(tab::building());
  CHECK_STR(ha.set(state_payload(0)), "");
  CHECK_STR(ha.set("{\"h\":\"a9326c033e45f2f4\",\"v\":[[3,\"55%\"]]}"), "");
  CHECK_STR(label_text(widget(4)->obj), "55%");
  CHECK_STR(ha.set("{\"h\":\"0000000000000000\",\"v\":[[3,\"1%\"]]}"), "hash");
  CHECK(advance_until([] { return std::string(tab::active_hash()) == "4384f665e4dd9f62"; }, 3000));
  char buf[64];
  CHECK(tab::slot_text(1, buf, sizeof buf) && std::string(buf) == "Sofa lamp");
  char want[64];
  CHECK(tab::slot_text(3, want, sizeof want));
  CHECK_STR(label_text(widget(4)->obj), want);
}

TEST(page_kept_across_swap) {
  Ha ha;
  ha.boot(false);
  const std::string all = layout_bytes("layout_all_cards.dl.json");
  CHECK(ha.show(all));
  tab::go_to(2);
  hh::advance(20);
  CHECK(ha.show(layout_bytes("layout_all_cards.nochart.dl.json")));
  CHECK(tab::current_page() == 2);
  CHECK(!hidden(active()->model->pages[2].obj) && hidden(active()->model->pages[0].obj));
  CHECK(ha.show(layout_bytes("layout_small.dl.json")));  // one page only
  CHECK(tab::current_page() == 0);
}

TEST(navigation) {
  Ha ha;
  ha.boot(false);
  CHECK(ha.show(layout_bytes("layout_all_cards.dl.json")));
  CHECK_STR(ha.set(state_payload(0)), "");
  hh::advance(20);
  // Side bar.
  hh::tap(48, 16 + 72 + 32);
  CHECK(tab::current_page() == 2);
  hh::tap(48, 16 + 32);
  CHECK(tab::current_page() == 0);
  // Swipes, wrapping at the ends.
  hh::drag(900, 700, 500, 690, 120);
  CHECK(tab::current_page() == 1);
  hh::drag(500, 700, 900, 700, 120);
  CHECK(tab::current_page() == 0);
  hh::drag(500, 700, 900, 700, 120);
  CHECK(tab::current_page() == 2);
  hh::drag(900, 700, 500, 700, 120);
  CHECK(tab::current_page() == 0);
  // A mostly vertical move is no swipe.
  hh::drag(700, 300, 760, 700, 120);
  CHECK(tab::current_page() == 0);
  // A swipe that starts on a button does not press it.
  fake::st.events.clear();
  D::Widget *b7 = widget(7);
  hh::drag(centre_x(b7->r), centre_y(b7->r), centre_x(b7->r) - 200, centre_y(b7->r), 120);
  CHECK(tab::current_page() == 1);
  CHECK(fake::events("tab1_input").empty());
  CHECK(!widget(7)->pressed);
  // A "go" button.
  D::Widget *go = widget(47);
  hh::tap(centre_x(go->r), centre_y(go->r));
  CHECK(tab::current_page() == 2);
  // A pressed look is drawn in the same frame as the touch.
  tab::go_to(0);
  hh::advance(20);
  b7 = widget(7);
  // The display's own refresh is held back, so only the engine's lv_refr_now
  // on the press can put the pressed look on screen.
  lv_timer_t *refr = lv_display_get_refr_timer(hh::g_disp);
  lv_timer_set_period(refr, 100000);
  lv_timer_reset(refr);
  hh::press(centre_x(b7->r), centre_y(b7->r));
  lv_timer_set_period(refr, LV_DEF_REFR_PERIOD);
  const D::Tok t = D::card_tokens(*active()->model, D::card_of(*active()->model, *b7));
  // Contract 4.7: pressed is card2_pr, or onbg_pr in a card that is on (this one is).
  const bool card_on = D::card_is_on(*active()->model, D::card_of(*active()->model, *b7));
  CHECK(card_on);
  const uint32_t want_pr = card_on ? t.onbg_pr : t.card2_pr;
  char px[96];
  snprintf(px, sizeof px, "got %04x want %04x", hh::pixel(b7->r[0] + 3, b7->r[1] + b7->r[3] / 2), hh::rgb565(want_pr));
  CHECK_MSG(hh::pixel(b7->r[0] + 3, b7->r[1] + b7->r[3] / 2) == hh::rgb565(want_pr), px);
  hh::release();
  hh::advance(20);
}

TEST(load_swap_cycles) {
  Ha ha;
  ha.boot(false);
  const std::string a = layout_bytes("layout_small.dl.json");
  const std::string b = a + "\n";  // valid JSON, other bytes, other hash
  const uint64_t a0 = fake::st.allocs, r0 = fake::st.releases;
  const int cycles = 1000;
  for (int i = 0; i < cycles; i++) {
    if (!CHECK(ha.show(i % 2 ? b : a))) break;
    hh::advance(15);
  }
  hh::advance(50);
  CHECK(fake::st.allocs - a0 == 2u * cycles);
  CHECK(fake::st.releases - r0 == 2u * (cycles - 1));
  CHECK(fake::st.live.size() == 2);
  CHECK(lv_obj_get_child_count(D::g.base) == 2);  // the waiting screen and one layout
  CHECK_STR(ha.clear(), "");
  hh::advance(50);
  CHECK(fake::st.live.empty() && !fake::st.bad_release);
  CHECK(lv_obj_get_child_count(D::g.base) == 1);
  CHECK(!hidden(D::g.waiting));
}

// ================================================================ flash cache

TEST(cache_and_reboot) {
  Ha ha;
  ha.boot(false);
  const std::string all = layout_bytes("layout_all_cards.dl.json");
  CHECK(ha.show(all));
  hh::advance(29000);
  CHECK(!fake::st.flash_has);  // 30 s after the swap, not before
  hh::advance(1100);
  CHECK(fake::st.flash_has && fake::st.flash.size() == all.size() &&
        memcmp(fake::st.flash.data(), all.data(), all.size()) == 0);
  // "Reboot": the same hash comes on screen by itself, all slots without
  // value. It is built in slices after begin(), not inside it.
  ha.boot(false, true);
  CHECK(advance_until([] { return std::string(tab::active_hash()) == "4384f665e4dd9f62"; }, 3000));
  CHECK(tab::slot_null(1) && tab::slot_null(2));
  CHECK(hidden(D::g.waiting));
  // The same layout again is not written again.
  hh::advance(31000);
  CHECK(fake::st.writes == 0 && fake::st.save_calls == 0);
  // A copy that cannot be read is erased, its block released, and the waiting screen shows.
  shutdown();
  CHECK(fake::st.flash_has && fake::st.live.empty());
  fake::st.flash_damaged = true;
  fake::st.erase_calls = 0;
  start();
  CHECK_STR(tab::active_hash(), "");
  CHECK(!fake::st.flash_has && fake::st.erase_calls == 1);
  CHECK(fake::st.live.empty() && !fake::st.bad_release);
  CHECK(!hidden(D::g.waiting));
  // A copy that is not a layout is erased too.
  shutdown();
  fake::st = fake::State();
  fake::st.flash_has = true;
  const std::string junk = "{\"v\":1,\"pages\":[";
  fake::st.flash.assign(junk.begin(), junk.end());
  start();
  CHECK_STR(tab::active_hash(), "");
  CHECK(!fake::st.flash_has);
  CHECK(fake::st.live.empty());
  // No memory for the stored copy: kept for the next boot, waiting screen.
  shutdown();
  fake::st = fake::State();
  fake::st.flash_has = true;
  fake::st.flash.assign(all.begin(), all.end());
  fake::st.alloc_fails = true;
  start();
  CHECK_STR(tab::active_hash(), "");
  CHECK(fake::st.flash_has && fake::st.erase_calls == 0);
  CHECK(!hidden(D::g.waiting));
  // tab_clear erases the stored copy.
  ha.boot(false);
  CHECK(ha.show(all));
  hh::advance(30100);
  CHECK(fake::st.flash_has);
  CHECK_STR(ha.clear(), "");
  CHECK(!fake::st.flash_has);
}

// A stored copy kept at boot (no memory to show it) must not come back after
// a later layout that is too big to store: it is erased when that layout is
// on screen, although `stored` names nothing.
TEST(cache_oversize_erases_a_kept_copy) {
  Ha ha;
  const std::string small = layout_bytes("layout_small.dl.json");
  const std::string all = layout_bytes("layout_all_cards.dl.json");
  shutdown();
  reset_platform();
  fake::st.flash_has = true;
  fake::st.flash.assign(small.begin(), small.end());
  fake::st.largest = 500000;  // too little to show the stored copy at boot
  start();
  CHECK_STR(tab::active_hash(), "");
  CHECK(fake::st.flash_has && fake::st.erase_calls == 0);
  fake::st.largest = 16000000;
  // A layout over the 128 KB cache limit: the all-cards layout with a large
  // key the tablet does not know.
  std::string big = all;
  big.insert(big.size() - 1, ",\"zz_unknown\":\"" + std::string(140 * 1024, 'x') + "\"");
  CHECK(big.size() > D::CACHE_LIMIT);
  ha.paired = false;
  CHECK(ha.hello());
  CHECK(ha.show(big));
  hh::advance(31000);
  CHECK(!fake::st.flash_has && fake::st.erase_calls == 1);
  // A reboot keeping flash shows the waiting screen, not the old small layout.
  ha.boot(false, true);
  CHECK_STR(tab::active_hash(), "");
  CHECK(!hidden(D::g.waiting));
}

TEST(cache_slow_save_and_swap) {
  Ha ha;
  ha.boot(false);
  fake::st.save_ms = 40000;  // a very slow flash
  const std::string all = layout_bytes("layout_all_cards.dl.json");
  const std::string small = layout_bytes("layout_small.dl.json");
  CHECK(ha.show(all));
  hh::advance(30100);
  CHECK(fake::cache_busy() && fake::st.save_calls == 1);
  // Swap during the save. The old layout's blocks go; the save has its own copy.
  CHECK(ha.show(small));
  hh::advance(200);
  CHECK(fake::st.live.size() == 2);
  hh::advance(30000);  // the second save is due while the first still runs
  CHECK(fake::st.save_calls == 1);
  hh::advance(12000);
  CHECK(fake::st.flash_has && fake::st.flash.size() == all.size() &&
        memcmp(fake::st.flash.data(), all.data(), all.size()) == 0);
  CHECK(fake::st.save_calls == 2);  // queued once the flash was free
  hh::advance(41000);
  CHECK(fake::st.flash.size() == small.size() && memcmp(fake::st.flash.data(), small.data(), small.size()) == 0);
  // cache_save answering false is retried 30 s later.
  ha.boot(false);
  fake::st.save_fail_next = 1;
  CHECK(ha.show(all));
  hh::advance(30100);
  CHECK(fake::st.save_calls == 1 && !fake::st.flash_has);
  hh::advance(30000);
  CHECK(fake::st.save_calls == 2 && fake::st.flash_has);
  // An erase during a save wins.
  ha.boot(false);
  fake::st.save_ms = 5000;
  CHECK(ha.show(all));
  hh::advance(30100);
  CHECK(fake::cache_busy());
  CHECK_STR(ha.clear(), "");
  hh::advance(12000);
  CHECK(!fake::st.flash_has && !fake::cache_busy());
}

// cache_save accepts a copy and the platform writes it later. A write that
// fails then (NVS full, a power glitch) is reported with cache_failed, and the
// engine stores the layout again 30 s later instead of never.
TEST(cache_failed_write_is_retried) {
  Ha ha;
  const std::string all = layout_bytes("layout_all_cards.dl.json");
  ha.boot(false);
  fake::st.write_fail_next = 1;
  CHECK(ha.show(all));
  hh::advance(30100);
  CHECK(fake::st.save_calls == 1 && !fake::st.flash_has);
  CHECK(D::g.stored[0] == 0);  // not counted as stored
  hh::advance(29000);
  CHECK(fake::st.save_calls == 1);
  hh::advance(1200);
  CHECK(fake::st.save_calls == 2 && fake::st.flash_has && fake::st.flash.size() == all.size());
  CHECK_STR(D::g.stored, "4384f665e4dd9f62");
  // Nothing more once stored, and no polling at rest.
  const int polls = fake::st.failed_polls;
  hh::advance(100000);
  CHECK(fake::st.save_calls == 2 && fake::st.failed_polls == polls);
  // A slow write that fails: seen when it ends.
  ha.boot(false);
  fake::st.save_ms = 5000;
  fake::st.write_fail_next = 1;
  CHECK(ha.show(all));
  hh::advance(30100);
  CHECK(fake::st.save_calls == 1 && fake::cache_busy());
  CHECK_STR(D::g.stored, "4384f665e4dd9f62");
  hh::advance(5100);
  CHECK(!fake::cache_busy() && !fake::st.flash_has && D::g.stored[0] == 0);
  hh::advance(36000);
  CHECK(fake::st.save_calls == 2 && fake::st.flash_has);
  // A failure seen after tab_clear changes nothing more: no layout to store.
  ha.boot(false);
  fake::st.save_ms = 5000;
  fake::st.write_fail_next = 1;
  CHECK(ha.show(all));
  hh::advance(30100);
  CHECK_STR(ha.clear(), "");
  hh::advance(80000);
  CHECK(fake::st.save_calls == 1 && !fake::st.flash_has);
}

// ================================================================ knock

static std::vector<int> knock_times(int limit) {
  std::vector<int> t;
  int at = 0, gap = 3;
  for (;;) {
    at += gap;
    if (at > limit) return t;
    t.push_back(at);
    if (t.size() >= 5) gap = std::min(gap * 2, 300);
  }
}

TEST(knock_schedule) {
  Ha ha;
  ha.boot(false);
  fake::st.connected = false;
  hh::advance(1000);
  fake::st.events.clear();
  const uint32_t t0 = hh::g_ms;
  fake::st.connected = true;
  const int limit = QUICK ? 400 : 1000;
  hh::advance((uint32_t) limit * 1000 + 500);
  auto ev = fake::events("tab1_hello");
  const auto want = knock_times(limit);
  CHECK_MSG(ev.size() == want.size(), std::to_string(ev.size()) + " knocks");
  for (size_t i = 0; i < ev.size() && i < want.size(); i++) {
    const int got = (int) (ev[i].at - t0);
    CHECK_MSG(got >= want[i] * 1000 && got <= want[i] * 1000 + 15, std::to_string(got));
    CHECK_STR(ev[i].d, "1|aabbccddeeff|3");
    CHECK(!ev[i].has_s);
  }
  // tab_status stops it.
  char out[768];
  tab::on_status(out, sizeof out);
  fake::st.events.clear();
  hh::advance(700000);
  CHECK(fake::events("tab1_hello").empty());
  // A new connection starts again; tab_hello stops it.
  fake::st.connected = false;
  hh::advance(100);
  fake::st.connected = true;
  hh::advance(9100);
  CHECK(fake::events("tab1_hello").size() == 3);
  CHECK(ha.hello());
  hh::advance(60000);
  CHECK(fake::events("tab1_hello").size() == 3);
  // A new connection also ends the session.
  CHECK(D::g.session);
  fake::st.connected = false;
  hh::advance(10);
  CHECK(!D::g.session);
}

// Home Assistant subscribes to states at the end of its connect sequence and
// may call tab_hello or tab_status before that. ha_connected() then turns true
// after the call, on the same connection: the session stays and no knock
// follows (contract 2.2: no knock once tab_hello or tab_status has arrived).
TEST(hello_before_subscription) {
  Ha ha;
  ha.boot(true);
  fake::st.connected = false;
  hh::advance(100);
  CHECK(ha.hello());
  CHECK(D::g.session);
  fake::st.events.clear();
  fake::st.connected = true;
  hh::advance(20000);
  CHECK(D::g.session);
  CHECK(fake::events("tab1_hello").empty());
  // The session works: signed calls are accepted and a tap is sent.
  CHECK(ha.show(layout_bytes("layout_all_cards.dl.json")));
  CHECK_STR(ha.set(state_payload(0)), "");
  hh::advance(20);
  fake::st.events.clear();
  D::Widget *sw = widget(3);
  if (CHECK(sw)) hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(fake::events("tab1_input").size() == 1);
  // tab_status before the subscription also stops the knocks; it starts no session.
  fake::st.connected = false;
  hh::advance(20);
  CHECK(!D::g.session);
  char out[768];
  CHECK(tab::on_status(out, sizeof out) > 0);
  fake::st.events.clear();
  fake::st.connected = true;
  hh::advance(20000);
  CHECK(fake::events("tab1_hello").empty());
  CHECK(!D::g.session);
  // Without such a call, a new connection knocks as before.
  fake::st.connected = false;
  hh::advance(20);
  fake::st.events.clear();
  fake::st.connected = true;
  hh::advance(3100);
  CHECK(fake::events("tab1_hello").size() == 1);
  // An early call counts for 30 s only: after that a new connection is new.
  fake::st.connected = false;
  hh::advance(20);
  CHECK(ha.hello());
  hh::advance(31000);
  fake::st.events.clear();
  fake::st.connected = true;
  hh::advance(3100);
  CHECK(!D::g.session);
  CHECK(fake::events("tab1_hello").size() == 1);
}

// ================================================================ policies

struct InputRig {
  Ha ha;
  void up() {
    ha.boot(true);
    CHECK(ha.hello());
    CHECK(ha.show(layout_bytes("layout_all_cards.dl.json")));
    CHECK_STR(ha.set(state_payload(0)), "");
    hh::advance(20);
    fake::st.events.clear();
  }
  size_t inputs() { return fake::events("tab1_input").size(); }
  uint32_t last_counter() {
    auto ev = fake::events("tab1_input");
    if (ev.empty()) return 0;
    const std::string &d = ev.back().d;
    size_t p = 0;
    for (int i = 0; i < 3; i++) p = d.find('|', p) + 1;
    return (uint32_t) atoi(d.c_str() + p);
  }
};

static int knob_x(D::Widget *w) {
  lv_obj_update_layout(w->o2);
  return (int) lv_obj_get_x(w->o2);
}

TEST(policy_optimistic) {
  InputRig rig;
  rig.up();
  D::Widget *sw = widget(3);  // tap p0, value slot 2 = 1
  CHECK(knob_x(sw) == 32);
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(rig.inputs() == 1);
  CHECK(knob_x(sw) == 4);  // the new state at once
  hh::advance(3800);
  CHECK(knob_x(sw) == 4);
  hh::advance(300);
  CHECK(knob_x(sw) == 32);  // no ack within 4 s: back
  // With an ok ack the new state stays until the value comes.
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  rig.ha.ack(rig.last_counter(), true, "");
  hh::advance(1000);
  CHECK(knob_x(sw) == 4);
  CHECK_STR(rig.ha.set("{\"h\":\"4384f665e4dd9f62\",\"v\":[[2,0]]}"), "");
  CHECK(knob_x(sw) == 4 && sw->ovr == 0);
  // A not-ok ack goes back at once.
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(knob_x(sw) == 32);
  rig.ha.ack(rig.last_counter(), false, "failed");
  CHECK(knob_x(sw) == 4);
  // A slider (change p0) keeps the released value until the answer, then
  // goes back to the real value 4 s later without one.
  D::Widget *sl = widget(10);
  hh::drag(sl->r[0] + 10, centre_y(sl->r), sl->r[0] + 172, centre_y(sl->r), 100);
  CHECK(sl->ovr == 1 && sl->ovr_value == 50);
  lv_obj_update_layout(sl->o2);
  CHECK(lv_obj_get_width(sl->o2) == 172);
  hh::advance(4100);
  double real = 0;
  CHECK(tab::slot_number(sl->value.slot, &real));
  lv_obj_update_layout(sl->o2);
  CHECK(sl->ovr == 0 && lv_obj_get_width(sl->o2) == (int) std::floor(real / 100 * sl->r[2] + 0.5));
}

TEST(policy_confirm_and_acks) {
  InputRig rig;
  rig.up();
  tab::go_to(2);
  hh::advance(20);
  // Policy 3 with cf: ask first; "No" sends nothing.
  D::Widget *lock = widget(53);
  hh::tap(centre_x(lock->r), centre_y(lock->r));
  CHECK(rig.inputs() == 0 && !hidden(D::g.question));
  hh::tap(504, 456);
  CHECK(rig.inputs() == 0 && hidden(D::g.question));
  // A tap outside the box cancels too.
  hh::tap(centre_x(lock->r), centre_y(lock->r));
  hh::tap(200, 100);
  CHECK(rig.inputs() == 0 && hidden(D::g.question));
  // A constant question text.
  D::Widget *arm = widget(58);
  hh::tap(centre_x(arm->r), centre_y(arm->r));
  CHECK_STR(label_text(D::g.q_text), "Arm home?");
  hh::tap(776, 456);
  CHECK(rig.inputs() == 1);
  CHECK(fake::events("tab1_input").back().d.find("|58|t|1|") != std::string::npos);
  CHECK(arm->fb == 1);  // waiting outline until the ack
  rig.ha.ack(rig.last_counter(), true, "");
  CHECK(arm->fb == 0);
  // Ack "denied": the message strip.
  hh::tap(centre_x(arm->r), centre_y(arm->r));
  hh::tap(776, 456);
  rig.ha.ack(rig.last_counter(), false, "denied");
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), "Not allowed on this tablet");
  hh::advance(2600);
  CHECK(hidden(D::g.strip));
  // Ack "confirm" for a policy 0 button: ask, then send again with confirmed 1.
  tab::go_to(0);
  hh::advance(20);
  D::Widget *b7 = widget(7);
  hh::tap(centre_x(b7->r), centre_y(b7->r));
  CHECK(fake::events("tab1_input").back().d.find("|7|t|0|") != std::string::npos);
  CHECK(b7->fb == 1);
  rig.ha.ack(rig.last_counter(), false, "confirm");
  CHECK(b7->fb == 0 && !hidden(D::g.question));
  CHECK_STR(label_text(D::g.q_text), "Are you sure?");
  const size_t before = rig.inputs();
  hh::tap(776, 456);
  CHECK(rig.inputs() == before + 1);
  CHECK(fake::events("tab1_input").back().d.find("|7|t|1|") != std::string::npos);
  // A refused (unsigned) ack is ignored.
  const uint32_t c = rig.last_counter();
  const std::string body = std::to_string(c) + "|1|";
  tab::on_ack((int) c, true, "", 0, "1.00000000000000000000000000000000", 34);
  CHECK(b7->fb == 1);
}

TEST(policy_pulse_and_progress) {
  InputRig rig;
  rig.up();
  tab::go_to(1);
  hh::advance(20);
  // Pulse (scene buttons, p1): accf for 300 ms.
  D::Widget *scene = widget(35);
  const D::Tok t = D::card_tokens(*active()->model, D::card_of(*active()->model, *scene));
  hh::tap(centre_x(scene->r), centre_y(scene->r));
  CHECK(scene->fb == 2);
  hh::refresh();
  CHECK(hh::pixel(scene->r[0] + 3, scene->r[1] + scene->r[3] / 2) == hh::rgb565(t.accf));
  hh::advance(320);
  CHECK(scene->fb == 0);
  // Progress (blinds buttons, p2): outline until a value of the card changes.
  tab::go_to(0);
  hh::advance(20);
  D::Widget *up = widget(14);
  hh::tap(centre_x(up->r), centre_y(up->r));
  CHECK(up->fb == 1);
  rig.ha.ack(rig.last_counter(), true, "");
  CHECK(up->fb == 1);  // ok ack: still working
  hh::advance(1000);
  CHECK_STR(rig.ha.set("{\"h\":\"4384f665e4dd9f62\",\"v\":[[12,\"opening\"]]}"), "");
  CHECK(up->fb == 0);
  // ... or until 30 s pass.
  hh::tap(centre_x(up->r), centre_y(up->r));
  hh::advance(29000);
  CHECK(up->fb == 1);
  hh::advance(1100);
  CHECK(up->fb == 0);
  // Thermostat plus: each press sends the stepped value.
  D::Widget *th = widget(23);  // 21.5, step 0.5
  lv_obj_update_layout(lv_screen_active());
  hh::tap(th->r[0] + th->r[2] - 18, th->r[1] + th->r[3] - 18);
  hh::tap(th->r[0] + th->r[2] - 18, th->r[1] + th->r[3] - 18);
  auto ev = fake::events("tab1_input");
  CHECK(ev.size() >= 2 && ev[ev.size() - 2].d.find("|23|c|0|22") != std::string::npos &&
        ev.back().d.find("|23|c|0|22.5") != std::string::npos);
  CHECK_STR(label_text(th->o1), "22.5");
}

TEST(no_input_without_session) {
  InputRig rig;
  rig.up();
  D::Widget *sw = widget(3);
  // HA away, Wi-Fi up (without Wi-Fi: wifi_notes_over_a_dashboard).
  fake::st.network = true;
  fake::st.connected = false;
  hh::advance(20);
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(rig.inputs() == 0 && !hidden(D::g.strip) && knob_x(sw) == 32);
  CHECK_STR(label_text(D::g.strip_label), "Home Assistant is not connected");
  // Back, but no session yet on this connection.
  fake::st.connected = true;
  hh::advance(3000);
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(rig.inputs() == 0);
  CHECK_STR(label_text(D::g.strip_label), "Home Assistant is not connected");
  CHECK(rig.ha.hello());
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(rig.inputs() == 1 && rig.last_counter() == 1);  // the counter starts at 1 per session
  // Not paired: no input.
  rig.ha.boot(false);
  CHECK(rig.ha.hello());
  CHECK(rig.ha.show(layout_bytes("layout_all_cards.dl.json")));
  CHECK_STR(rig.ha.set(state_payload(0)), "");
  hh::advance(20);
  fake::st.events.clear();
  hh::tap(centre_x(widget(3)->r), centre_y(widget(3)->r));
  CHECK(rig.inputs() == 0);
  // Contract 7.4: connected with a session, but not paired, says so.
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), "Not paired with Home Assistant");
  // No API key: touch needs encryption, whatever secret is stored.
  rig.ha.boot(true);
  fake::st.have_key = false;
  rig.ha.paired = false;  // HA does not sign without a key (contract 6.1)
  CHECK(rig.ha.hello());
  CHECK(rig.ha.show(layout_bytes("layout_all_cards.dl.json")));
  CHECK_STR(rig.ha.set(state_payload(0)), "");
  hh::advance(20);
  fake::st.events.clear();
  hh::tap(centre_x(widget(3)->r), centre_y(widget(3)->r));
  CHECK(rig.inputs() == 0);
  CHECK_STR(label_text(D::g.strip_label), "Touch is off: this tablet is not protected");
}

// ================================================================ load limits

static std::string hash16(const std::string &b) { return sha_hex(b).substr(0, 16); }

// pages x cards, each card with per_card labels; tag makes the bytes differ.
static std::string gen_cards(int pages, int cards, int per_card, const char *tag = "") {
  std::string s = "{\"pages\":[";
  int id = 1;
  for (int p = 0; p < pages; p++) {
    if (p) s += ",";
    s += "{\"title\":\"P" + std::to_string(p) + tag + "\",\"w\":[";
    for (int c = 0; c < cards; c++) {
      if (c) s += ",";
      const int cx = 100 + (c % 8) * 147, cy = (c / 8) % 8 * 100;
      s += "{\"id\":" + std::to_string(id++) + ",\"r\":[" + std::to_string(cx) + "," + std::to_string(cy) +
           ",140,96],\"t\":\"card\",\"w\":[";
      for (int k = 0; k < per_card; k++) {
        if (k) s += ",";
        s += "{\"id\":" + std::to_string(id++) + ",\"r\":[" + std::to_string(cx + 4) + "," +
             std::to_string(cy + 4 + k * 10) + ",130,10],\"size\":0,\"t\":\"label\",\"text\":\"x\"}";
      }
      s += "]}";
    }
    s += "]}";
  }
  s += "],\"v\":1}";
  return s;
}

// One page with one card holding labels - 1 labels.
static std::string gen_big_card(int labels) {
  std::string s = "{\"pages\":[{\"title\":\"P\",\"w\":[{\"id\":1,\"r\":[100,0,1180,800],\"t\":\"card\",\"w\":[";
  for (int i = 0; i < labels - 1; i++) {
    if (i) s += ",";
    const int x = 100 + (i % 20) * 59, y = (i / 20) % 100 * 8;
    s += "{\"id\":" + std::to_string(i + 2) + ",\"r\":[" + std::to_string(x) + "," + std::to_string(y) +
         ",58,8],\"size\":0,\"t\":\"label\",\"text\":\"x\"}";
  }
  s += "]}]}],\"v\":1}";
  return s;
}

static uint32_t count_objects(lv_obj_t *o) {
  uint32_t n = 1;
  const uint32_t k = lv_obj_get_child_count(o);
  for (uint32_t i = 0; i < k; i++) n += count_objects(lv_obj_get_child(o, (int32_t) i));
  return n;
}

static int slots_in_use() {
  int used = 0;
  for (const D::Layout &l : D::g.lay) used += l.state != D::L_FREE;
  return used;
}

// Commits that come faster than old layouts are deleted never run out of the
// layout slots: with memory to spare every commit is accepted, never "memory".
TEST(commit_burst) {
  const std::string small = layout_bytes("layout_small.dl.json");
  Ha ha;
  for (int gap : {0, 5, 15, 50}) {
    for (int pages : {3, 20}) {
      const std::string name = "commit_burst gap " + std::to_string(gap) + " pages " + std::to_string(pages);
      g_test = name.c_str();
      ha.boot(false);
      CHECK(ha.show(small));
      const std::string a = gen_cards(pages, 2, 1, "a"), b = gen_cards(pages, 2, 1, "b");
      for (int i = 0; i < 40; i++) {
        const std::string &x = (i % 2) ? a : b;
        CHECK_STR(ha.send(x, 16000), "");
        CHECK_STR(ha.commit(hash16(x)), "");
        hh::advance((uint32_t) gap);
      }
      CHECK(advance_until([&] { return hash16(a) == tab::active_hash(); }, 5000));
      hh::advance(2000);
      CHECK(slots_in_use() == 1 && fake::st.live.size() == 2 && !fake::st.bad_release);
    }
  }
  g_test = "commit_burst";
}

// Design 7.5: one build slice makes at most SLICE_OBJECTS objects and one
// delete slice removes at most DELETE_OBJECTS, also for a card with very many
// widgets: it is built and deleted over several slices. Small pages share a
// delete slice, so a retired layout frees its slot soon.
TEST(slice_budget) {
  const std::string small = layout_bytes("layout_small.dl.json");
  struct L {
    const char *name;
    std::string bytes;
  };
  const std::vector<L> ls = {{"40 cards x 4 labels x 3 pages", gen_cards(3, 40, 4)},
                             {"2 cards x 1 label x 20 pages", gen_cards(20, 2, 1)},
                             {"1 card x 200 labels", gen_big_card(201)},
                             {"1 card x 2047 labels", gen_big_card(2048)}};
  Ha ha;
  for (const L &l : ls) {
    g_test = l.name;
    ha.boot(false);
    CHECK(ha.show(small));
    hh::advance(300);
    const std::string h = hash16(l.bytes);
    CHECK_STR(ha.send(l.bytes, 16000), "");
    CHECK_STR(ha.commit(h), "");
    uint32_t before = count_objects(D::g.base), most_made = 0, slices = 0;
    for (int t = 0; t < 60000 && h != tab::active_hash(); t++) {
      hh::advance(1);
      const uint32_t n = count_objects(D::g.base);
      if (n > before) {
        slices++;
        most_made = std::max(most_made, n - before);
      }
      before = n;
    }
    CHECK_STR(tab::active_hash(), h);
    CHECK_MSG(most_made <= D::SLICE_OBJECTS, std::to_string(most_made) + " objects in one build slice");
    // Every widget was built, none skipped across slices.
    D::Layout *a = active();
    bool all_built = a != nullptr;
    for (uint32_t i = 0; a && i < a->model->nw; i++) all_built = all_built && a->model->w[i].obj;
    CHECK(all_built);
    const uint32_t objects = a ? count_objects(a->root) : 0;
    // Back to the small layout: the big one is deleted a slice at a time.
    CHECK_STR(ha.send(small, 16000), "");
    CHECK_STR(ha.commit(hash16(small)), "");
    before = count_objects(D::g.base);
    uint32_t most_gone = 0, delete_slices = 0;
    for (int t = 0; t < 60000 && (hash16(small) != tab::active_hash() || slots_in_use() > 1); t++) {
      hh::advance(1);
      const uint32_t n = count_objects(D::g.base);
      if (n < before) {
        delete_slices++;
        most_gone = std::max(most_gone, before - n);
      }
      before = n;
    }
    CHECK(slots_in_use() == 1 && fake::st.live.size() == 2);
    CHECK_MSG(most_gone <= D::DELETE_OBJECTS, std::to_string(most_gone) + " objects in one delete slice");
    CHECK_MSG(delete_slices <= objects / D::DELETE_OBJECTS + 2, std::to_string(delete_slices) + " delete slices");
    printf("  %-30s %u objects: %u build slices of at most %u, %u delete slices of at most %u\n", l.name, objects,
           slices, most_made, delete_slices, most_gone);
  }
  g_test = "slice_budget";
}

// One timer tick spends at most SLICE_MS on building and deleting together:
// the delete slice counts from the start of the tick, so work earlier in the
// tick leaves it less time, and none once SLICE_MS has gone. Here the poll at
// the start of each tick takes the whole slice, so nothing is deleted until
// it gets fast again.
TEST(delete_shares_tick_time) {
  const std::string small = layout_bytes("layout_small.dl.json");
  const std::string big = gen_cards(3, 40, 4);
  Ha ha;
  ha.boot(false);
  CHECK(ha.show(small));
  CHECK(ha.show(big));
  hh::advance(300);
  CHECK(slots_in_use() == 1);
  fake::st.on_poll = [] { hh::g_ms += D::SLICE_MS; };
  CHECK(ha.show(small));
  const uint32_t before = count_objects(D::g.base);
  hh::advance(500);
  CHECK_MSG(slots_in_use() == 2 && count_objects(D::g.base) == before,
            std::to_string(before - count_objects(D::g.base)) + " objects deleted with no time left");
  fake::st.on_poll = nullptr;
  CHECK(advance_until([] { return slots_in_use() == 1; }, 2000));
  CHECK(fake::st.live.size() == 2 && !fake::st.bad_release);
}

// Numbers out of range never reach a float-to-int cast (UBSan in the second
// run checks that): a layout with one answers "parse", a value is held to
// +-1e9 as HA's num() does, and a value that is not finite is skipped.
// Contract 4.3: a rectangle is cut to the 1280x800 screen. A thermostat
// larger than the screen was drawn with a radius at which LVGL's radius mask
// writes past its buffer (ASan, heap-buffer-overflow in circ_calc_aa4). HA
// itself sends negative sizes for cards one column wide, so cutting, not
// refusing, keeps those dashboards working.
TEST(rects_cut_to_screen) {
  int32_t width = 0, radius = 0;
  for (int s : {-5, 0, 1, 10, 120, 320, 800, 922, 1500, 2000, 4000, 30000}) {
    D::thermo_arc(s, &width, &radius);
    CHECK_MSG(radius >= 1 && radius <= 402 && width >= 1 && width <= radius, std::to_string(s));
  }
  D::thermo_arc(320, &width, &radius);
  CHECK(width == 37 && radius == 133);  // unchanged for the sizes the editor makes
  auto layout = [](const std::string &card, const std::string &thermo, const char *value = "{\"s\":1}") {
    return "{\"pages\":[{\"title\":\"P\",\"w\":[{\"hr\":[0,790,10,11],\"id\":1,\"r\":" + card +
           ",\"t\":\"card\",\"w\":[{\"id\":2,\"max\":35,\"min\":5,\"r\":" + thermo +
           ",\"step\":0.5,\"t\":\"thermostat\",\"value\":" + value + "}]}]}],\"slots\":\"n\",\"v\":1}";
  };
  using R = std::array<int, 4>;
  struct Case {
    const char *name;
    std::string bytes;
    const char *want;
    R card, thermo;
  };
  const R full{0, 0, 1280, 800};
  const std::vector<Case> cases = {
      {"thermostat 2000", layout("[0,0,2000,2000]", "[0,0,2000,2000]"), "", full, full},
      {"thermostat 1600 fixed value", layout("[0,0,1600,1600]", "[0,0,1600,1600]", "21"), "", full, full},
      {"thermostat 4000 off the corner", layout("[0,0,800,800]", "[-4000,-4000,4000,4000]"), "", R{0, 0, 800, 800},
       R{0, 0, 0, 0}},
      {"x below 0", layout("[-1,0,100,100]", "[0,0,10,10]"), "", R{0, 0, 99, 100}, R{0, 0, 10, 10}},
      {"w below 0 (HA, one column wide)", layout("[0,0,-10,100]", "[16,56,-16,-16]"), "", R{0, 0, 0, 100},
       R{16, 56, 0, 0}},
      {"right edge past 1280", layout("[1200,0,81,100]", "[1279,0,10,10]"), "", R{1200, 0, 80, 100},
       R{1279, 0, 1, 10}},
      {"bottom edge past 800", layout("[0,700,100,101]", "[0,900,10,10]"), "", R{0, 700, 100, 100},
       R{0, 800, 10, 0}},
      {"whole screen", layout("[0,0,1280,800]", "[0,0,1280,800]"), "", full, full},
      {"whole screen fixed value", layout("[0,0,1280,800]", "[0,0,1280,800]", "21"), "", full, full},
      {"bottom right corner", layout("[1180,700,100,100]", "[1270,790,10,10]"), "", R{1180, 700, 100, 100},
       R{1270, 790, 10, 10}},
      {"number past 4000", layout("[0,0,4001,10]", "[0,0,10,10]"), "parse", R{}, R{}},
      {"number below -4000", layout("[0,0,10,10]", "[-4001,0,10,10]"), "parse", R{}, R{}},
  };
  auto rect_of = [](const int16_t r[4]) { return R{r[0], r[1], r[2], r[3]}; };
  Ha ha;
  for (const Case &c : cases) {
    g_test = c.name;
    ha.boot(false);
    CHECK_STR(ha.send(c.bytes, 16000), "");
    const std::string got = ha.commit(hash16(c.bytes));
    CHECK_STR(got, c.want);
    if (!got.empty()) continue;
    // Whatever is accepted is drawn, with and without a value: under ASan
    // this is the overflow check.
    CHECK(advance_until([&] { return hash16(c.bytes) == tab::active_hash(); }, 3000));
    CHECK(widget(1) && rect_of(widget(1)->r) == c.card);
    CHECK(widget(1) && rect_of(widget(1)->hr) == (R{0, 790, 10, 10}));
    CHECK(widget(2) && rect_of(widget(2)->r) == c.thermo);
    hh::refresh();
    CHECK_STR(ha.set("{\"h\":\"" + hash16(c.bytes) + "\",\"v\":[[1,20]]}"), "");
    hh::advance(20);
    hh::refresh();
  }
  g_test = "rects_cut_to_screen";
}

// read_slots once walked the whole slots text for every slot when it held
// escapes: O(n^2) inside tab_commit, seconds for a 258 KB text. Now each
// character is decoded once, and the answers keep the order parse, version,
// limits (contract 5).
TEST(escaped_slots_are_fast) {
  auto body = [](const std::string &slots) { return "{\"pages\":[{\"w\":[]}],\"slots\":\"" + slots + "\",\"v\":1}"; };
  auto escaped = [](size_t n) {
    std::string s;
    s.reserve(n * 6);
    for (size_t i = 0; i < n; i++) s += "\\u0074";
    return s;
  };
  struct Case {
    const char *name;
    std::string bytes;
    const char *want;
  };
  const std::vector<Case> cases = {
      {"43000 escaped letters", body(escaped(43000)), "limits"},
      {"1025 escaped letters", body(escaped(1025)), "limits"},
      {"1024 escaped letters", body(escaped(1024)), ""},
      {"1024 plain letters", body(std::string(1024, 't')), ""},
      {"escaped wrong letter", body(escaped(3) + "\\u0078"), "parse"},
      {"escaped mixed", body("t\\u0062n\\u006cn"), ""},
      {"43000 escaped, bad page", "{\"pages\":[{\"w\":7}],\"slots\":\"" + escaped(43000) + "\",\"v\":1}", "parse"},
      {"43000 escaped, wrong last letter", body(escaped(42999) + "\\u0078"), "parse"},
      {"43000 escaped, newer version", "{\"pages\":[],\"slots\":\"" + escaped(43000) + "\",\"v\":3}", "version"},
      {"2000 escaped, slot 1500 used",
       "{\"pages\":[{\"title\":\"P\",\"w\":[{\"id\":1,\"r\":[100,20,376,56],\"t\":\"card\",\"w\":[{\"id\":2,"
       "\"r\":[116,30,300,28],\"size\":1,\"t\":\"label\",\"text\":{\"s\":1500}}]}]}],\"slots\":\"" +
           escaped(2000) + "\",\"v\":1}",
       "limits"},
      {"43000 plain letters", body(std::string(43000, 't')), "limits"},
  };
  for (const Case &c : cases) {
    g_test = c.name;
    D::Loader ld;
    const auto t0 = std::chrono::steady_clock::now();
    const char *code = D::model_check(c.bytes.data(), c.bytes.size(), ld);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK_STR(code, c.want);
    CHECK_MSG(ms < 50, std::to_string(ms) + " ms");
  }
  g_test = "escaped_slots_are_fast";
  // The decoded types are the slot types, read once per slot.
  const std::string mixed = body("t\\u0062n\\u006cn");
  D::Loader ld;
  CHECK_STR(D::model_check(mixed.data(), mixed.size(), ld), "");
  CHECK(ld.nslots == 5 && ld.slot_type_of(1) == 't' && ld.slot_type_of(2) == 'b' && ld.slot_type_of(3) == 'n' &&
        ld.slot_type_of(4) == 'l' && ld.slot_type_of(5) == 'n' && ld.slot_type_of(6) == 0);
  // A bound field that names an escaped slot is checked against its type.
  Ha ha;
  ha.boot(false);
  const std::string bound =
      "{\"pages\":[{\"title\":\"P\",\"w\":[{\"id\":1,\"r\":[100,20,376,56],\"t\":\"card\",\"w\":[{\"id\":2,"
      "\"r\":[116,30,300,28],\"size\":1,\"t\":\"label\",\"text\":{\"s\":2}}]}]}],\"slots\":\"\\u0062\\u0074\",\"v\":1}";
  CHECK_STR(ha.send(bound, 16000), "");
  CHECK_STR(ha.commit(hash16(bound)), "");
  const std::string wrong = std::string(bound).replace(bound.find("{\"s\":2}"), 7, "{\"s\":1}");
  CHECK_STR(ha.send(wrong, 16000), "");
  CHECK_STR(ha.commit(hash16(wrong)), "parse");
}

TEST(hostile_numbers) {
  const std::string A = layout_bytes("layout_all_cards.dl.json");
  auto sub = [&](const std::string &from, const std::string &to) {
    std::string s = A;
    const size_t p = s.find(from);
    CHECK_MSG(p != std::string::npos, from);
    if (p != std::string::npos) s.replace(p, from.size(), to);
    return s;
  };
  struct Case {
    const char *name;
    std::string bytes;
    const char *want;
  };
  const std::string lim = "\"max\":100,\"min\":0,\"r\":[116,368";
  const std::vector<Case> cases = {
      {"rect 1e999", sub("[100,20,376,56]", "[1e999,20,376,56]"), "parse"},
      {"rect 70 digits", sub("[100,20,376,56]", "[" + std::string(70, '1') + ",20,376,56]"), "parse"},
      {"rect 100 and 62 zeros", sub("[100,20,376,56]", "[100." + std::string(62, '0') + ",20,376,56]"), "parse"},
      {"rect 1e9", sub("[100,20,376,56]", "[1000000000,20,376,56]"), "parse"},
      {"tap go 1e300", sub("\"tap\":{\"go\":2}", "\"tap\":{\"go\":1e300}"), "parse"},
      {"tap go -1e300", sub("\"tap\":{\"go\":2}", "\"tap\":{\"go\":-1e300}"), "parse"},
      {"icon 1e300", sub("\"icon\":983861", "\"icon\":1e300"), "parse"},
      {"slot 1e300", sub("\"text\":{\"s\":3}", "\"text\":{\"s\":1e300}"), "parse"},
      {"nav icon 1e300", sub("\"icon\":984714,\"p\":2", "\"icon\":1e300,\"p\":2"), "parse"},
      {"slider limits 1e999", sub(lim, "\"max\":1e999,\"min\":-1e999,\"r\":[116,368"), "parse"},
      {"chart const 1e999", sub("\"value\":{\"s\":22}", "\"value\":[1e999,-1e999,0]"), "parse"},
      {"thermostat const 1e999", sub("\"value\":{\"s\":36}", "\"value\":1e999"), "parse"},
      {"slider limits 1e308", sub(lim, "\"max\":1e308,\"min\":-1e308,\"r\":[116,368"), ""},
      {"chart const 1e308", sub("\"value\":{\"s\":22}", "\"value\":[1e308,-1e308]"), ""},
      {"bars const 1e308", sub("\"value\":{\"s\":27}", "\"value\":[1e308,-1e308,5]"), ""},
      {"thermostat const 1e300", sub("\"value\":{\"s\":36}", "\"value\":1e300"), ""},
  };
  Ha ha;
  for (const Case &c : cases) {
    g_test = c.name;
    ha.boot(false);
    CHECK_STR(ha.send(c.bytes, 16000), "");
    CHECK_STR(ha.commit(hash16(c.bytes)), c.want);
    if (*c.want) continue;
    CHECK(advance_until([&] { return hash16(c.bytes) == tab::active_hash(); }, 3000));
    std::string values = state_payload(0);
    const size_t at = values.find("4384f665e4dd9f62");
    if (CHECK(at != std::string::npos)) values.replace(at, 16, hash16(c.bytes));
    CHECK_STR(ha.set(values), "");
    for (int p = 0; p < tab::page_count(); p++) {
      tab::go_to(p);
      hh::advance(10);
      hh::refresh();
    }
  }
  g_test = "hostile_numbers";
  // The parser itself.
  double v = 0;
  for (const char *bad : {"1e999", "-1e999", "1e400"}) CHECK_MSG(!json_num(bad, &v), bad);
  CHECK(!json_num(("1" + std::string(70, '1')).c_str(), &v));
  CHECK(json_num("1e308", &v) && v == 1e308);  // finite: the reader keeps it, the model holds it to 1e9
  {
    // Skipping a long number (an unknown key) still works.
    const std::string t = "[" + std::string(70, '1') + "]";
    tab::json::Reader r(t.data(), t.size());
    CHECK(r.skip() && r.at_end());
  }
  // tab_set: held to +-1e9, or skipped when not finite.
  ha.boot(false);
  CHECK(ha.show(A));
  CHECK_STR(ha.set(state_payload(0)), "");
  const std::string H = "\"h\":\"4384f665e4dd9f62\"";
  double before16 = 0;
  CHECK(tab::slot_number(16, &before16));
  CHECK_STR(ha.set("{" + H + ",\"v\":[[22,[1e308,-1e308]],[27,[1e308,-1e308,3]],[16,1e300],[18,1e300],[17,-1e300]]}"), "");
  double l[4] = {0, 0, 0, 0};
  CHECK(tab::slot_list(22, l, 4) == 2 && l[0] == 1e9 && l[1] == -1e9);
  CHECK(tab::slot_list(27, l, 4) == 3 && l[0] == 1e9 && l[1] == -1e9 && l[2] == 3);
  double x = 0;
  CHECK(tab::slot_number(16, &x) && x == 1e9);
  CHECK(tab::slot_number(18, &x) && x == 1e9);
  CHECK(tab::slot_number(17, &x) && x == -1e9);
  for (int p = 0; p < tab::page_count(); p++) {
    tab::go_to(p);
    hh::advance(10);
    hh::refresh();
  }
  CHECK_STR(ha.set("{" + H + ",\"v\":[[22,[1e999,-1e999,0]],[16,1e999],[36,-1e999],[8," + std::string(80, '1') +
                       "]]}"),
            "");
  CHECK(tab::slot_list(22, l, 4) == 2 && l[0] == 1e9);  // unchanged
  CHECK(tab::slot_number(16, &x) && x == 1e9);
  double s8 = 0, s36 = 0;
  CHECK(tab::slot_number(8, &s8) && tab::slot_number(36, &s36));
  CHECK(s8 - s8 == 0 && s36 - s36 == 0);
  for (int p = 0; p < tab::page_count(); p++) {
    tab::go_to(p);
    hh::advance(10);
    hh::refresh();
  }
  // The thermostat plus button with limits at 1e9 sends a number in range.
  tab::go_to(0);
  hh::advance(20);
  CHECK_STR(ha.set("{" + H + ",\"v\":[[16,21],[17,-1e300],[18,1e300],[19,1e300]]}"), "");
  CHECK(!D::fraction(NAN, 0, 1) && D::fraction(5, NAN, 10) == 0 && D::px_round(NAN) == 0 && D::px_round(1e300) == 1000000);
}

// ================================================================ boot, build failure, touch knock

// A stored layout with n thermostats (6 objects each), inside every limit.
static std::string many_thermostats(int n) {
  std::string w;
  for (int i = 0; i < n; i++) {
    const int x = 100 + (i % 12) * 96, y = 20 + ((i / 12) % 8) * 96;
    if (i) w += ",";
    w += "{\"change\":{\"p\":0},\"id\":" + std::to_string(i + 2) + ",\"r\":[" + std::to_string(x) + "," +
         std::to_string(y) + ",90,90],\"t\":\"thermostat\",\"value\":21}";
  }
  return "{\"pages\":[{\"title\":\"P\",\"w\":[{\"id\":1,\"r\":[100,20,1164,764],\"t\":\"card\",\"w\":[" + w +
         "]}]}],\"slots\":\"\",\"v\":1}";
}

// The stored layout is not built inside begin(): on_boot runs before the main
// loop, with nobody feeding the watchdog, and building without LVGL's layout
// pass in between slows with the square of the object count. begin() only
// stages it; the timer slices build and show it behind the waiting screen.
TEST(boot_builds_in_slices) {
  const std::string big = many_thermostats(1650);  // about 9,900 objects
  if (!CHECK_MSG(big.size() <= D::CACHE_LIMIT, std::to_string(big.size()) + " bytes")) return;
  shutdown();
  reset_platform();
  fake::st.flash_has = true;
  fake::st.flash.assign(big.begin(), big.end());
  const auto t0 = std::chrono::steady_clock::now();
  tab::begin(g_fonts, fake::platform());
  const double begin_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  // Nothing is built yet: staged, waiting screen up, the cache marker set.
  CHECK_STR(tab::active_hash(), "");
  CHECK(tab::building());
  CHECK(!hidden(D::g.waiting));
  D::Layout *s = D::staged_layout();
  if (!CHECK(s && s->state == D::L_BUILDING && s->b_stage == 0 && s->root == nullptr)) return;
  CHECK_STR(D::g.stored, hash16(big));
  CHECK_MSG(begin_ms < (QUICK ? 400 : 100), std::to_string(begin_ms) + " ms in begin()");
  // Every slice stays within the object budget, and the layout comes up.
  uint32_t worst = 0, slices = 0;
  const auto t1 = std::chrono::steady_clock::now();
  for (int i = 0; i < 2000 && std::string(tab::active_hash()).empty(); i++) {
    const uint32_t before = count_objects(D::g.base);
    hh::advance(D::TICK_MS);
    const uint32_t after = count_objects(D::g.base);
    if (after > before) {
      slices++;
      if (after - before > worst) worst = after - before;
    }
  }
  const double build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
  CHECK_STR(tab::active_hash(), hash16(big));
  CHECK(hidden(D::g.waiting));
  CHECK_MSG(worst <= (uint32_t) D::SLICE_OBJECTS + 8, std::to_string(worst) + " objects in one slice");
  CHECK(slices > 100);
  printf("  %zu bytes, %u objects: begin() %.1f ms, built in %u slices, %.0f ms\n", big.size(),
         (unsigned) count_objects(D::g.base), begin_ms, (unsigned) slices, build_ms);
  // Showing the stored copy writes nothing.
  hh::advance(31000);
  CHECK(fake::st.save_calls == 0 && fake::st.erase_calls == 0);
  // A hello while the stored copy is still building names no layout; HA then
  // sends its own, and that one wins.
  Ha ha;
  shutdown();
  fake::st.live.clear();
  tab::begin(g_fonts, fake::platform());
  CHECK(tab::building());
  const std::string small = layout_bytes("layout_small.dl.json");
  CHECK(ha.hello());
  CHECK(ha.show(small));
  CHECK_STR(tab::active_hash(), "a9326c033e45f2f4");
  // A stored copy whose build fails leaves the waiting screen and the copy.
  shutdown();
  reset_platform();
  fake::st.flash_has = true;
  fake::st.flash.assign(small.begin(), small.end());
  tab::begin(g_fonts, fake::platform());
  D::test_objects_left = 5;
  hh::advance(500);
  D::test_objects_left = -1;
  CHECK_STR(tab::active_hash(), "");
  CHECK_STR(status_build(), "build");
  CHECK(!hidden(D::g.waiting) && fake::st.flash_has);
}

// LVGL cannot make an object: the build stops with "build" (contract 5),
// whatever object it was, and the layout on screen stays.
TEST(build_failure_is_reported) {
  Ha ha;
  const std::string small = layout_bytes("layout_small.dl.json");
  const std::string all = layout_bytes("layout_all_cards.dl.json");
  ha.boot(false);
  CHECK(ha.hello());
  CHECK(ha.show(small));
  const uint32_t whole = [&] {
    // How many objects the all-cards layout makes.
    CHECK(ha.show(all));
    const uint32_t n = active() ? count_objects(active()->root) : 0;
    CHECK(ha.show(small));
    hh::advance(2000);
    return n;
  }();
  CHECK(whole > 100);
  int failed = 0;
  for (uint32_t allow = 0; allow < whole; allow += (QUICK ? 7 : 1)) {
    CHECK_STR(ha.send(all), "");
    D::test_objects_left = (int32_t) allow;
    CHECK_STR(ha.commit("4384f665e4dd9f62"), "");
    const bool ended = advance_until([] { return !tab::building(); }, 3000);
    const bool refused = D::test_objects_left == 0;
    D::test_objects_left = -1;
    if (!CHECK_MSG(ended, "allow " + std::to_string(allow))) break;
    if (refused && std::string(tab::active_hash()) == "a9326c033e45f2f4") {
      failed++;
      if (status_build() != "build") CHECK_MSG(false, "allow " + std::to_string(allow) + ": " + status_build());
    } else {
      // Every object was made before the limit: the layout came up.
      CHECK_MSG(std::string(tab::active_hash()) == "4384f665e4dd9f62", "allow " + std::to_string(allow));
      CHECK(ha.show(small));
    }
    hh::advance(300);  // the failed layout's objects are deleted
  }
  CHECK(failed > (QUICK ? 10 : 100));
  // The tablet still works afterwards.
  CHECK(ha.show(all));
  CHECK_STR(status_build(), "");
  // A heading with an icon is three objects, and is counted as three.
  D::Widget heading{};
  heading.kind = D::W_HEADING;
  CHECK(D::Loader::objects_of(heading) == 3);
}

// A touch that finds no session, on a tablet that is connected and paired,
// knocks at once and starts the knock schedule again (contract 2.2).
TEST(touch_without_session_knocks) {
  Ha ha;
  ha.boot(true);
  CHECK(ha.hello());
  CHECK(ha.show(layout_bytes("layout_all_cards.dl.json")));
  CHECK_STR(ha.set(state_payload(0)), "");
  hh::advance(20000);
  // The tablet loses its session; HA has greeted on this connection, so the
  // schedule is quiet.
  D::g.session = false;
  fake::st.events.clear();
  hh::advance(20000);
  CHECK(fake::events("tab1_hello").empty());
  D::Widget *sw = widget(3);
  if (!CHECK(sw)) return;
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(fake::events("tab1_input").empty());
  CHECK(fake::events("tab1_hello").size() == 1);
  // More touches within 3 s send no more knocks; the schedule goes on.
  hh::advance(700);
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  hh::advance(300);
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(fake::events("tab1_hello").size() == 1);
  hh::advance(2500);
  CHECK(fake::events("tab1_hello").size() == 2);
  // HA greets: the next touch is sent and nothing knocks.
  CHECK(ha.hello());
  fake::st.events.clear();
  hh::advance(4000);
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(fake::events("tab1_input").size() == 1);
  CHECK(fake::events("tab1_hello").empty());
  // Not paired, or no connection: the strip only, no knock.
  ha.boot(false);
  CHECK(ha.hello());
  CHECK(ha.show(layout_bytes("layout_all_cards.dl.json")));
  CHECK_STR(ha.set(state_payload(0)), "");
  hh::advance(20000);
  fake::st.events.clear();
  sw = widget(3);
  if (CHECK(sw)) hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(fake::events("tab1_hello").empty() && fake::events("tab1_input").empty());
}

// Contract 8: a tab_set without an `h` text is `parse`; an `h` that names no
// layout here is `hash`. HA greets again after `hash` only.
TEST(set_without_hash_is_parse) {
  Ha ha;
  ha.boot(false);
  CHECK(ha.hello());
  CHECK(ha.show(layout_bytes("layout_small.dl.json")));
  CHECK_STR(ha.set("{\"v\":[]}"), "parse");
  CHECK_STR(ha.set("{\"h\":5,\"v\":[]}"), "parse");
  CHECK_STR(ha.set("{\"h\":null,\"v\":[]}"), "parse");
  CHECK_STR(ha.set("{\"h\":\"a9326c033e45f2f4\",\"v\":7}"), "parse");
  CHECK_STR(ha.set("{\"h\":\"\",\"v\":[]}"), "hash");
  CHECK_STR(ha.set("{\"h\":\"0000000000000000\",\"v\":[]}"), "hash");
  CHECK_STR(ha.set("{\"h\":\"a9326c033e45f2f4\",\"v\":[]}"), "");
  CHECK_STR(ha.set("{\"h\":\"a9326c033e45f2f4\"}"), "");
}

// Contract 5: a chunk is at most min(16000, caps.max_chunk) bytes. A bigger
// one answers `size`: at seq 0 before anything is allocated, after seq 0 it
// ends the transfer. A chunk of exactly 16000 bytes is fine.
TEST(chunk_over_limit_is_size) {
  const std::string big = gen_cards(6, 40, 2, "chunk");
  const int size = (int) big.size();
  if (!CHECK_MSG(size > 2 * 16000 + 1 && size < 262144, std::to_string(size))) return;
  const std::string h = hash16(big);
  Ha ha;
  ha.boot(false);
  // The whole layout as one seq 0 chunk.
  const uint64_t allocs = fake::st.allocs;
  CHECK_STR(tab::on_layout(h.data(), 16, 0, 1, size, big.data(), big.size()), "size");
  CHECK(fake::st.allocs == allocs && fake::st.live.empty());
  CHECK_STR(tab::on_commit(h.data(), 16, "", 0), "incomplete");
  // One byte over at seq 0.
  CHECK_STR(tab::on_layout(h.data(), 16, 0, 3, size, big.data(), 16001), "size");
  CHECK(fake::st.allocs == allocs && fake::st.live.empty());
  // 16000 bytes, then a later chunk over the limit: size, and the transfer is gone.
  CHECK_STR(tab::on_layout(h.data(), 16, 0, 2, size, big.data(), 16000), "");
  CHECK(fake::st.live.size() == 1);
  CHECK_STR(tab::on_layout(h.data(), 16, 1, 2, size, big.data() + 16000, (size_t) size - 16000), "size");
  CHECK(fake::st.live.empty() && !fake::st.bad_release);
  CHECK_STR(tab::on_layout(h.data(), 16, 1, 2, size, big.data() + 16000, 16000), "order");
  CHECK_STR(tab::on_commit(h.data(), 16, "", 0), "incomplete");
  // The layout in 16000-byte chunks goes through and shows.
  CHECK_STR(ha.send(big, 16000), "");
  CHECK_STR(ha.commit(h), "");
  CHECK(advance_until([&] { return h == tab::active_hash(); }, 5000));
}

// Contract 4.7, button: with the icon beside the text, the text box gets the
// room beside the glyph that is drawn (W - 12 - icon - 6). An icon the font
// lacks draws the fallback, so the room is taken from the fallback's width
// too, as the text is cut.
TEST(button_icon_room_uses_drawn_glyph) {
  static const uint32_t MISSING = 0x10FFFF;
  const std::string text = "Kitchen lights are all on now";
  const std::string dl =
      "{\"nav\":[],\"pages\":[{\"title\":\"P\",\"w\":[{\"id\":1,\"r\":[100,40,600,400],\"t\":\"card\",\"w\":["
      "{\"icon\":" + std::to_string(MISSING) + ",\"id\":2,\"r\":[120,60,220,48],\"t\":\"button\",\"tap\":{\"p\":1},"
      "\"text\":\"" + text + "\"},"
      "{\"icon\":984024,\"id\":3,\"r\":[120,120,220,48],\"t\":\"button\",\"tap\":{\"p\":1},\"text\":\"" + text + "\"},"
      "{\"icon\":" + std::to_string(MISSING) + ",\"id\":4,\"r\":[120,180,220,72],\"t\":\"button\",\"tap\":{\"p\":1},"
      "\"text\":\"" + text + "\"}"
      "]}]}],\"screen\":[1280,800],\"theme\":{\"bw\":1,\"card\":1580067,\"r\":16,\"r2\":10,\"tx\":15264493,"
      "\"wall\":1053720},\"v\":1}";
  Ha ha;
  ha.boot(false);
  if (!CHECK(ha.show(dl))) return;
  const lv_font_t *ifont = D::icon_font(24);
  CHECK(!D::has_glyph(ifont, MISSING) && D::has_glyph(ifont, D::ICON_FALLBACK) && D::has_glyph(ifont, 984024));
  for (uint32_t id : {2u, 3u}) {
    D::Widget *b = widget(id);
    if (!CHECK(b && b->o1 && b->o2)) continue;
    const uint32_t drawn = id == 2 ? D::ICON_FALLBACK : 984024u;
    const int iw = lv_font_get_glyph_width(ifont, drawn, 0);
    const int W = b->r[2];
    lv_obj_update_layout(b->obj);
    const int x = (int) lv_obj_get_x(b->o2), w = (int) lv_obj_get_width(b->o2);
    CHECK_MSG(w == W - 12 - iw - 6, std::to_string(id) + ": width " + std::to_string(w));
    CHECK_MSG((int) lv_obj_get_x(b->o1) + iw + 6 == x, std::to_string(id));  // icon, gap 6, text
    CHECK(shows_text(b->o2, text, true));
    const std::string got = label_text(b->o2);
    const int tw = D::text_width(D::text_font(15), got.c_str(), got.size());
    CHECK_MSG(tw <= w && x + tw <= W - 6, std::to_string(id) + ": text ends at " + std::to_string(x + tw));
  }
  // Stacked (height 64 or more): the text has the full width, whatever the icon.
  D::Widget *st = widget(4);
  if (CHECK(st && st->o2)) {
    lv_obj_update_layout(st->obj);
    CHECK((int) lv_obj_get_width(st->o2) == st->r[2] - 12);
  }
}

// Contract 7.4: a forced swap (a finger down past 3 s) ends a slider drag as
// if it were released then. The value goes out once, signed, with the layout
// the drag started on (7.1). A slider that asks first sends nothing and the
// strip says so; without a session the strip says why, as for any touch.
TEST(forced_swap_ends_a_drag) {
  const std::string small = layout_bytes("layout_small.dl.json");
  const std::string hs = "a9326c033e45f2f4", hall = "4384f665e4dd9f62";
  InputRig rig;
  rig.up();
  D::Widget *s = widget(10);  // [116, 368, 344, 40], 0..100, change p0
  if (!CHECK(s)) return;
  const int y = centre_y(s->r);
  hh::press(s->r[0] + 40, y);
  hh::move(s->r[0] + 172, y);  // half way: 50
  CHECK_STR(rig.ha.send(small), "");
  CHECK_STR(rig.ha.commit(hs), "");
  hh::advance(2000);
  CHECK_STR(tab::active_hash(), hall);  // still waiting for the finger
  CHECK(rig.inputs() == 0);
  hh::advance(1600);
  CHECK_STR(tab::active_hash(), hs);
  auto ev = fake::events("tab1_input");
  CHECK(ev.size() == 1);
  if (!ev.empty()) {
    CHECK_MSG(ev[0].d.find("|" + hall + "|10|c|0|50") != std::string::npos, ev[0].d);
    CHECK(ev[0].has_s && ev[0].s == hmac_hex(rig.ha.secret, 32, ev[0].d, 16));
  }
  CHECK(hidden(D::g.strip) && hidden(D::g.question));
  // The finger lifts later: nothing more is sent, and the ack is harmless.
  hh::move(400, y);
  hh::release();
  hh::advance(50);
  CHECK(rig.inputs() == 1);
  rig.ha.ack(rig.last_counter(), true, "");
  CHECK_STR(tab::active_hash(), hs);

  // A slider that asks first.
  const std::string cf =
      "{\"pages\":[{\"title\":\"P\",\"w\":[{\"id\":1,\"r\":[100,40,600,300],\"t\":\"card\",\"w\":["
      "{\"cf\":1,\"change\":{\"p\":0},\"id\":2,\"max\":100,\"min\":0,\"r\":[120,100,400,40],\"step\":1,"
      "\"t\":\"slider\",\"value\":{\"s\":1}}]}]}],\"slots\":\"n\",\"v\":1}";
  const std::string hcf = hash16(cf);
  CHECK(rig.ha.show(cf));
  CHECK_STR(rig.ha.set("{\"h\":\"" + hcf + "\",\"v\":[[1,10]]}"), "");
  hh::advance(3000);  // the strip from before is gone
  fake::st.events.clear();
  hh::press(140, 120);
  hh::move(320, 120);
  CHECK_STR(rig.ha.send(small), "");
  CHECK_STR(rig.ha.commit(hs), "");
  hh::advance(3600);
  CHECK_STR(tab::active_hash(), hs);
  CHECK(rig.inputs() == 0 && hidden(D::g.question));
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), D::SWAP_DRAG_TEXT);
  hh::release();
  hh::advance(50);
  CHECK(rig.inputs() == 0 && hidden(D::g.question));

  // Home Assistant goes away during the drag: the usual strip, nothing sent.
  CHECK(rig.ha.show(layout_bytes("layout_all_cards.dl.json")));
  CHECK_STR(rig.ha.set(state_payload(0)), "");
  hh::advance(3000);
  fake::st.events.clear();
  s = widget(10);
  if (!CHECK(s)) return;
  hh::press(s->r[0] + 40, y);
  hh::move(s->r[0] + 100, y);
  CHECK_STR(rig.ha.send(small), "");
  CHECK_STR(rig.ha.commit(hs), "");
  fake::st.network = true;  // Home Assistant away, Wi-Fi up
  fake::st.connected = false;
  hh::advance(3600);
  CHECK_STR(tab::active_hash(), hs);
  CHECK(rig.inputs() == 0);
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), "Home Assistant is not connected");
  hh::release();
  hh::advance(50);
  CHECK(rig.inputs() == 0);
}

// Contract 2.1: Home Assistant keeps each tab_set at most 12000 bytes, but the
// tablet takes any size up to one ESPHome message (32768 bytes).
TEST(set_over_12000_bytes_is_accepted) {
  const int slots = 200;
  const std::string dl = "{\"pages\":[{\"title\":\"P\",\"w\":[]}],\"slots\":\"" + std::string(slots, 't') + "\",\"v\":1}";
  const std::string h = hash16(dl);
  Ha ha;
  ha.boot(true);
  CHECK(ha.hello());
  if (!CHECK(ha.show(dl))) return;
  int round = 0;
  for (size_t target : {size_t(12001), size_t(20000), size_t(32000)}) {
    const char fill = (char) ('a' + round++);
    std::string body = "{\"h\":\"" + h + "\",\"v\":[";
    int k = 0;
    while (body.size() + 2 < target && k < slots) {
      if (k) body += ",";
      k++;
      body += "[" + std::to_string(k) + ",\"" + std::string(250, fill) + "\"]";
    }
    body += "]}";
    if (!CHECK_MSG(body.size() > 12000 && body.size() < 32768, std::to_string(body.size()))) continue;
    CHECK_STR(ha.set(body), "");
    char got[300];
    CHECK(tab::slot_text(k, got, sizeof got) && std::string(got) == std::string(250, fill));
  }
}

// The host fonts carry the tablet's metrics: ESPHome's font component draws
// with no kerning, line_height = the font's height and base_line = height -
// ascender. LVGL's own TTF defaults differ (kerning on, other line heights).
TEST(font_metrics_match_the_tablet) {
  const tj::Value fonts = tj::load(CDIR + "/contract.json")["fonts"];
  auto same = [](const lv_font_t *f, const char *family, int px) {
    const hh::DeviceMetric *m = hh::device_metric(family, px);
    return m && f && (int) f->line_height == m->line_height && (int) f->base_line == m->base_line;
  };
  for (size_t i = 0; i < fonts["text_px"].size(); i++) {
    const lv_font_t *f = g_fonts.text[i];
    const std::string px = std::to_string(fonts["text_px"][i].i());
    CHECK_MSG(same(f, "text", fonts["text_px"][i].i()), px);
    CHECK_MSG(lv_font_get_glyph_width(f, 'T', 'o') == lv_font_get_glyph_width(f, 'T', 0), px);
    CHECK_MSG(lv_font_get_glyph_width(f, 'A', 'V') == lv_font_get_glyph_width(f, 'A', 0), px);
  }
  for (size_t i = 0; i < fonts["digits_px"].size(); i++)
    CHECK_MSG(same(g_fonts.digits[i], "digits", fonts["digits_px"][i].i()), std::to_string(fonts["digits_px"][i].i()));
  for (size_t i = 0; i < fonts["icon_px"].size(); i++)
    CHECK_MSG(same(g_fonts.icon[i], "icons", fonts["icon_px"][i].i()), std::to_string(fonts["icon_px"][i].i()));
  // LVGL lays text out with those numbers.
  lv_point_t p;
  lv_text_get_size(&p, "To", g_fonts.text[3], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  CHECK(p.y == 22);
  CHECK(p.x == lv_font_get_glyph_width(g_fonts.text[3], 'T', 0) + lv_font_get_glyph_width(g_fonts.text[3], 'o', 0));
}

// contract.json `fonts` decides the fonts (cards spec B.8): every size has
// its place in tab::Fonts and its font from text_font, digits_font and
// icon_font; the digits fonts hold exactly fonts.digits_glyphs, read as
// UTF-8; a digits font's line fits into its fonts.row_px.
TEST(fonts_follow_the_contract) {
  const tj::Value fonts = tj::load(CDIR + "/contract.json")["fonts"];
  CHECK(fonts["text_px"].size() == 7 && fonts["digits_px"].size() == 3 && fonts["icon_px"].size() == 4);
  Ha ha;
  ha.boot(false);
  for (size_t i = 0; i < fonts["text_px"].size(); i++) {
    const int px = fonts["text_px"][i].i();
    CHECK_MSG(D::text_font(px) == g_fonts.text[i], std::to_string(px));
    const hh::DeviceMetric *m = hh::device_metric("text", px);
    CHECK_MSG(m && D::line_height(D::text_font(px)) == m->line_height, std::to_string(px));
  }
  for (size_t i = 0; i < fonts["digits_px"].size(); i++) {
    const int px = fonts["digits_px"][i].i();
    CHECK_MSG(D::digits_font(px) == g_fonts.digits[i], std::to_string(px));
    const hh::DeviceMetric *m = hh::device_metric("digits", px);
    const tj::Value &row = fonts["row_px"][std::to_string(px).c_str()];
    CHECK_MSG(m && row.t == tj::Value::NUM && m->line_height <= row.i(),
              std::to_string(px) + " px: line " + std::to_string(m ? m->line_height : -1) + ", row " + std::to_string(row.i()));
  }
  for (size_t i = 0; i < fonts["icon_px"].size(); i++)
    CHECK_MSG(D::icon_font(fonts["icon_px"][i].i()) == g_fonts.icon[i], std::to_string(fonts["icon_px"][i].i()));
  // The 28 px text is its own font, not the 34 px one.
  CHECK(D::text_font(28) != D::text_font(34) && D::line_height(D::text_font(28)) == 34);
  // The 48 px digits keep engine 0.1.0's line.
  CHECK(hh::device_metric("digits", 48) && hh::device_metric("digits", 48)->line_height == 59);

  // The digits glyphs, character by character: "°" is U+00B0, two bytes in
  // UTF-8. Taken byte by byte, U+00C2 would be in the font too, and a text
  // the tablet cannot draw would pass here.
  const std::vector<uint32_t> want = hf::utf8_codepoints(fonts["digits_glyphs"].s);
  CHECK(want.size() == 18 && std::find(want.begin(), want.end(), 0xB0u) != want.end());
  for (const lv_font_t *f : g_fonts.digits) {
    for (uint32_t cp : want) CHECK_MSG(D::has_glyph(f, cp), std::to_string(cp));
    CHECK(!D::has_glyph(f, 0xC2) && !D::has_glyph(f, 'A') && !D::has_glyph(f, 0x2013));
  }
  CHECK(hf::utf8_codepoints("1\xC2\xB0") == std::vector<uint32_t>({'1', 0xB0}));
  CHECK(hf::utf8_codepoints("\xE2\x80\x93") == std::vector<uint32_t>({0x2013}));
  CHECK(hf::utf8_codepoints("\xC2").empty() && hf::utf8_codepoints("\xB0").empty() && hf::utf8_codepoints("\xC2" "A").empty());
  // The thermostat keeps its own list: "21:05" is not digits-only for it.
  CHECK(D::thermostat_fit_font("21.5", 400) == g_fonts.digits[0]);
  CHECK(D::thermostat_fit_font("21:05", 400) != g_fonts.digits[0]);
}

// ================================================================ set-up window and erase (design 10.8)

TEST(setup_window_closed_says_restart) {
  shutdown();
  tab::setup_window_closed();  // before the engine runs: nothing happens
  Ha ha;
  ha.boot(false);
  fake::st.have_key = false;  // a new tablet: Home Assistant never gave it a key
  fake::st.hotspot = true;
  hh::advance(D::HINT_POLL_MS + 20);
  CHECK_STR(label_text(D::g.hint), WAIT_HOTSPOT);
  tab::setup_window_closed();
  hh::advance(20);
  CHECK(!hidden(D::g.waiting));
  CHECK_STR(label_text(D::g.headline), T_CLOSED);
  CHECK_STR(label_text(D::g.hint), WAIT_RESTART);
  shot("setup_window_closed");
  // The window is closed for this boot: the hint no longer looks at the
  // network, and a network that comes back changes nothing.
  const int polls = fake::st.network_polls + fake::st.hotspot_polls;
  fake::st.network = true;
  hh::advance(3000);
  CHECK(fake::st.network_polls + fake::st.hotspot_polls == polls);
  CHECK_STR(label_text(D::g.hint), WAIT_RESTART);
}

// The waiting screen during the set-up window says what the tablet still
// needs (design 10.9): Wi-Fi from the set-up hotspot, or Home Assistant
// adding it. The network is read twice a second, never over a dashboard.
TEST(waiting_hint_follows_the_network) {
  Ha ha;
  ha.boot(false);  // no network, hotspot not on yet: a known Wi-Fi is being tried
  fake::st.have_key = false;  // a new tablet, not in Home Assistant yet
  fake::st.connected = false;
  CHECK(!hidden(D::g.waiting));
  CHECK_STR(label_text(D::g.hint), WAIT_CONNECTING);
  shot("waiting_connecting");
  // The hotspot is on (no Wi-Fi known, or 90 s without it): join it from a
  // phone, by the name the YAML gives it (Platform.hotspot).
  fake::st.hotspot = true;
  hh::advance(D::HINT_POLL_MS + 10);
  CHECK_STR(label_text(D::g.hint), WAIT_HOTSPOT);
  shot("waiting_hotspot");
  // On Wi-Fi: Home Assistant has not added it yet. Also while the hotspot is
  // still on right after its page set the Wi-Fi.
  fake::st.network = true;
  hh::advance(D::HINT_POLL_MS + 10);
  CHECK_STR(label_text(D::g.hint), WAIT_ADD);
  shot("waiting_on_wifi");
  fake::st.hotspot = false;
  hh::advance(D::HINT_POLL_MS + 10);
  CHECK_STR(label_text(D::g.hint), WAIT_ADD);
  // Wi-Fi lost again: back to the hotspot once it is on.
  fake::st.network = false;
  fake::st.hotspot = true;
  hh::advance(D::HINT_POLL_MS + 10);
  CHECK_STR(label_text(D::g.hint), WAIT_HOTSPOT);
  // At most one look every HINT_POLL_MS.
  int net = fake::st.network_polls;
  hh::advance(2000);
  CHECK(fake::st.network_polls - net >= 3 && fake::st.network_polls - net <= 5);

  // Over a dashboard with Home Assistant connected nothing reads the network
  // or the hotspot (while it is away, the Wi-Fi notes look: wifi_notes_over_a_dashboard).
  fake::st.network = true;
  fake::st.connected = true;
  CHECK(ha.show(layout_bytes("layout_small.dl.json")));
  CHECK(hidden(D::g.waiting));
  net = fake::st.network_polls;
  int hot = fake::st.hotspot_polls;
  hh::advance(5000);
  CHECK(fake::st.network_polls == net && fake::st.hotspot_polls == hot);
  // tab_clear brings the waiting screen back: the hint is right within one look.
  fake::st.network = false;
  CHECK_STR(ha.clear(), "");
  hh::advance(D::HINT_POLL_MS + 10);
  CHECK(!hidden(D::g.waiting));
  CHECK_STR(label_text(D::g.hint), WAIT_HOTSPOT);

  // The longest hotspot name (32 bytes, wide letters) still fits whole.
  shutdown();
  reset_platform();
  fake::st.hotspot = true;
  fake::st.hotspot_name = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
  start();
  const std::string longest = std::string("Join the Wi-Fi \xE2\x80\x9C") + fake::st.hotspot_name + "\xE2\x80\x9D from a phone";
  CHECK_STR(label_text(D::g.hint), longest);
  shot("waiting_hotspot_longest_name");
  // A platform that has no hotspot name, or cannot tell.
  shutdown();
  reset_platform();
  fake::st.hotspot = true;
  fake::st.hotspot_name = nullptr;
  start();
  CHECK_STR(label_text(D::g.hint), "Join the tablet\xE2\x80\x99s Wi-Fi hotspot from a phone");
  shutdown();
  reset_platform();
  fake::st.network_hook = false;
  fake::st.hotspot_hook = false;
  fake::st.connected = false;
  start();
  CHECK_STR(label_text(D::g.hint), WAIT_RUNNING);
  // Every text fits whole: waiting_screen_states.
}

static lv_obj_t *waiting_label(const std::string &text) {
  for (uint32_t i = 0; D::g.waiting && i < lv_obj_get_child_count(D::g.waiting); i++) {
    lv_obj_t *o = lv_obj_get_child(D::g.waiting, (int32_t) i);
    if (label_text(o) == text) return o;
  }
  return nullptr;
}

static bool text_colour_is(lv_obj_t *o, uint32_t rgb) {
  return o && lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), lv_color_hex(rgb));
}

// The waiting screen has one headline and one next step per state (design
// 10.10, 10.11), each whole at 1280x800 with the engine's fonts, in the
// contract's words (screens.json). The next step is the largest line after
// the headline and as bright: never the smallest or dimmest text on the
// screen. Only the erased state has a second line under it; every other
// state leaves that line empty. A screenshot of every state.
TEST(waiting_screen_states) {
  std::vector<std::string> titles, steps;
  auto says = [&](const std::string &title, const std::string &step, const char *name,
                  const std::string &more = std::string()) {
    CHECK_MSG(!hidden(D::g.waiting), name ? name : step);
    CHECK_MSG(label_text(D::g.headline) == title, label_text(D::g.headline) + ", want " + title);
    CHECK_MSG(label_text(D::g.hint) == step, label_text(D::g.hint) + ", want " + step);
    CHECK_MSG(label_text(D::g.hint2) == more, label_text(D::g.hint2) + ", want " + more);
    titles.push_back(title);
    steps.push_back(step);
    if (!more.empty()) steps.push_back(more);
    if (name) shot(name);
  };
  const uint32_t look = D::HINT_POLL_MS + 10;
  Ha ha;
  // 1. No Wi-Fi, set-up hotspot on (a new tablet: no key yet).
  ha.boot(false);
  fake::st.have_key = false;
  fake::st.connected = false;
  fake::st.hotspot = true;
  hh::advance(look);
  says(T_HOTSPOT, WAIT_HOTSPOT, "waiting_1_hotspot");
  // 2. A known Wi-Fi is tried; the hotspot is not on yet.
  fake::st.hotspot = false;
  hh::advance(look);
  says(T_CONNECTING, WAIT_CONNECTING, "waiting_2_connecting");
  // 3. (a) On Wi-Fi, no key, never erased, Home Assistant not connected: add
  // it in Home Assistant, where it shows up by itself.
  fake::st.network = true;
  hh::advance(look);
  says(T_ADD, WAIT_ADD, "waiting_3_add");
  // 4. (b) Home Assistant connected (here before its first key), and TAB-1
  // has not greeted it within GREET_WAIT_MS: turn it on in the TAB-1 panel.
  fake::st.connected = true;
  hh::advance(D::GREET_WAIT_MS + look);
  says(T_TURN_ON, WAIT_HA, "waiting_4_turn_on");
  // 5. (c) With its key, Home Assistant not connected: it is not running, or
  // it does not list the tablet.
  fake::st.have_key = true;
  fake::st.connected = false;
  hh::advance(look);
  says(T_WAITING, WAIT_RUNNING, "waiting_5_home_assistant");
  // 6. (s) Home Assistant connected with the key, TAB-1 has not greeted it yet.
  fake::st.connected = true;
  hh::advance(look);
  says(T_STARTING, WAIT_STARTING, "waiting_6_waiting_for_tab1");
  // 7. (g) Greeted: the dashboard is on its way, or may be (HINT_PICK_MS).
  CHECK(ha.hello());
  hh::advance(10);
  says(T_LOADING, WAIT_LOADING, "waiting_7_getting_the_dashboard");
  // 8. (d) Greeted, no dashboard after HINT_PICK_MS: nothing to show (the
  // panel says why: none made, none picked, or too big for the tablet).
  hh::advance(D::HINT_PICK_MS - 600);
  says(T_LOADING, WAIT_LOADING, nullptr);
  hh::advance(600 + look);
  says(T_PICK, WAIT_PICK, "waiting_8_nothing_to_show");
  // A dashboard on its way: "Getting the dashboard…" however long it takes,
  // then the dashboard covers it.
  ha.boot(true);
  fake::st.network = true;
  hh::advance(look);
  CHECK(ha.hello());
  const std::string small = layout_bytes("layout_small.dl.json");
  const std::string hash = sha_hex(small).substr(0, 16);
  const size_t half = small.size() / 2;
  CHECK_STR(tab::on_layout(hash.data(), 16, 0, 2, (int) small.size(), small.data(), half), "");
  hh::advance(D::HINT_PICK_MS + 1000);
  says(T_LOADING, WAIT_LOADING, nullptr);
  CHECK_STR(tab::on_layout(hash.data(), 16, 1, 2, (int) small.size(), small.data() + half, small.size() - half), "");
  CHECK_STR(ha.commit(hash), "");
  hh::advance(D::HINT_PICK_MS + 1000);
  CHECK(hidden(D::g.waiting) && hash == tab::active_hash());
  // tab_clear: nothing to show, within one tick.
  CHECK_STR(ha.clear(), "");
  hh::advance(10);
  says(T_PICK, WAIT_PICK, nullptr);
  // A new dashboard after it: on its way, then on screen.
  CHECK_STR(ha.send(small), "");
  hh::advance(look);
  says(T_LOADING, WAIT_LOADING, nullptr);
  CHECK_STR(ha.commit(hash), "");
  CHECK(advance_until([&] { return hash == tab::active_hash(); }, 3000));
  CHECK_STR(ha.clear(), "");
  hh::advance(10);
  // Home Assistant away: waiting for it. Back, it greets and still has no
  // dashboard for the tablet (it sends no second tab_clear): at once.
  fake::st.connected = false;
  hh::advance(look);
  says(T_WAITING, WAIT_RUNNING, nullptr);
  fake::st.connected = true;
  hh::advance(20);
  CHECK(ha.hello());
  hh::advance(look);
  says(T_PICK, WAIT_PICK, nullptr);
  // 9. (e) After the hold-the-screen erase (the erase mark), on Wi-Fi, no
  // key: confirm it in Home Assistant's Repairs, never "add" a device.
  shutdown();
  reset_platform();
  fake::st.erased_mark = true;
  fake::st.have_key = false;
  fake::st.network = true;
  fake::st.connected = false;
  start();
  hh::advance(look);
  says(T_ERASED, WAIT_ERASED, "waiting_9_erased", WAIT_ERASED2);
  // 10. The set-up window closed without a key.
  ha.boot(false);
  fake::st.have_key = false;
  fake::st.network = true;
  hh::advance(look);
  tab::setup_window_closed();
  hh::advance(20);
  says(T_CLOSED, WAIT_RESTART, "waiting_10_setup_not_finished");
  // 11. The window closed on a tablet with its key and no Wi-Fi: off and on,
  // then join the hotspot it brings back.
  ha.boot(false);
  hh::advance(look);
  tab::setup_window_closed();
  hh::advance(20);
  says(T_NO_WIFI, WAIT_OFF_ON, "waiting_11_no_wifi");
  // Eleven states, eleven headlines.
  CHECK(std::set<std::string>(titles.begin(), titles.end()).size() == 11);

  // A tablet turned on before, with its key, connected but not greeted (Home
  // Assistant starting, or its ESPHome device deleted and added again, or
  // TAB-1 not set up): waiting for TAB-1, whose step covers both, for
  // TURNED_ON_WAIT_MS; then turn it on, as the panel's new-tablet row says.
  ha.boot(true);
  fake::st.network = true;
  hh::advance(look);
  says(T_STARTING, WAIT_STARTING, nullptr);
  hh::advance(D::TURNED_ON_WAIT_MS - 1000);
  says(T_STARTING, WAIT_STARTING, nullptr);
  hh::advance(1000 + look);
  says(T_TURN_ON, WAIT_HA, "waiting_4b_turn_on_paired");

  // Fonts and colours: the headline 34 px, the next step 20 px, both in the
  // text colour; the tablet's name 16 px in the dimmer colour.
  lv_obj_t *name = waiting_label(fake::NAME);
  if (!CHECK(name)) return;
  CHECK(font_of(D::g.headline) == g_fonts.text[6] && font_of(D::g.hint) == g_fonts.text[4]);
  CHECK(font_of(D::g.hint2) == g_fonts.text[4]);
  CHECK(font_of(name) == g_fonts.text[2]);
  CHECK(text_colour_is(D::g.headline, D::DEF_TX) && text_colour_is(D::g.hint, D::DEF_TX));
  CHECK(text_colour_is(D::g.hint2, D::DEF_TX));
  CHECK(text_colour_is(name, D::DEF_TX2));
  CHECK(D::contrast(D::DEF_TX, D::DEF_WALL) > D::contrast(D::DEF_TX2, D::DEF_WALL));
  // Top to bottom without overlap, inside the screen.
  lv_obj_t *order[] = {lv_obj_get_child(D::g.waiting, 0), name, D::g.headline, D::g.hint, D::g.hint2};
  int32_t bottom = 0;
  for (lv_obj_t *o : order) {
    lv_obj_update_layout(o);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    CHECK_MSG(a.y1 >= bottom && a.x1 >= 0 && a.x2 < hh::W && a.y2 < hh::H, rect_text(o));
    bottom = a.y2 + 1;
  }
  // Every headline and step whole with room to spare (1088 px), also the
  // longest hotspot name (32 wide letters) to join; no Home Assistant jargon.
  steps.push_back(std::string("Join the Wi-Fi \xE2\x80\x9C") + std::string(32, 'W') + "\xE2\x80\x9D from a phone");
  steps.push_back(D::HINT_HOTSPOT.step);  // a platform without the hotspot's name
  steps.push_back(D::HINT_NO_WIFI.step);
  steps.push_back(WAIT_ERASED_LONG);  // the erased step for a name too long for its line
  // ... and with the real firmware's name, "Apollo TAB-1" and six MAC characters
  steps.push_back(screen_words(SCREENS["waiting"]["erased"]["step"], "", "Apollo TAB-1 12ee0e"));
  for (int k = 0; k < 2; k++)
    for (const std::string &t : k == 0 ? titles : steps) {
      const int w = D::text_width(g_fonts.text[k == 0 ? 6 : 4], t.c_str(), t.size());
      CHECK_MSG(w <= hh::W - 192, t + " is " + std::to_string(w) + " px");
      for (const char *jargon : {"integration", "entity", "config entry", "ESPHome", "API"})
        CHECK_MSG(t.find(jargon) == std::string::npos, t);
    }
  CHECK(titles.size() == 20 && steps.size() == 26);
  // The "off and on" step with a hotspot name of 32 wide letters is cut with
  // "…" (it would not fit); every name up to 20 letters fits whole.
  shutdown();
  reset_platform();
  fake::st.hotspot_name = "WWWWWWWWWWWWWWWWWWWW";
  start();
  tab::setup_window_closed();
  hh::advance(20);
  CHECK_STR(label_text(D::g.hint), std::string("Turn the tablet off and on, then join \xE2\x80\x9C") + fake::st.hotspot_name +
                                       "\xE2\x80\x9D from a phone");
  shutdown();
  reset_platform();
  fake::st.hotspot_name = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
  start();
  tab::setup_window_closed();
  hh::advance(20);
  CHECK(shows_text(D::g.hint, std::string("Turn the tablet off and on, then join \xE2\x80\x9C") + fake::st.hotspot_name +
                                  "\xE2\x80\x9D from a phone",
                   true));
  // Without a hotspot name: the words of a platform that cannot tell.
  shutdown();
  reset_platform();
  fake::st.hotspot_name = nullptr;
  start();
  tab::setup_window_closed();
  hh::advance(20);
  CHECK_STR(label_text(D::g.hint), D::HINT_NO_WIFI.step);
}

// The waiting screen follows what Home Assistant does right now (design
// 10.11): whether it is connected to the tablet and whether TAB-1 greeted it
// on that connection, whatever the tablet stored. Never on the touch path.
TEST(waiting_screen_follows_home_assistant) {
  const uint32_t look = D::HINT_POLL_MS + 10;
  auto says = [](const std::string &title, const std::string &step) {
    return CHECK_MSG(!hidden(D::g.waiting) && label_text(D::g.headline) == title && label_text(D::g.hint) == step,
                     label_text(D::g.headline) + " / " + label_text(D::g.hint) + ", want " + title);
  };
  Ha ha;

  // A new tablet, as Home Assistant adds it. No key, not connected: add it.
  ha.boot(false);
  fake::st.have_key = false;
  fake::st.connected = false;
  fake::st.network = true;
  hh::advance(look);
  says(T_ADD, WAIT_ADD);
  // Home Assistant's connection before it gives the key: the line stays for
  // GREET_WAIT_MS (the key comes at once), then turn it on.
  fake::st.connected = true;
  hh::advance(D::GREET_WAIT_MS - 600);
  says(T_ADD, WAIT_ADD);
  hh::advance(600 + look);
  says(T_TURN_ON, WAIT_HA);
  // It has its key; ESPHome connects again with it. Away: waiting for Home
  // Assistant. Back: waiting for TAB-1 for GREET_WAIT_MS (never turned on).
  fake::st.have_key = true;
  fake::st.connected = false;
  hh::advance(look);
  says(T_WAITING, WAIT_RUNNING);
  fake::st.connected = true;
  hh::advance(look);
  says(T_STARTING, WAIT_STARTING);
  hh::advance(D::GREET_WAIT_MS - 600 - look);
  says(T_STARTING, WAIT_STARTING);
  hh::advance(600 + look);
  says(T_TURN_ON, WAIT_HA);
  // An admin turns it on: TAB-1 greets it, and "Ready to turn on" goes at
  // the next tick. It pairs it (which ends the session) and greets it again:
  // waiting for TAB-1 in between, never "Ready to turn on" again.
  CHECK(ha.hello());
  hh::advance(10);
  says(T_LOADING, WAIT_LOADING);
  const tj::Value pair = fixture("pair.json")["cases"][0]["call"];
  CHECK_STR(tab::on_pair(pair["nonce"].s.data(), 32, pair["wrapped"].s.data(), 64, pair["kid"].s.data(), 16), "");
  CHECK(!D::g.session);
  hh::advance(look);
  says(T_STARTING, WAIT_STARTING);
  hh::advance(D::GREET_WAIT_MS * 2);
  says(T_STARTING, WAIT_STARTING);  // turned on now: the long wait
  ha.paired = true;
  CHECK(ha.hello());
  hh::advance(10);
  says(T_LOADING, WAIT_LOADING);
  CHECK(ha.show(layout_bytes("layout_small.dl.json")));
  CHECK(hidden(D::g.waiting));
  // Over a dashboard with Home Assistant connected nothing of this runs,
  // also not for taps.
  int net = fake::st.network_polls, hot = fake::st.hotspot_polls;
  const D::Hint mode = D::g.hint_mode;
  const uint32_t poll_at = D::g.hint_poll_at;
  for (int i = 0; i < 20; i++) {
    hh::tap(100 + 40 * i, 300);
    hh::advance(300);
  }
  CHECK(fake::st.network_polls == net && fake::st.hotspot_polls == hot);
  CHECK(D::g.hint_mode == mode && D::g.hint_poll_at == poll_at);
  // Home Assistant away over a dashboard: only the Wi-Fi notes look, at most
  // every HINT_POLL_MS, and taps add nothing to that (wifi_notes_over_a_dashboard).
  fake::st.network = true;
  fake::st.connected = false;
  hh::advance(20);
  net = fake::st.network_polls;
  hh::advance(6000);
  const int alone = fake::st.network_polls - net;
  net = fake::st.network_polls;
  const uint32_t from = hh::g_ms;
  for (int i = 0; i < 20; i++) {
    hh::tap(100 + 40 * i, 300);
    hh::advance(300);
  }
  const int tapped = fake::st.network_polls - net;
  const int most = (int) ((hh::g_ms - from) / D::HINT_POLL_MS) + 1;
  CHECK_MSG(alone >= 11 && alone <= 13 && tapped <= most, std::to_string(alone) + " / " + std::to_string(tapped));
  CHECK(fake::st.hotspot_polls == hot);  // on a network the hotspot is not read
  CHECK(D::g.hint_mode == mode && D::g.hint_poll_at == poll_at);
  fake::st.connected = true;
  hh::advance(D::GREET_WAIT_MS + look);
  net = fake::st.network_polls;
  hh::advance(3000);
  CHECK(fake::st.network_polls == net);

  // Paired with its key, Home Assistant away: waiting for it; connected and
  // greeted: getting the dashboard, then nothing to show (no dashboard yet).
  ha.boot(true);
  fake::st.network = true;
  fake::st.connected = false;
  hh::advance(look);
  says(T_WAITING, WAIT_RUNNING);
  hh::advance(D::GREET_WAIT_MS * 3);
  says(T_WAITING, WAIT_RUNNING);  // not connected: no wait runs out
  fake::st.connected = true;
  hh::advance(20);
  CHECK(ha.hello());
  hh::advance(10);
  says(T_LOADING, WAIT_LOADING);
  hh::advance(D::HINT_PICK_MS + look);
  says(T_PICK, WAIT_PICK);
  // tab_hello before ha_connected() turns true (contract 2.2) counts.
  fake::st.connected = false;
  hh::advance(look);
  says(T_WAITING, WAIT_RUNNING);
  CHECK(ha.hello());
  fake::st.connected = true;
  hh::advance(D::HINT_PICK_MS + look);
  says(T_PICK, WAIT_PICK);

  // A key but no pairing, Home Assistant away (added, not turned on yet):
  // waiting for Home Assistant.
  ha.boot(false);
  fake::st.network = true;
  fake::st.connected = false;
  hh::advance(look);
  says(T_WAITING, WAIT_RUNNING);
}

// Home Assistant restarts (or the power comes back to both): ESPHome
// connects early in its start, but TAB-1 greets nobody until its own start is
// done, which takes tens of seconds or minutes. A tablet turned on before
// says it waits for TAB-1, not to turn it on (the panel shows "Sending…" with
// no Turn on button meanwhile); once greeted, the words change at the next
// tick, also while the dashboard is still on its way (round 10, finding 1).
TEST(waiting_screen_while_home_assistant_starts) {
  const uint32_t look = D::HINT_POLL_MS + 10;
  auto says = [](const std::string &title, const std::string &step, const char *when) {
    return CHECK_MSG(!hidden(D::g.waiting) && label_text(D::g.headline) == title && label_text(D::g.hint) == step,
                     std::string(when) + ": " + label_text(D::g.headline) + " / " + label_text(D::g.hint) + ", want " +
                         title);
  };
  Ha ha;
  ha.boot(true);
  fake::st.network = true;
  fake::st.connected = true;
  CHECK(ha.hello());
  hh::advance(3600);
  says(T_PICK, WAIT_PICK, "before the restart");
  fake::st.connected = false;  // Home Assistant restarts
  hh::advance(5000);
  says(T_WAITING, WAIT_RUNNING, "Home Assistant down");
  fake::st.connected = true;  // ESPHome reconnects early in its start
  hh::advance(9000);
  says(T_STARTING, WAIT_STARTING, "9 s after the connection");
  hh::advance(31000);
  says(T_STARTING, WAIT_STARTING, "40 s after");
  shot("waiting_home_assistant_starting");
  hh::advance(200000);
  says(T_STARTING, WAIT_STARTING, "4 min after");
  CHECK(ha.hello());  // TAB-1 has started: it greets
  const std::string small = layout_bytes("layout_small.dl.json");
  const std::string hash = sha_hex(small).substr(0, 16);
  const size_t half = small.size() / 2;
  CHECK_STR(tab::on_layout(hash.data(), 16, 0, 2, (int) small.size(), small.data(), half), "");
  hh::advance(10);
  says(T_LOADING, WAIT_LOADING, "greeted, half a dashboard");
  hh::advance(5000);
  says(T_LOADING, WAIT_LOADING, "greeted, half a dashboard, 5 s later");
  CHECK_STR(tab::on_layout(hash.data(), 16, 1, 2, (int) small.size(), small.data() + half, small.size() - half), "");
  CHECK_STR(ha.commit(hash), "");
  CHECK(advance_until([&] { return hash == tab::active_hash(); }, 3000));
  CHECK(hidden(D::g.waiting));

  // A tablet never turned on: the short wait (TAB-1 greets the tablets it
  // turned on within about 3 s once Home Assistant has started).
  ha.boot(false);
  fake::st.network = true;
  fake::st.connected = true;
  hh::advance(D::GREET_WAIT_MS - 600);
  says(T_STARTING, WAIT_STARTING, "never turned on, within the wait");
  hh::advance(600 + look);
  says(T_TURN_ON, WAIT_HA, "never turned on, after the wait");
  // A turned-on tablet whose greeting never comes: turn it on, after the long wait.
  ha.boot(true);
  fake::st.network = true;
  fake::st.connected = true;
  hh::advance(D::TURNED_ON_WAIT_MS - 600);
  says(T_STARTING, WAIT_STARTING, "turned on, within the long wait");
  hh::advance(600 + look);
  says(T_TURN_ON, WAIT_HA, "turned on, after the long wait");
  // Greeted then: the turn-on words go at the next tick, before any dashboard.
  CHECK(ha.hello());
  hh::advance(10);
  says(T_LOADING, WAIT_LOADING, "greeted after the long wait");
}

// A paired tablet with its dashboard on screen loses Wi-Fi (a router change,
// say). While Home Assistant is not connected, the strip says what to do: the
// set-up hotspot to join once it is on, that it comes within 2 minutes
// before that, and a tap that cannot be sent says "No Wi-Fi", not "Home
// Assistant is not connected" (round 10, finding 4).
TEST(wifi_notes_over_a_dashboard) {
  const uint32_t look = D::HINT_POLL_MS + 10;
  InputRig rig;
  rig.up();
  fake::st.network = true;
  D::Widget *sw = widget(3);
  if (!CHECK(sw)) return;
  // Home Assistant connected: nothing reads the network over a dashboard.
  int net = fake::st.network_polls;
  hh::advance(5000);
  CHECK(fake::st.network_polls == net && hidden(D::g.strip));
  // Wi-Fi goes, and with it Home Assistant. A short loss shows nothing.
  fake::st.network = false;
  fake::st.connected = false;
  hh::advance(look);
  CHECK(hidden(D::g.strip));
  // A tap says "No Wi-Fi", for a moment.
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(rig.inputs() == 0 && !hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), NOTE_CONNECTING);
  hh::advance(D::STRIP_MS + 100);
  CHECK(hidden(D::g.strip));
  // NOTE_CONNECTING_MS without Wi-Fi: the note stays.
  hh::advance(D::NOTE_CONNECTING_MS);
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), NOTE_CONNECTING);
  shot("note_connecting_over_dashboard");
  // The hotspot comes on (90 s): join it.
  fake::st.hotspot = true;
  hh::advance(look);
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), NOTE_HOTSPOT);
  CHECK(!hidden(D::g.mark));  // with the "not connected" mark
  shot("note_hotspot_over_dashboard");
  hh::advance(120000);  // and 2 minutes later
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), NOTE_HOTSPOT);
  // A tap says the same; a passing message shows over it for a moment, then
  // the note is back at once.
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(rig.inputs() == 0);
  CHECK_STR(label_text(D::g.strip_label), NOTE_HOTSPOT);
  hh::advance(D::STRIP_MS + 10);
  D::show_strip("Not allowed on this tablet");
  hh::advance(D::STRIP_MS - 100);
  CHECK_STR(label_text(D::g.strip_label), "Not allowed on this tablet");
  hh::advance(120);
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), NOTE_HOTSPOT);
  // Taps add no look at the network: at most one each HINT_POLL_MS.
  net = fake::st.network_polls;
  int hot = fake::st.hotspot_polls;
  const uint32_t from = hh::g_ms;
  for (int i = 0; i < 20; i++) {
    hh::tap(centre_x(sw->r), centre_y(sw->r));
    hh::advance(300);
  }
  const int most = (int) ((hh::g_ms - from) / D::HINT_POLL_MS) + 1;
  CHECK(fake::st.network_polls - net <= most && fake::st.hotspot_polls - hot <= most);
  hh::advance(D::STRIP_MS);  // the last tap's message goes, the note stays
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), NOTE_HOTSPOT);
  // Wi-Fi set from a phone: the note goes; Home Assistant is not back yet,
  // so a tap says that.
  fake::st.network = true;
  fake::st.hotspot = true;  // still on for a moment after its page set the Wi-Fi
  hh::advance(look);
  CHECK(hidden(D::g.strip));
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK_STR(label_text(D::g.strip_label), "Home Assistant is not connected");
  // Home Assistant back: nothing reads the network any more.
  fake::st.connected = true;
  hh::advance(D::STRIP_MS + 100);
  CHECK(hidden(D::g.strip));
  net = fake::st.network_polls;
  hot = fake::st.hotspot_polls;
  hh::advance(5000);
  CHECK(fake::st.network_polls == net && fake::st.hotspot_polls == hot);
  // A note up when Home Assistant comes back goes at once.
  fake::st.network = false;
  fake::st.hotspot = true;
  fake::st.connected = false;
  hh::advance(look);
  CHECK(!hidden(D::g.strip));
  fake::st.connected = true;
  hh::advance(10);
  CHECK(hidden(D::g.strip));
  fake::st.network = true;

  // Power-on with the stored dashboard and no Wi-Fi: nothing at first, the
  // note after NOTE_CONNECTING_MS, the hotspot note once it is on.
  hh::advance(31000);  // stored in flash
  rig.ha.boot(true, true);
  fake::st.network = false;
  fake::st.connected = false;
  CHECK(advance_until([] { return !std::string(tab::active_hash()).empty(); }, 3000));
  hh::advance(2000);
  CHECK(hidden(D::g.strip) && hidden(D::g.waiting));
  hh::advance(D::NOTE_CONNECTING_MS);
  CHECK_STR(label_text(D::g.strip_label), NOTE_CONNECTING);
  fake::st.hotspot = true;
  hh::advance(look);
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), NOTE_HOTSPOT);
  // tab_clear (the dashboard goes): the waiting screen says it, the strip goes.
  fake::st.connected = true;
  CHECK(rig.ha.hello());
  CHECK_STR(rig.ha.clear(), "");
  fake::st.connected = false;
  hh::advance(look);
  CHECK(!hidden(D::g.waiting) && hidden(D::g.strip));
  CHECK_STR(label_text(D::g.hint), WAIT_HOTSPOT);

  // A platform that cannot tell the network counts as on one: no note.
  shutdown();
  reset_platform(true);
  fake::st.network_hook = false;
  fake::st.connected = false;
  rig.ha.paired = true;
  memcpy(fake::st.secret, rig.ha.secret, 32);
  fake::st.have_secret = true;
  start();
  hh::advance(D::NOTE_CONNECTING_MS + 2000);
  CHECK(fake::st.network_polls == 0);
}

// A tablet with its key whose window closed only for lack of network (for
// example a new router or Wi-Fi password): ESPHome keeps the hotspot off until
// a restart, so the tablet says how to get it back: off and on, then join the
// hotspot. ota_lock.h restarts the tablet once the network is back; the note
// goes then too.
// NO_WIFI: screens.json notes.off_on (load_screens).

TEST(setup_window_closed_with_a_key_says_no_wifi) {
  const uint32_t look = D::HINT_POLL_MS + 10;
  Ha ha;
  ha.boot(false);
  CHECK(fake::st.have_key);
  fake::st.connected = false;  // no network: Home Assistant cannot connect
  hh::advance(1000);
  // Before the window closes only the waiting screen's hint looks, at most
  // every HINT_POLL_MS.
  CHECK(fake::st.network_polls >= 1 && fake::st.network_polls <= 4);
  CHECK_STR(label_text(D::g.hint), WAIT_CONNECTING);
  tab::setup_window_closed();
  hh::advance(20);
  // On the waiting screen the hint says it; the strip stays away.
  CHECK(!hidden(D::g.waiting));
  CHECK_STR(label_text(D::g.headline), T_NO_WIFI);
  CHECK_STR(label_text(D::g.hint), WAIT_OFF_ON);  // whole: set_text would cut it with "…"
  CHECK(hidden(D::g.strip));
  shot("setup_window_closed_no_wifi");
  hh::advance(10000);
  CHECK_STR(label_text(D::g.hint), WAIT_OFF_ON);
  // At most one look each HINT_POLL_MS while the note is up.
  int polls = fake::st.network_polls;
  hh::advance(5000);
  CHECK(fake::st.network_polls - polls >= 9 && fake::st.network_polls - polls <= 11);
  // The network is back: the usual hint returns, and nothing reads the network any more.
  fake::st.network = true;
  hh::advance(look);
  CHECK(!D::g.no_wifi);
  CHECK_STR(label_text(D::g.hint), WAIT_RUNNING);  // with its key, Home Assistant not connected
  CHECK(hidden(D::g.strip));
  polls = fake::st.network_polls;
  hh::advance(1000);
  CHECK(fake::st.network_polls == polls);

  // Over a stored dashboard: the dashboard stays and the strip says it, for good.
  fake::st.connected = true;
  CHECK(ha.show(layout_bytes("layout_small.dl.json")));
  hh::advance(31000);  // stored in flash
  ha.boot(false, true);
  CHECK(advance_until([] { return !std::string(tab::active_hash()).empty(); }, 3000));
  fake::st.connected = false;  // ESPHome turns Home Assistant away once the window is closed
  hh::advance(20);
  tab::setup_window_closed();
  CHECK(!hidden(D::g.strip));  // at once
  hh::advance(20);
  CHECK(hidden(D::g.waiting));
  CHECK(!std::string(tab::active_hash()).empty());
  CHECK_STR(label_text(D::g.strip_label), NO_WIFI);  // whole
  CHECK_STR(label_text(D::g.hint), WAIT_OFF_ON);     // also there if the dashboard goes
  // The strip is as wide as the note needs, centred right of the mark.
  const int want_w = D::text_width(g_fonts.text[1], NO_WIFI.c_str(), NO_WIFI.size()) + 32;
  CHECK_MSG(want_w > D::STRIP_MIN_W && want_w <= D::STRIP_MAX_W, std::to_string(want_w));
  CHECK_MSG(obj_at(D::g.strip, 96 + (hh::W - 96 - want_w) / 2, hh::H - 24 - 52, want_w, 52), rect_text(D::g.strip));
  hh::advance(20000);
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), NO_WIFI);
  CHECK(!hidden(D::g.mark));  // with the "not connected" mark
  shot("setup_window_closed_no_wifi_over_dashboard");
  // A passing message (a refused tap, say) shows for a moment, in a strip of
  // the usual width, then the note is back at once. (This stored dashboard
  // has no values yet, so its switch takes no tap.)
  D::show_strip("Home Assistant is not connected");
  CHECK_MSG(obj_at(D::g.strip, 96 + (hh::W - 96 - D::STRIP_MIN_W) / 2, hh::H - 24 - 52, D::STRIP_MIN_W, 52),
            rect_text(D::g.strip));
  hh::advance(D::STRIP_MS - 100);
  CHECK_STR(label_text(D::g.strip_label), "Home Assistant is not connected");
  hh::advance(120);
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), NO_WIFI);
  // A refused tap says the note itself.
  CHECK_STR(D::why_not_sent(), NO_WIFI);
  // The network is back: the note goes.
  fake::st.network = true;
  hh::advance(look);
  CHECK(!D::g.no_wifi && hidden(D::g.strip));
  CHECK_STR(label_text(D::g.hint), WAIT_RUNNING);

  // A platform that cannot tell keeps the note until the restart.
  shutdown();
  reset_platform(true);
  fake::st.network_hook = false;
  fake::st.network = true;
  start();
  CHECK(advance_until([] { return !std::string(tab::active_hash()).empty(); }, 3000));
  tab::setup_window_closed();
  hh::advance(5000);
  CHECK(D::g.no_wifi && !hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), NO_WIFI);
  CHECK(fake::st.network_polls == 0);
}

// Home Assistant removed the tablet: the key is gone and the tablet restarted
// (ota_lock.h) with its old dashboard and pairing. When the new window closes,
// the restart hint covers that dashboard, which can no longer reach Home
// Assistant; the copy in flash stays for the next start.
TEST(setup_window_closed_covers_a_stored_dashboard) {
  Ha ha;
  ha.boot(true);
  CHECK(ha.hello());
  const std::string small = layout_bytes("layout_small.dl.json");
  CHECK(ha.show(small));
  hh::advance(31000);  // stored in flash
  CHECK(fake::st.flash_has);
  ha.boot(true, true);
  fake::st.have_key = false;
  CHECK(advance_until([] { return !std::string(tab::active_hash()).empty(); }, 3000));
  D::Layout *shown_layout = active();
  if (!CHECK(shown_layout && shown_layout->root)) return;
  lv_obj_t *root = shown_layout->root;
  CHECK(hidden(D::g.waiting) && !hidden(root));
  const int erases = fake::st.erase_calls;
  tab::setup_window_closed();
  hh::advance(20);
  CHECK(!hidden(D::g.waiting));
  CHECK_STR(label_text(D::g.hint), WAIT_RESTART);
  CHECK_STR(tab::active_hash(), "");
  CHECK(!tab::building());
  // The old dashboard is deleted in the background; nothing of it shows.
  auto all_free = [] {
    for (const D::Layout &l : D::g.lay)
      if (l.state != D::L_FREE) return false;
    return true;
  };
  CHECK(advance_until(all_free, 3000));
  CHECK(fake::st.erase_calls == erases && fake::st.flash_has);
  CHECK(fake::st.live.empty() && !fake::st.bad_release);
  // A tap on the waiting screen sends nothing.
  const size_t events = fake::st.events.size();
  hh::tap(640, 400);
  hh::advance(100);
  CHECK(fake::st.events.size() == events);
  shot("setup_window_closed_over_dashboard");
  // A stored dashboard still being built when the window closes stops too.
  const std::string big = many_thermostats(400);
  shutdown();
  reset_platform();
  fake::st.have_key = false;
  fake::st.flash_has = true;
  fake::st.flash.assign(big.begin(), big.end());
  tab::begin(g_fonts, fake::platform());
  CHECK(tab::building());
  tab::setup_window_closed();
  CHECK(!tab::building());
  CHECK(advance_until(all_free, 3000));
  CHECK_STR(tab::active_hash(), "");
  CHECK(!hidden(D::g.waiting));
  CHECK_STR(label_text(D::g.hint), WAIT_RESTART);
  CHECK(fake::st.flash_has && fake::st.live.empty());
}

// A fresh engine on a tablet powered on up_ms ago.
static void boot_at_uptime(uint32_t up_ms, bool hooks = true) {
  shutdown();
  reset_platform();
  fake::st.erase_hooks = hooks;
  fake::st.powered_on_at = hh::g_ms - up_ms;
  start();
}

static uint32_t uptime() { return hh::g_ms - fake::st.powered_on_at; }

// The engine's start line, or "".
static std::string started_line() {
  for (const std::string &l : fake::st.logs)
    if (l.rfind("Engine ", 0) == 0) return l;
  return "";
}

static void advance_to_uptime(uint32_t ms) {
  if (uptime() < ms) hh::advance(ms - uptime());
}

static lv_obj_t *child0(lv_obj_t *o) { return o ? lv_obj_get_child(o, 0) : nullptr; }

static bool bg_is(lv_obj_t *o, uint32_t rgb) {
  return o && lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), lv_color_hex(rgb));
}

// The two buttons: Erase on the left, Cancel on the right.
static const int ERASE_X = 508, CANCEL_X = 772, BUTTONS_Y = 468;

TEST(erase_hold_opens_the_question) {
  boot_at_uptime(0);
  if (!CHECK(D::g.erase_timer != nullptr)) return;
  const uint32_t children = lv_obj_get_child_count(D::g.overlay);
  advance_to_uptime(1000);
  // Held where Cancel will appear: lifting the finger must not press it.
  hh::press(CANCEL_X, BUTTONS_Y);
  hh::advance(4800);
  CHECK(!tab::erase_asking());
  hh::advance(400);
  if (!CHECK(tab::erase_asking())) return;
  CHECK(obj_at(D::g.erase_screen, 0, 0, hh::W, hh::H));
  CHECK_STR(label_text(D::g.e_title), "Erase this tablet?");
  CHECK_STR(label_text(D::g.e_text), "It forgets its Wi-Fi, Home Assistant and dashboard.");
  CHECK_STR(label_text(child0(D::g.e_erase)), "Erase");
  CHECK_STR(label_text(child0(D::g.e_cancel)), "Cancel");
  CHECK(obj_at(D::g.e_erase, ERASE_X - 120, BUTTONS_Y - 36, 240, 72));
  CHECK(obj_at(D::g.e_cancel, CANCEL_X - 120, BUTTONS_Y - 36, 240, 72));
  // Cancel is the highlighted default.
  CHECK(bg_is(D::g.e_cancel, D::ui_colour(D::C_ACCF)) && bg_is(D::g.e_erase, D::ui_colour(D::C_CARD2)));
  shot("erase_question");
  hh::release();
  hh::advance(100);
  CHECK(tab::erase_asking() && fake::st.erase_alls == 0);
  // A tap beside the buttons does nothing.
  hh::tap(100, 100);
  hh::tap(640, 700);
  CHECK(tab::erase_asking() && fake::st.erase_alls == 0);
  // Cancel: the question goes and hold-to-erase is over for this boot.
  hh::tap(CANCEL_X, BUTTONS_Y);
  CHECK(!tab::erase_asking());
  CHECK(D::g.erase_screen == nullptr && D::g.erase_timer == nullptr);
  hh::advance(20);
  CHECK(lv_obj_get_child_count(D::g.overlay) == children);
  CHECK(uptime() < 9000);
  hh::press(640, 400);
  hh::advance(6000);
  hh::release();
  CHECK(!tab::erase_asking() && fake::st.erase_alls == 0);
  CHECK(!hidden(D::g.waiting));
}

TEST(erase_hold_timing) {
  // Lifting the finger starts the 5 s again.
  boot_at_uptime(0);
  advance_to_uptime(100);
  hh::press(640, 400);
  advance_to_uptime(3000);
  hh::release();
  advance_to_uptime(3500);
  hh::press(640, 400);
  advance_to_uptime(6000);  // 5.9 s after the first press
  CHECK(!tab::erase_asking());
  advance_to_uptime(8400);
  CHECK(!tab::erase_asking());
  advance_to_uptime(8700);
  CHECK(tab::erase_asking());
  hh::release();
  hh::tap(CANCEL_X, BUTTONS_Y);
  CHECK(!tab::erase_asking() && fake::st.erase_alls == 0);

  // A hold that starts at 9.5 s counts, though it ends after the 10 s.
  boot_at_uptime(0);
  advance_to_uptime(9500);
  hh::press(640, 400);
  advance_to_uptime(14400);
  CHECK(!tab::erase_asking() && D::g.erase_timer != nullptr);
  advance_to_uptime(14700);
  CHECK(tab::erase_asking());
  hh::release();
  hh::tap(CANCEL_X, BUTTONS_Y);

  // After 10 s without a touch the watch ends; a later hold does nothing.
  boot_at_uptime(0);
  CHECK(D::g.erase_timer != nullptr);
  advance_to_uptime(10100);
  CHECK(D::g.erase_timer == nullptr && D::g.erase_step == D::E_OFF);
  hh::press(640, 400);
  hh::advance(6000);
  hh::release();
  CHECK(!tab::erase_asking() && fake::st.erase_alls == 0);

  // A press that starts just after 10 s: none.
  boot_at_uptime(9990 - 20);
  hh::advance(80);  // 10.05 s
  hh::press(640, 400);
  hh::advance(6000);
  hh::release();
  CHECK(!tab::erase_asking() && D::g.erase_timer == nullptr);

  // The engine starts after the window, or the platform has no erase: no timer at all.
  boot_at_uptime(12000);
  CHECK(D::g.erase_timer == nullptr);
  // The start line says how long after power-on the engine started, so the
  // time left for the hold can be measured on the tablet.
  CHECK_STR(started_line(), "Engine 0.2.0 started 12000 ms after power-on (boot 3, not paired)");
  hh::press(640, 400);
  hh::advance(6000);
  hh::release();
  CHECK(!tab::erase_asking());
  boot_at_uptime(0, false);
  CHECK(D::g.erase_timer == nullptr);
  CHECK_STR(started_line(), "Engine 0.2.0 started (boot 3, not paired)");  // no uptime to tell
  Ha ha;
  ha.boot(false);  // the other tests: powered on long ago
  CHECK(D::g.erase_timer == nullptr && D::g.erase_step == D::E_OFF);
}

// A touch sample the controller or its bus drops reads as a short lift. Up to
// ERASE_GAP_MS the hold and its 5 s count go on (design 10.9); a real lift
// longer than that starts them again.
static void lift(uint32_t ms) {
  hh::g_down = false;
  hh::advance(ms);
  hh::g_down = true;
}

TEST(erase_hold_survives_dropped_samples) {
  // The question opens 5 s after the touch began, within a poll or two:
  // about 5520 ms after power-on for a touch at 500 ms.
  auto held_from_500 = [](std::initializer_list<std::pair<uint32_t, uint32_t>> lifts) {
    boot_at_uptime(0);
    advance_to_uptime(500);
    hh::press(640, 400);
    for (const auto &l : lifts) {
      advance_to_uptime(l.first);
      lift(l.second);
    }
    advance_to_uptime(5400);
    const bool early = tab::erase_asking();
    advance_to_uptime(5600);
    const bool on_time = tab::erase_asking();
    hh::release();
    if (on_time) hh::tap(CANCEL_X, BUTTONS_Y);
    return !early && on_time && fake::st.erase_alls == 0;
  };
  // One dropped sample (a lift one poll sees).
  CHECK(held_from_500({{2500, D::ERASE_POLL_MS + 10}}));
  // Two dropped samples, apart, and two in a row.
  CHECK(held_from_500({{1500, D::ERASE_POLL_MS + 10}, {3500, D::ERASE_POLL_MS + 10}}));
  CHECK(held_from_500({{2500, 2 * D::ERASE_POLL_MS + 10}}));
  // A real lift of 300 ms starts the 5 s again.
  CHECK(!held_from_500({{2500, 300}}));
  boot_at_uptime(0);
  advance_to_uptime(500);
  hh::press(640, 400);
  advance_to_uptime(2500);
  lift(300);  // touched again at 2800
  advance_to_uptime(7700);
  CHECK(!tab::erase_asking());
  advance_to_uptime(7900);
  CHECK(tab::erase_asking());
  hh::release();
  hh::tap(CANCEL_X, BUTTONS_Y);
  // Lifting just before the 5 s is a lift: nothing opens, also not once the
  // gap allowance has passed.
  boot_at_uptime(0);
  advance_to_uptime(500);
  hh::press(640, 400);
  advance_to_uptime(5450);
  hh::g_down = false;
  hh::advance(2000);
  CHECK(!tab::erase_asking() && D::g.erase_timer != nullptr && !D::g.holding);

  // A hold that began before 10 s keeps going over a dropped sample after it.
  boot_at_uptime(0);
  advance_to_uptime(9500);
  hh::press(640, 400);
  advance_to_uptime(12000);
  lift(D::ERASE_POLL_MS + 10);
  advance_to_uptime(14400);
  CHECK(!tab::erase_asking() && D::g.erase_timer != nullptr);
  advance_to_uptime(14700);
  CHECK(tab::erase_asking());
  hh::release();
  hh::tap(CANCEL_X, BUTTONS_Y);
  // A real lift after 10 s ends the watch for this boot: the timer is gone,
  // so the touch path carries nothing of it any more.
  boot_at_uptime(0);
  advance_to_uptime(9500);
  hh::press(640, 400);
  advance_to_uptime(11000);
  lift(300);
  CHECK(D::g.erase_timer == nullptr && D::g.erase_step == D::E_OFF);
  hh::advance(6000);
  hh::release();
  CHECK(!tab::erase_asking() && fake::st.erase_alls == 0);
}

// Taps for ms milliseconds at (x, y): down for `down`, up for `up`, again.
static void taps(uint32_t ms, uint32_t down, uint32_t up, int x = 640, int y = 400) {
  hh::g_x = x;
  hh::g_y = y;
  const uint32_t end = hh::g_ms + ms;
  while ((int32_t) (end - hh::g_ms) > 0) {
    hh::g_down = true;
    hh::advance(down);
    hh::g_down = false;
    hh::advance(up);
  }
}

// Taps are lifts, so they never add up to a hold (design 10.10): at 3, 5 and
// 10 taps a second, short and long presses, each started at ten different
// moments against the 10 ms poll, for 8 s, also on the spot where Erase
// would appear. With the 50 ms poll of round 9, 10 taps a second could fall
// between the polls and read as one long press.
TEST(erase_taps_never_open_the_question) {
  struct Rate {
    uint32_t period;
    std::vector<uint32_t> downs;
  };
  const Rate rates[] = {{333, {33, 60, 100, 166, 233, 300}}, {200, {30, 60, 100, 140, 170}}, {100, {30, 50, 60, 70}}};
  const int phases = QUICK ? 3 : 10;
  int runs = 0;
  std::string opened;
  for (const Rate &r : rates)
    for (uint32_t down : r.downs)
      for (int phase = 0; phase < phases; phase++) {
        const bool on_erase = phase % 2 == 1;
        boot_at_uptime(0);
        advance_to_uptime(500 + (uint32_t) phase);
        taps(8000, down, r.period - down, on_erase ? ERASE_X : 640, on_erase ? BUTTONS_Y : 400);
        hh::g_down = false;
        hh::advance(100);
        runs++;
        if (tab::erase_asking() || fake::st.erase_alls != 0 || D::g.erase_step != D::E_WATCH)
          opened += " " + std::to_string(1000 / r.period) + "/s " + std::to_string(down) + " ms down, phase " +
                    std::to_string(phase) + ";";
      }
  CHECK_MSG(opened.empty(), "the question opened after taps:" + opened);
  CHECK(runs == 15 * phases);
  // Taps that go on past the 10 s end the watch within a few taps (the fourth
  // lift of the hold that began in time): the timer is gone and the touch
  // path carries nothing of it any more.
  boot_at_uptime(0);
  advance_to_uptime(9000);
  taps(2000, 60, 140);
  CHECK(D::g.erase_timer == nullptr && D::g.erase_step == D::E_OFF);
  taps(3000, 60, 140);
  CHECK(!tab::erase_asking() && fake::st.erase_alls == 0);
}

// A hold may lose the finger a few times (dropped samples), but only a few:
// at most ERASE_GAPS lifts, ERASE_GAPS_MS of them in all, and the finger stays
// within ERASE_MOVE_PX of where it went down. Otherwise the 5 s count starts
// again where the finger is.
TEST(erase_hold_forgives_only_a_few_lifts) {
  // When the question opens for a touch at 500 ms with these lifts:
  // 0 when it opens on time (5.5 s), else the uptime when it opened.
  auto opens_at = [](std::initializer_list<std::pair<uint32_t, uint32_t>> lifts) -> uint32_t {
    boot_at_uptime(0);
    advance_to_uptime(500);
    hh::press(640, 400);
    for (const auto &l : lifts) {
      advance_to_uptime(l.first);
      lift(l.second);
    }
    uint32_t at = 0;
    while (uptime() < 9900 && !tab::erase_asking()) hh::advance(10);
    if (tab::erase_asking()) at = uptime();
    hh::release();
    if (at) hh::tap(CANCEL_X, BUTTONS_Y);
    return at;
  };
  const uint32_t drop = D::ERASE_POLL_MS + 10;  // one dropped sample: a lift one poll sees
  // A plain hold, and three dropped samples, open it 5 s after the touch.
  uint32_t at = opens_at({});
  CHECK_MSG(at >= 5500 && at <= 5560, std::to_string(at));
  at = opens_at({{1500, drop}, {2500, drop}, {3500, drop}});
  CHECK_MSG(at >= 5500 && at <= 5560, std::to_string(at));
  // A fourth one starts the 5 s again from where the finger came back.
  at = opens_at({{1500, drop}, {2500, drop}, {3500, drop}, {4500, drop}});
  CHECK_MSG(at >= 4500 + drop + 5000 - 20 && at <= 4500 + drop + 5000 + 40, std::to_string(at));
  // Two lifts of 200 ms are forgiven; three are more than ERASE_GAPS_MS
  // together, so the count starts again at the third.
  at = opens_at({{1500, 200}, {3000, 200}});
  CHECK_MSG(at >= 5500 && at <= 5560, std::to_string(at));
  at = opens_at({{1500, 200}, {2500, 200}, {3500, 200}});
  CHECK_MSG(at >= 3500 + 200 + 5000 - 20 && at <= 3500 + 200 + 5000 + 40, std::to_string(at));

  // The finger that comes back after a dropped sample 100 px away is a new touch.
  boot_at_uptime(0);
  advance_to_uptime(500);
  hh::press(640, 400);
  advance_to_uptime(2500);
  hh::g_down = false;
  hh::advance(drop);
  hh::press(740, 400);
  advance_to_uptime(5700);
  CHECK(!tab::erase_asking());
  advance_to_uptime(7500 + drop - 20);
  CHECK(!tab::erase_asking());
  advance_to_uptime(7500 + drop + 80);
  CHECK(tab::erase_asking());
  hh::release();
  hh::tap(CANCEL_X, BUTTONS_Y);
  // A finger that slides 80 px while held starts the count again there; one
  // that rolls 50 px does not.
  boot_at_uptime(0);
  advance_to_uptime(500);
  hh::press(640, 400);
  advance_to_uptime(2500);
  hh::move(720, 400);
  advance_to_uptime(5700);
  CHECK(!tab::erase_asking());
  advance_to_uptime(7400);
  CHECK(!tab::erase_asking());
  advance_to_uptime(7600);
  CHECK(tab::erase_asking());
  hh::release();
  hh::tap(CANCEL_X, BUTTONS_Y);
  boot_at_uptime(0);
  advance_to_uptime(500);
  hh::press(640, 400);
  advance_to_uptime(2500);
  hh::move(670, 440);
  advance_to_uptime(5600);
  CHECK(tab::erase_asking());
  hh::release();
  hh::tap(CANCEL_X, BUTTONS_Y);
  CHECK(fake::st.erase_alls == 0);
}

TEST(erase_question_answers) {
  // Nobody answers: it cancels itself after 30 s.
  boot_at_uptime(0);
  hh::press(ERASE_X, BUTTONS_Y);  // where Erase will appear
  hh::advance(5200);
  if (!CHECK(tab::erase_asking())) return;
  hh::release();
  hh::advance(50);
  CHECK(tab::erase_asking() && fake::st.erase_alls == 0);  // lifting the finger pressed nothing
  hh::advance(29900 - (hh::g_ms - D::g.erase_at));
  CHECK(tab::erase_asking());
  hh::advance(200);
  CHECK(!tab::erase_asking() && D::g.erase_screen == nullptr && D::g.erase_timer == nullptr);
  CHECK(fake::st.erase_alls == 0);

  // Erase: "Erasing…" is drawn, then the platform erases once.
  boot_at_uptime(0);
  hh::press(CANCEL_X, BUTTONS_Y);
  hh::advance(5200);
  hh::release();
  CHECK(tab::erase_asking());
  hh::tap(ERASE_X, BUTTONS_Y);
  CHECK(!tab::erase_asking() && D::g.erase_step == D::E_ERASING);
  CHECK_STR(label_text(D::g.e_title), "Erasing\xE2\x80\xA6");
  CHECK(hidden(D::g.e_erase) && hidden(D::g.e_cancel) && hidden(D::g.e_text));
  CHECK(fake::st.erase_alls == 0);
  shot("erase_busy");
  const uint32_t clicked = hh::g_ms;
  hh::advance(300);
  CHECK(fake::st.erase_alls == 1);
  CHECK(fake::st.erased_at - clicked < 300 && D::g.erase_step == D::E_DONE && D::g.erase_timer == nullptr);
  hh::advance(2000);
  CHECK(fake::st.erase_alls == 1);
}

// After the hold-the-screen erase the tablet keeps a mark (nothing secret)
// until it has a key again. The Home Assistant that had it never offers it
// as a new device, it asks in Repairs; so the screen says to confirm it
// there, and never to add a device that shows up under its name, which
// could be another device (round 10, finding 2).
TEST(waiting_screen_after_the_erase) {
  const uint32_t look = D::HINT_POLL_MS + 10;
  auto says = [](const std::string &title, const std::string &step, const char *when) {
    return CHECK_MSG(!hidden(D::g.waiting) && label_text(D::g.headline) == title && label_text(D::g.hint) == step,
                     std::string(when) + ": " + label_text(D::g.headline) + " / " + label_text(D::g.hint) + ", want " +
                         title);
  };
  // The erase leaves the mark: hold, Erase, and the next start shows it.
  boot_at_uptime(0);
  hh::press(CANCEL_X, BUTTONS_Y);
  hh::advance(5200);
  hh::release();
  if (!CHECK(tab::erase_asking())) return;
  hh::tap(ERASE_X, BUTTONS_Y);
  hh::advance(300);
  CHECK(fake::st.erase_alls == 1 && fake::st.erased_mark);
  shutdown();
  reset_platform();
  fake::st.erased_mark = true;  // what the glue left in flash
  fake::st.have_key = false;    // and nothing else
  fake::st.network = false;
  fake::st.hotspot = true;
  fake::st.connected = false;
  start();
  CHECK(started_line().find("not paired, erased)") != std::string::npos);
  hh::advance(look);
  says(T_HOTSPOT, WAIT_HOTSPOT, "erased, no Wi-Fi");
  fake::st.network = true;
  hh::advance(look);
  says(T_ERASED, WAIT_ERASED, "erased, on Wi-Fi");
  hh::advance(D::GREET_WAIT_MS * 4);
  says(T_ERASED, WAIT_ERASED, "erased, on Wi-Fi, later");
  CHECK(label_text(D::g.headline).find("Add") == std::string::npos);
  // The admin confirms in Repairs: Home Assistant connects without a key and
  // gives back the one it kept at once. The words stay meanwhile.
  fake::st.connected = true;
  hh::advance(D::GREET_WAIT_MS - 1000);
  says(T_ERASED, WAIT_ERASED, "connected, before the key");
  CHECK(fake::st.erased_clears == 0 && fake::st.erased_mark);
  fake::st.have_key = true;
  hh::advance(look);
  CHECK(fake::st.erased_clears == 1 && !fake::st.erased_mark && !D::g.erased);
  says(T_STARTING, WAIT_STARTING, "the key is back");
  // Once only, also when Home Assistant goes away again.
  fake::st.connected = false;
  hh::advance(look * 4);
  says(T_WAITING, WAIT_RUNNING, "the key is back, Home Assistant away");
  CHECK(fake::st.erased_clears == 1);
  // A Home Assistant that never had it would rather list it as new; without
  // its key it says nothing more than "Repairs", and docs/engine.md says so.
  // The window closed before the key came: the restart hint, and the mark
  // stays for the next start.
  shutdown();
  reset_platform();
  fake::st.erased_mark = true;
  fake::st.have_key = false;
  fake::st.network = true;
  fake::st.connected = false;
  start();
  hh::advance(look);
  says(T_ERASED, WAIT_ERASED, "erased again");
  tab::setup_window_closed();
  hh::advance(20);
  says(T_CLOSED, WAIT_RESTART, "window closed");
  CHECK(fake::st.erased_mark && fake::st.erased_clears == 0);
  // A greeting with a key clears it too, also when the waiting screen is not
  // up (a dashboard came over a plain connection first).
  shutdown();
  reset_platform();
  fake::st.erased_mark = true;
  fake::st.have_key = false;
  fake::st.network = true;
  fake::st.connected = true;
  start();
  Ha ha;
  CHECK(ha.hello());
  CHECK(ha.show(layout_bytes("layout_small.dl.json")));
  CHECK(hidden(D::g.waiting) && D::g.erased);
  fake::st.have_key = true;
  hh::advance(2000);
  CHECK(D::g.erased && fake::st.erased_clears == 0);  // the waiting screen is not up: nothing looked
  CHECK(ha.hello());
  CHECK(!D::g.erased && fake::st.erased_clears == 1 && !fake::st.erased_mark);
  // A key at start (one built into the firmware comes back): cleared at once.
  shutdown();
  reset_platform();
  fake::st.erased_mark = true;
  start();
  CHECK(!D::g.erased && fake::st.erased_clears == 1);
  CHECK(started_line().find("erased") == std::string::npos);
  // A tablet erased over USB has no mark: a new tablet.
  shutdown();
  reset_platform();
  fake::st.have_key = false;
  fake::st.network = true;
  fake::st.connected = false;
  start();
  hh::advance(look);
  says(T_ADD, WAIT_ADD, "no mark");
  // A platform without erased_clear: the mark is forgotten for this boot.
  shutdown();
  reset_platform();
  fake::st.erased_mark = true;
  fake::st.erased_hook = false;
  fake::st.have_key = false;
  fake::st.network = true;
  fake::st.connected = false;
  start();
  hh::advance(look);
  says(T_ERASED, WAIT_ERASED, "no erased_clear");
  fake::st.have_key = true;
  hh::advance(look);
  CHECK(!D::g.erased && fake::st.erased_clears == 0);
  says(T_WAITING, WAIT_RUNNING, "no erased_clear, key back");
}

// tab_state.h's own words are the contract's (screens.json, CONTRACT.md 11),
// which the panel quotes: every headline and step, the parts the hotspot's
// and the tablet's name go between, the erased state's second line and its
// step without the name, the Wi-Fi notes, and the hotspot's name.
TEST(screens_match_contract) {
  const tj::Value &w = SCREENS["waiting"], &n = SCREENS["notes"];
  CHECK_STR(SCREENS["hotspot"].s, fake::State().hotspot_name);
  CHECK(w.size() == 11 && n.size() == 3);
  const std::string hotspot_step = std::string(D::WAIT_HOTSPOT_BEFORE) + "{hotspot}" + D::WAIT_HOTSPOT_AFTER;
  const std::string off_on_step = std::string(D::WAIT_OFF_ON_BEFORE) + "{hotspot}" + D::WAIT_HOTSPOT_AFTER;
  const std::string erased_step = std::string(D::WAIT_ERASED_BEFORE) + "{name}" + D::WAIT_ERASED_AFTER;
  struct Pair {
    const char *state;
    const D::HintText &t;
    std::string step;
  } pairs[] = {
      {"hotspot", D::HINT_HOTSPOT, hotspot_step},  {"connecting", D::HINT_CONNECTING, D::HINT_CONNECTING.step},
      {"add", D::HINT_ADD, D::HINT_ADD.step},      {"erased", D::HINT_ERASED, erased_step},
      {"starting", D::HINT_STARTING, D::HINT_STARTING.step}, {"turn_on", D::HINT_TURN_ON, D::HINT_TURN_ON.step},
      {"wait_ha", D::HINT_WAIT_HA, D::HINT_WAIT_HA.step},    {"loading", D::HINT_LOADING, D::HINT_LOADING.step},
      {"pick", D::HINT_PICK, D::HINT_PICK.step},   {"closed", D::HINT_CLOSED, D::HINT_CLOSED.step},
      {"no_wifi", D::HINT_NO_WIFI, off_on_step},
  };
  for (const Pair &p : pairs) {
    const tj::Value &s = w[p.state];
    CHECK_MSG(s["headline"].s == p.t.title, std::string(p.state) + ": " + p.t.title + " / " + s["headline"].s);
    CHECK_MSG(s["step"].s == p.step, std::string(p.state) + ": " + p.step + " / " + s["step"].s);
    CHECK_MSG(s["step2"].s == (p.t.more ? p.t.more : ""), p.state);
    CHECK_MSG(!s["when"].s.empty(), p.state);
  }
  CHECK_STR(w["erased"]["step_long_name"].s, D::HINT_ERASED.step);
  CHECK_STR(n["hotspot"]["text"].s, D::NOTE_PREFIX + hotspot_step);
  CHECK_STR(n["off_on"]["text"].s, D::NOTE_PREFIX + off_on_step);
  CHECK_STR(n["connecting"]["text"].s, D::NOTE_CONNECTING);
}

// The erased screen names this tablet the way Home Assistant names its
// question in Repairs, "Authentication expired for <name>" with the name
// Home Assistant gave the tablet when it added it (the friendly name, on the
// screen above the headline), and says to confirm only that one: any device
// on the network can make Home Assistant ask one for another tablet (round
// 11). The second line is for a Home Assistant that never had the tablet. A
// name too long for the line gives the step without it ("for the name
// above"), never one cut short: the end of the name tells tablets apart.
TEST(erased_step_names_this_tablet) {
  const uint32_t look = D::HINT_POLL_MS + 10;
  const tj::Value &erased = SCREENS["waiting"]["erased"];
  auto erased_with = [&](const char *name) {
    shutdown();
    reset_platform();
    fake::st.name = name;
    fake::st.erased_mark = true;
    fake::st.have_key = false;
    fake::st.network = true;
    fake::st.connected = false;
    start();
    hh::advance(look);
    CHECK_STR(label_text(D::g.headline), T_ERASED);
    CHECK_STR(label_text(D::g.hint2), WAIT_ERASED2);
    return label_text(D::g.hint);
  };
  const std::string open = "\xE2\x80\x9C", close = "\xE2\x80\x9D";
  // The test's name, and the real firmware's ("Apollo TAB-1" and the last six
  // characters of the MAC): the step quotes Home Assistant's question whole.
  for (const char *name : {fake::NAME, "Apollo TAB-1 12ee0e"}) {
    const std::string step = erased_with(name);
    CHECK_STR(step, screen_words(erased["step"], "", name));
    CHECK_MSG(step.find(open + "Authentication expired for " + name + close) != std::string::npos, step);
    CHECK_MSG(step.find("confirm only") != std::string::npos && step.find("Repairs") != std::string::npos, step);
    CHECK(waiting_label(name) != nullptr);  // the same name above the headline
    if (strcmp(name, fake::NAME) != 0) shot("waiting_9b_erased_real_name");
  }
  // The longest name that fits shows whole; one letter more gives the step
  // without the name, never a cut one.
  std::string fits;
  for (std::string n = "W"; n.size() < 100; n += "W") {
    const std::string s = screen_words(erased["step"], "", n.c_str());
    if (D::text_width(g_fonts.text[4], s.c_str(), s.size()) > hh::W - 192) break;
    fits = n;
  }
  CHECK(fits.size() >= 10 && fits.size() < 99);
  const std::string longer = fits + "W";
  CHECK_STR(erased_with(fits.c_str()), screen_words(erased["step"], "", fits.c_str()));
  CHECK_STR(erased_with(longer.c_str()), WAIT_ERASED_LONG);
  shot("waiting_9c_erased_long_name");
  CHECK(label_text(D::g.hint).find("\xE2\x80\xA6") == std::string::npos);
  // A name longer than the text buffer, none, or one with a control character.
  const std::string huge(200, 'a');
  CHECK_STR(erased_with(huge.c_str()), WAIT_ERASED_LONG);
  CHECK_STR(erased_with(""), WAIT_ERASED_LONG);
  CHECK_STR(erased_with("Tab\nlet"), WAIT_ERASED_LONG);
  // The second line goes with the state: the key comes, Home Assistant away.
  erased_with(fake::NAME);
  fake::st.have_key = true;
  hh::advance(look);
  CHECK_STR(label_text(D::g.headline), T_WAITING);
  CHECK_STR(label_text(D::g.hint2), "");
}

static bool clickable(lv_obj_t *o) { return o && lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE); }

static bool logged(const char *part) {
  for (const std::string &l : fake::st.logs)
    if (l.find(part) != std::string::npos) return true;
  return false;
}

// A control without a value yet is drawn faded (40%): there is nothing to
// change on it. While the tablet can send, a tap on it does nothing, and a
// swipe that starts on a faded slider still switches pages. While it cannot
// (right after a restart with a stored dashboard, Home Assistant connected
// but not greeting yet, or away), a tap says why, as on any other control,
// and knocks without a session (round 11, contract 7.4).
TEST(faded_controls_say_why) {
  const std::string all = layout_bytes("layout_all_cards.dl.json");
  const std::string hash = sha_hex(all).substr(0, 16);
  Ha ha;
  ha.boot(true);
  CHECK(ha.hello());
  CHECK(ha.show(all));
  hh::advance(30100);
  if (!CHECK(fake::st.flash_has)) return;
  // Restart: the stored dashboard, no values, Home Assistant connected and
  // not greeting yet.
  ha.boot(true, true);
  CHECK(advance_until([&] { return hash == tab::active_hash(); }, 3000));
  hh::advance(D::HINT_POLL_MS + 10);
  D::Widget *sw = widget(3), *sl = widget(10), *th = widget(23), *b7 = widget(7);
  if (!CHECK(sw && sl && th && b7)) return;
  CHECK(sw->faded && sl->faded && th->faded && !b7->faded);
  CHECK(clickable(sw->obj) && clickable(sl->obj) && clickable(th->o2) && clickable(th->o3));
  lv_obj_update_layout(lv_screen_active());
  const int plus_x = th->r[0] + th->r[2] - 18, plus_y = th->r[1] + th->r[3] - 18;
  fake::st.events.clear();
  fake::st.logs.clear();
  CHECK(D::g.touch_knock_at == 0);
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), "Home Assistant is not connected");
  CHECK(D::g.touch_knock_at != 0 && logged("Touch without a session"));
  CHECK(fake::events("tab1_hello").size() == 1 && fake::events("tab1_input").empty());
  CHECK(sw->pressed == 0 && sw->ovr == 0 && sw->faded);
  // The thermostat's buttons and the slider say the same.
  hh::advance(D::STRIP_MS + 100);
  CHECK(hidden(D::g.strip));
  hh::tap(plus_x, plus_y);
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), "Home Assistant is not connected");
  hh::advance(D::STRIP_MS + 100);
  hh::tap(centre_x(sl->r), centre_y(sl->r));
  CHECK(!hidden(D::g.strip) && sl->dragging == 0 && sl->ovr == 0);
  CHECK_STR(label_text(D::g.strip_label), "Home Assistant is not connected");
  CHECK(fake::events("tab1_input").empty());
  // A swipe that starts on the faded slider switches pages.
  hh::drag(centre_x(sl->r), centre_y(sl->r), centre_x(sl->r) - 300, centre_y(sl->r), 200);
  CHECK(D::g.page == 1 && sl->pressed == 0 && sl->dragging == 0);
  tab::go_to(0);
  hh::advance(20);

  // Greeted, still no values (the snapshot not here yet): the tablet can
  // send, and the faded controls take no input at all.
  CHECK(ha.hello());
  hh::advance(D::STRIP_MS + 100);
  CHECK(hidden(D::g.strip));
  fake::st.events.clear();
  hh::press(centre_x(sw->r), centre_y(sw->r));
  CHECK(sw->pressed == 0);
  hh::advance(50);
  hh::release();
  hh::tap(plus_x, plus_y);
  CHECK(th->pressed_part == 0);
  hh::tap(centre_x(sl->r), centre_y(sl->r));
  CHECK(hidden(D::g.strip) && fake::events("tab1_input").empty() && sl->ovr == 0 && sw->ovr == 0);
  // A drag along the faded slider is no slider drag: it is a page swipe
  // (to the right: the page before, wrapping), and one to the left too.
  hh::drag(sl->r[0] + 10, centre_y(sl->r), sl->r[0] + 172, centre_y(sl->r) + 2, 100);
  CHECK(D::g.page == 2 && sl->dragging == 0 && sl->ovr == 0 && fake::events("tab1_input").empty());
  tab::go_to(0);
  hh::advance(20);
  hh::drag(centre_x(sl->r), centre_y(sl->r), centre_x(sl->r) - 300, centre_y(sl->r), 200);
  CHECK(D::g.page == 1 && hidden(D::g.strip));
  tab::go_to(0);
  hh::advance(20);
  // A control that needs no value (a button) sends as always.
  hh::tap(centre_x(b7->r), centre_y(b7->r));
  CHECK(fake::events("tab1_input").size() == 1);
  // Values come: no longer faded, a tap sends.
  CHECK_STR(ha.set(state_payload(0)), "");
  hh::advance(20);
  CHECK(!sw->faded && !sl->faded && !th->faded);
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(fake::events("tab1_input").size() == 2);
  // A full snapshot that has no value for the switch fades it again (once
  // the value the tap showed has had its 4 s).
  hh::advance(D::ACK_TIMEOUT_MS + 100);
  CHECK_STR(ha.set("{\"f\":1,\"h\":\"" + hash + "\",\"v\":[]}"), "");
  hh::advance(20);
  CHECK(sw->faded && clickable(sw->obj));
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(fake::events("tab1_input").size() == 2 && hidden(D::g.strip));

  // Restart while Home Assistant is away (Wi-Fi up): a tap says so, and
  // nothing knocks (there is no connection to knock on).
  fake::st.connected = false;
  ha.boot(true, true);
  fake::st.connected = false;
  fake::st.network = true;
  CHECK(advance_until([&] { return hash == tab::active_hash(); }, 3000));
  hh::advance(12000);
  sw = widget(3);
  if (!CHECK(sw && sw->faded)) return;
  fake::st.events.clear();
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK(!hidden(D::g.strip));
  CHECK_STR(label_text(D::g.strip_label), "Home Assistant is not connected");
  CHECK(fake::events("tab1_hello").empty() && fake::events("tab1_input").empty());
  // Without Wi-Fi, the Wi-Fi note that fits.
  fake::st.network = false;
  fake::st.hotspot = true;
  hh::advance(D::STRIP_MS + D::HINT_POLL_MS + 100);
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK_STR(label_text(D::g.strip_label), NOTE_HOTSPOT);
  CHECK(fake::events("tab1_input").empty());
}

// ================================================================ the host build

// run.sh builds everything with -ffp-contract=off. Without it clang fuses
// a * (1 - t) + b * t into one multiply-add, which rounds once where the
// tablet (software doubles), Python and JavaScript round twice: at today's
// weight 0.82, 19 pairs of channel values come out one apart.
static volatile double g_mix_weight = 0.82;
__attribute__((noinline)) static double mix_as_written(double a, double b, double t) {
  return floor(a * (1 - t) + b * t + 0.5);
}
__attribute__((noinline)) static double mix_rounded_each_step(double a, double b, double t) {
  volatile double p = a * (1 - t);
  volatile double q = b * t;
  volatile double s = p + q;
  return floor(s + 0.5);
}
TEST(doubles_are_not_fused) {
  const double t = g_mix_weight;
  int fused = 0;
  for (int a = 0; a < 256; a++)
    for (int b = 0; b < 256; b++)
      if (mix_as_written(a, b, t) != mix_rounded_each_step(a, b, t)) fused++;
  CHECK_MSG(fused == 0, std::to_string(fused) + " pairs differ: build with -ffp-contract=off");
}

// The frozen sources of engine 0.1.0 (frozen/0.1.0, built by run.sh as
// test_frozen and run before this program) drew the frames of frames.h into
// TAB_FROZEN_FRAMES. This engine draws the same run, and every frame must be
// the same, byte for byte: version 1 pixels do not move.
TEST(v1_pixels_unchanged) {
  const char *env = getenv("TAB_FROZEN_FRAMES");
  if (!CHECK_MSG(env && *env, "TAB_FROZEN_FRAMES is not set: run engine_tests/run.sh, which draws engine 0.1.0's frames first"))
    return;
  const std::string dir = env;
  FILE *lf = fopen((dir + "/frames.txt").c_str(), "r");
  if (!CHECK_MSG(lf != nullptr, dir + "/frames.txt is missing: engine 0.1.0 did not run")) return;
  std::string old_list;
  for (int c; (c = fgetc(lf)) != EOF;) old_list += (char) c;
  fclose(lf);

  shutdown();
  std::string list;
  int frames_seen = 0;
  const std::string err = frames::run(CDIR, g_fonts, [&](const std::string &name) {
    list += name + "\n";
    frames_seen++;
    std::vector<uint16_t> old;
    if (!CHECK_MSG(frames::read(dir + "/" + name + ".raw", &old), name + ": engine 0.1.0 drew no such frame")) return;
    long differ = 0, first = -1;
    for (size_t i = 0; i < old.size(); i++)
      if (old[i] != hh::g_fb[i]) {
        if (first < 0) first = (long) i;
        differ++;
      }
    if (differ > 0) {
      shot("v1_pixels_" + name);
      CHECK_MSG(false, name + ": " + std::to_string(differ) + " pixels differ, the first at (" +
                           std::to_string(first % hh::W) + ", " + std::to_string(first / hh::W) + "); this engine's frame is " +
                           SHOTS + "/v1_pixels_" + name + ".png");
    }
  });
  CHECK_MSG(err.empty(), err);
  CHECK_MSG(list == old_list, "the frames differ from engine 0.1.0's list");
  // Both fixtures, every page, without and with values.
  CHECK_MSG(tab::page_count() > 1 && frames_seen >= 1 + 3 * tab::page_count(), std::to_string(frames_seen));
}

// ================================================================ main

int main(int argc, char **argv) {
  if (argc < 5) {
    fprintf(stderr, "usage: %s <contract dir> <text ttf> <icon ttf> <out dir> [--quick] [name ...]\n", argv[0]);
    return 2;
  }
  CDIR = argv[1];
  OUT = argv[4];
  SHOTS = getenv("TAB_SHOTS_DIR") ? getenv("TAB_SHOTS_DIR") : OUT + "/shots";
  std::vector<std::string> only;
  for (int i = 5; i < argc; i++) {
    if (!strcmp(argv[i], "--quick"))
      QUICK = true;
    else
      only.push_back(argv[i]);
  }
  std::string mk = "mkdir -p '" + SHOTS + "'";
  if (system(mk.c_str()) != 0) return 2;
  if (!load_screens()) {
    fprintf(stderr, "%s/screens.json lacks a waiting screen or a Wi-Fi note\n", CDIR.c_str());
    return 2;
  }

  hh::init();
  hh::g_each_ms = fake::pump;
  size_t tn = 0, in = 0;
  uint8_t *text = hh::load_file(argv[2], &tn);
  uint8_t *icon = hh::load_file(argv[3], &in);
  // The tablet's fonts, at contract.json's sizes, hold only the contract's
  // characters; so do these (tools/gen_engine_fonts.py reads the same keys).
  const tj::Value contract = tj::load(CDIR + "/contract.json");
  const tj::Value &cf = contract["fonts"];
  const size_t n_text = sizeof g_fonts.text / sizeof g_fonts.text[0];
  const size_t n_digits = sizeof g_fonts.digits / sizeof g_fonts.digits[0];
  const size_t n_icon = sizeof g_fonts.icon / sizeof g_fonts.icon[0];
  if (cf["text_px"].size() != n_text || cf["digits_px"].size() != n_digits || cf["icon_px"].size() != n_icon) {
    fprintf(stderr, "contract.json fonts has %zu text, %zu digits and %zu icon sizes; tab::Fonts holds %zu, %zu and %zu\n",
            cf["text_px"].size(), cf["digits_px"].size(), cf["icon_px"].size(), n_text, n_digits, n_icon);
    return 2;
  }
  const std::vector<uint32_t> text_ranges = hf::text_ranges(contract), digit_ranges = hf::digit_ranges(contract);
  if (text_ranges.empty() || digit_ranges.empty()) {
    fprintf(stderr, "contract.json has no fonts.text_glyphs, or no fonts.digits_glyphs in UTF-8\n");
    return 2;
  }
  for (size_t i = 0; i < n_text; i++)
    g_fonts.text[i] = hh::limited_font(hh::ttf_font(text, tn, cf["text_px"][i].i(), "text"), text_ranges);
  for (size_t i = 0; i < n_digits; i++)
    g_fonts.digits[i] = hh::limited_font(hh::ttf_font(text, tn, cf["digits_px"][i].i(), "digits"), digit_ranges);
  for (size_t i = 0; i < n_icon; i++) g_fonts.icon[i] = hh::ttf_font(icon, in, cf["icon_px"][i].i(), "icons");

  int ran = 0;
  for (const TestCase &t : registry()) {
    if (!only.empty() && std::find(only.begin(), only.end(), t.name) == only.end()) continue;
    g_test = t.name;
    const int before = g_failed;
    printf("- %s\n", t.name);
    fflush(stdout);
    t.fn();
    shutdown();
    ran++;
    printf("  %s\n", g_failed == before ? "ok" : "FAILED");
  }
  printf("\n%d tests, %d checks, %d failed\n", ran, g_checks, g_failed);
  for (auto &f : g_failures) printf("  %s\n", f.c_str());
  return g_failed == 0 ? 0 : 1;
}

// ================================================================ device layout version 2: parsing (package E2)
//
// Contract 4.2, 4.3 and 4.8, cards spec B.2. The keys of version 2 are read
// and kept; most are drawn by later packages, so these tests look at the
// model, and draw and press every layout that loads to show that nothing
// breaks.

// A JSON writer for tj values, for layouts made from the fixtures.
static void json_out(const tj::Value &v, std::string &o) {
  switch (v.t) {
    case tj::Value::NUL: o += "null"; break;
    case tj::Value::BOOL: o += v.b ? "true" : "false"; break;
    case tj::Value::NUM: o += v.s; break;
    case tj::Value::STR:
      o += '"';
      for (unsigned char c : v.s) {
        if (c == '"' || c == '\\') {
          o += '\\';
          o += (char) c;
        } else if (c < 0x20) {
          char b[8];
          snprintf(b, sizeof b, "\\u%04x", c);
          o += b;
        } else {
          o += (char) c;
        }
      }
      o += '"';
      break;
    case tj::Value::ARR:
      o += '[';
      for (size_t i = 0; i < v.a.size(); i++) {
        if (i) o += ',';
        json_out(v.a[i], o);
      }
      o += ']';
      break;
    case tj::Value::OBJ:
      o += '{';
      for (size_t i = 0; i < v.o.size(); i++) {
        if (i) o += ',';
        tj::Value k;
        k.t = tj::Value::STR;
        k.s = v.o[i].first;
        json_out(k, o);
        o += ':';
        json_out(v.o[i].second, o);
      }
      o += '}';
      break;
  }
}
static std::string json_text(const tj::Value &v) {
  std::string o;
  json_out(v, o);
  return o;
}

static std::string commit_answer(const std::string &bytes) {
  D::Loader ld;
  return D::model_check(bytes.data(), bytes.size(), ld);
}

// A version 2 layout: one page, one card holding `widgets`, slots "tnbilnn"
// (1 text, 2 number, 3 bool, 4 icon, 5 list, 6 and 7 numbers).
static const char *V2_SLOTS = "tnbilnn";
static std::string v2_card(const std::string &widgets, const std::string &card_keys = "",
                           const std::string &page_keys = "", int v = 2) {
  return "{\"nav\":[{\"p\":0,\"title\":\"P\"}],\"pages\":[{" + page_keys + "\"title\":\"P\",\"w\":[{" + card_keys +
         "\"id\":1,\"r\":[100,84,568,640],\"t\":\"card\",\"w\":[" + widgets + "]}]}],\"slots\":\"" + V2_SLOTS +
         "\",\"v\":" + std::to_string(v) + "}";
}

// Every page drawn, every widget pressed at its centre and dragged across:
// nothing may crash or leak (run under ASan and UBSan too).
static void draw_and_press_all(const char *label) {
  D::Layout *l = active();
  if (!CHECK_MSG(l && l->model, label)) return;
  const int pages = tab::page_count();
  for (int p = 0; p < pages; p++) {
    tab::go_to(p);
    hh::advance(30);
    l = active();
    if (!l || !l->model) return;
    D::Model &m = *l->model;
    for (uint32_t i = 0; i < m.nw; i++) {
      const D::Widget &w = m.w[i];
      if (w.page != p || !w.obj || w.r[2] <= 0 || w.r[3] <= 0) continue;
      const int x = centre_x(w.r), y = centre_y(w.r);
      hh::tap(x, y);
      hh::advance(10);
      hh::drag(w.r[0] + 1, y, w.r[0] + w.r[2] - 1, y, 120);
      hh::advance(10);
      if (!hidden(D::g.question)) D::question_answer(false);
      hh::advance(10);
      if (tab::current_page() != p) {
        tab::go_to(p);
        hh::advance(20);
      }
      l = active();
      if (!l || !l->model || &l->model->w[0] != m.w) return;  // another layout came up
    }
  }
  tab::go_to(0);
  hh::advance(20);
}

// One commit on a fresh, unpaired tablet: the answer, and when it is "" the
// layout on screen, drawn and pressed.
static std::string commit_and_draw(Ha &ha, const std::string &bytes, const char *label, bool press = true) {
  ha.boot(false);
  ha.hello();
  const std::string hash = sha_hex(bytes).substr(0, 16);
  const std::string sent = ha.send(bytes);
  if (!sent.empty()) return sent;
  const std::string got = ha.commit(hash);
  if (!got.empty()) return got;
  if (!CHECK_MSG(advance_until([&] { return hash == tab::active_hash(); }, 5000), label)) return "not shown";
  if (press) draw_and_press_all(label);
  return "";
}

static const tj::Value *find_item(const tj::Value &dl, uint32_t id) {
  for (const tj::Value &p : dl["pages"].a)
    for (const tj::Value &it : p["w"].a) {
      if (it["id"].n == id) return &it;
      for (const tj::Value &w : it["w"].a)
        if (w["id"].n == id) return &w;
    }
  return nullptr;
}

// A bound field of the model against its form in the layout.
static bool same_bound(const D::Bound &b, const tj::Value &v) {
  if (v.t == tj::Value::NUL) return b.how == D::B_NULL || (b.how == D::B_ABSENT && v.s.empty());
  if (v.t == tj::Value::OBJ) return b.how == D::B_SLOT && b.slot == (uint16_t) v["s"].n;
  if (v.t == tj::Value::NUM) return b.how == D::B_CONST && b.c.num == v.n;
  if (v.t == tj::Value::BOOL) return b.how == D::B_CONST && b.c.num == (v.b ? 1 : 0);
  return false;
}

static bool same_list(const double *got, int n, const tj::Value &want) {
  if (n != (int) want.size()) return false;
  for (int i = 0; i < n; i++)
    if (!got || got[i] != want[i].n) return false;
  return true;
}

// Every key of version 2 of every widget, as the model holds it, against the
// layout's bytes (contract 4.3: absent keys give the version 1 look).
static void check_v2_keys(const tj::Value &dl, const D::Model &m, const char *label) {
  for (uint32_t i = 0; i < m.nw; i++) {
    const D::Widget &w = m.w[i];
    const tj::Value *it = find_item(dl, w.id);
    const std::string id = std::string(label) + " widget " + std::to_string(w.id);
    if (!CHECK_MSG(it, id)) continue;
    const tj::Value &v = *it;
    auto num = [&](const char *k, double def) { return v.has(k) ? v[k].n : def; };
    if (w.kind == D::W_CARD) {
      CHECK_MSG(v.has("tint") ? same_bound(w.c, v["tint"]) : w.c.how == D::B_ABSENT, id + " tint");
      CHECK_MSG(v.has("un") ? same_bound(w.vis, v["un"]) : w.vis.how == D::B_ABSENT, id + " un");
      CHECK_MSG(v.has("ic") ? same_bound(w.ic, v["ic"]) : w.ic.how == D::B_ABSENT, id + " ic");
      CHECK_MSG(w.lit == num("lit", 0), id + " lit");
    } else {
      if (w.kind != D::W_HEADING)
        CHECK_MSG(v.has("vis") ? same_bound(w.vis, v["vis"]) : w.vis.how == D::B_ABSENT, id + " vis");
      if (w.kind != D::W_CHOICE) CHECK_MSG(v.has("c") ? same_bound(w.c, v["c"]) : w.c.how == D::B_ABSENT, id + " c");
    }
    const tj::Value &tap = v["tap"];
    const int tap_kind = tap.has("p") ? 1 : tap.has("go") ? 2 : tap.has("back") ? 3 : 0;
    CHECK_MSG(w.tap == tap_kind, id + " tap");
    if (tap_kind == 2) CHECK_MSG(w.tap_v == tap["go"].n, id + " tap go");
    const tj::Value &hold = v["hold"];
    CHECK_MSG(w.hold == (hold.has("go") ? 2 : 0) && (!hold.has("go") || w.hold_v == hold["go"].n), id + " hold");
    switch (w.kind) {
      case D::W_LABEL:
        CHECK_MSG(w.px == num("px", 0) && w.al == num("al", 0) && w.clk == num("clk", 0), id + " px al clk");
        if (v.has("clk")) CHECK_MSG(same_bound(w.value, v["value"]), id + " clock value");
        break;
      case D::W_BUTTON:
        CHECK_MSG(w.shape == num("shape", 0) && w.isz == num("isz", 0), id + " shape isz");
        CHECK_MSG(v.has("value") ? same_bound(w.value, v["value"]) : w.value.how == D::B_ABSENT, id + " value");
        CHECK_MSG(v.has("ic") ? same_bound(w.ic, v["ic"]) : w.ic.how == D::B_ABSENT, id + " ic");
        break;
      case D::W_SLIDER:
        CHECK_MSG(w.look == num("look", 0) && w.dir == num("dir", 0) && w.nv == num("nv", 0), id + " look dir nv");
        CHECK_MSG(v.has("rate") ? same_bound(w.mark, v["rate"]) : w.mark.how == D::B_ABSENT, id + " rate");
        CHECK_MSG(v.has("track") ? same_list(w.track, w.track_n, v["track"]) : w.track_n == 0 && !w.track, id + " track");
        break;
      case D::W_THERMO:
        CHECK_MSG(w.bs == num("bs", 36), id + " bs");
        CHECK_MSG(v.has("mark") ? same_bound(w.mark, v["mark"]) : w.mark.how == D::B_ABSENT, id + " mark");
        break;
      case D::W_CHART:
        CHECK_MSG(w.st == num("st", 0), id + " st");
        CHECK_MSG(v.has("mark") ? same_bound(w.mark, v["mark"]) : w.mark.how == D::B_ABSENT, id + " mark");
        break;
      case D::W_IMAGE: CHECK_MSG(w.look == num("look", 0), id + " look"); break;
      case D::W_CHOICE: {
        CHECK_MSG(w.rows == num("rows", 1) && w.look == num("look", 0), id + " rows look");
        CHECK_MSG(v.has("value") ? same_bound(w.value, v["value"]) : w.value.how == D::B_ABSENT, id + " value");
        CHECK_MSG(same_list(w.icons, w.icons_n, v["icons"]) && same_list(w.cols, w.cols_n, v["cols"]), id + " lists");
        CHECK_MSG(w.texts_n == v["texts"].size(), id + " texts");
        for (uint32_t k = 0; k < w.texts_n; k++) CHECK_MSG(D::choice_text(w, k) == v["texts"][(size_t) k].s, id + " text");
        const size_t most = std::max(v["texts"].size(), std::max(v["icons"].size(), v["cols"].size()));
        CHECK_MSG(w.options == most, id + " options");
        break;
      }
      default: break;
    }
  }
}

TEST(kit_loads) {
  for (const char *name : {"layout_v2_kit", "layout_v2_kit.nochoice"}) {
    g_test = name;
    const std::string bytes = layout_bytes((std::string(name) + ".dl.json").c_str());
    const tj::Value dl = fixture((std::string(name) + ".dl.json").c_str());
    const tj::Value meta = fixture((std::string(name) + ".meta.json").c_str());
    CHECK_STR(sha_hex(bytes).substr(0, 16), meta["hash"].s);
    D::Loader ld;
    CHECK_STR(D::model_check(bytes.data(), bytes.size(), ld), "");
    CHECK(ld.c.version == 2);
    CHECK(ld.c.pages == meta["counts"]["pages"].n && ld.c.normal == meta["counts"]["normal_pages"].n);
    CHECK(ld.c.widgets == meta["counts"]["widgets"].n && ld.c.ids == meta["counts"]["widgets"].n);
    CHECK(ld.c.slots == meta["counts"]["slots"].n);
    Ha ha;
    ha.boot(false);
    CHECK(ha.hello());
    CHECK(ha.show(bytes));
    D::Layout *l = active();
    if (!CHECK(l && l->model)) continue;
    D::Model &m = *l->model;
    // Pages: sub, ttl and theme (contract 4.8).
    CHECK(m.npages == dl["pages"].size());
    int themed = 0;
    for (uint32_t p = 0; p < m.npages; p++) {
      const tj::Value &pg = dl["pages"][(size_t) p];
      const D::Page &mp = m.pages[p];
      CHECK_MSG(mp.sub == (pg["sub"].n == 1 && p > 0 ? 1 : 0), "page " + std::to_string(p) + " sub");
      CHECK_MSG(mp.ttl == (pg.has("ttl") ? pg["ttl"].n : 60), "page " + std::to_string(p) + " ttl");
      if (!pg.has("theme")) {
        CHECK(mp.theme == 0);
        continue;
      }
      themed++;
      CHECK(mp.theme > 0 && mp.theme < m.nthemes);
      const D::Theme &t = m.themes[mp.theme];
      for (int c = 0; c < D::C_COUNT; c++) {
        const tj::Value &mine = pg["theme"][D::THEME_KEYS[c]];
        CHECK_MSG(t.c[c] == (mine.t == tj::Value::NUM ? (uint32_t) mine.n : m.theme.c[c]), D::THEME_KEYS[c]);
      }
      CHECK(t.r == (pg["theme"].has("r") ? pg["theme"]["r"].n : m.theme.r));
      CHECK(t.dark == (pg["theme"].has("dark") ? pg["theme"]["dark"].n : m.theme.dark));
    }
    CHECK(m.nthemes == 1 + themed && themed == 1);
    CHECK(memcmp(&m.themes[0], &m.theme, sizeof m.theme) == 0);
    check_v2_keys(dl, m, name);
    // A card's style with the six colours of version 2 carries them whole.
    const D::Widget *c7 = widget(7);
    if (CHECK(c7 && c7->style >= 0)) {
      const D::Tok t = D::card_tokens(m, c7);
      const tj::Value &st = find_item(dl, 7)->operator[]("style");
      CHECK(t.card_pr == st["card_pr"].n && t.card2_pr == st["card2_pr"].n && t.accf == st["accf"].n &&
            t.accf_pr == st["accf_pr"].n && t.onbg_pr == st["onbg_pr"].n && t.ion == st["ion"].n);
    }
    // Every widget the engine draws so far is at its rectangle, a round
    // button and a badge at their circle's square.
    for (uint32_t i = 0; i < m.nw; i++) {
      const D::Widget &w = m.w[i];
      const std::string id = std::string(name) + " widget " + std::to_string(w.id);
      if (w.kind == D::W_CHOICE) continue;  // drawn from package E8 on
      if (!CHECK_MSG(w.obj, id)) continue;
      // A round button and a badge: the centred circle's square (package E5).
      int bx = 0, by = 0, bw = w.r[2], bh = w.r[3];
      if (w.kind == D::W_BUTTON) D::button_box(w, &bx, &by, &bw, &bh);
      if (w.kind == D::W_IMAGE) D::image_box(w, &bx, &by, &bw, &bh);
      CHECK_MSG(obj_at(w.obj, w.r[0] + bx, w.r[1] + by, bw, bh), id + " at " + rect_text(w.obj));
    }
    // The values of state_v2.json's stream are all taken.
    if (!strcmp(name, "layout_v2_kit")) {
      const tj::Value st = fixture("state_v2.json");
      CHECK_STR(st["hash"].s, meta["hash"].s);
      for (const tj::Value &step : st["steps"].a)
        for (const tj::Value &pl : step["payloads"].a) CHECK_STR(ha.set(pl.s), "");
      CHECK_STR(ha.set(st["steps"][0]["payloads"][0].s), "");
    }
    hh::advance(40);
    for (int p = 0; p < tab::page_count(); p++) {
      tab::go_to(p);
      hh::advance(40);
      CHECK(tab::current_page() == p);
      shot(std::string(name) + "_page" + std::to_string(p));
      // Version 1 looks: the layout's wall on every page until package E9
      // draws page themes. Page 2 holds one small card (184 x 184, about
      // 3.6 % of the page area), so the bar is lower than shots_of_pages'.
      const double share = share_not(hh::rgb565(m.theme.c[D::C_WALL]), 96);
      CHECK_MSG(share > 0.025, std::string(name) + " page " + std::to_string(p) + ": " + std::to_string(share));
    }
    tab::go_to(0);
    hh::advance(20);
    g_cut_report.clear();
    report_cuts(name);
    const std::string path = OUT + "/cut_texts.txt";
    FILE *f = fopen(path.c_str(), "a");
    if (f) {
      fprintf(f, "%s", g_cut_report.empty() ? (std::string(name) + ": no text is cut.\n").c_str() : g_cut_report.c_str());
      fclose(f);
    }
    draw_and_press_all(name);
  }
  g_test = "kit_loads";
}

TEST(versions) {
  // v 1 and 2 load, 3 answers version (contract 4.2, 5).
  CHECK_STR(commit_answer(v2_card("", "", "", 1)), "");
  CHECK_STR(commit_answer(v2_card("", "", "", 2)), "");
  CHECK_STR(commit_answer(v2_card("", "", "", 3)), "version");
  CHECK_STR(commit_answer(v2_card("", "", "", 0)), "version");
  CHECK_STR(commit_answer("{\"pages\":[],\"v\":2.0}"), "");
  CHECK_STR(commit_answer("{\"pages\":[],\"v\":1.5}"), "version");
  CHECK_STR(commit_answer("{\"pages\":7,\"v\":3}"), "version");  // version before shape
  CHECK_STR(commit_answer("{\"pages\":[],\"v\":true}"), "parse");
  {
    D::Loader ld;
    const std::string one = v2_card("", "", "", 1), two = v2_card("", "", "", 2);
    D::model_check(one.data(), one.size(), ld);
    CHECK(ld.c.version == 1);
    D::model_check(two.data(), two.size(), ld);
    CHECK(ld.c.version == 2);
  }
  // Every case of commit_v2.json, each one transfer on a fresh tablet that
  // is not paired, as in chunks.json.
  const tj::Value f = fixture("commit_v2.json");
  CHECK(f["cases"].size() == 13);
  Ha ha;
  for (const tj::Value &c : f["cases"].a) {
    g_test = c["name"].s.c_str();
    ha.boot(false);
    CHECK(c["calls"].size() == c["expect"].size());
    for (size_t i = 0; i < c["calls"].size(); i++) {
      const tj::Value &k = c["calls"][i];
      CHECK_STR(tab::on_layout(k["hash"].s.data(), k["hash"].s.size(), k["seq"].i(), k["total"].i(), k["size"].i(),
                               k["data"].s.data(), k["data"].s.size()),
                c["expect"][i].s == "ok" ? "" : c["expect"][i].s);
    }
    const std::string hash = c["commit_hash"].s;
    CHECK_STR(tab::on_commit(hash.data(), hash.size(), "", 0), c["commit"].s == "ok" ? "" : c["commit"].s);
    if (c["commit"].s == "ok") {
      CHECK(advance_until([&] { return hash == tab::active_hash(); }, 5000));
      CHECK(tab::page_count() > 0);
    } else {
      hh::advance(50);
      CHECK_STR(tab::active_hash(), "");
    }
    // What the tablet keeps of lists that are too long: what HA's compile
    // cuts them to (`cuts`), the first 8 colours, the first 16 options, the
    // texts cut to 40 bytes at a character border (contract 4.3).
    if (c["name"].s == "uncut_lists") {
      for (auto &kv : f["cuts"]["widgets"].o) {
        const D::Widget *w = widget((uint32_t) atoi(kv.first.c_str()));
        const tj::Value &want = kv.second;
        if (!CHECK_MSG(w, kv.first)) continue;
        if (want.has("track")) CHECK_MSG(same_list(w->track, w->track_n, want["track"]), kv.first + " track");
        if (want.has("icons")) CHECK_MSG(same_list(w->icons, w->icons_n, want["icons"]), kv.first + " icons");
        if (want.has("cols")) CHECK_MSG(same_list(w->cols, w->cols_n, want["cols"]), kv.first + " cols");
        if (want.has("texts")) {
          CHECK_MSG(w->texts_n == want["texts"].size(), kv.first + " texts");
          for (uint32_t k = 0; k < w->texts_n; k++)
            CHECK_MSG(D::choice_text(*w, k) == want["texts"][(size_t) k].s, kv.first + " text " + std::to_string(k));
        }
      }
    }
  }
  g_test = "versions";
  // The page records of a case at the limit: page 0 is normal, the rest are
  // detail pages with the default ttl.
  ha.boot(false);
  const tj::Value &c64 = f["cases"][0];
  CHECK_STR(c64["name"].s, "pages_64");
  CHECK(ha.show(c64["calls"][0]["data"].s));
  if (CHECK(active() && active()->model && active()->model->npages == 64)) {
    const D::Model &m = *active()->model;
    CHECK(m.pages[0].sub == 0);
    for (int p = 1; p < 64; p++) CHECK(m.pages[p].sub == 1 && m.pages[p].ttl == 60);
  }
}

TEST(hostile_v2) {
  struct Case {
    const char *name;
    std::string bytes;
    const char *want;
  };
  const std::string S = "\"r\":[116,140,344,40],\"t\":\"slider\",\"value\":{\"s\":2},\"min\":0,\"max\":100,\"step\":1";
  const std::string L = "\"r\":[116,140,344,40],\"t\":\"label\",\"text\":\"x\"";
  const std::string B = "\"r\":[116,140,344,40],\"t\":\"button\",\"text\":\"x\",\"tap\":{\"p\":0}";
  const std::string C = "\"r\":[116,140,344,96],\"t\":\"choice\",\"change\":{\"p\":0}";
  const std::string T = "\"r\":[116,140,248,228],\"t\":\"thermostat\",\"value\":{\"s\":2},\"min\":7,\"max\":35,\"step\":0.5";
  const std::string H = "\"r\":[116,140,344,104],\"t\":\"chart\",\"value\":{\"s\":5}";
  const std::string I = "\"r\":[116,140,64,64],\"t\":\"image\",\"value\":{\"s\":4}";
  auto w = [](const std::string &keys, const std::string &base) { return "{\"id\":2," + keys + (keys.empty() ? "" : ",") + base + "}"; };
  std::string texts1000 = "[", texts_bad = "[";
  for (int i = 0; i < 1000; i++) {
    texts1000 += std::string(i ? "," : "") + "\"t" + std::to_string(i) + "\"";
    texts_bad += std::string(i ? "," : "") + (i == 500 ? "5" : "\"t\"");
  }
  texts1000 += "]";
  texts_bad += "]";
  std::string pages64 = "[";
  for (int p = 0; p < 64; p++)
    pages64 += std::string(p ? "," : "") + "{\"sub\":" + (p ? "1" : "0") + ",\"theme\":{\"acc\":" + std::to_string(p * 1000) +
               "},\"title\":\"P\",\"w\":[]}";
  pages64 += "]";
  const std::vector<Case> cases = {
      // Wrong JSON types are parse (contract 4.3).
      {"px text", v2_card(w("\"px\":\"28\"", L)), "parse"},
      {"al bool", v2_card(w("\"al\":true", L)), "parse"},
      {"look list", v2_card(w("\"look\":[1]", S)), "parse"},
      {"track text", v2_card(w("\"track\":\"red\"", S)), "parse"},
      {"track slot", v2_card(w("\"track\":{\"s\":2}", S)), "parse"},
      {"track entry text", v2_card(w("\"track\":[1,\"a\"]", S)), "parse"},
      {"track entry fraction", v2_card(w("\"track\":[1,2.5]", S)), "parse"},
      {"track entry over", v2_card(w("\"track\":[1,16777216]", S)), "parse"},
      {"track entry under", v2_card(w("\"track\":[-1,2]", S)), "parse"},
      {"track 10th entry bad", v2_card(w("\"track\":[1,2,3,4,5,6,7,8,9,\"x\"]", S)), "parse"},
      {"texts text", v2_card(w("\"texts\":\"abc\"", C)), "parse"},
      {"texts entry number", v2_card(w("\"texts\":[1]", C)), "parse"},
      {"texts entry null", v2_card(w("\"texts\":[\"a\",null]", C)), "parse"},
      {"texts 501st entry number", v2_card(w("\"texts\":" + texts_bad, C)), "parse"},
      {"icons negative", v2_card(w("\"icons\":[-1]", C)), "parse"},
      {"icons over", v2_card(w("\"icons\":[1114112]", C)), "parse"},
      {"cols over", v2_card(w("\"cols\":[16777216]", C)), "parse"},
      {"cols text", v2_card(w("\"cols\":[\"#ff0000\"]", C)), "parse"},
      {"c text", v2_card(w("\"c\":\"#ffffff\"", L)), "parse"},
      {"c 16777216", v2_card(w("\"c\":16777216", L)), "parse"},
      {"c fraction", v2_card(w("\"c\":0.5", L)), "parse"},
      {"c negative", v2_card(w("\"c\":-1", L)), "parse"},
      {"c bool", v2_card(w("\"c\":true", L)), "parse"},
      {"c text slot", v2_card(w("\"c\":{\"s\":1}", L)), "parse"},
      {"tint bool", v2_card("", "\"tint\":true,"), "parse"},
      {"tint 16777216", v2_card("", "\"tint\":16777216,"), "parse"},
      {"heading c text", "{\"pages\":[{\"title\":\"P\",\"w\":[{\"c\":\"x\",\"id\":1,\"r\":[0,0,9,9],\"t\":\"heading\",\"text\":\"h\"}]}],\"v\":2}", "parse"},
      {"vis text", v2_card(w("\"vis\":\"x\"", L)), "parse"},
      {"vis number slot", v2_card(w("\"vis\":{\"s\":2}", L)), "parse"},
      {"un number slot", v2_card("", "\"un\":{\"s\":2},"), "parse"},
      {"ic text", v2_card("", "\"ic\":\"x\","), "parse"},
      {"ic bool slot", v2_card(w("\"ic\":{\"s\":3}", B)), "parse"},
      {"lit text", v2_card("", "\"lit\":\"x\","), "parse"},
      {"hold number", v2_card(w("\"hold\":7", L)), "parse"},
      {"hold policy 9", v2_card(w("\"hold\":{\"p\":9}", L)), "parse"},
      {"card hold text", v2_card("", "\"hold\":\"x\","), "parse"},
      {"back text", v2_card(w("\"tap\":{\"back\":\"x\"}", L)), "parse"},
      {"back fraction", v2_card(w("\"tap\":{\"back\":0.5}", L)), "parse"},
      {"mark text", v2_card(w("\"mark\":\"x\"", T)), "parse"},
      {"mark list slot", v2_card(w("\"mark\":{\"s\":5}", H)), "parse"},
      {"rate bool", v2_card(w("\"rate\":true", S)), "parse"},
      {"clock value text", v2_card(w("\"clk\":1,\"value\":\"x\"", L)), "parse"},
      {"button value text", v2_card(w("\"value\":\"x\"", B)), "parse"},
      {"choice value text", v2_card(w("\"texts\":[\"a\"],\"value\":\"x\"", C)), "parse"},
      {"choice value list slot", v2_card(w("\"texts\":[\"a\"],\"value\":{\"s\":5}", C)), "parse"},
      {"style ion over", v2_card("", "\"style\":{\"ion\":16777216},"), "parse"},
      {"style card_pr text", v2_card("", "\"style\":{\"card_pr\":\"x\"},"), "parse"},
      {"sub text", v2_card("", "", "\"sub\":\"1\","), "parse"},
      {"ttl text", v2_card("", "", "\"ttl\":\"60\","), "parse"},
      {"theme number", v2_card("", "", "\"theme\":5,"), "parse"},
      {"theme colour text", v2_card("", "", "\"theme\":{\"acc\":\"x\"},"), "parse"},
      {"theme colour over", v2_card("", "", "\"theme\":{\"acc\":16777216},"), "parse"},
      {"theme r over", v2_card("", "", "\"theme\":{\"r\":401},"), "parse"},
      // A fraction in a look key, in clk, nv or sub is parse.
      {"px fraction", v2_card(w("\"px\":28.5", L)), "parse"},
      {"ttl fraction", v2_card("", "", "\"ttl\":1.5,"), "parse"},
      {"clk fraction", v2_card(w("\"clk\":1.5", L)), "parse"},
      {"nv fraction", v2_card(w("\"nv\":0.5", S)), "parse"},
      {"sub fraction", v2_card("", "", "\"sub\":0.5,"), "parse"},
      {"rows fraction", v2_card(w("\"texts\":[\"a\"],\"rows\":2.5", C)), "parse"},
      {"bs fraction", v2_card(w("\"bs\":40.5", T)), "parse"},
      {"lit fraction", v2_card("", "\"lit\":1.5,"), "parse"},
      // These load.
      {"every new key null",
       v2_card(w("\"vis\":null,\"hold\":null,\"c\":null,\"px\":null,\"al\":null,\"clk\":null", L) + "," +
                   w("\"vis\":null,\"look\":null,\"dir\":null,\"track\":null,\"nv\":null,\"rate\":null,\"c\":null", S) + "," +
                   w("\"vis\":null,\"texts\":null,\"icons\":null,\"cols\":null,\"value\":null,\"rows\":null,\"look\":null", C) + "," +
                   w("\"mark\":null,\"bs\":null,\"c\":null", T) + "," + w("\"mark\":null,\"st\":null", H) + "," +
                   w("\"look\":null,\"c\":null", I) + "," + w("\"value\":null,\"ic\":null,\"shape\":null,\"isz\":null", B),
               "\"hold\":null,\"ic\":null,\"tint\":null,\"lit\":null,\"un\":null,", "\"sub\":null,\"ttl\":null,\"theme\":null,"),
       ""},
      {"constant colour 16777215", v2_card(w("\"c\":16777215", L)), ""},
      {"colour slot", v2_card(w("\"c\":{\"s\":2}", L)), ""},
      {"unknown clk", v2_card(w("\"clk\":7,\"value\":{\"s\":2}", L)), ""},
      {"choice without a list", v2_card(w("", C)), ""},
      {"choice with empty lists", v2_card(w("\"texts\":[],\"icons\":[],\"cols\":[]", C)), ""},
      {"choice without value", v2_card(w("\"texts\":[\"a\",\"b\"]", C)), ""},
      {"choice of 1000 texts", v2_card(w("\"texts\":" + texts1000, C)), ""},
      {"64 pages with a theme each",
       "{\"nav\":[],\"pages\":" + pages64 + ",\"slots\":\"\",\"theme\":{\"acc\":1},\"v\":2}", ""},
      {"go to a missing page", v2_card(w("\"tap\":{\"go\":9},\"hold\":{\"go\":9}", L), "\"hold\":{\"go\":9},"), ""},
      {"hold with p or back", v2_card(w("\"hold\":{\"p\":1}", L) + "," + w("\"hold\":{\"back\":1}", B)), ""},
      {"hold on a slider is not a key there", v2_card(w("\"hold\":\"x\"", S)), ""},
      {"back 2 does nothing", v2_card(w("\"tap\":{\"back\":2}", B)), ""},
  };
  for (const Case &c : cases) {
    g_test = c.name;
    CHECK_STR(commit_answer(c.bytes), c.want);
  }
  g_test = "hostile_v2";
  Ha ha;
  // Look keys out of range are held to the nearest value (contract 4.3).
  {
    const std::string held = v2_card(
        "{\"id\":2,\"px\":3,\"al\":-5," + L + "},{\"id\":3,\"px\":1000000000,\"al\":9," + L + "},{\"id\":4,\"look\":5,\"dir\":7,\"nv\":2," + S +
            "},{\"id\":5,\"look\":-1,\"dir\":-1,\"nv\":1," + S + "},{\"id\":6,\"texts\":[\"a\"],\"rows\":0,\"look\":3," + C +
            "},{\"id\":7,\"texts\":[\"a\"],\"rows\":9," + C + "},{\"id\":8,\"bs\":10," + T + "},{\"id\":9,\"bs\":99," + T +
            "},{\"id\":10,\"st\":3," + H + "},{\"id\":11,\"look\":9," + I + "},{\"id\":12,\"shape\":9,\"isz\":4," + B +
            "},{\"id\":13,\"shape\":-2,\"isz\":-1," + B + "},{\"id\":14,\"clk\":2,\"value\":{\"s\":2}," + L + "},{\"id\":15,\"clk\":0," + L +
            "},{\"id\":16,\"clk\":-1," + L + "},{\"id\":17,\"tap\":{\"back\":1}," + L + "}",
        "\"lit\":7,");
    CHECK_STR(commit_and_draw(ha, held, "held"), "");
    auto W = [](uint32_t id) { return widget(id); };
    if (CHECK(W(1) && W(2) && W(3) && W(4) && W(5) && W(6) && W(7) && W(8) && W(9) && W(10) && W(11) && W(12) && W(13) &&
              W(14) && W(15) && W(16) && W(17))) {
      CHECK(W(1)->lit == 3);
      CHECK(W(2)->px == 14 && W(2)->al == 0 && W(3)->px == 255 && W(3)->al == 2);
      CHECK(W(4)->look == 1 && W(4)->dir == 2 && W(4)->nv == 0 && W(5)->look == 0 && W(5)->dir == 0 && W(5)->nv == 1);
      CHECK(W(6)->rows == 1 && W(6)->look == 1 && W(7)->rows == 4);
      CHECK(W(8)->bs == 36 && W(9)->bs == 64 && W(10)->st == 1 && W(11)->look == 2);
      CHECK(W(12)->shape == 2 && W(12)->isz == 1 && W(13)->shape == 0 && W(13)->isz == 0);
      CHECK(W(14)->clk == 2 && W(14)->value.how == D::B_SLOT && W(15)->clk == 0 && W(16)->clk == 0);
      CHECK(W(17)->tap == 3);
    }
  }
  // ttl: below 0 is 0, 1 to 4 is 5, above 3600 is 3600; sub 2 counts as 0.
  {
    std::string pages = "[{\"title\":\"0\",\"w\":[]}";
    const int ttls[] = {-1, 0, 1, 3, 4, 5, 3600, 4000};
    for (int t : ttls) pages += ",{\"sub\":1,\"title\":\"d\",\"ttl\":" + std::to_string(t) + ",\"w\":[]}";
    pages += ",{\"sub\":2,\"title\":\"s2\",\"w\":[]},{\"sub\":1,\"title\":\"none\",\"w\":[]}]";
    CHECK_STR(commit_and_draw(ha, "{\"pages\":" + pages + ",\"v\":2}", "ttl"), "");
    const int want[] = {0, 0, 5, 5, 5, 5, 3600, 3600};
    if (CHECK(active() && active()->model->npages == 11)) {
      const D::Model &m = *active()->model;
      for (int i = 0; i < 8; i++) CHECK_MSG(m.pages[1 + i].ttl == want[i] && m.pages[1 + i].sub == 1, std::to_string(ttls[i]));
      CHECK(m.pages[9].sub == 0 && m.pages[10].sub == 1 && m.pages[10].ttl == 60);
    }
  }
  // A choice: 17 options and more keep 16; no list and empty lists load,
  // are never built and take no touch; no value is none selected.
  {
    std::string t17 = "[", i17 = "[", c17 = "[";
    for (int i = 0; i < 17; i++) {
      t17 += std::string(i ? "," : "") + "\"o" + std::to_string(i) + "\"";
      i17 += std::string(i ? "," : "") + "983568";
      c17 += std::string(i ? "," : "") + std::to_string(i * 4096);
    }
    t17 += "]";
    i17 += "]";
    c17 += "]";
    const std::string a40 = std::string(40, 'a'), a41 = std::string(41, 'a'), a255 = std::string(255, 'a');
    const std::string e_on_border = std::string(39, 'a') + "\xC3\xA9";  // two bytes over 39 and 40: cut to 39
    const std::string euro_on_border = std::string(38, 'a') + "\xE2\x82\xAC";  // three bytes over 38 to 40: cut to 38
    const std::string escaped = std::string(38, 'a') + "\\u00e9z";  // an escape that decodes to two bytes: 40
    const std::string bytes = v2_card(
        "{\"id\":2,\"texts\":" + t17 + ",\"icons\":" + i17 + ",\"cols\":" + c17 + "," + C + "},{\"id\":3," + C +
        "},{\"id\":4,\"texts\":[],\"icons\":[],\"cols\":[]," + C + "},{\"id\":5,\"texts\":[\"a\",\"b\"]," + C +
        "},{\"id\":6,\"texts\":" + texts1000 + "," + C + "},{\"id\":7,\"texts\":[\"" + a40 + "\",\"" + a41 + "\",\"" + a255 +
        "\",\"" + e_on_border + "\",\"" + euro_on_border + "\",\"" + escaped + "\",\"\"]," + C +
        "},{\"id\":8,\"icons\":[0,983568],\"texts\":[\"x\"]," + C + "},{\"id\":9,\"texts\":[\"first\"],\"texts\":[\"p\",\"q\",\"r\"]," + C +
        "},{\"id\":10,\"track\":[1,2,3],\"track\":[4,5]," + S + "},{\"id\":11,\"track\":[1]," + S +
        "},{\"id\":12,\"track\":[1,2,3,4,5,6,7,8,9]," + S + "},{\"id\":13,\"track\":[]," + S + "}");
    CHECK_STR(commit_and_draw(ha, bytes, "choices"), "");
    D::Widget *w2 = widget(2), *w3 = widget(3), *w4 = widget(4), *w5 = widget(5), *w6 = widget(6), *w7 = widget(7),
              *w8 = widget(8), *w9 = widget(9), *w10 = widget(10), *w11 = widget(11), *w12 = widget(12), *w13 = widget(13);
    if (CHECK(w2 && w3 && w4 && w5 && w6 && w7 && w8 && w9 && w10 && w11 && w12 && w13)) {
      CHECK(w2->options == 16 && w2->texts_n == 16 && w2->icons_n == 16 && w2->cols_n == 16);
      CHECK_STR(D::choice_text(*w2, 15), "o15");
      CHECK_STR(D::choice_text(*w2, 16), "");
      CHECK(w2->cols[15] == 15 * 4096);
      CHECK(w3->options == 0 && w4->options == 0 && D::Loader::objects_of(*w3) == 0 && D::Loader::objects_of(*w4) == 0);
      CHECK(w3->obj == nullptr && w4->obj == nullptr);
      CHECK(w5->options == 2 && w5->value.how == D::B_ABSENT && D::Loader::objects_of(*w5) == 1);
      CHECK(w6->options == 16 && w6->texts_n == 16);
      CHECK_STR(D::choice_text(*w6, 15), "t15");
      CHECK(w7->texts_n == 7);
      CHECK_STR(D::choice_text(*w7, 0), a40);
      CHECK_STR(D::choice_text(*w7, 1), a40);
      CHECK_STR(D::choice_text(*w7, 2), a40);
      CHECK_STR(D::choice_text(*w7, 3), std::string(39, 'a'));
      CHECK_STR(D::choice_text(*w7, 4), std::string(38, 'a'));
      CHECK_STR(D::choice_text(*w7, 5), std::string(38, 'a') + "\xC3\xA9");
      CHECK_STR(D::choice_text(*w7, 6), "");
      CHECK(w8->options == 2 && w8->icons[0] == 0 && w8->icons[1] == 983568);
      // A key given twice: the last one counts.
      CHECK(w9->texts_n == 3);
      CHECK_STR(D::choice_text(*w9, 0), "p");
      CHECK(w10->track_n == 2 && w10->track[0] == 4 && w10->track[1] == 5);
      CHECK(w11->track_n == 0 && !w11->track && w13->track_n == 0);
      CHECK(w12->track_n == 8 && w12->track[7] == 8);
    }
    // A press on a choice that has no option does nothing and sends nothing.
    fake::st.events.clear();
    if (w3) hh::tap(centre_x(w3->r), centre_y(w3->r));
    hh::advance(50);
    CHECK(fake::events("tab1_input").empty());
  }
  // Every new part on rectangles of 0 x 0, 1 x 1, off the screen and
  // shorter than two corner radii; values at and past their edges.
  {
    const char *rects[] = {"[116,140,0,0]", "[116,140,1,1]", "[1300,900,50,50]", "[-200,-200,100,100]", "[116,140,6,6]"};
    std::string ws;
    int id = 2;
    for (const char *r : rects) {
      const std::string rr = std::string("\"r\":") + r;
      auto add = [&](const std::string &rest) { ws += std::string(ws.empty() ? "" : ",") + "{\"id\":" + std::to_string(id++) + "," + rr + "," + rest + "}"; };
      add("\"t\":\"label\",\"text\":\"21:05\",\"px\":192,\"al\":1,\"c\":{\"s\":6}");
      add("\"t\":\"label\",\"text\":\"\",\"clk\":1,\"px\":96,\"value\":{\"s\":2}");
      add("\"t\":\"button\",\"text\":\"x\",\"tap\":{\"p\":0},\"shape\":1,\"isz\":1,\"icon\":983568,\"c\":{\"s\":6},\"value\":{\"s\":3}");
      add("\"t\":\"slider\",\"value\":{\"s\":2},\"min\":5,\"max\":5,\"step\":1,\"look\":1,\"dir\":1,\"track\":[1,2,3],\"rate\":{\"s\":7},\"change\":{\"p\":0}");
      add("\"t\":\"slider\",\"value\":{\"s\":2},\"min\":9,\"max\":1,\"step\":0,\"look\":0,\"dir\":2,\"track\":[1,2,3,4,5,6,7,8],\"nv\":1,\"icon\":983568");
      add("\"t\":\"thermostat\",\"value\":{\"s\":2},\"min\":35,\"max\":7,\"step\":0.5,\"mark\":{\"s\":6},\"bs\":64,\"c\":{\"s\":6},\"change\":{\"p\":0}");
      add("\"t\":\"chart\",\"value\":{\"s\":5},\"kind\":1,\"mark\":{\"s\":6},\"st\":1,\"c\":{\"s\":6}");
      add("\"t\":\"image\",\"value\":{\"s\":4},\"look\":1,\"c\":{\"s\":6}");
      add("\"t\":\"choice\",\"texts\":[\"a\",\"b\",\"c\"],\"icons\":[983568,0],\"cols\":[1,2,3,4],\"rows\":4,\"value\":{\"s\":6},\"change\":{\"p\":0}");
    }
    const std::string bytes = v2_card(ws, "\"tint\":{\"s\":6},\"un\":{\"s\":3},\"lit\":2,\"on\":{\"s\":3},\"tap\":{\"p\":0},");
    CHECK_STR(commit_and_draw(ha, bytes, "sizes", false), "");
    const std::string h = tab::active_hash();
    const char *values[] = {"[[2,1000000000],[6,1000000000],[7,1000000000]]", "[[2,-1000000000],[6,-1],[7,-1]]",
                            "[[2,0.5],[6,0.5],[7,0.5],[3,1]]", "[[2,null],[6,null],[7,null],[3,null]]",
                            "[[2,5],[6,16754519],[5,[1,2,3]],[4,983568],[3,0]]"};
    for (const char *v : values) {
      CHECK_STR(ha.set("{\"h\":\"" + h + "\",\"v\":" + v + "}"), "");
      hh::advance(30);
      draw_and_press_all("sizes");
    }
    // A colour slot holds a colour only while it is a whole number 0 to
    // 0xFFFFFF (contract 4.4).
    D::Widget *lab = widget(2);
    if (CHECK(lab && active())) {
      uint32_t col = 0;
      CHECK(D::bound_colour(*active()->model, lab->c, &col) && col == 16754519);
      for (const char *bad : {"1000000000", "-1", "0.5", "16777216", "null"}) {
        CHECK_STR(ha.set("{\"h\":\"" + h + "\",\"v\":[[6," + bad + "]]}"), "");
        CHECK_MSG(!D::bound_colour(*active()->model, lab->c, &col), bad);
      }
    }
  }
  // 64 pages that each carry a theme: 65 themes, each its own.
  {
    CHECK_STR(commit_and_draw(ha, "{\"nav\":[],\"pages\":" + pages64 + ",\"slots\":\"\",\"theme\":{\"acc\":1,\"tx\":7},\"v\":2}", "themes", false), "");
    if (CHECK(active() && active()->model->npages == 64)) {
      const D::Model &m = *active()->model;
      CHECK(m.nthemes == 65);
      for (int p = 0; p < 64; p++) {
        const D::Theme &t = m.themes[m.pages[p].theme];
        CHECK_MSG(m.pages[p].theme == p + 1 && t.c[D::C_ACC] == (uint32_t) p * 1000 && t.c[D::C_TX] == 7, std::to_string(p));
      }
      CHECK(m.themes[0].c[D::C_ACC] == 1);
    }
  }
}

// The per-page draw counts of a version 2 layout (contract 4.2): the 9th
// chart and the 9th thermostat of a page are skipped like a kind the engine
// does not know (their ids still count), the 13th track is dropped from its
// slider. A version 1 layout keeps everything.
TEST(page_cost_limits) {
  auto page = [](int first_id, int charts, int arcs, int tracks, bool bad_ninth) {
    std::string ws;
    int id = first_id + 1;
    auto add = [&](const std::string &rest) { ws += std::string(ws.empty() ? "" : ",") + "{\"id\":" + std::to_string(id++) + ",\"r\":[116,140,40,40]," + rest + "}"; };
    // A slider with one colour has no track and does not count.
    add("\"t\":\"slider\",\"value\":1,\"min\":0,\"max\":2,\"step\":1,\"track\":[255]");
    for (int i = 0; i < charts; i++) add(std::string("\"t\":\"chart\",\"value\":[1,2]") + (i == 8 && bad_ninth ? ",\"st\":\"skipped, so never read\"" : ""));
    for (int i = 0; i < arcs; i++) add("\"t\":\"thermostat\",\"value\":20,\"min\":7,\"max\":35,\"step\":1");
    for (int i = 0; i < tracks; i++) add("\"t\":\"slider\",\"value\":1,\"min\":0,\"max\":2,\"step\":1,\"track\":[255,65280]");
    return "{\"title\":\"P\",\"w\":[{\"id\":" + std::to_string(first_id) + ",\"r\":[100,84,568,640],\"t\":\"card\",\"w\":[" + ws + "]}]}";
  };
  // Page 0: 9, 9 and 13 (the first slider has no track). Page 1: 8, 8 and 12.
  Ha ha;
  for (int v : {2, 1}) {
    // Version 1 reads the 9th chart, so a wrong key there is parse.
    const std::string pages = "[" + page(100, 9, 9, 13, v == 2) + "," + page(200, 8, 8, 12, false) + "]";
    if (v == 1) {
      const std::string bad = "{\"pages\":[" + page(100, 9, 9, 13, true) + "],\"v\":1}";
      CHECK_STR(commit_answer(bad), "parse");
    }
    g_test = v == 2 ? "page_cost_limits v2" : "page_cost_limits v1";
    const std::string bytes = "{\"pages\":" + pages + ",\"v\":" + std::to_string(v) + "}";
    D::Loader ld;
    CHECK_STR(D::model_check(bytes.data(), bytes.size(), ld), "");
    CHECK(ld.c.ids == 2 + 2 + 9 + 9 + 13 + 8 + 8 + 12);
    CHECK(ld.c.drop_charts == (v == 2 ? 1u : 0u) && ld.c.drop_arcs == (v == 2 ? 1u : 0u) &&
          ld.c.drop_tracks == (v == 2 ? 1u : 0u));
    CHECK(ld.c.widgets == ld.c.ids - (v == 2 ? 2 : 0));
    fake::st.logs.clear();
    CHECK_STR(commit_and_draw(ha, bytes, g_test), "");
    int lines = 0;
    for (const std::string &s : fake::st.logs)
      if (s.find("over a page's limit") != std::string::npos) lines++;
    CHECK(lines == (v == 2 ? 1 : 0));
    // Page 0: ids 102..110 charts, 111..119 thermostats, 120..132 tracks.
    for (int i = 0; i < 9; i++) {
      CHECK_MSG((widget(102 + i) != nullptr) == (v == 1 || i < 8), "chart " + std::to_string(i));
      CHECK_MSG((widget(111 + i) != nullptr) == (v == 1 || i < 8), "thermostat " + std::to_string(i));
    }
    for (int i = 0; i < 13; i++) {
      const D::Widget *s = widget(120 + i);
      if (!CHECK(s)) continue;
      CHECK_MSG(s->track_n == (v == 1 || i < 12 ? 2 : 0), "track " + std::to_string(i));
    }
    CHECK(widget(101) && widget(101)->track_n == 0);
    // Page 1 holds exactly the limits: everything is kept.
    for (int i = 0; i < 8; i++) CHECK(widget(202 + i) && widget(210 + i));
    for (int i = 0; i < 12; i++) CHECK(widget(218 + i) && widget(218 + i)->track_n == 2);
  }
  g_test = "page_cost_limits";
}

// Layouts made from the kit by dropping, repeating and retyping keys, by
// numbers put in place of numbers and by empty and 1 px rectangles: none
// crashes, each answers "", parse, version or limits, and each that loads
// is drawn (cards spec B.9).
TEST(fuzz_keys) {
  const tj::Value kit = fixture("layout_v2_kit.dl.json");
  uint32_t seed = 0x2545F491u;
  auto rnd = [&](uint32_t n) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return n ? seed % n : 0;
  };
  // Every object and array of the tree, to pick one from.
  std::function<void(tj::Value &, std::vector<tj::Value *> &)> nodes = [&](tj::Value &v, std::vector<tj::Value *> &out) {
    if (v.t == tj::Value::OBJ || v.t == tj::Value::ARR) out.push_back(&v);
    for (auto &kv : v.o) nodes(kv.second, out);
    for (auto &e : v.a) nodes(e, out);
  };
  auto number = [](const char *text) {
    tj::Value n;
    n.t = tj::Value::NUM;
    n.s = text;
    n.n = atof(text);
    return n;
  };
  auto retype = [&](tj::Value &v) {
    static const char *const NUMS[] = {"0", "-1", "0.5", "1000000000", "2", "17", "65", "16777216"};
    tj::Value n;
    switch (rnd(7)) {
      case 0: n = number(NUMS[rnd(8)]); break;
      case 1: n.t = tj::Value::STR; n.s = "x"; break;
      case 2: n.t = tj::Value::NUL; break;
      case 3: n.t = tj::Value::BOOL; n.b = true; break;
      case 4: n.t = tj::Value::ARR; n.a.push_back(number(NUMS[rnd(8)])); break;
      case 5: n.t = tj::Value::OBJ; n.o.push_back({"s", number(NUMS[rnd(8)])}); break;
      default: n.t = tj::Value::ARR; break;
    }
    v = n;
  };
  const int runs = QUICK ? 1000 : 10000;
  const int draw_every = QUICK ? 50 : 10;
  std::map<std::string, int> answers;
  Ha ha;
  int loaded = 0, drawn = 0;
  const auto started = std::chrono::steady_clock::now();
  for (int run = 0; run < runs; run++) {
    tj::Value t = kit;
    const int edits = 1 + (int) rnd(4);
    for (int e = 0; e < edits; e++) {
      std::vector<tj::Value *> all;
      nodes(t, all);
      tj::Value &n = *all[rnd((uint32_t) all.size())];
      if (n.t == tj::Value::OBJ && !n.o.empty()) {
        const size_t k = rnd((uint32_t) n.o.size());
        tj::Value &val = n.o[k].second;
        switch (rnd(5)) {
          case 0: n.o.erase(n.o.begin() + (long) k); break;              // drop
          case 1: n.o.push_back(n.o[k]); break;                          // repeat
          case 2: retype(val); break;                                    // retype
          case 3:
            if (val.t == tj::Value::NUM) {
              static const char *const NUMS[] = {"0", "-1", "0.5", "1000000000"};
              val = number(NUMS[rnd(4)]);
            } else {
              retype(val);
            }
            break;
          default:
            if (n.o[k].first == "r" || n.o[k].first == "hr") {  // an empty or 1 px rectangle
              const bool one = rnd(2) != 0;
              val.a[2] = number(one ? "1" : "0");
              val.a[3] = number(one ? "1" : "0");
            } else {
              retype(val);
            }
        }
      } else if (n.t == tj::Value::ARR && !n.a.empty()) {
        const size_t k = rnd((uint32_t) n.a.size());
        switch (rnd(3)) {
          case 0: n.a.erase(n.a.begin() + (long) k); break;
          case 1: n.a.push_back(n.a[k]); break;
          default: retype(n.a[k]);
        }
      }
    }
    const std::string bytes = json_text(t);
    const std::string got = commit_answer(bytes);
    answers[got]++;
    if (!CHECK_MSG(got == "" || got == "parse" || got == "version" || got == "limits", got)) break;
    if (got != "") continue;
    loaded++;
    if (loaded % draw_every != 1) continue;
    drawn++;
    const std::string shown = commit_and_draw(ha, bytes, "fuzz", drawn % 4 == 1);
    if (!CHECK_MSG(shown.empty(), "run " + std::to_string(run) + ": " + shown)) break;
  }
  const long long ms = (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
  printf("  %d layouts in %lld ms: %d load (%d drawn), %d parse, %d version, %d limits\n", runs, ms, answers[""], drawn,
         answers["parse"], answers["version"], answers["limits"]);
  CHECK(answers[""] > runs / 20 && answers["parse"] > runs / 20);
}

// The key tables find a key with about one comparison, however many names
// they hold: the item table knows 44 (cards spec B.2).
TEST(keys_found_at_once) {
  const D::KeyIndex *tables[] = {&D::ITEM_INDEX, &D::THEME_INDEX, &D::STYLE_INDEX, &D::PAGE_INDEX,
                                 &D::TOP_INDEX,  &D::TAP_INDEX,   &D::NAV_INDEX};
  for (const D::KeyIndex *t : tables) {
    for (int i = 0; i < t->count; i++) {
      tab::json::Span k{t->names[i], strlen(t->names[i]), false};
      CHECK_MSG(t->find(k) == i, t->names[i]);
      tab::json::Span esc{t->names[i], strlen(t->names[i]), true};
      CHECK(t->find(esc) == -1);
    }
    for (const char *other : {"", "x", "zz", "texts2", "Id", "valu", "values"}) {
      tab::json::Span k{other, strlen(other), false};
      CHECK_MSG(t->find(k) == -1, other);
    }
  }
  CHECK(D::ITEM_INDEX.count == 44);
  const std::string bytes = layout_bytes("layout_v2_kit.dl.json");
  D::test_counts() = D::TestCounts{};
  D::Loader ld;
  CHECK_STR(D::model_check(bytes.data(), bytes.size(), ld), "");
  const D::TestCounts c = D::test_counts();
  printf("  kit: %llu keys, %llu names compared\n", (unsigned long long) c.keys, (unsigned long long) c.key_compares);
  CHECK(c.keys > 400);
  CHECK_MSG(c.key_compares * 4 <= c.keys * 5, std::to_string(c.key_compares) + " compares");  // at most 1.25 a key
}

// No walk of the heap while a layout is being built (cards spec B.6): the
// largest free block is read once per commit, in stage(); build steps read
// free PSRAM, and tab_status and tab_hello report the block read at the
// commit until the build ends (contract 3.1). The rest of this test comes
// with package E9.
TEST(offscreen_costs_nothing) {
  Ha ha;
  ha.boot(false);
  CHECK(ha.hello());
  const tj::Value f = fixture("commit_v2.json");
  CHECK(ha.show(f["cases"][0]["calls"][0]["data"].s));  // 64 pages
  const std::string kit = layout_bytes("layout_v2_kit.dl.json");
  CHECK_STR(ha.send(kit), "");
  fake::st.largest = 15000000;
  fake::st.largest_calls = 0;
  CHECK_STR(ha.commit("ef98088e1fda910d"), "");
  CHECK(fake::st.largest_calls == 1 && tab::building());
  fake::st.largest = 9000000;
  char out[768];
  tj::Value v;
  int steps = 0;
  while (tab::building() && steps < 3000) {
    CHECK(tj::parse(out, tab::on_status(out, sizeof out), &v) && v["mem"]["block"].n == 15000000);
    const size_t n = tab::on_hello(NONCE, 32, "", 0, out, sizeof out);
    CHECK(n > 0 && tj::parse(out, n, &v) && v["mem"]["block"].n == 15000000);
    hh::advance(1);
    steps++;
  }
  CHECK(steps > 3);  // the build took several steps
  CHECK_MSG(fake::st.largest_calls == 1, std::to_string(fake::st.largest_calls) + " calls");
  CHECK(advance_until([] { return std::string(tab::active_hash()) == "ef98088e1fda910d"; }, 5000));
  CHECK(tj::parse(out, tab::on_status(out, sizeof out), &v) && v["mem"]["block"].n == 9000000);
  CHECK(fake::st.largest_calls == 2);
  // A build that runs short of free PSRAM stops with memory, whatever the
  // largest block was at the commit.
  CHECK_STR(ha.send(layout_bytes("layout_all_cards.dl.json")), "");
  fake::st.largest = 16000000;
  CHECK_STR(ha.commit("4384f665e4dd9f62"), "");
  fake::st.free = 1000000;
  hh::advance(50);
  fake::st.free = 28000000;
  CHECK_STR(status_build(), "memory");
  CHECK_STR(tab::active_hash(), "ef98088e1fda910d");
}

// ================================================================ colours and repaints (cards spec B.3, B.4)

static uint32_t colour_of(const tj::Value &v) { return (uint32_t) v.n; }

// A theme and a card style of maths.json (device form) as the engine holds them.
static D::Theme maths_theme(const tj::Value &t) {
  D::Theme th{};
  for (int c = 0; c < D::C_COUNT; c++) th.c[c] = colour_of(t[D::THEME_KEYS[c]]);
  th.dark = (uint8_t) t["dark"].n;
  th.r = (int16_t) t["r"].n;
  th.r2 = (int16_t) t["r2"].n;
  th.bw = (int16_t) t["bw"].n;
  return th;
}

static D::CardStyle maths_style(const tj::Value &s) {
  D::CardStyle st{};
  st.gd = -1;
  for (int i = 0; i < D::S_COUNT; i++)
    if (s.has(D::STYLE_KEYS[i])) {
      st.c[i] = colour_of(s[D::STYLE_KEYS[i]]);
      st.mask |= 1u << i;
    }
  return st;
}

static std::string hex6(uint32_t c) {
  char b[16];
  snprintf(b, sizeof b, "%06x", (unsigned) c);
  return b;
}

// The colour rules of contract 4.7 against every vector of maths.json that
// the colours cover: the table, mix (and every exact half), lum, contrast,
// readable, legible, blend at 0.82 (also the pairs a fused multiply-add gets
// wrong), the card colours for every lit with and without a tint, with the
// tokens of each style, and the two colours of an image in each state. The
// geometry of maths.json (label sizes, the clock, sliders, tracks, choice
// cells, chart steps, bars, the image's box) comes with the packages that
// draw those parts.
TEST(maths_fixture) {
  const tj::Value f = fixture("maths.json");
  // The table, bit for bit.
  CHECK(f["linear"].size() == 256);
  int bad = 0;
  for (int i = 0; i < 256; i++) {
    const double want = f["linear"][i].n;
    if (memcmp(&want, &D::LINEAR[i], sizeof want) != 0) bad++;
  }
  CHECK_MSG(bad == 0, std::to_string(bad) + " entries of LINEAR differ from maths.json");

  bad = 0;
  std::string first;
  auto miss = [&](const std::string &what) {
    if (!bad++) first = what;
  };
  CHECK(f["mix"].size() > 1000);
  for (const tj::Value &v : f["mix"].a)
    if (D::mix(colour_of(v[0]), colour_of(v[1]), (uint32_t) v[2].n) != colour_of(v[3]))
      miss("mix " + v[0].s + " " + v[1].s + " " + v[2].s);
  int halves = 0;
  for (const auto &kv : f["mix_halves"].o) {
    const uint32_t p = (uint32_t) atoi(kv.first.c_str());
    for (const tj::Value &v : kv.second.a)
      for (int shift = 0; shift <= 16; shift += 8) {
        halves++;
        if (D::mix(colour_of(v[0]) << shift, colour_of(v[1]) << shift, p) != colour_of(v[2]) << shift)
          miss("mix half " + kv.first + " " + v[0].s + " " + v[1].s);
      }
  }
  CHECK(halves > 300);
  for (const tj::Value &v : f["lum"].a)
    if (D::lum(colour_of(v[0])) != v[1].n) miss("lum " + v[0].s);
  for (const tj::Value &v : f["contrast"].a)
    if (D::contrast(colour_of(v[0]), colour_of(v[1])) != v[2].n) miss("contrast " + v[0].s + " " + v[1].s);
  for (const tj::Value &v : f["readable"].a)
    if (D::readable(colour_of(v[0])) != colour_of(v[1])) miss("readable " + v[0].s);
  for (const tj::Value &v : f["legible"].a)
    if (D::legible(colour_of(v[0]), colour_of(v[1]), colour_of(v[2])) != colour_of(v[3]))
      miss("legible " + v[0].s + " " + v[1].s + " " + v[2].s);
  const double t = f["blend"]["t"].n;
  CHECK(t == 0.82);
  for (const tj::Value &v : f["blend"]["cases"].a)
    if (D::blend(colour_of(v[0]), colour_of(v[1]), t) != colour_of(v[2])) miss("blend " + v[0].s + " " + v[1].s);
  for (const char *part : {"fused", "halves"}) {
    CHECK(f["blend"][part].size() > 10);
    for (const tj::Value &v : f["blend"][part].a)
      for (int shift = 0; shift <= 16; shift += 8)
        if (D::blend(colour_of(v[0]) << shift, colour_of(v[1]) << shift, t) != colour_of(v[2]) << shift)
          miss(std::string("blend ") + part + " " + v[0].s + " " + v[1].s);
  }
  CHECK_MSG(bad == 0, std::to_string(bad) + " colour vectors differ, the first: " + first);

  // The card colours, with the tokens of the page theme and the style.
  bad = 0;
  CHECK(f["card"].size() >= 200);
  for (const tj::Value &v : f["card"].a) {
    const D::Theme th = maths_theme(f["themes"][v["theme"].s.c_str()]);
    const D::CardStyle st = maths_style(f["styles"][v["style"].s.c_str()]);
    const D::Tok tok = D::style_tokens(th, st.mask ? &st : nullptr);
    const bool has_tint = !v["tint"].is_null();
    const D::CardColours k = D::card_colours(tok, v["on"].n == 1, v["lit"].i(), has_tint, has_tint ? colour_of(v["tint"]) : 0);
    const tj::Value &e = v["expect"];
    const std::string what = v["theme"].s + "/" + v["style"].s + " on " + v["on"].s + " lit " + v["lit"].s + " tint " +
                             (has_tint ? v["tint"].s : "none");
    const struct {
      const char *key;
      uint32_t got;
    } fields[] = {{"A", k.A},
                  {"AF", k.AF},
                  {"AT", k.AT},
                  {"AP", k.AP},
                  {"F", k.F},
                  {"FT", k.FT},
                  {"FS", k.FS},
                  {"FI", k.FI},
                  {"FP", k.FP},
                  {"background", k.background},
                  {"text", k.text},
                  {"border", k.border},
                  {"header_icon", k.header_icon},
                  {"border_width", (uint32_t) k.border_width},
                  {"dark", k.dark},
                  {"filled", k.filled}};
    for (const auto &fd : fields)
      if (fd.got != colour_of(e[fd.key])) miss(what + ": " + fd.key + " " + hex6(fd.got) + ", not " + hex6(colour_of(e[fd.key])));
    if (tok.bg != colour_of(v["tokens"]["bg"]) || tok.ion != colour_of(v["tokens"]["ion"]) ||
        tok.onbg_pr != colour_of(v["tokens"]["onbg_pr"]))
      miss(what + ": tokens");
  }
  CHECK_MSG(bad == 0, std::to_string(bad) + " card colours differ, the first: " + first);
  // FS on a tinted card is tx when tx2 does not stand out on F (contract
  // 4.7). No vector of maths.json reaches that branch, so two by hand: a
  // dark card whose tx2 is close to the fill, and one whose tx2 stands out.
  {
    D::Tok tok = D::theme_tokens(maths_theme(f["themes"]["midnight"]));
    tok.tx2 = 0x404040;
    const D::CardColours dim = D::card_colours(tok, true, 0, true, 0x303030);
    CHECK(D::contrast(tok.tx2, dim.F) < 3 && dim.FS == tok.tx);
    tok.tx2 = 0xC0C0C0;
    const D::CardColours bright = D::card_colours(tok, true, 0, true, 0x303030);
    CHECK(D::contrast(tok.tx2, bright.F) >= 3 && bright.FS == tok.tx2);
  }

  // An image's box (or circle) and icon colour.
  bad = 0;
  int images = 0;
  for (const tj::Value &v : f["image"].a) {
    const tj::Value &e = v["expect"];
    if (e.is_null()) continue;  // a rectangle of no size: nothing is drawn
    images++;
    const D::Tok tok = D::theme_tokens(maths_theme(f["themes"][v["theme"].s.c_str()]));
    const bool has_tint = !v["tint"].is_null(), has_c = !v["c"].is_null();
    const D::CardColours k = D::card_colours(tok, v["on"].n == 1, v["lit"].i(), has_tint, has_tint ? colour_of(v["tint"]) : 0);
    const D::ImageColours ic = D::image_colours(tok, k, v["look"].i(), has_c, has_c ? colour_of(v["c"]) : 0);
    const std::string what = v["theme"].s + " " + v["state"].s + " look " + v["look"].s + " c " + (has_c ? v["c"].s : "none");
    if (ic.icon != colour_of(e["icon"])) miss(what + ": icon " + hex6(ic.icon));
    if (e["fill"].is_null() ? ic.boxed != 0 : (ic.boxed != 1 || ic.fill != colour_of(e["fill"]))) miss(what + ": fill");
    if (e.has("plain") && ic.plain != (uint8_t) e["plain"].n) miss(what + ": plain");
  }
  CHECK(images >= 480);
  CHECK_MSG(bad == 0, std::to_string(bad) + " image colours differ, the first: " + first);

  // (package E5) An image's box and icon size by look; a rectangle of no
  // size draws nothing.
  bad = 0;
  int boxes = 0, empty = 0;
  for (const tj::Value &v : f["image"].a) {
    D::Widget w{};
    w.kind = D::W_IMAGE;
    w.look = (uint8_t) v["look"].i();
    w.r[2] = (int16_t) v["w"].i();
    w.r[3] = (int16_t) v["h"].i();
    int x, y, bw, bh;
    D::image_box(w, &x, &y, &bw, &bh);
    const std::string what = "look " + v["look"].s + " " + v["w"].s + "x" + v["h"].s;
    const tj::Value &e = v["expect"];
    if (e.is_null()) {
      empty++;
      if (bw > 0 && bh > 0) miss(what + ": drawn");
      continue;
    }
    boxes++;
    if (D::image_icon_size(w.look, w.r[2], w.r[3]) != e["icon_px"].i()) miss(what + ": icon size");
    if (e["box"].is_null()) {
      if (w.look != 2) miss(what + ": a box");
      continue;
    }
    if (w.look == 2 || x != e["box"][0].i() || y != e["box"][1].i() || bw != e["box"][2].i() || bh != e["box"][3].i())
      miss(what + ": box " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(bw) + "," +
           std::to_string(bh));
  }
  CHECK(boxes >= 480 && empty >= 100);
  CHECK_MSG(bad == 0, std::to_string(bad) + " image boxes differ, the first: " + first);

  // (package E5) The label size rule before the width is checked, the small
  // colour and the sizes tried in turn while the text is too wide.
  bad = 0;
  CHECK(f["label"].size() >= 25);
  for (const tj::Value &v : f["label"].a) {
    const int px = v["px"].is_null() ? 0 : v["px"].i();
    bool small = false;
    const int got = D::label_size(v["size"].i(), px, v["text"].s.c_str(), &small);
    const tj::Value &e = v["expect"];
    const std::string what = "px " + (v["px"].is_null() ? std::string("none") : v["px"].s) + " size " + v["size"].s +
                             " \"" + v["text"].s + "\"";
    if (got != e["px"].i()) miss(what + ": " + std::to_string(got));
    if (small != (e["small"].n == 1)) miss(what + ": small");
    std::vector<int> chain;
    for (int s = D::label_smaller(got); s; s = D::label_smaller(s)) chain.push_back(s);
    bool same = chain.size() == e["smaller"].size();
    for (size_t i = 0; same && i < chain.size(); i++) same = chain[i] == e["smaller"][i].i();
    if (!same) miss(what + ": smaller");
  }
  CHECK_MSG(bad == 0, std::to_string(bad) + " label sizes differ, the first: " + first);

  // (package E6) The slider: the amount and the value under a finger, and
  // where a value is drawn, for both looks, the three directions, tracks of
  // 0, 2, 7 and 8 colours, and rectangles of no size, 1 px and thin ones.
  bad = 0;
  int presses = 0, sends = 0, knobs = 0, bars = 0, tracks = 0;
  CHECK(f["slider"].size() >= 300);
  for (const tj::Value &v : f["slider"].a) {
    const tj::Value &r = v["rect"];
    const D::SliderGeom g = D::slider_geom(r[2].i(), r[3].i(), (uint8_t) v["look"].i(), (uint8_t) v["dir"].i(),
                                           (uint8_t) v["track"].i(), v["r2"].i());
    const double lo = v["min"].n, hi = v["max"].n, step = v["step"].n;
    const std::string what = "look " + v["look"].s + " dir " + v["dir"].s + " track " + v["track"].s + " " +
                             v["rect_name"].s + " " + v["min"].s + ".." + v["max"].s;
    for (const tj::Value &p : v["presses"].a) {
      presses++;
      const std::string at = what + " at " + p[0].s + "," + p[1].s;
      const double amount = D::slider_amount_at(g, D::slider_along(g, r[0].i(), r[1].i(), p[0].n, p[1].n));
      if (amount != p[2].n) miss(at + ": amount " + std::to_string(amount));
      const bool sent = D::slider_sends(g, lo, hi);
      if (sent) sends++;
      if (sent != !p[3].is_null()) miss(at + (sent ? ": sends" : ": sends nothing"));
      else if (sent && D::clamp_step(lo + amount * (hi - lo), lo, hi, step) != p[3].n) miss(at + ": value");
    }
    for (const tj::Value &sh : v["shapes"].a) {
      const tj::Value &e = sh["expect"];
      const bool has = !sh["value"].is_null();
      const std::string at = what + " value " + (has ? sh["value"].s : "none") + " change " + sh["change"].s;
      if (g.L != e["L"].i() || g.S != e["S"].i() || (e.has("R") && g.R != e["R"].i())) miss(at + ": L S R");
      const int pos = D::slider_shown(g, has, sh["value"].n, lo, hi);
      if (e.has("knob")) {
        knobs++;
        if (e["knob"].is_null() ? pos != -1 : pos != e["knob"].i()) miss(at + ": knob " + std::to_string(pos));
      } else if (e.has("fill")) {
        bars++;
        const int fill = pos > 0 ? pos : 0;
        const bool handle = sh["change"].n == 1 && D::bar_handle(fill);
        if (fill != e["fill"].i() || D::bar_drawn(g, fill) != e["drawn"].i()) miss(at + ": fill " + std::to_string(fill));
        if (e["handle"].is_null() ? handle : (!handle || fill - 12 != e["handle"][0].i() || fill - 8 != e["handle"][1].i()))
          miss(at + ": handle");
        if (D::bar_icon_room(g) != !e["icon_centre"].is_null() || (D::bar_icon_room(g) && e["icon_centre"].i() != 24))
          miss(at + ": icon");
        if (D::bar_icon_on_fill(g, fill) != (e["icon_on_fill"].n == 1)) miss(at + ": icon on the fill");
      } else {
        tracks++;
        int a1, b0;
        D::track_caps(g, &a1, &b0);
        const tj::Value &caps = e["caps"];
        if (caps[0][0].i() != 0 || caps[0][1].i() != a1 || caps[1][0].i() != b0 || caps[1][1].i() != g.L)
          miss(at + ": caps " + std::to_string(a1) + " " + std::to_string(b0));
        const size_t n = g.x1 > g.x0 ? (size_t) g.colours - 1 : 0;
        bool same = e["pieces"].size() == n;
        for (size_t k = 0; same && k < n; k++) {
          int from, to;
          D::track_piece(g.colours, g.x0, g.x1, (int) k, &from, &to);
          same = from == e["pieces"][k][0].i() && to == e["pieces"][k][1].i();
        }
        if (!same) miss(at + ": pieces");
        if (e["mark"].is_null() ? pos != -1 : pos != e["mark"].i()) miss(at + ": mark " + std::to_string(pos));
      }
    }
  }
  CHECK(presses >= 2000 && sends >= 1000 && knobs >= 200 && bars >= 400 && tracks >= 2000);
  CHECK_MSG(bad == 0, std::to_string(bad) + " slider vectors differ, the first: " + first);

  // (package E6) The pieces of a track, n - 1 of them from x0 to x1, none
  // when x1 is not above x0.
  bad = 0;
  CHECK(f["track"].size() >= 30);
  for (const tj::Value &v : f["track"].a) {
    const int n = v["n"].i(), x0 = v["x0"].i(), x1 = v["x1"].i();
    const size_t count = n >= 2 && x1 > x0 ? (size_t) n - 1 : 0;
    bool same = v["expect"].size() == count;
    for (size_t k = 0; same && k < count; k++) {
      int from, to;
      D::track_piece(n, x0, x1, (int) k, &from, &to);
      same = from == v["expect"][k][0].i() && to == v["expect"][k][1].i();
    }
    if (!same) miss("track " + v["n"].s + " " + v["x0"].s + " " + v["x1"].s);
  }
  CHECK_MSG(bad == 0, std::to_string(bad) + " track pieces differ, the first: " + first);
  // The engine's list of digit glyphs is contract.json's.
  CHECK_STR(D::DIGITS_GLYPHS, tj::load(CDIR + "/contract.json")["fonts"]["digits_glyphs"].s);
}

// The card's look as its state gives it now, worked out apart from the record.
static D::CardColours look_now(D::Model &m, const D::Widget &card) {
  uint32_t tint = 0;
  const bool has_tint = D::bound_colour(m, card.c, &tint);
  return D::card_colours(D::card_tokens(m, &card), D::card_is_on(m, &card), card.lit, has_tint, tint);
}

static bool same_look(const D::CardColours &a, const D::CardColours &b) {
  return a.A == b.A && a.AF == b.AF && a.AT == b.AT && a.AP == b.AP && a.F == b.F && a.FT == b.FT && a.FS == b.FS &&
         a.FI == b.FI && a.FP == b.FP && a.background == b.background && a.text == b.text && a.border == b.border &&
         a.header_icon == b.header_icon && a.border_width == b.border_width && a.dark == b.dark && a.filled == b.filled &&
         a.lit_on == b.lit_on && a.sel_border == b.sel_border;
}

// Every card's record and every widget's own colours hold what the state
// gives now.
static void looks_hold(D::Model &m, const std::string &when) {
  int cards = 0, kept = 0;
  for (uint32_t i = 0; i < m.nw; i++) {
    D::Widget &w = m.w[i];
    if (!w.obj) continue;
    if (w.kind == D::W_CARD) {
      cards++;
      const D::CardLook *k = D::card_look(m, &w);
      if (!CHECK_MSG(k, when + ": card " + std::to_string(w.id) + " has no record")) continue;
      CHECK_MSG(same_look(k->k, look_now(m, w)), when + ": card " + std::to_string(w.id));
      const D::Tok t = D::card_tokens(m, &w);
      CHECK_MSG(k->t.bg == t.bg && k->t.tx == t.tx && k->t.acc == t.acc && k->t.ion == t.ion && k->t.onbg_pr == t.onbg_pr &&
                    k->t.card2 == t.card2 && k->t.r2 == t.r2 && k->t.bw == t.bw,
                when + ": tokens of card " + std::to_string(w.id));
      continue;
    }
    D::Widget copy = w;
    D::widget_colour_update(m, copy);
    if (w.c_has) kept++;
    CHECK_MSG(copy.c_has == w.c_has && copy.c_now == w.c_now && copy.c_on == w.c_on,
              when + ": widget " + std::to_string(w.id) + " keeps " + hex6(w.c_now) + "/" + hex6(w.c_on) + ", the state gives " +
                  hex6(copy.c_now) + "/" + hex6(copy.c_on));
  }
  CHECK(cards > 5 && kept > 5);
}

// A card's nine colours are worked out at the build and again only when its
// `on`, `tint` or `un` changes; a widget's own colours at the build, when its
// `c` changes and after its card's. A repaint with unchanged state works out
// no contrast, also for widgets that carry `c`, the badges and a bar with a
// handle (cards spec B.3). The optimistic value of a card comes with package
// E4, and with it the record's update for it.
TEST(card_look_cache) {
  Ha ha;
  ha.boot(false);
  CHECK(ha.hello());
  const std::string kit = layout_bytes("layout_v2_kit.dl.json");
  const std::string hash = sha_hex(kit).substr(0, 16);
  CHECK(ha.show(kit));
  D::Layout *l = active();
  if (!CHECK(l && l->model)) return;
  D::Model &m = *l->model;
  int cards = 0;
  for (uint32_t i = 0; i < m.nw; i++) cards += m.w[i].kind == D::W_CARD;
  CHECK(m.nlooks == cards && cards == 15);
  looks_hold(m, "built without values");
  // The tokens are the page's theme (page 1 has its own) with the style on
  // top (card 7's style names none of these).
  const tj::Value dl = fixture("layout_v2_kit.dl.json");
  int themed = 0;
  for (uint32_t i = 0; i < m.nw; i++) {
    const D::Widget &w = m.w[i];
    if (w.kind != D::W_CARD) continue;
    const tj::Value &pt = dl["pages"][(size_t) w.page]["theme"];
    auto role = [&](const char *key) { return colour_of(pt.has(key) ? pt[key] : dl["theme"][key]); };
    const D::Tok &t = D::card_look(m, &w)->t;
    CHECK_MSG(t.bg == role("card") && t.tx == role("tx") && t.acc == role("acc") && t.card2 == role("card2") &&
                  t.line == role("line"),
              "tokens of card " + std::to_string(w.id));
    themed += pt.has("card") ? 1 : 0;
  }
  CHECK(themed == 7);
  auto set = [&](const std::string &v) { return ha.set("{\"h\":\"" + hash + "\",\"v\":[" + v + "]}"); };
  auto counts = [] { return D::test_counts(); };
  auto reset = [] {
    D::test_counts() = D::TestCounts{};
    D::test_contrasts() = 0;
  };

  // The values of state_v2.json's first step: on, tint, un and the widgets' c.
  CHECK_STR(ha.set(fixture("state_v2.json")["steps"][0]["payloads"][0].s), "");
  hh::advance(50);
  looks_hold(m, "after the first snapshot");
  D::Widget *c2 = widget(2), *c7 = widget(7), *c55 = widget(55);
  if (!CHECK(c2 && c7 && c55)) return;
  CHECK(D::card_look(m, c2)->k.filled == 1 && D::card_look(m, c2)->has_tint == 1 && D::card_look(m, c2)->tint == 16754519);
  CHECK(D::card_look(m, c7)->k.filled == 0 && D::card_look(m, c7)->k.lit_on == 1 && D::card_look(m, c7)->unavailable == 0);

  // A repaint with unchanged state, of every widget on every page, and the
  // frames of every page (the chart and thermostat draw callbacks).
  reset();
  for (uint32_t i = 0; i < m.nw; i++) D::paint_widget(*l, m.w[i]);
  for (int p = 0; p < tab::page_count(); p++) {
    tab::go_to(p);
    hh::advance(20);
    lv_obj_invalidate(lv_screen_active());
    hh::refresh();
  }
  tab::go_to(0);
  hh::advance(20);
  CHECK_MSG(D::test_contrasts() == 0, std::to_string(D::test_contrasts()) + " contrasts");
  CHECK(counts().look_updates == 0 && counts().colour_updates == 0);

  // A text: no colour work.
  reset();
  CHECK_STR(set("[5,\"Off\"]"), "");
  CHECK(D::test_contrasts() == 0 && counts().look_updates == 0 && counts().colour_updates == 0);

  // `on` of three cards (2 and 7 on page 0, 55 on page 2): their records and
  // their widgets', nothing else.
  reset();
  CHECK_STR(set("[2,0]"), "");
  CHECK_MSG(counts().look_updates == 3, std::to_string(counts().look_updates));
  CHECK_MSG(counts().colour_updates == c2->children + c7->children + c55->children, std::to_string(counts().colour_updates));
  CHECK(D::test_contrasts() > 0);
  CHECK(D::card_look(m, c2)->k.filled == 0 && D::card_look(m, c2)->k.background == D::card_look(m, c2)->t.bg);
  looks_hold(m, "on 0");

  // `tint` (slot 4 is also the c of the bars 6 and 11 in cards 2 and 7).
  reset();
  CHECK_STR(set("[4,255]"), "");
  CHECK(counts().look_updates == 2 && D::card_look(m, c2)->tint == 255 && D::card_look(m, c7)->tint == 255);
  looks_hold(m, "tint 0x0000ff");
  // A colour slot with a value that is no colour: no tint.
  for (const char *odd : {"1e9", "-1", "0.5", "16777216"}) {
    CHECK_STR(set(std::string("[4,") + odd + "]"), "");
    CHECK_MSG(D::card_look(m, c2)->has_tint == 0, odd);
    looks_hold(m, std::string("tint ") + odd);
  }
  // Another value that is no colour changes no colour: the widgets of the
  // two cards are not worked out again, but for the two bars whose own `c`
  // is that slot.
  reset();
  CHECK_STR(set("[4,-2]"), "");
  CHECK_MSG(counts().look_updates == 2 && counts().colour_updates == 2 && D::card_look(m, c2)->has_tint == 0,
            std::to_string(counts().look_updates) + " " + std::to_string(counts().colour_updates));
  CHECK_STR(set("[4,16711680]"), "");
  CHECK(D::card_look(m, c2)->has_tint == 1);

  // `un` of card 7.
  reset();
  CHECK_STR(set("[8,1]"), "");
  CHECK(counts().look_updates == 1 && D::card_look(m, c7)->unavailable == 1);
  looks_hold(m, "un 1");

  // A widget's own c (label 50 on page 1, not on screen): its colours only.
  reset();
  D::Widget *lab = widget(50);
  if (!CHECK(lab)) return;
  CHECK_STR(set("[33,1382429]"), "");
  CHECK(counts().look_updates == 0 && counts().colour_updates == 1);
  const D::CardLook *k46 = D::card_look(m, D::card_of(m, *lab));
  CHECK(lab->c_has && lab->c_now == D::legible(1382429, k46->k.background, k46->k.text));
  looks_hold(m, "c of label 50");

  // The same values again: nothing.
  reset();
  CHECK_STR(set("[2,0],[4,16711680],[8,1],[33,1382429]"), "");
  CHECK(counts().look_updates == 0 && counts().colour_updates == 0 && D::test_contrasts() == 0);

  // A repaint after all of it still works nothing out.
  hh::advance(50);
  reset();
  for (uint32_t i = 0; i < m.nw; i++) D::paint_widget(*l, m.w[i]);
  CHECK(D::test_contrasts() == 0 && counts().look_updates == 0);
}

// LVGL lays the screen out again before the next frame (layout_update_core).
static bool layout_pending() {
  lv_obj_t *s = lv_screen_active();
  return s && s->scr_layout_inv;
}

// The four repaint rules of cards spec B.4 for the parts of version 1: no
// style call for a repaint with unchanged state; a tab_set paints only the
// page on screen and marks the rest stale, which the timer's slices paint
// (not in a tick that ran a build step), and the page's own before it
// shows; a changed thermostat value, a changed `on` and a new text lay
// nothing out.
TEST(repaint_cost) {
  Ha ha;
  ha.boot(false);
  CHECK(ha.hello());
  const std::string all = layout_bytes("layout_all_cards.dl.json");
  CHECK(ha.show(all));
  CHECK_STR(ha.set(state_payload(0)), "");
  hh::advance(100);
  D::Layout *l = active();
  if (!CHECK(l && l->model)) return;
  D::Model &m = *l->model;
  CHECK(tab::current_page() == 0 && l->stale == 0);
  auto set = [&](const std::string &v) { return ha.set("{\"h\":\"4384f665e4dd9f62\",\"v\":[" + v + "]}"); };
  auto reset = [] {
    D::test_counts() = D::TestCounts{};
    D::test_contrasts() = 0;
  };

  // Rules 2 and 3: a repaint with unchanged state sets nothing.
  lv_obj_invalidate(lv_screen_active());
  hh::refresh();
  CHECK(!layout_pending());
  reset();
  int painted = 0;
  for (uint32_t i = 0; i < m.nw; i++)
    if (m.w[i].obj) {
      D::paint_widget(*l, m.w[i]);
      painted++;
    }
  CHECK(painted > 60);
  CHECK_MSG(D::test_counts().style_sets == 0, std::to_string(D::test_counts().style_sets) + " style calls");
  CHECK(!layout_pending() && D::test_contrasts() == 0);

  // Rule 1: a label and a thermostat of page 1 while page 0 is on screen.
  D::Widget *lab = widget(32), *th = widget(49), *lab0 = widget(4);
  if (!CHECK(lab && th && lab0 && lab->page == 1 && th->page == 1 && lab0->page == 0)) return;
  const std::string before = label_text(lab->obj), th_before = label_text(th->o1);
  reset();
  CHECK_STR(set("[26,\"Changed\"],[36,22.5]"), "");
  CHECK_MSG(D::test_counts().style_sets == 0, std::to_string(D::test_counts().style_sets) + " style calls");
  CHECK(lab->stale && th->stale && l->stale == 2 && m.pages[1].stale == 2 && m.pages[0].stale == 0);
  CHECK(label_text(lab->obj) == before && label_text(th->o1) == th_before);
  // The slices paint them.
  hh::advance(20);
  CHECK(!lab->stale && !th->stale && l->stale == 0 && m.pages[1].stale == 0);
  CHECK_STR(label_text(lab->obj), "Changed");
  CHECK_STR(label_text(th->o1), "22.5");
  CHECK(D::test_counts().style_sets > 0);
  // A page shows only once what is stale on it is painted, slices or not.
  CHECK_STR(set("[26,\"Again\"]"), "");
  CHECK(lab->stale);
  tab::go_to(1);
  CHECK(!lab->stale && l->stale == 0);
  CHECK_STR(label_text(lab->obj), "Again");
  // The page on screen is painted at once, and page 0 now waits.
  CHECK_STR(set("[26,\"Now\"],[3,\"Stale\"]"), "");
  CHECK_STR(label_text(lab->obj), "Now");
  CHECK(!lab->stale && lab0->stale);
  hh::advance(20);
  CHECK(!lab0->stale);
  CHECK_STR(label_text(lab0->obj), "Stale");

  // Rule 3: on the page on screen, a thermostat's new value (same size of
  // text) and a label's text lay nothing out.
  tab::go_to(0);
  hh::advance(20);
  lv_obj_invalidate(lv_screen_active());
  hh::refresh();
  CHECK(!layout_pending());
  D::Widget *th0 = widget(23);
  if (!CHECK(th0)) return;
  CHECK_STR(set("[16,22.5]"), "");
  CHECK_STR(label_text(th0->o1), "22.5");
  CHECK(!layout_pending());
  hh::refresh();
  CHECK_STR(set("[3,\"A new text\"]"), "");
  CHECK_STR(label_text(lab0->obj), "A new text");
  CHECK(!layout_pending());
  hh::refresh();

  // Rule 1 again: no stale paint in a tick that ran a build step.
  CHECK_STR(set("[26,\"During a build\"]"), "");
  CHECK(lab->stale);
  const std::string kit = layout_bytes("layout_v2_kit.dl.json");
  CHECK_STR(ha.send(kit), "");
  CHECK_STR(ha.commit(sha_hex(kit).substr(0, 16)), "");
  int ticks = 0;
  for (int t = 0; t < 2000 && D::staged_layout() && D::staged_layout()->state == D::L_BUILDING; t++) {
    hh::advance(1);
    ticks++;
    if (D::staged_layout() && D::staged_layout()->state == D::L_BUILDING) CHECK(lab->stale && l->stale == 1);
  }
  CHECK_MSG(ticks > 10, std::to_string(ticks) + " ms of building");

  // A card's `on` lays nothing out either (card 2 of the kit: a badge, two
  // labels and a bar; the switch that shares its slot is on page 2).
  const std::string kh = sha_hex(kit).substr(0, 16);
  CHECK(advance_until([&] { return kh == tab::active_hash(); }, 5000));
  CHECK_STR(ha.set(fixture("state_v2.json")["steps"][0]["payloads"][0].s), "");
  hh::advance(50);
  lv_obj_invalidate(lv_screen_active());
  hh::refresh();
  CHECK(!layout_pending());
  D::Widget *card2 = widget(2);
  if (!CHECK(card2 && D::card_look(*active()->model, card2))) return;
  CHECK(D::card_look(*active()->model, card2)->k.filled == 1);
  reset();
  CHECK_STR(ha.set("{\"h\":\"" + kh + "\",\"v\":[[2,0]]}"), "");
  CHECK(D::card_look(*active()->model, card2)->k.filled == 0);
  CHECK(D::test_counts().style_sets > 0);
  CHECK(!layout_pending());

  // (package E6) A moving bar: no style call, no layout pass, and only the
  // strip between the old and the new end of the fill is drawn again. The
  // two bars of slot 7 (cards 1 and 2), and the upright one of card 3.
  hh::refresh();
  lv_display_t *disp = lv_display_get_default();
  D::Widget *bar1 = widget(6), *bar2 = widget(11), *up = widget(15);
  if (!CHECK(bar1 && bar2 && up && bar1->sl_pos == (int) D::px_round(0.8 * 344))) return;
  // The rectangles drawn again, as the parts of each that meet r.
  auto redrawn = [&](const int16_t r[4], int *along) {
    int from = 1 << 20, to = -(1 << 20), n = 0;
    for (uint32_t i = 0; i < disp->inv_p; i++) {
      if (disp->inv_area_joined[i]) continue;
      const lv_area_t &a = disp->inv_areas[i];
      if (a.x2 < r[0] || a.x1 >= r[0] + r[2] || a.y2 < r[1] || a.y1 >= r[1] + r[3]) continue;
      n++;
      from = std::min(from, (int) a.x1);
      to = std::max(to, (int) a.x2 + 1);
    }
    *along = to - from;
    return n;
  };
  reset();
  CHECK_STR(ha.set("{\"h\":\"" + kh + "\",\"v\":[[7,70]]}"), "");
  CHECK(bar1->sl_pos == (int) D::px_round(0.7 * 344) && bar2->sl_pos == bar1->sl_pos);
  CHECK_MSG(D::test_counts().style_sets == 0, std::to_string(D::test_counts().style_sets) + " style calls");
  CHECK(!layout_pending());
  int along = 0;
  // 34 px moved, with the radius, the handle and a pixel each side: under 60.
  CHECK(redrawn(bar1->r, &along) >= 1 && along > 34 && along < 60);
  CHECK(redrawn(bar2->r, &along) >= 1 && along > 34 && along < 60);
  hh::refresh();
  CHECK_STR(ha.set("{\"h\":\"" + kh + "\",\"v\":[[7,75]]}"), "");
  CHECK(redrawn(up->r, &along) >= 1 && along <= up->r[2] + 28);
  int tall = 1 << 20, top = -(1 << 20);
  for (uint32_t i = 0; i < disp->inv_p; i++) {
    const lv_area_t &a = disp->inv_areas[i];
    if (disp->inv_area_joined[i] || a.x2 < up->r[0] || a.x1 >= up->r[0] + up->r[2]) continue;
    tall = std::min(tall, (int) a.y1);
    top = std::max(top, (int) a.y2 + 1);
  }
  CHECK_MSG(top - tall < 40, std::to_string(top - tall) + " rows of the upright bar");  // 10 px moved
  CHECK(!layout_pending() && D::test_counts().style_sets == 0);
  hh::refresh();
  // The same value again draws nothing.
  CHECK_STR(ha.set("{\"h\":\"" + kh + "\",\"v\":[[7,75]]}"), "");
  CHECK(redrawn(bar1->r, &along) == 0 && redrawn(up->r, &along) == 0);
}

// ================================================================ cards as buttons (package E4)
//
// Contract 4.3 ("Who gets a press") and 7.4, cards spec B.5: the tap of a
// card, its pressed look, pulse and waiting outline, the border of `lit` 2,
// the optimistic bool per slot, `un`, `vis`, the hold, and the press that is
// lost. The kit's ids and rectangles are those of layout_v2_kit.dl.json.

static std::string kit_hash() { return sha_hex(layout_bytes("layout_v2_kit.dl.json")).substr(0, 16); }

// The slot values of an input_v2.json case as a full snapshot.
static std::string snapshot_of(const tj::Value &slots, const std::string &hash) {
  std::string v;
  for (const tj::Value &s : slots.a) {
    if (!v.empty()) v += ',';
    v += "[" + json_text(s[0]) + "," + json_text(s[1]) + "]";
  }
  return "{\"f\":1,\"h\":\"" + hash + "\",\"v\":[" + v + "]}";
}

static std::vector<std::string> fields_of(const std::string &d) {
  std::vector<std::string> f;
  size_t at = 0;
  for (size_t bar = d.find('|'); bar != std::string::npos; bar = d.find('|', at)) {
    f.push_back(d.substr(at, bar - at));
    at = bar + 1;
  }
  f.push_back(d.substr(at));
  return f;
}

// The inputs sent so far as "<id><event>" words: "2t 6c".
static std::string sent_words() {
  std::string s;
  for (const auto &e : fake::events("tab1_input")) {
    const auto f = fields_of(e.d);
    if (f.size() != 9) return "(bad input " + e.d + ")";
    if (!s.empty()) s += ' ';
    s += f[5] + f[6];
  }
  return s;
}

static uint32_t last_input_counter() {
  const auto ev = fake::events("tab1_input");
  if (ev.empty()) return 0;
  const auto f = fields_of(ev.back().d);
  return f.size() == 9 ? (uint32_t) atoi(f[3].c_str()) : 0;
}

static void finger(int x, int y, bool down) {
  hh::g_x = x;
  hh::g_y = y;
  hh::g_down = down;
}

// A press at one point held `ms`, then the release and 40 ms after it.
static void press_for(int x, int y, uint32_t ms) {
  finger(x, y, true);
  hh::advance(ms);
  finger(x, y, false);
  hh::advance(40);
}

// The touches, values and acks of an input_v2.json case, each at its time
// from now; then 200 ms for the release to be read.
static void play_case(Ha &ha, const tj::Value &cs, const std::string &hash) {
  struct Ev {
    uint32_t ms;
    int kind;  // 0 touch, 1 tab_set, 2 ack
    const tj::Value *v;
  };
  std::vector<Ev> evs;
  for (const tj::Value &t : cs["touch"].a) evs.push_back({(uint32_t) t[0].n, 0, &t});
  for (const tj::Value &s : cs["sets"].a) evs.push_back({(uint32_t) s[0].n, 1, &s});
  for (const tj::Value &a : cs["acks"].a) evs.push_back({(uint32_t) a[0].n, 2, &a});
  std::stable_sort(evs.begin(), evs.end(), [](const Ev &a, const Ev &b) { return a.ms < b.ms; });
  const uint32_t t0 = hh::g_ms;
  for (const Ev &e : evs) {
    if (t0 + e.ms > hh::g_ms) hh::advance(t0 + e.ms - hh::g_ms);
    const tj::Value &v = *e.v;
    if (e.kind == 0) {
      finger((int) v[2].n, (int) v[3].n, v[1].s == "up" ? false : v[1].s == "down" ? true : hh::g_down);
    } else if (e.kind == 1) {
      std::string pairs;
      for (const tj::Value &p : v[1].a) pairs += (pairs.empty() ? "[" : ",[") + json_text(p[0]) + "," + json_text(p[1]) + "]";
      CHECK_STR(ha.set("{\"h\":\"" + hash + "\",\"v\":[" + pairs + "]}"), "");
    } else {
      ha.ack(last_input_counter(), v[1].b, v[2].s);
    }
  }
  hh::advance(200);
}

// What a played case did against what it expects; empty when all holds.
static std::vector<std::string> case_misses(const tj::Value &cs, const std::string &hash) {
  std::vector<std::string> miss;
  const auto ev = fake::events("tab1_input");
  const tj::Value &want = cs["events"];
  if (ev.size() != want.size())
    miss.push_back("sent \"" + sent_words() + "\", want " + std::to_string(want.size()) + " inputs");
  for (size_t i = 0; i < ev.size() && i < want.size(); i++) {
    const auto f = fields_of(ev[i].d);
    const tj::Value &w = want[i];
    const bool value_ok = w[3].is_null() ? f.size() == 9 && f[8].empty()
                                         : f.size() == 9 && !f[8].empty() && atof(f[8].c_str()) == w[3].n;
    if (f.size() != 9 || f[4] != hash || f[5] != std::to_string((uint32_t) w[0].n) || f[6] != w[1].s ||
        f[7] != std::to_string((int) w[2].n) || !value_ok)
      miss.push_back("input " + std::to_string(i) + " is " + ev[i].d);
  }
  if (tab::current_page() != (int) cs["end_page"].n)
    miss.push_back("ends on page " + std::to_string(tab::current_page()) + ", want " + cs["end_page"].s);
  if (!hidden(D::g.question) != (cs["question"].n == 1)) miss.push_back("question box");
  if (cs.has("counter") && cs.has("payload") && !ev.empty() &&
      (ev[0].d != cs["payload"]["d"].s || ev[0].s != cs["payload"]["s"].s))
    miss.push_back("payload " + ev[0].d + " " + ev[0].s);
  return miss;
}

// One case on a fresh, paired tablet with a session, on its page and with
// its values (input_v2.json "note").
static std::vector<std::string> run_input_case(const tj::Value &cs) {
  Ha ha;
  ha.boot(true);
  if (!ha.hello()) return {"no session"};
  if (!ha.show(layout_bytes("layout_v2_kit.dl.json"))) return {"the kit is not on screen"};
  const std::string hash = kit_hash();
  if (!ha.set(snapshot_of(cs["slots"], hash)).empty()) return {"the snapshot is refused"};
  tab::go_to((int) cs["page"].n);
  hh::advance(50);
  fake::st.events.clear();
  if (cs.has("counter")) D::g.counter = (uint32_t) cs["counter"].n - 1;
  play_case(ha, cs, hash);
  return case_misses(cs, hash);
}

// Touch cases that need a part of a later package: each is played and
// reported, and is not a failure yet. The package turns it green and takes
// it off this list.
static const char *const INPUT_LATER[][2] = {
    {"choice_cell", "E8"},
    {"choice_gap", "E8"},               {"choice_hold_is_none", "E8"},
    {"choice_released_in_another_cell", "E8"}, {"choice_second_row", "E8"},
    {"choice_asks_first", "E8"},        {"swipe_right_wraps", "E9"},
    {"back_entry", "E9"},               {"back_button", "E9"},
    {"detail_swipe_right_returns", "E9"}, {"detail_swipe_left_does_nothing", "E9"},
};

static const char *input_later(const std::string &name) {
  for (const auto &row : INPUT_LATER)
    if (name == row[0]) return row[1];
  return nullptr;
}

// Every touch case of input_v2.json (contract 4.3 and 7.4): the touches at
// their points and times, with the tab_set a case sends mid-press, emit
// exactly the listed inputs, end on the listed page, with the question box
// as listed; a case with a counter emits the payload of the fixture.
TEST(input_v2_fixture) {
  const tj::Value file = fixture("input_v2.json");
  CHECK_STR(file["layout"].s, "layout_v2_kit");
  CHECK_STR(file["hash"].s, kit_hash());
  CHECK_STR(file["mac"].s, fake::MAC);
  CHECK_STR(file["nonce"].s, NONCE);
  CHECK_STR(file["secret_hex"].s, SECRET_HEX);
  int run = 0, later = 0, counted = 0;
  std::vector<std::string> seen;
  for (const tj::Value &cs : file["cases"].a) {
    if (!cs.has("touch")) continue;  // the cases for Home Assistant alone
    const std::string name = cs["name"].s;
    seen.push_back(name);
    const auto miss = run_input_case(cs);
    if (const char *pkg = input_later(name)) {
      later++;
      printf("  %s: package %s, %s\n", name.c_str(), pkg, miss.empty() ? "passes already" : miss[0].c_str());
      continue;
    }
    run++;
    if (cs.has("counter")) counted++;
    std::string all;
    for (const auto &m : miss) all += m + "; ";
    CHECK_MSG(miss.empty(), name + ": " + all);
  }
  for (const auto &row : INPUT_LATER)
    CHECK_MSG(std::find(seen.begin(), seen.end(), row[0]) != seen.end(), std::string(row[0]) + " is no case");
  printf("  %d cases run, %d for later packages, %d with their payload\n", run, later, counted);
  CHECK(run >= 45 && counted >= 4 && run + later == (int) seen.size());
}

// The kit on a paired tablet with a session, page 0 on screen, and the
// values of the first case of input_v2.json; or another layout without
// values.
struct KitRig {
  Ha ha;
  std::string hash;
  bool up(const std::string &bytes = "", bool values = true) {
    const std::string b = bytes.empty() ? layout_bytes("layout_v2_kit.dl.json") : bytes;
    ha.boot(true);
    if (!ha.hello() || !ha.show(b)) return false;
    hash = sha_hex(b).substr(0, 16);
    if (values && !ha.set(snapshot_of(fixture("input_v2.json")["cases"][0]["slots"], hash)).empty()) return false;
    hh::advance(50);
    fake::st.events.clear();
    return true;
  }
  std::string set(const std::string &v) { return ha.set("{\"h\":\"" + hash + "\",\"v\":[" + v + "]}"); }
  // A clean start for the next press: feedback over, nothing sent so far.
  void settle() {
    hh::advance(4500);
    if (!hidden(D::g.question)) D::question_answer(false);
    fake::st.events.clear();
  }
};

// The table "Who gets a press" of contract 4.3, row by row, on the kit's
// "Routing" card (page 1: tap and hold, a thermostat with change, a swap of
// two buttons, labels, a bar that only shows, a switch) and card 2 (page 0).
// The gap of a `choice` comes with package E8 (input_v2_fixture lists it).
TEST(card_tap_and_children) {
  KitRig rig;
  if (!CHECK(rig.up())) return;
  D::Widget *c2 = widget(2), *c12 = widget(12), *c46 = widget(46);
  if (!CHECK(c2 && c12 && c46)) return;
  // A card with tap or hold is a button; one without is not.
  CHECK(clickable(c2->obj) && clickable(c46->obj) && !clickable(c12->obj));
  tab::go_to(1);
  hh::advance(20);
  struct Row {
    const char *what;
    const char *set;  // values before the press, or ""
    int x, y;
    const char *sent;
  };
  const Row rows[] = {
      {"a child with tap is never the card's", "", 1036, 628, "53t"},
      {"a label is the card's", "", 1124, 554, "46t"},
      {"a label with its own colour is the card's", "", 1124, 518, "46t"},
      {"a bar that only shows is the card's", "", 1124, 588, "46t"},
      {"so is the same bar faded for want of a value", "[34,null]", 1124, 588, "46t"},
      {"the body of a thermostat is the card's", "[34,40]", 888, 546, "46t"},
      {"a thermostat's button is the button's", "", 970, 626, "47c"},
      {"a faded switch swallows the press", "[35,null]", 1036, 628, ""},
      {"the card outside its widgets", "[35,1]", 1200, 420, "46t"},
  };
  for (const Row &r : rows) {
    if (*r.set) CHECK_STR(rig.set(r.set), "");
    hh::tap(r.x, r.y);
    hh::advance(20);
    CHECK_MSG(sent_words() == r.sent, std::string(r.what) + ": sent \"" + sent_words() + "\"");
    CHECK(tab::current_page() == 1);
    rig.settle();
  }
  // Drift on card 2: 20 px, 13 px and back, 9 and 9 are no tap; 8 and 8 is
  // (11.3 px). None of these is a swipe.
  tab::go_to(0);
  hh::advance(20);
  struct Drift {
    int dx, dy, back;
    const char *sent;
  };
  const Drift drifts[] = {{20, 0, 0, ""}, {13, 0, 1, ""}, {9, 9, 0, ""}, {8, 8, 0, "2t"}, {0, 11, 0, "2t"}, {0, 12, 0, ""}};
  for (const Drift &d : drifts) {
    finger(300, 240, true);
    hh::advance(30);
    finger(300 + d.dx, 240 + d.dy, true);
    hh::advance(30);
    if (d.back) finger(300, 240, true);
    hh::advance(40);
    finger(hh::g_x, hh::g_y, false);
    hh::advance(40);
    CHECK_MSG(sent_words() == d.sent, std::to_string(d.dx) + "," + std::to_string(d.dy) + (d.back ? " and back" : "") +
                                          ": sent \"" + sent_words() + "\"");
    CHECK(tab::current_page() == 0 && !c2->pressed);
    rig.settle();
  }
  // A swipe that starts on a card taps nothing and shows no pressed look:
  // it has moved 12 px before the 80 ms are over.
  bool looked = false;
  finger(300, 240, true);
  for (int i = 1; i <= 25; i++) {
    hh::advance(25);
    finger(300 - 6 * i, 240, true);
    looked |= c2->pressed != 0;
  }
  finger(hh::g_x, hh::g_y, false);
  hh::advance(40);
  CHECK(tab::current_page() == 1 && sent_words().empty() && !looked);
}

// A theme whose every role has its own colour, so a check of one role
// cannot pass on another (a layout without a theme has every role 0).
static const char *DISTINCT_THEME =
    "\"theme\":{\"wall\":1052688,\"card\":2105376,\"card_pr\":3158064,\"card2\":4210752,\"card2_pr\":5263440,"
    "\"line\":6316128,\"tx\":14737632,\"tx2\":10526880,\"acc\":16711680,\"accf\":65280,\"accf_pr\":43520,"
    "\"acct\":255,\"acc2\":16711935,\"onbg\":7360512,\"onbg_pr\":8413184,\"ontx\":16777200,\"ion\":65535,"
    "\"dark\":1,\"r\":16,\"r2\":10,\"bw\":0},";

// A layout of two pages in DISTINCT_THEME: page 0 holds card 1 with
// `card_keys` and the widgets `widgets`, page 1 a heading. Slots `slots`.
static std::string two_pages(const std::string &card_keys, const std::string &widgets, const std::string &slots,
                             const std::string &style = "") {
  return std::string("{") + DISTINCT_THEME + "\"screen\":[1280,800]," +
         "\"nav\":[{\"p\":0,\"title\":\"P\"},{\"p\":1,\"title\":\"Q\"}],\"pages\":[{\"title\":\"P\",\"w\":[{" + card_keys +
         (style.empty() ? "" : "\"style\":" + style + ",") +
         "\"hr\":[116,100,536,32],\"icon\":983861,\"id\":1,\"r\":[100,84,568,400],\"t\":\"card\",\"title\":\"Lamp\","
         "\"w\":[" + widgets + "]}]},{\"title\":\"Q\",\"w\":[{\"id\":90,\"r\":[100,20,568,56],\"t\":\"heading\","
         "\"text\":\"Q\"}]}],\"slots\":\"" + slots + "\",\"v\":2}";
}

// A light card: `on` and a switch and a button on slot 1, another switch on
// slot 2, a label (slots "bbn").
static const char *LAMP_WIDGETS =
    "{\"id\":2,\"r\":[116,140,200,48],\"t\":\"switch\",\"tap\":{\"p\":0},\"value\":{\"s\":1}},"
    "{\"id\":3,\"r\":[116,200,200,48],\"t\":\"button\",\"tap\":{\"p\":0},\"text\":\"On\",\"value\":{\"s\":1}},"
    "{\"id\":4,\"r\":[116,260,200,48],\"t\":\"switch\",\"tap\":{\"p\":0},\"value\":{\"s\":2}},"
    "{\"id\":5,\"r\":[116,320,300,28],\"size\":1,\"t\":\"label\",\"text\":\"Label\"}";

static uint32_t text_colour(lv_obj_t *o) {
  return o ? lv_color_to_u32(lv_obj_get_style_text_color(o, LV_PART_MAIN)) & 0xFFFFFF : 0;
}

static uint32_t bg_colour(lv_obj_t *o) {
  return o ? lv_color_to_u32(lv_obj_get_style_bg_color(o, LV_PART_MAIN)) & 0xFFFFFF : 0;
}

static int knob_at(D::Widget *w) {
  lv_obj_update_layout(w->o2);
  return (int) lv_obj_get_x(w->o2);
}

// Contract 7.4 for a card with tap: policy 0 with a bound `on` shows the
// other state at once and goes back on an ack that is not ok or after 4 s
// without one; the optimistic bool is per slot inside the card, on the card,
// its switches and its buttons together; without a value for `on`, and
// without `on`, the waiting outline; policy 1 the pulse; policy 2 the
// outline until a value of the card changes; policy 3 the question, then
// the outline. And the pressed look, 80 ms after the touch.
TEST(card_feedback) {
  KitRig rig;
  if (!CHECK(rig.up(two_pages("\"on\":{\"s\":1},\"tap\":{\"p\":0},", LAMP_WIDGETS, "bbn"), false))) return;
  CHECK_STR(rig.set("[1,1],[2,0],[3,5]"), "");
  hh::advance(20);
  D::Model &m = *active()->model;
  D::Widget *card = widget(1), *sw = widget(2), *bt = widget(3), *sw2 = widget(4);
  if (!CHECK(card && sw && bt && sw2)) return;
  auto filled = [&] { return D::card_look(m, card)->k.filled == 1; };
  auto agree = [&](bool on) {
    return filled() == on && knob_at(sw) == (on ? 32 : 4) && D::button_selected(m, *bt) == on && knob_at(sw2) == 4;
  };
  CHECK(agree(true));
  // The card's tap: the other state at once, on the card, the switch and
  // the button of its slot, not on the switch of another slot. The card's
  // colours are worked out once.
  D::test_counts() = D::TestCounts{};
  hh::tap(500, 420);
  CHECK_STR(sent_words(), "1t");
  CHECK(card->ovr == 1 && card->ovr_slot == 1 && card->fb == 0 && agree(false));
  CHECK_MSG(D::test_counts().look_updates == 1, std::to_string(D::test_counts().look_updates));
  // A repaint with that state works nothing out.
  D::test_contrasts() = 0;
  for (uint32_t i = 0; i < m.nw; i++) D::paint_widget(*active(), m.w[i]);
  CHECK(D::test_contrasts() == 0);
  // An ack that is not ok: back at once.
  rig.ha.ack(last_input_counter(), false, "failed");
  CHECK(card->ovr == 0 && agree(true));
  // No ack: back after 4 s.
  hh::tap(500, 420);
  hh::advance(3800);
  CHECK(agree(false));
  hh::advance(300);
  CHECK(agree(true));
  // An ok ack: the state stays until the value comes.
  hh::tap(500, 420);
  rig.ha.ack(last_input_counter(), true, "");
  hh::advance(1000);
  CHECK(card->ovr == 2 && agree(false));
  CHECK_STR(rig.set("[1,0]"), "");
  CHECK(card->ovr == 0 && agree(false));
  // The switch's tap: the card and the button follow.
  rig.settle();
  hh::tap(centre_x(sw->r), centre_y(sw->r));
  CHECK_STR(sent_words(), "2t");
  CHECK(card->ovr_slot == 1 && agree(true));
  rig.ha.ack(last_input_counter(), false, "failed");
  CHECK(agree(false));
  // The button's tap: the card and the switch follow.
  rig.settle();
  hh::tap(centre_x(bt->r), centre_y(bt->r));
  CHECK_STR(sent_words(), "3t");
  CHECK(agree(true));
  hh::advance(4100);
  CHECK(agree(false));
  // The switch of slot 2 flips alone.
  rig.settle();
  hh::tap(centre_x(sw2->r), centre_y(sw2->r));
  CHECK(knob_at(sw2) == 32 && knob_at(sw) == 4 && !filled() && card->ovr_slot == 2);
  rig.settle();
  // The switch of slot 2 while the card's own tap waits: the card, the
  // switch and the button of slot 1 keep the card's flip; each ends alone.
  CHECK(agree(false));
  hh::tap(500, 420);
  CHECK(card->ovr == 1 && agree(true));
  hh::tap(centre_x(sw2->r), centre_y(sw2->r));
  CHECK_STR(sent_words(), "1t 4t");
  CHECK(card->ovr == 1 && card->ovr_slot == 2 && knob_at(sw2) == 32);
  CHECK(filled() && knob_at(sw) == 32 && D::button_selected(m, *bt));
  rig.ha.ack(last_input_counter(), false, "failed");  // the switch's
  CHECK(card->ovr == 1 && agree(true));
  hh::advance(4100);
  CHECK(card->ovr == 0 && agree(false));
  rig.settle();
  // `on` without a value: the waiting outline, 2 px in acc, until the ack.
  CHECK_STR(rig.set("[1,null]"), "");
  const D::CardLook *k = D::card_look(m, card);
  CHECK(k->k.A == 0xFF0000 && k->k.FI == 0x00FFFF);  // acc and ion of DISTINCT_THEME
  hh::tap(500, 420);
  CHECK_STR(sent_words(), "1t");
  CHECK(card->fb == 1 && card->ovr == 0);
  CHECK(lv_obj_get_style_outline_width(card->obj, LV_PART_MAIN) == 2 &&
        (lv_color_to_u32(lv_obj_get_style_outline_color(card->obj, LV_PART_MAIN)) & 0xFFFFFF) == k->k.A);
  rig.ha.ack(last_input_counter(), true, "");
  CHECK(card->fb == 0 && lv_obj_get_style_outline_width(card->obj, LV_PART_MAIN) == 0);
  // While filled the outline is ion.
  CHECK_STR(rig.set("[1,1]"), "");
  D::start_feedback(*active(), *card, 2, 't', false, 0);
  CHECK((lv_color_to_u32(lv_obj_get_style_outline_color(card->obj, LV_PART_MAIN)) & 0xFFFFFF) == k->k.FI);
  // Policy 2: until a value of the card changes (here the switch of slot 2).
  CHECK(card->fb == 1);
  CHECK_STR(rig.set("[2,1]"), "");
  CHECK(card->fb == 0);

  // A card without `on`: the waiting outline.
  if (!CHECK(rig.up(two_pages("\"tap\":{\"p\":0},", LAMP_WIDGETS, "bbn"), false))) return;
  CHECK_STR(rig.set("[1,1],[2,0]"), "");
  card = widget(1);
  hh::tap(500, 420);
  CHECK(sent_words() == "1t" && card->fb == 1 && card->ovr == 0);
  hh::advance(4100);
  CHECK(card->fb == 0);

  // Policy 1, the pulse: accf for 300 ms; the header and the labels acct.
  if (!CHECK(rig.up(two_pages("\"on\":{\"s\":1},\"tap\":{\"p\":1},", LAMP_WIDGETS, "bbn"), false))) return;
  CHECK_STR(rig.set("[1,0],[2,0]"), "");
  D::Model &m1 = *active()->model;
  card = widget(1);
  D::Widget *lab = widget(5);
  k = D::card_look(m1, card);
  // accf and acct of DISTINCT_THEME, each unlike what the card shows at rest.
  CHECK(k->k.AF == 0x00FF00 && k->k.AT == 0x0000FF && k->t.bg == 0x202020);
  CHECK(k->k.text != k->k.AT && k->k.header_icon != k->k.AT && k->k.F != k->k.AF);
  hh::refresh();
  CHECK(hh::pixel(560, 440) == hh::rgb565(k->t.bg));
  hh::tap(500, 420);
  CHECK(sent_words() == "1t" && card->fb == 2);
  hh::refresh();
  CHECK(hh::pixel(560, 440) == hh::rgb565(k->k.AF));
  CHECK(text_colour(lab->obj) == k->k.AT && text_colour(card->o1) == k->k.AT && text_colour(card->o2) == k->k.AT);
  hh::advance(320);
  hh::refresh();
  CHECK(card->fb == 0 && hh::pixel(560, 440) == hh::rgb565(k->t.bg));
  CHECK(text_colour(lab->obj) == k->k.text && text_colour(card->o1) == k->k.header_icon);

  // Policy 3, and cf: the question, then the outline.
  if (!CHECK(rig.up(two_pages("\"confirm\":\"Sure?\",\"on\":{\"s\":1},\"tap\":{\"p\":3},", LAMP_WIDGETS, "bbn"), false)))
    return;
  CHECK_STR(rig.set("[1,1],[2,0]"), "");
  card = widget(1);
  hh::tap(500, 420);
  CHECK(sent_words().empty() && !hidden(D::g.question) && card->fb == 0 && card->ovr == 0);
  CHECK_STR(label_text(D::g.q_text), "Sure?");
  hh::tap(776, 456);  // Yes
  CHECK(fake::events("tab1_input").size() == 1 && fake::events("tab1_input")[0].d.find("|1|t|1|") != std::string::npos);
  CHECK(card->fb == 1 && card->ovr == 0 && D::card_look(*active()->model, card)->k.filled == 1);

  // The pressed look: 80 ms after the touch, card_pr (on: onbg_pr), gone at
  // the release; not after 12 px of movement.
  if (!CHECK(rig.up(two_pages("\"hold\":{\"go\":1},\"on\":{\"s\":1},\"tap\":{\"p\":0},", LAMP_WIDGETS, "bbn"), false)))
    return;
  CHECK_STR(rig.set("[1,0],[2,0]"), "");
  D::Model &m3 = *active()->model;
  card = widget(1);
  k = D::card_look(m3, card);
  CHECK(k->pr == k->t.card_pr && k->pr == 0x303030 && k->t.bg == 0x202020 && k->k.FP == 0x806000);
  finger(500, 420, true);
  hh::advance(1);
  CHECK(advance_until([] { return D::g.tracking; }, 10));
  const uint32_t at = D::g.press_at;
  hh::advance(at + 79 - hh::g_ms);
  CHECK(!card->pressed);
  hh::refresh();
  CHECK(hh::pixel(560, 440) == hh::rgb565(k->t.bg));
  hh::advance(6);
  CHECK(card->pressed);
  hh::refresh();
  CHECK(hh::pixel(560, 440) == hh::rgb565(k->pr));
  finger(500, 420, false);
  hh::advance(40);
  CHECK(!card->pressed && sent_words() == "1t");
  hh::refresh();
  rig.settle();
  CHECK_STR(rig.set("[1,1]"), "");
  finger(500, 420, true);
  hh::advance(120);
  hh::refresh();
  CHECK(card->pressed && hh::pixel(560, 440) == hh::rgb565(k->k.FP));
  // Moved 12 px: the look goes, and the release is no tap.
  finger(512, 420, true);
  hh::advance(20);
  CHECK(!card->pressed);
  finger(500, 420, false);
  hh::advance(40);
  CHECK(sent_words().empty());
  bool looked = false;
  finger(500, 420, true);
  hh::advance(30);
  finger(500, 433, true);
  for (int i = 0; i < 100; i++) {
    hh::advance(5);
    looked |= card->pressed != 0;
  }
  finger(500, 433, false);
  hh::advance(40);
  CHECK(!looked && sent_words().empty());
  // A style with bg and no card_pr: mix(tx, bg, 90).
  if (!CHECK(rig.up(two_pages("\"tap\":{\"p\":0},", LAMP_WIDGETS, "bbn", "{\"bg\":2105392}"), false))) return;
  card = widget(1);
  k = D::card_look(*active()->model, card);
  CHECK(k->t.bg == 2105392 && k->pr == D::mix(k->t.tx, 2105392, 90) && k->pr != k->t.card_pr);
  finger(500, 420, true);
  hh::advance(120);
  hh::refresh();
  CHECK(card->pressed && hh::pixel(560, 440) == hh::rgb565(k->pr));
  finger(500, 420, false);
  hh::advance(40);
}

// The border of `lit` 2 (contract 4.7): while `on` is 1, `acc` and at least
// 2 wide, drawn over the card's own border without changing the object's
// border width, so no widget of the card moves and nothing is laid out.
TEST(lit2_border) {
  KitRig rig;
  if (!CHECK(rig.up())) return;
  D::Model &m = *active()->model;
  D::Widget *c7 = widget(7), *img = widget(8);
  if (!CHECK(c7 && img)) return;
  const D::CardLook *k = D::card_look(m, c7);
  CHECK(k->t.bw == 0 && c7->lit == 2);
  CHECK(k->k.lit_on == 1 && k->k.border_width == 2 && k->k.border == k->k.A && c7->ring == 2);
  CHECK(lv_obj_get_style_border_width(c7->obj, LV_PART_MAIN) == 0);
  hh::refresh();
  const int y = 84 + 124;
  CHECK(hh::pixel(484, y) == hh::rgb565(k->k.A) && hh::pixel(485, y) == hh::rgb565(k->k.A));
  CHECK(hh::pixel(486, y) == hh::rgb565(k->t.bg));
  const std::string before = rect_text(img->obj);
  lv_obj_invalidate(lv_screen_active());
  hh::refresh();
  CHECK(!layout_pending());
  CHECK_STR(rig.set("[2,0]"), "");
  CHECK(c7->ring == 0 && k->k.border_width == 0 && !layout_pending());
  hh::refresh();
  CHECK(hh::pixel(484, y) == hh::rgb565(k->t.bg));
  CHECK_STR(rig.set("[2,1]"), "");
  CHECK(c7->ring == 2 && !layout_pending());
  hh::refresh();
  CHECK(hh::pixel(485, y) == hh::rgb565(k->k.A));
  CHECK_STR(rect_text(img->obj), before);
  CHECK(lv_obj_get_style_border_width(c7->obj, LV_PART_MAIN) == 0);
  // A border of 1 becomes 2; one of 3 stays 3, and nothing is drawn over it.
  for (int bw : {1, 3}) {
    const std::string style = "{\"bw\":" + std::to_string(bw) + "}";
    if (!CHECK(rig.up(two_pages("\"lit\":2,\"on\":1,", LAMP_WIDGETS, "bbn", style), false))) return;
    D::Widget *c = widget(1);
    k = D::card_look(*active()->model, c);
    CHECK_MSG(k->k.border_width == (bw > 2 ? bw : 2) && c->ring == (bw > 2 ? 0 : 2), std::to_string(bw));
    CHECK(lv_obj_get_style_border_width(c->obj, LV_PART_MAIN) == bw);
    hh::refresh();
    CHECK_MSG(hh::pixel(100, 284) == hh::rgb565(k->k.A) && hh::pixel(101, 284) == hh::rgb565(k->k.A),
              std::to_string(bw));
    CHECK_MSG(hh::pixel(100 + (bw > 2 ? bw : 2), 284) == hh::rgb565(k->t.bg), std::to_string(bw));
  }
}

// A hold (contract 4.3 and 7.4): 500 ms opens the page that the table names,
// sends nothing, and the release is no tap; 499 ms is a tap; 12 px of
// movement is no hold. None on a slider with change, a thermostat's button
// or a `choice` with change; yes on a thermostat's body, on a button with
// tap (the card's page), on a label with its own hold. None during the erase
// watch: such a press releases as the card's tap.
TEST(hold_is_not_a_tap) {
  KitRig rig;
  if (!CHECK(rig.up())) return;
  // Released before 500 ms on card 2 (tap and hold): a tap. The release is
  // read up to one touch period (16 ms) after the finger goes.
  finger(300, 240, true);
  hh::advance(1);
  CHECK(advance_until([] { return D::g.tracking; }, 10));
  hh::advance(D::g.press_at + 499 - 16 - hh::g_ms);
  CHECK(tab::current_page() == 0);
  finger(300, 240, false);
  hh::advance(40);
  CHECK(tab::current_page() == 0 && sent_words() == "2t");
  rig.settle();
  // 500 ms: page 3, nothing sent, no tap at the release.
  finger(300, 240, true);
  hh::advance(1);
  CHECK(advance_until([] { return D::g.tracking; }, 10));
  hh::advance(D::g.press_at + 499 - hh::g_ms);
  CHECK(tab::current_page() == 0);
  hh::advance(D::TICK_MS + 1);
  CHECK(tab::current_page() == 3 && sent_words().empty());
  hh::advance(500);
  finger(300, 240, false);
  hh::advance(40);
  CHECK(tab::current_page() == 3 && sent_words().empty());
  tab::go_to(0);
  hh::advance(20);
  // 13 px and back: no hold, and no tap.
  finger(300, 240, true);
  hh::advance(30);
  finger(313, 240, true);
  hh::advance(30);
  finger(300, 240, true);
  hh::advance(700);
  finger(300, 240, false);
  hh::advance(40);
  CHECK(tab::current_page() == 0 && sent_words().empty());
  struct Row {
    const char *what;
    int page, x, y, end;
    const char *sent;
  };
  const Row rows[] = {
      {"a slider with change: none, its release sends", 0, 288, 292, 0, "6c"},
      {"a widget with only hold: its own", 0, 996, 636, 4, ""},
      {"a thermostat's button: none, the button sends", 1, 970, 626, 1, "47c"},
      {"the body of a thermostat: the card's", 1, 888, 546, 4, ""},
      {"a button with tap and no hold: the card's", 1, 1124, 472, 4, ""},
      {"a label: the card's", 1, 1124, 554, 4, ""},
      {"a card without hold: none, the release is its tap", 1, 520, 540, 1, "41t"},
  };
  for (const Row &r : rows) {
    tab::go_to(r.page);
    hh::advance(20);
    press_for(r.x, r.y, 700);
    CHECK_MSG(tab::current_page() == r.end && sent_words() == r.sent,
              std::string(r.what) + ": page " + std::to_string(tab::current_page()) + ", sent \"" + sent_words() + "\"");
    rig.settle();
  }
  // A label with its own hold in a card without one; a `choice` with
  // change in a card with one (the choice is drawn from package E8 on; the
  // hold already reads the model); a hold of the form {"p": n} is no hold.
  const std::string widgets =
      "{\"hold\":{\"go\":1},\"id\":2,\"r\":[116,140,300,28],\"size\":1,\"t\":\"label\",\"text\":\"Hold me\"},"
      "{\"change\":{\"p\":0},\"id\":3,\"r\":[116,200,536,48],\"t\":\"choice\",\"texts\":[\"A\",\"B\"],\"value\":{\"s\":1}}";
  if (!CHECK(rig.up(two_pages("", widgets, "n"), false))) return;
  CHECK(widget(2)->hold == 2 && !clickable(widget(1)->obj));
  press_for(200, 154, 700);
  CHECK(tab::current_page() == 1);
  if (!CHECK(rig.up(two_pages("\"hold\":{\"go\":1},", widgets, "n"), false))) return;
  press_for(300, 224, 700);
  CHECK(tab::current_page() == 0);
  press_for(500, 420, 700);
  CHECK(tab::current_page() == 1);
  if (!CHECK(rig.up(two_pages("\"hold\":{\"p\":0},\"tap\":{\"p\":1},", widgets, "n"), false))) return;
  CHECK(widget(1)->hold == 0);
  press_for(500, 420, 700);
  CHECK(tab::current_page() == 0 && sent_words() == "1t");
  // No hold while the question is open, also over a card with one.
  if (!CHECK(rig.up(two_pages("\"confirm\":\"Sure?\",\"hold\":{\"go\":1},\"tap\":{\"p\":3},", widgets, "n"), false)))
    return;
  hh::tap(500, 420);
  CHECK(!hidden(D::g.question) && D::g.q_open);
  finger(500, 420, true);
  hh::advance(700);
  CHECK(tab::current_page() == 0 && !hidden(D::g.question));
  finger(500, 420, false);
  hh::advance(40);
  CHECK(tab::current_page() == 0 && sent_words().empty());
  rig.settle();

  // The erase watch: a press that begins in the first 10 s after power-on
  // opens no page, and its release is the card's tap; after the watch the
  // same press opens the page.
  shutdown();
  reset_platform();
  memcpy(fake::st.secret, rig.ha.secret, 32);
  fake::st.have_secret = true;
  fake::st.powered_on_at = hh::g_ms;
  start();
  rig.ha.paired = true;
  rig.ha.hellos = 0;
  rig.ha.nonce = NONCE;
  CHECK(rig.ha.hello());
  const std::string kit = layout_bytes("layout_v2_kit.dl.json");
  CHECK(rig.ha.show(kit));
  rig.hash = kit_hash();
  CHECK_STR(rig.ha.set(snapshot_of(fixture("input_v2.json")["cases"][0]["slots"], rig.hash)), "");
  hh::advance(50);
  fake::st.events.clear();
  CHECK(D::g.erase_timer != nullptr && uptime() < D::ERASE_ARM_MS - 1000);
  press_for(300, 240, 600);
  CHECK(tab::current_page() == 0 && sent_words() == "2t");
  // Also when the watch ends under the finger: the press began in it.
  fake::st.events.clear();
  finger(300, 240, true);
  hh::advance(30);
  CHECK(D::g.tracking && D::g.press_in_watch);
  D::erase_stop();
  hh::advance(700);
  finger(300, 240, false);
  hh::advance(40);
  CHECK(tab::current_page() == 0 && sent_words() == "2t");
  advance_to_uptime(D::ERASE_ARM_MS + 100);
  CHECK(D::g.erase_timer == nullptr);
  fake::st.events.clear();
  press_for(300, 240, 600);
  CHECK(tab::current_page() == 3 && sent_words().empty());
}

// A press is lost (contract 7.4) when the widget that has it is hidden,
// faded or its card turns unavailable while the finger is down: the release
// sends nothing, also when the widget shows again first. A press that is the
// card's stays the card's when a widget under it that only shows fades.
TEST(press_lost) {
  KitRig rig;
  if (!CHECK(rig.up())) return;
  tab::go_to(1);
  hh::advance(20);
  D::Widget *arm = widget(48), *disarm = widget(49), *sw = widget(53);
  if (!CHECK(arm && disarm && sw)) return;
  struct Row {
    const char *what;
    int page, x, y;
    const char *during;  // the values sent while the finger is down
    const char *again;   // and then, or ""
    const char *sent;
    const char *after;   // the values after the release
  };
  const Row rows[] = {
      {"a swap under the finger", 1, 1124, 472, "[31,0],[32,1]", "", "", "[31,1],[32,0]"},
      {"a swap and back", 1, 1124, 472, "[31,0],[32,1]", "[31,1],[32,0]", "", ""},
      {"the value of the pressed switch goes away", 1, 1036, 628, "[35,null]", "", "", "[35,1]"},
      {"the card turns unavailable", 0, 684, 240, "[8,1]", "", "", "[8,0]"},
      {"the card turns unavailable and back", 0, 684, 240, "[8,1]", "[8,0]", "", ""},
      {"a bar that only shows fades: the card's press", 1, 1124, 588, "[34,null]", "", "46t", "[34,40]"},
      {"a label in it is not hidden: the card's press", 1, 1124, 554, "[33,0]", "", "46t", ""},
  };
  for (const Row &r : rows) {
    tab::go_to(r.page);
    hh::advance(20);
    finger(r.x, r.y, true);
    hh::advance(50);
    CHECK_STR(rig.set(r.during), "");
    if (*r.again) {
      hh::advance(20);
      CHECK_STR(rig.set(r.again), "");
    }
    hh::advance(30);
    finger(r.x, r.y, false);
    hh::advance(40);
    CHECK_MSG(sent_words() == r.sent, std::string(r.what) + ": sent \"" + sent_words() + "\"");
    CHECK_MSG(!arm->pressed && !disarm->pressed && !sw->pressed && D::g.cp_layout < 0 && tab::current_page() == r.page,
              r.what);
    if (*r.after) CHECK_STR(rig.set(r.after), "");
    rig.settle();
  }
  // A drag whose slider loses its value sends nothing.
  tab::go_to(0);
  hh::advance(20);
  D::Widget *bar = widget(6);
  finger(200, 292, true);
  hh::advance(30);
  finger(260, 292, true);
  hh::advance(30);
  CHECK(bar->dragging);
  CHECK_STR(rig.set("[7,null]"), "");
  CHECK(!bar->dragging && bar->faded);
  finger(300, 292, true);
  hh::advance(30);
  finger(300, 292, false);
  hh::advance(40);
  CHECK(sent_words().empty());
  CHECK_STR(rig.set("[7,80]"), "");
  rig.settle();
  // A card press and its hold are dropped at a forced swap.
  finger(300, 240, true);
  hh::advance(50);
  CHECK(D::g.cp_layout >= 0);
  const std::string small = layout_bytes("layout_small.dl.json");
  CHECK_STR(rig.ha.send(small), "");
  CHECK_STR(rig.ha.commit(sha_hex(small).substr(0, 16)), "");
  hh::advance(D::SWAP_WAIT_MS + 200);
  CHECK(tab::active_hash() == sha_hex(small).substr(0, 16));
  CHECK(D::g.cp_layout < 0 && tab::current_page() == 0);
  hh::advance(600);
  finger(300, 240, false);
  hh::advance(40);
  CHECK(sent_words().empty() && tab::current_page() == 0);
}

// `un` 1 (contract 4.3 and 4.7): the header and every widget of the card at
// 40%, once (a widget faded for want of a value is not faded again), the card
// itself not; nothing in the card sends while the tablet can send, a hold
// works, and while the tablet cannot send a tap says why.
TEST(unavailable_card) {
  KitRig rig;
  if (!CHECK(rig.up())) return;
  D::Model &m = *active()->model;
  D::Widget *c7 = widget(7);
  if (!CHECK(c7)) return;
  // Each object's own opacity: 40% on a parent and again on its child would
  // be 16% on screen.
  auto opa = [](lv_obj_t *o) { return o ? (int) lv_obj_get_style_opa(o, LV_PART_MAIN) : -1; };
  auto all_at = [&](int want) {
    bool ok = opa(c7->o1) == want && opa(c7->o2) == want && opa(c7->obj) == LV_OPA_COVER;
    for (uint32_t k = 1; k <= c7->children; k++) ok = ok && opa(m.w[c7 - m.w + k].obj) == want;
    return ok;
  };
  CHECK(all_at(LV_OPA_COVER) && !D::card_unavailable(m, *c7));
  CHECK_STR(rig.set("[7,null]"), "");  // the bar of card 7 has no value: faded
  D::Widget *bar = widget(11);
  CHECK(bar->faded && opa(bar->obj) == LV_OPA_40);
  CHECK_STR(rig.set("[8,1]"), "");
  CHECK(D::card_unavailable(m, *c7) && D::card_unavailable(m, *bar));
  // The bar is one drawn object (package E6): no part of it to fade twice.
  CHECK(all_at(LV_OPA_40) && !bar->o1 && !bar->o2 && !bar->o3);
  // The 40% is set once: repainting sets nothing.
  D::test_counts() = D::TestCounts{};
  for (uint32_t k = 0; k <= c7->children; k++) D::paint_widget(*active(), m.w[c7 - m.w + k]);
  CHECK(D::test_counts().style_sets == 0);
  CHECK_STR(rig.set("[7,80]"), "");
  CHECK(all_at(LV_OPA_40));
  // Nothing sends; the hold works.
  for (const auto &p : {std::make_pair(684, 240), std::make_pair(672, 292), std::make_pair(708, 190)}) {
    hh::tap(p.first, p.second);
    hh::advance(20);
    CHECK_MSG(sent_words().empty() && hidden(D::g.strip), std::to_string(p.first) + "," + std::to_string(p.second));
  }
  press_for(684, 240, 600);
  CHECK(tab::current_page() == 3 && sent_words().empty());
  tab::go_to(0);
  hh::advance(20);
  // Without Home Assistant a tap on the card says why.
  fake::st.network = true;
  fake::st.connected = false;
  hh::advance(20);
  hh::tap(684, 240);
  CHECK(!hidden(D::g.strip) && sent_words().empty());
  CHECK_STR(label_text(D::g.strip_label), "Home Assistant is not connected");
  fake::st.connected = true;
  CHECK(rig.ha.hello());
  hh::advance(D::STRIP_MS + 100);
  // Back: full opacity, and the card sends.
  CHECK_STR(rig.set("[8,0]"), "");
  CHECK(all_at(LV_OPA_COVER));
  fake::st.events.clear();
  hh::tap(684, 240);
  CHECK_STR(sent_words(), "7t");
  // A `go` sends nothing and still works: on a widget faded for want of a
  // value, on a card with `un` 1 and on a button in that card.
  const std::string go_widgets =
      "{\"id\":3,\"r\":[116,200,200,48],\"t\":\"button\",\"tap\":{\"go\":1},\"text\":\"Go\"},"
      "{\"id\":4,\"r\":[116,260,200,48],\"t\":\"switch\",\"tap\":{\"go\":1},\"value\":{\"s\":2}}";
  if (!CHECK(rig.up(two_pages("\"tap\":{\"go\":1},\"un\":{\"s\":1},", go_widgets, "bb"), false))) return;
  CHECK_STR(rig.set("[1,0],[2,null]"), "");
  D::Model &gm = *active()->model;
  CHECK(widget(4)->faded && !D::card_unavailable(gm, *widget(1)));
  hh::tap(216, 284);
  hh::advance(20);
  CHECK_MSG(tab::current_page() == 1, "a faded switch's go");
  tab::go_to(0);
  hh::advance(20);
  CHECK_STR(rig.set("[1,1]"), "");
  CHECK(D::card_unavailable(gm, *widget(1)));
  for (const auto &p : {std::make_pair(500, 420), std::make_pair(216, 224)}) {
    hh::tap(p.first, p.second);
    hh::advance(20);
    CHECK_MSG(tab::current_page() == 1, std::to_string(p.first) + "," + std::to_string(p.second));
    tab::go_to(0);
    hh::advance(20);
  }
  CHECK(sent_words().empty());
}

// `vis` (contract 4.3, 4.4): 0 and no value hide a widget, which is not
// drawn and not there for a touch: the press goes to what is under it, else
// to the card. Two widgets on one rectangle.
TEST(visible_by_value) {
  KitRig rig;
  if (!CHECK(rig.up())) return;
  tab::go_to(1);
  hh::advance(20);
  D::Model &m = *active()->model;
  D::Widget *arm = widget(48), *disarm = widget(49), *c46 = widget(46);
  if (!CHECK(arm && disarm && c46)) return;
  const D::CardLook *k = D::card_look(m, c46);
  const int x = 1010, y = 472;  // the button's own background, left of its text
  struct Row {
    const char *values;
    bool arm, disarm;
    const char *sent;
  };
  const Row rows[] = {{"[31,1],[32,0]", true, false, "48t"},
                      {"[31,0],[32,1]", false, true, "49t"},
                      {"[31,null],[32,null]", false, false, "46t"},
                      {"[31,0],[32,0]", false, false, "46t"},
                      {"[31,1],[32,null]", true, false, "48t"}};
  for (const Row &r : rows) {
    CHECK_STR(rig.set(r.values), "");
    CHECK_MSG(hidden(arm->obj) == !r.arm && hidden(disarm->obj) == !r.disarm, r.values);
    hh::refresh();
    const uint32_t want = r.arm || r.disarm ? k->t.card2 : k->k.background;
    CHECK_MSG(hh::pixel(x, y) == hh::rgb565(want), r.values);
    hh::tap(1124, y);
    hh::advance(20);
    CHECK_MSG(sent_words() == r.sent, std::string(r.values) + ": sent \"" + sent_words() + "\"");
    rig.settle();
  }
  // Without the key a widget always shows (the label next to them).
  CHECK(!hidden(widget(51)->obj) && widget(51)->vis.how == D::B_ABSENT);
}

// ================================================================ label, button, switch, image, heading (package E5)
//
// Contract 4.7: a label's `px` (with the digit sizes and the width check),
// `al` and `c`; a button's selected look, swatch (`c`), `shape`, `isz` and
// `ic`; a card header's `ic`; the badge and the bare image; a heading's `c`
// on the page's theme.

// The first and last column in [x, x + w) of rows [y, y + h) whose pixels
// differ from bg (the text's ink); -1 when there is none.
static void ink_columns(int x, int y, int w, int h, uint32_t bg, int *first, int *last) {
  *first = *last = -1;
  const uint16_t b = hh::rgb565(bg);
  for (int cx = x; cx < x + w; cx++)
    for (int cy = y; cy < y + h; cy++)
      if (hh::pixel(cx, cy) != b) {
        if (*first < 0) *first = cx;
        *last = cx;
        break;
      }
}

// A card with labels: `on` slot 1, `tint` slot 2, a text slot 3, a colour
// slot 4, in DISTINCT_THEME, whose tx2 stands out on a tinted fill (so FS is
// tx2 and FT is tx there, two different colours).
static const char *LABEL_WIDGETS =
    "{\"id\":2,\"r\":[116,140,300,28],\"size\":0,\"t\":\"label\",\"text\":\"small\"},"
    "{\"id\":3,\"r\":[116,170,300,28],\"size\":1,\"t\":\"label\",\"text\":\"medium\"},"
    "{\"id\":4,\"px\":15,\"r\":[116,200,300,28],\"size\":1,\"t\":\"label\",\"text\":\"px 15\"},"
    "{\"id\":5,\"px\":16,\"r\":[116,230,300,28],\"size\":0,\"t\":\"label\",\"text\":\"px 16\"},"
    "{\"id\":6,\"px\":96,\"r\":[116,262,520,120],\"size\":1,\"t\":\"label\",\"text\":{\"s\":3}},"
    "{\"id\":7,\"px\":192,\"r\":[416,140,236,118],\"size\":1,\"t\":\"label\",\"text\":\"21:05\"},"
    "{\"id\":8,\"px\":48,\"r\":[116,384,120,60],\"size\":1,\"t\":\"label\",\"text\":\"1234567890123456\"},"
    "{\"al\":1,\"id\":9,\"r\":[252,384,200,28],\"size\":1,\"t\":\"label\",\"text\":\"Mid\"},"
    "{\"al\":2,\"id\":10,\"r\":[252,414,200,28],\"size\":1,\"t\":\"label\",\"text\":\"End\"},"
    "{\"c\":{\"s\":4},\"id\":11,\"r\":[460,384,190,28],\"size\":1,\"t\":\"label\",\"text\":\"Own\"},"
    "{\"id\":12,\"px\":96,\"r\":[460,414,190,60],\"size\":1,\"t\":\"label\",\"text\":\"42\"}";

// The label size rule against the tablet's fonts, the small colour by `px`,
// FS and FT on a filled tinted card, `al`, `c`, a line higher than its box,
// and the font set only when the text needs another (cards spec B.4, rule 3).
TEST(label_looks) {
  KitRig rig;
  if (!CHECK(rig.up(two_pages("\"on\":{\"s\":1},\"tint\":{\"s\":2},", LABEL_WIDGETS, "bntn"), false))) return;
  CHECK_STR(rig.set("[1,0],[2,null],[3,\"12:34\"],[4,null]"), "");
  hh::advance(20);
  D::Model &m = *active()->model;
  D::Widget *card = widget(1);
  D::Widget *small = widget(2), *medium = widget(3), *p15 = widget(4), *p16 = widget(5), *big = widget(6);
  D::Widget *wide = widget(7), *cut = widget(8), *mid = widget(9), *end = widget(10), *own = widget(11),
            *tall = widget(12);
  if (!CHECK(card && small && medium && p15 && p16 && big && wide && cut && mid && end && own && tall)) return;
  const D::CardLook *k = D::card_look(m, card);

  // Sizes: `size` alone, `px` of a text size, a digit size, the width check.
  CHECK(font_of(small->obj) == D::text_font(14) && font_of(medium->obj) == D::text_font(18));
  CHECK(font_of(p15->obj) == D::text_font(15) && font_of(p16->obj) == D::text_font(16));
  CHECK(font_of(big->obj) == D::digits_font(96));
  // 21:05 at 192 is wider than its 236 px; at 96 it fits.
  CHECK_MSG(D::text_width(D::digits_font(192), "21:05", 5) > 236 && D::text_width(D::digits_font(96), "21:05", 5) <= 236,
            std::to_string(D::text_width(D::digits_font(192), "21:05", 5)) + " and " +
                std::to_string(D::text_width(D::digits_font(96), "21:05", 5)));
  CHECK(font_of(wide->obj) == D::digits_font(96) && !wide->cut);
  // Too wide at 48 too: 34, cut with an ellipsis.
  CHECK(font_of(cut->obj) == D::text_font(34) && cut->cut);
  const std::string ct = label_text(cut->obj);
  CHECK(ct.size() >= 3 && ct.compare(ct.size() - 3, 3, "\xE2\x80\xA6") == 0);
  // A line higher than its box starts at the box's top.
  CHECK(D::line_height(D::digits_font(96)) > 60 && font_of(tall->obj) == D::digits_font(96));
  CHECK(lv_obj_get_style_pad_top(tall->obj, LV_PART_MAIN) == 0);
  CHECK(lv_obj_get_style_pad_top(big->obj, LV_PART_MAIN) == (120 - D::line_height(D::digits_font(96))) / 2);

  // Colours off, on, and on with a tint: 14 and 15 px are small.
  auto colours_are = [&](uint32_t sm, uint32_t tx, const std::string &when) {
    CHECK_MSG(text_colour(small->obj) == sm && text_colour(p15->obj) == sm, when + " small " + hex6(text_colour(small->obj)));
    CHECK_MSG(text_colour(medium->obj) == tx && text_colour(p16->obj) == tx && text_colour(big->obj) == tx,
              when + " text " + hex6(text_colour(medium->obj)));
  };
  colours_are(k->t.tx2, k->t.tx, "off");
  CHECK_STR(rig.set("[1,1]"), "");
  CHECK(k->k.filled && k->k.FS == k->t.ontx && k->k.FT == k->t.ontx);
  colours_are(k->t.ontx, k->t.ontx, "on");
  CHECK_STR(rig.set("[2,16711680]"), "");
  CHECK(k->k.filled && k->k.FS == k->t.tx2 && k->k.FT == k->t.tx && k->k.FS != k->k.FT);
  colours_are(k->k.FS, k->k.FT, "on with a tint");
  // `c`: legible on the card's background now, with its text colour now.
  // 0x808080 stands out on the card (0x202020) and not on its fill.
  CHECK(text_colour(own->obj) == k->k.FT);
  CHECK_STR(rig.set("[2,null],[4,8421504]"), "");
  const uint32_t on_fill = D::legible(8421504, k->k.F, k->k.FT);
  CHECK(k->k.filled && text_colour(own->obj) == on_fill && own->c_has && on_fill != 8421504);
  CHECK_STR(rig.set("[1,0]"), "");
  CHECK(text_colour(own->obj) == 8421504 && D::legible(8421504, k->t.bg, k->t.tx) == 8421504);

  // `al`: left, centre and right in the box.
  CHECK(lv_obj_get_style_text_align(mid->obj, LV_PART_MAIN) == LV_TEXT_ALIGN_CENTER);
  CHECK(lv_obj_get_style_text_align(end->obj, LV_PART_MAIN) == LV_TEXT_ALIGN_RIGHT);
  CHECK(lv_obj_get_style_text_align(medium->obj, LV_PART_MAIN) != LV_TEXT_ALIGN_CENTER &&
        lv_obj_get_style_text_align(medium->obj, LV_PART_MAIN) != LV_TEXT_ALIGN_RIGHT);
  lv_obj_invalidate(lv_screen_active());
  hh::refresh();
  int f0, l0, f1, l1, f2, l2;
  ink_columns(116, 170, 300, 28, k->t.bg, &f0, &l0);
  ink_columns(252, 384, 200, 28, k->t.bg, &f1, &l1);
  ink_columns(252, 414, 200, 28, k->t.bg, &f2, &l2);
  CHECK_MSG(f0 >= 116 && f0 <= 120, std::to_string(f0));
  CHECK_MSG(f1 > 252 + 60 && l1 < 452 - 60 && abs((f1 - 252) - (452 - 1 - l1)) <= 3,
            std::to_string(f1) + ".." + std::to_string(l1));
  CHECK_MSG(f2 > 252 + 120 && l2 >= 452 - 4 && l2 <= 451, std::to_string(f2) + ".." + std::to_string(l2));

  // The font follows the text: digits, then a word (34), then digits again.
  // It is set only when it changes, so a new time of the same size lays
  // nothing out.
  hh::refresh();
  CHECK(!layout_pending());
  D::test_counts() = D::TestCounts{};
  CHECK_STR(rig.set("[3,\"12:35\"]"), "");
  CHECK(font_of(big->obj) == D::digits_font(96) && label_text(big->obj) == "12:35" && !layout_pending());
  CHECK_MSG(D::test_counts().style_sets == 1, std::to_string(D::test_counts().style_sets));  // the text
  CHECK_STR(rig.set("[3,\"Kitchen\"]"), "");
  CHECK(font_of(big->obj) == D::text_font(34));
  CHECK(lv_obj_get_style_pad_top(big->obj, LV_PART_MAIN) == (120 - D::line_height(D::text_font(34))) / 2);
  hh::refresh();
  CHECK_STR(rig.set("[3,\"21.5\xC2\xB0\"]"), "");
  CHECK(font_of(big->obj) == D::digits_font(96));
  CHECK_STR(rig.set("[3,\"\xE2\x88\x92" "5\"]"), "");  // U+2212 is no digit glyph
  CHECK(font_of(big->obj) == D::text_font(34));
  hh::refresh();
  // A repaint with unchanged state sets nothing and works out nothing.
  D::test_counts() = D::TestCounts{};
  D::test_contrasts() = 0;
  for (uint32_t i = 0; i < m.nw; i++) D::paint_widget(*active(), m.w[i]);
  CHECK(D::test_counts().style_sets == 0 && D::test_contrasts() == 0 && !layout_pending());
}

// A heading's `c` and its text colour come from the theme of its page: the
// kit's page 1 has its own, and a heading with `c` on such a page is legible
// on that page's wall.
TEST(heading_colours) {
  KitRig rig;
  if (!CHECK(rig.up())) return;
  D::Model &m = *active()->model;
  D::Widget *h1 = widget(1), *h30 = widget(30);
  if (!CHECK(h1 && h30)) return;
  const D::Theme &t0 = D::page_theme(m, 0), &t1 = D::page_theme(m, 1);
  CHECK(t1.c[D::C_TX] != m.theme.c[D::C_TX] && t1.c[D::C_WALL] != m.theme.c[D::C_WALL]);
  const uint32_t want1 = D::legible(5030794, t0.c[D::C_WALL], t0.c[D::C_TX]);
  CHECK(text_colour(h1->o1) == want1 && text_colour(h1->o2) == want1);
  CHECK(text_colour(h30->o2) == t1.c[D::C_TX]);
  // `c` on a page with its own theme.
  std::string themed = two_pages("", LAMP_WIDGETS, "bbn");
  const std::string from = "{\"title\":\"Q\",\"w\":[{\"id\":90,";
  const size_t at = themed.find(from);
  if (!CHECK(at != std::string::npos)) return;
  themed.replace(at, from.size(),
                 "{\"theme\":{\"tx\":1382429,\"wall\":15659251},\"title\":\"Q\",\"w\":[{\"c\":{\"s\":3},\"icon\":983772,"
                 "\"id\":90,");
  if (!CHECK(rig.up(themed, false))) return;
  D::Model &m2 = *active()->model;
  D::Widget *h = widget(90);
  if (!CHECK(h && h->page == 1)) return;
  CHECK_STR(rig.set("[3,16766474]"), "");
  hh::advance(20);
  const uint32_t own = D::legible(16766474, 15659251, 1382429);
  CHECK(own != D::legible(16766474, m2.theme.c[D::C_WALL], m2.theme.c[D::C_TX]));
  CHECK_MSG(text_colour(h->o2) == own && text_colour(h->o1) == own, hex6(text_colour(h->o2)));
  CHECK_STR(rig.set("[3,null]"), "");
  hh::advance(20);
  CHECK(text_colour(h->o2) == 1382429 && text_colour(h->o1) == 1382429);
}

// Buttons of every new look, on a card whose `on` shares slot 2 with a
// button, the swatch and a switch: slots 1 b, 2 b, 3 n (the swatch's
// colour), 4 b (the bare button's value), 5 i (`ic`).
static const char *BUTTON_WIDGETS =
    "{\"id\":2,\"r\":[116,140,160,48],\"t\":\"button\",\"tap\":{\"p\":0},\"text\":\"Sel\",\"value\":{\"s\":2}},"
    "{\"c\":{\"s\":3},\"id\":3,\"r\":[292,140,160,48],\"t\":\"button\",\"tap\":{\"p\":0},\"value\":{\"s\":2}},"
    "{\"icon\":984101,\"id\":4,\"isz\":1,\"r\":[468,140,120,80],\"shape\":1,\"t\":\"button\",\"tap\":{\"p\":1},"
    "\"text\":\"No\"},"
    "{\"icon\":983512,\"id\":5,\"r\":[116,240,160,48],\"shape\":2,\"t\":\"button\",\"tap\":{\"p\":0},"
    "\"value\":{\"s\":4}},"
    "{\"ic\":{\"s\":5},\"icon\":983568,\"id\":6,\"r\":[292,240,160,48],\"t\":\"button\",\"tap\":{\"p\":1},"
    "\"text\":\"Fan\"},"
    "{\"id\":7,\"r\":[468,240,120,48],\"t\":\"switch\",\"tap\":{\"p\":0},\"value\":{\"s\":2}}";

static std::string glyph(uint32_t cp) {
  char b[5];
  D::icon_utf8(cp, b);
  return b;
}

// Contract 7.4 for a button with a bound `value` (policy 0: the other state
// at once, back on an ack that is not ok or after 4 s without one, kept
// after an ok ack until the value comes), with the card's `on` and a switch
// of the same slot; a card whose `on` has no value. And contract 4.7: the
// selected look (accf, acct, pressed accf_pr, the 2 px border in acct when
// accf does not stand out on the card's background now), the swatch, the
// round and the bare shape, `isz` and `ic` on a button and a card header.
TEST(button_selected_feedback) {
  KitRig rig;
  if (!CHECK(rig.up(two_pages("\"ic\":{\"s\":5},\"on\":{\"s\":2},\"tap\":{\"p\":0},", BUTTON_WIDGETS, "bbnbi"), false)))
    return;
  CHECK_STR(rig.set("[1,0],[2,0],[3,16728128],[4,0],[5,0]"), "");
  hh::advance(20);
  D::Model &m = *active()->model;
  D::Widget *card = widget(1), *sel = widget(2), *sw = widget(3), *round = widget(4), *bare = widget(5),
            *fan = widget(6), *swi = widget(7);
  if (!CHECK(card && sel && sw && round && bare && fan && swi)) return;
  const D::CardLook *k = D::card_look(m, card);
  const int sy = 164;  // the middle row of the first row of buttons
  auto looks = [&](bool on, const std::string &when) {
    hh::refresh();
    bool ok = (k->k.filled == 1) == on && D::button_selected(m, *sel) == on && D::button_selected(m, *sw) == on &&
              knob_at(swi) == (on ? 32 : 4);
    ok = ok && bg_colour(sel->obj) == (on ? k->k.AF : k->t.card2) && text_colour(sel->o2) == (on ? k->k.AT : k->t.tx);
    ok = ok && hh::pixel(124, sy) == hh::rgb565(on ? k->k.AF : k->t.card2);
    // The swatch: background c, content readable(c); selected, a 3 px
    // border inside in the card's text colour now.
    ok = ok && bg_colour(sw->obj) == 16728128 && sw->ring == (on ? 3 : 0);
    ok = ok && hh::pixel(293, sy) == hh::rgb565(on ? k->k.text : 16728128) &&
         hh::pixel(294, sy) == hh::rgb565(on ? k->k.text : 16728128) && hh::pixel(296, sy) == hh::rgb565(16728128);
    CHECK_MSG(ok, when);
  };
  looks(false, "at rest");
  CHECK(sw->c_has && sw->c_now == 16728128 && sw->c_on == D::readable(16728128));
  CHECK_STR(rig.set("[2,1]"), "");
  looks(true, "value 1");
  CHECK(k->k.text == k->t.ontx && sel->ring == 0);  // accf stands out: no border
  CHECK_STR(rig.set("[2,0]"), "");
  looks(false, "value 0");

  // The button's tap: at once on the button, the swatch, the card and the
  // switch of its slot. Not ok: back at once.
  hh::tap(196, sy);
  CHECK_STR(sent_words(), "2t");
  looks(true, "tapped");
  rig.ha.ack(last_input_counter(), false, "failed");
  looks(false, "ack not ok");
  // No ack: back after 4 s.
  rig.settle();
  hh::tap(196, sy);
  hh::advance(3800);
  looks(true, "3.8 s without an ack");
  hh::advance(300);
  looks(false, "4.1 s without an ack");
  // An ok ack: the state stays until the value comes.
  rig.settle();
  hh::tap(196, sy);
  rig.ha.ack(last_input_counter(), true, "");
  hh::advance(1000);
  CHECK(sel->ovr == 2);
  looks(true, "ok ack");
  CHECK_STR(rig.set("[2,1]"), "");
  CHECK(sel->ovr == 0);
  looks(true, "the value came");
  // The swatch's tap flips the same slot.
  rig.settle();
  hh::tap(372, sy);
  CHECK_STR(sent_words(), "3t");
  looks(false, "the swatch tapped");
  hh::advance(4100);
  looks(true, "the swatch's flip ended");
  // A card whose `on` has no value: the button's value counts as 0, so its
  // tap shows 1 on the button and on the card, and goes back after 4 s.
  rig.settle();
  CHECK_STR(rig.set("[2,null]"), "");
  looks(false, "no value");
  hh::tap(196, sy);
  CHECK_STR(sent_words(), "2t");
  looks(true, "no value, tapped");
  hh::advance(4100);
  looks(false, "no value, 4.1 s");
  rig.settle();
  CHECK_STR(rig.set("[2,0]"), "");

  // Pressed: card2_pr (on: onbg_pr); selected accf_pr; a swatch
  // mix(readable(c), c, 85).
  auto pressed_bg = [&](D::Widget *w, int x, int y) {
    finger(x, y, true);
    hh::advance(20);
    hh::refresh();
    const uint32_t got = bg_colour(w->obj);
    const uint16_t pix = hh::pixel(x, y);
    finger(x, y, false);
    hh::advance(30);
    rig.settle();
    rig.set("[2,0],[4,0]");
    hh::advance(20);
    return std::make_pair(got, pix);
  };
  auto p = pressed_bg(sel, 124, sy);
  CHECK(p.first == k->t.card2_pr && p.second == hh::rgb565(k->t.card2_pr));
  CHECK_STR(rig.set("[2,1]"), "");
  p = pressed_bg(sel, 124, sy);  // the tap flips it to 0 at the release; pressed while 1
  CHECK(p.first == k->k.AP && p.second == hh::rgb565(k->k.AP));
  p = pressed_bg(sw, 300, sy);
  CHECK(p.first == D::mix(D::readable(16728128), 16728128, 85) && p.first != 16728128);

  // Bare: no background at rest (the card shows through), pressed card2_pr,
  // on: onbg_pr; selected fills accf.
  hh::refresh();
  CHECK(lv_obj_get_style_bg_opa(bare->obj, LV_PART_MAIN) == LV_OPA_TRANSP);
  CHECK(hh::pixel(120, 244) == hh::rgb565(k->t.bg));
  p = pressed_bg(bare, 120, 244);
  CHECK(p.first == k->t.card2_pr && p.second == hh::rgb565(k->t.card2_pr));
  CHECK_STR(rig.set("[2,1]"), "");  // the card is filled
  hh::refresh();
  CHECK(hh::pixel(120, 244) == hh::rgb565(k->k.F));
  finger(120, 244, true);
  hh::advance(20);
  hh::refresh();
  CHECK(hh::pixel(120, 244) == hh::rgb565(k->k.FP) && k->k.FP == k->t.onbg_pr);
  finger(120, 244, false);
  hh::advance(30);
  rig.settle();
  CHECK_STR(rig.set("[2,0],[4,1]"), "");
  hh::refresh();
  CHECK(D::button_selected(m, *bare) && hh::pixel(120, 244) == hh::rgb565(k->k.AF));
  CHECK(text_colour(bare->o1) == k->k.AT);
  CHECK_STR(rig.set("[4,0]"), "");

  // Round: the circle of side min(w, h), centred, no text; `isz` 1 gives the
  // largest icon not above min(w, h) / 2 (28 for 80).
  CHECK(obj_at(round->obj, 488, 140, 80, 80) && round->o2 == nullptr);
  CHECK(lv_obj_get_style_radius(round->obj, LV_PART_MAIN) == LV_RADIUS_CIRCLE);
  CHECK(font_of(round->o1) == D::icon_font(28) && label_text(round->o1) == glyph(984101));
  hh::refresh();
  CHECK(hh::pixel(490, 142) == hh::rgb565(k->t.bg) && hh::pixel(492, 180) == hh::rgb565(k->t.card2));
  // A press beside the circle, inside the rectangle, is the card's.
  hh::tap(472, 210);
  CHECK_STR(sent_words(), "1t");
  rig.settle();
  CHECK_STR(rig.set("[2,0]"), "");
  hh::tap(528, 180);
  CHECK_STR(sent_words(), "4t");
  rig.settle();

  // `ic`: the icon while it has a value above 0, on a button and in the
  // card's header; 0 gives `icon` back.
  CHECK(label_text(fan->o1) == glyph(983568) && label_text(card->o1) == glyph(983861));
  CHECK_STR(rig.set("[5,985117]"), "");
  CHECK(label_text(fan->o1) == glyph(985117) && label_text(card->o1) == glyph(985117));
  CHECK_STR(rig.set("[5,0]"), "");
  CHECK(label_text(fan->o1) == glyph(983568) && label_text(card->o1) == glyph(983861));
  CHECK_STR(rig.set("[5,null]"), "");
  CHECK(label_text(fan->o1) == glyph(983568) && label_text(card->o1) == glyph(983861));
  // Without `isz` a button's icon is 24.
  CHECK(font_of(fan->o1) == D::icon_font(24));

  // A repaint with unchanged state sets nothing and works out nothing.
  CHECK_STR(rig.set("[2,1],[4,1]"), "");
  D::test_counts() = D::TestCounts{};
  D::test_contrasts() = 0;
  for (uint32_t i = 0; i < m.nw; i++) D::paint_widget(*active(), m.w[i]);
  CHECK(D::test_counts().style_sets == 0 && D::test_contrasts() == 0);

  // accf that does not stand out on the card (contrast below 1.3): a 2 px
  // border in acct inside a selected button, only while the card's
  // background now is the one it does not stand out on.
  if (!CHECK(rig.up(two_pages("\"on\":{\"s\":1},", BUTTON_WIDGETS, "bbnbi", "{\"accf\":2302755}"), false))) return;
  CHECK_STR(rig.set("[1,0],[2,1],[3,null],[4,0],[5,0]"), "");
  hh::advance(20);
  D::Model &m2 = *active()->model;
  sel = widget(2);
  card = widget(1);
  const D::CardLook *k2 = D::card_look(m2, card);
  CHECK(k2->k.AF == 2302755 && D::contrast(k2->k.AF, k2->t.bg) < 1.3 && k2->k.sel_border);
  hh::refresh();
  CHECK(sel->ring == 2 && hh::pixel(117, sy) == hh::rgb565(k2->k.AT) && hh::pixel(119, sy) == hh::rgb565(k2->k.AF));
  // The swatch slot without a value: a plain selected button, with the border.
  CHECK(!widget(3)->c_has && widget(3)->ring == 2);
  CHECK_STR(rig.set("[1,1]"), "");
  CHECK(D::contrast(k2->k.AF, k2->k.background) >= 1.3 && !k2->k.sel_border);
  hh::refresh();
  CHECK(sel->ring == 0 && hh::pixel(117, sy) == hh::rgb565(k2->k.AF));
  CHECK_STR(rig.set("[1,0],[2,0]"), "");
  hh::refresh();
  CHECK(sel->ring == 0 && hh::pixel(117, sy) == hh::rgb565(k2->t.card2));
}

// The image's looks (contract 4.7, "Image, badge and bare"): the badge is
// the centred circle with a wash of K and the largest icon not above 0.6 of
// it; the bare image has no box and the largest icon not above min(w, h);
// K by state (plain, filled, lit 1); pressed card2_pr (on: onbg_pr) with
// radius r2; the pulse without a box; the placeholder without a value; a
// hold beside the circle is the card's.
static const char *IMAGE_WIDGETS =
    "{\"id\":2,\"look\":1,\"r\":[116,140,120,80],\"t\":\"image\",\"tap\":{\"p\":0},\"value\":{\"s\":2}},"
    "{\"c\":{\"s\":3},\"id\":3,\"look\":2,\"r\":[252,140,120,80],\"t\":\"image\",\"tap\":{\"p\":0},\"value\":984463},"
    "{\"id\":4,\"r\":[388,140,64,64],\"t\":\"image\",\"value\":984463},"
    "{\"hold\":{\"go\":1},\"id\":5,\"look\":1,\"r\":[116,240,120,80],\"t\":\"image\",\"value\":984463}";

TEST(image_looks) {
  for (int lit : {0, 1}) {
    KitRig rig;
    const std::string keys = "\"lit\":" + std::to_string(lit) + ",\"on\":{\"s\":1},";
    if (!CHECK(rig.up(two_pages(keys, IMAGE_WIDGETS, "bin"), false))) return;
    CHECK_STR(rig.set("[1,0],[2,984211],[3,null]"), "");
    hh::advance(20);
    D::Model &m = *active()->model;
    D::Widget *card = widget(1), *badge = widget(2), *bare = widget(3), *box = widget(4), *held = widget(5);
    if (!CHECK(card && badge && bare && box && held)) return;
    const D::CardLook *k = D::card_look(m, card);
    const std::string at = "lit " + std::to_string(lit);
    // Places and icon sizes: the badge 80 (0.6 * 80 = 48), bare 72, the box 28.
    CHECK_MSG(obj_at(badge->obj, 136, 140, 80, 80) && obj_at(bare->obj, 252, 140, 120, 80), at);
    CHECK(lv_obj_get_style_radius(badge->obj, LV_PART_MAIN) == LV_RADIUS_CIRCLE);
    CHECK(font_of(badge->o1) == D::icon_font(48) && font_of(bare->o1) == D::icon_font(72) &&
          font_of(box->o1) == D::icon_font(28));
    CHECK(lv_obj_get_style_bg_opa(bare->obj, LV_PART_MAIN) == LV_OPA_TRANSP);
    // Plain: circle card2, icon tx2; bare icon tx2 legible on the card.
    auto plain_now = [&] {
      return bg_colour(badge->obj) == k->t.card2 && text_colour(badge->o1) == k->t.tx2 &&
             text_colour(bare->o1) == D::legible(k->t.tx2, k->t.bg, k->t.tx) && text_colour(box->o1) == k->t.acc2;
    };
    CHECK_MSG(plain_now(), at);
    hh::refresh();
    CHECK(hh::pixel(140, 180) == hh::rgb565(k->t.card2) && hh::pixel(137, 141) == hh::rgb565(k->t.bg));
    CHECK(hh::pixel(254, 142) == hh::rgb565(k->t.bg));
    // On: K is ion while filled (lit 0), acc with lit 1.
    CHECK_STR(rig.set("[1,1]"), "");
    const uint32_t K = lit == 0 ? k->k.FI : k->k.A;
    const uint32_t wash = D::mix(K, k->k.background, 80);
    CHECK_MSG(bg_colour(badge->obj) == wash && text_colour(badge->o1) == D::legible(K, wash, k->k.text), at);
    CHECK(text_colour(bare->o1) == D::legible(K, k->k.background, k->k.text));
    CHECK(wash != k->t.card2);
    hh::refresh();
    CHECK(hh::pixel(140, 180) == hh::rgb565(wash));
    // `c` wins over the state.
    CHECK_STR(rig.set("[3,16729402]"), "");
    CHECK(text_colour(bare->o1) == D::legible(16729402, k->k.background, k->k.text));
    // Pressed: card2_pr, on: onbg_pr, radius r2.
    finger(140, 180, true);
    hh::advance(20);
    CHECK(bg_colour(badge->obj) == (k->k.filled ? k->k.FP : k->t.card2_pr) &&
          lv_obj_get_style_radius(badge->obj, LV_PART_MAIN) == k->t.r2);
    finger(140, 180, false);
    hh::advance(30);
    rig.settle();
    CHECK(lv_obj_get_style_radius(badge->obj, LV_PART_MAIN) == LV_RADIUS_CIRCLE);
    finger(256, 144, true);
    hh::advance(20);
    hh::refresh();
    CHECK(lv_obj_get_style_bg_opa(bare->obj, LV_PART_MAIN) == LV_OPA_COVER &&
          hh::pixel(256, 150) == hh::rgb565(k->k.filled ? k->k.FP : k->t.card2_pr));
    finger(256, 144, false);
    hh::advance(30);
    rig.settle();
    CHECK(lv_obj_get_style_bg_opa(bare->obj, LV_PART_MAIN) == LV_OPA_TRANSP);
    // No value: the placeholder, faded.
    CHECK_STR(rig.set("[2,null]"), "");
    CHECK(label_text(badge->o1) == glyph(D::ICON_PLACEHOLDER) && lv_obj_get_style_opa(badge->obj, LV_PART_MAIN) == LV_OPA_40);
    CHECK_STR(rig.set("[2,984211]"), "");
    CHECK(label_text(badge->o1) == glyph(984211));
    // The widget's own hold in its circle; beside it the card's, which has
    // none. (No hold opens while the erase watch of the first 10 s runs.)
    CHECK(advance_until([] { return D::g.erase_timer == nullptr; }, D::ERASE_ARM_MS + 1000));
    press_for(176, 280, 700);
    CHECK(tab::current_page() == 1);
    tab::go_to(0);
    hh::advance(20);
    press_for(118, 242, 700);
    CHECK(tab::current_page() == 0 && sent_words().empty());
    // A repaint with unchanged state sets nothing and works out nothing.
    D::test_counts() = D::TestCounts{};
    D::test_contrasts() = 0;
    for (uint32_t i = 0; i < m.nw; i++) D::paint_widget(*active(), m.w[i]);
    CHECK(D::test_counts().style_sets == 0 && D::test_contrasts() == 0);
  }
  // The pulse: no box or badge, the icon acct.
  KitRig rig;
  if (!CHECK(rig.up(two_pages("\"tap\":{\"p\":1},", IMAGE_WIDGETS, "bin"), false))) return;
  CHECK_STR(rig.set("[1,0],[2,984211],[3,null]"), "");
  hh::advance(20);
  D::Widget *card = widget(1), *badge = widget(2), *box = widget(4);
  const D::CardLook *k = D::card_look(*active()->model, card);
  hh::tap(500, 420);
  CHECK(card->fb == 2);
  CHECK(lv_obj_get_style_bg_opa(badge->obj, LV_PART_MAIN) == LV_OPA_TRANSP && text_colour(badge->o1) == k->k.AT);
  CHECK(lv_obj_get_style_bg_opa(box->obj, LV_PART_MAIN) == LV_OPA_TRANSP && text_colour(box->o1) == k->k.AT);
  hh::advance(320);
  CHECK(card->fb == 0 && bg_colour(badge->obj) == k->t.card2 && lv_obj_get_style_bg_opa(badge->obj, LV_PART_MAIN) == LV_OPA_COVER);
}

// A switch's `c` (contract 4.7): at value 1 the track is legible(c, the
// card's background now, its text colour now) and the knob readable of it;
// at 0, and without a colour, the look of version 1.
TEST(switch_colour) {
  KitRig rig;
  if (!CHECK(rig.up())) return;
  D::Model &m = *active()->model;
  D::Widget *sw = widget(28), *card = widget(22);
  if (!CHECK(sw && card)) return;
  const D::CardLook *k = D::card_look(m, card);
  CHECK_STR(rig.set("[21,1],[22,5030794]"), "");
  const uint32_t track = D::legible(5030794, k->k.background, k->k.text);
  CHECK(bg_colour(sw->o1) == track && bg_colour(sw->o2) == D::readable(track));
  hh::refresh();
  CHECK(hh::pixel(sw->r[0] + 6, centre_y(sw->r)) == hh::rgb565(track));
  CHECK_STR(rig.set("[22,16766474]"), "");
  const uint32_t track2 = D::legible(16766474, k->k.background, k->k.text);
  CHECK(track2 != track && bg_colour(sw->o1) == track2 && bg_colour(sw->o2) == D::readable(track2));
  CHECK_STR(rig.set("[22,null]"), "");
  CHECK(bg_colour(sw->o1) == k->k.A && bg_colour(sw->o2) == k->k.AT);
  CHECK_STR(rig.set("[21,0],[22,5030794]"), "");
  CHECK(bg_colour(sw->o1) == k->t.card2 && bg_colour(sw->o2) == k->t.tx2);
}

// ================================================================ the slider (package E6)
//
// Contract 4.7, "Slider" and the (2) rows after it; cards spec B.4 and B.5.
// The geometry itself is checked against maths.json in maths_fixture.

// Sliders of every look and direction on card 1 of two_pages (slots "nnn"):
// knobs from the left, the bottom and the top; a bar from the top; a knob
// with a track of 2 colours and an upright bar with one of 8; a knob whose
// track of 1 colour is no track and a bar whose 9 colours keep 8; a knob and
// a bar with `nv`; and two pairs of the same track, one with `nv` (drawn at
// full opacity without a value) and one without (faded).
static const char *SLIDER_WIDGETS =
    "{\"change\":{\"p\":0},\"id\":2,\"max\":100,\"min\":0,\"r\":[116,140,200,40],\"step\":1,\"t\":\"slider\","
    "\"value\":{\"s\":1}},"
    "{\"change\":{\"p\":0},\"dir\":1,\"id\":3,\"max\":100,\"min\":0,\"r\":[340,140,40,200],\"step\":1,"
    "\"t\":\"slider\",\"value\":{\"s\":1}},"
    "{\"change\":{\"p\":0},\"dir\":2,\"id\":4,\"max\":100,\"min\":0,\"r\":[400,140,40,200],\"step\":1,"
    "\"t\":\"slider\",\"value\":{\"s\":1}},"
    "{\"change\":{\"p\":0},\"dir\":2,\"icon\":983263,\"id\":5,\"look\":1,\"max\":100,\"min\":0,\"r\":[460,140,60,200],\"step\":1,"
    "\"t\":\"slider\",\"value\":{\"s\":1}},"
    "{\"change\":{\"p\":0},\"id\":6,\"max\":100,\"min\":0,\"r\":[116,200,200,40],\"step\":1,\"t\":\"slider\","
    "\"track\":[16711680,255],\"value\":{\"s\":2}},"
    "{\"change\":{\"p\":0},\"dir\":1,\"id\":7,\"look\":1,\"max\":100,\"min\":0,\"r\":[540,140,60,200],\"step\":1,"
    "\"t\":\"slider\",\"track\":[16711680,65280,255,16776960,65535,16711935,16777215,0],\"value\":{\"s\":2}},"
    "{\"id\":8,\"max\":100,\"min\":0,\"r\":[116,260,200,40],\"step\":1,\"t\":\"slider\",\"track\":[65280],"
    "\"value\":{\"s\":2}},"
    "{\"id\":9,\"look\":1,\"max\":100,\"min\":0,\"r\":[340,360,300,40],\"step\":1,\"t\":\"slider\","
    "\"track\":[1,2,3,4,5,6,7,8,9],\"value\":{\"s\":2}},"
    "{\"change\":{\"p\":0},\"id\":10,\"max\":100,\"min\":0,\"nv\":1,\"r\":[116,320,200,40],\"step\":1,"
    "\"t\":\"slider\",\"value\":{\"s\":3}},"
    "{\"change\":{\"p\":2},\"id\":11,\"look\":1,\"max\":100,\"min\":0,\"nv\":1,\"r\":[116,380,200,40],\"step\":1,"
    "\"t\":\"slider\",\"value\":{\"s\":3}},"
    "{\"id\":12,\"max\":100,\"min\":0,\"nv\":1,\"r\":[116,430,200,40],\"step\":1,\"t\":\"slider\","
    "\"track\":[16711680,16776960,65280,65535,255,16711935,16711680],\"value\":{\"s\":3}},"
    "{\"id\":13,\"max\":100,\"min\":0,\"r\":[340,430,200,40],\"step\":1,\"t\":\"slider\","
    "\"track\":[16711680,16776960,65280,65535,255,16711935,16711680],\"value\":{\"s\":3}},"
    "{\"id\":14,\"look\":1,\"max\":100,\"min\":0,\"nv\":1,\"r\":[116,472,200,10],\"step\":1,\"t\":\"slider\","
    "\"track\":[16746770,16775677,13623551],\"value\":{\"s\":3}},"
    "{\"id\":15,\"look\":1,\"max\":100,\"min\":0,\"r\":[340,472,200,10],\"step\":1,\"t\":\"slider\","
    "\"track\":[16746770,16775677,13623551],\"value\":{\"s\":3}}";

// The value field of the last input sent, or "" for none.
static std::string last_input_value() {
  const auto ev = fake::events("tab1_input");
  if (ev.empty()) return "";
  const auto f = fields_of(ev.back().d);
  return f.size() == 9 ? f[6] + "=" + f[8] : "";
}

// The channels of an RGB565 pixel, 0 to 31 (green halved to 5 bits' scale).
static void channels(uint16_t p, double c[3]) {
  c[0] = p >> 11;
  c[1] = ((p >> 5) & 63) / 2.0;
  c[2] = p & 31;
}

// How far a pixel is from a colour, the largest channel difference.
static double colour_gap(uint16_t p, uint32_t colour) {
  double a[3], b[3];
  channels(p, a);
  channels(hh::rgb565(colour), b);
  double d = 0;
  for (int i = 0; i < 3; i++) d = fmax(d, fabs(a[i] - b[i]));
  return d;
}

// Value from the finger for the three directions and both looks, with and
// without a track; tracks of 2 and 8 colours as drawn, 1 colour (no track)
// and 9 (the first 8); `nv`; a faded track as one picture; a drag on a bar
// ended by a forced swap; the colours a bar is drawn in.
TEST(slider_looks) {
  KitRig rig;
  if (!CHECK(rig.up(two_pages("", SLIDER_WIDGETS, "nnn"), false))) return;
  CHECK_STR(rig.set("[1,40],[2,50],[3,null]"), "");
  hh::advance(50);
  D::Model &m = *active()->model;
  D::Widget *w[16] = {};
  for (int i = 2; i <= 15; i++) {
    w[i] = widget((uint32_t) i);
    if (!CHECK_MSG(w[i] && w[i]->obj, std::to_string(i))) return;
  }
  // One object for a bar and for any slider with a track; the four of
  // version 1 for a knob, also one whose track of 1 colour is no track.
  for (int i : {2, 3, 4, 8, 10}) CHECK_MSG(D::Loader::objects_of(*w[i]) == 4 && w[i]->o1 && w[i]->o3, std::to_string(i));
  for (int i : {5, 6, 7, 9, 11, 12, 13, 14, 15})
    CHECK_MSG(D::Loader::objects_of(*w[i]) == 1 && !w[i]->o1 && !w[i]->o2 && !w[i]->o3 &&
                  lv_obj_get_child_count(w[i]->obj) == 0,
              std::to_string(i));
  CHECK(w[8]->track_n == 0 && !w[8]->track && w[6]->track_n == 2 && w[7]->track_n == 8);
  CHECK(w[9]->track_n == 8 && w[9]->track[7] == 8);

  // Value from the finger: along the axis of each direction, from x0 to x1
  // with a track (x0 = R: 7 on the knob look, min(r2, S div 2) on a bar).
  struct Press {
    int id, x, y;
    const char *want;
  } presses[] = {
      {2, 116 + 150, 160, "c=75"},             // from the left: 150 of 200
      {3, 360, 140 + 200 - 50, "c=25"},        // from the bottom: 50 of 200
      {4, 420, 140 + 50, "c=25"},              // from the top: 50 of 200
      {5, 490, 140 + 150, "c=75"},             // a bar from the top
      {6, 116 + 7 + 93, 220, "c=50"},          // (100 - 7) / 186
      {7, 570, 140 + 200 - 55, "c=25"},        // (55 - 10) / 180, from the bottom
      {6, 116 + 3, 220, "c=0"},                // on the start cap: 0
      {6, 116 + 197, 220, "c=100"},            // on the end cap: 100
      {10, 116 + 50, 340, "c=25"},             // nv: no value, and it takes input
      {11, 116 + 150, 400, "c=75"},
  };
  for (const Press &p : presses) {
    fake::st.events.clear();
    hh::tap(p.x, p.y);
    hh::advance(20);
    CHECK_MSG(last_input_value() == p.want, std::to_string(p.id) + ": " + last_input_value() + ", want " + p.want);
    CHECK_MSG(fake::events("tab1_input").size() == 1 && tab::current_page() == 0, std::to_string(p.id));
    // Policy 2 (bar 11): the waiting outline on the drawn object until the answer.
    if (p.id == 11) CHECK(w[11]->fb == 1 && lv_obj_get_style_outline_width(w[11]->obj, LV_PART_MAIN) == 2);
    rig.settle();
    CHECK_STR(rig.set("[1,40],[2,50],[3,null]"), "");
  }
  hh::advance(D::PROGRESS_TIMEOUT_MS);  // the value bar 11 sent is shown no more
  // A drag that starts on an upright slider moves it and is never a swipe.
  fake::st.events.clear();
  hh::drag(360, 300, 300, 180, 200);
  hh::advance(20);
  CHECK_STR(last_input_value(), "c=80");
  CHECK(tab::current_page() == 0);
  rig.settle();
  CHECK_STR(rig.set("[1,40],[2,50],[3,null]"), "");
  hh::advance(20);

  // The knob from the bottom: the fill grows upwards to the knob.
  lv_obj_update_layout(w[3]->obj);
  CHECK(lv_obj_get_height(w[3]->o2) == 80 && lv_obj_get_y(w[3]->o2) == 120 && lv_obj_get_y(w[3]->o3) == 200 - 80 - 11);
  CHECK(lv_obj_get_height(w[4]->o2) == 80 && lv_obj_get_y(w[4]->o2) == 0 && lv_obj_get_y(w[4]->o3) == 80 - 11);
  CHECK(lv_obj_get_x(w[3]->o1) == 13 && lv_obj_get_width(w[3]->o1) == 14);

  // Tracks as drawn: each cap in its colour, the pieces fading from the
  // start, the mark white on the place of the value.
  hh::refresh();
  CHECK(colour_gap(hh::pixel(118, 220), 0xFF0000) <= 1 && colour_gap(hh::pixel(116 + 198, 220), 0x0000FF) <= 1);
  CHECK(colour_gap(hh::pixel(116 + 100, 220), 0xFFFFFF) <= 1);  // the mark at 7 + 0.5 * 186 = 100
  CHECK(colour_gap(hh::pixel(116 + 40, 220), 0xFF0000) > 4 && colour_gap(hh::pixel(116 + 40, 220), 0x0000FF) > 4);
  // From the bottom, 8 colours: the bottom cap in the first, the top cap in
  // the last; piece 0 (10 to 35) red at its start and green at its end.
  const int bottom = 140 + 200;
  CHECK(colour_gap(hh::pixel(570, bottom - 3), 0xFF0000) <= 1 && colour_gap(hh::pixel(570, 140 + 3), 0x000000) <= 1);
  CHECK(colour_gap(hh::pixel(552, bottom - 11), 0xFF0000) < colour_gap(hh::pixel(552, bottom - 11), 0x00FF00));
  CHECK(colour_gap(hh::pixel(552, bottom - 34), 0x00FF00) < colour_gap(hh::pixel(552, bottom - 34), 0xFF0000));
  // The mark of the upright bar: 12 along, S + 4 across, at 10 + 0.5 * 180 = 100.
  CHECK(colour_gap(hh::pixel(570, bottom - 100), 0xFFFFFF) <= 1);
  CHECK(colour_gap(hh::pixel(540 - 1, bottom - 100), 0x15181D) <= 1);  // 2 px past the side, its border

  // `nv`: without a value at full opacity, the track only; without `nv` faded.
  auto opa = [](D::Widget *x) { return (int) lv_obj_get_style_opa(x->obj, LV_PART_MAIN); };
  CHECK(opa(w[10]) == LV_OPA_COVER && !w[10]->faded && hidden(w[10]->o2) && hidden(w[10]->o3));
  CHECK(opa(w[11]) == LV_OPA_COVER && !w[11]->faded && w[11]->sl_pos == 0);
  CHECK(opa(w[12]) == LV_OPA_COVER && w[12]->sl_pos == -1 && opa(w[13]) == LV_OPA_40 && w[13]->faded);
  const D::CardLook *k = D::card_look(m, widget(1));
  if (!CHECK(k)) return;
  CHECK(colour_gap(hh::pixel(116 + 100, 400), k->t.card2) <= 1);  // a bar with nv and no value: the track
  CHECK_STR(rig.set("[3,30]"), "");
  CHECK(!hidden(w[10]->o3) && w[12]->sl_pos == D::slider_pos_of(D::slider_geom(200, 40, 0, 0, 7, 10), 0.3));
  CHECK_STR(rig.set("[3,null]"), "");

  // A faded track is one picture: each pixel of the faded copy is 40% of
  // the one drawn at full opacity over the card, no part through another.
  hh::refresh();
  auto one_picture = [&](int x0, int x1, int y0, int y1, int dx) {
    int worst = 0, seen = 0;
    double bg[3];
    channels(hh::rgb565(k->t.bg), bg);
    for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++) {
        double full[3], faded[3];
        channels(hh::pixel(x, y), full);
        channels(hh::pixel(x + dx, y), faded);
        if (full[0] == bg[0] && full[1] == bg[1] && full[2] == bg[2]) continue;
        seen++;
        for (int i = 0; i < 3; i++) {
          const double want = 0.4 * full[i] + 0.6 * bg[i];
          if (fabs(faded[i] - want) > 1.6) worst++;
        }
      }
    return seen > 100 && worst == 0;
  };
  CHECK_MSG(one_picture(116, 316, 430, 470, 224), "the knob look's track");
  CHECK_MSG(one_picture(116, 316, 472, 482, 224), "a bar's track");

  // A bar's colours, from the widget's own (worked out at the build): the
  // fill c_now, the handle c_on = readable(fill); the icon tx2 below 48.
  CHECK_STR(rig.set("[1,60]"), "");
  hh::refresh();
  D::Widget *bar = w[5];
  const int fill = (int) D::px_round(0.6 * 200);
  CHECK(bar->sl_pos == fill && bar->sl_fill == bar->c_now && bar->c_on == D::readable(bar->c_now));
  CHECK(colour_gap(hh::pixel(470, 140 + fill / 2), bar->c_now) == 0);
  CHECK(colour_gap(hh::pixel(490, 140 + fill - 10), bar->c_on) == 0);  // the handle, fill - 12 to fill - 8
  CHECK(colour_gap(hh::pixel(490, 140 + fill + 20), k->t.card2) == 0);  // the track past the fill
  // The icon, centred 24 from the start edge: readable(fill) on a fill of
  // 48 or more, else tx2 (the card is not filled).
  auto icon_pixels = [&](uint32_t colour) {
    int n = 0, sum = 0;
    for (int y = 140; y < 140 + 60; y++)
      for (int x = 490 - 16; x < 490 + 16; x++)
        if (hh::pixel(x, y) == hh::rgb565(colour)) {
          n++;
          sum += y - 140;
        }
    // Its pixels centre on 24 from the start edge (the top: dir 2).
    return n > 20 && std::abs(2 * sum - 48 * n) <= 3 * n ? n : 0;
  };
  CHECK(bar->sl_icon == bar->c_on && icon_pixels(bar->c_on) > 20);
  CHECK_STR(rig.set("[1,20]"), "");
  hh::refresh();
  CHECK(bar->sl_pos == 40 && bar->sl_icon == k->t.tx2 && icon_pixels(k->t.tx2) > 20);
  CHECK_STR(rig.set("[1,60]"), "");

  // A drag on a bar ended by a forced swap sends the value under the finger.
  fake::st.events.clear();
  const std::string small = layout_bytes("layout_small.dl.json");
  hh::press(490, 150);
  hh::move(490, 140 + 120);  // 120 of 200 from the top: 60
  hh::move(490, 140 + 170);  // 85
  CHECK_STR(rig.ha.send(small), "");
  CHECK_STR(rig.ha.commit(hash16(small)), "");
  hh::advance(D::SWAP_WAIT_MS + 600);
  CHECK_STR(tab::active_hash(), hash16(small));
  const auto ev = fake::events("tab1_input");
  CHECK_MSG(ev.size() == 1 && ev[0].d.find("|" + rig.hash + "|5|c|0|85") != std::string::npos,
            ev.empty() ? std::string("nothing sent") : ev[0].d);
  hh::release();
  hh::advance(50);
  CHECK(fake::events("tab1_input").size() == 1);

  // Nothing is sent by a slider whose max is not above its min, nor by one
  // whose track has no length (a knob look 12 long: x1 = 5 is below x0 = 7).
  const std::string none =
      "{\"change\":{\"p\":0},\"id\":2,\"max\":5,\"min\":5,\"r\":[116,140,200,40],\"step\":1,\"t\":\"slider\","
      "\"value\":{\"s\":1}},"
      "{\"change\":{\"p\":0},\"id\":3,\"look\":1,\"max\":{\"s\":2},\"min\":5,\"r\":[116,200,200,40],\"step\":1,"
      "\"t\":\"slider\",\"value\":{\"s\":1}},"
      "{\"change\":{\"p\":0},\"id\":4,\"max\":100,\"min\":0,\"r\":[116,260,12,40],\"step\":1,\"t\":\"slider\","
      "\"track\":[255,65280],\"value\":{\"s\":1}},"
      "{\"change\":{\"p\":0},\"id\":5,\"max\":100,\"min\":0,\"r\":[116,320,12,40],\"step\":1,\"t\":\"slider\","
      "\"value\":{\"s\":1}}";
  if (!CHECK(rig.up(two_pages("", none, "nn"), false))) return;
  CHECK_STR(rig.set("[1,5],[2,3]"), "");
  for (const auto &p : {std::make_pair(200, 160), std::make_pair(200, 220), std::make_pair(122, 280)}) {
    hh::tap(p.first, p.second);
    hh::advance(20);
    CHECK_MSG(sent_words().empty(), std::to_string(p.second) + ": " + sent_words());
  }
  // Without a track the same 12 px send (the run is the length).
  hh::tap(122, 340);
  hh::advance(20);
  CHECK_STR(sent_words(), "5c");

  // In a card that is not available, the 40% is one picture too: under the
  // mark and under the fill nothing of the track is drawn, so each shows as
  // 40% of its colour over the card.
  const std::string un =
      "{\"id\":2,\"max\":100,\"min\":0,\"r\":[116,140,300,40],\"step\":1,\"t\":\"slider\","
      "\"track\":[255,65280],\"value\":{\"s\":1}},"
      "{\"id\":3,\"look\":1,\"max\":100,\"min\":0,\"r\":[116,200,300,40],\"step\":1,\"t\":\"slider\","
      "\"value\":{\"s\":1}}";
  // A white track on a dark card, so a track under a part would show.
  if (!CHECK(rig.up(two_pages("\"un\":{\"s\":2},", un, "nb", "{\"card2\":16777215}"), false))) return;
  CHECK_STR(rig.set("[1,50],[2,1]"), "");
  hh::refresh();
  D::Widget *mk = widget(2), *fl = widget(3);
  const D::CardLook *uk = D::card_look(*active()->model, widget(1));
  if (!CHECK(mk && fl && uk && uk->unavailable && mk->sl_pos == 150 && fl->sl_pos == 150)) return;
  CHECK((int) lv_obj_get_style_opa(mk->obj, LV_PART_MAIN) == LV_OPA_40);
  auto over_card = [&](uint16_t got, uint32_t colour) {
    double g[3], c[3], b[3];
    channels(got, g);
    channels(hh::rgb565(colour), c);
    channels(hh::rgb565(uk->t.bg), b);
    for (int i = 0; i < 3; i++)
      if (fabs(g[i] - (0.4 * c[i] + 0.6 * b[i])) > 1.6) return false;
    return true;
  };
  CHECK_MSG(over_card(hh::pixel(116 + 150, 160), 0xFFFFFF), "the mark");
  CHECK_MSG(over_card(hh::pixel(116 + 75, 220), fl->c_now), "the fill");
  CHECK_MSG(over_card(hh::pixel(116 + 225, 220), uk->t.card2), "the track past the fill");
  CHECK_STR(rig.set("[2,0]"), "");
  hh::refresh();
  CHECK(colour_gap(hh::pixel(116 + 150, 160), 0xFFFFFF) == 0 && colour_gap(hh::pixel(116 + 75, 220), fl->c_now) == 0);

  // On the kit's filled card 1: the track in FP, the fill legible(c, FP, FT).
  if (!CHECK(rig.up())) return;
  hh::refresh();
  D::Widget *kb = widget(6);
  const D::CardLook *kk = D::card_look(*active()->model, widget(2));
  if (!CHECK(kb && kk && kk->k.filled)) return;
  CHECK(kb->sl_track == kk->k.FP && kb->c_now == D::legible(16754519, kk->k.FP, kk->k.text));
  CHECK(colour_gap(hh::pixel(kb->r[0] + kb->r[2] - 20, centre_y(kb->r)), kk->k.FP) == 0);
  CHECK(colour_gap(hh::pixel(kb->r[0] + 100, kb->r[1] + 6), kb->c_now) == 0);
}
