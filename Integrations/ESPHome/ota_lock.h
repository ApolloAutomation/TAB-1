#pragma once
// ============================================================================
//  ota_lock.h — once the tablet has its API key, only that key installs firmware
//
//  The production firmware ships with no shared secret: an open set-up
//  hotspot, `api: encryption:` with no key (Home Assistant provisions one on
//  first connect) and `ota: password: ""`. ESPHome 2026.10 cannot require
//  encrypted updates with a key that only exists at runtime, so this file
//  closes the two plaintext doors itself as soon as the tablet has its key,
//  or the set-up window (`provisioning:`) has closed without one:
//
//    - network updates (port 3232) get a password nobody knows; an upload
//      encrypted with the API key never asks for it (ESPHome's Noise path);
//    - the set-up page's firmware upload (POST /update) answers 403.
//
//  When the key is in the YAML (the bench file, or a tablet adopted in
//  ESPHome Builder) the firmware always has a key, so it locks at boot too.
//  ESPHome never writes such a key to the tablet's settings, so this file
//  does (keep_key): firmware installed later that reads its key from there,
//  like Apollo's release through "Firmware Update", then starts with the same
//  key and locked, instead of with no key and an open set-up window.
//
//  The lock holds until the next boot. A tablet that has a key locks again at
//  boot, before its update port opens. Safe mode runs neither on_boot nor
//  on_loop; there `safe_mode: on_safe_mode` locks the port (safe mode has no
//  web OTA handler, so the page needs no gate there).
//
//  When Home Assistant removes the tablet it clears the key. The tablet then
//  restarts about 2 s later, so a fresh set-up window opens and closes again
//  instead of staying open for the rest of that boot.
//
//  The password is hex(SHA-256(key || 32 random bytes || CPU counters)) when
//  the tablet has a key it can read, 32 random bytes as hex otherwise. Every
//  copy of the key and the random bytes is wiped after use.
//
//  Every function runs on the main loop: the OTA password is a plain
//  std::string read by the OTA state machine on the same task. The 403 gate
//  runs on the web server task and only reads an atomic flag.
//
//  The part in namespace tab_lock::core has no ESPHome or ESP-IDF code, so
//  tools/check_engine_package.py --host builds and tests it on a computer
//  with TAB_LOCK_HOST_TEST defined.
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

// The version of this file. ESPHome builds an ota_lock.h from the ESPHome
// config folder instead of this one when there is one there, so Engine.yaml
// stops a build whose copy has another number, or none (engine/tab_files.h).
#define TAB1_FILES_LOCK 12

#include "engine/tab_crypto.h"  // the engine's plain C++ SHA-256
#if __has_include("engine/tab_files.h")  // an older engine/ has none: the #error below says so
#include "engine/tab_files.h"
#endif
#if !defined(TAB1_FILES_ENGINE) || TAB1_FILES_ENGINE != TAB1_FILES_LOCK
#error "ota_lock.h and engine/ come from different copies: delete the old ota_lock.h or engine/ from your ESPHome config folder (docs/engine.md, \"Old copies of the package's files\")"
#endif

#ifndef TAB_LOCK_HOST_TEST
#include <string>

#include <esp_cpu.h>

#include "esphome/core/defines.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/components/esphome/ota/ota_esphome.h"
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif
#ifdef USE_PROVISIONING
#include "esphome/components/provisioning/provisioning.h"
#endif
#ifdef USE_NETWORK
#include "esphome/components/network/util.h"
#endif
#ifdef USE_WEBSERVER_OTA
#include "esphome/components/web_server_base/web_server_base.h"
#endif

// The lock needs a door it can close. ESPHome builds the update password
// check (USE_OTA_PASSWORD) only when `password:` is in the `ota: esphome`
// block, and a client that does not ask for encryption goes from the feature
// exchange straight to the upload (ota_esphome.cpp, FEATURE_ACK). So without
// that check this file could set no password and plaintext updates would stay
// open. The only other safe build is one that requires encrypted updates with
// the key in the YAML (the bench file): port 3232 then refuses every client
// that does not encrypt with that key. A runtime key with `encryption:` in
// the ota block is not enough: until Home Assistant provisions the key,
// nothing is offered and nothing is checked.
#if !defined(USE_OTA_PASSWORD) && \
    !(defined(USE_OTA_ENCRYPTION_REQUIRED) && defined(USE_OTA_ENCRYPTION) && !defined(USE_OTA_ENCRYPTION_PROVISIONED))
#error "ota_lock.h: the `ota: - platform: esphome` block needs `password: \"\"` (ota_lock.h sets it at runtime), or `encryption:` with the API key in the YAML. Without either, plaintext updates would stay open."
#endif
#endif  // TAB_LOCK_HOST_TEST

namespace tab_lock {

inline void wipe(void *p, size_t n) {
  volatile auto *v = static_cast<volatile uint8_t *>(p);
  while (n-- > 0)
    *v++ = 0;
}

// ---------------------------------------------------------------------------
//  The decisions, without ESPHome (host-tested)
// ---------------------------------------------------------------------------
namespace core {

// Restart this long after the key was cleared: Home Assistant's removal call
// has its answer by then.
static constexpr uint32_t CLEARED_REBOOT_MS = 2000;

// What poll() sees on one main loop pass.
struct Inputs {
  bool key_active;      // the api server holds a key
  bool has_window;      // a provisioning manager exists
  bool provisioned;     // every provisioning source has its credentials
  bool closed;          // the set-up window expired without them
  bool setup_complete;  // every component ran setup()
  bool network;         // connected to a network
  uint32_t now_ms;
};

struct State {
  bool locked;       // the update password is set and the page gate is on
  bool had_key;      // a key was active during this boot
  bool cleared;      // ... and is gone now
  bool restarting;   // a restart was asked for; nothing more to do
  bool fail_logged;  // "could not lock" was logged
  uint32_t cleared_at;
};

enum Action : uint8_t {
  NONE = 0,
  LOCK_KEY,        // lock: the tablet has its key
  LOCK_CLOSED,     // lock: the window closed without a key
  KEY_CLEARED,     // the key was cleared; a restart follows (log it)
  RESTART_CLEARED, // restart now: the key was cleared CLEARED_REBOOT_MS ago
  RESTART_OFFLINE, // restart now: the window closed while offline, and the network is back
};

inline Action step(State &s, const Inputs &in) {
  if (s.restarting)
    return NONE;
  // Once the key was cleared the restart follows, also when a key comes back
  // in the meantime: only a restart closes the set-key path again.
  if (s.cleared) {
    if (in.now_ms - s.cleared_at >= CLEARED_REBOOT_MS) {  // unsigned: safe across the wrap
      s.restarting = true;
      return RESTART_CLEARED;
    }
    return NONE;
  }
  if (in.key_active) {
    s.had_key = true;
  } else if (s.had_key) {
    s.cleared = true;
    s.cleared_at = in.now_ms;
    return KEY_CLEARED;
  }
  if (s.locked) {
    // A tablet that has a key but no network for the whole window after a
    // boot ends up "closed": the api server then turns Home Assistant away
    // until the next boot (api_connection.cpp, on_hello_request). Once the
    // network is back, one restart brings it back to normal.
    if (in.has_window && in.closed && in.key_active && in.network) {
      s.restarting = true;
      return RESTART_OFFLINE;
    }
    return NONE;
  }
  if (in.key_active)
    return LOCK_KEY;
  if (in.has_window && in.setup_complete) {
    // save_noise_psk() reports "provisioned" at once and makes the key live
    // 100 ms later (api_server.cpp), so this usually locks before the key works.
    if (in.provisioned)
      return LOCK_KEY;
    if (in.closed)
      return LOCK_CLOSED;
  }
  return NONE;
}

// A build with its key in the YAML: must the copy in the settings be written?
// Yes when there is none, or it holds another key (an older one from Home
// Assistant). An all-zero key is no key, in the YAML or in the settings.
inline bool stored_key_stale(const uint8_t *yaml_key, const uint8_t *stored) {
  if (yaml_key == nullptr)
    return false;
  uint8_t any = 0, diff = stored == nullptr ? 1 : 0;
  for (int i = 0; i < 32; i++) {
    any |= yaml_key[i];
    if (stored != nullptr)
      diff |= static_cast<uint8_t>(yaml_key[i] ^ stored[i]);
  }
  return any != 0 && diff != 0;
}

enum Made : uint8_t { MADE_NONE = 0, MADE_RANDOM, MADE_KEY, MADE_KEY_AND_RANDOM };

// The update password as 64 lower-case hex characters and a NUL in out.
//   key and random:  hex(SHA-256(key || random || counter))
//   random only:     hex(random)
//   key only:        hex(SHA-256(key || counter)); still unknown to anyone
//                    without the key, who could update with the key anyway
//   neither:         nothing (out is all NUL); the caller must not lock with it
// key and random are 32 bytes or nullptr. counter is 8 bytes, little-endian.
inline Made make_password(const uint8_t *key, const uint8_t *random, uint64_t counter, char out[65]) {
  memset(out, 0, 65);
  if (key == nullptr && random == nullptr)
    return MADE_NONE;
  uint8_t digest[32];
  if (key == nullptr) {
    tab::crypto::to_hex(random, 32, out);
    out[64] = 0;
    return MADE_RANDOM;
  }
  uint8_t count[8];
  for (int i = 0; i < 8; i++)
    count[i] = static_cast<uint8_t>(counter >> (8 * i));
  tab::crypto::Sha256 sha;
  tab::crypto::sha256_init(sha);
  tab::crypto::sha256_update(sha, key, 32);
  if (random != nullptr)
    tab::crypto::sha256_update(sha, random, 32);
  tab::crypto::sha256_update(sha, count, sizeof count);
  tab::crypto::sha256_final(sha, digest);
  tab::crypto::to_hex(digest, sizeof digest, out);
  out[64] = 0;
  wipe(&sha, sizeof sha);
  wipe(digest, sizeof digest);
  wipe(count, sizeof count);
  return random != nullptr ? MADE_KEY_AND_RANDOM : MADE_KEY;
}

}  // namespace core

#ifndef TAB_LOCK_HOST_TEST
// ---------------------------------------------------------------------------
//  The tablet side
// ---------------------------------------------------------------------------
static const char *const TAG = "tab.lock";

inline core::State &state() {
  static core::State s{};
  return s;
}

// Read by the 403 gate on the web server task.
inline std::atomic<bool> &page_locked() {
  static std::atomic<bool> value{false};
  return value;
}

// The firmware has a key: saved in flash (provisioned by Home Assistant), or
// built in from the YAML. Reads flash directly, so it works before the api
// server has loaded the key and in safe mode, which has no api server.
inline bool key_saved() {
#if defined(USE_API_NOISE) && defined(USE_OTA_ENCRYPTION_PROVISIONED)
  esphome::noise::psk_t psk{};
  bool saved = esphome::api::load_saved_noise_psk(psk);
  wipe(psk.data(), psk.size());
  return saved;
#elif defined(USE_OTA_ENCRYPTION)
  // The key is in the YAML (bench, or a tablet adopted in ESPHome Builder):
  // such a build always has one, also in safe mode.
  return true;
#else
  return false;
#endif
}

// The api server holds a key (provisioned, or from the YAML).
inline bool key_active() {
#ifdef USE_API_NOISE
  auto *server = esphome::api::global_api_server;
  return server != nullptr && server->get_noise_ctx().has_psk();
#else
  return false;
#endif
}

// Copies the key for the password: from the api server, or from flash before
// the api server loaded it and in safe mode. A key built in from the YAML
// cannot be read in safe mode (only the OTA component holds it there); the
// password is then random alone.
inline bool read_key(uint8_t out[32]) {
#ifdef USE_API_NOISE
  esphome::noise::psk_t psk{};
  bool ok = false;
  auto *server = esphome::api::global_api_server;
  if (server != nullptr && server->get_noise_ctx().has_psk()) {
    server->get_noise_ctx().load_psk(psk);
    ok = !esphome::noise::NoiseContext::is_all_zeros(psk);
  }
#ifdef USE_OTA_ENCRYPTION_PROVISIONED
  if (!ok)
    ok = esphome::api::load_saved_noise_psk(psk);
#endif
  if (ok)
    memcpy(out, psk.data(), 32);
  wipe(psk.data(), psk.size());
  return ok;
#else
  (void) out;
  return false;
#endif
}

// Where ESPHome keeps a key from Home Assistant (NOISE_PSK_PREF_HASH in
// api_server.cpp; tools/check_engine_package.py --compile compares the two).
static constexpr uint32_t KEY_SLOT = 88491486UL;

// A build with the key in its YAML (a tablet adopted in ESPHome Builder, the
// bench file) keeps the copy in the settings equal to that key. ESPHome only
// reserves the place in such a build. Without the copy, Apollo's release
// installed over it from Home Assistant ("Firmware Update") would start with
// no key: updates unlocked and a set-up window open to the whole network.
// Written only when it differs, so once per key. Home Assistant cannot clear
// a key that is in the YAML; an erase removes the copy until the next start.
inline void keep_key() {
#if defined(USE_API_NOISE) && defined(USE_API_NOISE_PSK_FROM_YAML)
  auto *server = esphome::api::global_api_server;
  if (server == nullptr || !server->get_noise_ctx().has_psk())
    return;
  esphome::api::SavedNoisePsk yaml{};
  esphome::api::SavedNoisePsk stored{};
  server->get_noise_ctx().load_psk(yaml.psk);
  static esphome::ESPPreferenceObject pref =
      esphome::global_preferences->make_preference<esphome::api::SavedNoisePsk>(KEY_SLOT, true);
  const bool have = pref.load(&stored);
  if (core::stored_key_stale(yaml.psk.data(), have ? stored.psk.data() : nullptr)) {
    if (pref.save(&yaml) && esphome::global_preferences->sync()) {
      ESP_LOGI(TAG, "Kept this firmware's API key in the settings for later firmware");
    } else {
      ESP_LOGE(TAG, "Could not keep the API key in the settings; do not install Apollo's release over this firmware");
    }
  }
  wipe(&yaml, sizeof yaml);
  wipe(&stored, sizeof stored);
#endif
}

#ifdef USE_WEBSERVER_OTA
// Registered before the web OTA handler, and the first handler that accepts a
// request gets it (web_server_idf.cpp, request_handler_ and
// handle_multipart_upload_), so while locked this one takes POST /update.
class UpdateGate : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    if (!page_locked().load(std::memory_order_relaxed) || request->method() != HTTP_POST)
      return false;
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->url_to(url_buf) == "/update";
  }
  void handleRequest(AsyncWebServerRequest *request) override {
    request->send(403, "text/plain", "Firmware updates need this tablet's API key");
  }
  // The upload is read and dropped; nothing reaches the OTA backend.
  void handleUpload(AsyncWebServerRequest *, const std::string &, size_t, uint8_t *, size_t, bool) override {}
  bool isRequestHandlerTrivial() const override { return false; }
};
#endif

// Sets the password nobody knows and turns the page gate on. False when no
// password could be made (no key to read and no random bytes): the gate is
// on, network updates stay as they were, and poll() tries again.
inline bool lock(esphome::ESPHomeOTAComponent *ota, const char *why) {
  core::State &s = state();
  if (s.locked)
    return true;
  page_locked().store(true);
#ifdef USE_OTA_PASSWORD
  uint8_t key[32];
  uint8_t rnd[32];
  char pw[65];
  const bool have_key = read_key(key);
  const bool have_rnd = esphome::random_bytes(rnd, sizeof rnd);
  const uint64_t counter = (static_cast<uint64_t>(esp_cpu_get_cycle_count()) << 32) | esphome::micros();
  const core::Made made = core::make_password(have_key ? key : nullptr, have_rnd ? rnd : nullptr, counter, pw);
  wipe(key, sizeof key);
  wipe(rnd, sizeof rnd);
  if (made == core::MADE_NONE) {
    // Never fall back to a guessable password.
    if (!s.fail_logged) {
      ESP_LOGE(TAG, "No random bytes and no key: plaintext updates stay open; trying again");
      s.fail_logged = true;
    }
    wipe(pw, sizeof pw);
    return false;
  }
  if (made == core::MADE_KEY) {
    ESP_LOGW(TAG, "No random bytes; the update password comes from the API key alone");
  }
  ota->set_auth_password(std::string(pw, 64));
  wipe(pw, sizeof pw);
  s.locked = true;
  ESP_LOGI(TAG, "Updates locked to the API key (%s)", why);
#else
  // Only a build that requires encrypted updates with the key in its YAML
  // gets here (the #error above stops every other one): port 3232 already
  // takes nothing else, so there is no password to set.
  (void) ota;
  s.locked = true;
  ESP_LOGI(TAG, "Updates need the OTA encryption key (%s)", why);
#endif
  return true;
}

// esphome: on_boot, priority 700. That is before Wi-Fi (setup priority 250)
// can start the hotspot and before the OTA and web OTA components (200) open
// their doors, so the gate is first in the web server's handler list and a
// tablet with a key never accepts a plaintext upload, not even right after boot.
inline void boot(esphome::ESPHomeOTAComponent *ota) {
#ifdef USE_WEBSERVER_OTA
  auto *base = esphome::web_server_base::global_web_server_base;
  if (base != nullptr)
    base->add_handler_without_auth(new UpdateGate());  // NOLINT: lives as long as the server
#endif
  if (key_saved())
    lock(ota, "key in flash or firmware");
  keep_key();
}

// esphome: on_loop, every main loop pass (about every 2 ms here).
inline void poll(esphome::ESPHomeOTAComponent *ota) {
  core::Inputs in{};
  in.key_active = key_active();
#ifdef USE_PROVISIONING
  auto *pm = esphome::provisioning::global_provisioning_manager;
  in.has_window = pm != nullptr;
  in.provisioned = pm != nullptr && pm->is_provisioned();
  in.closed = pm != nullptr && pm->closed();
#endif
  in.setup_complete = esphome::App.is_setup_complete();
#ifdef USE_NETWORK
  in.network = esphome::network::is_connected();
#endif
  in.now_ms = esphome::millis();
  switch (core::step(state(), in)) {
    case core::LOCK_KEY:
      lock(ota, "key provisioned");
      break;
    case core::LOCK_CLOSED:
      lock(ota, "set-up window closed without a key");
      break;
    case core::KEY_CLEARED:
      ESP_LOGW(TAG, "The API key was cleared; restarting in 2 s for a fresh set-up window");
      break;
    case core::RESTART_CLEARED:
      esphome::App.safe_reboot();
      break;
    case core::RESTART_OFFLINE:
      ESP_LOGW(TAG, "Set-up window closed while offline; restarting to reconnect");
      esphome::App.safe_reboot();
      break;
    default:
      break;
  }
}

// safe_mode: on_safe_mode. Safe mode registers only the components that come
// before the safe mode check in setup(): logger, Wi-Fi with the hotspot and
// its set-up page, mDNS, the OTA port and safe_mode itself. No api server, no
// provisioning, no on_boot, no on_loop, and no web OTA handler (the page then
// has no /update at all). This runs after App.setup() and before the first
// loop, so before the OTA port handles any connection.
inline void safe_mode(esphome::ESPHomeOTAComponent *ota) {
  if (key_saved())
    lock(ota, "safe mode");
  // Never provisioned: stays as open as its set-up window would be.
}
#endif  // TAB_LOCK_HOST_TEST

}  // namespace tab_lock
