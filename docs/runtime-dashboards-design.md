# TAB-1 runtime dashboards: architecture and design (rev 2)

**Status: historical.** This is the design proposal of 2026-11-10, written
before the dashboard engine was built. It is kept for its reasoning, not as a
spec. The protocol the firmware implements is
[`Integrations/ESPHome/engine_tests/contract/CONTRACT.md`](../Integrations/ESPHome/engine_tests/contract/CONTRACT.md);
where the two differ, CONTRACT.md wins. The firmware overview is
[`Integrations/ESPHome/docs/engine.md`](../Integrations/ESPHome/docs/engine.md).

Tags: **[V]** verified in research, **[L]** likely, **[S]** speculative, **[R]** a reviewer's source citation that I accepted but did not re-check. No new measurements were made in this revision.

## 1. Recommendation

Build a **native runtime engine on the device, fed by one HACS/Marketplace integration**. Do not ship an add-on.

- **Device:** an ESPHome external component builds LVGL widgets at runtime from a pushed layout. Dashboard changes need no reflash.
- **HA side:** one integration does layout, import, theme resolution, Jinja, history decimation and asset conversion. HACS cannot ship add-ons **[V]**.
- **Pretty comes first from design:** 2–3 curated Apollo themes and a designed card set, built only from cheap style features. "Match my HA theme" is an optional colour import.
- **Editing:** the user edits a dedicated HA dashboard in HA's own sections editor ("mirror mode"). There is no custom drag-and-drop designer.
- **Existing dashboards:** imported as a starting point (entities, names, icons, order, grouping, actions) into the designed auto-layout. They are not translated 1:1.
- **Custom cards:** mapped by meaning onto TAB-1 cards. They are not visually emulated.
- **Baking:** only the wallpaper is pre-rendered. Card chrome is drawn natively.
- **Cut from the committed roadmap:** the headless-Chromium App and the QuickJS/button-card sandbox.

## 2. Architecture comparison

| | Touch response | Visual quality | Import coverage | Effort | Verdict |
|---|---|---|---|---|---|
| **A. Native runtime engine + integration** | Local, ≤25 ms to pressed pixel | High with designed cards | Built-ins plus semantic mapping | High | **Chosen** |
| B. A + server image tiles | Same; image tiles 0.1–0.5 s **[S]** | Highest | Near-total for read-only cards | A + a second product | Deferred; revisit only if import data demands it |
| C. Pure server rendering | 0.6–1.5 s (Puppet) **[V]** | Pixel-exact Lovelace | 100% | Medium | Rejected: loses the speed advantage, and ~19.5% of installs cannot run Apps **[V]** |
| D. Compile-time YAML | Local | As A | As A | Low | Rejected: minutes per change |
| E. openHASP / HASPmota | Local | Dated **[L]** | None | — | Rejected: LVGL 7 with no P4, or leaving ESPHome **[V]** |
| F. LVGL XML | — | — | — | — | Closed: removed in 9.5, restrictive licence **[V]** |

- **Position:** "instant and designed for the wall, starting from your HA setup". An Android tablet in kiosk mode will always win on exact Lovelace fidelity; TAB-1 wins on touch latency.
- **Borrow ideas only:** openHASP's "HA evaluates, device draws" split and espcontrol's single contract file **[V]**.
- **Binding:** use LVGL's open-source `lv_observer`; only the XML layer is closed **[V]**.

## 3. Recommended system

### 3.1 HA side: integration `apollo_tab`

- **Discovery:** device registry by manufacturer/model from the ESPHome project name **[V]**. One layout per panel; each panel has its own area, and auto-layout leads with that room.
- **Auto-layout (ships first):** builds pages from the area, floor and entity registries using the designed card set. This serves users whose dashboards are browser-generated strategies (`config_not_found`) **[V]**.
- **Mirror mode:** one dedicated HA dashboard per panel is the single source of truth. The integration re-syncs on `lovelace_updated` **[V]**. It ships `tab1-*` Lovelace cards plus a preview card.
- **Import mode:** a one-time extraction from any dashboard into a new mirror dashboard. No re-sync afterwards.
- **Preview:** the device engine compiled to WebAssembly with the same `lv_conf.h`, loading the same server-made assets **[L]**.
- **Resolver:** theme variables to concrete values, plus HA's state-colour lookup ported to Python **[V]**.
- **Live evaluation:** `async_track_template_result`, server-side visibility, and history decimated to pixel width **[V]**.
- **Assets:** Pillow (an HA core dependency **[R]**); numpy goes in the manifest requirements because it is only a version constraint **[R]**.
- **Compatibility module:** all HA internals (`LOVELACE_DATA`, the ESPHome client) sit behind one file. CI runs against each HA beta.
- **Diagnostics:** an HA diagnostics dump with layout, import report and versions.
- **Actions:** run under a per-panel HA user context, so the logbook shows which panel acted.

### 3.2 Transport

Everything goes over the ESPHome native API. The link is already encrypted and needs no URL, token or TLS. HTTP pull is dropped: TLS would cost tens of KB of internal RAM **[R]**, and `http_request` blocks the main loop **[L]**.

| Flow | Mechanism |
|---|---|
| Layout and assets, HA to device | `tab_blob(kind, seq, total, hash, data)`, ≤24 KB per call, acknowledged per chunk. Binary is base64, so about 18 KB of payload per chunk. A 150 KB wallpaper is about 9 calls. |
| State, HA to device | `tab_set(data)` with **one string argument** carrying packed `slot=value` deltas. |
| Input, device to HA | `esphome.tab1_input {w, g, v, n, mac}` event. |
| Result, HA to device | `tab_ack(w, n, ok)` after the service call returns. |
| Handshake | The device publishes `layout_hash` and `engine_caps` as text sensors. The integration reads them on connect, then pushes the layout if stale and a full state snapshot. |

- **Why one string for state:** `string[]` arguments allocate `std::string`s from the ~90 KB internal heap on every call. A scalar string is a zero-copy view into the PSRAM receive buffer **[R]** (`api/__init__.py:116-124`).
- **Why not a connect-time event:** events sent before HA subscribes are dropped **[R]** (`api_server.cpp:537-551`).
- **Recorder problem:** every `hass.services.async_call` is stored as a `call_service` event with its data **[R]**. So high-rate state pushes, blobs and secrets go through the ESPHome client object directly, behind the compatibility module. This is a private API and a real maintenance risk. The fallback is the public service at a reduced rate. I reject the reviewer's alternative of documenting a recorder exclude: it hides all service-call history.
- **Event gating:** resolved. `esphome.*` events are not gated by "allow device actions" **[R]** (`manager.py:351-376`).
- **Spoofing:** any ESPHome device can fire `esphome.*` events. The two reviewers disagree on whether `device_id` can be overridden, so assume it can. Each panel gets a secret at pairing, and each event carries a counter and an HMAC. The integration rejects mismatches.
- **Layout load:** built in a second buffer and swapped atomically on a hash match. Partial transfers are discarded.
- **Version skew:** `engine_caps` lists widget types and the schema range. The integration down-levels and offers firmware through the ESPHome update entity. Support the current and previous engine version.
- **Degraded mode:** simple controls carry a direct fallback action (`domain.service` plus entity). If no ack arrives, the device uses `homeassistant.action`, as today's tiles do. With no integration installed, the panel keeps today's 12-slot mode.
- **Existing bug:** `save_entity` re-runs `subscribe_ha` and stacks callbacks (`Display.yaml:378-395, 562`) **[L]**.

### 3.3 Device side

- **Engine:** C++ against the pure LVGL C API, with a thin ESPHome adapter. One source builds for device, host tests and WASM.
- **Threading:** every LVGL call on ESPHome's main loop **[V]**.
- **Memory:** a per-layout PSRAM arena for all engine state (widget records, binding tables, strings), reset on reload. No `new`, `std::function` or `std::vector` on the default heap. Wallpaper buffers are preallocated at boot. Before each load, check the largest free PSRAM block: one failed `lv_malloc` moves LVGL to internal RAM permanently **[V]**.
- **Styling:** `lv_obj_remove_style_all()` and shared `lv_style_t` tokens. This also removes the default theme's 80 ms transitions **[V]**. A light/dark switch is one style update, because chrome is native.
- **Pages:** all built at layout load; a switch is `lv_screen_load` **[V]**.
- **Layout:** the integration sends absolute pixel rectangles. No on-device grid solver.
- **Fonts and icons, MVP:** embedded in firmware. The app slot is 7.75 MB and firmware is 1.78 MB **[V]**. That means a base font covering Latin Extended, Greek and Cyrillic at the type scale, plus the full 899 KB MDI TTF rasterised by tiny_ttf (about 1 ms per cold glyph **[S]**). Runtime icons and text never render blank. Other scripts chain through LVGL's font `fallback` to a pushed TTF later.
- **Storage, MVP:** no repartitioning. The layout (tens of KB) lives in NVS; the wallpaper is re-pushed at boot. A new partition table ships from the factory only. OTA repartitioning can brick a unit on power loss **[R]** (`ota_partitions_esp_idf.cpp:190-195`).
- **Build flags:** OBSERVER, ARC, SCALE, CHART, SPAN, LINE, A8, RGB565A8, TINY_TTF, SNAPSHOT, moved to `esphome: build_flags` **[V]**. Leave the shadow cache at 0.
- **PSRAM budget:** measure free PSRAM on today's firmware first. The earlier table omitted the 2 MB framebuffer, the rotate buffer and code running from PSRAM **[R]**.

### 3.4 Schema (own JSON, versioned)

One contract file generates the firmware constants, the Python validator and the card palette.

```json
{"v":1,"hash":"9f2c","theme":{"bg":"#111111","card":"#1c1c1c","r":12},
 "wall":"a/wall.jpg",
 "pages":[{"id":"home","w":[
  {"id":7,"t":"tile","r":[32,96,384,64],"icon":"mdi:lightbulb",
   "s":{"state":12,"color":13,"on":14,"vis":18},
   "feat":[{"t":"slider","s":15}],"tap":"toggle","policy":"optimistic",
   "fb":"light.toggle|light.kitchen"}]}]}
```

- `s` values are slot numbers. The real HA action stays in the integration, keyed by widget id.
- The schema cannot express over-budget styles. Blur, layered opacity, angled gradients and scaled images have no live form.
- **Widget set:** tile with features, heading, button, row list, slider/toggle, arc, graph, weather, media, clock, image, popup, confirm modal.

### 3.5 What happens on a tap

1. The touch is sampled (5 ms loop). The engine forces an immediate refresh rather than waiting for LVGL's 16 ms timer **[R]** (`lv_conf.h:8`). Budget: pressed pixel within 25 ms.
2. Local behaviour follows a per-domain policy:

| Policy | Domains | Behaviour |
|---|---|---|
| Optimistic | light, switch, fan, input_boolean | Flips at once; reverts only on an error ack or a missing ack |
| Sent pulse | script, scene, button | Brief "sent" pulse; no state expected |
| Progress | cover, climate set-point | Shows "opening…" or the target until state arrives; long per-domain timeout |
| Confirmed only | lock, alarm, garage | Native confirm modal; never shows the new state before HA reports it |

3. Lovelace's `confirmation` option becomes a native modal.
4. Sliders send on release, and at most 5 Hz while dragging.

## 4. Converting existing Lovelace dashboards

### 4.1 Layout

- **Geometry decision:** 3 section columns of about 384 px with HA's 12-column sub-grid, a 64 px row unit, and type scaled inside the cards (body 18–20 px). The earlier "HA geometry scaled 1.3x" was wrong: both reviewers showed it yields 2 columns. A 2-column large-type density is a per-panel option. Validate on a real dashboard **[S]**.
- **Sections views in mirror mode** map directly.
- **Masonry, entities lists and auto-generated dashboards** go through import, then the room-aware auto-layout. A 1:1 translation of a phone dashboard would look dated **[L]**.
- **Pagination:** whole sections are packed into pages in order. A section is never split, except a section taller than one page, which splits at a row boundary with a repeated heading. No full-page scrolling.
- **View tabs** become pages on a native nav rail.
- **Strategy stubs**, including strategies on individual sections **[R]**: Python versions of the most-used ones, with a monthly fixture diff against HA beta.

### 4.2 Card mapping

| Lovelace card | TAB-1 card | Lost |
|---|---|---|
| tile, button, entity (common features) | Tile | Rare features move to a popup |
| heading, section | Heading | — |
| entities, glance | Row list | Exotic rows |
| light, thermostat, humidifier | Tile plus arc popup | Colour wheel later |
| gauge, sensor, history/statistics-graph | Arc / graph | Pan and zoom |
| weather-forecast, media-control, clock | Weather / media / clock | Fixed layouts |
| markdown | Label | HTML, tables |
| picture, picture-entity | Image | Low frame rate |
| stacks, grid | Flattened | — |
| conditional, entity-filter | Server-side visibility | — |
| map, iframe, energy, picture-elements, calendar | Omitted, listed in the report | All |

### 4.3 Conditions, templates, unsupported cards

- **Visibility:** evaluated in HA and pushed as a slot **[V]**. Because chrome is native, a hidden card disappears. The integration then pushes new rectangles for that section, so it collapses with no gap.
- **Jinja:** tracked in HA, results pushed. HA's rate limits apply **[V]**.
- **Unsupported cards:** shown in the preview and the import report, each with a "report this card" link. On the device they are omitted by default; an admin option shows placeholders.

## 5. Custom cards and styling

### 5.1 Semantic mapping

For each custom card type, extract the entity, name, icon, tap/hold actions and any server-side Jinja, then map to a TAB-1 card. Budget days per card type **[S]**.

| Card family | Handling |
|---|---|
| Mushroom, Bubble (Jinja fields), mini-media-player | Mapped to Tile / Media. Jinja colour and text honoured **[V]** |
| mini-graph-card; apexcharts simple series | Mapped to Graph; data aggregated in HA |
| stack-in-card, decluttering, auto-entities, layout-card | Expanded in the converter |
| button-card, config-template-card (browser JS) | Static fields mapped; JS templates dropped and reported |
| Everything else | Omitted and reported |

The import report says "mapped to TAB-1 Light card". It never claims visual compatibility.

### 5.2 Themes and card-mod

- **Default:** curated Apollo themes, dark-first, using surface steps and a 1 px highlight. This needs a visual designer as owner, with designed off, on, pressed, unavailable and popup states. Enforce WCAG 4.5:1 contrast and a minimum text size.
- **Optional import:** about 25 HA theme tokens resolved in HA **[V]**.
- **card-mod:** only colour, radius, border, opacity and icon overrides on the card shell. The rest is ignored and reported.
- **Wallpaper (phase 3):** one per dashboard by default. Sent as a 4:4:4 JPEG, decoded to RGB888, then dithered to RGB565 once on the CPU at load. The earlier "dithered, then JPEG" cancelled itself out. Hard card edges stay native.
- **Glass:** pre-blurred wallpaper crops as per-card image objects. They hide with their card.
- **State tints:** opaque swaps of the card colour, or translucent only on small icon badges with a schema area cap.

### 5.3 Images

Always exact size, never scaled on the device (13x cost **[L]**). Artwork is resized in HA. Check that the hardware JPEG path does not contend with `fast_display.h`'s DMA copies.

## 6. Speed guardrails

| Class | Items | Cost vs flat baseline |
|---|---|---|
| Free | Solid fills, radius, vertical gradient, thin border, A8 icons, text, 1:1 images | ≤1.25x **[L]** |
| Capped per page | Arcs, charts, horizontal gradients | 2–3x **[L]** |
| Not expressible live | Blur, drop shadow, large translucency, layered opacity, angled gradients, scaling | 4.7x–77x **[L]** |

1. **Budgets:** pressed pixel ≤25 ms. State update ≤5 ms. Page switch ≤40 ms flat, and ≤50 ms with a wallpaper. The research's own estimate for a wallpaper page is about 44 ms, and that estimate is unmeasured for PSRAM reads.
2. **Partial redraw:** each bound value is a fixed-rectangle object. No auto-sizing labels.
3. **Animation:** small areas only. 150–250 ms colour crossfades. No full-screen motion.
4. **Push rate:** deltas only, coalesced to ≤10 Hz per device.
5. **Reconnect:** full snapshot in chunks. While HA is disconnected, cached layout with a "stale" look.
6. **Enforcement:** the schema caps arcs and charts per page. Users see a simple smoothness indicator; millisecond figures are developer-mode only. Publishing is never blocked on an estimate with ±30–50% error.

## 7. Roadmap, risks, prototypes

Assumed team **[S]**: two engineers (firmware, HA/Python with light TypeScript) and a part-time visual designer. Three stacks: C++ engine with WASM, Python integration, thin TypeScript preview card.

| Phase | Duration [S] | Delivers | Exit criteria |
|---|---|---|---|
| 0. Prototypes | 3 weeks | Nothing shipped | Risks below measured |
| 1. MVP | 10–12 weeks | Integration; auto-layout by room; about 8 designed cards; 2–3 Apollo themes; optimistic policies; degraded mode; idle clock and night dimming | Adoption to first good-looking dashboard in under 5 minutes; p95 tap-to-pixel ≤25 ms; state-to-pixel under 100 ms; users rate it better than today's UI |
| 2. Edit and import | 8–10 weeks | Mirror mode; preview card; import with report; popups, graph, weather, media; presence wake; full-screen alerts | Share of imported cards mapped; support tickets per 100 units |
| 3. Polish | 6–8 weeks | Wallpaper and glass; HA theme import; extra scripts; factory repartition | Wallpaper page switch ≤50 ms |
| Uncommitted | — | Image-tile App; JS sandbox; alarm, todo, calendar | Only if import data shows demand |

**Phase 0 prototypes, in order:**

1. PSRAM-to-internal copy rate with execute-from-PSRAM on, and a wallpaper page on hardware. This decides whether wallpapers ship at all.
2. Push path: state-to-pixel latency with 50 entities, recorder growth, and whether the direct-client route holds.
3. Arena engine: 1,000 load/unload cycles, watching internal heap.
4. HMAC input path plus degraded mode on a freshly adopted device.
5. tiny_ttf icon cost on device, and WASM-vs-device screenshot diff.
6. HA-side CPU and RAM on an HA Green for template tracking and asset conversion.

**Other risks:**

- **Distribution:** HACS became the built-in Marketplace in HA 2026.11 (released 4 Nov 2026) **[V]**; custom repositories reportedly need a GitHub sign-in **[R]**. Submit to the default catalog at the first tagged release and treat inclusion as a launch gate. Review takes months **[V]**.
- **Licence:** firmware component is GPLv3 through ESPHome **[V]**. Make the integration MIT and borrow no GPL code on the HA side.
- **Differentiation is unvalidated:** interview 10–20 buyers and collect an opt-in card-type histogram before phase 2.
- **Migration:** users who edited the compile-time YAML keep that path. The engine is a package they opt into.

**Decisions for the product owner:**

- Call it a Marketplace/HACS integration, not an add-on.
- Promise "instant, designed for the wall, starts from your HA setup", not "your dashboard on the wall".
- Pages paginate; they do not scroll.
- Approve a visual designer for phase 1.

## Changes after review

- **Baking narrowed** to wallpaper and glass crops; card chrome is native. Both reviewers were right that baked chrome broke visibility, state colouring and theme switching.
- **Custom designer dropped** for mirror mode on HA's editor plus one-time import. One source of truth.
- **Card emulation replaced** by semantic mapping. App and QuickJS moved to uncommitted.
- **MVP made design-led:** curated themes and a designer; HA theme import is optional.
- **Transport reworked:** assets over the API instead of HTTP, single-string `tab_set`, text-sensor handshake, per-chunk acks, atomic swap, capability negotiation.
- **Recorder and secrets:** pushes bypass the service registry. I pushed back on the recorder-exclude suggestion.
- **Input security:** HMAC per event; event gating risk closed.
- **Optimistic UI:** per-domain policy table, explicit ack, confirmation modal, slider throttling.
- **Degraded and no-integration modes** added.
- **Fonts:** base font and full MDI TTF embedded in firmware with tiny_ttf, reversing my earlier "skip on-device TTF".
- **Storage:** no OTA repartitioning; factory only.
- **Geometry fixed:** 3 columns with larger type inside cards; 2-column option.
- **Budgets restated:** 25 ms touch, 50 ms wallpaper page switch; blocking cost meter removed.
- **Roadmap:** durations, team, exit criteria, wall-panel essentials, Marketplace distribution, MIT licence.
- **Not addressed in depth:** localisation of panel strings, and template sharing across panels.

The research notes, the reviews and the benchmark source were working files and are not in this repository.

Relevant paths: `Integrations/ESPHome/` (`Display.yaml`, `tiles.h`, `fast_display.h`, `tools/gen_display.py`, `TAB-1.yaml`, `Core.yaml`).