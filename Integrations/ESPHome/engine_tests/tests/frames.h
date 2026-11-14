#pragma once
// The frames of v1_pixels_unchanged (B.9 of the cards spec): one fixed run
// over the version 1 fixtures that both test programs make, this engine and
// the frozen sources of engine 0.1.0 (engine_tests/frozen/0.1.0). The run
// compares the two sets byte for byte. No hash is stored anywhere: the host's
// glyphs come from a floating-point rasteriser, so only two programs built in
// the same run, with the same compiler, flags, LVGL and fonts, can be
// compared.
//
// Only what engine 0.1.0 already had is used here: the calls of tab_engine.h,
// tab::crypto, and the fake platform. A later engine must keep them.
//
//   frames::run(contract_dir, fonts, sink)   sink(name) once per frame, with
//                                            the frame in hh::g_fb
//   frames::write / frames::read      one frame as raw RGB565 bytes
#include <cstdio>
#include <string>
#include <vector>
#include "harness.h"
#include "fake_platform.h"
#include "tab_engine.h"
#include "tj.h"

namespace frames {

static const char *LAYOUTS[] = {"layout_small", "layout_all_cards"};
static const char *FRAME_NONCE = "0123456789abcdef0123456789abcdef";
static const char *FRAME_SECRET_HEX = "6465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f80818283";
static const char *FRAME_PSK_HEX = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

static std::string file_text(const std::string &path) {
  size_t n = 0;
  uint8_t *d = hh::load_file(path.c_str(), &n);
  std::string s(reinterpret_cast<char *>(d), n);
  free(d);
  return s;
}

static std::string hex_of(const uint8_t *d, size_t n) {
  std::string s(n * 2, '0');
  tab::crypto::to_hex(d, n, &s[0]);
  return s;
}

// The steps of state_stream.json: the payloads and what each call answers
// ("ok" or an error code).
struct Step {
  std::vector<std::string> payloads;
  std::string expect;
};
static std::vector<Step> state_steps(const std::string &cdir) {
  const tj::Value f = tj::load(cdir + "/fixtures/state_stream.json");
  std::vector<Step> steps;
  for (const tj::Value &step : f["steps"].a) {
    Step st;
    for (const tj::Value &p : step["payloads"].a) st.payloads.push_back(p.s);
    st.expect = step["expect"].s;
    steps.push_back(st);
  }
  return steps;
}

struct Session {
  uint8_t secret[32];
  std::string nonce = FRAME_NONCE;
  uint32_t n = 0;

  std::string auth(const char *action, const std::string &body) {
    const std::string msg = std::string("apollo-tab1 call v1|") + fake::MAC + "|" + nonce + "|" + std::to_string(++n) +
                            "|" + action + "|" + body;
    uint8_t d[32];
    tab::crypto::hmac_sha256(secret, 32, msg.data(), msg.size(), d);
    return std::to_string(n) + "." + hex_of(d, 16);
  }
};

// Every page of the layout on screen, drawn whole.
template <class Sink>
static void pages(const std::string &prefix, Sink &sink) {
  for (int p = 0; p < tab::page_count(); p++) {
    tab::go_to(p);
    hh::advance(100);
    lv_obj_invalidate(lv_screen_active());
    hh::refresh();
    sink(prefix + ".p" + std::to_string(p));
  }
  tab::go_to(0);
  hh::advance(20);
}

// Sends a layout in chunks that never end inside a UTF-8 character, commits
// it and waits until it is on screen.
static std::string show(Session &s, const std::string &bytes) {
  uint8_t d[32];
  tab::crypto::sha256(bytes.data(), bytes.size(), d);
  const std::string hash = hex_of(d, 8);
  const size_t chunk = 3000;
  std::vector<std::string> parts;
  for (size_t i = 0; i < bytes.size();) {
    size_t end = i + chunk < bytes.size() ? i + chunk : bytes.size();
    while (end < bytes.size() && end > i && ((uint8_t) bytes[end] & 0xC0) == 0x80) end--;
    parts.push_back(bytes.substr(i, end - i));
    i = end;
  }
  for (size_t i = 0; i < parts.size(); i++) {
    const char *r = tab::on_layout(hash.data(), 16, (int) i, (int) parts.size(), (int) bytes.size(), parts[i].data(),
                                   parts[i].size());
    if (*r) return std::string("tab_layout: ") + r;
  }
  const std::string a = s.auth("tab_commit", hash);
  const char *r = tab::on_commit(hash.data(), hash.size(), a.data(), a.size());
  if (*r) return std::string("tab_commit: ") + r;
  for (int t = 0; t < 10000 && hash != tab::active_hash(); t++) hh::advance(1);
  if (hash != tab::active_hash()) return "the layout " + hash + " never came on screen";
  hh::advance(100);
  return "";
}

// The fixed run, on an engine that is not started. Returns "" or what went
// wrong. Leaves the engine running.
template <class Fonts, class Sink>
static std::string run(const std::string &cdir, const Fonts &fonts, Sink sink) {
  hh::g_down = false;
  hh::advance(40);
  fake::st = fake::State();
  tab::crypto::from_hex(FRAME_PSK_HEX, 32, fake::st.key);
  Session s;
  tab::crypto::from_hex(FRAME_SECRET_HEX, 32, s.secret);
  memcpy(fake::st.secret, s.secret, 32);
  fake::st.have_secret = true;
  tab::begin(fonts, fake::platform());
  hh::advance(20);

  // A session: the first hello of the boot, signed with call number 0.
  const std::string msg = std::string("apollo-tab1 call v1|") + fake::MAC + "|" + s.nonce + "|0|tab_hello|";
  uint8_t d[32];
  tab::crypto::hmac_sha256(s.secret, 32, msg.data(), msg.size(), d);
  const std::string hello_auth = "0." + hex_of(d, 16);
  char out[768];
  if (tab::on_hello(s.nonce.data(), s.nonce.size(), hello_auth.data(), hello_auth.size(), out, sizeof out) == 0 ||
      !strstr(out, "\"session\":1"))
    return "tab_hello started no session";

  for (const char *name : LAYOUTS) {
    const std::string err = show(s, file_text(cdir + "/fixtures/" + name + ".dl.json"));
    if (!err.empty()) return std::string(name) + ": " + err;
    pages(std::string(name) + ".none", sink);
  }
  // layout_all_cards with the values of state_stream.json, drawn after the
  // first full snapshot, before each refused step and after the last one.
  const auto steps = state_steps(cdir);
  if (steps.size() < 2) return "state_stream.json has fewer than 2 steps";
  for (size_t k = 0; k < steps.size(); k++) {
    for (const std::string &p : steps[k].payloads) {
      const std::string a = s.auth("tab_set", p);
      const std::string r = tab::on_set(p.data(), p.size(), a.data(), a.size());
      if (r != (steps[k].expect == "ok" ? "" : steps[k].expect))
        return "tab_set of step " + std::to_string(k) + " answered \"" + r + "\", not " + steps[k].expect;
      hh::advance(20);
    }
    if (k == 0 || k + 1 == steps.size() || steps[k + 1].expect != "ok")
      pages("layout_all_cards.step" + std::to_string(k), sink);
  }
  return "";
}

// Raw RGB565 frames, W x H, little-endian as in memory.
static bool write(const std::string &path) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  const size_t n = (size_t) hh::W * hh::H;
  const bool ok = fwrite(hh::g_fb, 2, n, f) == n;
  return fclose(f) == 0 && ok;
}

static bool read(const std::string &path, std::vector<uint16_t> *out) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return false;
  const size_t n = (size_t) hh::W * hh::H;
  out->assign(n, 0);
  const bool ok = fread(out->data(), 2, n, f) == n && fgetc(f) == EOF;
  fclose(f);
  return ok;
}

}  // namespace frames
