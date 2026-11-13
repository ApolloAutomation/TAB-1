# Apollo TAB-1 — ESPHome Dashboard Handoff

**What this is:** firmware for the Waveshare **ESP32-P4-NANO + 10.1" DSI LCD**
that turns it into a configurable **Home Assistant touch dashboard** — a grid of
tiles you can re-type, resize, and bind to HA entities right on the device.

**Status:** compiles cleanly on `esphome` **2026.7.1** (local, macOS/homebrew).
RAM ≈ **19%**, Flash ≈ **19%** of the app partition. **Not yet flashed to real
hardware** — the "Verify on hardware" list below is the outstanding QA.

**100% mainline ESPHome — no external components / forks.** (The original config
was a broken kitchen-sink built on `p1ngb4ck/esphome@dev` forks; that's what we
tore out.)

---

## Hardware

| Part | Detail |
|------|--------|
| SoC | ESP32-P4 (`variant: ESP32P4`, 16 MB flash, 360 MHz, 32 MB PSRAM hex/200 MHz) |
| WiFi | **No native radio** — on-board ESP32-C6 co-processor over SDIO (`esp32_hosted`, pins reset54/clk18/cmd19/d0-d3=14-17) |
| Display | 10.1" MIPI-DSI, model `WAVESHARE-P4-NANO-10.1` (native 800×1280 → **1280×800 landscape** via `lvgl: rotation: 270°`, PPA-accelerated on P4) |
| DSI power | internal LDO channel 3 @ 2.5 V (`esp_ldo`) |
| Backlight/panel power | I²C chip @ **0x45** on `i2c_bus_a`; register **0x96** = PWM brightness (driven at boot + by the Settings slider) |
| Touch | GT911 on `i2c_bus_a` |

---

## Files

```
Integrations/ESPHome/
├── TAB-1.yaml     # FLASH THIS. Entry point: substitutions, on_boot power/scripts,
│                  #   wifi/api/ota/logger/web_server/http_request, includes tiles.h,
│                  #   pulls in Core.yaml + Display.yaml as packages.
├── Core.yaml      # Board/system: esp32 P4, psram, esp_ldo, esp32_hosted (C6 wifi),
│                  #   i2c bus, a few diagnostic sensors. (~75 lines)
├── Display.yaml   # GENERATED. Panel + GT911 touch + the whole LVGL tile dashboard,
│                  #   12 entity text-boxes, 12 online_image sources, globals, scripts.
│                  #   ~2050 lines. See "The generator" below. Hand-editable now.
├── tiles.h        # ★ Tile BEHAVIOUR (C++). Table-driven, documented. THE place to
│                  #   add/change tile types. Included via `esphome: includes:`.
└── tools/
    └── gen_display.py   # Regenerates Display.yaml (the repetitive per-tile YAML).
```

---

## Build / flash / verify

All commands run from the repo root (`Integrations/ESPHome/TAB-1.yaml` is the target):

```bash
# Validate config (fast, schema only)
esphome config Integrations/ESPHome/TAB-1.yaml

# Full compile (real toolchain — the trustworthy check; catches lambda/LVGL errors)
esphome compile Integrations/ESPHome/TAB-1.yaml

# Flash over USB-C (P4 uses USB-Serial-JTAG)
esphome run Integrations/ESPHome/TAB-1.yaml
```

> **Always `esphome compile`, not just `config`** — config validation does NOT
> catch C++ lambda / LVGL-C-API mistakes. Several real bugs this project hit
> (e.g. a label lambda returning `const char*` instead of `std::string`) only
> surfaced at compile.

In Home Assistant, after the device appears: enable **"Allow the device to
perform Home Assistant actions"** on the device, or tiles can read state but not
control anything.

---

## What the dashboard does

- **Main page:** up to **12 tiles** packed into a **4×3 cell grid** (the max).
- **13 tile types** (behaviour defined in `tiles.h`):
  `Value, Switch, Light (tap toggles + slider dims), Button, Scene, Cover,
  Climate (slider = setpoint), Lock, Fan (tap toggles + slider = speed),
  Media (tap = play/pause), Graph, Picture, Camera`.
- **Resize / remove / add:** each tile is Small (1×1), Wide (2×1), Tall (1×2),
  Big (2×2), or removed. A first-fit packer (`tiles::layout()`) places them;
  tiles that don't fit are hidden. "+ Add tile" in Settings brings a removed one back.
- **Per-tile entity binding:** each tile has a **"Tile N entity"** text box
  (editable in HA *and* on-device) holding an entity_id — or, for Picture/Camera,
  an image URL.
- **Graph tiles:** a 24-bar sparkline of the entity's numeric history, auto-scaled,
  updated every 30 s.
- **Picture/Camera tiles:** fetch a JPEG from the URL. Picture loads once; Camera
  re-fetches every 5 s.
- **Settings page** (swipe **left** from main, swipe right to go back): screen
  brightness, **Demo mode**, "+ Add tile".
- **Demo mode:** fills all 12 tiles with dummy examples of every type (no HA
  needed). Non-destructive — turn it off and your real config returns.

### On-device editing

- **Long-press a tile** (hold ~0.9 s) → editor with **Type** + **Size** rollers,
  a **Set entity ID** button (on-screen keyboard), **Set / Remove / Cancel**.
- Type/size changes are **live**. Entity changes are saved and re-subscribed;
  a **restart** guarantees a fully clean binding.

### Persistence (survives power cycle)

Everything is saved to flash via `restore_value`:
- `text` entities `t0_entity … t11_entity` — the entity_id / URL per tile.
- global `tile_types` (`std::array<int8_t,12>`) — each tile's type.
- global `tile_sizes` (`std::array<int8_t,12>`) — each tile's size.

Runtime-only (not saved): `tile_reading`, `graph_hist`, `demo_mode`, `active_tile`.

---

## How runtime HA binding works (the important bit)

ESPHome normally bakes `homeassistant` entity subscriptions at **compile time**.
To let users pick entities at runtime, the boot script `subscribe_ha` calls
`api::global_api_server->subscribe_home_assistant_state(<the text-box value>, …)`
for each tile. This needs `api: homeassistant_states: true` (set in TAB-1.yaml),
and the callback must be wrapped `std::function<void(const std::string&)>(…)` to
disambiguate the overload. **Because subscriptions form at boot, changing a
tile's entity takes effect on the next restart** (an on-device edit also
re-subscribes live, but restart is the clean path).

Control (tap / slider) uses `homeassistant.action` with the service name and
entity_id computed by lambdas from `tiles.h` — so no per-type YAML branching for
taps.

---

## How to add / change a tile type

Almost everything lives in **`tiles.h`**, which has a step-by-step guide at the
top. In short:

1. Add a value to `enum TileType` (append **before** `TT_COUNT` so saved values
   keep their meaning).
2. Add a row to `TYPE_SPECS[]` in the same order (name, has_slider, slider range,
   taps_act, shows_graph, shows_image).
3. Add the same name, same order, to the **Type roller options** in the generator
   / `Display.yaml`.
4. If a tap should call HA, add a `case` to `tap_service()`; if the slider should,
   extend `slider_service()` / `slider_data_key()`.
5. If it shows a value, teach `format_state()`.

The grid, packing, persistence, subscriptions, editor and demo mode are all
generic and pick the new type up automatically.

---

## The generator

`Display.yaml` is produced by **`tools/gen_display.py`** because the 12 tiles are
highly repetitive (each has its own widgets, subscription lambda, graph bars,
image source, and control actions). Key knobs at the top: `N` (tiles), `BARS`
(graph bar count), `TYPE_OPTIONS` (roller order — must match the `tiles.h` enum).

```bash
# Regenerate after changing the generator (run from Integrations/ESPHome so it
# writes Display.yaml next to TAB-1.yaml — mind the working directory!):
cd Integrations/ESPHome && python3 tools/gen_display.py && esphome compile TAB-1.yaml
```

`Display.yaml` is also perfectly fine to **hand-edit** now; just know that
re-running the generator overwrites it. (If you plan to hand-edit long-term,
delete the generator or keep changes in the generator instead.)

---

## Verify on hardware (outstanding QA — compile can't check these)

- **Touch orientation** — if taps land mirrored/rotated, uncomment the `transform`
  under `touchscreen:` in Display.yaml and adjust `swap_xy` / `mirror_x` / `mirror_y`.
- **Label re-centering on resize** — LVGL should re-apply alignment when a tile
  changes size; confirm a Big tile's value stays centered.
- **Brief boot overlap** — tiles overlap for a moment before `layout_tiles` runs
  (~boot priority 100). Harmless; can hide the grid until layout if it bothers you.
- **Swipe-to-settings over tiles** — gestures should bubble to the page; if finicky
  over a tile, enable gesture bubbling.
- **Picture/Camera** — needs a reachable JPEG snapshot URL.

---

## Known limitations & suggested next steps

1. **Picture/Camera:** JPEG only (change the one `format:` line for PNG); image is
   a fixed **240×150 centered** regardless of tile size; **Camera = refreshing
   snapshot, not live video** (mainline ESPHome limit). HA cameras need a **direct
   snapshot URL** the device can reach — HA's authenticated camera-proxy URLs
   won't work without a token. *Next:* scale image to tile size; optional
   per-tile format.
2. **Sliders are send-only** — the Light/Fan/Climate sliders send values but don't
   reflect the entity's *current* level (they start where you left them). *Next:*
   subscribe to the `brightness` / `percentage` / `temperature` attribute and set
   the slider on state updates.
3. **On-device entity edit** re-subscribes live, which can leave duplicate
   subscriptions until a restart. *Next:* per-tile re-subscribe, or a small
   "restart to apply" confirmation.
4. **More types** are easy to add (Alarm panel, richer Media with prev/next,
   Weather, Number/input_number, Select/input_select).
5. **API encryption** is commented out in TAB-1.yaml — add a key for a secure HA pairing.

---

## Repo / git note

- The live config is **`Integrations/ESPHome/`**. `TAB-1.yaml` includes
  `Core.yaml` and `Display.yaml` from that same folder.
- ⚠️ There is a **stray `Display.yaml` at the repo root** (`TAB-1/Display.yaml`).
  It was created accidentally by the generator's relative-path write landing in
  the wrong working directory during a couple of runs, and it is **not used by
  the firmware**. Recommend removing it (`git rm Display.yaml` from the repo root)
  so only `Integrations/ESPHome/Display.yaml` remains. Left in place here rather
  than deleted without your say-so.
- Suggested commit set: `Integrations/ESPHome/{TAB-1.yaml,Core.yaml,Display.yaml,
  tiles.h,tools/gen_display.py,HANDOFF.md}` and removal of the root `Display.yaml`.
