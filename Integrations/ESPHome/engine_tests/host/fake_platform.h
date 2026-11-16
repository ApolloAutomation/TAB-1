#pragma once
// Fake Platform hooks for the host tests (design 7.9): count allocations, hold
// the "flash" in memory with an optional slow, deferred save, record events.
//
// Flash operations follow the glue's rules (design 7.2): they take effect in
// call order, an erase during a running save is done after it, and a newer
// save or erase replaces one that is queued and has not started.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#include "harness.h"
#include "tab_engine.h"

namespace fake {

struct Event {
  std::string name, d, s;
  bool has_s;
  uint32_t at;
};

struct Op {
  int kind = 0;  // 0 none, 1 save, 2 erase
  std::vector<uint8_t> data;
};

static const char *NAME = "TAB-1 Kitchen";  // the friendly name, on the waiting screen

struct State {
  // Memory.
  bool alloc_fails = false;
  size_t largest = 16000000;
  uint32_t free = 28000000;  // free PSRAM; a heap that is nearly full lowers both
  int largest_calls = 0;     // each one walks the tablet's whole heap
  uint64_t allocs = 0, releases = 0;
  std::set<void *> live;
  bool bad_release = false;
  // Keys.
  bool have_key = true;
  uint8_t key[32] = {};
  bool have_secret = false;
  uint8_t secret[32] = {};
  int secret_saves = 0;
  // Flash.
  bool flash_has = false;
  std::vector<uint8_t> flash;
  bool flash_damaged = false;  // cache_load fails
  uint32_t save_ms = 0;        // 0: saves finish at once
  Op running, queued;
  uint32_t running_since = 0;
  int save_calls = 0, erase_calls = 0, save_fail_next = 0, writes = 0;
  int write_fail_next = 0;   // accepted saves whose write fails later, as NVS full
  bool save_failed = false;  // read once by cache_failed()
  int failed_polls = 0;
  // Connection and events.
  bool connected = true;
  void (*on_poll)() = nullptr;  // runs in ha_connected(), at the start of each engine tick
  std::vector<Event> events;
  std::vector<std::string> logs;
  bool print_logs = false;
  // Power-on, in simulated milliseconds. By default ten minutes before the
  // platform was made, so hold-to-erase is over before the engine starts.
  uint32_t powered_on_at = hh::g_ms - 600000;
  bool erase_hooks = true;  // false: the platform has no uptime_ms and erase_all
  int erase_alls = 0;
  uint32_t erased_at = 0;
  // The erase mark the glue leaves after the hold-to-erase (design 10.11).
  bool erased_mark = false;
  bool erased_hook = true;  // false: the platform has no erased_clear
  int erased_clears = 0;
  // Network and set-up hotspot (read only while the waiting screen shows
  // during the set-up window, and over a dashboard while Home Assistant is
  // not connected or after the window closed with a key and no network).
  bool network = false;
  bool network_hook = true;  // false: the platform has no network_up
  int network_polls = 0;
  bool hotspot = false;      // the set-up hotspot is on
  bool hotspot_hook = true;  // false: the platform has no hotspot_on
  int hotspot_polls = 0;
  const char *hotspot_name = "Apollo TAB1 Hotspot";  // the YAML's `wifi: ap: ssid:`
  const char *name = NAME;  // the tablet's name (Platform.name)
};

static State st;
static const char *MAC = "aabbccddeeff";
static uint32_t BOOT = 3;

static void *alloc(size_t n) {
  if (st.alloc_fails) return nullptr;
  void *p = malloc(n ? n : 1);
  st.allocs++;
  st.live.insert(p);
  return p;
}

static void release(void *p) {
  if (!p) return;
  if (!st.live.erase(p)) st.bad_release = true;
  st.releases++;
  free(p);
}

static size_t largest_block() {
  st.largest_calls++;
  return st.largest;
}
static uint32_t free_internal() { return 61000; }
static uint32_t free_psram() { return st.free; }

static bool api_key(uint8_t out[32]) {
  if (!st.have_key) return false;
  memcpy(out, st.key, 32);
  return true;
}

static bool secret_load(uint8_t out[32]) {
  if (!st.have_secret) return false;
  memcpy(out, st.secret, 32);
  return true;
}

static bool secret_save(const uint8_t in[32]) {
  memcpy(st.secret, in, 32);
  st.have_secret = true;
  st.secret_saves++;
  return true;
}

static void apply(const Op &op) {
  if (op.kind == 1 && st.write_fail_next > 0) {
    // The glue's failed write leaves no valid copy (nvs_write_layout).
    st.write_fail_next--;
    st.flash.clear();
    st.flash_has = false;
    st.save_failed = true;
  } else if (op.kind == 1) {
    st.flash = op.data;
    st.flash_has = true;
    st.writes++;
    st.save_failed = false;
  } else if (op.kind == 2) {
    st.flash.clear();
    st.flash_has = false;
  }
}

static void submit(Op op) {
  if (st.save_ms == 0 && st.running.kind == 0) {
    apply(op);
    return;
  }
  if (st.running.kind == 0) {
    st.running = op;
    st.running_since = hh::g_ms;
  } else {
    st.queued = op;  // replaces one that has not started
  }
}

// Called every simulated millisecond: finishes the running operation.
static void pump() {
  if (st.running.kind == 0) return;
  if (hh::g_ms - st.running_since < st.save_ms) return;
  apply(st.running);
  st.running = st.queued;
  st.queued = Op();
  st.running_since = hh::g_ms;
}

static size_t cache_size() { return st.flash_has ? st.flash.size() : 0; }

static bool cache_load(uint8_t *dst, size_t cap) {
  if (!st.flash_has || st.flash_damaged || st.flash.size() > cap) return false;
  memcpy(dst, st.flash.data(), st.flash.size());
  return true;
}

static bool cache_save(const uint8_t *data, size_t n) {
  st.save_calls++;
  if (st.save_fail_next > 0) {
    st.save_fail_next--;
    return false;
  }
  Op op;
  op.kind = 1;
  op.data.assign(data, data + n);  // the copy is made before returning
  submit(op);
  return true;
}

static void cache_erase() {
  st.erase_calls++;
  Op op;
  op.kind = 2;
  submit(op);
}

static bool cache_busy() { return st.running.kind != 0 || st.queued.kind != 0; }

static bool cache_failed() {
  st.failed_polls++;
  const bool f = st.save_failed;
  st.save_failed = false;
  return f;
}

static bool ha_connected() {
  if (st.on_poll) st.on_poll();
  return st.connected;
}

static void send_event(const char *name, const char *d, const char *s) {
  Event e;
  e.name = name;
  e.d = d ? d : "";
  e.has_s = s != nullptr;
  e.s = s ? s : "";
  e.at = hh::g_ms;
  st.events.push_back(e);
}

static void random_bytes(uint8_t *out, size_t n) {
  for (size_t i = 0; i < n; i++) out[i] = (uint8_t) rand();
}

static void log(int level, const char *line) {
  st.logs.push_back(line);
  if (st.print_logs) fprintf(stderr, "  [engine %d] %s\n", level, line);
}

static uint32_t uptime_ms() { return hh::g_ms - st.powered_on_at; }

static bool network_up() {
  st.network_polls++;
  return st.network;
}

static bool hotspot_on() {
  st.hotspot_polls++;
  return st.hotspot;
}

// The tablet restarts here; the fake only counts, and leaves the mark.
static void erase_all() {
  st.erase_alls++;
  st.erased_at = hh::g_ms;
  st.erased_mark = true;
}

static void erased_clear() {
  st.erased_clears++;
  st.erased_mark = false;
}

static tab::Platform platform() {
  tab::Platform p{};
  p.mac = MAC;
  p.name = st.name;
  p.boot = BOOT;
  p.alloc = alloc;
  p.release = release;
  p.largest_block = largest_block;
  p.free_internal = free_internal;
  p.free_psram = free_psram;
  p.api_key = api_key;
  p.secret_load = secret_load;
  p.secret_save = secret_save;
  p.cache_size = cache_size;
  p.cache_load = cache_load;
  p.cache_save = cache_save;
  p.cache_erase = cache_erase;
  p.cache_busy = cache_busy;
  p.cache_failed = cache_failed;
  p.ha_connected = ha_connected;
  p.send_event = send_event;
  p.random = random_bytes;
  p.log = log;
  if (st.erase_hooks) {
    p.uptime_ms = uptime_ms;
    p.erase_all = erase_all;
  }
  p.erased = st.erased_mark;
  if (st.erased_hook) p.erased_clear = erased_clear;
  if (st.network_hook) p.network_up = network_up;
  if (st.hotspot_hook) p.hotspot_on = hotspot_on;
  p.hotspot = st.hotspot_name;
  return p;
}

// Events with this name, oldest first.
static std::vector<Event> events(const char *name) {
  std::vector<Event> out;
  for (auto &e : st.events)
    if (e.name == name) out.push_back(e);
  return out;
}

}  // namespace fake
