// The engine's behaviour: the calls from Home Assistant (contract 2-8),
// pairing and signed calls, the engine timer (connection, knock, build, swap,
// delete, touch feedback, flash cache, swipes) and signed input. Implements
// the interface in tab_engine.h.
#include "tab_engine.h"  // first, outside the guard (see tab_state.h)

#ifndef TAB_CORE_H
#define TAB_CORE_H

#include <cstdlib>
#include "tab_state.h"
#include "tab_ui.h"

namespace tab {
namespace detail {

// ---------------------------------------------------------------- helpers

inline bool due(uint32_t at) { return (int32_t) (now() - at) >= 0; }

inline uint32_t later(uint32_t ms) {
  const uint32_t t = now() + ms;
  return t ? t : 1;  // 0 means "nothing scheduled"
}

inline bool span_is(const char *s, size_t n, const char *lit) {
  const size_t m = strlen(lit);
  return n == m && (m == 0 || memcmp(s, lit, m) == 0);
}

inline void hash_of(const uint8_t *data, size_t n, char out[17]) {
  uint8_t d[32];
  crypto::sha256(data, n, d);
  crypto::to_hex(d, 8, out);
  out[16] = 0;
}

inline void kid_of(const uint8_t secret[32], char out[17]) {
  static const char LABEL[] = "apollo-tab1 kid v1";
  uint8_t d[32];
  crypto::hmac_sha256(secret, 32, LABEL, sizeof LABEL - 1, d);
  crypto::to_hex(d, 8, out);
  out[16] = 0;
}

inline const char *mac() { return g.pf.mac ? g.pf.mac : ""; }

inline Layout *layout_of(const Widget *w) {
  for (int i = 0; i < LAYOUTS; i++) {
    Layout &l = g.lay[i];
    if (l.state == L_FREE || !l.model) continue;
    if (w >= l.model->w && w < l.model->w + l.model->nw) return &l;
  }
  return nullptr;
}

// A number in wire form (contract 4.1, reference num()): clamped, rounded to
// 3 decimals, an integer when whole, else the shortest text that reads back.
inline void wire_number(double v, char *out, size_t cap) {
  if (!(v == v)) v = 0;
  if (v > 1e9) v = 1e9;
  if (v < -1e9) v = -1e9;
  char b[48];
  snprintf(b, sizeof b, "%.3f", v);
  const double r = strtod(b, nullptr);
  if (r == floor(r)) {
    snprintf(out, cap, "%.0f", r == 0 ? 0.0 : r);
    return;
  }
  for (int p = 1; p <= 17; p++) {
    snprintf(out, cap, "%.*g", p, r);
    if (strtod(out, nullptr) == r) return;
  }
}

// ---------------------------------------------------------------- JSON answers

struct Out {
  char *p;
  size_t cap;
  size_t n;
  bool ok;
  void raw(const char *s) {
    const size_t m = strlen(s);
    if (!ok || n + m >= cap) {
      ok = false;
      return;
    }
    memcpy(p + n, s, m);
    n += m;
    p[n] = 0;
  }
  void str(const char *s) {  // engine text only: hashes, codes, versions
    raw("\"");
    raw(s);
    raw("\"");
  }
  void num(unsigned long long v) {
    char b[24];
    snprintf(b, sizeof b, "%llu", v);
    raw(b);
  }
};

// While a layout is being built, `block` is the value read when it was
// staged: the heap is not walked during a build (contract 3.1).
inline void write_mem(Out &o) {
  o.raw("\"mem\":{\"block\":");
  if (staged_layout())
    o.num((unsigned long long) g.staged_block);
  else
    o.num(g.pf.largest_block ? (unsigned long long) g.pf.largest_block() : 0);
  o.raw(",\"int\":");
  o.num(g.pf.free_internal ? g.pf.free_internal() : 0);
  o.raw(",\"psram\":");
  o.num(g.pf.free_psram ? g.pf.free_psram() : 0);
  o.raw("}");
}

inline const char *hash_or_empty(const Layout *l) { return l ? l->hash : ""; }

// ---------------------------------------------------------------- signed calls

inline bool has_api_key() {
  uint8_t key[32];
  const bool ok = g.pf.api_key && g.pf.api_key(key);
  memset(key, 0, sizeof key);
  return ok;
}

// Paired and able to sign. A tablet without an API key ignores its stored
// secret and counts as not paired until a key is back (contract 6.1): no
// check of auth, no kid, no signed input over a plain connection.
inline bool signing() { return g.paired && has_api_key(); }

// Contract 3: a nonce starts at most one session per boot. The last
// NONCE_RING session nonces of this boot are kept, in binary.
inline bool nonce_used(const char *nonce) {
  uint8_t b[16];
  crypto::from_hex(nonce, 16, b);
  for (int i = 0; i < g.nonces_n; i++)
    if (memcmp(g.nonces[i], b, 16) == 0) return true;
  return false;
}

inline void nonce_remember(const char *nonce) {
  crypto::from_hex(nonce, 16, g.nonces[g.nonces_next]);
  g.nonces_next = (uint8_t) ((g.nonces_next + 1) % NONCE_RING);
  if (g.nonces_n < NONCE_RING) g.nonces_n++;
}

// Contract 6.2 (check_auth in the reference code). body may hold any bytes.
inline bool check_auth(const char *auth, size_t auth_len, const char *action, const char *nonce, const char *body,
                       size_t body_len) {
  if (!signing()) return true;
  const bool hello = strcmp(action, "tab_hello") == 0;
  if (!hello && !g.session) return false;
  if (!auth) return false;
  size_t dot = 0;
  while (dot < auth_len && auth[dot] != '.') dot++;
  if (dot == 0 || dot > 9 || dot >= auth_len) return false;
  uint32_t n = 0;
  for (size_t i = 0; i < dot; i++) {
    if (auth[i] < '0' || auth[i] > '9') return false;
    n = n * 10 + (uint32_t) (auth[i] - '0');
  }
  if (!crypto::is_hex(auth + dot + 1, auth_len - dot - 1, 32)) return false;
  if (hello ? n != 0 : n <= g.last_n) return false;
  char head[12];
  const int hn = snprintf(head, sizeof head, "%u", (unsigned) n);
  if (hn < 1 || (size_t) hn != dot || memcmp(head, auth, dot) != 0) return false;  // no leading zeros
  crypto::Hmac m;
  crypto::hmac_init(m, g.secret, 32);
  crypto::hmac_update_str(m, "apollo-tab1 call v1|");
  crypto::hmac_update_str(m, mac());
  crypto::hmac_update_str(m, "|");
  crypto::hmac_update(m, nonce, 32);
  crypto::hmac_update_str(m, "|");
  crypto::hmac_update(m, head, (size_t) hn);
  crypto::hmac_update_str(m, "|");
  crypto::hmac_update_str(m, action);
  crypto::hmac_update_str(m, "|");
  if (body_len) crypto::hmac_update(m, body, body_len);
  uint8_t d[32];
  crypto::hmac_final(m, d);
  char hex[32];
  crypto::to_hex(d, 16, hex);
  if (!crypto::equal(hex, auth + dot + 1, 32)) return false;
  if (!hello) g.last_n = n;
  return true;
}

// ---------------------------------------------------------------- connection

inline void forget_pending() { memset(g.pend, 0, sizeof g.pend); }

inline void poll_connection() {
  const bool c = g.pf.ha_connected && g.pf.ha_connected();
  if (c == g.connected) return;
  g.connected = c;
  if (c) g.greet_from = now();  // the waiting screen gives TAB-1 GREET_WAIT_MS to greet
  const bool greeted = g.early && (int32_t) (now() - g.early_at) < (int32_t) EARLY_WINDOW_MS;
  g.early = false;
  if (c && greeted) {
    // Home Assistant subscribes to states at the very end of its connect
    // sequence, so its first tab_hello or tab_status can arrive on this
    // connection before ha_connected() turns true. Keep the session that
    // call started, and do not knock: HA has answered (contract 2.2).
    g.knocking = false;
    say(2, "Home Assistant connected");
    return;
  }
  g.session = false;
  forget_pending();
  if (c) {
    g.knocking = true;
    g.knock_count = 0;
    g.knock_gap_s = KNOCK_FIRST_S;
    g.knock_next = later(KNOCK_FIRST_S * 1000u);
    say(2, "Home Assistant connected");
  } else {
    g.knocking = false;
    g.away_since = now();
    say(1, "Home Assistant disconnected");
  }
}

// Contract 2.2: at 3, 6, 9, 12, 15 s, then the gap doubles up to 300 s.
inline void knock_tick() {
  if (!g.knocking || !g.connected || !due(g.knock_next)) return;
  char d[48];
  snprintf(d, sizeof d, "%d|%s|%u", INPUT_VERSION, mac(), (unsigned) g.pf.boot);
  if (g.pf.send_event) g.pf.send_event("tab1_hello", d, nullptr);
  if (g.knock_count < 255) g.knock_count++;
  if (g.knock_count >= KNOCK_REPEATS) {
    const int gap = g.knock_gap_s * 2;
    g.knock_gap_s = (uint16_t) (gap > KNOCK_MAX_S ? KNOCK_MAX_S : gap);
  }
  g.knock_next += g.knock_gap_s * 1000u;
  if (!g.knock_next) g.knock_next = 1;
}

// A touch found no session although Home Assistant is connected and the
// tablet is paired: HA holds a session this tablet lost, or never greeted.
// Knock now instead of waiting for the schedule, at most once per
// KNOCK_FIRST_S, and start the schedule again (contract 2.2).
inline void knock_for_touch() {
  if (!g.connected) return;
  if (g.touch_knock_at && (int32_t) (now() - g.touch_knock_at) < (int32_t) (KNOCK_FIRST_S * 1000)) return;
  g.touch_knock_at = now();
  if (!g.touch_knock_at) g.touch_knock_at = 1;
  say(1, "Touch without a session; asking Home Assistant to greet again");
  char d[48];
  snprintf(d, sizeof d, "%d|%s|%u", INPUT_VERSION, mac(), (unsigned) g.pf.boot);
  if (g.pf.send_event) g.pf.send_event("tab1_hello", d, nullptr);
  g.knocking = true;
  g.knock_count = 0;
  g.knock_gap_s = KNOCK_FIRST_S;
  g.knock_next = later(KNOCK_FIRST_S * 1000u);
}

// ---------------------------------------------------------------- layouts

inline void release_block(void *p) {
  if (p && g.pf.release) g.pf.release(p);
}

inline void release_layout(Layout &l) {
  release_block(l.model);
  release_block(l.bytes);
  memset(&l, 0, sizeof l);
}

inline void drop_transfer() {
  release_block(g.tr.bytes);
  memset(&g.tr, 0, sizeof g.tr);
}

// Hides a layout and queues its objects for deletion, a page per tick.
inline void retire(int idx) {
  Layout *l = layout_at(idx);
  if (!l || l->state == L_FREE || l->state == L_RETIRING) return;
  if (g.q_open && g.q_layout == idx) question_close();
  if (l->root) lv_obj_add_flag(l->root, LV_OBJ_FLAG_HIDDEN);
  l->state = L_RETIRING;
  l->d_page = 0;
  if (g.active == idx) g.active = -1;
  if (g.staged == idx) g.staged = -1;
  for (FbRef &r : g.fbl)
    if (r.layout == idx) r.layout = -1;
}

// A free layout slot. When every slot is taken (commits faster than old
// layouts are deleted), the oldest retired layout is deleted at once: full
// slots are never a reason to answer "memory".
inline int free_layout() {
  int oldest = -1;
  for (int i = 0; i < LAYOUTS; i++) {
    if (g.lay[i].state == L_FREE) return i;
    if (g.lay[i].state == L_RETIRING &&
        (oldest < 0 || (uint8_t) (g.gen_next - g.lay[i].gen) > (uint8_t) (g.gen_next - g.lay[oldest].gen)))
      oldest = i;
  }
  if (oldest < 0) return -1;
  Budget all = unlimited();
  delete_step(g.lay[oldest], all);
  release_layout(g.lay[oldest]);
  return oldest;
}

// Shows a built layout in one step and retires the one it replaces.
inline void activate(int idx) {
  Layout &n = g.lay[idx];
  const int old = g.active;
  n.state = L_ACTIVE;
  g.active = (int8_t) idx;
  if (g.staged == idx) g.staged = -1;
  g.build_code = "";
  if (old >= 0 && old != idx) retire(old);
  // (2) A card press or a hold in progress is dropped at the swap (cards spec B.5).
  g.cp_layout = -1;
  g.held = true;
  const int page = (g.page >= 0 && g.page < n.model->npages) ? g.page : 0;
  g.page = page;
  show_page(n, page);
  show_layout(n);
  g.save_at = strcmp(g.stored, n.hash) != 0 ? later(CACHE_DELAY_MS) : 0;
  say(2, "Layout %s on screen", n.hash);
}

// The build in step 3 of design 7.5 failed: keep the old layout.
inline void fail_build(const char *code) {
  const int idx = g.staged;
  if (idx < 0) return;
  say(1, "Build of %s stopped: %s", g.lay[idx].hash, code);
  retire(idx);
  g.build_code = code;
}

// Checks, parses and stages the bytes. Returns "" or a contract error code.
// On success the bytes block belongs to the new layout.
inline const char *stage(uint8_t *bytes, uint32_t size, const char *hash, int *out_idx) {
  Loader ld;
  const char *code = model_check(reinterpret_cast<const char *>(bytes), size, ld);
  if (*code) return code;
  const size_t msize = model_size(ld.c);
  const size_t objects = ld.c.objects + ld.c.pages + 2;
  const size_t need = msize + objects * OBJECT_BYTES + MEMORY_MARGIN;
  // The one look at the largest block for this layout (it walks the heap).
  const size_t block_now = g.pf.largest_block ? g.pf.largest_block() : 0;
  if (block_now < need) return "memory";
  const int idx = free_layout();
  if (idx < 0) return "memory";
  void *block = g.pf.alloc ? g.pf.alloc(msize) : nullptr;
  if (!block) return "memory";
  Model *m = model_fill(reinterpret_cast<const char *>(bytes), size, ld.c, block);
  if (!m) {
    release_block(block);
    return "parse";
  }
  Layout &l = g.lay[idx];
  memset(&l, 0, sizeof l);
  l.state = L_BUILDING;
  l.gen = ++g.gen_next;
  memcpy(l.hash, hash, 16);
  l.hash[16] = 0;
  l.bytes = bytes;
  l.size = size;
  l.model = m;
  *out_idx = idx;
  g.staged_block = block_now;
  if (ld.c.drop_charts || ld.c.drop_arcs || ld.c.drop_tracks)
    say(1, "Layout %s: %u charts, %u thermostats and %u tracks over a page's limit are not drawn", l.hash,
        (unsigned) ld.c.drop_charts, (unsigned) ld.c.drop_arcs, (unsigned) ld.c.drop_tracks);
  return "";
}

// ---------------------------------------------------------------- touch feedback

// Paints a widget whose feedback changed. (2) A card's feedback, and an
// optimistic bool its card shows for the same slot (contract 7.4), change
// the card's colours and every widget in it.
inline void feedback_paint(Layout &l, Widget &w) {
  Model &m = *l.model;
  Widget *card = w.kind == W_CARD ? &w : card_of(m, w);
  const uint16_t wi = (uint16_t) (&w - m.w);
  if (!card || (card != &w && !(card->ovr_slot && card->ovr_from == wi))) {
    paint_widget(l, w);
    return;
  }
  const bool changed = card_look_update(m, *card);
  paint_widget(l, *card);
  const uint32_t ci = (uint32_t) (card - m.w);
  for (uint32_t k = 1; k <= card->children && ci + k < m.nw; k++) {
    Widget &c = m.w[ci + k];
    if (changed) widget_colour_update(m, c);
    paint_widget(l, c);
  }
}

inline void fb_track(Layout &l, Widget &w) {
  const int li = layout_index(&l);
  const uint16_t wi = (uint16_t) (&w - l.model->w);
  FbRef *spare = nullptr;
  for (FbRef &r : g.fbl) {
    if (r.layout == li && r.gen == l.gen && r.widget == wi) return;
    if (!spare && r.layout < 0) spare = &r;
  }
  if (!spare) {
    // Full: end the oldest feedback now.
    spare = &g.fbl[0];
    Layout *ol = layout_at(spare->layout);
    if (ol && ol->state == L_ACTIVE && ol->gen == spare->gen) {
      Widget &ow = ol->model->w[spare->widget];
      ow.fb = 0;
      ow.ovr = 0;
      feedback_paint(*ol, ow);
    }
  }
  spare->layout = (int8_t) li;
  spare->gen = l.gen;
  spare->widget = wi;
}

// The bool a tap with policy 0 flips at once (contract 7.4): a switch's
// value, (2) a card's slot-bound `on` that has a value, a button's
// slot-bound value. nullptr: the tap shows the waiting outline instead.
inline const Bound *flipped_by_tap(Model &m, Widget &w) {
  double v;
  switch (w.kind) {
    case W_SWITCH: return &w.value;
    case W_CARD: return w.on.how == B_SLOT && bound_num(m, w.on, &v) ? &w.on : nullptr;
    case W_BUTTON: return w.value.how == B_SLOT ? &w.value : nullptr;
    default: return nullptr;
  }
}

inline void start_feedback(Layout &l, Widget &w, uint8_t policy, char event, bool has_value, double value) {
  Model &m = *l.model;
  const uint32_t t = now();
  w.fbp = policy;
  // (2) A choice shows the tapped option as selected with policy 0 only;
  // policy 1 flashes the tapped cell, policy 2 shows the outline (7.4).
  if (w.kind == W_CHOICE && event == 'c' && has_value) w.ch_pulse = (int8_t) value;
  if (event == 'c' && has_value && policy != 3 && (w.kind != W_CHOICE || policy == 0)) {
    w.ovr = 1;
    w.ovr_value = value;
    w.ovr_until = t + (policy == 2 ? PROGRESS_TIMEOUT_MS : ACK_TIMEOUT_MS);
  }
  const Bound *flip = event == 't' && policy == 0 ? flipped_by_tap(m, w) : nullptr;
  switch (policy) {
    case 0:
      if (flip) {
        bool cur = false;
        if (w.kind == W_CARD)
          cur = card_is_on(m, &w);
        else
          bool_value(m, w, *flip, &cur);
        w.ovr = 1;
        w.ovr_value = cur ? 0 : 1;
        w.ovr_until = t + ACK_TIMEOUT_MS;
        // The card shows it for that slot, and so does every switch and
        // button of the card bound to it.
        Widget *card = w.kind == W_CARD ? &w : card_of(m, w);
        if (card && flip->how == B_SLOT) {
          card->ovr_slot = flip->slot;
          card->ovr_from = (uint16_t) (&w - m.w);
        }
      } else if (event == 't') {
        w.fb = 1;
        w.fb_until = t + ACK_TIMEOUT_MS;
      }
      break;
    case 1:
      w.fb = 2;
      w.fb_until = t + PULSE_MS;
      break;
    case 2:
      w.fb = 1;
      w.fb_until = t + PROGRESS_TIMEOUT_MS;
      break;
    default:
      w.fb = 1;
      w.fb_until = t + CONFIRM_TIMEOUT_MS;
      break;
  }
  fb_track(l, w);
  feedback_paint(l, w);
}

inline bool can_send() { return g.connected && g.session && signing(); }

inline const char *note_words(Note n);

// Why a touch sends nothing (contract 7.4), or nullptr when it can be sent.
// Without Home Assistant and without a network, the Wi-Fi note that fits:
// what note_tick saw at its last look (at most HINT_POLL_MS ago), so a touch
// reads nothing from the platform.
inline const char *why_not_sent() {
  if (!g.connected) {
    if (g.note) return note_words(g.note);
    if (g.net_down_at) return NOTE_CONNECTING;
    return "Home Assistant is not connected";
  }
  if (!has_api_key()) return "Touch is off: this tablet is not protected";
  if (!g.paired) return "Not paired with Home Assistant";
  if (!g.session) return "Home Assistant is not connected";
  return nullptr;
}

// Shows why a touch sends nothing. Without a session only, it also knocks.
inline void refuse_touch() {
  const char *why = why_not_sent();
  show_strip(why ? why : "Home Assistant is not connected");
  if (g.connected && signing() && !g.session) knock_for_touch();
}

// Builds, signs and fires one input (contract 7.1, 7.2).
inline bool send_input(Layout &l, Widget &w, char event, bool confirmed, bool has_value, double value) {
  if (!can_send()) {
    refuse_touch();
    return false;
  }
  g.counter++;
  char val[40] = "";
  if (has_value) wire_number(value, val, sizeof val);
  char d[256];
  const int n = snprintf(d, sizeof d, "%d|%s|%s|%u|%s|%u|%c|%c|%s", INPUT_VERSION, mac(), g.nonce,
                         (unsigned) g.counter, l.hash, (unsigned) w.id, event, confirmed ? '1' : '0', val);
  if (n <= 0 || n > 200) return false;
  uint8_t mac_out[32];
  crypto::hmac_sha256(g.secret, 32, d, (size_t) n, mac_out);
  char s[33];
  crypto::to_hex(mac_out, 16, s);
  s[32] = 0;
  if (g.pf.send_event) g.pf.send_event("tab1_input", d, s);
  return true;
}

// Sends and starts the feedback of the policy; remembers it for the ack.
inline void send_with_policy(Layout &l, Widget &w, char event, bool confirmed, bool has_value, double value,
                             uint8_t policy) {
  if (!send_input(l, w, event, confirmed, has_value, value)) return;
  Pending *p = nullptr;
  for (Pending &x : g.pend)
    if (!x.used) {
      p = &x;
      break;
    }
  if (!p) {
    p = &g.pend[0];
    for (Pending &x : g.pend)
      if ((int32_t) (x.at - p->at) < 0) p = &x;
  }
  p->used = 1;
  p->counter = g.counter;
  p->at = now();
  p->widget = (uint16_t) (&w - l.model->w);
  p->layout = (int8_t) layout_index(&l);
  p->gen = l.gen;
  p->policy = policy;
  p->event = event;
  p->has_value = has_value ? 1 : 0;
  p->value = value;
  start_feedback(l, w, policy, event, has_value, value);
}

inline bool asks_first(const Widget &w, uint8_t policy) { return policy == 3 || w.cf; }

inline void input(Layout &l, Widget &w, char event, bool has_value, double value, uint8_t policy) {
  if (!can_send()) {
    refuse_touch();
    return;
  }
  if (asks_first(w, policy)) {
    question_open(l, w, event, has_value, value);
    return;
  }
  send_with_policy(l, w, event, false, has_value, value, policy);
}

inline void widget_tapped(Layout &l, Widget &w) {
  if (w.tap == 2) {
    show_page(l, w.tap_v);
    return;
  }
  if (w.tap != 1) return;
  input(l, w, 't', false, 0, w.tap_v);
}

inline void slider_released(Layout &l, Widget &w, double value) {
  // (2) No length to move over, or max not above min: nothing to send.
  if (!w.change || !slider_can_send(*l.model, w)) return;
  input(l, w, 'c', true, value, w.change_v);
}

inline void thermostat_step(Layout &l, Widget &w, int direction) {
  if (!w.change) return;
  double cur;
  if (!thermostat_value(*l.model, w, &cur)) return;
  double lo, hi, step;
  limits_of(*l.model, w, &lo, &hi, &step);
  const double next = clamp_step(cur + direction * (step > 0 ? step : 1), lo, hi, step);
  if (next == cur) return;
  input(l, w, 'c', true, next, w.change_v);
}

// (2) A tap on option i of a choice sends it, also when it is the one
// selected (contract 4.3).
inline void choice_picked(Layout &l, Widget &w, int option) {
  if (!w.change || option < 0 || option >= w.options) return;
  input(l, w, 'c', true, option, w.change_v);
}

inline void question_answer(bool yes) {
  if (!g.q_open) return;
  Layout *l = layout_at(g.q_layout);
  const uint8_t gen = g.q_gen;
  const uint16_t wi = g.q_widget;
  question_close();
  if (!yes || !l || l->state != L_ACTIVE || l->gen != gen || !l->model || wi >= l->model->nw) return;
  send_with_policy(*l, l->model->w[wi], g.q_event, true, g.q_has_value, g.q_value, 3);
}

// A new value arrived for layout l (g.dirty): ends "working" and acked values.
inline bool card_dirty(Model &m, const Widget &w) {
  const int ci = w.card >= 0 ? w.card : (int) (&w - m.w);
  const Widget &card = m.w[ci];
  if (widget_dirty(card)) return true;
  if (card.kind == W_CARD)
    for (uint32_t k = 1; k <= card.children && ci + k < m.nw; k++)
      if (widget_dirty(m.w[ci + k])) return true;
  return false;
}

inline void values_arrived(Layout &l) {
  const int li = layout_index(&l);
  for (FbRef &r : g.fbl) {
    if (r.layout != li || r.gen != l.gen) continue;
    Widget &w = l.model->w[r.widget];
    bool changed = false;
    if (w.ovr == 2 && bound_dirty(w.kind == W_CARD ? w.on : w.value)) {
      w.ovr = 0;
      changed = true;
    }
    if (w.fb == 1 && w.fbp == 2 && card_dirty(*l.model, w)) {
      w.fb = 0;
      w.ovr = 0;
      changed = true;
    }
    if (changed) feedback_paint(l, w);
  }
}

inline void feedback_tick() {
  for (FbRef &r : g.fbl) {
    if (r.layout < 0) continue;
    Layout *l = layout_at(r.layout);
    if (!l || l->state != L_ACTIVE || l->gen != r.gen || !l->model || r.widget >= l->model->nw) {
      r.layout = -1;
      continue;
    }
    Widget &w = l->model->w[r.widget];
    bool changed = false;
    if (w.fb && due(w.fb_until)) {
      w.fb = 0;
      changed = true;
    }
    if (w.ovr && due(w.ovr_until)) {
      w.ovr = 0;
      changed = true;
    }
    if (changed) feedback_paint(*l, w);
    if (!w.fb && !w.ovr) r.layout = -1;
  }
  for (Pending &p : g.pend)
    if (p.used && (int32_t) (now() - p.at) >= (int32_t) PROGRESS_TIMEOUT_MS) p.used = 0;
}

// ---------------------------------------------------------------- timer

inline lv_indev_t *pointer() {
  if (g.indev) return g.indev;
  for (lv_indev_t *i = lv_indev_get_next(nullptr); i; i = lv_indev_get_next(i))
    if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER) {
      g.indev = i;
      break;
    }
  return g.indev;
}

inline bool finger_down() {
  lv_indev_t *i = pointer();
  return i && lv_indev_get_state(i) == LV_INDEV_STATE_PRESSED;
}

// Returns true when it ran a build step.
inline bool build_tick() {
  Layout *s = staged_layout();
  if (!s || s->state != L_BUILDING) return false;
  // Free PSRAM is a number the heap keeps; its largest block would be a walk
  // of the whole heap on every tick. stage() checked the block once.
  if (g.pf.free_psram && g.pf.free_psram() < MEMORY_MARGIN + SLICE_OBJECTS * OBJECT_BYTES) {
    fail_build("memory");
    return false;
  }
  g.obj_fail = false;
  const bool done = build_step(*s, SLICE_MS);
  if (g.obj_fail) {
    fail_build("build");
    return true;
  }
  if (done) {
    s->state = L_READY;
    s->ready_at = now();
  }
  return true;
}

// Widgets of the shown layout that changed while their page was not on
// screen are painted here, a few a tick, never in a tick that ran a build
// step (cards spec B.4, rule 1).
inline void stale_tick(uint32_t tick_start) {
  Layout *a = active_layout();
  if (a && a->state == L_ACTIVE && a->stale) paint_stale(*a, tick_start);
}

// A slider still held when a forced swap retires its layout would lose the
// person's value: its release reaches a retired layout and sends nothing. So
// the drag ends here, as if released now, with the layout it started on
// (contract 7.1, 7.4). A slider that asks first cannot ask on a layout that
// is going away: it sends nothing and the strip says so.
inline void end_drag_for_swap(Layout &a) {
  if (!a.model) return;
  for (uint32_t i = 0; i < a.model->nw; i++) {
    Widget &w = a.model->w[i];
    if (!w.dragging) continue;
    w.dragging = 0;
    w.pressed = 0;
    if (!w.change) continue;
    if (can_send() && asks_first(w, w.change_v)) {
      show_strip(SWAP_DRAG_TEXT);
      continue;
    }
    slider_released(a, w, w.drag_value);  // sends, or shows why it cannot
  }
}

inline void swap_tick() {
  Layout *s = staged_layout();
  if (!s || s->state != L_READY) return;
  if (finger_down()) {
    if ((int32_t) (now() - s->ready_at) < (int32_t) SWAP_WAIT_MS) return;
    // The finger stayed down past SWAP_WAIT_MS: swap anyway.
    Layout *a = active_layout();
    if (a) end_drag_for_swap(*a);
  }
  activate(g.staged);
}

// Deletes retired layouts within one slice's budget. The time counts from the
// start of this timer tick, so a build slice and a delete slice together stay
// within SLICE_MS: while a build runs, deleting waits.
inline void delete_tick(uint32_t tick_start) {
  Budget b{DELETE_OBJECTS, tick_start, SLICE_MS};
  for (int i = 0; i < LAYOUTS && b.left(); i++) {
    Layout &l = g.lay[i];
    if (l.state != L_RETIRING) continue;
    if (delete_step(l, b)) release_layout(l);
  }
}

// A passing message ends: the Wi-Fi note under it shows again, if any.
inline void strip_tick() {
  if (g.strip_until && due(g.strip_until)) {
    g.strip_until = 0;
    g.note_shown = N_NONE;  // the strip shows no note now; note_tick puts it back
    hide_strip();
  }
}

// "<before><hotspot's name><after>" in buf, or fallback without a name.
inline const char *with_hotspot(char *buf, size_t cap, const char *before, const char *after, const char *fallback) {
  if (!g.pf.hotspot || !g.pf.hotspot[0]) return fallback;
  snprintf(buf, cap, "%s%s%s", before, g.pf.hotspot, after);
  return buf;
}

// The erased step with this tablet's name, the way Home Assistant names its
// question in Repairs: "Authentication expired for <name>". The step without
// the name (HINT_ERASED.step, "for the name above") when there is none, or
// when the line would not fit: a long name would lose its end, which is what
// tells two tablets apart.
inline const char *erased_step() {
  const char *name = g.pf.name;
  if (!name || !name[0]) return HINT_ERASED.step;
  const int n = snprintf(g.hint_text, sizeof g.hint_text, "%s%s%s", WAIT_ERASED_BEFORE, name, WAIT_ERASED_AFTER);
  if (n <= 0 || n >= (int) sizeof g.hint_text) return HINT_ERASED.step;
  for (int i = 0; i < n; i++)
    if ((uint8_t) g.hint_text[i] < 0x20) return HINT_ERASED.step;
  const lv_font_t *font = lv_obj_get_style_text_font(g.hint, LV_PART_MAIN);
  if (text_width(font, g.hint_text, (size_t) n) > SCREEN_W - 192) return HINT_ERASED.step;
  return g.hint_text;
}

// The waiting screen's headline and next step (HintText in tab_state.h).
inline void hint_show(Hint mode) {
  if (g.hint_mode == mode || !g.hint) return;
  g.hint_mode = mode;
  HintText t = HINT_TURN_ON;
  switch (mode) {
    case H_HOTSPOT:
      t = HINT_HOTSPOT;
      t.step = with_hotspot(g.hint_text, sizeof g.hint_text, WAIT_HOTSPOT_BEFORE, WAIT_HOTSPOT_AFTER, t.step);
      break;
    case H_CONNECTING: t = HINT_CONNECTING; break;
    case H_ADD: t = HINT_ADD; break;
    case H_ERASED:
      t = HINT_ERASED;
      t.step = erased_step();
      break;
    case H_STARTING: t = HINT_STARTING; break;
    case H_WAIT_HA: t = HINT_WAIT_HA; break;
    case H_LOADING: t = HINT_LOADING; break;
    case H_PICK: t = HINT_PICK; break;
    case H_CLOSED: t = HINT_CLOSED; break;
    case H_NO_WIFI:
      t = HINT_NO_WIFI;
      t.step = with_hotspot(g.hint_text, sizeof g.hint_text, WAIT_OFF_ON_BEFORE, WAIT_HOTSPOT_AFTER, t.step);
      break;
    default: break;
  }
  set_text(g.headline, t.title, SCREEN_W - 192);
  set_text(g.hint, t.step, SCREEN_W - 192);
  set_text(g.hint2, t.more ? t.more : "", SCREEN_W - 192);
}

// The hold-to-erase mark goes once the tablet has a key again (Home
// Assistant gave back the key it kept, or a new Home Assistant its own).
inline void forget_erased() {
  if (!g.erased) return;
  g.erased = false;
  if (g.pf.erased_clear) g.pf.erased_clear();
  say(2, "The tablet has a key again; the erase mark is cleared");
}

// On a network: what Home Assistant does with this tablet right now.
//   - Greeted by TAB-1 on this connection: H_LOADING while a dashboard is on
//     its way and for HINT_PICK_MS after the greeting, then H_PICK (at once
//     after tab_clear). Never the words from before the greeting.
//   - Connected, not greeted: with a key H_STARTING for the wait (longer for
//     a tablet turned on before: Home Assistant may still be starting), then
//     H_TURN_ON; without a key (Home Assistant gives it one at once) the
//     words from before the connection for GREET_WAIT_MS, then H_TURN_ON.
//   - Not connected: H_WAIT_HA with a key, otherwise H_ERASED after the
//     hold-the-screen erase, H_ADD for a new tablet.
inline Hint hint_on_network() {
  const bool key = has_api_key();
  if (key) forget_erased();
  if (g.connected && g.session) {
    if (g.tr.bytes || staged_layout()) return H_LOADING;
    if (g.cleared || (int32_t) (now() - g.session_at) >= (int32_t) HINT_PICK_MS) return H_PICK;
    return H_LOADING;
  }
  if (g.connected) {
    const uint32_t wait = g.paired && key ? TURNED_ON_WAIT_MS : GREET_WAIT_MS;
    if ((int32_t) (now() - g.greet_from) >= (int32_t) wait) return H_TURN_ON;
    if (key) return H_STARTING;
  } else if (key) {
    return H_WAIT_HA;
  }
  return g.erased ? H_ERASED : H_ADD;
}

// Without a network: join the hotspot once it is on; until then the tablet is
// still trying a known Wi-Fi. A platform that cannot tell counts as on one.
inline Hint hint_wanted() {
  if (g.pf.network_up && !g.pf.network_up())
    return !g.pf.hotspot_on || g.pf.hotspot_on() ? H_HOTSPOT : H_CONNECTING;
  return hint_on_network();
}

// Looks at the network at most every HINT_POLL_MS, only while the waiting
// screen shows, until the set-up window closes without what the tablet needs.
// Over a dashboard this is one flag test per tick, and nothing at all once
// that window closed. Never on the touch path.
inline void hint_tick() {
  if (g.window_closed || !g.waiting || lv_obj_has_flag(g.waiting, LV_OBJ_FLAG_HIDDEN)) return;
  if (g.hint_poll_at && !due(g.hint_poll_at)) return;
  g.hint_poll_at = later(HINT_POLL_MS);
  hint_show(hint_wanted());
}

// The words of a Wi-Fi note (tab_state.h NOTE_PREFIX), with the hotspot's name.
inline const char *note_words(Note n) {
  char step[128];
  const char *s = nullptr;
  switch (n) {
    case N_CONNECTING: return NOTE_CONNECTING;
    case N_HOTSPOT:
      s = with_hotspot(step, sizeof step, WAIT_HOTSPOT_BEFORE, WAIT_HOTSPOT_AFTER, HINT_HOTSPOT.step);
      break;
    case N_OFF_ON:
      s = with_hotspot(step, sizeof step, WAIT_OFF_ON_BEFORE, WAIT_HOTSPOT_AFTER, HINT_NO_WIFI.step);
      break;
    default: return "";
  }
  snprintf(g.note_text, sizeof g.note_text, "%s%s", NOTE_PREFIX, s);
  return g.note_text;
}

// The window closed with a key, and the network is back: the note goes and
// the waiting screen says what Home Assistant does (ota_lock.h restarts the
// tablet in a moment).
inline void no_wifi_clear() {
  g.no_wifi = false;
  g.note = N_NONE;
  hint_show(hint_on_network());
  say(2, "Network back");
}

// Which note fits now. Reads the network, and the hotspot without one.
inline Note note_wanted() {
  if (g.no_wifi) {
    // A platform that cannot tell keeps this note until the restart.
    if (g.pf.network_up && g.pf.network_up()) {
      no_wifi_clear();
      return N_NONE;
    }
    return N_OFF_ON;
  }
  if (!g.pf.network_up || g.pf.network_up()) {  // a platform that cannot tell counts as on a network
    g.net_down_at = 0;
    return N_NONE;
  }
  if (!g.net_down_at) g.net_down_at = later(0);
  if (!g.pf.hotspot_on || g.pf.hotspot_on()) return N_HOTSPOT;
  return (int32_t) (now() - g.net_down_at) >= (int32_t) NOTE_CONNECTING_MS ? N_CONNECTING : N_NONE;
}

// The Wi-Fi notes over a dashboard (design 10.11). While Home Assistant is
// not connected (it is the only reason a dashboard cannot reach it that the
// tablet can fix by itself), and after the window closed with a key and no
// network, the network is read at most every HINT_POLL_MS; the note stays in
// the strip, a passing message shows over it for STRIP_MS, then it is back.
// On the waiting screen the hint says it, so the strip stays away. With Home
// Assistant connected and no note up this is a few flag tests per tick, and
// nothing of it runs on the touch path.
inline void note_tick() {
  if (!g.strip) return;
  const bool watch = g.no_wifi || (!g.connected && active_layout());
  if (!watch) {
    g.net_down_at = 0;  // the next watch looks again at once
    g.note_poll_at = 0;
    if (g.note == N_NONE && g.note_shown == N_NONE) return;
    g.note = N_NONE;
  } else if (!g.note_poll_at || due(g.note_poll_at)) {
    g.note_poll_at = later(HINT_POLL_MS);
    g.note = note_wanted();
  }
  if (g.strip_until) return;  // a passing message is up
  const Note want = active_layout() ? g.note : N_NONE;
  if (want == g.note_shown) return;
  g.note_shown = want;
  if (want)
    show_note(note_words(want));
  else
    hide_strip();
}

// The platform writes an accepted copy later (design 7.2). While that write
// runs, watch for its failure: the copy is then not stored, so the marker is
// cleared and the save runs again after CACHE_DELAY_MS.
inline void cache_watch() {
  if (!g.save_check) return;
  const bool busy = g.pf.cache_busy && g.pf.cache_busy();  // before cache_failed: a write may end in between
  if (g.pf.cache_failed && g.pf.cache_failed()) {
    g.save_check = false;
    if (!g.stored[0]) return;
    say(1, "Layout %s could not be stored in flash; trying again later", g.stored);
    g.stored[0] = 0;
    if (active_layout()) g.save_at = later(CACHE_DELAY_MS);
  } else if (!busy) {
    g.save_check = false;
  }
}

inline void cache_tick() {
  cache_watch();
  if (!g.save_at || !due(g.save_at)) return;
  Layout *a = active_layout();
  if (!a || strcmp(g.stored, a->hash) == 0) {
    g.save_at = 0;
    return;
  }
  if (a->size > CACHE_LIMIT) {
    // Not stored; forget any older copy so a reboot does not bring it back.
    if ((g.stored[0] || g.flash_copy) && g.pf.cache_erase) g.pf.cache_erase();
    g.flash_copy = false;
    g.stored[0] = 0;
    g.save_at = 0;
    return;
  }
  if (g.pf.cache_busy && g.pf.cache_busy()) {
    g.save_at = later(1000);
    return;
  }
  if (g.pf.cache_save && g.pf.cache_save(a->bytes, a->size)) {
    g.flash_copy = true;
    memcpy(g.stored, a->hash, sizeof g.stored);
    g.save_at = 0;
    g.save_check = true;
  } else {
    g.save_at = later(CACHE_DELAY_MS);
  }
}

// Left and right swipes switch pages (wrapping), as in the showcase. A swipe
// that starts on a slider is the slider's.
inline void swipe_tick() {
  lv_indev_t *in = pointer();
  Layout *a = active_layout();
  if (!in || lv_indev_get_state(in) != LV_INDEV_STATE_PRESSED) {
    g.tracking = false;
    g.press_on_slider = false;
    return;
  }
  lv_point_t p;
  lv_indev_get_point(in, &p);
  if (!g.tracking) {
    press_begin(p);
    return;
  }
  press_track(p);
  if (!a || g.swallowed || g.press_on_slider || g.q_open || g.erase_screen) return;
  const int dx = (int) (p.x - g.start.x), dy = (int) (p.y - g.start.y);
  if (abs(dx) < SWIPE_PX || abs(dx) < 2 * abs(dy)) return;
  g.swallowed = true;
  const int n = a->model->npages;
  if (n > 1) {
    int page = g.page + (dx < 0 ? 1 : -1);
    if (page < 0) page = n - 1;
    if (page >= n) page = 0;
    show_page(*a, page);
  }
  lv_indev_wait_release(in);
}

// (2) The pressed look of a card (contract 4.7): 80 ms after the touch, while
// the finger is down and has not moved 12 px, and no hold or swipe took the
// press.
inline void card_press_tick() {
  Layout *l = nullptr;
  Widget *c = pressed_card(&l);
  if (!c) return;
  const bool want = g.tracking && !g.moved && !g.swallowed && (int32_t) (now() - g.press_at) >= (int32_t) CARD_PRESS_MS;
  if (want == (c->pressed != 0)) return;
  c->pressed = want ? 1 : 0;
  paint_widget(*l, *c);
}

// (2) Whether a widget is there for a touch (contract 4.3): drawn, not
// hidden, with a rectangle. A `choice` without an option or with cells below
// 1 px has no object.
inline bool widget_there(Model &m, const Widget &w) {
  if (w.r[2] <= 0 || w.r[3] <= 0 || !vis_shows(m, w)) return false;
  return w.obj != nullptr;
}

inline bool point_in(const int16_t r[4], lv_point_t p) {
  return p.x >= r[0] && p.x < r[0] + r[2] && p.y >= r[1] && p.y < r[1] + r[3];
}

// (2) A round button and a badge take a press in their centred circle's
// square, where their object is (cards spec B.4); outside it the press is
// the card's, for a hold as for a tap.
inline bool point_in_box(const Widget &w, lv_point_t p) {
  int bx = 0, by = 0, bw = w.r[2], bh = w.r[3];
  if (w.kind == W_BUTTON) button_box(w, &bx, &by, &bw, &bh);
  if (w.kind == W_IMAGE) image_box(w, &bx, &by, &bw, &bh);
  const int16_t r[4] = {(int16_t) (w.r[0] + bx), (int16_t) (w.r[1] + by), (int16_t) bw, (int16_t) bh};
  return point_in(r, p);
}

inline bool point_on(lv_obj_t *o, lv_point_t p) {
  if (!o) return false;
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  return p.x >= a.x1 && p.x <= a.x2 && p.y >= a.y1 && p.y <= a.y2;
}

// (2) The page a hold opens, by the table "Who gets a press" of contract
// 4.3, from the point where the press began: on the page on screen, the
// topmost card there; in it the topmost widget there. None on a slider or a
// `choice` with change, or on a thermostat's buttons; a widget's own `hold`;
// else the card's. -1: none. A look through the model, so a widget with only
// `hold` need not be clickable.
inline int hold_page(Layout &l, lv_point_t p) {
  Model &m = *l.model;
  if (g.page < 0 || g.page >= (int) m.npages) return -1;
  const Page &pg = m.pages[g.page];
  const int end = (int) pg.first + (int) pg.count;
  for (int i = end - 1; i >= (int) pg.first; i--) {
    const Widget &c = m.w[i];
    if (c.kind != W_CARD || !c.obj || !point_in(c.r, p)) continue;
    for (int k = c.children; k >= 1; k--) {
      if (i + k >= end) continue;
      const Widget &w = m.w[i + k];
      if (!widget_there(m, w) || !point_in(w.r, p) || !point_in_box(w, p)) continue;
      if ((w.kind == W_SLIDER || w.kind == W_CHOICE) && w.change) return -1;
      if (w.kind == W_THERMO && w.change && (point_on(w.o2, p) || point_on(w.o3, p))) return -1;
      if (w.hold == 2) return w.hold_v;
      break;
    }
    return c.hold == 2 ? c.hold_v : -1;
  }
  return -1;
}

// (2) A press that stays down 500 ms and has not moved 12 px opens its hold
// page (contract 7.4). Nothing is sent, and LVGL drops the CLICKED of the
// release. The engine times it; LVGL's long press time is not used. Nothing
// at all while the erase watch runs, or for a press that began during it:
// that press stays an ordinary press, and its release is a tap.
inline void hold_tick() {
  if (!g.tracking || g.held || g.moved || g.swallowed || g.press_in_watch || g.erase_timer || g.q_open ||
      g.erase_screen)
    return;
  if ((int32_t) (now() - g.press_at) < (int32_t) HOLD_MS) return;
  g.held = true;  // looked at once
  Layout *a = active_layout();
  if (!a || !a->model) return;
  const int page = hold_page(*a, g.start);
  if (page < 0 || page >= (int) a->model->npages) return;
  g.swallowed = true;
  lv_indev_wait_release(pointer());
  card_press_tick();  // the pressed look goes
  show_page(*a, page);
}

inline void timer_cb(lv_timer_t *) {
  const uint32_t start = now();
  poll_connection();
  knock_tick();
  const bool built = build_tick();
  swap_tick();
  if (!built) stale_tick(start);
  delete_tick(start);
  feedback_tick();
  strip_tick();
  note_tick();
  hint_tick();
  update_mark();
  cache_tick();
  swipe_tick();
  card_press_tick();
  hold_tick();
}

// ---------------------------------------------------------------- hold to erase

// Its own timer, so the engine timer and the touch path carry nothing of it.
// It exists only while it can still matter: from begin() until ERASE_ARM_MS
// after power-on (longer only while a hold that started in time goes on),
// and while the question is open.
inline void erase_stop() {
  if (g.erase_timer) lv_timer_delete(g.erase_timer);  // also from inside its own callback
  g.erase_timer = nullptr;
  g.holding = false;
  if (g.erase_step != E_DONE) g.erase_step = E_OFF;
}

inline void erase_hold_start() {
  g.holding = true;
  g.hold_at = g.down_at = now();
  g.lifted = false;
  g.gaps = 0;
  g.gaps_ms = 0;
  lv_indev_get_point(pointer(), &g.hold_point);
}

// The finger is down again during a hold. False when this no longer counts as
// the same hold: it came back after one lift too many, after too long off the
// screen in all, or away from where it went down (taps, a swipe).
inline bool erase_still_held() {
  if (g.lifted) {
    g.lifted = false;
    g.gaps++;
    g.gaps_ms += now() - g.down_at;
    if (g.gaps > ERASE_GAPS || g.gaps_ms > ERASE_GAPS_MS) return false;
  }
  lv_point_t p;
  lv_indev_get_point(pointer(), &p);
  const int32_t dx = p.x - g.hold_point.x, dy = p.y - g.hold_point.y;
  return dx * dx + dy * dy <= ERASE_MOVE_PX * ERASE_MOVE_PX;
}

inline void erase_cb(lv_timer_t *) {
  switch (g.erase_step) {
    case E_WATCH: {
      const bool in_time = g.pf.uptime_ms() < ERASE_ARM_MS;
      if (!finger_down()) {
        // A dropped touch sample looks like a short lift: the hold goes on.
        if (g.holding && now() - g.down_at <= ERASE_GAP_MS) {
          g.lifted = true;
          return;
        }
        g.holding = false;
        if (!in_time) erase_stop();
        return;
      }
      if (g.holding && !erase_still_held()) g.holding = false;  // the count starts again here
      if (!g.holding) {
        if (!in_time) {  // a touch that starts after the window never counts
          erase_stop();
          return;
        }
        erase_hold_start();
        return;
      }
      g.down_at = now();
      if (now() - g.hold_at < ERASE_HOLD_MS) return;
      g.holding = false;
      g.erase_step = E_ASK;
      g.erase_at = now();
      lv_indev_wait_release(pointer());  // lifting the finger presses nothing
      erase_screen_open();
      say(1, "Erase question open");
      return;
    }
    case E_ASK:
      if (now() - g.erase_at < ERASE_ASK_MS) return;
      erase_screen_close();
      erase_stop();
      say(2, "Erase question cancelled by itself");
      return;
    case E_ERASING: {
      if (now() - g.erase_at < ERASE_DRAW_MS) return;
      g.erase_step = E_DONE;
      erase_stop();
      say(1, "Erasing this tablet");
      g.pf.erase_all();  // restarts the tablet; returns only in the host tests
      return;
    }
    default:
      erase_stop();
      return;
  }
}

inline void erase_answer(bool erase) {
  if (g.erase_step != E_ASK) return;
  if (!erase) {
    erase_screen_close();
    erase_stop();
    say(2, "Erase cancelled");
    return;
  }
  g.erase_step = E_ERASING;
  g.erase_at = now();
  erase_screen_busy();
}

inline void erase_begin() {
  if (!g.pf.erase_all || !g.pf.uptime_ms || g.pf.uptime_ms() >= ERASE_ARM_MS) return;
  g.erase_timer = lv_timer_create(erase_cb, ERASE_POLL_MS, nullptr);
  if (g.erase_timer) g.erase_step = E_WATCH;
}

// ---------------------------------------------------------------- boot

inline void boot_from_cache() {
  if (!g.pf.cache_size || !g.pf.cache_load) return;
  const size_t n = g.pf.cache_size();
  if (n == 0) return;
  g.flash_copy = true;
  if (n > MAX_LAYOUT) {
    if (g.pf.cache_erase) g.pf.cache_erase();
    g.flash_copy = false;
    return;
  }
  uint8_t *bytes = g.pf.alloc ? static_cast<uint8_t *>(g.pf.alloc(n + 1)) : nullptr;
  if (!bytes) return;
  if (!g.pf.cache_load(bytes, n)) {
    release_block(bytes);
    say(1, "Stored layout could not be read; erased");
    if (g.pf.cache_erase) g.pf.cache_erase();
    g.flash_copy = false;
    return;
  }
  bytes[n] = 0;
  char hash[17];
  hash_of(bytes, n, hash);
  int idx = -1;
  const char *code = stage(bytes, (uint32_t) n, hash, &idx);
  if (*code) {
    release_block(bytes);
    if (strcmp(code, "memory") == 0) {
      say(1, "No memory to show the stored layout; kept for the next start");
      return;
    }
    say(1, "Stored layout refused (%s); erased", code);
    if (g.pf.cache_erase) g.pf.cache_erase();
    g.flash_copy = false;
    return;
  }
  // Not built here: on_boot runs before the main loop, where nothing feeds
  // the task watchdog and LVGL lays nothing out between steps, so a large
  // stored layout would block boot for seconds. It is staged like a commit;
  // build_tick and swap_tick build and show it in slices behind the waiting
  // screen. `stored` names the flash copy now, so showing it writes nothing.
  g.staged = (int8_t) idx;
  memcpy(g.stored, hash, sizeof g.stored);
}

inline void teardown() {
  for (int i = 0; i < LAYOUTS; i++) {
    Layout &l = g.lay[i];
    if (l.root) lv_obj_delete(l.root);
    l.root = nullptr;
    release_layout(l);
  }
  drop_transfer();
  ui_teardown();
  if (g.timer) lv_timer_delete(g.timer);
  g.timer = nullptr;
  erase_stop();
}

// ---------------------------------------------------------------- tab_set

// Applies one value to a slot. Returns true when the slot changed.
inline bool set_slot(Slot &s, json::Reader r) {
  const json::Kind k = r.peek();
  if (k == json::K_NULL) {
    if (s.null) return false;
    s.null = 1;
    return true;
  }
  switch (s.type) {
    case 't': {
      json::Span sp;
      if (k != json::K_STRING || !r.string(&sp)) return false;
      char tmp[TEXT_CAP];
      const size_t n = json::unescape_max(sp, tmp, TEXT_CAP - 1);
      tmp[n] = 0;
      if (!s.null && strcmp(s.text, tmp) == 0) return false;
      memcpy(s.text, tmp, n + 1);
      s.null = 0;
      return true;
    }
    case 'b':
    case 'n':
    case 'i': {
      double v;
      if (k == json::K_TRUE || k == json::K_FALSE) {
        if (s.type != 'b') return false;
        v = k == json::K_TRUE ? 1 : 0;
      } else if (k != json::K_NUMBER || !r.number(&v)) {
        return false;
      }
      if (s.type == 'b') v = v != 0 ? 1 : 0;
      if (s.type == 'i' && (v < 0 || v > 0x10FFFF || v != floor(v))) return false;
      v = in_range(v);  // finite (the reader refuses others), now within +-1e9
      if (!s.null && s.num == v) return false;
      s.num = v;
      s.null = 0;
      return true;
    }
    case 'l': {
      if (k != json::K_ARRAY) return false;
      double tmp[MAX_LIST];
      uint16_t cnt = 0;
      r.begin_array();
      bool first = true;
      while (r.next_item(first)) {
        double v;
        if (r.peek() != json::K_NUMBER || !r.number(&v)) return false;
        if (cnt < MAX_LIST) tmp[cnt++] = in_range(v);
      }
      if (r.err) return false;
      if (!s.null && s.list_n == cnt && memcmp(s.list, tmp, cnt * sizeof(double)) == 0) return false;
      memcpy(s.list, tmp, cnt * sizeof(double));
      s.list_n = cnt;
      s.null = 0;
      return true;
    }
    default:
      return false;
  }
}

inline void mark_dirty(int slot) {
  g.dirty[(slot - 1) >> 3] |= (uint8_t) (1u << ((slot - 1) & 7));
  g.dirty_any = true;
}

inline void clear_dirty() {
  memset(g.dirty, 0, sizeof g.dirty);
  g.dirty_any = false;
}

}  // namespace detail

// ================================================================ interface

inline void begin(const Fonts &fonts, const Platform &platform) {
  using namespace detail;
  if (g.started) teardown();
  memset(&g, 0, sizeof g);
  g.started = true;
  g.fonts = fonts;
  g.pf = platform;
  g.active = g.staged = -1;
  g.q_layout = -1;
  g.cp_layout = -1;
  g.build_code = "";
  for (FbRef &r : g.fbl) r.layout = -1;
  g.away_since = now();
  g.erased = g.pf.erased;
  if (has_api_key()) forget_erased();  // a key at start: the erase is over
  if (g.pf.secret_load && g.pf.secret_load(g.secret)) {
    g.paired = true;
    kid_of(g.secret, g.kid);
  }
  ui_begin();
  hint_tick();
  pointer();
  g.timer = lv_timer_create(timer_cb, TICK_MS, nullptr);
  erase_begin();
  boot_from_cache();
  show_waiting(active_layout() == nullptr);
  // The uptime shows how much of the first ERASE_ARM_MS is left for the
  // erase hold (it is counted from power-on).
  const char *how = g.paired ? "paired" : g.erased ? "not paired, erased" : "not paired";
  if (g.pf.uptime_ms)
    say(2, "Engine %s started %u ms after power-on (boot %u, %s)", ENGINE_VERSION, (unsigned) g.pf.uptime_ms(),
        (unsigned) g.pf.boot, how);
  else
    say(2, "Engine %s started (boot %u, %s)", ENGINE_VERSION, (unsigned) g.pf.boot, how);
}

// `provisioning: on_timeout`: the set-up window closed before the tablet had
// everything it needs, and ESPHome now turns Home Assistant away and keeps
// the hotspot off until the next restart.
//   - With an API key, the tablet had no network for the whole window (for
//     example a new router or Wi-Fi password). ota_lock.h restarts it once
//     the network is back; until then a note says to turn the tablet off and
//     on, which brings the hotspot back, and to join it from a phone. The
//     dashboard stays.
//   - Without one, only a restart helps, so the waiting screen says so. It also
//     covers a stored dashboard (a tablet Home Assistant removed restarts with
//     its old one): that dashboard can no longer reach Home Assistant. The
//     stored copy in flash stays for the next start.
inline void setup_window_closed() {
  using namespace detail;
  if (!g.started) return;
  g.window_closed = true;  // the hint no longer looks at the network
  if (has_api_key()) {
    g.no_wifi = true;
    g.note = N_OFF_ON;  // over a dashboard at once; note_tick looks at the network from now on
    g.note_poll_at = later(HINT_POLL_MS);
    hint_show(H_NO_WIFI);
    note_tick();
    say(1, "Set-up window closed with no network: hotspot off until a restart; restarting once the network is back");
    return;
  }
  hint_show(H_CLOSED);
  drop_transfer();
  if (g.staged >= 0) retire(g.staged);
  if (g.active >= 0) retire(g.active);
  g.build_code = "";
  question_close();
  show_waiting(true);
  say(1, "Set-up window closed without a key; restart the tablet to finish setup");
}

inline size_t on_hello(const char *nonce, size_t nonce_len, const char *auth, size_t auth_len, char *out, size_t cap) {
  using namespace detail;
  if (!out || cap == 0) return 0;
  out[0] = 0;
  if (!g.started) return 0;
  poll_connection();
  g.knocking = false;
  if (!g.connected) {
    g.early = true;
    g.early_at = now();
  }
  bool fresh = false;
  if (nonce && crypto::is_hex(nonce, nonce_len, 32) && !nonce_used(nonce) &&
      check_auth(auth, auth_len, "tab_hello", nonce, nullptr, 0)) {
    fresh = true;
    nonce_remember(nonce);
    g.session = true;
    g.session_at = now();
    g.hint_poll_at = 0;  // the waiting screen drops what it said before the greeting at the next tick
    memcpy(g.nonce, nonce, 32);
    g.nonce[32] = 0;
    g.counter = 0;
    g.last_n = 0;
    forget_pending();
  }
  const bool enc = has_api_key();
  if (enc) forget_erased();
  Out o{out, cap, 0, true};
  o.raw("{\"boot\":");
  o.num(g.pf.boot);
  o.raw(",\"caps\":{\"icons\":2,\"max_chunk\":");
  o.num(MAX_CHUNK);
  o.raw(",\"max_layout\":");
  o.num(MAX_LAYOUT);
  o.raw(",\"max_pages\":");
  o.num(MAX_PAGES);
  o.raw(",\"max_slots\":");
  o.num(MAX_SLOTS);
  o.raw(",\"max_stored\":");
  o.num(CACHE_LIMIT);
  o.raw(",\"max_widgets\":");
  o.num(MAX_WIDGETS);
  o.raw(",\"screen\":[1280,800],\"w\":[\"heading\",\"card\",\"label\",\"button\",\"switch\",\"slider\","
        "\"thermostat\",\"chart\",\"image\",\"choice\"]},\"enc\":");
  o.num(enc ? 1 : 0);
  o.raw(",\"engine\":");
  o.str(ENGINE_VERSION);
  o.raw(",\"kid\":");
  o.str(g.paired && enc ? g.kid : "");
  o.raw(",\"layout\":");
  o.str(hash_or_empty(active_layout()));
  o.raw(",");
  write_mem(o);
  o.raw(",\"proto\":[");
  o.num(PROTO_LOW);
  o.raw(",");
  o.num(PROTO_HIGH);
  o.raw("],\"session\":");
  o.num(fresh ? 1 : 0);
  o.raw("}");
  if (!o.ok) {
    out[0] = 0;
    return 0;
  }
  return o.n;
}

inline size_t on_status(char *out, size_t cap) {
  using namespace detail;
  if (!out || cap == 0) return 0;
  out[0] = 0;
  if (!g.started) return 0;
  poll_connection();
  g.knocking = false;
  if (!g.connected) {
    g.early = true;
    g.early_at = now();
  }
  Out o{out, cap, 0, true};
  o.raw("{\"build\":");
  o.str(g.build_code ? g.build_code : "");
  o.raw(",\"layout\":");
  o.str(hash_or_empty(active_layout()));
  o.raw(",");
  write_mem(o);
  o.raw(",\"session\":");  // HA reads this before it answers a knock (contract 2.2)
  o.num(g.session ? 1 : 0);
  o.raw(",\"staged\":");
  o.str(hash_or_empty(staged_layout()));
  o.raw("}");
  if (!o.ok) {
    out[0] = 0;
    return 0;
  }
  return o.n;
}

inline const char *on_pair(const char *nonce, size_t nonce_len, const char *wrapped, size_t wrapped_len,
                           const char *kid, size_t kid_len) {
  using namespace detail;
  if (!g.started) return "bad_key";
  poll_connection();
  uint8_t psk[32];
  if (!g.pf.api_key || !g.pf.api_key(psk)) return "needs_encryption";
  if (!nonce || !wrapped || !kid || !crypto::is_hex(nonce, nonce_len, 32) || !crypto::is_hex(wrapped, wrapped_len, 64) ||
      !crypto::is_hex(kid, kid_len, 16) || (g.session && memcmp(nonce, g.nonce, 32) == 0)) {
    memset(psk, 0, sizeof psk);
    return "bad_key";
  }
  crypto::Hmac m;
  crypto::hmac_init(m, psk, 32);
  crypto::hmac_update_str(m, "apollo-tab1 wrap v1|");
  crypto::hmac_update_str(m, mac());
  crypto::hmac_update_str(m, "|");
  crypto::hmac_update(m, nonce, 32);
  uint8_t pad[32];
  crypto::hmac_final(m, pad);
  memset(psk, 0, sizeof psk);
  memset(&m, 0, sizeof m);
  uint8_t secret[32];
  crypto::from_hex(wrapped, 32, secret);
  for (int i = 0; i < 32; i++) secret[i] ^= pad[i];
  memset(pad, 0, sizeof pad);
  char k[17];
  kid_of(secret, k);
  if (!crypto::equal(k, kid, 16)) {
    memset(secret, 0, sizeof secret);
    return "bad_key";
  }
  if (g.pf.secret_save && !g.pf.secret_save(secret)) say(0, "Pairing secret could not be stored; it lasts until reboot");
  memcpy(g.secret, secret, 32);
  memset(secret, 0, sizeof secret);
  memcpy(g.kid, k, sizeof g.kid);
  g.paired = true;
  g.session = false;      // pairing ends the session
  g.greet_from = now();  // TAB-1 greets again right after it paired
  g.last_n = 0;
  g.counter = 0;
  forget_pending();
  say(2, "Paired, key id %s", g.kid);
  return "";
}

inline const char *on_layout(const char *hash, size_t hash_len, int seq, int total, int size, const char *data,
                             size_t data_len) {
  using namespace detail;
  if (!g.started) return "memory";
  poll_connection();
  if (seq == 0) {
    // A new transfer: drop the unfinished one and any build in progress, also
    // when the header turns out to be wrong. Header errors are checked before
    // anything is allocated, in this order (contract 5): too_big, then order
    // (hash not 16 lower-case hex, or total below 1), then size (size below 1,
    // more data than size, or a chunk over MAX_CHUNK), then memory.
    drop_transfer();
    if (g.staged >= 0) retire(g.staged);
    g.build_code = "";
    if (size > (int) MAX_LAYOUT) return "too_big";
    if (!hash || !crypto::is_hex(hash, hash_len, 16) || total < 1) return "order";
    if (size < 1 || data_len > (size_t) size || data_len > MAX_CHUNK) return "size";
    uint8_t *bytes = g.pf.alloc ? static_cast<uint8_t *>(g.pf.alloc((size_t) size + 1)) : nullptr;
    if (!bytes) return "memory";
    g.tr.bytes = bytes;
    memcpy(g.tr.hash, hash, 16);
    g.tr.hash[16] = 0;
    g.tr.total = total;
    g.tr.size = size;
    g.tr.next = 1;
    g.tr.got = (uint32_t) data_len;
    if (data_len) memcpy(bytes, data, data_len);
    bytes[g.tr.got] = 0;
    g.tr.done = g.tr.next == total && g.tr.got == (uint32_t) size;
    return "";
  }
  if (!g.tr.bytes || seq != g.tr.next || seq >= g.tr.total || total != g.tr.total || size != g.tr.size || !hash ||
      hash_len != 16 || memcmp(hash, g.tr.hash, 16) != 0)
    return "order";
  // A chunk over MAX_CHUNK (caps.max_chunk), or more bytes than announced,
  // ends the transfer (contract 5).
  if (data_len > MAX_CHUNK || g.tr.got + data_len > (size_t) g.tr.size) {
    drop_transfer();
    return "size";
  }
  if (data_len) memcpy(g.tr.bytes + g.tr.got, data, data_len);
  g.tr.got += (uint32_t) data_len;
  g.tr.bytes[g.tr.got] = 0;
  g.tr.next++;
  g.tr.done = g.tr.next == g.tr.total && g.tr.got == (uint32_t) g.tr.size;
  return "";
}

inline const char *on_commit(const char *hash, size_t hash_len, const char *auth, size_t auth_len) {
  using namespace detail;
  if (!g.started) return "memory";
  poll_connection();
  if (!check_auth(auth, auth_len, "tab_commit", g.nonce, hash, hash ? hash_len : 0)) return "auth";
  const bool shaped = hash && crypto::is_hex(hash, hash_len, 16);
  Layout *a = active_layout();
  if (shaped && a && memcmp(a->hash, hash, 16) == 0) {
    if (g.tr.bytes && memcmp(g.tr.hash, hash, 16) == 0) drop_transfer();
    return "";  // already on screen: nothing changes
  }
  const bool finished = shaped && g.tr.bytes && g.tr.done && memcmp(g.tr.hash, hash, 16) == 0;
  Layout *s = staged_layout();
  // A repeated commit of the hash that is already staged (HA lost the first
  // answer and asks again): success, and the build carries on unchanged.
  if (!finished && shaped && s && memcmp(s->hash, hash, 16) == 0) return "";
  if (!finished) return "incomplete";
  char got[17];
  hash_of(g.tr.bytes, g.tr.size, got);
  if (memcmp(got, hash, 16) != 0) {
    drop_transfer();
    return "hash";
  }
  int idx = -1;
  const char *code = stage(g.tr.bytes, (uint32_t) g.tr.size, got, &idx);
  if (*code) {
    drop_transfer();
    return code;
  }
  g.tr.bytes = nullptr;  // the layout owns it now
  drop_transfer();
  if (g.staged >= 0) retire(g.staged);
  g.staged = (int8_t) idx;
  g.build_code = "";
  g.cleared = false;
  say(2, "Layout %s committed (%u bytes), building", g.lay[idx].hash, (unsigned) g.lay[idx].size);
  return "";
}

inline const char *on_set(const char *data, size_t data_len, const char *auth, size_t auth_len) {
  using namespace detail;
  if (!g.started) return "memory";
  poll_connection();
  if (!check_auth(auth, auth_len, "tab_set", g.nonce, data, data ? data_len : 0)) return "auth";
  if (!data) return "parse";
  {
    json::Reader whole(data, data_len);
    if (whole.peek() != json::K_OBJECT || !whole.skip() || !whole.at_end()) return "parse";
  }
  static constexpr const char *KEYS[3] = {"h", "f", "v"};
  static constexpr KeyIndex SET_INDEX(KEYS, 3);
  const char *at[3];
  json::Reader r(data, data_len);
  if (!scan_object(r, SET_INDEX, at) || !at[0]) return "parse";
  auto reader_at = [&](const char *p) { return json::Reader(p, (size_t) (data + data_len - p)); };
  json::Span h;
  {
    json::Reader hr = reader_at(at[0]);
    if (!hr.string(&h)) return "parse";
  }
  bool full = false;
  if (at[1]) {
    json::Reader fr = reader_at(at[1]);
    double f;
    if (fr.peek() == json::K_NUMBER && fr.number(&f)) full = f == 1;
  }
  if (at[2] && reader_at(at[2]).peek() != json::K_ARRAY && reader_at(at[2]).peek() != json::K_NULL) return "parse";
  Layout *l = nullptr;
  Layout *a = active_layout(), *s = staged_layout();
  if (!h.escaped && h.n == 16) {
    if (a && memcmp(a->hash, h.p, 16) == 0)
      l = a;
    else if (s && memcmp(s->hash, h.p, 16) == 0)
      l = s;
  }
  if (!l) return "hash";
  Model &m = *l->model;
  clear_dirty();
  if (full) {
    for (uint32_t i = 0; i < m.nslots; i++) {
      if (!m.slots[i].null) mark_dirty((int) i + 1);
      m.slots[i].null = 1;
    }
  }
  if (at[2] && reader_at(at[2]).peek() == json::K_ARRAY) {
    json::Reader vr = reader_at(at[2]);
    vr.begin_array();
    bool first = true;
    while (vr.next_item(first)) {
      json::Reader pr = vr;
      if (!vr.skip()) break;
      if (pr.peek() != json::K_ARRAY) continue;
      pr.begin_array();
      bool pfirst = true;
      if (!pr.next_item(pfirst)) continue;
      double sv;
      if (pr.peek() != json::K_NUMBER || !pr.number(&sv)) continue;
      if (!pr.next_item(pfirst)) continue;
      json::Reader value = pr;
      if (!pr.skip()) continue;
      if (pr.next_item(pfirst)) continue;  // more than two items: skipped
      if (sv != floor(sv) || sv < 1 || sv > m.nslots) continue;
      const int slot = (int) sv;
      if (set_slot(m.slots[slot - 1], value)) mark_dirty(slot);
    }
  }
  if (g.dirty_any) {
    if (l->state == L_ACTIVE) values_arrived(*l);
    paint_dirty(*l);
  }
  clear_dirty();
  return "";
}

inline void on_ack(int n, bool ok, const char *code, size_t code_len, const char *auth, size_t auth_len) {
  using namespace detail;
  if (!g.started) return;
  poll_connection();
  if (code_len > 64 || (code_len && !code)) return;
  char body[96];
  const int bn = snprintf(body, sizeof body, "%d|%c|", n, ok ? '1' : '0');
  if (bn <= 0) return;
  if (code_len) memcpy(body + bn, code, code_len);
  if (!check_auth(auth, auth_len, "tab_ack", g.nonce, body, (size_t) bn + code_len)) return;
  Pending *p = nullptr;
  for (Pending &x : g.pend)
    if (x.used && (int) x.counter == n) p = &x;
  if (!p) return;
  p->used = 0;
  const bool denied = span_is(code, code_len, "denied");
  const bool confirm = span_is(code, code_len, "confirm");
  Layout *l = layout_at(p->layout);
  Widget *w = (l && l->state == L_ACTIVE && l->gen == p->gen && l->model && p->widget < l->model->nw)
                  ? &l->model->w[p->widget]
                  : nullptr;
  if (w) {
    if (ok && !denied && !confirm) {
      if (w->ovr == 1) {
        w->ovr = 2;
        if (w->fbp != 2) w->ovr_until = now() + ACK_TIMEOUT_MS;
      }
      if (w->fb == 1 && w->fbp != 2) w->fb = 0;
    } else {
      w->ovr = 0;
      if (w->fb == 1) w->fb = 0;
    }
    feedback_paint(*l, *w);
  }
  if (denied) show_strip("Not allowed on this tablet");
  if (confirm && w) question_open(*l, *w, p->event, p->has_value != 0, p->value);
}

inline const char *on_clear(const char *auth, size_t auth_len) {
  using namespace detail;
  if (!g.started) return "memory";
  poll_connection();
  if (!check_auth(auth, auth_len, "tab_clear", g.nonce, nullptr, 0)) return "auth";
  drop_transfer();
  if (g.staged >= 0) retire(g.staged);
  if (g.active >= 0) retire(g.active);
  g.build_code = "";
  g.page = 0;
  question_close();
  show_waiting(true);
  g.save_at = 0;
  g.stored[0] = 0;
  if (g.pf.cache_erase) g.pf.cache_erase();
  g.flash_copy = false;
  g.cleared = true;      // the waiting screen says there is nothing to show
  g.hint_poll_at = 0;    // at the next tick
  say(2, "Layout cleared");
  return "";
}

// ---------------------------------------------------------------- diagnostics

inline const char *active_hash() { return detail::hash_or_empty(detail::active_layout()); }
inline const char *staged_hash() { return detail::hash_or_empty(detail::staged_layout()); }
inline bool building() { return detail::staged_layout() != nullptr; }

inline int page_count() {
  detail::Layout *a = detail::active_layout();
  return a ? a->model->npages : 0;
}

inline int current_page() { return detail::active_layout() ? detail::g.page : 0; }

inline bool erase_asking() { return detail::g.erase_step == detail::E_ASK && detail::g.erase_screen != nullptr; }

inline void go_to(int page) {
  detail::Layout *a = detail::active_layout();
  if (a) detail::show_page(*a, page);
}

inline int slot_count() {
  detail::Layout *a = detail::active_layout();
  return a ? a->model->nslots : 0;
}

namespace detail {
inline Slot *active_slot(int slot) {
  Layout *a = active_layout();
  if (!a || slot < 1 || slot > a->model->nslots) return nullptr;
  return &a->model->slots[slot - 1];
}
}  // namespace detail

inline char slot_type(int slot) {
  detail::Slot *s = detail::active_slot(slot);
  return s ? s->type : 0;
}

inline bool slot_null(int slot) {
  detail::Slot *s = detail::active_slot(slot);
  return !s || s->null;
}

inline bool slot_text(int slot, char *out, size_t cap) {
  detail::Slot *s = detail::active_slot(slot);
  if (!s || s->null || s->type != 't' || !out || cap == 0) return false;
  size_t n = strlen(s->text);
  if (n >= cap) n = cap - 1;
  memcpy(out, s->text, n);
  out[n] = 0;
  return true;
}

inline bool slot_bool(int slot, bool *out) {
  detail::Slot *s = detail::active_slot(slot);
  if (!s || s->null || s->type != 'b') return false;
  if (out) *out = s->num != 0;
  return true;
}

inline bool slot_number(int slot, double *out) {
  detail::Slot *s = detail::active_slot(slot);
  if (!s || s->null || s->type != 'n') return false;
  if (out) *out = s->num;
  return true;
}

inline bool slot_icon(int slot, uint32_t *out) {
  detail::Slot *s = detail::active_slot(slot);
  if (!s || s->null || s->type != 'i') return false;
  if (out) *out = (uint32_t) s->num;
  return true;
}

inline int slot_list(int slot, double *out, int cap) {
  detail::Slot *s = detail::active_slot(slot);
  if (!s || s->null || s->type != 'l') return -1;
  int n = s->list_n;
  if (n > cap) n = cap < 0 ? 0 : cap;
  if (out && n > 0) memcpy(out, s->list, (size_t) n * sizeof(double));
  return n;
}

}  // namespace tab

#endif  // TAB_CORE_H
