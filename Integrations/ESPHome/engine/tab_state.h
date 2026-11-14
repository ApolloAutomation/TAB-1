// The engine's fixed state, value access and colour roles. Every function
// that tab_ui.h and tab_core.h call across each other is declared here, so the
// headers can be included in any order.
//
// tab_engine.h comes first and outside the guard: it includes tab_core.h,
// which includes this file, so the interface types exist before this body.
#include "tab_engine.h"

#ifndef TAB_STATE_H
#define TAB_STATE_H

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "tab_crypto.h"
#include "tab_json.h"
#include "tab_model.h"

namespace tab {
namespace detail {

static const char *const ENGINE_VERSION = "0.2.0";
// The highest contract version this build tells Home Assistant it speaks.
// It stays 1 until the engine draws all of version 2: a build from the
// middle of that work must get version 1 layouts, which it draws as engine
// 0.1.0 did (cards spec B.8). The host tests and a bench build set it to 2.
#ifndef TAB_PROTO_HIGH
#define TAB_PROTO_HIGH 1
#endif
static const int PROTO_LOW = 1, PROTO_HIGH = TAB_PROTO_HIGH;
// Field 1 of the input payload and of the knock: `input_version` in
// contract.json, 1 in both contract versions. It is not `proto`.
static const int INPUT_VERSION = 1;
static const uint32_t MAX_CHUNK = 16000;
static const uint32_t CACHE_LIMIT = 128 * 1024;  // larger layouts are not stored
static const size_t MEMORY_MARGIN = 1024 * 1024; // PSRAM kept free during a build
static const uint32_t OBJECT_BYTES = 400;        // estimate per LVGL object
static const uint32_t TICK_MS = 5;
static const uint32_t SLICE_MS = 4;
static const uint32_t SLICE_OBJECTS = 48;        // objects per build slice at most
static const uint32_t DELETE_OBJECTS = 96;       // objects per delete slice at most
static const uint32_t STALE_WIDGETS = 16;        // stale widgets painted per slice at most
static const uint32_t SWAP_WAIT_MS = 3000;       // longest wait for a finger to lift
// The strip when that wait ends during a drag on a slider that asks first (contract 7.4).
static const char *const SWAP_DRAG_TEXT = "The dashboard changed. Try again";
static const uint32_t ACK_TIMEOUT_MS = 4000;
static const uint32_t PROGRESS_TIMEOUT_MS = 30000;
static const uint32_t CONFIRM_TIMEOUT_MS = 10000;
static const uint32_t PULSE_MS = 300;
static const uint32_t STRIP_MS = 2500;
static const uint32_t AWAY_MARK_MS = 10000;
static const uint32_t CACHE_DELAY_MS = 30000;
static const uint32_t EARLY_WINDOW_MS = 30000;  // a call before the state subscription counts this long
static const int KNOCK_FIRST_S = 3, KNOCK_REPEATS = 5, KNOCK_MAX_S = 300;
static const int SWIPE_PX = 48;
// (2) A press that has moved this far from its first point is no longer a
// card tap and no longer a hold, and stays so (contract 4.3): dx*dx + dy*dy.
static const int32_t MOVED_PX2 = 12 * 12;
static const uint32_t CARD_PRESS_MS = 80;  // a card shows its pressed look this long after the touch
static const uint32_t HOLD_MS = 500;       // a press held this long opens the `hold` page
// Hold-to-erase (design 10.8): a touch that starts within ERASE_ARM_MS of
// power-on and stays down ERASE_HOLD_MS asks whether to erase the tablet.
// After ERASE_ARM_MS nothing of this runs on the touch path any more.
static const uint32_t ERASE_ARM_MS = 10000;
static const uint32_t ERASE_HOLD_MS = 5000;
static const uint32_t ERASE_ASK_MS = 30000;  // the question cancels itself
// Fast enough to see the lift between two taps: a finger is off the screen
// for 30 ms or more even at 10 taps a second. Only during the first
// ERASE_ARM_MS (and while asking), never on the touch path.
static const uint32_t ERASE_POLL_MS = 10;
// A touch controller or its bus can drop a sample now and then, which reads as
// a short lift. A lift up to ERASE_GAP_MS keeps the hold and its 5 s count,
// but only a few of them: at most ERASE_GAPS per hold, together at most
// ERASE_GAPS_MS (each counted from the last poll that saw the finger to the
// first that sees it again), and the finger must come back within
// ERASE_MOVE_PX of where it went down. Otherwise the count starts again, so
// taps never add up to a hold.
static const uint32_t ERASE_GAP_MS = 200;
static const uint8_t ERASE_GAPS = 3;
static const uint32_t ERASE_GAPS_MS = 600;
static const int32_t ERASE_MOVE_PX = 60;
static const uint32_t ERASE_DRAW_MS = 150;   // "Erasing…" is on screen before the erase starts
static const char *const ERASE_TITLE = "Erase this tablet?";
static const char *const ERASE_TEXT = "It forgets its Wi-Fi, Home Assistant and dashboard.";
static const char *const ERASE_BUSY = "Erasing\xE2\x80\xA6";
// The waiting screen says where the tablet is (a headline) and what to do
// next (the line under it), in the words of Home Assistant and the TAB-1
// panel (sidebar "TAB-1", Settings > Devices & services, button "Turn on with
// this dashboard", status "Nothing to show yet"). It follows what Home
// Assistant does right now: whether it is connected to the tablet (a
// connection that follows its states, Platform.ha_connected) and whether
// TAB-1 greeted it on that connection (a tab_hello that started a session).
// One pair per state (enum Hint), picked by hint_wanted() in tab_core.h:
//   H_HOTSPOT     no network, set-up hotspot on: join it from a phone, by its
//                 name (Platform.hotspot) between the two parts;
//   H_CONNECTING  no network, hotspot not on yet (a known Wi-Fi is tried
//                 first; ESPHome turns the hotspot on after 90 s without it);
//   H_ADD         on a network, no API key, never erased, Home Assistant not
//                 connected: a new tablet, which Home Assistant lists by
//                 itself in Settings > Devices & services;
//   H_ERASED      the same after the hold-the-screen erase (Platform.erased,
//                 until a key comes): the Home Assistant that had the tablet
//                 never offers it as new, it asks in Repairs, "Authentication
//                 expired for <name>", with the name it gave the tablet, which
//                 is the friendly name the screen shows above. The step names
//                 exactly that question (any device can make Home Assistant
//                 ask one for another tablet), and the second line covers a
//                 Home Assistant that never had the tablet (a new home);
//   H_STARTING    Home Assistant connected, the tablet has its key, TAB-1 has
//                 not greeted it yet: GREET_WAIT_MS for a tablet never turned
//                 on, TURNED_ON_WAIT_MS for one turned on before (TAB-1 greets
//                 nobody while Home Assistant starts);
//   H_TURN_ON     Home Assistant connected, no greeting after that wait (a
//                 tablet without a key: after GREET_WAIT_MS): TAB-1 greets
//                 only tablets an admin turned on, and the panel shows this
//                 one as new with its "Turn on with this dashboard";
//   H_WAIT_HA     with an API key, Home Assistant not connected: it is not
//                 running, or it does not list the tablet (deleted there);
//   H_LOADING     greeted on this connection, a dashboard on its way, or
//                 HINT_PICK_MS not over yet;
//   H_PICK        greeted, nothing to show: tab_clear, or no dashboard for
//                 HINT_PICK_MS after the greeting (the panel says why: none
//                 made yet, none picked, or one too big for the tablet);
//   H_CLOSED      the set-up window closed without a key;
//   H_NO_WIFI     the window closed on a tablet with a key that had no network
//                 for all of it: ESPHome keeps the hotspot off until a restart,
//                 so the step names the hotspot to join after it.
struct HintText {
  const char *title;
  const char *step;
  const char *more;  // a second line under the step, or nullptr
};
static const char *const WAIT_HOTSPOT_BEFORE = "Join the Wi-Fi \xE2\x80\x9C";
static const char *const WAIT_HOTSPOT_AFTER = "\xE2\x80\x9D from a phone";
static const char *const WAIT_OFF_ON_BEFORE = "Turn the tablet off and on, then join \xE2\x80\x9C";  // + name + WAIT_HOTSPOT_AFTER
// The steps of HINT_HOTSPOT and HINT_NO_WIFI are for a platform without the
// hotspot's name; with one, hint_show() puts it between BEFORE and AFTER.
static const HintText HINT_HOTSPOT = {"Set up Wi-Fi", "Join the tablet\xE2\x80\x99s Wi-Fi hotspot from a phone", nullptr};
static const HintText HINT_CONNECTING = {"Connecting to Wi-Fi\xE2\x80\xA6",
                                         "If it cannot connect, its set-up hotspot comes on within 2 minutes", nullptr};
static const HintText HINT_ADD = {"Add this tablet in Home Assistant",
                                  "It shows up by itself in Settings > Devices & services", nullptr};
// The H_ERASED step is WAIT_ERASED_BEFORE + the tablet's name +
// WAIT_ERASED_AFTER; HINT_ERASED.step only when that does not fit on one line
// (a long name: its end, which tells two tablets apart, would be cut) or there
// is no name. contract/screens.json "erased" holds the same words.
static const char *const WAIT_ERASED_BEFORE =
    "In Settings > System > Repairs, confirm only \xE2\x80\x9C" "Authentication expired for ";
static const char *const WAIT_ERASED_AFTER = "\xE2\x80\x9D";
static const HintText HINT_ERASED = {
    "Confirm this tablet in Home Assistant",
    "In Settings > System > Repairs, confirm only \xE2\x80\x9C" "Authentication expired\xE2\x80\x9D for the name above",
    "If this Home Assistant never had this tablet, add it in Settings > Devices & services"};
static const HintText HINT_STARTING = {
    "Waiting for TAB-1 in Home Assistant",
    "If Home Assistant has finished starting, open TAB-1 there and turn this tablet on", nullptr};
static const HintText HINT_TURN_ON = {"Ready to turn on", "Open TAB-1 in Home Assistant and turn this tablet on", nullptr};
static const HintText HINT_WAIT_HA = {
    "Waiting for Home Assistant",
    "Check that Home Assistant is running and lists this tablet in Settings > Devices & services", nullptr};
static const HintText HINT_LOADING = {"Connected to Home Assistant", "Getting the dashboard\xE2\x80\xA6", nullptr};
static const HintText HINT_PICK = {"Nothing to show yet",
                                   "Open TAB-1 in Home Assistant to see what this tablet should show", nullptr};
static const HintText HINT_CLOSED = {"Setup not finished", "Restart the tablet to finish setup", nullptr};
static const HintText HINT_NO_WIFI = {"No Wi-Fi", "Turn the tablet off and on to set up Wi-Fi", nullptr};
static const uint32_t HINT_POLL_MS = 500;
// After a greeting with nothing to show, wait this long for a dashboard
// before saying there is none (HA sends one right after the greeting).
static const uint32_t HINT_PICK_MS = 3000;
// Once Home Assistant has started, TAB-1 greets a tablet it knows within
// about 3 s of the connection (it looks 0.2, 1 and 3 s after it, and answers
// the knocks at 3 and 6 s). While Home Assistant starts, TAB-1 greets nobody
// until its own start is done, which can take minutes after ESPHome
// connected, so a tablet turned on before waits TURNED_ON_WAIT_MS before it
// says to turn it on; one never turned on (or without a key) GREET_WAIT_MS.
static const uint32_t GREET_WAIT_MS = 8000;
static const uint32_t TURNED_ON_WAIT_MS = 300000;
// The Wi-Fi notes over a dashboard, in the message strip, which then stays
// (a passing message shows over it for a moment). Each is NOTE_PREFIX and a
// waiting screen step, but for the hotspot that is not on yet, which only
// shows after NOTE_CONNECTING_MS without a network (Wi-Fi often comes back
// sooner). Only while Home Assistant is not connected (design 10.11).
static const char *const NOTE_PREFIX = "No Wi-Fi. ";
static const char *const NOTE_CONNECTING = "No Wi-Fi. Its set-up hotspot comes on within 2 minutes";
static const uint32_t NOTE_CONNECTING_MS = 20000;
// The message strip: centred over the dashboard, as wide as its text needs
// between these (a longer text is cut with "…").
static const int STRIP_MIN_W = 560, STRIP_MAX_W = 1088;

// Icon code points from icons.json (level 1). check_icons in the host tests
// compares them with the contract copy.
static const uint32_t ICON_VIEW_DASHBOARD = 984430;  // mdi:view-dashboard
static const uint32_t ICON_LAN_DISCONNECT = 983833;  // mdi:lan-disconnect
static const uint32_t ICON_MINUS = 983924;           // mdi:minus
static const uint32_t ICON_PLUS = 984085;            // mdi:plus
static const uint32_t ICON_PLACEHOLDER = 985462;     // mdi:image-outline
static const uint32_t ICON_FALLBACK = 984613;        // mdi:help-circle-outline
static const uint32_t ICON_TABLET = 986830;          // mdi:tablet-dashboard

// Colours of the engine's own screens before any layout arrives: the
// editor's defaults (styles.js).
static const uint32_t DEF_WALL = 0x101418, DEF_CARD = 0x181C23, DEF_CARD2 = 0x232833, DEF_TX = 0xE8EAED,
                      DEF_TX2 = 0x9AA0A8, DEF_ACC = 0xF5B547;

enum LayoutState : uint8_t { L_FREE = 0, L_BUILDING, L_READY, L_ACTIVE, L_RETIRING };

// Hold-to-erase: off, watching for the hold, asking, erasing, done.
enum EraseStep : uint8_t { E_OFF = 0, E_WATCH, E_ASK, E_ERASING, E_DONE };

// What the waiting screen says (see HintText). H_NONE: nothing yet.
enum Hint : uint8_t {
  H_NONE = 0,
  H_TURN_ON,
  H_HOTSPOT,
  H_CONNECTING,
  H_CLOSED,
  H_NO_WIFI,
  H_WAIT_HA,
  H_PICK,
  H_ADD,
  H_ERASED,
  H_STARTING,
  H_LOADING
};

// Which Wi-Fi note the message strip holds over a dashboard (NOTE_PREFIX).
enum Note : uint8_t { N_NONE = 0, N_CONNECTING, N_HOTSPOT, N_OFF_ON };

struct Layout {
  uint8_t state;
  uint8_t gen;
  char hash[17];
  uint8_t *bytes;   // the received device layout, never written after the transfer
  uint32_t size;
  Model *model;     // the second block
  lv_obj_t *root;
  lv_obj_t *bar;
  uint16_t b_item;   // build progress: next page item (widget record)
  uint16_t b_child;  // next widget of the card at b_item; 0: the card itself
  uint8_t b_stage;   // 0 root, 1 items, 2 values, 3 done
  uint32_t ready_at;
  uint16_t d_page;   // delete progress
  uint16_t stale;    // widgets with a stale mark (shown layout only, cards spec B.4)
  uint16_t s_page;   // the page the next stale slice starts on
};

// What one slice may still spend: objects, and milliseconds since start.
struct Budget {
  uint32_t objects;
  uint32_t start;
  uint32_t ms;
  bool left() const;
};

struct Transfer {
  uint8_t *bytes;
  char hash[17];
  int total, size, next;
  uint32_t got;
  bool done;
};

// A sent input that waits for its tab_ack.
struct Pending {
  uint32_t counter;
  uint32_t at;
  uint16_t widget;
  int8_t layout;
  uint8_t gen;
  uint8_t policy;
  uint8_t used;
  char event;         // 't' or 'c', to send again after an ack "confirm"
  uint8_t has_value;
  double value;
};

// Widgets that show touch feedback, so the timer looks only at those.
struct FbRef {
  int8_t layout;
  uint8_t gen;
  uint16_t widget;
};

static const int LAYOUTS = 6;
static const int NONCE_RING = 16;  // session nonces kept per boot (contract 3)
static const int PENDING = 8;
static const int FB_REFS = 16;

struct State {
  bool started;
  Fonts fonts;
  Platform pf;
  // Pairing and session (contract 3 and 6).
  bool paired;
  uint8_t secret[32];
  char kid[17];
  bool session;
  char nonce[33];
  uint32_t last_n;
  uint32_t counter;
  uint8_t nonces[NONCE_RING][16];  // session nonces of this boot, a ring
  uint8_t nonces_n, nonces_next;
  // Connection and knock (contract 2.2).
  bool connected;
  bool early;  // tab_hello or tab_status arrived while ha_connected() was false
  uint32_t early_at;
  uint32_t away_since;
  bool knocking;
  uint32_t knock_next;
  uint16_t knock_gap_s;
  uint8_t knock_count;
  uint32_t touch_knock_at;  // the last knock a touch without a session sent; 0: none
  // Layouts.
  Layout lay[LAYOUTS];
  int8_t active, staged;
  uint8_t gen_next;
  Transfer tr;
  const char *build_code;
  // The largest free PSRAM block read when the layout being built was
  // staged. tab_status and tab_hello report it during the build: asking the
  // heap walks all of it (contract 3.1).
  size_t staged_block;
  bool obj_fail;  // LVGL could not create an object during this build slice
  int page;
  // Flash cache.
  char stored[17];
  uint32_t save_at;  // 0: nothing scheduled
  bool save_check;   // cache_save accepted a copy; its write has not been seen to end
  // Flash may hold a stored copy, also one `stored` does not name: one
  // kept at boot because it could not be shown, or a write that failed.
  bool flash_copy;
  // Screens.
  lv_obj_t *screen, *base, *overlay, *waiting, *mark;
  lv_obj_t *headline, *hint, *hint2;  // the waiting screen's state, next step and its second line
  lv_obj_t *strip, *strip_label;
  lv_obj_t *question, *q_box, *q_text, *q_no, *q_yes;
  uint32_t strip_until;  // a passing message hides then; 0: none
  int16_t strip_w;       // the strip's width now
  bool no_wifi;          // the window closed with a key and no network (N_OFF_ON until it is back)
  bool window_closed;    // setup_window_closed() ran in this boot
  Hint hint_mode;        // what the waiting screen says
  uint32_t hint_poll_at; // the next look at the network; 0: at once
  char hint_text[128];   // a step with the hotspot's or the tablet's name
  Note note;             // the Wi-Fi note wanted over a dashboard
  Note note_shown;       // the note the strip shows now (no passing message over it)
  uint32_t note_poll_at; // the next look at the network for the notes; 0: at once
  uint32_t net_down_at;  // since when the notes saw no network; 0: they saw one, or did not look
  char note_text[160];   // the note's text
  bool erased;           // Platform.erased: hold-to-erase, no key since
  uint32_t session_at;   // when the last greeting (a tab_hello that started a session) came
  uint32_t greet_from;   // since when this connection waits for a greeting (connect, pairing)
  bool cleared;          // tab_clear came, and nothing was committed since
  // Question box.
  bool q_open;
  int8_t q_layout;
  uint8_t q_gen;
  uint16_t q_widget;
  char q_event;
  bool q_has_value;
  double q_value;
  // Touch.
  lv_indev_t *indev;
  bool tracking, swallowed, press_on_slider;
  lv_point_t start;
  // (2) The press now (cards spec B.5): when it began, whether it moved 12 px
  // (never cleared before the next press), whether its hold was looked at,
  // whether it began while the erase watch ran, and the card it is on.
  uint32_t press_at;
  bool moved, held, press_in_watch;
  int8_t cp_layout;  // -1: no card has the press
  uint8_t cp_gen;
  uint16_t cp_widget;
  // Feedback.
  Pending pend[PENDING];
  FbRef fbl[FB_REFS];
  uint8_t dirty[(MAX_SLOTS + 7) / 8];
  bool dirty_any;
  lv_timer_t *timer;
  // Hold-to-erase. The screen is made when the question opens.
  lv_obj_t *erase_screen, *e_title, *e_text, *e_erase, *e_cancel;
  lv_timer_t *erase_timer;  // only during the first ERASE_ARM_MS, and while asking
  uint8_t erase_step;       // EraseStep
  bool holding;
  uint32_t hold_at, erase_at;
  uint32_t down_at;        // the last poll that saw the finger during a hold
  lv_point_t hold_point;   // where the hold went down
  bool lifted;             // a poll during this hold did not see the finger
  uint8_t gaps;            // forgiven lifts in this hold
  uint32_t gaps_ms;        // and their time together
};

inline State g;

inline uint32_t now() { return lv_tick_get(); }

inline bool Budget::left() const { return objects > 0 && now() - start < ms; }

// No limit: for a boot, or to free a layout slot at once.
inline Budget unlimited() { return Budget{UINT32_MAX, now(), UINT32_MAX}; }

inline void say(int level, const char *fmt, ...) {
  if (!g.pf.log) return;
  char line[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  g.pf.log(level, line);
}

inline Layout *layout_at(int i) { return (i >= 0 && i < LAYOUTS) ? &g.lay[i] : nullptr; }
inline Layout *active_layout() { return layout_at(g.active); }
inline Layout *staged_layout() { return layout_at(g.staged); }
inline int layout_index(const Layout *l) { return (int) (l - g.lay); }

// ---------------------------------------------------------------- values

inline Slot *slot_of(Model &m, const Bound &b) {
  if (b.how != B_SLOT || b.slot < 1 || b.slot > m.nslots) return nullptr;
  return &m.slots[b.slot - 1];
}

// True when the field has no value (absent, null, or a slot without value).
inline bool bound_null(Model &m, const Bound &b) {
  if (b.how == B_CONST) return false;
  if (b.how == B_SLOT) {
    Slot *s = slot_of(m, b);
    return !s || s->null;
  }
  return true;
}

inline bool bound_text(Model &m, const Bound &b, const char **out) {
  if (b.how == B_CONST && b.type == 't') {
    *out = b.c.text;
    return true;
  }
  Slot *s = slot_of(m, b);
  if (!s || s->null || s->type != 't') return false;
  *out = s->text;
  return true;
}

inline bool bound_num(Model &m, const Bound &b, double *out) {
  if (b.how == B_CONST && b.type != 't' && b.type != 'l') {
    *out = b.c.num;
    return true;
  }
  Slot *s = slot_of(m, b);
  if (!s || s->null || s->type == 't' || s->type == 'l') return false;
  *out = s->num;
  return true;
}

inline int bound_list(Model &m, const Bound &b, const double **out) {
  if (b.how == B_CONST && b.type == 'l') {
    *out = b.c.list;
    return b.list_n;
  }
  Slot *s = slot_of(m, b);
  if (!s || s->null || s->type != 'l') return -1;
  *out = s->list;
  return s->list_n;
}

// (2) A colour field (contract 4.4): false for no value, and for a number
// that is not whole or not 0 to 0xFFFFFF.
inline bool bound_colour(Model &m, const Bound &b, uint32_t *out) {
  double v;
  if (!bound_num(m, b, &v) || !(v >= 0 && v <= MAX_COLOUR) || v != floor(v)) return false;
  *out = (uint32_t) v;
  return true;
}

inline bool bound_dirty(const Bound &b) {
  return b.how == B_SLOT && b.slot >= 1 && b.slot <= MAX_SLOTS && (g.dirty[(b.slot - 1) >> 3] >> ((b.slot - 1) & 7) & 1);
}

// ---------------------------------------------------------------- colours

// The theme of a page: its own (contract 4.8), else the layout's.
inline const Theme &page_theme(const Model &m, int page) {
  if (page >= 0 && page < (int) m.npages && m.themes && m.pages[page].theme < m.nthemes)
    return m.themes[m.pages[page].theme];
  return m.theme;
}

inline Tok theme_tokens(const Theme &th) {
  Tok t{};
  t.bg = th.c[C_CARD];
  t.bg2 = 0;
  t.has_bg2 = false;
  t.tx = th.c[C_TX];
  t.tx2 = th.c[C_TX2];
  t.acc = th.c[C_ACC];
  t.acc2 = th.c[C_ACC2];
  t.acct = th.c[C_ACCT];
  t.accf = th.c[C_ACCF];
  t.accf_pr = th.c[C_ACCF_PR];
  t.card2 = th.c[C_CARD2];
  t.card2_pr = th.c[C_CARD2_PR];
  t.card_pr = th.c[C_CARD_PR];
  t.line = th.c[C_LINE];
  t.onbg = th.c[C_ONBG];
  t.onbg_pr = th.c[C_ONBG_PR];
  t.ontx = th.c[C_ONTX];
  t.ion = th.c[C_ION];
  t.r = th.r;
  t.r2 = th.r2;
  t.bw = th.bw;
  t.gd = -1;
  return t;
}

// The layout's own theme: the side bar and the engine's pieces (until the
// page theme reaches them).
inline Tok theme_tokens(const Model &m) { return theme_tokens(m.theme); }

// A card's tokens (contract 4.7): the theme with the style on top; ion and
// onbg_pr the style's own, else worked out when the style's onbg differs
// from the theme's, else the theme's. style may be nullptr.
inline Tok style_tokens(const Theme &th, const CardStyle *s) {
  Tok t = theme_tokens(th);
  if (!s) return t;
  uint32_t *dst[S_COUNT] = {&t.bg,    &t.bg2,      &t.tx,   &t.tx2,      &t.acc,      &t.acc2,
                            &t.acct,  &t.card2,    &t.line, &t.onbg,     &t.ontx,     &t.card_pr,
                            &t.card2_pr, &t.accf, &t.accf_pr, &t.onbg_pr, &t.ion};
  for (int i = 0; i < S_ONTX + 1; i++)
    if (s->mask & (1u << i)) *dst[i] = s->c[i];
  t.has_bg2 = (s->mask & (1u << S_BG2)) != 0;
  if (s->has_r) t.r = s->r;
  if (s->has_r2) t.r2 = s->r2;
  if (s->has_bw) t.bw = s->bw;
  t.gd = s->gd;
  // A style with its own "on" fill gets its own ion and onbg_pr.
  if ((s->mask & (1u << S_ONBG)) && s->c[S_ONBG] != th.c[C_ONBG]) {
    t.ion = contrast(t.acc, s->c[S_ONBG]) >= 3 ? t.acc : t.ontx;
    t.onbg_pr = blend(t.ontx, s->c[S_ONBG], 0.82);
  }
  // (2) The roles a style names itself win over those worked out above.
  for (int i = S_CARD_PR; i < S_COUNT; i++)
    if (s->mask & (1u << i)) *dst[i] = s->c[i];
  return t;
}

// Works the tokens out; paints read CardLook.t instead (card_look).
inline Tok card_tokens(const Model &m, const Widget *card) {
  const Theme &th = page_theme(m, card ? card->page : 0);
  if (!card || card->style < 0 || card->style >= (int) m.nstyles) return theme_tokens(th);
  return style_tokens(th, &m.styles[card->style]);
}

inline Widget *card_of(Model &m, const Widget &w) {
  return (w.card >= 0 && w.card < (int) m.nw) ? &m.w[w.card] : nullptr;
}

// (2) The optimistic bool a card shows for one slot (contract 7.4): set by
// a tap with policy 0 on the card, a switch or a button of the card, and
// shown by the card's `on` and every switch and button `value` of the card
// bound to the same slot, while the widget that was tapped still shows it.
// The card's own flip of its `on` slot keeps showing while a tap on another
// slot of the card waits too (the record names only the last tap).
inline bool slot_override(Model &m, const Widget *card, const Bound &b, double *v) {
  if (!card || card->kind != W_CARD || b.how != B_SLOT) return false;
  if (card->ovr_slot && card->ovr_slot == b.slot && card->ovr_from < m.nw) {
    const Widget &o = m.w[card->ovr_from];
    if (o.ovr) {
      *v = o.ovr_value;
      return true;
    }
  }
  if (card->ovr && card->on.how == B_SLOT && card->on.slot == b.slot) {
    *v = card->ovr_value;
    return true;
  }
  return false;
}

// A card's `on` now: the optimistic value while one is shown, else the slot.
inline bool card_is_on(Model &m, const Widget *card) {
  if (!card) return false;
  double v;
  if (slot_override(m, card, card->on, &v)) return v != 0;
  return bound_num(m, card->on, &v) && v != 0;
}

// The record of a card, or nullptr for a widget outside a card and a card
// whose colours were not worked out yet.
inline CardLook *card_look(Model &m, const Widget *card) {
  if (!card || card->cl < 0 || card->cl >= (int) m.nlooks) return nullptr;
  CardLook &k = m.looks[card->cl];
  return k.ready ? &k : nullptr;
}

// Works a card's nine colours out again from its `on`, `tint` and `un`
// (cards spec B.3), and its tokens the first time. The one place that knows
// the tint rule. Returns true when anything the card shows changed.
inline bool card_look_update(Model &m, Widget &card) {
  if (card.kind != W_CARD || card.cl < 0 || card.cl >= (int) m.nlooks) return false;
  TAB_COUNT(look_updates);
  CardLook &k = m.looks[card.cl];
  if (!k.ready) {
    k.t = card_tokens(m, &card);
    // Pressed (contract 4.7): card_pr; a style with bg and no card_pr: mix(tx, bg, 90).
    const CardStyle *s = card.style >= 0 && card.style < (int) m.nstyles ? &m.styles[card.style] : nullptr;
    const bool own_bg = s && (s->mask & (1u << S_BG)) && !(s->mask & (1u << S_CARD_PR));
    k.pr = own_bg ? mix(k.t.tx, k.t.bg, 90) : k.t.card_pr;
  }
  uint32_t tint = 0;
  const bool has_tint = bound_colour(m, card.c, &tint);
  double un = 0;
  const uint8_t unavailable = bound_num(m, card.vis, &un) && un == 1 ? 1 : 0;
  const uint8_t on = card_is_on(m, &card) ? 1 : 0;
  if (k.ready && k.on == on && k.has_tint == (has_tint ? 1 : 0) && k.tint == tint && k.unavailable == unavailable)
    return false;
  k.k = card_colours(k.t, on != 0, card.lit, has_tint, tint);
  k.on = on;
  k.has_tint = has_tint ? 1 : 0;
  k.tint = has_tint ? tint : 0;
  k.unavailable = unavailable;
  k.ready = 1;
  return true;
}

// (2) The card is not available (`un` 1, contract 4.3): for a card, and for a
// widget of a card. Read from the card's record, worked out with its state.
inline bool card_unavailable(Model &m, const Widget &w) {
  const CardLook *k = card_look(m, w.kind == W_CARD ? &w : card_of(m, w));
  return k && k->unavailable;
}

// A widget's own colours that contract 4.7 gets through contrast, readable
// or legible, kept in the widget (cards spec B.3): worked out at the build,
// when its `c` slot is written with another value, and after its card's
// colours changed. Paints read them and never call contrast. Returns true
// when they changed.
inline bool widget_colour_update(Model &m, Widget &w) {
  TAB_COUNT(colour_updates);
  const uint32_t before_now = w.c_now, before_on = w.c_on;
  const uint8_t before_has = w.c_has;
  uint32_t c = 0;
  const bool has_c = w.kind != W_CARD && w.kind != W_CHOICE && bound_colour(m, w.c, &c);
  w.c_has = 0;
  w.c_now = w.c_on = 0;
  if (w.kind == W_HEADING) {
    if (has_c) {
      const Theme &th = page_theme(m, w.page);
      w.c_now = legible(c, th.c[C_WALL], th.c[C_TX]);
      w.c_has = 1;
    }
  } else if (CardLook *k = card_look(m, card_of(m, w))) {
    const CardColours &cc = k->k;
    switch (w.kind) {
      case W_LABEL:
      case W_THERMO:
      case W_CHART:
        if (has_c) {
          w.c_now = legible(c, cc.background, cc.text);
          w.c_has = 1;
        }
        break;
      case W_SWITCH:
        if (has_c) {
          w.c_now = legible(c, cc.background, cc.text);
          w.c_on = readable(w.c_now);
          w.c_has = 1;
        }
        break;
      case W_BUTTON:
        if (has_c) {
          w.c_now = c;
          w.c_on = readable(c);
          w.c_has = 1;
        }
        break;
      case W_SLIDER: {
        // The fill; a bar's handle and icon go on it. `c` does not colour a
        // slider with a track.
        const uint32_t track = cc.filled ? cc.FP : k->t.card2;
        w.c_now = has_c && !w.track_n ? legible(c, track, cc.text) : cc.filled ? cc.FI : cc.A;
        w.c_on = w.look == 1 ? readable(w.c_now) : 0;
        w.c_has = 1;
        break;
      }
      case W_IMAGE: {
        const ImageColours ic = image_colours(k->t, cc, w.look, has_c, c);
        w.c_now = ic.fill;
        w.c_on = ic.icon;
        w.c_has = 1;
        break;
      }
      default:
        break;
    }
  }
  return w.c_now != before_now || w.c_on != before_on || w.c_has != before_has;
}

// ------------------------------------------------- calls across headers

// tab_ui.h
void ui_begin();
void ui_teardown();
bool build_step(Layout &l, uint32_t budget_ms);
void build_abort(Layout &l);
bool delete_step(Layout &l, Budget &b);
void show_layout(Layout &l);
void show_page(Layout &l, int page);
void paint_widget(Layout &l, Widget &w);
void paint_dirty(Layout &l);
void paint_stale(Layout &l, uint32_t start);
void show_waiting(bool on);
void show_strip(const char *text);
void show_note(const char *text);
void hide_strip();
void update_mark();
void question_open(Layout &l, Widget &w, char event, bool has_value, double value);
void question_close();
void restyle_overlay();
void erase_screen_open();
void erase_screen_busy();
void erase_screen_close();

// tab_core.h
bool can_send();
void refuse_touch();
bool send_input(Layout &l, Widget &w, char event, bool confirmed, bool has_value, double value);
void widget_tapped(Layout &l, Widget &w);
void slider_released(Layout &l, Widget &w, double value);
void thermostat_step(Layout &l, Widget &w, int direction);
void choice_picked(Layout &l, Widget &w, int option);
void question_answer(bool yes);
void erase_answer(bool erase);
void fb_track(Layout &l, Widget &w);
Layout *layout_of(const Widget *w);
void poll_connection();

}  // namespace detail
}  // namespace tab

#endif  // TAB_STATE_H
