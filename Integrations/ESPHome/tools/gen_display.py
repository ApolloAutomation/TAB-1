#!/usr/bin/env python3
"""Generate Display.yaml — configurable, resizable tile dashboard for Apollo TAB-1.

The tile *behaviour* lives in tiles.h; this only generates the (very repetitive)
LVGL widgets, per-tile scripts and Home Assistant subscriptions.
"""

N = 12            # tiles (4x3 max)
BARS = 24         # bars in a Graph tile's sparkline (more = thinner points)
# Roller order MUST match tiles.h enum TileType.
TYPE_OPTIONS = ["Value", "Switch", "Light", "Button", "Scene", "Cover",
                "Climate", "Lock", "Fan", "Media", "Graph", "Picture", "Camera"]
# C++ array literal of the 12 entity text-box components (used by the on-device
# entity-ID editor, since a tile's text box can't be picked by a dynamic YAML id).
BOXES = ", ".join(f"id(t{i}_entity)" for i in range(N))

HEADER = '''# ============================================================================
#  Display.yaml — GENERATED. The tile dashboard UI. Board hardware (panel,
#  touch, backlight) lives in boards/*.yaml, selected in TAB-1.yaml.
#  Behaviour lives in tiles.h (included from TAB-1.yaml).
#
#   - Main page: up to 12 configurable tiles in a 4x3 cell grid.
#   - Long-press a tile to edit its TYPE and SIZE, or Remove it.
#   - Each tile binds to a Home Assistant entity via its "Tile N entity" text
#     box (editable in HA, saved to flash). Bind takes effect on restart.
#   - Swipe LEFT for Settings (brightness, demo mode, add tile).
# ============================================================================

color:
  - id: bg_dark
    hex: "0F1420"
  - id: card_bg
    hex: "1B2233"
  - id: card_pressed       # instant tap feedback, before Home Assistant answers
    hex: "2A3550"
  - id: accent
    hex: "3FA7F3"
  - id: on_color
    hex: "F4A900"
  - id: text_primary
    hex: "F2F4F8"
  - id: text_muted
    hex: "8A93A6"

time:
  - platform: homeassistant
    id: ha_time
    on_time_sync:
      - then:
          - lvgl.label.update: { id: lbl_time, text: { time_format: "%H:%M", time: ha_time } }
    on_time:
      - seconds: 0
        minutes: /1
        then:
          - lvgl.label.update: { id: lbl_time, text: { time_format: "%H:%M", time: ha_time } }

binary_sensor:
  - platform: status
    id: sys_status
    on_state:
      - lvgl.label.update:
          id: lbl_ha_status
          text: !lambda 'return std::string(x ? "Connected" : "Offline");'

# ============================================================================
#  Per-tile entity boxes (editable in Home Assistant, saved across reboots).
# ============================================================================
text:
'''


def text_entities():
    out = []
    for i in range(N):
        out.append(f'''  - platform: template
    id: t{i}_entity
    name: "Tile {i+1} entity"
    optimistic: true
    restore_value: true
    mode: text
    icon: "mdi:tag-outline"
    entity_category: config
    max_length: 64''')
    return "\n".join(out) + "\n"


GLOBALS = f'''
globals:
  # Tile type per slot (tiles::TileType). Saved; changed on-device via long-press.
  - id: tile_types
    type: 'std::array<int8_t, {N}>'
    restore_value: true
    initial_value: 'std::array<int8_t, {N}>{{}}'
  # Tile size per slot: 0=removed 1=Small 2=Wide 3=Tall 4=Big. Saved. Default all Small.
  - id: tile_sizes
    type: 'std::array<int8_t, {N}>'
    restore_value: true
    initial_value: 'std::array<int8_t, {N}>{{1,1,1,1,1,1,1,1,1,1,1,1}}'
  # Latest numeric value per tile (for Graph tiles). Runtime only.
  - id: tile_reading
    type: 'std::array<float, {N}>'
    restore_value: false
    initial_value: 'std::array<float, {N}>{{}}'
  # Graph history: {N} tiles x {BARS} samples, flattened. Runtime only.
  - id: graph_hist
    type: 'std::array<float, {N * BARS}>'
    restore_value: false
    initial_value: 'std::array<float, {N * BARS}>{{}}'
  # Demo mode shows dummy examples of every type. Runtime only (off at boot).
  - id: demo_mode
    type: bool
    restore_value: false
    initial_value: 'false'
  # Which tile the long-press editor is currently editing.
  - id: active_tile
    type: int
    restore_value: false
    initial_value: '0'
'''


def online_images():
    # One image source per tile. The URL is a placeholder until a Picture/Camera
    # tile sets it at runtime from its entity box. Decoded to a fixed size and
    # centered; unused sources never fetch (update_interval: never).
    out = ["\n# --- Image sources for Picture / Camera tiles (URL set at runtime) ---",
           "online_image:"]
    for i in range(N):
        out.append(f'''  - id: oi{i}
    url: "http://127.0.0.1/"
    format: JPEG          # camera snapshots & most web images are JPEG
    type: RGB565
    resize: 240x150
    buffer_size: 32768
    update_interval: never''')
    return "\n".join(out) + "\n"


def arr(fmt):
    return ", ".join(fmt.format(i=i) for i in range(N))


def scripts():
    names_ = arr("id(tn{i})")
    badges_ = arr("id(tt{i})")
    vals_ = arr("id(tv{i})")
    sliders_ = arr("id(ts{i})")
    graphs_ = arr("id(graphc{i})")
    images_ = arr("id(tim{i})")
    ois_ = arr("id(oi{i})")
    tiles_ = arr("id(tile{i})")
    ents_ = arr("id(t{i}_entity).state")
    gbars = ",\n            ".join(
        "{ " + ", ".join(f"id(g{i}_{j})" for j in range(BARS)) + " }" for i in range(N))
    return f'''
script:
  # ---- Repaint each tile's name / badge / value / slider / graph visibility --
  - id: render_tiles
    then:
      - lambda: |-
          lv_obj_t *names_[{N}]   = {{ {names_} }};
          lv_obj_t *badges_[{N}]  = {{ {badges_} }};
          lv_obj_t *vals_[{N}]    = {{ {vals_} }};
          lv_obj_t *sliders_[{N}] = {{ {sliders_} }};
          lv_obj_t *graphs_[{N}]  = {{ {graphs_} }};
          lv_obj_t *images_[{N}]  = {{ {images_} }};
          online_image::OnlineImage *ois_[{N}] = {{ {ois_} }};
          std::string ents_[{N}]  = {{ {ents_} }};
          for (int i = 0; i < {N}; i++) {{
            int ty = id(demo_mode) ? tiles::demo_type(i) : id(tile_types)[i];
            std::string nm = id(demo_mode) ? tiles::demo_name(i)
                                           : tiles::friendly_name(ents_[i]);
            lv_label_set_text(names_[i], nm.c_str());
            lv_label_set_text(badges_[i], tiles::type_name(ty));
            if (id(demo_mode))
              lv_label_set_text(vals_[i], tiles::demo_value(i).c_str());
            // Slider: shown for Light / Fan / Climate.
            if (tiles::has_slider(ty)) {{
              lv_obj_clear_flag(sliders_[i], LV_OBJ_FLAG_HIDDEN);
              lv_slider_set_range(sliders_[i], tiles::slider_min(ty), tiles::slider_max(ty));
            }} else {{
              lv_obj_add_flag(sliders_[i], LV_OBJ_FLAG_HIDDEN);
            }}
            // Graph tiles show bars instead of the big value label.
            if (tiles::shows_graph(ty)) {{
              lv_obj_clear_flag(graphs_[i], LV_OBJ_FLAG_HIDDEN);
              lv_obj_add_flag(vals_[i], LV_OBJ_FLAG_HIDDEN);
            }} else {{
              lv_obj_add_flag(graphs_[i], LV_OBJ_FLAG_HIDDEN);
              if (tiles::shows_image(ty)) lv_obj_add_flag(vals_[i], LV_OBJ_FLAG_HIDDEN);
              else lv_obj_clear_flag(vals_[i], LV_OBJ_FLAG_HIDDEN);
            }}
            // Image tiles (Picture / Camera): show the image and (re)load its URL.
            if (tiles::shows_image(ty)) {{
              lv_obj_clear_flag(images_[i], LV_OBJ_FLAG_HIDDEN);
              if (!id(demo_mode) && !ents_[i].empty()) {{
                ois_[i]->set_url(ents_[i]);
                ois_[i]->update();
              }}
            }} else {{
              lv_obj_add_flag(images_[i], LV_OBJ_FLAG_HIDDEN);
            }}
          }}
      - script.execute: paint_graphs

  # ---- Pack tiles into the 4x3 grid and position them absolutely ------------
  - id: layout_tiles
    then:
      - lambda: |-
          static tiles::TileRect rects[{N}];
          int8_t sz[{N}];
          for (int i = 0; i < {N}; i++)
            sz[i] = id(demo_mode) ? tiles::demo_size(i) : id(tile_sizes)[i];
          tiles::layout(sz, {N}, rects, 1232, 700, 16);
          lv_obj_t *tw_[{N}] = {{ {tiles_} }};
          for (int i = 0; i < {N}; i++) {{
            if (rects[i].shown) {{
              lv_obj_clear_flag(tw_[i], LV_OBJ_FLAG_HIDDEN);
              lv_obj_set_pos(tw_[i], rects[i].x, rects[i].y);
              lv_obj_set_size(tw_[i], rects[i].w, rects[i].h);
            }} else {{
              lv_obj_add_flag(tw_[i], LV_OBJ_FLAG_HIDDEN);
            }}
          }}

  # ---- Repaint the sparkline bars of every Graph tile -----------------------
  - id: paint_graphs
    then:
      - lambda: |-
          lv_obj_t *gb[{N}][{BARS}] = {{
            {gbars}
          }};
          for (int i = 0; i < {N}; i++) {{
            int ty = id(demo_mode) ? tiles::demo_type(i) : id(tile_types)[i];
            if (!tiles::shows_graph(ty)) continue;
            float *h = &id(graph_hist)[i * {BARS}];
            for (int j = 0; j < {BARS}; j++)
              lv_bar_set_value(gb[i][j], tiles::history_bar_pct(h, {BARS}, j), LV_ANIM_OFF);
          }}

  # ---- Subscribe every configured entity to its tile (called at boot) -------
  - id: subscribe_ha
    then:
{subscribe_blocks()}
  # ---- Demo mode ------------------------------------------------------------
  - id: enter_demo
    then:
      - lambda: |-
          id(demo_mode) = true;
          for (int i = 0; i < {N}; i++) {{
            if (tiles::demo_type(i) == tiles::TT_GRAPH)
              for (int j = 0; j < {BARS}; j++)
                id(graph_hist)[i * {BARS} + j] = 50.0f + 40.0f * sinf(j * 0.7f);
          }}
      - script.execute: render_tiles
      - script.execute: layout_tiles
  - id: exit_demo
    then:
      - lambda: 'id(demo_mode) = false;'
      - script.execute: render_tiles
      - script.execute: layout_tiles

  # ---- Save the on-screen keyboard's text into the active tile's entity box -
  - id: save_entity
    then:
      - lambda: |-
          text::Text *boxes[{N}] = {{ {BOXES} }};
          std::string v = std::string(lv_textarea_get_text(id(entity_input)));
          boxes[id(active_tile)]->make_call().set_value(v).perform();
      - script.execute: subscribe_ha
      - script.execute: render_tiles

# ---- Feed the Graph tiles from their latest value, every 30s ---------------
interval:
  - interval: 30s
    then:
      - lambda: |-
          if (id(demo_mode)) return;
          for (int i = 0; i < {N}; i++)
            if (tiles::shows_graph(id(tile_types)[i]) && !isnan(id(tile_reading)[i]))
              tiles::history_push(&id(graph_hist)[i * {BARS}], {BARS}, id(tile_reading)[i]);
      - script.execute: paint_graphs

  # ---- Re-fetch Camera tiles every 5s (Picture tiles load once) -------------
  - interval: 5s
    then:
      - lambda: |-
          if (id(demo_mode)) return;
          online_image::OnlineImage *ois_[{N}] = {{ {ois_} }};
          std::string ents_[{N}] = {{ {ents_} }};
          for (int i = 0; i < {N}; i++)
            if (tiles::is_camera(id(tile_types)[i]) && !ents_[i].empty())
              ois_[i]->update();'''


def subscribe_blocks():
    blocks = []
    for i in range(N):
        blocks.append(f'''      - lambda: |-
          std::string e{i} = id(t{i}_entity).state;
          if (!e{i}.empty()) {{
            api::global_api_server->subscribe_home_assistant_state(
                e{i}, optional<std::string>(),
                std::function<void(const std::string &)>([](const std::string &s) {{
              if (id(demo_mode)) return;
              id(tile_reading)[{i}] = tiles::parse_number(s);
              lv_label_set_text(id(tv{i}), tiles::format_state(id(tile_types)[{i}], s).c_str());
              if (tiles::state_is_active(s)) lv_obj_add_state(id(tile{i}), LV_STATE_CHECKED);
              else lv_obj_clear_state(id(tile{i}), LV_STATE_CHECKED);
            }}));
          }}''')
    return "\n".join(blocks) + "\n"


LVGL_HEAD = f'''
# ============================================================================
#  The dashboard UI
# ============================================================================
lvgl:
  displays:
    - dsi_display
  touchscreens:
    - touchscreen_id: touchscreen_id
      long_press_time: 900ms      # longer hold so swipes don't open the editor
  rotation: 270°
  buffer_size: 12%   # 1/8 screen → fits internal SRAM; much faster than a PSRAM buffer
  default_font: montserrat_20
  bg_color: bg_dark

  theme:
    label:
      text_color: text_primary

  style_definitions:
    - id: card
      bg_color: card_bg
      bg_opa: COVER
      radius: 16
      border_width: 0
      pad_all: 18
      text_color: text_primary
    - id: card_title
      text_color: text_muted
      text_font: montserrat_20
    - id: tile_card
      bg_color: card_bg
      bg_opa: COVER
      radius: 16
      border_width: 0
      pad_all: 12
      text_color: text_primary
    - id: tile_name
      text_color: text_primary
      text_font: montserrat_20
    - id: tile_badge
      text_color: text_muted
      text_font: montserrat_16
    - id: tile_value
      text_color: accent
      text_font: montserrat_36
    - id: spark_bar
      bg_color: bg_dark
      bg_opa: COVER
      radius: 1
      border_width: 0
      pad_all: 0

  # ---- Long-press tile editor: type + size ---------------------------------
  top_layer:
    widgets:
      - obj:
          id: type_picker
          hidden: true
          align: CENTER
          width: 680
          height: 620
          bg_color: card_bg
          bg_opa: COVER
          radius: 18
          border_width: 0
          widgets:
            - label: {{ text: "Edit tile", styles: card_title, align: TOP_MID, y: 14 }}
            - button:
                align: TOP_MID
                y: 44
                width: 360
                height: 50
                widgets:
                  - label: {{ text: "Set entity ID", align: CENTER }}
                on_click:
                  - lambda: |-
                      text::Text *boxes[{N}] = {{ {BOXES} }};
                      lv_textarea_set_text(id(entity_input), boxes[id(active_tile)]->state.c_str());
                  - lvgl.widget.show: entity_editor
            - label: {{ text: "Type", styles: card_title, align: TOP_LEFT, x: 78, y: 112 }}
            - label: {{ text: "Size", styles: card_title, align: TOP_RIGHT, x: -86, y: 112 }}
            - roller:
                id: type_roller
                align: CENTER
                x: -160
                y: 16
                width: 280
                options: {TYPE_OPTIONS}
                visible_row_count: 5
            - roller:
                id: size_roller
                align: CENTER
                x: 160
                y: 16
                width: 280
                options: ["Small", "Wide", "Tall", "Big"]
                visible_row_count: 5
            - button:
                align: BOTTOM_LEFT
                x: 18
                y: -18
                width: 200
                height: 62
                bg_color: accent
                widgets:
                  - label: {{ text: "Set", align: CENTER, text_color: bg_dark }}
                on_click:
                  - lambda: |-
                      int i = id(active_tile);
                      id(tile_types)[i] = (int8_t) id(type_roller)->get_selected_index();
                      id(tile_sizes)[i] = (int8_t) (id(size_roller)->get_selected_index() + 1);
                  - lvgl.widget.hide: type_picker
                  - script.execute: render_tiles
                  - script.execute: layout_tiles
            - button:
                align: BOTTOM_MID
                y: -18
                width: 200
                height: 62
                widgets:
                  - label: {{ text: "Remove", align: CENTER }}
                on_click:
                  - lambda: 'id(tile_sizes)[id(active_tile)] = 0;'
                  - lvgl.widget.hide: type_picker
                  - script.execute: layout_tiles
            - button:
                align: BOTTOM_RIGHT
                x: -18
                y: -18
                width: 200
                height: 62
                widgets:
                  - label: {{ text: "Cancel", align: CENTER }}
                on_click:
                  - lvgl.widget.hide: type_picker

      # ---- On-screen keyboard to type a tile's entity_id -------------------
      - obj:
          id: entity_editor
          hidden: true
          align: CENTER
          width: 1180
          height: 720
          bg_color: card_bg
          bg_opa: COVER
          radius: 12
          border_width: 0
          widgets:
            - label: {{ text: "Entity ID", styles: card_title, align: TOP_LEFT, x: 24, y: 18 }}
            - textarea:
                id: entity_input
                align: TOP_MID
                y: 48
                width: 92%
                height: 58
                one_line: true
                max_length: 64
                placeholder_text: "sensor.living_room_temperature"
            - button:
                align: TOP_LEFT
                x: 24
                y: 118
                width: 190
                height: 54
                widgets:
                  - label: {{ text: "Cancel", align: CENTER }}
                on_click:
                  - lvgl.widget.hide: entity_editor
            - button:
                align: TOP_RIGHT
                x: -24
                y: 118
                width: 190
                height: 54
                bg_color: accent
                widgets:
                  - label: {{ text: "Save", align: CENTER, text_color: bg_dark }}
                on_click:
                  - script.execute: save_entity
                  - lvgl.widget.hide: entity_editor
                  - lvgl.widget.hide: type_picker
            - keyboard:
                id: kb
                textarea: entity_input
                align: BOTTOM_MID
                width: 100%
                height: 430

  pages:
    # ==================== Main: the tile grid =============================
    - id: main_page
      bg_color: bg_dark
      scrollbar_mode: "OFF"
      pad_all: 0
      on_swipe_left:
        # The release that ends a swipe would also "click" the tile under the
        # finger (toggling it); wait_release swallows it.
        - lambda: 'lv_indev_wait_release(lv_indev_active());'
        - lvgl.page.show: {{ id: settings_page, animation: NONE }}
      widgets:
        - obj:
            align: TOP_MID
            y: 0
            width: 1280
            height: 64
            bg_color: bg_dark
            bg_opa: COVER
            border_width: 0
            radius: 0
            pad_all: 0
            scrollable: false
            widgets:
              - label: {{ id: lbl_time, text: "--:--", text_font: montserrat_28, align: LEFT_MID, x: 24 }}
              - label: {{ text: "Home", text_font: montserrat_28, align: CENTER }}
              - label: {{ id: lbl_ha_status, text: "Offline", text_color: text_muted, align: RIGHT_MID, x: -24 }}
        - obj:
            align: TOP_MID
            y: 72
            width: 1232
            height: 700
            bg_opa: TRANSP
            border_width: 0
            pad_all: 0
            scrollbar_mode: "OFF"
            scrollable: false
            widgets:
'''


def tile_widget(i):
    bars = "\n".join(
        f'''                          - bar: {{ id: g{i}_{j}, flex_grow: 1, height: 100%, clickable: false, styles: spark_bar, indicator: {{ bg_color: accent }} }}'''
        for j in range(BARS))
    return f'''              - obj:
                  id: tile{i}
                  width: 296
                  height: 216
                  styles: tile_card
                  clickable: true
                  scrollable: false
                  pressed:
                    bg_color: card_pressed
                  checked:
                    border_width: 3
                    border_color: on_color
                  widgets:
                    - label: {{ id: tn{i}, text: "Tile {i+1}", styles: tile_name, align: TOP_LEFT }}
                    - label: {{ id: tt{i}, text: "Value", styles: tile_badge, align: TOP_RIGHT }}
                    - label: {{ id: tv{i}, text: "--", styles: tile_value, align: CENTER, y: 6 }}
                    - image:
                        id: tim{i}
                        hidden: true
                        src: oi{i}
                        align: CENTER
                    - obj:
                        id: graphc{i}
                        hidden: true
                        align: BOTTOM_MID
                        width: 100%
                        height: 55%
                        bg_opa: TRANSP
                        border_width: 0
                        pad_all: 0
                        scrollable: false
                        clickable: false   # plain objs/bars are clickable by default and would eat the tile's taps
                        layout: {{ type: flex, flex_flow: ROW, pad_column: 2 }}
                        widgets:
{bars}
                    - slider:
                        id: ts{i}
                        hidden: true
                        gesture_bubble: false
                        align: BOTTOM_MID
                        width: 100%
                        height: 26
                        radius: 13
                        indicator: {{ radius: 13 }}
                        knob: {{ pad_all: 8, radius: 13 }}
                        on_release:
                          - if:
                              condition: {{ lambda: 'return id(tile_types)[{i}] == tiles::TT_LIGHT;' }}
                              then:
                                - homeassistant.action:
                                    action: light.turn_on
                                    data: {{ entity_id: !lambda 'return id(t{i}_entity).state;', brightness_pct: !lambda 'return (int) x;' }}
                          - if:
                              condition: {{ lambda: 'return id(tile_types)[{i}] == tiles::TT_FAN;' }}
                              then:
                                - homeassistant.action:
                                    action: fan.set_percentage
                                    data: {{ entity_id: !lambda 'return id(t{i}_entity).state;', percentage: !lambda 'return (int) x;' }}
                          - if:
                              condition: {{ lambda: 'return id(tile_types)[{i}] == tiles::TT_CLIMATE;' }}
                              then:
                                - homeassistant.action:
                                    action: climate.set_temperature
                                    data: {{ entity_id: !lambda 'return id(t{i}_entity).state;', temperature: !lambda 'return x;' }}
                  on_click:
                    - if:
                        condition:
                          lambda: 'return !id(demo_mode) && tiles::taps_act(id(tile_types)[{i}]) && !id(t{i}_entity).state.empty();'
                        then:
                          - homeassistant.action:
                              action: !lambda 'return tiles::tap_service(id(tile_types)[{i}], id(t{i}_entity).state);'
                              data:
                                entity_id: !lambda 'return id(t{i}_entity).state;'
                  on_long_press:
                    - lambda: |-
                        id(active_tile) = {i};
                        id(type_roller)->set_selected_index(id(tile_types)[{i}], LV_ANIM_OFF);
                        int sc = id(tile_sizes)[{i}];
                        id(size_roller)->set_selected_index(sc < 1 ? 0 : sc - 1, LV_ANIM_OFF);
                    - lvgl.widget.show: type_picker'''


SETTINGS = '''
    # ==================== Settings (swipe left to reach) ==================
    - id: settings_page
      bg_color: bg_dark
      scrollbar_mode: "OFF"
      on_swipe_right:
        - lambda: 'lv_indev_wait_release(lv_indev_active());'
        - lvgl.page.show: { id: main_page, animation: NONE }
      widgets:
        - label: { text: "Settings", text_font: montserrat_28, align: TOP_MID, y: 24 }
        - obj:
            align: TOP_MID
            y: 90
            width: 900
            height: 150
            styles: card
            widgets:
              - label: { text: "Screen Brightness", styles: card_title, align: TOP_LEFT }
              - slider:
                  id: sld_backlight
                  gesture_bubble: false
                  align: BOTTOM_MID
                  width: 100%
                  height: 28
                  radius: 14
                  min_value: 10
                  max_value: 100
                  value: 100
                  indicator: { radius: 14 }
                  knob: { pad_all: 8, radius: 14 }
                  on_value:
                    - script.execute:
                        id: set_backlight
                        percent: !lambda 'return x;'
        - obj:
            align: TOP_MID
            y: 260
            width: 900
            height: 92
            styles: card
            widgets:
              - label: { text: "Demo mode (show every tile type)", align: LEFT_MID }
              - switch:
                  id: demo_switch
                  align: RIGHT_MID
                  on_value:
                    - if:
                        condition: { lambda: 'return x;' }
                        then:
                          - script.execute: enter_demo
                        else:
                          - script.execute: exit_demo
        - button:
            align: TOP_MID
            y: 380
            width: 320
            height: 64
            bg_color: accent
            widgets:
              - label: { text: "+ Add tile", align: CENTER, text_color: bg_dark }
            on_click:
              - lambda: |-
                  for (int i = 0; i < 12; i++)
                    if (id(tile_sizes)[i] == 0) { id(tile_sizes)[i] = 1; break; }
              - script.execute: layout_tiles
        - label: { text: "Long-press a tile to change type/size. Swipe right to go back.", styles: card_title, align: BOTTOM_MID, y: -18 }
'''


def main():
    parts = [HEADER, text_entities(), online_images(), GLOBALS, scripts(), LVGL_HEAD]
    parts.append("\n".join(tile_widget(i) for i in range(N)) + "\n")
    parts.append(SETTINGS)
    out = "".join(parts)
    with open("Display.yaml", "w") as f:
        f.write(out)
    print("wrote Display.yaml (%d lines)" % (out.count("\n") + 1))


main()
