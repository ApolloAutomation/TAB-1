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
    // Drawn with the looks of version 1: every widget the engine draws so
    // far is at its rectangle.
    for (uint32_t i = 0; i < m.nw; i++) {
      const D::Widget &w = m.w[i];
      const std::string id = std::string(name) + " widget " + std::to_string(w.id);
      if (w.kind == D::W_CHOICE) continue;  // drawn from package E8 on
      if (!CHECK_MSG(w.obj, id)) continue;
      if (w.kind == D::W_BUTTON && w.shape) continue;  // its own shape from package E5 on
      CHECK_MSG(obj_at(w.obj, w.r[0], w.r[1], w.r[2], w.r[3]), id + " at " + rect_text(w.obj));
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
