#pragma once
// ============================================================================
//  engine_glue.h — the ESPHome side of the TAB-1 dashboard engine
//
//  The engine (engine/tab_engine.h) is plain C++ and LVGL and knows nothing
//  about ESPHome. This file gives it what it needs from the tablet:
//
//    memory   one PSRAM block per call, never internal RAM
//    flash    the pairing secret and the boot counter in ESPHome preferences;
//             the last layout in NVS (namespace "tab1"), written by a short
//             low-priority task from a private copy, in call order
//    API      the encryption key, "Home Assistant is connected", the events
//    network  "connected to a network", "the set-up hotspot is on" and the
//             hotspot's name, for the waiting screen's hint and the Wi-Fi
//             notes over a dashboard
//    erase    hold-to-erase: wipes every NVS namespace this firmware uses
//             ("esphome" and "tab1"), then the whole NVS partition, leaves
//             the erase mark ("tab1" "erased", one byte, nothing secret) and
//             restarts (design 10.8, 10.11)
//
//  and it turns the eight Home Assistant actions of Engine.yaml into engine
//  calls. Every action runs synchronously on the main loop, so the string
//  arguments are views into ESPHome's receive buffer; nothing is copied here.
//
//  The flash queue (namespace tab_glue::cache) has no ESPHome or ESP-IDF code,
//  so tools/check_engine_package.py --host builds and tests it on a computer
//  with TAB_GLUE_HOST_TEST defined.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

// The version of this file. ESPHome builds an engine_glue.h from the ESPHome
// config folder instead of this one when there is one there, so Engine.yaml
// stops a build whose copy has another number, or none (engine/tab_files.h).
#define TAB1_FILES_GLUE 16

#ifndef TAB_GLUE_HOST_TEST
#include <string>

#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_rom_crc.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "esphome/core/defines.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/json/json_util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/core/string_ref.h"
#ifdef USE_NETWORK
#include "esphome/components/network/util.h"
#endif
#ifdef USE_WIFI_AP
#include "esphome/components/wifi/wifi_component.h"
#endif

#include "engine/tab_engine.h"
#if !defined(TAB1_FILES_ENGINE) || TAB1_FILES_ENGINE != TAB1_FILES_GLUE
#error "engine_glue.h and engine/ come from different copies: delete the old engine_glue.h or engine/ from your ESPHome config folder (docs/engine.md, \"Old copies of the package's files\")"
#endif
#endif

namespace tab_glue {

// ---------------------------------------------------------------------------
//  Flash queue for the stored layout
//
//  The engine hands over a layout with cache_save() and may free or reuse its
//  bytes right after, so the queue copies them into a PSRAM block of its own.
//  A worker task writes the copy and frees it. Rules (design 7.2):
//    - a save is refused (false) while another save is being written; the
//      engine tries again later. An erase is never refused for that;
//    - operations take effect in call order;
//    - an erase during a running save is done after that save;
//    - a newer save or erase replaces one that is queued and not yet started
//      (a save can be queued behind a running erase).
// ---------------------------------------------------------------------------
namespace cache {

// Layouts larger than this are not stored (design 7.6). Saving one erases the
// stored copy instead, so a reboot never brings back an older layout.
static constexpr size_t MAX_STORED_BYTES = 128 * 1024;

// What the queue needs from the platform. The tablet fills it in start(); the
// host test fills it with fakes.
struct Backend {
  void *(*alloc)(size_t bytes);  // a PSRAM block, or nullptr
  void (*release)(void *block);
  void (*lock)();    // short critical section, no allocation inside
  void (*unlock)();
  bool (*spawn)();   // start a worker that calls worker_run(); false: could not
  bool (*write)(const uint8_t *data, size_t n);  // replace the stored layout
  bool (*erase)();   // forget the stored layout
};

enum Kind : uint8_t { NONE = 0, SAVE = 1, ERASE = 2 };

struct Op {
  Kind kind;
  uint8_t *data;  // owned by the queue (SAVE only)
  size_t n;
};

struct State {
  Backend backend{};
  Op queued{NONE, nullptr, 0};  // waiting, not started
  Kind running{NONE};           // the operation being written now
  bool worker{false};           // a worker is running
  // Outcome of finished operations, read and logged by the main loop.
  uint32_t finished{0};
  uint32_t reported{0};
  Kind last_kind{NONE};
  bool last_ok{true};
  size_t last_n{0};
  // The last save that finished could not be written; read by the engine.
  bool save_failed{false};
};

inline State &state() {
  static State s;
  return s;
}

inline void set_backend(const Backend &backend) { state().backend = backend; }

// Body of the worker task: runs queued operations until none is left.
inline void worker_run() {
  State &s = state();
  for (;;) {
    s.backend.lock();
    Op op = s.queued;
    s.queued = Op{NONE, nullptr, 0};
    if (op.kind == NONE) {
      s.worker = false;
      s.backend.unlock();
      return;
    }
    s.running = op.kind;
    s.backend.unlock();

    bool ok = op.kind == SAVE ? s.backend.write(op.data, op.n) : s.backend.erase();
    if (op.data != nullptr)
      s.backend.release(op.data);

    s.backend.lock();
    s.running = NONE;
    s.last_kind = op.kind;
    s.last_ok = ok;
    s.last_n = op.n;
    if (op.kind == SAVE)
      s.save_failed = !ok;
    s.finished++;
    s.backend.unlock();
  }
}

// Queues `op` (taking ownership of its data) and starts a worker if none runs.
// A save is refused while another save is being written: its copy is freed
// and the result is false.
inline bool submit(Op op) {
  State &s = state();
  bool start = false;
  s.backend.lock();
  if (op.kind == SAVE && s.running == SAVE) {
    s.backend.unlock();
    s.backend.release(op.data);
    return false;
  }
  Op dropped = s.queued;
  s.queued = op;
  if (!s.worker) {
    s.worker = true;
    start = true;
  }
  s.backend.unlock();
  if (dropped.data != nullptr)
    s.backend.release(dropped.data);
  if (start && !s.backend.spawn()) {
    s.backend.lock();
    Op back = s.queued;
    s.queued = Op{NONE, nullptr, 0};
    s.worker = false;
    s.backend.unlock();
    if (back.data != nullptr)
      s.backend.release(back.data);
    return false;
  }
  return true;
}

// True while a save is being written.
inline bool saving() {
  State &s = state();
  s.backend.lock();
  bool b = s.running == SAVE;
  s.backend.unlock();
  return b;
}

// Platform.cache_save: copies the bytes before it returns. False when a save
// is being written, when there is no memory for the copy or when no worker
// could be started; the engine tries again later. A layout too big to store
// erases the stored one instead and counts as done.
inline bool save(const uint8_t *data, size_t n) {
  if (n > MAX_STORED_BYTES)
    return submit(Op{ERASE, nullptr, 0});
  if (saving())  // checked again under the lock in submit()
    return false;
  State &s = state();
  auto *copy = static_cast<uint8_t *>(s.backend.alloc(n > 0 ? n : 1));
  if (copy == nullptr)
    return false;
  if (n > 0)
    std::memcpy(copy, data, n);
  return submit(Op{SAVE, copy, n});
}

// Platform.cache_erase.
inline bool erase() { return submit(Op{ERASE, nullptr, 0}); }

// Platform.cache_busy: a save or erase has not finished.
inline bool busy() {
  State &s = state();
  s.backend.lock();
  bool b = s.worker || s.queued.kind != NONE;
  s.backend.unlock();
  return b;
}

// Takes the outcome of the operations finished since the last call. True
// when there is something new; `kind`, `ok` and `n` describe the last one.
inline bool take_report(Kind *kind, bool *ok, size_t *n) {
  State &s = state();
  s.backend.lock();
  bool fresh = s.finished != s.reported;
  s.reported = s.finished;
  *kind = s.last_kind;
  *ok = s.last_ok;
  *n = s.last_n;
  s.backend.unlock();
  return fresh;
}

// Platform.cache_failed: true once when the last save that finished could not
// be written. The engine then stores that layout again later.
inline bool take_save_failed() {
  State &s = state();
  s.backend.lock();
  bool failed = s.save_failed;
  s.save_failed = false;
  s.backend.unlock();
  return failed;
}

}  // namespace cache

#ifndef TAB_GLUE_HOST_TEST

static const char *const TAG = "tab";

// Signature of the event sender that Engine.yaml passes to start(): fires
// esphome.<name> with data d (and s when not null) through a YAML script.
using EventFn = void (*)(const char *name, const char *d, const char *s);

// Response size for tab_hello and tab_status (design 7.2: always enough).
static constexpr size_t RESPONSE_CAP = 768;

// ---------------------------------------------------------------------------
//  Platform hooks
// ---------------------------------------------------------------------------
namespace detail {

inline EventFn &event_fn() {
  static EventFn fn = nullptr;
  return fn;
}

inline void *psram_alloc(size_t bytes) {
  return heap_caps_malloc(bytes > 0 ? bytes : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
inline void psram_release(void *block) { heap_caps_free(block); }
inline size_t largest_block() { return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM); }
inline uint32_t free_internal() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
inline uint32_t free_psram() { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

inline void wipe(void *p, size_t n) {
  volatile auto *v = static_cast<volatile uint8_t *>(p);
  while (n-- > 0)
    *v++ = 0;
}

inline bool api_key(uint8_t out[32]) {
#ifdef USE_API_NOISE
  auto *server = esphome::api::global_api_server;
  if (server == nullptr)
    return false;
  auto &ctx = server->get_noise_ctx();
  if (!ctx.has_psk())
    return false;
  esphome::noise::psk_t psk;
  ctx.load_psk(psk);
  bool ok = !esphome::noise::NoiseContext::is_all_zeros(psk);
  if (ok)
    std::memcpy(out, psk.data(), 32);
  wipe(psk.data(), psk.size());
  return ok;
#else
  (void) out;
  return false;
#endif
}

// The pairing secret lives in ESPHome preferences (NVS), with a marker so an
// empty or foreign record never counts as a secret.
static constexpr uint32_t SECRET_MAGIC = 0x54414231;  // "TAB1"
struct SecretRecord {
  uint32_t magic;
  uint8_t key[32];
};

inline esphome::ESPPreferenceObject &secret_pref() {
  static esphome::ESPPreferenceObject pref;
  return pref;
}

inline bool secret_load(uint8_t out[32]) {
  SecretRecord rec{};
  bool ok = secret_pref().load(&rec) && rec.magic == SECRET_MAGIC;
  if (ok)
    std::memcpy(out, rec.key, 32);
  wipe(&rec, sizeof(rec));
  return ok;
}

inline bool secret_save(const uint8_t in[32]) {
  SecretRecord rec{};
  rec.magic = SECRET_MAGIC;
  std::memcpy(rec.key, in, 32);
  // Pairing is rare (at most once a minute), so write it to flash right away.
  bool ok = secret_pref().save(&rec) && esphome::global_preferences->sync();
  wipe(&rec, sizeof(rec));
  return ok;
}

inline uint32_t next_boot_count() {
  auto pref = esphome::global_preferences->make_preference<uint32_t>(esphome::fnv1_hash("apollo_tab1_boot"), true);
  uint32_t boot = 0;
  if (!pref.load(&boot))
    boot = 0;
  boot++;
  // Braces around every log-only body: a log level that compiles the macro
  // away would otherwise leave an empty if (-Wempty-body).
  if (!pref.save(&boot) || !esphome::global_preferences->sync()) {
    ESP_LOGW(TAG, "Could not store the boot counter");
  }
  return boot;
}

// --- The stored layout in NVS ------------------------------------------------
// Two keys in namespace "tab1": "layout" holds the bytes exactly as the engine
// gave them, "hdr" their size and CRC-32. The header is removed before a new
// layout is written and written last, so a power cut mid-way leaves no valid
// copy instead of a damaged one.
static constexpr const char *NVS_NAMESPACE = "tab1";
static constexpr const char *KEY_LAYOUT = "layout";
static constexpr const char *KEY_HEADER = "hdr";
static constexpr uint32_t HEADER_MAGIC = 0x4C424154;  // "TABL"

struct Header {
  uint32_t magic;
  uint32_t size;
  uint32_t crc;
};

inline bool read_header(nvs_handle_t h, Header *hdr) {
  size_t len = sizeof(Header);
  if (nvs_get_blob(h, KEY_HEADER, hdr, &len) != ESP_OK || len != sizeof(Header))
    return false;
  return hdr->magic == HEADER_MAGIC && hdr->size > 0 && hdr->size <= cache::MAX_STORED_BYTES;
}

// Runs in the worker task.
inline bool nvs_write_layout(const uint8_t *data, size_t n) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
    return false;
  nvs_erase_key(h, KEY_HEADER);
  esp_err_t err = nvs_set_blob(h, KEY_LAYOUT, data, n);
  if (err == ESP_ERR_NVS_NOT_ENOUGH_SPACE) {
    // Room for the new copy only after the old one is gone.
    nvs_erase_key(h, KEY_LAYOUT);
    nvs_commit(h);
    err = nvs_set_blob(h, KEY_LAYOUT, data, n);
  }
  if (err == ESP_OK) {
    Header hdr{HEADER_MAGIC, static_cast<uint32_t>(n), esp_rom_crc32_le(0, data, n)};
    err = nvs_set_blob(h, KEY_HEADER, &hdr, sizeof(hdr));
  }
  if (err != ESP_OK)
    nvs_erase_key(h, KEY_LAYOUT);
  esp_err_t commit = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK && commit == ESP_OK;
}

// Runs in the worker task.
inline bool nvs_erase_layout() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err == ESP_ERR_NVS_NOT_FOUND)
    return true;
  if (err != ESP_OK)
    return false;
  esp_err_t e1 = nvs_erase_key(h, KEY_HEADER);
  esp_err_t e2 = nvs_erase_key(h, KEY_LAYOUT);
  esp_err_t commit = nvs_commit(h);
  nvs_close(h);
  auto gone = [](esp_err_t e) { return e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND; };
  return gone(e1) && gone(e2) && commit == ESP_OK;
}

inline void log_cache_report() {
  cache::Kind kind;
  bool ok;
  size_t n;
  if (!cache::take_report(&kind, &ok, &n))
    return;
  if (kind == cache::SAVE) {
    if (ok) {
      ESP_LOGI(TAG, "Layout stored in flash (%u bytes)", (unsigned) n);
    } else {
      ESP_LOGW(TAG, "Could not store the layout in flash (%u bytes)", (unsigned) n);
    }
  } else if (kind == cache::ERASE) {
    if (ok) {
      ESP_LOGI(TAG, "Stored layout erased");
    } else {
      ESP_LOGW(TAG, "Could not erase the stored layout");
    }
  }
}

inline size_t cache_size() {
  log_cache_report();
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
    return 0;
  Header hdr{};
  size_t len = 0;
  bool ok = read_header(h, &hdr) && nvs_get_blob(h, KEY_LAYOUT, nullptr, &len) == ESP_OK && len == hdr.size;
  nvs_close(h);
  return ok ? len : 0;
}

inline bool cache_load(uint8_t *dst, size_t cap) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
    return false;
  Header hdr{};
  bool ok = read_header(h, &hdr) && hdr.size <= cap;
  if (ok) {
    size_t len = cap;
    ok = nvs_get_blob(h, KEY_LAYOUT, dst, &len) == ESP_OK && len == hdr.size &&
         esp_rom_crc32_le(0, dst, len) == hdr.crc;
  }
  nvs_close(h);
  if (!ok) {
    ESP_LOGW(TAG, "The stored layout is missing or damaged");
  }
  return ok;
}

inline bool cache_save(const uint8_t *data, size_t n) {
  log_cache_report();
  if (n > cache::MAX_STORED_BYTES) {
    ESP_LOGI(TAG, "Layout of %u bytes is too big to store; the stored copy is erased", (unsigned) n);
  }
  bool ok = cache::save(data, n);
  if (!ok) {
    ESP_LOGD(TAG, "Layout not stored now (a save is running, or no memory); the engine tries again later");
  }
  return ok;
}

inline void cache_erase() {
  log_cache_report();
  if (!cache::erase()) {
    ESP_LOGW(TAG, "Could not start erasing the stored layout");
  }
}

inline bool cache_busy() {
  log_cache_report();
  return cache::busy();
}

inline bool cache_failed() {
  log_cache_report();
  return cache::take_save_failed();
}

// Critical section shared by the main loop and the worker task.
inline portMUX_TYPE &cache_mux() {
  static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  return mux;
}
inline void cache_lock() { portENTER_CRITICAL(&cache_mux()); }
inline void cache_unlock() { portEXIT_CRITICAL(&cache_mux()); }

inline void cache_task(void *) {
  cache::worker_run();
  vTaskDelete(nullptr);
}

// A short-lived low-priority task with a 4 KB stack (design 7.4), so the main
// loop never waits for flash.
inline bool cache_spawn() {
  return xTaskCreatePinnedToCore(cache_task, "tab1_cache", 4096, nullptr, 1, nullptr, tskNO_AFFINITY) == pdPASS;
}

inline bool ha_connected() {
  auto *server = esphome::api::global_api_server;
  return server != nullptr && server->is_connected_with_state_subscription();
}

inline void send_event(const char *name, const char *d, const char *s) {
  EventFn fn = event_fn();
  if (fn != nullptr)
    fn(name, d, s);
}

inline void random_bytes(uint8_t *out, size_t n) { esp_fill_random(out, n); }

// Platform.network_up. The engine reads it at most twice a second, only while
// its waiting screen shows during the set-up window, or over a dashboard
// while Home Assistant is not connected (the Wi-Fi notes).
inline bool network_up() {
#ifdef USE_NETWORK
  return esphome::network::is_connected();
#else
  return false;
#endif
}

#ifdef USE_WIFI_AP
// Platform.hotspot_on: the set-up hotspot (`wifi: ap:`) is on, so the waiting
// screen can say to join it. Read as network_up.
inline bool hotspot_on() {
  auto *wifi = esphome::wifi::global_wifi_component;
  return wifi != nullptr && wifi->is_ap_active();
}

// Platform.hotspot: the hotspot's name as the YAML sets it ("Apollo TAB1
// Hotspot"), read once at start. nullptr when there is none.
inline const char *hotspot_name() {
  static char name[33];  // an SSID has at most 32 bytes
  auto *wifi = esphome::wifi::global_wifi_component;
  if (wifi == nullptr || !wifi->has_ap())
    return nullptr;
  const esphome::wifi::WiFiAP ap = wifi->get_ap();
  const esphome::StringRef ssid = ap.get_ssid();
  if (ssid.empty())
    return nullptr;
  const size_t n = ssid.size() < sizeof(name) - 1 ? ssid.size() : sizeof(name) - 1;
  std::memcpy(name, ssid.c_str(), n);
  name[n] = '\0';
  return name;
}
#endif

// --- Hold-to-erase -------------------------------------------------------------
// Milliseconds since power-on: the engine offers the erase question only to a
// touch that starts in the first 10 s.
inline uint32_t uptime_ms() { return esphome::millis(); }

// Every NVS namespace this firmware writes. tools/check_engine_package.py
// --compile reads every nvs_open in the generated sources and fails when one
// names a namespace missing here.
//   "esphome"  ESPHome preferences: Wi-Fi network and password, the API key
//              Home Assistant provisioned, the pairing secret
//              ("apollo_tab1_secret"), the boot counter, the safe mode counter
//   "tab1"     the stored dashboard ("layout", "hdr")
static constexpr const char *ERASE_NAMESPACES[] = {"esphome", NVS_NAMESPACE};

inline bool erase_namespace(const char *ns) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
  if (err == ESP_ERR_NVS_NOT_FOUND)
    return true;
  if (err != ESP_OK)
    return false;
  err = nvs_erase_all(h);
  esp_err_t commit = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK && commit == ESP_OK;
}

// The erase mark (design 10.11): one byte in namespace "tab1", written after
// the whole partition was erased, so it is all that is left. It holds nothing
// secret; it only tells the waiting screen that Home Assistant may still
// know this tablet and asks about it in Repairs. Removed once the tablet has
// a key again (Platform.erased_clear).
static constexpr const char *KEY_ERASED = "erased";

inline bool erased_mark() {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
    return false;
  uint8_t mark = 0;
  bool set = nvs_get_u8(h, KEY_ERASED, &mark) == ESP_OK && mark == 1;
  nvs_close(h);
  return set;
}

// Platform.erased_clear: once, on the main loop (a short NVS write).
inline void erased_clear() {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
    return;
  esp_err_t err = nvs_erase_key(h, KEY_ERASED);
  esp_err_t commit = nvs_commit(h);
  nvs_close(h);
  if ((err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) || commit != ESP_OK) {
    ESP_LOGW(TAG, "Could not clear the erase mark");
  }
}

// After global_preferences->reset(): NVS is closed and erased. Opened again
// for this one write; ESPHome writes nothing more before the restart (its
// handle is gone).
inline bool erased_mark_write() {
  if (nvs_flash_init() != ESP_OK)
    return false;
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
    return false;
  esp_err_t err = nvs_set_u8(h, KEY_ERASED, 1);
  esp_err_t commit = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK && commit == ESP_OK;
}

// Platform.erase_all, on the main loop after the person chose Erase. Never
// returns. No App.safe_reboot(): its shutdown steps would save preferences
// again. The main loop is blocked here, so the watchdog (5 s, panics) is fed
// before every step that may take a while; App.feed_wdt() feeds at least once
// a second, so each step has 4 s.
inline void erase_all() {
  ESP_LOGW(TAG, "Erasing this tablet: Wi-Fi, API key, pairing and stored dashboard");
  // A layout write in the cache task finishes first (at most about 1 s).
  for (int i = 0; i < 100 && cache::busy(); i++) {
    esphome::App.feed_wdt();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  for (const char *ns : ERASE_NAMESPACES) {
    esphome::App.feed_wdt();
    if (!erase_namespace(ns)) {
      ESP_LOGE(TAG, "Could not erase NVS namespace %s", ns);
    }
  }
  // Then the whole NVS partition, once, the way ESPHome's factory reset does
  // it: reset() drops pending preference writes, erases the partition
  // (nvs_flash_deinit, nvs_flash_erase) and lets no later write reach flash.
  esphome::App.feed_wdt();
  esphome::global_preferences->reset();
  esphome::App.feed_wdt();
  if (!erased_mark_write()) {
    ESP_LOGE(TAG, "Could not leave the erase mark");
  }
  esp_restart();
}

inline void log_line(int level, const char *line) {
  switch (level) {
    case 0:
      ESP_LOGE(TAG, "%s", line);
      break;
    case 1:
      ESP_LOGW(TAG, "%s", line);
      break;
    case 2:
      ESP_LOGI(TAG, "%s", line);
      break;
    default:
      ESP_LOGD(TAG, "%s", line);
      break;
  }
}

// The engine writes finished JSON; the action response wants a JsonObject.
// Parse it (PSRAM allocator) and copy every key. Nothing is added or renamed.
inline void copy_json(const char *text, size_t n, JsonObject root) {
  if (n == 0) {
    ESP_LOGE(TAG, "Engine response did not fit");
    return;
  }
  JsonDocument doc = esphome::json::parse_json(reinterpret_cast<const uint8_t *>(text), n);
  JsonObjectConst src = doc.as<JsonObjectConst>();
  if (src.isNull()) {
    ESP_LOGE(TAG, "Engine response is not a JSON object");
    return;
  }
  for (JsonPairConst kv : src)
    root[kv.key()] = kv.value();
}

inline const char *&result() {
  static const char *r = "";
  return r;
}

}  // namespace detail

// ---------------------------------------------------------------------------
//  Start-up, called once from Engine.yaml (script tab_engine_start)
// ---------------------------------------------------------------------------
inline void start(const tab::Fonts &fonts, EventFn send_event) {
  static tab::Fonts kept_fonts;
  static tab::Platform platform{};
  static char mac[esphome::MAC_ADDRESS_BUFFER_SIZE];

  kept_fonts = fonts;
  detail::event_fn() = send_event;
  esphome::get_mac_address_into_buffer(mac);
  detail::secret_pref() =
      esphome::global_preferences->make_preference<detail::SecretRecord>(esphome::fnv1_hash("apollo_tab1_secret"), true);

  cache::set_backend(cache::Backend{
      detail::psram_alloc,
      detail::psram_release,
      detail::cache_lock,
      detail::cache_unlock,
      detail::cache_spawn,
      detail::nvs_write_layout,
      detail::nvs_erase_layout,
  });

  const esphome::StringRef &friendly = esphome::App.get_friendly_name();
  platform.mac = mac;
  platform.name = friendly.empty() ? esphome::App.get_name().c_str() : friendly.c_str();
  platform.boot = detail::next_boot_count();
  platform.alloc = detail::psram_alloc;
  platform.release = detail::psram_release;
  platform.largest_block = detail::largest_block;
  platform.free_internal = detail::free_internal;
  platform.free_psram = detail::free_psram;
  platform.api_key = detail::api_key;
  platform.secret_load = detail::secret_load;
  platform.secret_save = detail::secret_save;
  platform.cache_size = detail::cache_size;
  platform.cache_load = detail::cache_load;
  platform.cache_save = detail::cache_save;
  platform.cache_erase = detail::cache_erase;
  platform.cache_busy = detail::cache_busy;
  platform.cache_failed = detail::cache_failed;
  platform.ha_connected = detail::ha_connected;
  platform.send_event = detail::send_event;
  platform.random = detail::random_bytes;
  platform.log = detail::log_line;
  platform.uptime_ms = detail::uptime_ms;
  platform.erase_all = detail::erase_all;
  platform.erased = detail::erased_mark();
  platform.erased_clear = detail::erased_clear;
  platform.network_up = detail::network_up;
#ifdef USE_WIFI_AP
  platform.hotspot_on = detail::hotspot_on;
  platform.hotspot = detail::hotspot_name();
#endif

  ESP_LOGI(TAG, "Starting the dashboard engine (boot %u)", (unsigned) platform.boot);
  tab::begin(kept_fonts, platform);
  ESP_LOGI(TAG, "Free after start: internal %u bytes, PSRAM %u bytes, largest PSRAM block %u bytes",
           (unsigned) detail::free_internal(), (unsigned) detail::free_psram(), (unsigned) detail::largest_block());
}

// ---------------------------------------------------------------------------
//  The eight actions (Engine.yaml). Status actions store the engine's answer;
//  api.respond reads it with ok() and error(). The answer is "" or exactly one
//  contract error code, which goes out unchanged as error_message.
// ---------------------------------------------------------------------------
inline bool ok() {
  const char *r = detail::result();
  return r == nullptr || r[0] == '\0';
}

inline std::string error() { return ok() ? std::string() : std::string(detail::result()); }

inline void hello(const esphome::StringRef &nonce, const esphome::StringRef &auth, JsonObject root) {
  char buf[RESPONSE_CAP];
  size_t n = tab::on_hello(nonce.c_str(), nonce.size(), auth.c_str(), auth.size(), buf, sizeof(buf));
  detail::copy_json(buf, n, root);
}

inline void status(JsonObject root) {
  char buf[RESPONSE_CAP];
  size_t n = tab::on_status(buf, sizeof(buf));
  detail::copy_json(buf, n, root);
}

inline void pair(const esphome::StringRef &nonce, const esphome::StringRef &wrapped, const esphome::StringRef &kid) {
  detail::result() =
      tab::on_pair(nonce.c_str(), nonce.size(), wrapped.c_str(), wrapped.size(), kid.c_str(), kid.size());
}

inline void layout(const esphome::StringRef &hash, int32_t seq, int32_t total, int32_t size,
                   const esphome::StringRef &data) {
  detail::result() = tab::on_layout(hash.c_str(), hash.size(), seq, total, size, data.c_str(), data.size());
}

inline void commit(const esphome::StringRef &hash, const esphome::StringRef &auth) {
  detail::result() = tab::on_commit(hash.c_str(), hash.size(), auth.c_str(), auth.size());
}

inline void set(const esphome::StringRef &data, const esphome::StringRef &auth) {
  detail::result() = tab::on_set(data.c_str(), data.size(), auth.c_str(), auth.size());
}

inline void ack(int32_t n, bool ok, const esphome::StringRef &code, const esphome::StringRef &auth) {
  tab::on_ack(n, ok, code.c_str(), code.size(), auth.c_str(), auth.size());
}

inline void clear(const esphome::StringRef &auth) { detail::result() = tab::on_clear(auth.c_str(), auth.size()); }

// `provisioning: on_timeout` in the entry files: the set-up window closed
// unprovisioned (no key, or no network yet). The engine tells the two apart:
// a restart hint without a key, a no-Wi-Fi note (off and on, then join the
// hotspot) with one.
inline void setup_window_closed() { tab::setup_window_closed(); }

#endif  // TAB_GLUE_HOST_TEST

}  // namespace tab_glue
