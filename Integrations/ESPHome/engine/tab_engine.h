// TAB-1 dashboard engine: the whole interface between the core and the
// ESPHome package (design section 7.2). The core draws a device layout that
// Home Assistant sends at runtime (contract/CONTRACT.md) with plain LVGL.
//
// Header only: C++17, the LVGL C API and the C library. No ESPHome and no
// ESP-IDF code, so the same files build on the tablet and on a PC.
#ifndef TAB_ENGINE_H
#define TAB_ENGINE_H

#include <cstddef>
#include <cstdint>
#include "lvgl.h"
#include "tab_files.h"  // TAB1_FILES_ENGINE, checked by Engine.yaml

namespace tab {

// The sizes of contract.json `fonts`, in its order.
struct Fonts {
  const lv_font_t *text[7];    // 14, 15, 16, 18, 20, 28, 34 px
  const lv_font_t *digits[3];  // 48, 96, 192 px, only fonts.digits_glyphs (digits, . , - : % + space, U+00B0)
  const lv_font_t *icon[4];    // 24, 28, 48, 72 px, the glyphs of icons.json up to level 2
};

struct Platform {
  const char *mac;                                    // 12 lower-case hex, lives forever
  const char *name;                                   // friendly name, for the waiting screen
  uint32_t boot;                                      // boot counter
  void *(*alloc)(size_t bytes);                       // one PSRAM block, or nullptr. Never internal RAM.
  void (*release)(void *block);
  size_t (*largest_block)();                          // largest free PSRAM block
  uint32_t (*free_internal)();
  uint32_t (*free_psram)();
  bool (*api_key)(uint8_t out[32]);                   // false: no encryption key
  bool (*secret_load)(uint8_t out[32]);               // false: none stored
  bool (*secret_save)(const uint8_t in[32]);
  size_t (*cache_size)();                             // 0: nothing stored
  bool (*cache_load)(uint8_t *dst, size_t cap);
  bool (*cache_save)(const uint8_t *data, size_t n);  // see below
  void (*cache_erase)();
  bool (*cache_busy)();                               // a save or erase has not finished
  bool (*cache_failed)();                             // true once: an accepted save could not be written
  bool (*ha_connected)();                             // a client that subscribed to states is connected
  void (*send_event)(const char *name, const char *d, const char *s);  // s may be nullptr
  void (*random)(uint8_t *out, size_t n);
  void (*log)(int level, const char *line);           // 0 error, 1 warning, 2 info, 3 debug
  // Hold-to-erase (design 10.8). Either one nullptr: no hold-to-erase.
  uint32_t (*uptime_ms)();                            // milliseconds since power-on
  void (*erase_all)();                                // forget Wi-Fi, API key, pairing and layout, leave
                                                      // the erase mark, then restart
  // The erase mark (design 10.11), nothing secret: the hold-to-erase erased
  // this tablet and no API key came since. The waiting screen then sends the
  // owner to Home Assistant's question in Repairs, not to add a new device.
  // The engine calls erased_clear() once when it sees a key.
  bool erased;
  void (*erased_clear)();                             // nullptr: nothing to clear
  // The waiting screen's state and the Wi-Fi notes over a dashboard. Read at
  // most twice a second while the waiting screen shows, until the set-up
  // window closes on a tablet that lacks a key or Wi-Fi (a tablet with both
  // never sees it close); over a dashboard only while Home Assistant is not
  // connected, and after the window closed with a key and no network; never
  // on the touch path. nullptr: unknown. Without network_up the tablet counts
  // as on a network (no Wi-Fi note), but the note after a closed window stays
  // until the restart.
  bool (*network_up)();                               // connected to a network
  bool (*hotspot_on)();                               // the set-up hotspot is on
  const char *hotspot;                                // its name (SSID), lives forever; nullptr: none
};

// Start. Shows the stored layout or the waiting screen. Call once, after LVGL is up.
void begin(const Fonts &fonts, const Platform &platform);

// The set-up window closed before the tablet had everything it needs (ESPHome
// `provisioning: on_timeout`). Without a key the waiting screen asks for a
// restart. With one the tablet only had no network for the whole window; a
// note says to turn it off and on and join the hotspot, until the network is
// back.
void setup_window_closed();

// Calls from Home Assistant. Each returns "" or one contract error code.
// The returned text is a constant that lives forever. String arguments need
// not be NUL-terminated; the length is given. None is kept after the call.
// tab_hello and tab_status write their JSON answer into out (NUL-terminated)
// and return its length, or 0 if cap is too small. 768 bytes is always enough.
size_t      on_hello(const char *nonce, size_t nonce_len,
                     const char *auth, size_t auth_len, char *out, size_t cap);
size_t      on_status(char *out, size_t cap);
const char *on_pair(const char *nonce, size_t nonce_len,
                    const char *wrapped, size_t wrapped_len,
                    const char *kid, size_t kid_len);
const char *on_layout(const char *hash, size_t hash_len, int seq, int total, int size,
                      const char *data, size_t data_len);
const char *on_commit(const char *hash, size_t hash_len, const char *auth, size_t auth_len);
const char *on_set(const char *data, size_t data_len, const char *auth, size_t auth_len);
void        on_ack(int n, bool ok, const char *code, size_t code_len,
                   const char *auth, size_t auth_len);
const char *on_clear(const char *auth, size_t auth_len);

// For tests and diagnostics.
const char *active_hash();          // "" when nothing is shown
const char *staged_hash();          // "" when nothing is being built
bool building();                    // a committed layout is still being built
int  page_count();
int  current_page();
void go_to(int page);
int  slot_count();
char slot_type(int slot);           // 't' 'b' 'n' 'l' 'i', or 0 for a bad slot number
bool slot_null(int slot);           // true: no value (also for a bad slot number)
bool slot_text(int slot, char *out, size_t cap);          // false: not a text slot, or null
bool slot_bool(int slot, bool *out);
bool slot_number(int slot, double *out);
bool slot_icon(int slot, uint32_t *out);
int  slot_list(int slot, double *out, int cap);           // count copied, -1: not a list, or null
bool erase_asking();                // the erase question is on screen

}  // namespace tab

// The implementation, in plain headers next to this one.
#include "tab_core.h"

#endif  // TAB_ENGINE_H
