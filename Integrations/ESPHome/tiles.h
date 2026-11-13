#pragma once
// ============================================================================
//  tiles.h  —  behaviour for the Apollo TAB-1 configurable dashboard tiles.
//
//  This header is the ONE place that decides what each tile TYPE does. The YAML
//  (Display.yaml) is deliberately dumb — it just calls the helpers below. To
//  add or change a tile type you should only need to touch this file (plus one
//  line in the on-screen "Type" roller in Display.yaml).
//
//  ---------------------------------------------------------------------------
//  HOW TO ADD A NEW TILE TYPE
//  ---------------------------------------------------------------------------
//   1. Add a value to `enum TileType`, appended just BEFORE `TT_COUNT`.
//      (Appending — never reordering — keeps every tile that users already
//       saved to flash pointing at the same type.)
//   2. Add a matching row to `TYPE_SPECS[]`, in the SAME position as the enum
//      value. That row declares the human name, whether it uses the slider
//      (and its range), whether a tap calls Home Assistant, and whether it
//      draws a graph or an image.
//   3. Add the same human name, in the same order, to the "Type" roller's
//      `options:` list in Display.yaml so it can be picked on the device.
//   4. If a tap should call a Home Assistant action, add a `case` to
//      `tap_service()`. If the slider should call one, add a `case` to
//      `slider_service()` / `slider_data_key()`.
//   5. If the tile shows a value, teach `format_state()` how to display it.
//
//  That's it — the grid, packing, persistence, subscriptions, editor and demo
//  mode are all generic and will pick the new type up automatically.
// ============================================================================

#include <string>
#include <cctype>
#include <cstdlib>
#include <cmath>

namespace tiles {

// ---------------------------------------------------------------------------
//  1. The tile types.
//     The order here must match TYPE_SPECS[] and the on-screen "Type" roller.
// ---------------------------------------------------------------------------
enum TileType {
  TT_VALUE = 0,  // read-only: show an entity's state (sensor, etc.)
  TT_SWITCH,     // toggle an on/off entity (switch, light, input_boolean, ...)
  TT_LIGHT,      // light: tap toggles on/off, slider sets brightness
  TT_BUTTON,     // momentary: press a button / run a script / activate a scene
  TT_SCENE,      // activate a scene
  TT_COVER,      // cover: tap toggles open/close (blinds, garage, ...)
  TT_CLIMATE,    // thermostat: slider sets the target temperature
  TT_LOCK,       // lock: tap toggles lock/unlock
  TT_FAN,        // fan: tap toggles on/off, slider sets speed %
  TT_MEDIA,      // media player: tap = play / pause
  TT_GRAPH,      // history sparkline of a numeric sensor
  TT_PICTURE,    // static image fetched from a URL
  TT_CAMERA,     // image fetched from a URL, auto-refreshed
  TT_COUNT,      // <-- keep last: the number of types
};

// ---------------------------------------------------------------------------
//  2. Per-type properties. One row per TileType, in the SAME order as above.
// ---------------------------------------------------------------------------
struct TypeSpec {
  const char *name;    // shown in the picker and on the tile's type badge
  bool  has_slider;    // does this type show/use the tile's slider?
  int   slider_min;    // slider range (only used when has_slider is true)
  int   slider_max;
  bool  taps_act;      // does tapping the tile call a Home Assistant action?
  bool  shows_graph;   // does the tile draw the bar sparkline?
  bool  shows_image;   // does the tile draw an image (picture / camera)?
};

static const TypeSpec TYPE_SPECS[TT_COUNT] = {
    //  name        slider  min  max   tap    graph  image
    {  "Value",   false,    0,   0,   false, false, false },  // TT_VALUE
    {  "Switch",  false,    0,   0,   true,  false, false },  // TT_SWITCH
    {  "Light",   true,     0, 100,   true,  false, false },  // TT_LIGHT
    {  "Button",  false,    0,   0,   true,  false, false },  // TT_BUTTON
    {  "Scene",   false,    0,   0,   true,  false, false },  // TT_SCENE
    {  "Cover",   false,    0,   0,   true,  false, false },  // TT_COVER
    {  "Climate", true,    15,  30,   false, false, false },  // TT_CLIMATE
    {  "Lock",    false,    0,   0,   true,  false, false },  // TT_LOCK
    {  "Fan",     true,     0, 100,   true,  false, false },  // TT_FAN
    {  "Media",   false,    0,   0,   true,  false, false },  // TT_MEDIA
    {  "Graph",   false,    0,   0,   false, true,  false },  // TT_GRAPH
    {  "Picture", false,    0,   0,   false, false, true  },  // TT_PICTURE
    {  "Camera",  false,    0,   0,   false, false, true  },  // TT_CAMERA
};

// Safe accessor — clamps out-of-range values (e.g. from an older flash save).
inline const TypeSpec &spec(int t) {
  if (t < 0 || t >= TT_COUNT) t = TT_VALUE;
  return TYPE_SPECS[t];
}

inline const char *type_name(int t) { return spec(t).name; }
inline bool has_slider(int t)        { return spec(t).has_slider; }
inline int  slider_min(int t)        { return spec(t).slider_min; }
inline int  slider_max(int t)        { return spec(t).slider_max; }
inline bool taps_act(int t)          { return spec(t).taps_act; }
inline bool shows_graph(int t)       { return spec(t).shows_graph; }
inline bool shows_image(int t)       { return spec(t).shows_image; }
inline bool is_camera(int t)         { return t == TT_CAMERA; }  // needs refresh

// ---------------------------------------------------------------------------
//  3. Small string helpers.
// ---------------------------------------------------------------------------
inline bool starts_with(const std::string &s, const char *prefix) {
  return s.rfind(prefix, 0) == 0;
}

// "sensor.living_room_temperature"  ->  "Living room temperature"
inline std::string friendly_name(const std::string &entity) {
  if (entity.empty()) return "- unset -";
  size_t dot = entity.find('.');
  std::string n = (dot == std::string::npos) ? entity : entity.substr(dot + 1);
  for (auto &c : n) {
    if (c == '_') c = ' ';
  }
  if (!n.empty()) n[0] = (char) toupper((unsigned char) n[0]);
  return n;
}

// Parse a numeric Home Assistant state into a float (NaN if it isn't a number).
inline float parse_number(const std::string &s) {
  if (s.empty()) return NAN;
  char *end = nullptr;
  float v = strtof(s.c_str(), &end);
  return (end == s.c_str()) ? NAN : v;
}

// ---------------------------------------------------------------------------
//  4. Behaviour: what a tap does, and how a value is shown.
// ---------------------------------------------------------------------------

// Does this raw state count as "active", so the tile lights up?
inline bool state_is_active(const std::string &s) {
  return s == "on" || s == "open" || s == "opening" || s == "playing" ||
         s == "heat" || s == "cool" || s == "home" || s == "unlocked";
}

// The Home Assistant action a tap should call. Empty string means "do nothing".
// `entity` is the tile's configured entity_id (used to disambiguate buttons).
inline std::string tap_service(int t, const std::string &entity) {
  switch (t) {
    case TT_SWITCH:
    case TT_LIGHT:
    case TT_FAN:    return "homeassistant.toggle";  // domain-agnostic on/off
    case TT_COVER:  return "cover.toggle";
    case TT_LOCK:   return "lock.toggle";
    case TT_SCENE:  return "scene.turn_on";
    case TT_MEDIA:  return "media_player.media_play_pause";
    case TT_BUTTON:
      if (starts_with(entity, "input_button.")) return "input_button.press";
      if (starts_with(entity, "script."))       return "script.turn_on";
      if (starts_with(entity, "scene."))        return "scene.turn_on";
      return "button.press";
    default:
      return "";  // Value / Climate / Graph / Picture / Camera: tap does nothing
  }
}

// The service the slider should call, and the data key it should send the
// value under. (The key differs per domain, so both are needed.)
inline std::string slider_service(int t) {
  switch (t) {
    case TT_LIGHT:   return "light.turn_on";
    case TT_FAN:     return "fan.set_percentage";
    case TT_CLIMATE: return "climate.set_temperature";
    default:         return "";
  }
}
inline std::string slider_data_key(int t) {
  switch (t) {
    case TT_LIGHT:   return "brightness_pct";
    case TT_FAN:     return "percentage";
    case TT_CLIMATE: return "temperature";
    default:         return "";
  }
}

// How to display the raw HA state string for a given tile type.
inline std::string format_state(int t, const std::string &s) {
  if (t == TT_BUTTON || t == TT_SCENE) return "";   // action tiles: no value
  if (t == TT_PICTURE || t == TT_CAMERA) return "";  // image tiles: no value
  if (s.empty() || s == "unknown" || s == "unavailable") return "--";
  if (s == "on")       return "On";
  if (s == "off")      return "Off";
  if (s == "open")     return "Open";
  if (s == "closed")   return "Closed";
  if (s == "locked")   return "Locked";
  if (s == "unlocked") return "Unlocked";
  if (s == "playing")  return "Playing";
  if (s == "paused")   return "Paused";
  if (s == "idle")     return "Idle";
  return s;  // sensors and everything else: show the raw value
}

// ---------------------------------------------------------------------------
//  5. Graph history helpers (for TT_GRAPH tiles).
//     Each graph tile keeps a small ring buffer of recent values; the bars are
//     auto-scaled to that buffer's min/max so any sensor range is readable.
// ---------------------------------------------------------------------------

// Shift the buffer left by one and append `v` at the end.
inline void history_push(float *hist, int bars, float v) {
  for (int i = 0; i < bars - 1; i++) hist[i] = hist[i + 1];
  hist[bars - 1] = v;
}

// Map one history slot to a 0..100 bar height, auto-scaled to the buffer.
// Returns 0 for empty (NaN) slots.
inline int history_bar_pct(const float *hist, int bars, int slot) {
  float mn = 1e30f, mx = -1e30f;
  for (int i = 0; i < bars; i++) {
    if (!isnan(hist[i])) {
      if (hist[i] < mn) mn = hist[i];
      if (hist[i] > mx) mx = hist[i];
    }
  }
  if (mx <= mn) {          // flat or single sample: center everything
    return isnan(hist[slot]) ? 0 : 50;
  }
  if (isnan(hist[slot])) return 0;
  return (int) roundf((hist[slot] - mn) / (mx - mn) * 100.0f);
}

// ---------------------------------------------------------------------------
//  6. Grid packing: place variable-size tiles into the 4x3 cell grid.
//     size code:  0 = removed   1 = Small(1x1)  2 = Wide(2x1)
//                 3 = Tall(1x2)  4 = Big(2x2)
// ---------------------------------------------------------------------------
struct TileRect { bool shown; int x, y, w, h; };

inline void size_dims(int code, int &cw, int &ch) {
  switch (code) {
    case 1: cw = 1; ch = 1; break;  // Small
    case 2: cw = 2; ch = 1; break;  // Wide
    case 3: cw = 1; ch = 2; break;  // Tall
    case 4: cw = 2; ch = 2; break;  // Big
    default: cw = 0; ch = 0; break;  // removed
  }
}

// First-fit pack the tiles (in index order) into a 4-col x 3-row grid.
// Tiles that don't fit in the remaining cells are left hidden.
inline void layout(const int8_t *sizes, int n, TileRect *out,
                   int grid_w, int grid_h, int gap) {
  const int COLS = 4, ROWS = 3;
  int cell_w = (grid_w - (COLS - 1) * gap) / COLS;
  int cell_h = (grid_h - (ROWS - 1) * gap) / ROWS;
  bool occ[ROWS][COLS];
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) occ[r][c] = false;

  for (int i = 0; i < n; i++) {
    out[i].shown = false;
    int cw, ch;
    size_dims(sizes[i], cw, ch);
    if (cw == 0) continue;
    if (cw > COLS) cw = COLS;
    if (ch > ROWS) ch = ROWS;
    bool placed = false;
    for (int r = 0; r + ch <= ROWS && !placed; r++) {
      for (int c = 0; c + cw <= COLS && !placed; c++) {
        bool fits = true;
        for (int rr = r; rr < r + ch && fits; rr++)
          for (int cc = c; cc < c + cw && fits; cc++)
            if (occ[rr][cc]) fits = false;
        if (!fits) continue;
        for (int rr = r; rr < r + ch; rr++)
          for (int cc = c; cc < c + cw; cc++) occ[rr][cc] = true;
        out[i].shown = true;
        out[i].x = c * (cell_w + gap);
        out[i].y = r * (cell_h + gap);
        out[i].w = cw * cell_w + (cw - 1) * gap;
        out[i].h = ch * cell_h + (ch - 1) * gap;
        placed = true;
      }
    }
  }
}

// ---------------------------------------------------------------------------
//  7. Demo mode: a representative type + dummy data for each of the 12 tiles,
//     so all the types can be seen without configuring Home Assistant.
// ---------------------------------------------------------------------------
inline int demo_type(int i) {
  static const int demo[12] = {
      TT_VALUE, TT_SWITCH, TT_LIGHT,  TT_BUTTON,
      TT_SCENE, TT_COVER,  TT_CLIMATE, TT_LOCK,
      TT_FAN,   TT_MEDIA,  TT_GRAPH,  TT_CAMERA,
  };
  return demo[i];
}

inline int demo_size(int i) {
  return demo_type(i) == TT_GRAPH ? 2 /*Wide*/ : 1 /*Small*/;
}

inline std::string demo_name(int i) {
  static const char *names[12] = {
      "Temperature", "Desk lamp",  "Kitchen",   "Doorbell",
      "Movie night", "Blinds",     "Thermostat", "Front door",
      "Bedroom fan", "Speaker",    "CO2",       "Front cam",
  };
  return names[i];
}

// Dummy display value for the demo (already formatted for the screen).
inline std::string demo_value(int i) {
  static const char *vals[12] = {
      "22.4",   "On",     "On",     "",
      "",       "Open",   "21",     "Locked",
      "On",     "Playing", "612",   "",
  };
  return vals[i];
}

}  // namespace tiles
