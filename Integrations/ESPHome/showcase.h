#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "lvgl.h"

namespace showcase {

// Everything that differs between the tablet and the PC test build.
struct Assets {
  const lv_font_t *icons_28 = nullptr;       // Material Design Icons, 28 px
  const lv_font_t *icons_40 = nullptr;       // 40 px
  const lv_font_t *icons_64 = nullptr;       // 64 px
  const lv_image_dsc_t *wallpaper = nullptr; // 1280x800 RGB565, Clock page background
  const lv_image_dsc_t *digits[11] = {};     // clock digits '0'..'9' then ':' (A8 masks)
  uint32_t (*now_us)() = nullptr;            // microsecond clock for render timing (optional)
  void (*log)(const char *line) = nullptr;   // one log line, no newline (optional)
  void (*set_backlight)(int percent) = nullptr;  // screen brightness 10..100 (optional)
};

enum Page  { PG_HOME, PG_LIVING, PG_KITCHEN, PG_BEDROOM, PG_CLIMATE, PG_MEDIA, PG_SECURITY, PG_STYLES, PG_CLOCK, PAGE_COUNT };
enum Theme { TH_MIDNIGHT, TH_DAYLIGHT, TH_WARM, TH_SOFT, TH_MONO, TH_GLASS, THEME_COUNT };

void build(lv_obj_t *parent, const Assets &assets);   // create everything; shows PG_HOME; schedules the tour
void tick(uint32_t now_ms);                           // advance simulation, swipe detection and the tour
void set_clock(int year, int month, int day, int weekday, int hour, int minute, int second);

int  page_count();                  // 9
int  current_page();
void go_to(int page);               // instant switch
const char *page_name(int page);    // "Home", "Living room", ...
const char *theme_name(int theme);  // "midnight", ...
int  page_theme(int page);          // style currently applied to that page

void set_style_override(int theme); // 0..4 = that style on every page but the Clock; -1 = per-page styles
int  style_override();

void start_tour();
void cancel_tour();
bool tour_active();
uint32_t last_render_us();          // duration of the most recent render

struct State;                       // defined further down in this header
State &state();                     // the live model (tests read and poke it)
void mark_dirty();                  // repaint the visible page from the model on the next tick
lv_obj_t *page_obj(int page);       // the page container (tests walk its children)

}  // namespace showcase

// Material Design Icons used by the cards. The same list is compiled into the
// icon fonts in Showcase.yaml, so add a glyph in both places.
#define IC_HOME                   "\U000F02DC"  // mdi:home
#define IC_SOFA                   "\U000F04B9"  // mdi:sofa
#define IC_SILVERWARE_FORK_KNIFE  "\U000F0A70"  // mdi:silverware-fork-knife
#define IC_BED                    "\U000F02E3"  // mdi:bed
#define IC_HOME_THERMOMETER       "\U000F0F54"  // mdi:home-thermometer
#define IC_MUSIC_NOTE             "\U000F0388"  // mdi:music-note
#define IC_SHIELD_HOME            "\U000F068A"  // mdi:shield-home
#define IC_PALETTE                "\U000F03D8"  // mdi:palette
#define IC_CLOCK_OUTLINE          "\U000F0150"  // mdi:clock-outline
#define IC_LIGHTBULB              "\U000F0335"  // mdi:lightbulb
#define IC_LIGHTBULB_OUTLINE      "\U000F0336"  // mdi:lightbulb-outline
#define IC_LIGHTBULB_GROUP        "\U000F1253"  // mdi:lightbulb-group
#define IC_LOCK                   "\U000F033E"  // mdi:lock
#define IC_LOCK_OPEN              "\U000F033F"  // mdi:lock-open
#define IC_SHIELD_OFF             "\U000F099E"  // mdi:shield-off
#define IC_SHIELD_LOCK            "\U000F099D"  // mdi:shield-lock
#define IC_THERMOMETER            "\U000F050F"  // mdi:thermometer
#define IC_HOME_FLOOR_G           "\U000F0D85"  // mdi:home-floor-g
#define IC_HOME_FLOOR_1           "\U000F0D80"  // mdi:home-floor-1
#define IC_DOOR                   "\U000F081A"  // mdi:door
#define IC_DESK                   "\U000F1239"  // mdi:desk
#define IC_SHOWER                 "\U000F09A0"  // mdi:shower
#define IC_CHEVRON_RIGHT          "\U000F0142"  // mdi:chevron-right
#define IC_CEILING_LIGHT          "\U000F0769"  // mdi:ceiling-light
#define IC_FLOOR_LAMP             "\U000F08DD"  // mdi:floor-lamp
#define IC_LED_STRIP_VARIANT      "\U000F1051"  // mdi:led-strip-variant
#define IC_WALL_SCONCE_FLAT       "\U000F091D"  // mdi:wall-sconce-flat
#define IC_LAMP                   "\U000F06B5"  // mdi:lamp
#define IC_TELEVISION             "\U000F0502"  // mdi:television
#define IC_WATER_PERCENT          "\U000F058E"  // mdi:water-percent
#define IC_MOVIE_OPEN             "\U000F0FCE"  // mdi:movie-open
#define IC_SPA                    "\U000F0CD1"  // mdi:spa
#define IC_POWER                  "\U000F0425"  // mdi:power
#define IC_PLUS                   "\U000F0415"  // mdi:plus
#define IC_MINUS                  "\U000F0374"  // mdi:minus
#define IC_PLAY                   "\U000F040A"  // mdi:play
#define IC_PAUSE                  "\U000F03E4"  // mdi:pause
#define IC_SKIP_NEXT              "\U000F04AD"  // mdi:skip-next
#define IC_SKIP_PREVIOUS          "\U000F04AE"  // mdi:skip-previous
#define IC_FIRE                   "\U000F0238"  // mdi:fire
#define IC_SNOWFLAKE              "\U000F0717"  // mdi:snowflake
#define IC_LEAF                   "\U000F032A"  // mdi:leaf
#define IC_FAN                    "\U000F0210"  // mdi:fan
#define IC_COFFEE_MAKER           "\U000F109F"  // mdi:coffee-maker
#define IC_DISHWASHER             "\U000F0AAC"  // mdi:dishwasher
#define IC_FRIDGE_OUTLINE         "\U000F028F"  // mdi:fridge-outline
#define IC_STOVE                  "\U000F04DE"  // mdi:stove
#define IC_FLASH                  "\U000F0241"  // mdi:flash
#define IC_POT_STEAM              "\U000F065A"  // mdi:pot-steam
#define IC_BLINDS                 "\U000F00AC"  // mdi:blinds
#define IC_BLINDS_OPEN            "\U000F1011"  // mdi:blinds-open
#define IC_ARROW_UP               "\U000F005D"  // mdi:arrow-up
#define IC_ARROW_DOWN             "\U000F0045"  // mdi:arrow-down
#define IC_STOP                   "\U000F04DB"  // mdi:stop
#define IC_WEATHER_NIGHT          "\U000F0594"  // mdi:weather-night
#define IC_WHITE_BALANCE_SUNNY    "\U000F05A8"  // mdi:white-balance-sunny
#define IC_BOOK_OPEN_VARIANT      "\U000F00BE"  // mdi:book-open-variant
#define IC_ALARM                  "\U000F0020"  // mdi:alarm
#define IC_WEATHER_PARTLY_CLOUDY  "\U000F0595"  // mdi:weather-partly-cloudy
#define IC_WEATHER_CLOUDY         "\U000F0590"  // mdi:weather-cloudy
#define IC_WEATHER_SUNNY          "\U000F0599"  // mdi:weather-sunny
#define IC_WEATHER_RAINY          "\U000F0597"  // mdi:weather-rainy
#define IC_MOLECULE_CO2           "\U000F07E4"  // mdi:molecule-co2
#define IC_SOLAR_POWER            "\U000F0A72"  // mdi:solar-power
#define IC_SPEAKER                "\U000F04C3"  // mdi:speaker
#define IC_VOLUME_HIGH            "\U000F057E"  // mdi:volume-high
#define IC_CCTV                   "\U000F07AE"  // mdi:cctv
#define IC_GARAGE                 "\U000F06D9"  // mdi:garage
#define IC_GARAGE_OPEN            "\U000F06DA"  // mdi:garage-open
#define IC_CHECK                  "\U000F012C"  // mdi:check
#define IC_CHECK_CIRCLE           "\U000F05E0"  // mdi:check-circle
#define IC_CLOSE                  "\U000F0156"  // mdi:close
#define IC_BRIGHTNESS_6           "\U000F00DF"  // mdi:brightness-6
#define IC_TOGGLE_SWITCH          "\U000F0521"  // mdi:toggle-switch
#define IC_TOGGLE_SWITCH_OFF      "\U000F0522"  // mdi:toggle-switch-off
#define IC_POWER_PLUG             "\U000F06A5"  // mdi:power-plug

namespace showcase {

struct Palette {
  uint32_t wall, card, card_pr, card2, card2_pr, line, tx, tx2, acc, accf, accf_pr, acct, acc2, onbg, onbg_pr, ontx, ion;
  uint8_t radius, radius2, border;
};
static const Palette PALETTES[6] = {
  /* MIDNIGHT */ {0x101418, 0x181C23, 0x232833, 0x232833, 0x303746, 0x2A303A, 0xE8EAED, 0x9AA0A8, 0xF5B547, 0xF5B547, 0xD99A2B, 0x0E1116, 0x6CB4FF, 0x33291A, 0x4A3A20, 0xF7E3BC, 0xF5B547, 16, 10, 0},
  /* DAYLIGHT */ {0xEEF0F3, 0xFFFFFF, 0xE9ECF0, 0xE2E6EC, 0xD2D7DE, 0xDDE1E6, 0x15181D, 0x5D6570, 0xB86E00, 0x9C5C00, 0x7E4A00, 0xFFFFFF, 0x1F6FD1, 0xFFF1D6, 0xF2DFB8, 0x5A3600, 0xB86E00, 16, 10, 0},
  /* WARM     */ {0x1B1714, 0x201C18, 0x322A23, 0x322A23, 0x41372E, 0x3A322A, 0xF2EADF, 0xB3A797, 0xFF9F5A, 0xFF9F5A, 0xE0813E, 0x1B1714, 0x7FC8A9, 0x45301F, 0x5A3E27, 0xFFD9BC, 0xFF9F5A, 18, 12, 1},
  /* SOFT     */ {0xF6F3F9, 0xFFFFFF, 0xF1ECF7, 0xEFEAF5, 0xE2DAEE, 0xFFFFFF, 0x2A2433, 0x6B6279, 0x7C5CD6, 0x7C5CD6, 0x6547B8, 0xFFFFFF, 0x2F9C8C, 0xEEE7FB, 0xDDD2F6, 0x3B2A73, 0x7C5CD6, 28, 20, 0},
  /* MONO     */ {0x0A0A0A, 0x0A0A0A, 0x282828, 0x202020, 0x383838, 0xF0F4F0, 0xF0F4F0, 0xA6A6A6, 0xF0F4F0, 0xF0F4F0, 0xC8CCC8, 0x0A0A0A, 0xF0F4F0, 0xF0F4F0, 0xC8CCC8, 0x0A0A0A, 0x0A0A0A, 8, 6, 2},
  /* GLASS    */ {0x1A3E62, 0x2D4567, 0x3E5473, 0x3A4F74, 0x4A5E8C, 0x5B7096, 0xFFFFFF, 0xDCE3F0, 0xFFD48A, 0xFFD48A, 0xE0B66C, 0x10243F, 0xA8D8FF, 0x5A5648, 0x6E6850, 0xFFF4DC, 0xFFD48A, 24, 16, 1},
};

// SC_GLASS_RECTS_BEGIN  (x, y, w, h) of every frosted card on the Clock page.
// tools/gen_showcase_assets.py reads this block and paints the same shapes into
// the wallpaper, so edit the numbers here and re-run the script.
#define SC_GLASS_RADIUS 24
#define SC_GLASS_RECTS(X) \
  X(24, 80, 816, 416)    \
  X(856, 80, 400, 416)   \
  X(24, 512, 296, 240)   \
  X(336, 512, 296, 240)  \
  X(648, 512, 296, 240)  \
  X(960, 512, 296, 240)
// SC_GLASS_RECTS_END
struct Rect { int16_t x, y, w, h; };
#define SC_RECT_ENTRY(x, y, w, h) {x, y, w, h},
static const Rect GLASS_RECTS[] = { SC_GLASS_RECTS(SC_RECT_ENTRY) };

enum Room  { RM_LIVING, RM_KITCHEN, RM_HALLWAY, RM_BEDROOM, RM_OFFICE, RM_BATHROOM, ROOM_COUNT };
enum Phase { PH_IDLE, PH_CONFIRM, PH_BUSY };
struct Light  { uint8_t room; const char *name; const char *icon; bool on; uint8_t bri; };   // bri 1..100
struct Guard  { bool secured; Phase phase; uint32_t deadline; };  // lock or garage door; secured = locked / closed
struct State {
  Light lights[13];
  bool sw_tv, sw_coffee, sw_fan, sw_wake;       // switches
  int16_t thermo_target, thermo_current;         // tenths; target 160..280 in steps of 5
  uint8_t thermo_mode;                           // 0 Heat, 1 Cool, 2 Auto, 3 Off
  uint32_t thermo_pending_until;                 // ms; 0 = nothing pending
  int16_t room_temp[ROOM_COUNT];                 // tenths; room_temp[RM_LIVING] mirrors thermo_current
  uint8_t hum_living, hum_bedroom; uint16_t co2; uint8_t solar_x10, power_x10;   // 46, 51, 612, 24, 18
  uint8_t hum_living_min, hum_living_max;        // lowest and highest hum_living seen, shown as the 24 h range
  int16_t fridge_x10, oven;                      // 40, 180
  int16_t temp_hist[24], bed_temp_hist[24];      // tenths, oldest first
  uint8_t power_hist[24], kitchen_power_hist[24];// tenths of kW per hour of day, index = hour 0..23
  uint8_t cover_pos, cover_target; bool cover_moving;   // 0 closed .. 100 open
  bool media_playing; uint8_t media_track; uint16_t media_elapsed; uint8_t media_volume; bool spk_living, spk_kitchen;
  Guard lock_front, lock_back, garage;
  uint8_t alarm;                                 // 0 Disarmed, 1 Armed home, 2 Armed away
  uint8_t alarm_next; Phase alarm_phase; uint32_t alarm_deadline;
  uint8_t dish_state; uint8_t dish_left;         // 0 Running, 1 Paused, 2 Done; minutes left of 120
  int8_t scene_pulse_page, scene_pulse_idx; uint32_t scene_pulse_until;   // which scene chip is lit
  int8_t style_override;                         // -1 = per-page styles, else 0..4
  uint8_t backlight;                             // 10..100
  // clock
  int year, month, day, weekday, hour, minute;   // weekday 1 = Sunday .. 7 = Saturday
  uint32_t minute_started_ms; bool clock_from_host;
  uint32_t chart_version;                        // bumped whenever a history array changes
};

static const int16_t TEMP_HIST_INIT[24] = {206,204,203,201,200,199,199,201,205,209,212,214,216,218,219,220,219,217,215,213,212,210,209,208};
static const int16_t BED_HIST_INIT[24]  = {192,190,189,188,187,186,186,188,191,194,197,199,201,203,204,204,203,202,201,200,199,199,198,198};
static const uint8_t POWER_HIST_INIT[24]   = {4,3,3,3,3,4,9,16,12,8,7,9,14,11,8,7,9,15,24,29,22,18,12,6};
static const uint8_t KITCHEN_HIST_INIT[24] = {1,1,1,1,1,1,5,9,4,2,2,3,8,5,2,2,3,9,16,19,11,6,3,1};
struct Track { const char *title, *artist, *album; uint16_t seconds; };
static const Track TRACKS[4] = {
  {"Late night jazz", "Blue Room Quartet", "After Hours, Vol. 2", 340},
  {"Soft focus",      "Mara Lind",         "Quiet Rooms",          252},
  {"Blue hour",       "The Paper Kites Trio", "Dusk",              287},
  {"Night train",     "Odd Meter",         "Platform 9",           305},
};

struct FontStyles {
  lv_style_t F16, F20, F28, F36, F48, I28, I40, I64;
};

static FontStyles fs;

static void init_font_styles(const Assets &a) {
  lv_style_init(&fs.F16);
  lv_style_init(&fs.F20);
  lv_style_init(&fs.F28);
  lv_style_init(&fs.F36);
  lv_style_init(&fs.F48);
  lv_style_init(&fs.I28);
  lv_style_init(&fs.I40);
  lv_style_init(&fs.I64);
  lv_style_set_text_font(&fs.F16, &lv_font_montserrat_16);
  lv_style_set_text_font(&fs.F20, &lv_font_montserrat_20);
  lv_style_set_text_font(&fs.F28, &lv_font_montserrat_28);
  lv_style_set_text_font(&fs.F36, &lv_font_montserrat_36);
  lv_style_set_text_font(&fs.F48, &lv_font_montserrat_48);
  lv_style_set_text_font(&fs.I28, a.icons_28 ? a.icons_28 : &lv_font_montserrat_20);
  lv_style_set_text_font(&fs.I40, a.icons_40 ? a.icons_40 : &lv_font_montserrat_20);
  lv_style_set_text_font(&fs.I64, a.icons_64 ? a.icons_64 : &lv_font_montserrat_20);
}

struct Styles {
  lv_style_t page, card, card_pr, card_on, card_on_pr;
  lv_style_t inset, inset_on, btn, btn_pr, btn_on, btn_on_pr;
  lv_style_t tx, tx2, t_acc, t_acc2, t_on, t_ion, t_acct;
  lv_style_t fill, fill2, fill_dim, fill_on, track, knob, knob_on;
  lv_style_t arc_bg, arc_fg, line, dot, dot_on, img_tx, radius2;
};

static void init_styles(Styles &s) {
  lv_style_t *all[] = {
    &s.page, &s.card, &s.card_pr, &s.card_on, &s.card_on_pr,
    &s.inset, &s.inset_on, &s.btn, &s.btn_pr, &s.btn_on, &s.btn_on_pr,
    &s.tx, &s.tx2, &s.t_acc, &s.t_acc2, &s.t_on, &s.t_ion, &s.t_acct,
    &s.fill, &s.fill2, &s.fill_dim, &s.fill_on, &s.track, &s.knob,
    &s.knob_on, &s.arc_bg, &s.arc_fg, &s.line, &s.dot, &s.dot_on,
    &s.img_tx, &s.radius2
  };
  for (lv_style_t *v : all) lv_style_init(v);
}

static void fill_styles(Styles &s, const Palette &p, bool baked) {
  lv_style_set_bg_color(&s.page, lv_color_hex(p.wall));
  lv_style_set_bg_opa(&s.page, baked ? LV_OPA_TRANSP : LV_OPA_COVER);

  lv_style_set_bg_color(&s.card, lv_color_hex(p.card));
  lv_style_set_bg_opa(&s.card, baked ? LV_OPA_TRANSP : LV_OPA_COVER);
  lv_style_set_radius(&s.card, p.radius);
  lv_style_set_border_color(&s.card, lv_color_hex(p.line));
  lv_style_set_border_width(&s.card, baked ? 0 : p.border);
  lv_style_set_border_opa(&s.card, LV_OPA_COVER);
  // Negative padding keeps child coordinates independent of the card border.
  lv_style_set_pad_all(&s.card, baked ? 0 : -p.border);

  lv_style_set_bg_color(&s.card_pr, lv_color_hex(p.card_pr));
  lv_style_set_bg_opa(&s.card_pr, LV_OPA_COVER);

  lv_style_set_bg_color(&s.card_on, lv_color_hex(p.onbg));
  lv_style_set_bg_opa(&s.card_on, LV_OPA_COVER);
  lv_style_set_border_color(&s.card_on, lv_color_hex(p.acc));

  lv_style_set_bg_color(&s.card_on_pr, lv_color_hex(p.onbg_pr));
  lv_style_set_bg_opa(&s.card_on_pr, LV_OPA_COVER);
  lv_style_set_border_color(&s.card_on_pr, lv_color_hex(p.acc));

  lv_style_set_bg_color(&s.inset, lv_color_hex(p.card2));
  lv_style_set_bg_opa(&s.inset, LV_OPA_COVER);
  lv_style_set_radius(&s.inset, p.radius2);
  lv_style_set_bg_color(&s.inset_on, lv_color_hex(p.onbg_pr));

  lv_style_set_bg_color(&s.btn, lv_color_hex(p.card2));
  lv_style_set_bg_opa(&s.btn, LV_OPA_COVER);
  lv_style_set_radius(&s.btn, p.radius2);

  lv_style_set_bg_color(&s.btn_pr, lv_color_hex(p.card2_pr));
  lv_style_set_bg_opa(&s.btn_pr, LV_OPA_COVER);
  lv_style_set_radius(&s.btn_pr, p.radius2);

  // Where the primary fill equals the highlighted card (Mono), an outline keeps
  // the button visible. Negative padding keeps its children in place, and these
  // are set for every palette so that switching theme clears them again.
  int b = p.accf == p.onbg ? 2 : 0;
  lv_style_set_bg_color(&s.btn_on, lv_color_hex(p.accf));
  lv_style_set_bg_opa(&s.btn_on, LV_OPA_COVER);
  lv_style_set_radius(&s.btn_on, p.radius2);
  lv_style_set_border_width(&s.btn_on, b);
  lv_style_set_border_color(&s.btn_on, lv_color_hex(p.acct));
  lv_style_set_border_opa(&s.btn_on, LV_OPA_COVER);
  lv_style_set_pad_all(&s.btn_on, -b);

  lv_style_set_bg_color(&s.btn_on_pr, lv_color_hex(p.accf_pr));
  lv_style_set_bg_opa(&s.btn_on_pr, LV_OPA_COVER);
  lv_style_set_radius(&s.btn_on_pr, p.radius2);
  lv_style_set_border_width(&s.btn_on_pr, b);
  lv_style_set_border_color(&s.btn_on_pr, lv_color_hex(p.acct));
  lv_style_set_border_opa(&s.btn_on_pr, LV_OPA_COVER);
  lv_style_set_pad_all(&s.btn_on_pr, -b);

  lv_style_set_text_color(&s.tx, lv_color_hex(p.tx));
  lv_style_set_text_color(&s.tx2, lv_color_hex(p.tx2));
  lv_style_set_text_color(&s.t_acc, lv_color_hex(p.acc));
  lv_style_set_text_color(&s.t_acc2, lv_color_hex(p.acc2));
  lv_style_set_text_color(&s.t_on, lv_color_hex(p.ontx));
  lv_style_set_text_color(&s.t_ion, lv_color_hex(p.ion));
  lv_style_set_text_color(&s.t_acct, lv_color_hex(p.acct));

  lv_style_set_bg_color(&s.fill, lv_color_hex(p.acc));
  lv_style_set_bg_opa(&s.fill, LV_OPA_COVER);
  lv_style_set_radius(&s.fill, 4);

  lv_style_set_bg_color(&s.fill2, lv_color_hex(p.acc2));
  lv_style_set_bg_opa(&s.fill2, LV_OPA_COVER);
  lv_style_set_radius(&s.fill2, 4);

  lv_style_set_bg_color(&s.fill_dim, lv_color_hex(p.tx2));
  lv_style_set_bg_opa(&s.fill_dim, LV_OPA_COVER);
  lv_style_set_radius(&s.fill_dim, 4);

  lv_style_set_bg_color(&s.fill_on, lv_color_hex(p.ion));

  lv_style_set_bg_color(&s.track, lv_color_hex(p.card2));
  lv_style_set_bg_opa(&s.track, LV_OPA_COVER);
  lv_style_set_radius(&s.track, 8);

  lv_style_set_bg_color(&s.knob, lv_color_hex(p.tx));
  lv_style_set_bg_opa(&s.knob, LV_OPA_COVER);
  lv_style_set_radius(&s.knob, LV_RADIUS_CIRCLE);
  lv_style_set_bg_color(&s.knob_on, lv_color_hex(p.onbg));

  lv_style_set_arc_color(&s.arc_bg, lv_color_hex(p.card2));
  lv_style_set_arc_opa(&s.arc_bg, LV_OPA_COVER);
  lv_style_set_arc_rounded(&s.arc_bg, false);

  lv_style_set_arc_color(&s.arc_fg, lv_color_hex(p.acc2));
  lv_style_set_arc_opa(&s.arc_fg, LV_OPA_COVER);
  lv_style_set_arc_rounded(&s.arc_fg, false);

  lv_style_set_line_color(&s.line, lv_color_hex(p.acc2));
  lv_style_set_line_width(&s.line, 3);
  lv_style_set_line_rounded(&s.line, false);
  lv_style_set_line_opa(&s.line, LV_OPA_COVER);

  lv_style_set_bg_color(&s.dot, lv_color_hex(p.tx2));
  lv_style_set_bg_opa(&s.dot, LV_OPA_COVER);
  lv_style_set_radius(&s.dot, LV_RADIUS_CIRCLE);

  lv_style_set_bg_color(&s.dot_on, lv_color_hex(p.acc));
  lv_style_set_bg_opa(&s.dot_on, LV_OPA_COVER);
  lv_style_set_radius(&s.dot_on, LV_RADIUS_CIRCLE);

  lv_style_set_image_recolor(&s.img_tx, lv_color_hex(p.tx));
  lv_style_set_image_recolor_opa(&s.img_tx, LV_OPA_COVER);

  lv_style_set_radius(&s.radius2, p.radius2);
}

static lv_obj_t *mk_obj(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return o;
}

static lv_obj_t *mk_card(lv_obj_t *parent, Styles &s, int x, int y,
                         int w, int h, bool clickable, bool has_on) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_add_style(o, &s.card, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  if (clickable) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(o, &s.card_pr, LV_STATE_PRESSED);
    if (has_on) {
      lv_obj_add_style(o, &s.card_on, LV_STATE_CHECKED);
      lv_obj_add_style(o, &s.card_on_pr, LV_STATE_CHECKED | LV_STATE_PRESSED);
    }
  } else {
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    if (has_on) lv_obj_add_style(o, &s.card_on, LV_STATE_CHECKED);
  }
  return o;
}

static lv_obj_t *mk_label(lv_obj_t *parent, int x, int y, int w, int h,
                          lv_style_t *font, lv_style_t *color,
                          lv_style_t *checked_color, const char *text,
                          lv_text_align_t align) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_remove_style_all(l);
  lv_obj_remove_flag(l, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  lv_obj_set_pos(l, x, y);
  lv_obj_set_size(l, w, h);
  lv_obj_add_style(l, font, 0);
  lv_obj_add_style(l, color, 0);
  if (checked_color) lv_obj_add_style(l, checked_color, LV_STATE_CHECKED);
  lv_obj_set_style_text_align(l, align, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  lv_label_set_text(l, text ? text : "");
  return l;
}

static lv_obj_t *mk_btn(lv_obj_t *parent, Styles &s, int x, int y,
                        int w, int h, lv_event_cb_t cb, void *user) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_style(o, &s.btn, 0);
  lv_obj_add_style(o, &s.btn_pr, LV_STATE_PRESSED);
  lv_obj_add_style(o, &s.btn_on, LV_STATE_CHECKED);
  lv_obj_add_style(o, &s.btn_on_pr, LV_STATE_CHECKED | LV_STATE_PRESSED);
  if (cb) lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, user);
  return o;
}

static lv_obj_t *mk_btn_text(lv_obj_t *btn, Styles &s, lv_style_t *font,
                             const char *text) {
  lv_obj_t *l = mk_label(btn, 0, 0, 1, 1, font, &s.tx, &s.t_acct,
                         text, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_size(l, LV_PCT(100), 22);
  lv_obj_center(l);
  return l;
}

static lv_obj_t *mk_btn_icon(lv_obj_t *btn, Styles &s, lv_style_t *icon_font,
                             const char *icon) {
  int size = icon_font == &fs.I64 ? 64 : icon_font == &fs.I40 ? 40 : 28;
  lv_obj_t *l = mk_label(btn, 0, 0, size, size, icon_font, &s.tx, &s.t_acct,
                         icon, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(l);
  return l;
}

static lv_obj_t *mk_slider(lv_obj_t *parent, Styles &s, int x, int y, int w,
                           int lo, int hi, lv_event_cb_t cb, void *user) {
  lv_obj_t *o = lv_slider_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, 16);
  lv_slider_set_range(o, lo, hi);
  lv_obj_add_style(o, &s.track, LV_PART_MAIN);
  lv_obj_add_style(o, &s.fill, LV_PART_INDICATOR);
  lv_obj_add_style(o, &s.knob, LV_PART_KNOB);
  lv_obj_set_style_radius(o, 8, LV_PART_INDICATOR);
  lv_obj_set_style_pad_all(o, 8, LV_PART_KNOB);
  lv_obj_set_ext_click_area(o, 36);
  lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE));
  if (cb) {
    lv_obj_add_event_cb(o, cb, LV_EVENT_VALUE_CHANGED, user);
    lv_obj_add_event_cb(o, cb, LV_EVENT_RELEASED, user);
  }
  return o;
}

static void set_text(lv_obj_t *label, const char *txt) {
  const char *v = txt ? txt : "";
  const char *old = lv_label_get_text(label);
  if (!old || strcmp(old, v) != 0) lv_label_set_text(label, v);
}

static void set_state_one(lv_obj_t *obj, bool on) {
  bool old = lv_obj_has_state(obj, LV_STATE_CHECKED);
  if (old == on) return;
  if (on) lv_obj_add_state(obj, LV_STATE_CHECKED);
  else lv_obj_remove_state(obj, LV_STATE_CHECKED);
}

static void set_checked(lv_obj_t *obj, bool on) {
  if (lv_obj_has_state(obj, LV_STATE_CHECKED) == on) return;
  set_state_one(obj, on);
  uint32_t n = lv_obj_get_child_count(obj);
  for (uint32_t i = 0; i < n; ++i) set_checked(lv_obj_get_child(obj, i), on);
}

static void set_hidden(lv_obj_t *obj, bool hidden) {
  bool old = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
  if (old == hidden) return;
  if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void set_w(lv_obj_t *obj, int w) {
  if (lv_obj_get_width(obj) != w) lv_obj_set_width(obj, w);
}

static void set_h(lv_obj_t *obj, int h) {
  if (lv_obj_get_height(obj) != h) lv_obj_set_height(obj, h);
}

static void set_x(lv_obj_t *obj, int x) {
  if (lv_obj_get_x(obj) != x) lv_obj_set_x(obj, x);
}

static void set_rect(lv_obj_t *obj, int x, int y, int w, int h) {
  if (lv_obj_get_x(obj) != x || lv_obj_get_y(obj) != y)
    lv_obj_set_pos(obj, x, y);
  if (lv_obj_get_width(obj) != w || lv_obj_get_height(obj) != h)
    lv_obj_set_size(obj, w, h);
}

static void set_slider(lv_obj_t *slider, int v) {
  if (lv_obj_has_state(slider, LV_STATE_PRESSED)) return;
  if (lv_slider_get_value(slider) != v)
    lv_slider_set_value(slider, v, LV_ANIM_OFF);
}

static void fmt_tenths(char *buf, size_t n, int tenths) {
  int whole = tenths / 10;
  int frac = tenths % 10;
  if (frac < 0) frac = -frac;
  snprintf(buf, n, "%d.%d\xC2\xB0", whole, frac);
}

static void fmt_mmss(char *buf, size_t n, int seconds) {
  if (seconds < 0) seconds = 0;
  snprintf(buf, n, "%d:%02d", seconds / 60, seconds % 60);
}

struct StatusChip {
  lv_obj_t *root, *icon, *text;
};

static void build_status_chip(StatusChip &c, lv_obj_t *parent, Styles &s,
                              int x, int y) {
  c.root = mk_card(parent, s, x, y, 296, 56, false, true);
  c.icon = mk_label(c.root, 16, 14, 28, 28, &fs.I28, &s.t_acc2, &s.t_ion,
                    "", LV_TEXT_ALIGN_CENTER);
  c.text = mk_label(c.root, 56, 17, 224, 22, &fs.F20, &s.tx, &s.t_on,
                    "", LV_TEXT_ALIGN_LEFT);
}

static void refresh_status_chip(StatusChip &c, const char *icon,
                                const char *text, bool checked) {
  set_text(c.icon, icon);
  set_text(c.text, text);
  set_checked(c.root, checked);
}

struct FloorCard {
  lv_obj_t *root, *icon, *name, *sub, *count, *caption, *button, *button_text;
};

static void build_floor(FloorCard &c, lv_obj_t *parent, Styles &s,
                        int x, int y, const char *icon, const char *name,
                        lv_event_cb_t cb, void *user) {
  c.root = mk_card(parent, s, x, y, 296, 292, false, false);
  c.icon = mk_label(c.root, 16, 16, 40, 40, &fs.I40, &s.t_acc2, nullptr,
                    icon, LV_TEXT_ALIGN_CENTER);
  c.name = mk_label(c.root, 68, 14, 212, 22, &fs.F20, &s.tx, nullptr,
                    name, LV_TEXT_ALIGN_LEFT);
  c.sub = mk_label(c.root, 68, 40, 212, 18, &fs.F16, &s.tx2, nullptr,
                   "3 rooms", LV_TEXT_ALIGN_LEFT);
  c.count = mk_label(c.root, 16, 84, 264, 52, &fs.F48, &s.tx, nullptr,
                     "0", LV_TEXT_ALIGN_LEFT);
  c.caption = mk_label(c.root, 16, 142, 264, 18, &fs.F16, &s.tx2, nullptr,
                       "", LV_TEXT_ALIGN_LEFT);
  c.button = mk_btn(c.root, s, 16, 188, 264, 88, cb, user);
  c.button_text = mk_btn_text(c.button, s, &fs.F20, "Turn all on");
}

static void refresh_floor(FloorCard &c, int on, int total) {
  char a[16], b[40];
  snprintf(a, sizeof(a), "%d", on);
  snprintf(b, sizeof(b), "of %d lights on", total);
  set_text(c.count, a);
  set_text(c.caption, b);
  set_text(c.button_text, on ? "Turn all off" : "Turn all on");
  set_checked(c.button, on != 0);
}

struct RoomCard {
  lv_obj_t *root, *icon, *name, *lights, *chevron;
  lv_obj_t *temp, *extra, *button, *button_icon, *button_text;
};

static void build_room(RoomCard &c, lv_obj_t *parent, Styles &s,
                       int x, int y, const char *icon, const char *name,
                       bool has_page, lv_event_cb_t body_cb, void *body_user,
                       lv_event_cb_t btn_cb, void *btn_user) {
  c.root = mk_card(parent, s, x, y, 296, 292, has_page, false);
  if (has_page && body_cb)
    lv_obj_add_event_cb(c.root, body_cb, LV_EVENT_CLICKED, body_user);
  c.icon = mk_label(c.root, 16, 16, 40, 40, &fs.I40, &s.t_acc2, nullptr,
                    icon, LV_TEXT_ALIGN_CENTER);
  c.name = mk_label(c.root, 68, 14, 176, 22, &fs.F20, &s.tx, nullptr,
                    name, LV_TEXT_ALIGN_LEFT);
  c.lights = mk_label(c.root, 68, 40, 176, 18, &fs.F16, &s.tx2, nullptr,
                      "Lights off", LV_TEXT_ALIGN_LEFT);
  c.chevron = mk_label(c.root, 252, 16, 28, 28, &fs.I28, &s.tx2, nullptr,
                       IC_CHEVRON_RIGHT, LV_TEXT_ALIGN_CENTER);
  set_hidden(c.chevron, !has_page);
  c.temp = mk_label(c.root, 16, 88, 264, 40, &fs.F36, &s.tx, nullptr,
                    "", LV_TEXT_ALIGN_LEFT);
  c.extra = mk_label(c.root, 16, 136, 264, 18, &fs.F16, &s.tx2, nullptr,
                     "", LV_TEXT_ALIGN_LEFT);
  c.button = mk_btn(c.root, s, 16, 188, 264, 88, btn_cb, btn_user);
  c.button_icon = mk_label(c.button, 64, 30, 28, 28, &fs.I28, &s.tx,
                           &s.t_acct, IC_LIGHTBULB_OUTLINE,
                           LV_TEXT_ALIGN_CENTER);
  c.button_text = mk_label(c.button, 100, 33, 120, 22, &fs.F20, &s.tx,
                           &s.t_acct, "Lights off", LV_TEXT_ALIGN_LEFT);
}

static void refresh_room(RoomCard &c, int on, int total, int temp_tenths,
                         const char *extra) {
  char a[48], b[20];
  if (on == 0) snprintf(a, sizeof(a), "Lights off");
  else if (total == 1) snprintf(a, sizeof(a), "%d of 1 light on", on);
  else snprintf(a, sizeof(a), "%d of %d lights on", on, total);
  fmt_tenths(b, sizeof(b), temp_tenths);
  set_text(c.lights, a);
  set_text(c.temp, b);
  set_text(c.extra, extra);
  set_text(c.button_icon, on ? IC_LIGHTBULB : IC_LIGHTBULB_OUTLINE);
  set_text(c.button_text, on ? "Lights on" : "Lights off");
  set_checked(c.button, on != 0);
}

struct LightTile {
  lv_obj_t *root, *icon, *name, *sub, *track, *fill;
};

static void build_light_tile(LightTile &c, lv_obj_t *parent, Styles &s,
                             int x, int y, const char *icon, const char *name,
                             lv_event_cb_t cb, void *user) {
  c.root = mk_card(parent, s, x, y, 296, 156, true, true);
  if (cb) lv_obj_add_event_cb(c.root, cb, LV_EVENT_CLICKED, user);
  c.icon = mk_label(c.root, 16, 16, 40, 40, &fs.I40, &s.tx2, &s.t_ion,
                    icon, LV_TEXT_ALIGN_CENTER);
  c.name = mk_label(c.root, 16, 78, 264, 22, &fs.F20, &s.tx, &s.t_on,
                    name, LV_TEXT_ALIGN_LEFT);
  c.sub = mk_label(c.root, 16, 104, 264, 18, &fs.F16, &s.tx2, &s.t_on,
                   "Off", LV_TEXT_ALIGN_LEFT);
  c.track = mk_obj(c.root, 16, 134, 264, 6);
  lv_obj_add_style(c.track, &s.inset, 0);
  lv_obj_add_style(c.track, &s.inset_on, LV_STATE_CHECKED);
  lv_obj_set_style_radius(c.track, 3, 0);
  c.fill = mk_obj(c.track, 0, 0, 0, 6);
  lv_obj_add_style(c.fill, &s.fill_dim, 0);
  lv_obj_add_style(c.fill, &s.fill_on, LV_STATE_CHECKED);
  lv_obj_set_style_radius(c.fill, 3, 0);
}

static void refresh_light_tile(LightTile &c, bool on, int bri) {
  char b[16];
  if (bri < 0) bri = 0;
  if (bri > 100) bri = 100;
  snprintf(b, sizeof(b), "%d%%", bri);
  set_text(c.sub, on ? b : "Off");
  set_w(c.fill, 264 * bri / 100);
  set_hidden(c.fill, !on);
  set_checked(c.root, on);
}

struct SwitchTile {
  lv_obj_t *root, *icon, *name, *sub, *pill, *knob;
};

static void build_switch_tile(SwitchTile &c, lv_obj_t *parent, Styles &s,
                              int x, int y, const char *icon, const char *name,
                              lv_event_cb_t cb, void *user) {
  c.root = mk_card(parent, s, x, y, 296, 156, true, true);
  if (cb) lv_obj_add_event_cb(c.root, cb, LV_EVENT_CLICKED, user);
  c.icon = mk_label(c.root, 16, 16, 40, 40, &fs.I40, &s.tx2, &s.t_ion,
                    icon, LV_TEXT_ALIGN_CENTER);
  c.name = mk_label(c.root, 16, 78, 264, 22, &fs.F20, &s.tx, &s.t_on,
                    name, LV_TEXT_ALIGN_LEFT);
  c.sub = mk_label(c.root, 16, 104, 264, 18, &fs.F16, &s.tx2, &s.t_on,
                   "", LV_TEXT_ALIGN_LEFT);
  c.pill = mk_obj(c.root, 224, 20, 56, 32);
  lv_obj_add_style(c.pill, &s.inset, 0);
  lv_obj_add_style(c.pill, &s.fill_on, LV_STATE_CHECKED);
  lv_obj_set_style_radius(c.pill, 16, 0);
  c.knob = mk_obj(c.pill, 4, 4, 24, 24);
  lv_obj_add_style(c.knob, &s.fill_dim, 0);
  lv_obj_add_style(c.knob, &s.knob_on, LV_STATE_CHECKED);
  lv_obj_set_style_radius(c.knob, 12, 0);
}

static void refresh_switch_tile(SwitchTile &c, bool on, const char *sub) {
  set_text(c.sub, sub);
  set_x(c.knob, on ? 28 : 4);
  set_checked(c.root, on);
}

struct SensorTile {
  lv_obj_t *root, *title, *icon, *value, *sub, *range;
};

static void build_sensor(SensorTile &c, lv_obj_t *parent, Styles &s,
                         int x, int y, int h, const char *title,
                         const char *icon) {
  c.root = mk_card(parent, s, x, y, 296, h, false, false);
  c.title = mk_label(c.root, 16, 16, 220, 18, &fs.F16, &s.tx2, nullptr,
                     title, LV_TEXT_ALIGN_LEFT);
  c.icon = mk_label(c.root, 252, 12, 28, 28, &fs.I28, &s.t_acc2, nullptr,
                    icon, LV_TEXT_ALIGN_CENTER);
  c.value = mk_label(c.root, 16, h >= 200 ? 92 : 56, 264, 40,
                     &fs.F36, &s.tx, nullptr, "", LV_TEXT_ALIGN_LEFT);
  c.sub = mk_label(c.root, 16, h >= 200 ? 144 : 108, 264, 18,
                   &fs.F16, &s.tx2, nullptr, "", LV_TEXT_ALIGN_LEFT);
  c.range = nullptr;
  if (h >= 200)
    c.range = mk_label(c.root, 16, h - 34, 264, 18, &fs.F16, &s.tx2,
                       nullptr, "", LV_TEXT_ALIGN_LEFT);
}

static void refresh_sensor(SensorTile &c, const char *value, const char *sub,
                           const char *range = nullptr) {
  set_text(c.value, value);
  set_text(c.sub, sub);
  if (c.range) set_text(c.range, range);
}

struct GroupCard {
  lv_obj_t *root, *icon, *title, *count, *caption, *value;
  lv_obj_t *slider, *button, *button_text;
};

static void build_group(GroupCard &c, lv_obj_t *parent, Styles &s,
                        int x, int y, lv_event_cb_t slider_cb,
                        lv_event_cb_t btn_cb, void *user) {
  c.root = mk_card(parent, s, x, y, 296, 328, false, false);
  c.icon = mk_label(c.root, 16, 16, 40, 40, &fs.I40, &s.tx2, &s.t_ion,
                    IC_LIGHTBULB_GROUP, LV_TEXT_ALIGN_CENTER);
  c.title = mk_label(c.root, 68, 14, 212, 22, &fs.F20, &s.tx, nullptr,
                     "All lights", LV_TEXT_ALIGN_LEFT);
  c.count = mk_label(c.root, 68, 40, 212, 18, &fs.F16, &s.tx2, nullptr,
                     "0 of 0 on", LV_TEXT_ALIGN_LEFT);
  c.caption = mk_label(c.root, 16, 96, 150, 18, &fs.F16, &s.tx2, nullptr,
                       "Brightness", LV_TEXT_ALIGN_LEFT);
  c.value = mk_label(c.root, 216, 96, 64, 18, &fs.F16, &s.tx, nullptr,
                     "1%", LV_TEXT_ALIGN_RIGHT);
  c.slider = mk_slider(c.root, s, 28, 144, 240, 1, 100, slider_cb, user);
  c.button = mk_btn(c.root, s, 16, 224, 264, 88, btn_cb, user);
  c.button_text = mk_btn_text(c.button, s, &fs.F20, "Turn all on");
}

static void refresh_group(GroupCard &c, int on, int total, int brightness) {
  char a[32], b[16];
  snprintf(a, sizeof(a), "%d of %d on", on, total);
  snprintf(b, sizeof(b), "%d%%", brightness);
  set_text(c.count, a);
  set_text(c.value, b);
  set_slider(c.slider, brightness);
  set_text(c.button_text, on ? "Turn all off" : "Turn all on");
  set_state_one(c.icon, on != 0);
  set_checked(c.button, on != 0);
}

struct SceneDef {
  const char *icon, *text;
};

struct SceneCard {
  lv_obj_t *root, *title, *chip[4], *icon[4], *text[4];
};

static void build_scenes(SceneCard &c, lv_obj_t *parent, Styles &s,
                         int x, int y, const SceneDef defs[4],
                         lv_event_cb_t cb, int user_base) {
  c.root = mk_card(parent, s, x, y, 296, 328, false, false);
  c.title = mk_label(c.root, 16, 16, 264, 18, &fs.F16, &s.tx2, nullptr,
                     "Scenes", LV_TEXT_ALIGN_LEFT);
  const int px[4] = {16, 154, 16, 154};
  const int py[4] = {48, 48, 186, 186};
  for (int i = 0; i < 4; ++i) {
    c.chip[i] = mk_btn(c.root, s, px[i], py[i], 126, 126, cb,
                       (void *)(intptr_t)(user_base + i));
    c.icon[i] = mk_label(c.chip[i], 49, 26, 28, 28, &fs.I28, &s.tx,
                         &s.t_acct, defs[i].icon, LV_TEXT_ALIGN_CENTER);
    c.text[i] = mk_label(c.chip[i], 4, 74, 118, 18, &fs.F16, &s.tx,
                         &s.t_acct, defs[i].text, LV_TEXT_ALIGN_CENTER);
  }
}

static void refresh_scenes(SceneCard &c, int lit_idx) {
  for (int i = 0; i < 4; ++i) set_checked(c.chip[i], i == lit_idx);
}

struct ThermostatCard {
  lv_obj_t *root, *title, *status, *arc, *target, *now;
  lv_obj_t *minus, *plus, *mode_btn[4], *mode_text[4];
  bool large;
};

static void build_thermostat(ThermostatCard &c, lv_obj_t *parent, Styles &s,
                             int x, int y, bool large,
                             lv_event_cb_t minus_cb, lv_event_cb_t plus_cb,
                             lv_event_cb_t mode_cb) {
  c.large = large;
  c.root = mk_card(parent, s, x, y, large ? 504 : 296,
                   large ? 672 : 414, false, false);
  c.title = mk_label(c.root, 16, 16, large ? 300 : 130, large ? 22 : 18,
                     large ? &fs.F20 : &fs.F16, &s.tx2, nullptr,
                     large ? "Living room thermostat" : "Thermostat",
                     LV_TEXT_ALIGN_LEFT);
  c.status = mk_label(c.root, large ? 16 : 150, large ? 400 : 16,
                      large ? 472 : 130, large ? 22 : 18,
                      large ? &fs.F20 : &fs.F16, &s.tx, nullptr, "",
                      large ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_RIGHT);

  c.arc = lv_arc_create(c.root);
  lv_obj_remove_style_all(c.arc);
  lv_obj_remove_flag(c.arc, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_pos(c.arc, large ? 132 : 68, large ? 100 : 88);
  lv_obj_set_size(c.arc, large ? 240 : 160, large ? 240 : 160);
  lv_obj_add_style(c.arc, &s.arc_bg, LV_PART_MAIN);
  lv_obj_add_style(c.arc, &s.arc_fg, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(c.arc, large ? 14 : 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(c.arc, large ? 14 : 10, LV_PART_INDICATOR);
  lv_arc_set_bg_angles(c.arc, 135, 45);
  lv_arc_set_range(c.arc, 160, 280);

  c.target = mk_label(c.root, large ? 92 : 48, large ? 194 : 142,
                      large ? 320 : 200, 52, large ? &fs.F48 : &fs.F36, &s.tx, nullptr,
                      "", LV_TEXT_ALIGN_CENTER);
  c.now = mk_label(c.root, large ? 92 : 48, large ? 252 : 198,
                   large ? 320 : 200, large ? 22 : 18,
                   large ? &fs.F20 : &fs.F16, &s.tx2, nullptr,
                   "", LV_TEXT_ALIGN_CENTER);

  c.minus = mk_btn(c.root, s, 16, large ? 436 : 310,
                   large ? 228 : 124, large ? 96 : 88,
                   minus_cb, nullptr);
  mk_btn_icon(c.minus, s, &fs.I28, IC_MINUS);
  c.plus = mk_btn(c.root, s, large ? 260 : 156, large ? 436 : 310,
                  large ? 228 : 124, large ? 96 : 88,
                  plus_cb, nullptr);
  mk_btn_icon(c.plus, s, &fs.I28, IC_PLUS);

  const char *names[4] = {"Heat", "Cool", "Auto", "Off"};
  for (int i = 0; i < 4; ++i) {
    c.mode_btn[i] = nullptr;
    c.mode_text[i] = nullptr;
    if (large) {
      c.mode_btn[i] = mk_btn(c.root, s, 16 + i * 121, 548, 109, 96,
                             mode_cb, (void *)(intptr_t)i);
      c.mode_text[i] = mk_btn_text(c.mode_btn[i], s, &fs.F20, names[i]);
    }
  }
}

static void refresh_thermostat(ThermostatCard &c, int target_tenths,
                               int current_tenths, int mode,
                               const char *status) {
  char target[20], current[20], line[80];
  const char *modes[4] = {"Heat", "Cool", "Auto", "Off"};
  fmt_tenths(target, sizeof(target), target_tenths);
  fmt_tenths(current, sizeof(current), current_tenths);
  set_text(c.target, target);
  lv_arc_set_value(c.arc, target_tenths);
  if (c.large) {
    set_text(c.now, modes[(mode >= 0 && mode < 4) ? mode : 3]);
    snprintf(line, sizeof(line), "Now %s \xE2\x80\xA2 %s",
             current, status ? status : "");
    set_text(c.status, line);
    for (int i = 0; i < 4; ++i) set_checked(c.mode_btn[i], i == mode);
  } else {
    snprintf(line, sizeof(line), "Now %s", current);
    set_text(c.now, line);
    set_text(c.status, status);
  }
}

struct SparkCard {
  lv_obj_t *root, *title, *value, *line, *left, *right;
  lv_point_precise_t pts[96];  // flat runs plus three points for each changing vertical run
  uint32_t version;
  int w, h;
};

static void build_spark(SparkCard &c, lv_obj_t *parent, Styles &s,
                        int x, int y, int w, int h, const char *title) {
  c.w = w;
  c.h = h;
  c.version = UINT32_MAX;
  c.root = mk_card(parent, s, x, y, w, h, false, false);
  c.title = mk_label(c.root, 16, 16, w - 32, 18, &fs.F16, &s.tx2, nullptr,
                     title, LV_TEXT_ALIGN_LEFT);
  c.value = mk_label(c.root, 16, 40, w - 32, 30, &fs.F28, &s.tx, nullptr,
                     "", LV_TEXT_ALIGN_LEFT);
  c.line = lv_line_create(c.root);
  lv_obj_remove_style_all(c.line);
  lv_obj_remove_flag(c.line, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_pos(c.line, 16, 88);
  lv_obj_set_size(c.line, w - 32, h - 136);
  lv_obj_add_style(c.line, &s.line, 0);
  c.left = mk_label(c.root, 16, h - 34, 100, 18, &fs.F16, &s.tx2, nullptr,
                    "24 h ago", LV_TEXT_ALIGN_LEFT);
  c.right = mk_label(c.root, w - 116, h - 34, 100, 18, &fs.F16, &s.tx2,
                     nullptr, "now", LV_TEXT_ALIGN_RIGHT);
}

static void refresh_spark(SparkCard &c, const int16_t hist[24],
                          uint32_t version, int current_tenths) {
  char b[20];
  fmt_tenths(b, sizeof(b), current_tenths);
  set_text(c.value, b);
  if (c.version == version) return;
  c.version = version;
  int16_t low = hist[0], high = hist[0];
  for (int i = 1; i < 24; ++i) {
    if (hist[i] < low) low = hist[i];
    if (hist[i] > high) high = hist[i];
  }
  int aw = c.w - 32;
  int ah = c.h - 136;
  // Each hour is a flat run, joined to the next by a vertical run. Horizontal
  // and vertical segments draw as plain fills; sloped ones need a per-row mask
  // and were the costliest part of a page with a chart on it. A vertical run
  // overshoots by a pixel at each end so it covers the outer corners of the
  // three-pixel line, which would otherwise show a notch.
  int n = 0;
  int y = high == low ? ah / 2 :
          2 + (ah - 4) * (high - hist[0]) / (high - low);
  c.pts[n].x = 2;
  c.pts[n].y = y;
  ++n;
  for (int i = 0; i < 24; ++i) {
    int x1 = (i + 1) * (aw - 4) / 24 + 2;
    c.pts[n].x = x1;
    c.pts[n].y = y;
    ++n;
    if (i < 23) {
      int next_y = high == low ? ah / 2 :
                   2 + (ah - 4) * (high - hist[i + 1]) / (high - low);
      if (next_y != y) {
        int ymin = next_y < y ? next_y : y;
        int ymax = next_y > y ? next_y : y;
        c.pts[n].x = x1;
        c.pts[n].y = ymin - 1;
        ++n;
        c.pts[n].x = x1;
        c.pts[n].y = ymax + 2;
        ++n;
        c.pts[n].x = x1;
        c.pts[n].y = next_y;
        ++n;
      }
      y = next_y;
    }
  }
  lv_line_set_points(c.line, c.pts, n);
}

struct BarsCard {
  lv_obj_t *root, *title, *value, *left, *right, *bar[24];
  uint8_t choice[24];
};

static void build_bars(BarsCard &c, lv_obj_t *parent, Styles &s,
                       int x, int y, int w, int h, const char *title,
                       const uint8_t hist[24]) {
  c.root = mk_card(parent, s, x, y, w, h, false, false);
  c.title = mk_label(c.root, 16, 16, w - 32, 18, &fs.F16, &s.tx2, nullptr,
                     title, LV_TEXT_ALIGN_LEFT);
  c.value = mk_label(c.root, 16, 40, w - 32, 30, &fs.F28, &s.tx, nullptr,
                     "", LV_TEXT_ALIGN_LEFT);
  c.left = mk_label(c.root, 16, h - 34, 100, 18, &fs.F16, &s.tx2, nullptr,
                    "00:00", LV_TEXT_ALIGN_LEFT);
  c.right = mk_label(c.root, w - 116, h - 34, 100, 18,
                     &fs.F16, &s.tx2, nullptr, "23:00", LV_TEXT_ALIGN_RIGHT);
  int aw = w - 32;
  int ah = h - 136;
  int bw = (aw - 23 * 4) / 24;
  int vmax = 1;
  for (int i = 0; i < 24; ++i) if (hist[i] > vmax) vmax = hist[i];
  for (int i = 0; i < 24; ++i) {
    int bh = ah * hist[i] / vmax;
    if (bh < 4) bh = 4;
    c.bar[i] = mk_obj(c.root, 16 + i * (bw + 4), 88 + ah - bh, bw, bh);
    lv_obj_set_style_radius(c.bar[i], 2, 0);
    c.choice[i] = 255;
  }
}

static void refresh_bars(BarsCard &c, Styles &s, int hour,
                         const char *value) {
  set_text(c.value, value);
  for (int i = 0; i < 24; ++i) {
    uint8_t next = i < hour ? 0 : (i == hour ? 1 : 2);
    if (c.choice[i] == next) continue;
    if (c.choice[i] != 255) {
      lv_style_t *old = c.choice[i] == 0 ? &s.fill2 :
                        (c.choice[i] == 1 ? &s.fill : &s.inset);
      lv_obj_remove_style(c.bar[i], old, 0);
    }
    lv_style_t *add = next == 0 ? &s.fill2 : (next == 1 ? &s.fill : &s.inset);
    lv_obj_add_style(c.bar[i], add, 0);
    c.choice[i] = next;
  }
}

struct CoverCard {
  lv_obj_t *root, *icon, *title, *state, *rail, *shade, *pelmet;
  lv_obj_t *pos_chip, *pos_label;
  lv_obj_t *up, *stop, *down;
};

static void build_cover(CoverCard &c, lv_obj_t *parent, Styles &s,
                        int x, int y, lv_event_cb_t up, lv_event_cb_t stop,
                        lv_event_cb_t down) {
  c.root = mk_card(parent, s, x, y, 296, 672, false, false);
  c.icon = mk_label(c.root, 16, 16, 40, 40, &fs.I40, &s.t_acc2, nullptr,
                    IC_BLINDS, LV_TEXT_ALIGN_CENTER);
  c.title = mk_label(c.root, 68, 14, 212, 22, &fs.F20, &s.tx, nullptr,
                     "Blinds", LV_TEXT_ALIGN_LEFT);
  c.state = mk_label(c.root, 68, 40, 212, 18, &fs.F16, &s.tx2, nullptr,
                     "", LV_TEXT_ALIGN_LEFT);
  c.rail = mk_obj(c.root, 16, 96, 132, 560);
  lv_obj_add_style(c.rail, &s.inset, 0);
  c.shade = mk_obj(c.rail, 0, 0, 132, 560);
  lv_obj_add_style(c.shade, &s.fill2, 0);
  lv_obj_add_style(c.shade, &s.radius2, 0);
  c.pelmet = mk_obj(c.rail, 8, 8, 116, 12);
  lv_obj_add_style(c.pelmet, &s.fill2, 0);
  lv_obj_set_style_radius(c.pelmet, 6, 0);
  c.pos_chip = mk_obj(c.rail, 22, 260, 88, 40);
  lv_obj_add_style(c.pos_chip, &s.card, 0);
  lv_obj_add_style(c.pos_chip, &s.radius2, 0);
  lv_obj_set_style_border_width(c.pos_chip, 0, 0);
  lv_obj_set_style_pad_all(c.pos_chip, 0, 0);
  c.pos_label = mk_label(c.pos_chip, 0, 9, 88, 22, &fs.F20, &s.tx, nullptr,
                         "", LV_TEXT_ALIGN_CENTER);
  c.up = mk_btn(c.root, s, 164, 96, 116, 176, up, nullptr);
  mk_btn_icon(c.up, s, &fs.I28, IC_ARROW_UP);
  c.stop = mk_btn(c.root, s, 164, 288, 116, 176, stop, nullptr);
  mk_btn_icon(c.stop, s, &fs.I28, IC_STOP);
  c.down = mk_btn(c.root, s, 164, 480, 116, 176, down, nullptr);
  mk_btn_icon(c.down, s, &fs.I28, IC_ARROW_DOWN);
}

static void refresh_cover(CoverCard &c, int pos, const char *state) {
  if (pos < 0) pos = 0;
  if (pos > 100) pos = 100;
  set_text(c.icon, pos < 50 ? IC_BLINDS : IC_BLINDS_OPEN);
  set_text(c.state, state);
  char value[16];
  snprintf(value, sizeof(value), "%d%%", pos);
  set_text(c.pos_label, value);
  int h = 560 * (100 - pos) / 100;
  set_h(c.shade, h);
  set_hidden(c.shade, h == 0);
}

static void set_round_all(lv_obj_t *o, int radius) {
  lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
  lv_obj_set_style_radius(o, radius, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_radius(o, radius, LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_set_style_radius(o, radius,
                          LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
}

struct MediaMini {
  lv_obj_t *root, *art, *art_icon, *title, *artist, *progress;
  lv_obj_t *prev, *play, *next, *play_icon;
};

static void build_media_mini(MediaMini &c, lv_obj_t *parent, Styles &s,
                             int x, int y, lv_event_cb_t prev,
                             lv_event_cb_t play, lv_event_cb_t next) {
  c.root = mk_card(parent, s, x, y, 296, 242, false, false);
  c.art = mk_obj(c.root, 16, 16, 72, 72);
  lv_obj_add_style(c.art, &s.inset, 0);
  c.art_icon = mk_label(c.art, 16, 16, 40, 40, &fs.I40, &s.t_acc2, nullptr,
                        IC_MUSIC_NOTE, LV_TEXT_ALIGN_CENTER);
  c.title = mk_label(c.root, 104, 22, 176, 22, &fs.F20, &s.tx, nullptr,
                     "", LV_TEXT_ALIGN_LEFT);
  c.artist = mk_label(c.root, 104, 50, 176, 18, &fs.F16, &s.tx2, nullptr,
                      "", LV_TEXT_ALIGN_LEFT);

  c.progress = lv_bar_create(c.root);
  lv_obj_remove_style_all(c.progress);
  lv_obj_remove_flag(c.progress, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_pos(c.progress, 16, 108);
  lv_obj_set_size(c.progress, 264, 6);
  lv_obj_add_style(c.progress, &s.track, LV_PART_MAIN);
  lv_obj_add_style(c.progress, &s.fill, LV_PART_INDICATOR);
  lv_obj_set_style_radius(c.progress, 3, LV_PART_MAIN);
  lv_obj_set_style_radius(c.progress, 3, LV_PART_INDICATOR);

  c.prev = mk_btn(c.root, s, 28, 150, 64, 64, prev, nullptr);
  c.play = mk_btn(c.root, s, 116, 150, 64, 64, play, nullptr);
  c.next = mk_btn(c.root, s, 204, 150, 64, 64, next, nullptr);
  set_round_all(c.prev, 32);
  set_round_all(c.play, 32);
  set_round_all(c.next, 32);
  lv_obj_set_ext_click_area(c.prev, 12);
  lv_obj_set_ext_click_area(c.play, 12);
  lv_obj_set_ext_click_area(c.next, 12);
  mk_btn_icon(c.prev, s, &fs.I28, IC_SKIP_PREVIOUS);
  c.play_icon = mk_btn_icon(c.play, s, &fs.I28, IC_PLAY);
  mk_btn_icon(c.next, s, &fs.I28, IC_SKIP_NEXT);
  set_checked(c.play, true);
}

static void refresh_media_mini(MediaMini &c, const char *title,
                               const char *artist, int seconds, int elapsed,
                               bool playing) {
  if (seconds < 1) seconds = 1;
  if (elapsed < 0) elapsed = 0;
  if (elapsed > seconds) elapsed = seconds;
  set_text(c.title, title);
  set_text(c.artist, artist);
  lv_bar_set_range(c.progress, 0, seconds);
  lv_bar_set_value(c.progress, elapsed, LV_ANIM_OFF);
  set_text(c.play_icon, playing ? IC_PAUSE : IC_PLAY);
}

struct NowPlaying {
  lv_obj_t *root, *art, *art_icon, *caption, *title, *artist, *album;
  lv_obj_t *speakers, *progress, *elapsed, *duration;
  lv_obj_t *prev, *play, *next, *play_icon;
};

static void build_now_playing(NowPlaying &c, lv_obj_t *parent, Styles &s,
                              int x, int y, lv_event_cb_t prev,
                              lv_event_cb_t play, lv_event_cb_t next) {
  c.root = mk_card(parent, s, x, y, 816, 672, false, false);
  c.art = mk_obj(c.root, 32, 32, 320, 320);
  lv_obj_add_style(c.art, &s.inset, 0);
  c.art_icon = mk_label(c.art, 128, 128, 64, 64, &fs.I64, &s.t_acc2, nullptr,
                        IC_MUSIC_NOTE, LV_TEXT_ALIGN_CENTER);
  c.caption = mk_label(c.root, 384, 44, 400, 18, &fs.F16, &s.tx2, nullptr,
                       "PAUSED", LV_TEXT_ALIGN_LEFT);
  c.title = mk_label(c.root, 384, 76, 400, 40, &fs.F36, &s.tx, nullptr,
                     "", LV_TEXT_ALIGN_LEFT);
  c.artist = mk_label(c.root, 384, 128, 400, 22, &fs.F20, &s.tx2, nullptr,
                      "", LV_TEXT_ALIGN_LEFT);
  c.album = mk_label(c.root, 384, 160, 400, 18, &fs.F16, &s.tx2, nullptr,
                     "", LV_TEXT_ALIGN_LEFT);
  c.speakers = mk_label(c.root, 384, 316, 400, 18, &fs.F16, &s.tx2, nullptr,
                        "", LV_TEXT_ALIGN_LEFT);

  c.progress = lv_bar_create(c.root);
  lv_obj_remove_style_all(c.progress);
  lv_obj_remove_flag(c.progress, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_pos(c.progress, 32, 400);
  lv_obj_set_size(c.progress, 752, 8);
  lv_obj_add_style(c.progress, &s.track, LV_PART_MAIN);
  lv_obj_add_style(c.progress, &s.fill, LV_PART_INDICATOR);
  lv_obj_set_style_radius(c.progress, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(c.progress, 4, LV_PART_INDICATOR);

  c.elapsed = mk_label(c.root, 32, 420, 100, 18, &fs.F16, &s.tx2, nullptr,
                       "0:00", LV_TEXT_ALIGN_LEFT);
  c.duration = mk_label(c.root, 684, 420, 100, 18, &fs.F16, &s.tx2, nullptr,
                        "0:00", LV_TEXT_ALIGN_RIGHT);

  c.prev = mk_btn(c.root, s, 224, 488, 96, 96, prev, nullptr);
  c.play = mk_btn(c.root, s, 344, 472, 128, 128, play, nullptr);
  c.next = mk_btn(c.root, s, 496, 488, 96, 96, next, nullptr);
  set_round_all(c.prev, 48);
  set_round_all(c.play, 64);
  set_round_all(c.next, 48);
  mk_btn_icon(c.prev, s, &fs.I28, IC_SKIP_PREVIOUS);
  c.play_icon = mk_btn_icon(c.play, s, &fs.I40, IC_PLAY);
  mk_btn_icon(c.next, s, &fs.I28, IC_SKIP_NEXT);
  set_checked(c.play, true);
}

static void refresh_now_playing(NowPlaying &c, const char *title,
                                const char *artist, const char *album,
                                int seconds, int elapsed, bool playing,
                                const char *speakers) {
  char a[16], b[16];
  if (seconds < 1) seconds = 1;
  if (elapsed < 0) elapsed = 0;
  if (elapsed > seconds) elapsed = seconds;
  set_text(c.caption, playing ? "NOW PLAYING" : "PAUSED");
  set_text(c.title, title);
  set_text(c.artist, artist);
  set_text(c.album, album);
  set_text(c.speakers, speakers);
  lv_bar_set_range(c.progress, 0, seconds);
  lv_bar_set_value(c.progress, elapsed, LV_ANIM_OFF);
  fmt_mmss(a, sizeof(a), elapsed);
  fmt_mmss(b, sizeof(b), seconds);
  set_text(c.elapsed, a);
  set_text(c.duration, b);
  set_text(c.play_icon, playing ? IC_PAUSE : IC_PLAY);
}

struct SpeakerCard {
  lv_obj_t *root, *title, *living, *kitchen;
  lv_obj_t *living_text, *kitchen_text;
};

static void build_speakers(SpeakerCard &c, lv_obj_t *parent, Styles &s,
                           int x, int y, lv_event_cb_t living_cb,
                           lv_event_cb_t kitchen_cb) {
  c.root = mk_card(parent, s, x, y, 400, 156, false, false);
  c.title = mk_label(c.root, 16, 14, 200, 18, &fs.F16, &s.tx2, nullptr,
                     "Speakers", LV_TEXT_ALIGN_LEFT);
  c.living = mk_btn(c.root, s, 16, 52, 176, 88, living_cb, nullptr);
  c.kitchen = mk_btn(c.root, s, 208, 52, 176, 88, kitchen_cb, nullptr);
  c.living_text = mk_btn_text(c.living, s, &fs.F20, "Living room");
  c.kitchen_text = mk_btn_text(c.kitchen, s, &fs.F20, "Kitchen");
}

static void refresh_speakers(SpeakerCard &c, bool living, bool kitchen) {
  set_checked(c.living, living);
  set_checked(c.kitchen, kitchen);
}

struct VolumeCard {
  lv_obj_t *root, *title, *value, *icon, *slider;
};

static void build_volume(VolumeCard &c, lv_obj_t *parent, Styles &s,
                         int x, int y, lv_event_cb_t cb) {
  c.root = mk_card(parent, s, x, y, 400, 156, false, false);
  c.title = mk_label(c.root, 16, 16, 200, 18, &fs.F16, &s.tx2, nullptr,
                     "Volume", LV_TEXT_ALIGN_LEFT);
  c.value = mk_label(c.root, 304, 10, 80, 30, &fs.F28, &s.tx, nullptr,
                     "0%", LV_TEXT_ALIGN_RIGHT);
  c.icon = mk_label(c.root, 16, 86, 28, 28, &fs.I28, &s.t_acc2, nullptr,
                    IC_VOLUME_HIGH, LV_TEXT_ALIGN_CENTER);
  c.slider = mk_slider(c.root, s, 72, 92, 300, 0, 100, cb, nullptr);
}

static void refresh_volume(VolumeCard &c, int volume) {
  char b[16];
  snprintf(b, sizeof(b), "%d%%", volume);
  set_text(c.value, b);
  set_slider(c.slider, volume);
}

struct ListCard {
  lv_obj_t *root, *title, *row[3], *index[3], *item[3], *duration[3];
};

static void build_up_next(ListCard &c, lv_obj_t *parent, Styles &s,
                          int x, int y, lv_event_cb_t cb) {
  c.root = mk_card(parent, s, x, y, 400, 328, false, false);
  c.title = mk_label(c.root, 16, 16, 200, 18, &fs.F16, &s.tx2, nullptr,
                     "Up next", LV_TEXT_ALIGN_LEFT);
  for (int i = 0; i < 3; ++i) {
    c.row[i] = lv_obj_create(c.root);
    lv_obj_remove_style_all(c.row[i]);
    lv_obj_set_pos(c.row[i], 16, 48 + i * 88);
    lv_obj_set_size(c.row[i], 368, 88);
    lv_obj_remove_flag(c.row[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c.row[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(c.row[i], &s.btn_pr, LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(c.row[i], cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)i);
    char n[4];
    snprintf(n, sizeof(n), "%d", i + 1);
    c.index[i] = mk_label(c.row[i], 12, 35, 24, 18, &fs.F16, &s.tx2,
                          nullptr, n, LV_TEXT_ALIGN_LEFT);
    c.item[i] = mk_label(c.row[i], 48, 33, 230, 22, &fs.F20, &s.tx,
                         nullptr, "", LV_TEXT_ALIGN_LEFT);
    c.duration[i] = mk_label(c.row[i], 288, 35, 68, 18, &fs.F16, &s.tx2,
                             nullptr, "", LV_TEXT_ALIGN_RIGHT);
  }
}

static void refresh_up_next(ListCard &c, int k, const char *title,
                            const char *duration) {
  if (k < 0 || k >= 3) return;
  set_text(c.item[k], title);
  set_text(c.duration[k], duration);
}

struct ConfirmView {
  const char *icon, *name, *status, *prompt, *a, *b;
  bool highlighted, a_primary, b_primary, small_text;
};

struct ConfirmCard {
  lv_obj_t *root, *icon, *name, *status, *prompt;
  lv_obj_t *btn_a, *btn_b, *text_a, *text_b;
  bool small_text;
};

static void build_confirm(ConfirmCard &c, lv_obj_t *parent, Styles &s,
                          int x, int y, lv_event_cb_t a_cb,
                          lv_event_cb_t b_cb, void *user) {
  c.root = mk_card(parent, s, x, y, 296, 328, false, true);
  c.icon = mk_label(c.root, 116, 24, 64, 64, &fs.I64, &s.tx, &s.t_ion,
                    "", LV_TEXT_ALIGN_CENTER);
  c.name = mk_label(c.root, 16, 100, 264, 22, &fs.F20, &s.tx, &s.t_on,
                    "", LV_TEXT_ALIGN_CENTER);
  c.status = mk_label(c.root, 16, 128, 264, 18, &fs.F16, &s.tx2, &s.t_on,
                      "", LV_TEXT_ALIGN_CENTER);
  c.prompt = mk_label(c.root, 16, 176, 264, 18, &fs.F16, &s.tx, &s.t_on,
                      "", LV_TEXT_ALIGN_CENTER);
  c.btn_a = mk_btn(c.root, s, 16, 224, 264, 88, a_cb, user);
  c.btn_b = mk_btn(c.root, s, 154, 224, 126, 88, b_cb, user);
  c.text_a = mk_btn_text(c.btn_a, s, &fs.F20, "");
  c.text_b = mk_btn_text(c.btn_b, s, &fs.F20, "");
  c.small_text = false;
}

static void confirm_font(lv_obj_t *label, bool small) {
  lv_style_t *old_font = small ? &fs.F20 : &fs.F16;
  lv_style_t *new_font = small ? &fs.F16 : &fs.F20;
  lv_obj_remove_style(label, old_font, 0);
  lv_obj_add_style(label, new_font, 0);
}

static void refresh_confirm(ConfirmCard &c, Styles &s,
                            const ConfirmView &v) {
  (void)s;
  set_text(c.icon, v.icon);
  set_text(c.name, v.name);
  set_text(c.status, v.status);
  set_text(c.prompt, v.prompt);
  set_text(c.text_a, v.a);
  set_text(c.text_b, v.b);
  set_hidden(c.prompt, v.prompt == nullptr);
  set_hidden(c.btn_b, v.b == nullptr);
  set_rect(c.btn_a, 16, 224, v.b ? 126 : 264, 88);

  if (c.small_text != v.small_text) {
    confirm_font(c.text_a, v.small_text);
    confirm_font(c.text_b, v.small_text);
    c.small_text = v.small_text;
  }

  set_state_one(c.root, v.highlighted);
  set_state_one(c.icon, v.highlighted);
  set_state_one(c.name, v.highlighted);
  set_state_one(c.status, v.highlighted);
  set_state_one(c.prompt, v.highlighted);
  set_checked(c.btn_a, v.a_primary);
  set_checked(c.btn_b, v.b_primary);
}

struct CameraCard {
  lv_obj_t *root, *panel, *icon, *live_chip, *live_text;
  lv_obj_t *caption_chip, *caption;
};

static void build_camera(CameraCard &c, lv_obj_t *parent, Styles &s,
                         int x, int y) {
  c.root = mk_card(parent, s, x, y, 608, 328, false, false);
  c.panel = mk_obj(c.root, 12, 12, 584, 304);
  lv_obj_add_style(c.panel, &s.inset, 0);
  c.icon = mk_label(c.panel, 272, 132, 64, 64, &fs.I64, &s.tx2, nullptr,
                    IC_CCTV, LV_TEXT_ALIGN_CENTER);
  c.live_chip = mk_obj(c.panel, 492, 16, 76, 32);
  lv_obj_add_style(c.live_chip, &s.card, 0);
  lv_obj_set_style_radius(c.live_chip, 16, 0);
  lv_obj_set_style_border_width(c.live_chip, 0, 0);
  lv_obj_set_style_pad_all(c.live_chip, 0, 0);
  c.live_text = mk_label(c.live_chip, 0, 7, 76, 18, &fs.F16, &s.tx, nullptr,
                         "LIVE", LV_TEXT_ALIGN_CENTER);
  c.caption_chip = mk_obj(c.panel, 16, 252, 184, 36);
  lv_obj_add_style(c.caption_chip, &s.card, 0);
  lv_obj_set_style_radius(c.caption_chip, 8, 0);
  lv_obj_set_style_border_width(c.caption_chip, 0, 0);
  lv_obj_set_style_pad_all(c.caption_chip, 0, 0);
  c.caption = mk_label(c.caption_chip, 16, 9, 152, 18, &fs.F16, &s.tx,
                       nullptr, "", LV_TEXT_ALIGN_LEFT);
}

static void refresh_camera(CameraCard &c, const char *caption) {
  set_text(c.caption, caption);
}

struct WeatherCard {
  lv_obj_t *root, *icon, *temp, *condition, *detail;
  lv_obj_t *day[5], *forecast_icon[5], *high[5], *low[5];
};

static void build_weather(WeatherCard &c, lv_obj_t *parent, Styles &s,
                          int x, int y) {
  c.root = mk_card(parent, s, x, y, 296, 328, false, false);
  c.icon = mk_label(c.root, 16, 16, 64, 64, &fs.I64, &s.t_acc, nullptr,
                    IC_WEATHER_PARTLY_CLOUDY, LV_TEXT_ALIGN_CENTER);
  c.temp = mk_label(c.root, 96, 22, 184, 52, &fs.F48, &s.tx, nullptr,
                    "12\xC2\xB0", LV_TEXT_ALIGN_LEFT);
  c.condition = mk_label(c.root, 16, 92, 264, 22, &fs.F20, &s.tx, nullptr,
                         "Partly cloudy", LV_TEXT_ALIGN_LEFT);
  c.detail = mk_label(c.root, 16, 120, 264, 18, &fs.F16, &s.tx2, nullptr,
                      "Feels like 10\xC2\xB0 \xE2\x80\xA2 Wind 14 km/h",
                      LV_TEXT_ALIGN_LEFT);

  const char *icons[5] = {
    IC_WEATHER_PARTLY_CLOUDY, IC_WEATHER_SUNNY, IC_WEATHER_CLOUDY,
    IC_WEATHER_RAINY, IC_WEATHER_PARTLY_CLOUDY
  };
  const int highs[5] = {14, 16, 13, 11, 12};
  const int lows[5] = {8, 9, 7, 6, 7};
  for (int i = 0; i < 5; ++i) {
    int px = 16 + 53 * i;
    char hi[12], lo[12];
    snprintf(hi, sizeof(hi), "%d\xC2\xB0", highs[i]);
    snprintf(lo, sizeof(lo), "%d\xC2\xB0", lows[i]);
    c.day[i] = mk_label(c.root, px, 172, 52, 18, &fs.F16, &s.tx2, nullptr,
                        "", LV_TEXT_ALIGN_CENTER);
    c.forecast_icon[i] = mk_label(c.root, px + 12, 198, 28, 28,
                                  &fs.I28, &s.t_acc2, nullptr, icons[i],
                                  LV_TEXT_ALIGN_CENTER);
    c.high[i] = mk_label(c.root, px, 236, 52, 18, &fs.F16, &s.tx, nullptr,
                         hi, LV_TEXT_ALIGN_CENTER);
    c.low[i] = mk_label(c.root, px, 258, 52, 18, &fs.F16, &s.tx2, nullptr,
                        lo, LV_TEXT_ALIGN_CENTER);
  }
}

static void refresh_weather_days(WeatherCard &c,
                                 const char *const days[5]) {
  for (int i = 0; i < 5; ++i) set_text(c.day[i], days[i]);
}

struct RowList {
  lv_obj_t *root, *title, *icon[3], *name[3], *value[3];
};

static void build_rowlist(RowList &c, lv_obj_t *parent, Styles &s,
                          int x, int y) {
  c.root = mk_card(parent, s, x, y, 296, 328, false, false);
  c.title = mk_label(c.root, 16, 16, 264, 18, &fs.F16, &s.tx2, nullptr,
                     "Air and energy", LV_TEXT_ALIGN_LEFT);
  const char *icons[3] = {IC_WATER_PERCENT, IC_MOLECULE_CO2, IC_SOLAR_POWER};
  const char *names[3] = {"Humidity", "CO2", "Solar"};
  for (int i = 0; i < 3; ++i) {
    int py = 48 + i * 88;
    c.icon[i] = mk_label(c.root, 16, py + 30, 28, 28, &fs.I28, &s.t_acc2,
                         nullptr, icons[i], LV_TEXT_ALIGN_CENTER);
    c.name[i] = mk_label(c.root, 56, py + 33, 120, 22, &fs.F20, &s.tx,
                         nullptr, names[i], LV_TEXT_ALIGN_LEFT);
    c.value[i] = mk_label(c.root, 170, py + 33, 110, 22, &fs.F20, &s.tx,
                          nullptr, "", LV_TEXT_ALIGN_RIGHT);
  }
}

static void refresh_rowlist(RowList &c, const char *v0, const char *v1,
                            const char *v2) {
  set_text(c.value[0], v0);
  set_text(c.value[1], v1);
  set_text(c.value[2], v2);
}

struct DishwasherCard {
  lv_obj_t *root, *icon, *title, *state, *left, *sub;
  lv_obj_t *progress, *button, *button_text;
};

static void build_dish(DishwasherCard &c, lv_obj_t *parent, Styles &s,
                       int x, int y, lv_event_cb_t cb) {
  c.root = mk_card(parent, s, x, y, 296, 328, false, false);
  c.icon = mk_label(c.root, 16, 16, 40, 40, &fs.I40, &s.t_acc2, nullptr,
                    IC_DISHWASHER, LV_TEXT_ALIGN_CENTER);
  c.title = mk_label(c.root, 68, 14, 212, 22, &fs.F20, &s.tx, nullptr,
                     "Dishwasher", LV_TEXT_ALIGN_LEFT);
  c.state = mk_label(c.root, 68, 40, 212, 18, &fs.F16, &s.tx2, nullptr,
                     "Running", LV_TEXT_ALIGN_LEFT);
  c.left = mk_label(c.root, 16, 92, 264, 40, &fs.F36, &s.tx, nullptr,
                    "", LV_TEXT_ALIGN_LEFT);
  c.sub = mk_label(c.root, 16, 138, 264, 18, &fs.F16, &s.tx2, nullptr,
                   "Eco 50\xC2\xB0 programme", LV_TEXT_ALIGN_LEFT);

  c.progress = lv_bar_create(c.root);
  lv_obj_remove_style_all(c.progress);
  lv_obj_remove_flag(c.progress, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_pos(c.progress, 16, 176);
  lv_obj_set_size(c.progress, 264, 8);
  lv_bar_set_range(c.progress, 0, 120);
  lv_obj_add_style(c.progress, &s.track, LV_PART_MAIN);
  lv_obj_add_style(c.progress, &s.fill, LV_PART_INDICATOR);
  lv_obj_set_style_radius(c.progress, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(c.progress, 4, LV_PART_INDICATOR);

  c.button = mk_btn(c.root, s, 16, 224, 264, 88, cb, nullptr);
  c.button_text = mk_btn_text(c.button, s, &fs.F20, "Pause");
}

static void refresh_dish(DishwasherCard &c, int state, int left) {
  char b[24];
  if (state == 2) {
    set_text(c.state, "Finished");
    set_text(c.left, "Done");
    set_text(c.button_text, "Start");
    lv_bar_set_value(c.progress, 120, LV_ANIM_OFF);
  } else {
    if (left < 0) left = 0;
    if (left > 120) left = 120;
    snprintf(b, sizeof(b), "%d min", left);
    set_text(c.left, b);
    set_text(c.state, state == 1 ? "Paused" : "Running");
    set_text(c.button_text, state == 1 ? "Resume" : "Pause");
    lv_bar_set_value(c.progress, 120 - left, LV_ANIM_OFF);
  }
}

struct Swatch {
  lv_obj_t *root, *name, *hint, *check;
  lv_obj_t *mini_tile, *mini_tile_icon, *mini_tile_name, *mini_tile_sub;
  lv_obj_t *mini_primary, *mini_primary_text;
};

static void build_swatch(Swatch &c, lv_obj_t *parent, Styles &own,
                         int x, int y, const char *name, bool glass,
                         lv_event_cb_t cb, void *user) {
  c.root = lv_obj_create(parent);
  lv_obj_remove_style_all(c.root);
  lv_obj_set_pos(c.root, x, y);
  lv_obj_set_size(c.root, 400, 276);
  lv_obj_remove_flag(c.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(c.root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_style(c.root, &own.card, 0);
  lv_obj_add_style(c.root, &own.page, 0);
  lv_obj_set_style_radius(c.root, 20, 0);
  lv_obj_set_style_border_width(c.root, 2, 0);
  lv_obj_set_style_pad_all(c.root, -2, 0);
  if (cb) lv_obj_add_event_cb(c.root, cb, LV_EVENT_CLICKED, user);

  c.name = mk_label(c.root, 20, 14, 300, 22, &fs.F20, &own.tx, nullptr,
                    name, LV_TEXT_ALIGN_LEFT);
  c.hint = mk_label(c.root, 20, 40, 330, 18, &fs.F16, &own.tx2, nullptr,
                    glass ? "Pre-rendered page \xE2\x80\xA2 tap to open it"
                          : "Tap to use on every page",
                    LV_TEXT_ALIGN_LEFT);
  c.check = mk_label(c.root, 352, 14, 28, 28, &fs.I28, &own.t_acc, nullptr,
                     IC_CHECK_CIRCLE, LV_TEXT_ALIGN_CENTER);

  c.mini_tile = mk_card(c.root, own, 20, 110, 172, 116, false, true);
  c.mini_tile_icon = mk_label(c.mini_tile, 12, 12, 28, 28, &fs.I28,
                              &own.tx2, &own.t_ion, IC_LIGHTBULB,
                              LV_TEXT_ALIGN_CENTER);
  c.mini_tile_name = mk_label(c.mini_tile, 12, 60, 148, 22, &fs.F20,
                              &own.tx, &own.t_on, "Ceiling",
                              LV_TEXT_ALIGN_LEFT);
  c.mini_tile_sub = mk_label(c.mini_tile, 12, 86, 148, 18, &fs.F16,
                             &own.tx2, &own.t_on, "80%",
                             LV_TEXT_ALIGN_LEFT);
  set_checked(c.mini_tile, true);

  c.mini_primary = mk_obj(c.root, 208, 144, 172, 48);
  lv_obj_add_style(c.mini_primary, &own.btn, 0);
  lv_obj_add_style(c.mini_primary, &own.btn_on, LV_STATE_CHECKED);
  c.mini_primary_text = mk_label(c.mini_primary, 0, 0, 172, 18, &fs.F16,
                                 &own.tx, &own.t_acct, "Turn on",
                                 LV_TEXT_ALIGN_CENTER);
  lv_obj_center(c.mini_primary_text);
  set_checked(c.mini_primary, true);
  set_hidden(c.check, true);
}

static void refresh_swatch(Swatch &c, bool active, bool glass) {
  set_hidden(c.check, !active);
  if (glass) {
    set_text(c.hint, "Pre-rendered page \xE2\x80\xA2 tap to open it");
  } else {
    set_text(c.hint, active ? "In use on every page"
                            : "Tap to use on every page");
  }
}

struct GlassCard {
  lv_obj_t *root, *icon, *chevron, *caption, *value, *sub;
};

static void build_glass(GlassCard &c, lv_obj_t *parent, Styles &s,
                        int x, int y, const char *icon, const char *caption,
                        lv_event_cb_t cb, void *user) {
  c.root = mk_card(parent, s, x, y, 296, 240, true, false);
  lv_obj_set_style_radius(c.root, 24, 0);
  if (cb) lv_obj_add_event_cb(c.root, cb, LV_EVENT_CLICKED, user);
  c.icon = mk_label(c.root, 20, 20, 40, 40, &fs.I40, &s.t_acc, nullptr,
                    icon, LV_TEXT_ALIGN_CENTER);
  c.chevron = mk_label(c.root, 248, 26, 28, 28, &fs.I28, &s.tx2, nullptr,
                       IC_CHEVRON_RIGHT, LV_TEXT_ALIGN_CENTER);
  c.caption = mk_label(c.root, 20, 84, 256, 18, &fs.F16, &s.tx2, nullptr,
                       caption, LV_TEXT_ALIGN_LEFT);
  c.value = mk_label(c.root, 20, 110, 256, 40, &fs.F36, &s.tx, nullptr,
                     "", LV_TEXT_ALIGN_LEFT);
  c.sub = mk_label(c.root, 20, 162, 256, 18, &fs.F16, &s.tx2, nullptr,
                   "", LV_TEXT_ALIGN_LEFT);
}

static void refresh_glass(GlassCard &c, const char *icon,
                          const char *value, const char *sub) {
  set_text(c.icon, icon);
  set_text(c.value, value);
  set_text(c.sub, sub);
}

struct Ctx {
  Assets assets;
  lv_obj_t *root;
  lv_obj_t *page[PAGE_COUNT];
  Styles page_styles[PAGE_COUNT];
  Styles swatch_styles[THEME_COUNT];
  bool style_stale[PAGE_COUNT];                  // style values changed since the page's objects were refreshed
  State st;

  lv_obj_t *hdr_icon[PAGE_COUNT], *hdr_title[PAGE_COUNT];
  lv_obj_t *hdr_date[PAGE_COUNT], *hdr_time[PAGE_COUNT];

  StatusChip home_chip[4];
  FloorCard home_floor[2];
  RoomCard home_room[ROOM_COUNT];

  LightTile living_light[4];
  GroupCard living_group;
  SceneCard living_scenes;
  ThermostatCard living_thermo;
  SparkCard living_temp;
  MediaMini living_media;
  SwitchTile living_tv;
  SensorTile living_humidity;

  LightTile kitchen_light[3];
  SwitchTile kitchen_coffee;
  GroupCard kitchen_group;
  DishwasherCard kitchen_dish;
  BarsCard kitchen_power;
  SensorTile kitchen_fridge, kitchen_oven;
  SceneCard kitchen_scenes;

  LightTile bedroom_light[3];
  SwitchTile bedroom_fan, bedroom_wake;
  GroupCard bedroom_group;
  SceneCard bedroom_scenes;
  CoverCard bedroom_cover;
  SparkCard bedroom_temp;
  SensorTile bedroom_humidity;

  ThermostatCard climate_thermo;
  SparkCard climate_temp;
  BarsCard climate_power;
  WeatherCard climate_weather;
  RowList climate_air;

  NowPlaying media_now;
  SpeakerCard media_speakers;
  VolumeCard media_volume;
  ListCard media_next;

  CameraCard security_camera[2];
  ConfirmCard security_confirm[4];

  Swatch style_swatch[THEME_COUNT];
  lv_obj_t *style_reset, *style_brightness, *style_brightness_value;

  lv_obj_t *clock_card, *clock_weather;
  lv_obj_t *clock_digit[5], *clock_date, *clock_greeting;
  lv_obj_t *clock_forecast_day[5];
  GlassCard clock_glass[4];
  int8_t shown_digit[5];
  int16_t digit_ink_left[11], digit_ink_right[11];
  bool digit_ink_scanned;

  lv_indev_t *indev;
  bool indev_registered;
  bool tracking, swallowed, press_on_slider;
  lv_point_t start;

  uint8_t tour_state;
  uint8_t tour_page;
  uint8_t tour_samples;
  uint64_t tour_sum_us;
  uint32_t tour_max_us;
  int8_t tour_max_page;
  uint32_t tour_deadline;

  uint32_t render_t0, last_render;
  uint32_t last_100;
  uint32_t last_media, last_thermo, last_dish;
  uint32_t last_sensor, last_history;
  uint32_t last_backlight;
  uint32_t seed;

  int current;
  bool dirty;
  bool refresh_soon;
  int log_page;
  lv_display_t *disp;
};

static Ctx g;

static const int DEFAULT_THEME[PAGE_COUNT] = {
  TH_DAYLIGHT, TH_MIDNIGHT, TH_WARM, TH_SOFT, TH_DAYLIGHT,
  TH_WARM, TH_MONO, TH_MIDNIGHT, TH_GLASS
};

static const char *const PAGE_NAMES[PAGE_COUNT] = {
  "Home", "Living room", "Kitchen", "Bedroom", "Climate and energy",
  "Media", "Security", "Styles", "Clock"
};

static const char *const PAGE_TITLES[PAGE_COUNT] = {
  "Good evening", "Living room", "Kitchen", "Bedroom",
  "Climate and energy", "Media", "Security", "Styles", "Clock"
};

static const char *const PAGE_ICONS[PAGE_COUNT] = {
  IC_HOME, IC_SOFA, IC_SILVERWARE_FORK_KNIFE, IC_BED,
  IC_HOME_THERMOMETER, IC_MUSIC_NOTE, IC_SHIELD_HOME,
  IC_PALETTE, IC_CLOCK_OUTLINE
};

static const char *const THEME_NAMES[THEME_COUNT] = {
  "midnight", "daylight", "warm", "soft", "mono", "glass"
};

static const char *const WD_SHORT[7] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char *const WD_LONG[7] = {
  "Sunday", "Monday", "Tuesday", "Wednesday",
  "Thursday", "Friday", "Saturday"
};

static const char *const MONTH_SHORT[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const char *const MONTH_LONG[12] = {
  "January", "February", "March", "April", "May", "June",
  "July", "August", "September", "October", "November", "December"
};

static const SceneDef LIVING_SCENES[4] = {
  {IC_MOVIE_OPEN, "Movie"},
  {IC_SILVERWARE_FORK_KNIFE, "Dinner"},
  {IC_SPA, "Relax"},
  {IC_POWER, "All off"}
};

static const SceneDef KITCHEN_SCENES[4] = {
  {IC_POT_STEAM, "Cooking"},
  {IC_SILVERWARE_FORK_KNIFE, "Dinner"},
  {IC_DISHWASHER, "Clean up"},
  {IC_POWER, "All off"}
};

static const SceneDef BEDROOM_SCENES[4] = {
  {IC_WEATHER_NIGHT, "Good night"},
  {IC_WHITE_BALANCE_SUNNY, "Wake up"},
  {IC_BOOK_OPEN_VARIANT, "Reading"},
  {IC_POWER, "All off"}
};

struct SceneLight {
  uint8_t index;
  bool on;
  uint8_t bri;
};

struct SceneEffect {
  uint8_t first;
  uint8_t count;
  uint8_t flags;
};

static const SceneLight SCENE_LIGHTS[] = {
  {0, false, 80}, {1, true, 20}, {2, true, 30}, {3, false, 70},
  {0, true, 60}, {1, true, 50}, {2, false, 50}, {3, true, 40},
  {0, false, 80}, {1, true, 40}, {2, true, 40}, {3, true, 30},
  {0, false, 80}, {1, false, 60}, {2, false, 50}, {3, false, 70},
  {4, true, 100}, {5, true, 100}, {6, true, 100},
  {4, true, 50}, {5, true, 30}, {6, true, 70},
  {4, true, 100}, {5, true, 100}, {6, true, 100},
  {4, false, 100}, {5, false, 70}, {6, false, 60},
  {8, false, 60}, {9, false, 40}, {10, false, 40},
  {8, true, 60}, {9, false, 40}, {10, false, 40},
  {8, false, 60}, {9, true, 60}, {10, true, 60},
  {8, false, 60}, {9, false, 40}, {10, false, 40}
};

enum {
  SE_TV_ON = 1,
  SE_TV_OFF = 2,
  SE_COFFEE_OFF = 4,
  SE_DISH_START = 8,
  SE_FAN_OFF = 16,
  SE_COVER_CLOSE = 32,
  SE_COVER_OPEN = 64
};

static const SceneEffect SCENE_EFFECTS[12] = {
  {0, 4, SE_TV_ON}, {4, 4, 0}, {8, 4, 0}, {12, 4, SE_TV_OFF},
  {16, 3, 0}, {19, 3, 0}, {22, 3, SE_DISH_START},
  {25, 3, SE_COFFEE_OFF},
  {28, 3, (uint8_t)(SE_FAN_OFF | SE_COVER_CLOSE)},
  {31, 3, SE_COVER_OPEN}, {34, 3, 0}, {37, 3, SE_FAN_OFF}
};

static const uint8_t ROOM_FIRST[ROOM_COUNT] = {0, 4, 7, 8, 11, 12};
static const uint8_t ROOM_COUNT_LIGHTS[ROOM_COUNT] = {4, 3, 1, 3, 1, 1};

static uint32_t now_us_value() {
  return g.assets.now_us ? g.assets.now_us() : lv_tick_get() * 1000U;
}

static void request_model_refresh() {
  g.dirty = true;
  g.refresh_soon = true;
}

static int lights_on(int room) {
  int n = 0;
  for(int i = 0; i < 13; ++i)
    if(g.st.lights[i].room == room && g.st.lights[i].on) ++n;
  return n;
}

static int lights_total(int room) {
  int n = 0;
  for(int i = 0; i < 13; ++i)
    if(g.st.lights[i].room == room) ++n;
  return n;
}

static int floor_on(int floor) {
  int n = 0;
  for(int i = 0; i < 13; ++i) {
    bool ground = g.st.lights[i].room <= RM_HALLWAY;
    if((floor == 0) == ground && g.st.lights[i].on) ++n;
  }
  return n;
}

static int floor_total(int floor) {
  int n = 0;
  for(int i = 0; i < 13; ++i) {
    bool ground = g.st.lights[i].room <= RM_HALLWAY;
    if((floor == 0) == ground) ++n;
  }
  return n;
}

static int all_on() {
  int n = 0;
  for(int i = 0; i < 13; ++i) if(g.st.lights[i].on) ++n;
  return n;
}

static int group_brightness(int room) {
  int sum = 0, n = 0;
  for(int i = 0; i < 13; ++i)
    if(g.st.lights[i].room == room && g.st.lights[i].on) {
      sum += g.st.lights[i].bri;
      ++n;
    }
  if(n) return sum / n;
  for(int i = 0; i < 13; ++i)
    if(g.st.lights[i].room == room) {
      sum += g.st.lights[i].bri;
      ++n;
    }
  return n ? sum / n : 0;
}

static const char *greeting() {
  int h = g.st.hour;
  if(h >= 5 && h <= 11) return "Good morning";
  if(h >= 12 && h <= 17) return "Good afternoon";
  if(h >= 18 && h <= 22) return "Good evening";
  return "Good night";
}

static void thermostat_status(char *buf, size_t n, uint32_t now) {
  if(g.st.thermo_pending_until &&
     (int32_t)(now - g.st.thermo_pending_until) < 0) {
    char t[24];
    fmt_tenths(t, sizeof(t), g.st.thermo_target);
    snprintf(buf, n, "Setting %s...", t);
  } else if(g.st.thermo_mode == 3) {
    snprintf(buf, n, "Off");
  } else if((g.st.thermo_mode == 0 || g.st.thermo_mode == 2) &&
            g.st.thermo_current < g.st.thermo_target - 2) {
    snprintf(buf, n, "Heating");
  } else if((g.st.thermo_mode == 1 || g.st.thermo_mode == 2) &&
            g.st.thermo_current > g.st.thermo_target + 2) {
    snprintf(buf, n, "Cooling");
  } else {
    snprintf(buf, n, "Idle");
  }
}

static void cover_text(char *buf, size_t n) {
  if(g.st.cover_moving) {
    snprintf(buf, n, g.st.cover_target > g.st.cover_pos ?
             "Opening... %u%%" : "Closing... %u%%", g.st.cover_pos);
  } else if(g.st.cover_pos == 100) {
    snprintf(buf, n, "Open");
  } else if(g.st.cover_pos == 0) {
    snprintf(buf, n, "Closed");
  } else {
    snprintf(buf, n, "Open %u%%", g.st.cover_pos);
  }
}

static void blinds_extra(char *buf, size_t n) {
  if(g.st.cover_pos == 100) snprintf(buf, n, "Blinds open");
  else if(g.st.cover_pos == 0) snprintf(buf, n, "Blinds closed");
  else snprintf(buf, n, "Blinds %u%%", g.st.cover_pos);
}

static const char *dish_extra() {
  if(g.st.dish_state == 0) return "Dishwasher running";
  if(g.st.dish_state == 1) return "Dishwasher paused";
  return "Dishwasher done";
}

static const char *alarm_name() {
  if(g.st.alarm == 1) return "Armed home";
  if(g.st.alarm == 2) return "Armed away";
  return "Disarmed";
}

static void set_group(int room) {
  bool any = lights_on(room) != 0;
  for(int i = 0; i < 13; ++i)
    if(g.st.lights[i].room == room) g.st.lights[i].on = !any;
}

static void set_floor_group(int floor) {
  bool any = floor_on(floor) != 0;
  for(int i = 0; i < 13; ++i) {
    bool ground = g.st.lights[i].room <= RM_HALLWAY;
    if((floor == 0) == ground) g.st.lights[i].on = !any;
  }
}

static void on_jump(lv_event_t *e) {
  go_to((int)(intptr_t)lv_event_get_user_data(e));
}

static void on_light(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if(i >= 0 && i < 13) {
    g.st.lights[i].on = !g.st.lights[i].on;
    request_model_refresh();
  }
}

static void on_switch(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if(i == 0) g.st.sw_tv = !g.st.sw_tv;
  else if(i == 1) g.st.sw_coffee = !g.st.sw_coffee;
  else if(i == 2) g.st.sw_fan = !g.st.sw_fan;
  else if(i == 3) g.st.sw_wake = !g.st.sw_wake;
  request_model_refresh();
}

static void on_room_group(lv_event_t *e) {
  int room = (int)(intptr_t)lv_event_get_user_data(e);
  if(room >= 0 && room < ROOM_COUNT) {
    set_group(room);
    request_model_refresh();
  }
}

static void on_floor_group(lv_event_t *e) {
  int floor = (int)(intptr_t)lv_event_get_user_data(e);
  if(floor == 0 || floor == 1) {
    set_floor_group(floor);
    request_model_refresh();
  }
}

static void on_group_slider(lv_event_t *e) {
  if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  int room = (int)(intptr_t)lv_event_get_user_data(e);
  int v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
  bool any = lights_on(room) != 0;
  for(int i = 0; i < 13; ++i) {
    if(g.st.lights[i].room != room) continue;
    if(any) {
      if(g.st.lights[i].on) g.st.lights[i].bri = (uint8_t)v;
    } else {
      g.st.lights[i].on = true;
      g.st.lights[i].bri = (uint8_t)v;
    }
  }
  request_model_refresh();
}

// A blind already at the target has nothing to do, so it must not report moving.
static void set_cover_target(uint8_t target) {
  g.st.cover_target = target;
  g.st.cover_moving = g.st.cover_target != g.st.cover_pos;
}

static void apply_scene(int page, int idx) {
  int base = page == PG_LIVING ? 0 : page == PG_KITCHEN ? 4 : 8;
  int si = base + idx;
  if(si < 0 || si >= 12) return;
  const SceneEffect &se = SCENE_EFFECTS[si];
  for(int i = 0; i < se.count; ++i) {
    const SceneLight &sl = SCENE_LIGHTS[se.first + i];
    g.st.lights[sl.index].on = sl.on;
    if(sl.on) g.st.lights[sl.index].bri = sl.bri;
  }
  if(se.flags & SE_TV_ON) g.st.sw_tv = true;
  if(se.flags & SE_TV_OFF) g.st.sw_tv = false;
  if(se.flags & SE_COFFEE_OFF) g.st.sw_coffee = false;
  if(se.flags & SE_DISH_START) {
    g.st.dish_state = 0;
    g.st.dish_left = 120;
  }
  if(se.flags & SE_FAN_OFF) g.st.sw_fan = false;
  if(se.flags & SE_COVER_CLOSE) set_cover_target(0);
  if(se.flags & SE_COVER_OPEN) set_cover_target(100);
  g.st.scene_pulse_page = (int8_t)page;
  g.st.scene_pulse_idx = (int8_t)idx;
  g.st.scene_pulse_until = lv_tick_get() + 600U;
  request_model_refresh();
}

static void on_scene(lv_event_t *e) {
  int n = (int)(intptr_t)lv_event_get_user_data(e);
  int page = n / 4;
  int idx = n % 4;
  if(page == PG_LIVING || page == PG_KITCHEN || page == PG_BEDROOM)
    apply_scene(page, idx);
}

static void on_thermo_minus(lv_event_t *) {
  int v = g.st.thermo_target - 5;
  g.st.thermo_target = (int16_t)(v < 160 ? 160 : v);
  g.st.thermo_pending_until = lv_tick_get() + 1500U;
  request_model_refresh();
}

static void on_thermo_plus(lv_event_t *) {
  int v = g.st.thermo_target + 5;
  g.st.thermo_target = (int16_t)(v > 280 ? 280 : v);
  g.st.thermo_pending_until = lv_tick_get() + 1500U;
  request_model_refresh();
}

static void on_thermo_mode(lv_event_t *e) {
  int mode = (int)(intptr_t)lv_event_get_user_data(e);
  if(mode >= 0 && mode <= 3) {
    g.st.thermo_mode = (uint8_t)mode;
    g.st.thermo_pending_until = lv_tick_get() + 1500U;
    request_model_refresh();
  }
}

static void on_cover_up(lv_event_t *) {
  set_cover_target(100);
  request_model_refresh();
}

static void on_cover_down(lv_event_t *) {
  set_cover_target(0);
  request_model_refresh();
}

static void on_cover_stop(lv_event_t *) {
  g.st.cover_moving = false;
  request_model_refresh();
}

static void media_prev() {
  g.st.media_track = (uint8_t)((g.st.media_track + 3) % 4);
  g.st.media_elapsed = 0;
  request_model_refresh();
}

static void media_next() {
  g.st.media_track = (uint8_t)((g.st.media_track + 1) % 4);
  g.st.media_elapsed = 0;
  request_model_refresh();
}

static void on_media_prev(lv_event_t *) { media_prev(); }
static void on_media_next(lv_event_t *) { media_next(); }

static void on_media_play(lv_event_t *) {
  g.st.media_playing = !g.st.media_playing;
  request_model_refresh();
}

static void on_up_next(lv_event_t *e) {
  int k = (int)(intptr_t)lv_event_get_user_data(e);
  if(k >= 0 && k < 3) {
    g.st.media_track = (uint8_t)((g.st.media_track + 1 + k) % 4);
    g.st.media_elapsed = 0;
    g.st.media_playing = true;
    request_model_refresh();
  }
}

static void on_speaker_living(lv_event_t *) {
  g.st.spk_living = !g.st.spk_living;
  request_model_refresh();
}

static void on_speaker_kitchen(lv_event_t *) {
  g.st.spk_kitchen = !g.st.spk_kitchen;
  request_model_refresh();
}

static void on_volume(lv_event_t *e) {
  if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    g.st.media_volume =
      (uint8_t)lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
    request_model_refresh();
  }
}

static void on_dish(lv_event_t *) {
  if(g.st.dish_state == 0) g.st.dish_state = 1;
  else if(g.st.dish_state == 1) g.st.dish_state = 0;
  else {
    g.st.dish_state = 0;
    g.st.dish_left = 120;
  }
  request_model_refresh();
}

static Guard *guard_for(int id) {
  if(id == 0) return &g.st.lock_front;
  if(id == 1) return &g.st.lock_back;
  if(id == 2) return &g.st.garage;
  return nullptr;
}

static void on_confirm_a(lv_event_t *e) {
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  uint32_t now = lv_tick_get();
  if(id < 3) {
    Guard *q = guard_for(id);
    if(!q || q->phase == PH_BUSY) return;
    if(q->phase == PH_CONFIRM) {
      q->phase = PH_IDLE;
      q->deadline = 0;
    } else if(q->secured) {
      q->phase = PH_CONFIRM;
      q->deadline = now + 5000U;
    } else {
      q->phase = PH_BUSY;
      q->deadline = now + (id == 2 ? 3000U : 1500U);
    }
  } else {
    if(g.st.alarm_phase == PH_BUSY) return;
    if(g.st.alarm_phase == PH_CONFIRM) {
      g.st.alarm_phase = PH_IDLE;
      g.st.alarm_deadline = 0;
    } else if(g.st.alarm == 0) {
      g.st.alarm_next = 1;
      g.st.alarm_phase = PH_BUSY;
      g.st.alarm_deadline = now + 1500U;
    } else {
      g.st.alarm_phase = PH_CONFIRM;
      g.st.alarm_deadline = now + 5000U;
    }
  }
  request_model_refresh();
}

static void on_confirm_b(lv_event_t *e) {
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  uint32_t now = lv_tick_get();
  if(id < 3) {
    Guard *q = guard_for(id);
    if(q && q->phase == PH_CONFIRM) {
      q->phase = PH_BUSY;
      q->deadline = now + (id == 2 ? 3000U : 1500U);
      request_model_refresh();
    }
  } else if(g.st.alarm == 0 && g.st.alarm_phase == PH_IDLE) {
    g.st.alarm_next = 2;
    g.st.alarm_phase = PH_BUSY;
    g.st.alarm_deadline = now + 1500U;
    request_model_refresh();
  } else if(g.st.alarm != 0 && g.st.alarm_phase == PH_CONFIRM) {
    g.st.alarm_next = 0;
    g.st.alarm_phase = PH_BUSY;
    g.st.alarm_deadline = now + 1500U;
    request_model_refresh();
  }
}

static void on_swatch(lv_event_t *e) {
  int t = (int)(intptr_t)lv_event_get_user_data(e);
  if(t == TH_GLASS) go_to(PG_CLOCK);
  else set_style_override(t);
}

static void on_style_reset(lv_event_t *) {
  set_style_override(-1);
}

static void on_brightness(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  uint32_t now = lv_tick_get();
  int v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
  if(code == LV_EVENT_VALUE_CHANGED) {
    g.st.backlight = (uint8_t)v;
    if(g.assets.set_backlight &&
       (uint32_t)(now - g.last_backlight) >= 50U) {
      g.last_backlight = now;
      g.assets.set_backlight(v);
    }
    request_model_refresh();
  } else if(code == LV_EVENT_RELEASED) {
    g.st.backlight = (uint8_t)v;
    g.last_backlight = now;
    if(g.assets.set_backlight) g.assets.set_backlight(v);
    request_model_refresh();
  }
}

static void refresh_header(int p) {
  char date[32], time[16];
  int wd = g.st.weekday >= 1 && g.st.weekday <= 7 ? g.st.weekday - 1 : 0;
  int mo = g.st.month >= 1 && g.st.month <= 12 ? g.st.month - 1 : 0;
  snprintf(date, sizeof(date), "%s %d %s",
           WD_SHORT[wd], g.st.day, MONTH_SHORT[mo]);
  snprintf(time, sizeof(time), "%02d:%02d", g.st.hour, g.st.minute);
  set_text(g.hdr_title[p], p == PG_HOME ? greeting() : PAGE_TITLES[p]);
  set_text(g.hdr_date[p], date);
  set_text(g.hdr_time[p], time);
}

static void refresh_home() {
  char a[48], b[48], temp[24], extra[48];
  int n = all_on();
  if(n == 0) snprintf(a, sizeof(a), "All lights off");
  else if(n == 1) snprintf(a, sizeof(a), "1 light on");
  else snprintf(a, sizeof(a), "%d lights on", n);
  refresh_status_chip(g.home_chip[0], IC_LIGHTBULB_GROUP, a, false);
  refresh_status_chip(g.home_chip[1],
    g.st.lock_front.secured ? IC_LOCK : IC_LOCK_OPEN,
    g.st.lock_front.secured ? "Front door locked" : "Front door unlocked",
    !g.st.lock_front.secured);
  refresh_status_chip(g.home_chip[2],
    g.st.alarm == 0 ? IC_SHIELD_OFF :
    g.st.alarm == 1 ? IC_SHIELD_HOME : IC_SHIELD_LOCK,
    alarm_name(), g.st.alarm == 0);
  fmt_tenths(temp, sizeof(temp), g.st.thermo_current);
  snprintf(b, sizeof(b), "%s inside", temp);
  refresh_status_chip(g.home_chip[3], IC_THERMOMETER, b, false);

  refresh_floor(g.home_floor[0], floor_on(0), floor_total(0));
  refresh_floor(g.home_floor[1], floor_on(1), floor_total(1));

  refresh_room(g.home_room[RM_LIVING],
    lights_on(RM_LIVING), lights_total(RM_LIVING),
    g.st.room_temp[RM_LIVING],
    g.st.media_playing ? "Music playing" : "Music paused");
  refresh_room(g.home_room[RM_KITCHEN],
    lights_on(RM_KITCHEN), lights_total(RM_KITCHEN),
    g.st.room_temp[RM_KITCHEN], dish_extra());
  refresh_room(g.home_room[RM_HALLWAY],
    lights_on(RM_HALLWAY), lights_total(RM_HALLWAY),
    g.st.room_temp[RM_HALLWAY],
    g.st.lock_front.secured ? "Front door locked" : "Front door unlocked");
  blinds_extra(extra, sizeof(extra));
  refresh_room(g.home_room[RM_BEDROOM],
    lights_on(RM_BEDROOM), lights_total(RM_BEDROOM),
    g.st.room_temp[RM_BEDROOM], extra);
  refresh_room(g.home_room[RM_OFFICE],
    lights_on(RM_OFFICE), lights_total(RM_OFFICE),
    g.st.room_temp[RM_OFFICE], "Humidity 44%");
  refresh_room(g.home_room[RM_BATHROOM],
    lights_on(RM_BATHROOM), lights_total(RM_BATHROOM),
    g.st.room_temp[RM_BATHROOM], "Humidity 58%");
}

static void refresh_living() {
  char status[48], hum[16], range[48];
  for(int i = 0; i < 4; ++i)
    refresh_light_tile(g.living_light[i],
                       g.st.lights[i].on, g.st.lights[i].bri);
  refresh_group(g.living_group, lights_on(RM_LIVING),
                lights_total(RM_LIVING), group_brightness(RM_LIVING));
  int lit = g.st.scene_pulse_page == PG_LIVING &&
            (int32_t)(lv_tick_get() - g.st.scene_pulse_until) < 0 ?
            g.st.scene_pulse_idx : -1;
  refresh_scenes(g.living_scenes, lit);
  thermostat_status(status, sizeof(status), lv_tick_get());
  refresh_thermostat(g.living_thermo, g.st.thermo_target,
                     g.st.thermo_current, g.st.thermo_mode, status);
  refresh_spark(g.living_temp, g.st.temp_hist, g.st.chart_version,
                g.st.thermo_current);
  const Track &t = TRACKS[g.st.media_track];
  refresh_media_mini(g.living_media, t.title, t.artist, t.seconds,
                     g.st.media_elapsed, g.st.media_playing);
  refresh_switch_tile(g.living_tv, g.st.sw_tv,
                      g.st.sw_tv ? "On" : "Off");
  snprintf(hum, sizeof(hum), "%u%%", g.st.hum_living);
  snprintf(range, sizeof(range), "24 h range %u%% to %u%%",
           g.st.hum_living_min, g.st.hum_living_max);
  refresh_sensor(g.living_humidity, hum, "Comfortable", range);
}

static void refresh_kitchen() {
  char value[24], fridge[20], oven[20];
  for(int i = 0; i < 3; ++i)
    refresh_light_tile(g.kitchen_light[i],
                       g.st.lights[4 + i].on, g.st.lights[4 + i].bri);
  refresh_switch_tile(g.kitchen_coffee, g.st.sw_coffee,
                      g.st.sw_coffee ? "Brewing" : "Off");
  refresh_group(g.kitchen_group, lights_on(RM_KITCHEN),
                lights_total(RM_KITCHEN), group_brightness(RM_KITCHEN));
  refresh_dish(g.kitchen_dish, g.st.dish_state, g.st.dish_left);
  int hour = g.st.hour >= 0 && g.st.hour < 24 ? g.st.hour : 0;
  int pv = g.st.kitchen_power_hist[hour];
  snprintf(value, sizeof(value), "%d.%d kW", pv / 10, pv % 10);
  refresh_bars(g.kitchen_power, g.page_styles[PG_KITCHEN], hour, value);
  snprintf(fridge, sizeof(fridge), "%d.%d" "\xC2\xB0",
           g.st.fridge_x10 / 10,
           g.st.fridge_x10 < 0 ? -(g.st.fridge_x10 % 10) :
                                g.st.fridge_x10 % 10);
  snprintf(oven, sizeof(oven), "%d" "\xC2\xB0", g.st.oven);
  refresh_sensor(g.kitchen_fridge, fridge, "Door closed");
  refresh_sensor(g.kitchen_oven, oven, "Preheated");
  int lit = g.st.scene_pulse_page == PG_KITCHEN &&
            (int32_t)(lv_tick_get() - g.st.scene_pulse_until) < 0 ?
            g.st.scene_pulse_idx : -1;
  refresh_scenes(g.kitchen_scenes, lit);
}

static void refresh_bedroom() {
  char cover[48], hum[16];
  for(int i = 0; i < 3; ++i)
    refresh_light_tile(g.bedroom_light[i],
                       g.st.lights[8 + i].on, g.st.lights[8 + i].bri);
  refresh_switch_tile(g.bedroom_fan, g.st.sw_fan,
                      g.st.sw_fan ? "On" : "Off");
  refresh_group(g.bedroom_group, lights_on(RM_BEDROOM),
                lights_total(RM_BEDROOM), group_brightness(RM_BEDROOM));
  int lit = g.st.scene_pulse_page == PG_BEDROOM &&
            (int32_t)(lv_tick_get() - g.st.scene_pulse_until) < 0 ?
            g.st.scene_pulse_idx : -1;
  refresh_scenes(g.bedroom_scenes, lit);
  cover_text(cover, sizeof(cover));
  refresh_cover(g.bedroom_cover, g.st.cover_pos, cover);
  refresh_spark(g.bedroom_temp, g.st.bed_temp_hist, g.st.chart_version,
                g.st.room_temp[RM_BEDROOM]);
  snprintf(hum, sizeof(hum), "%u%%", g.st.hum_bedroom);
  refresh_sensor(g.bedroom_humidity, hum, "Comfortable");
  refresh_switch_tile(g.bedroom_wake, g.st.sw_wake,
                      g.st.sw_wake ? "06:30" : "Off");
}

static void forecast_days(WeatherCard &c) {
  const char *days[5];
  int wd = g.st.weekday >= 1 && g.st.weekday <= 7 ? g.st.weekday - 1 : 0;
  for(int i = 0; i < 5; ++i) days[i] = WD_SHORT[(wd + i + 1) % 7];
  refresh_weather_days(c, days);
}

static void refresh_climate() {
  char status[48], power[24], hum[20], co2[24], solar[24];
  thermostat_status(status, sizeof(status), lv_tick_get());
  refresh_thermostat(g.climate_thermo, g.st.thermo_target,
                     g.st.thermo_current, g.st.thermo_mode, status);
  refresh_spark(g.climate_temp, g.st.temp_hist, g.st.chart_version,
                g.st.thermo_current);
  snprintf(power, sizeof(power), "%u.%u kW",
           g.st.power_x10 / 10, g.st.power_x10 % 10);
  refresh_bars(g.climate_power, g.page_styles[PG_CLIMATE],
               g.st.hour, power);
  forecast_days(g.climate_weather);
  snprintf(hum, sizeof(hum), "%u%%", g.st.hum_living);
  snprintf(co2, sizeof(co2), "%u ppm", g.st.co2);
  snprintf(solar, sizeof(solar), "%u.%u kW",
           g.st.solar_x10 / 10, g.st.solar_x10 % 10);
  refresh_rowlist(g.climate_air, hum, co2, solar);
}

static void refresh_media_page() {
  const Track &t = TRACKS[g.st.media_track];
  const char *speakers;
  if(g.st.spk_living && g.st.spk_kitchen)
    speakers = g.st.media_playing ?
      "Playing on Living room and Kitchen" :
      "Paused on Living room and Kitchen";
  else if(g.st.spk_living)
    speakers = g.st.media_playing ?
      "Playing on Living room" : "Paused on Living room";
  else if(g.st.spk_kitchen)
    speakers = g.st.media_playing ?
      "Playing on Kitchen" : "Paused on Kitchen";
  else speakers = "No speaker selected";
  refresh_now_playing(g.media_now, t.title, t.artist, t.album,
                      t.seconds, g.st.media_elapsed,
                      g.st.media_playing, speakers);
  refresh_speakers(g.media_speakers,
                   g.st.spk_living, g.st.spk_kitchen);
  refresh_volume(g.media_volume, g.st.media_volume);
  for(int k = 0; k < 3; ++k) {
    int i = (g.st.media_track + 1 + k) % 4;
    char duration[16];
    fmt_mmss(duration, sizeof(duration), TRACKS[i].seconds);
    refresh_up_next(g.media_next, k, TRACKS[i].title, duration);
  }
}

static ConfirmView guard_view(int id) {
  Guard *q = guard_for(id);
  bool garage = id == 2;
  const char *name = id == 0 ? "Front door" :
                     id == 1 ? "Back door" : "Garage door";
  ConfirmView v{};
  v.name = name;
  v.small_text = false;
  if(q->secured) {
    v.icon = garage ? IC_GARAGE : IC_LOCK;
    v.highlighted = false;
    v.status = q->phase == PH_BUSY ?
      (garage ? "Opening..." : "Unlocking...") :
      (garage ? "Closed" : "Locked");
    if(q->phase == PH_CONFIRM) {
      v.prompt = id == 0 ? "Unlock front door?" :
                 id == 1 ? "Unlock back door?" : "Open garage door?";
      v.a = "Cancel";
      v.b = garage ? "Open" : "Unlock";
      v.a_primary = false;
      v.b_primary = true;
    } else {
      v.prompt = nullptr;
      v.a = q->phase == PH_BUSY ? "Please wait" :
            garage ? "Open" : "Unlock";
      v.b = nullptr;
      v.a_primary = false;
      v.b_primary = false;
    }
  } else {
    v.icon = garage ? IC_GARAGE_OPEN : IC_LOCK_OPEN;
    v.highlighted = true;
    v.status = q->phase == PH_BUSY ?
      (garage ? "Closing..." : "Locking...") :
      (garage ? "Open" : "Unlocked");
    v.prompt = nullptr;
    v.a = q->phase == PH_BUSY ? "Please wait" :
          garage ? "Close" : "Lock";
    v.b = nullptr;
    v.a_primary = q->phase == PH_IDLE;
    v.b_primary = false;
  }
  return v;
}

static ConfirmView alarm_view() {
  ConfirmView v{};
  v.icon = g.st.alarm == 0 ? IC_SHIELD_OFF :
           g.st.alarm == 1 ? IC_SHIELD_HOME : IC_SHIELD_LOCK;
  v.name = alarm_name();
  v.highlighted = g.st.alarm != 0;
  v.small_text = g.st.alarm == 0 && g.st.alarm_phase == PH_IDLE;
  if(g.st.alarm == 0) {
    v.status = g.st.alarm_phase == PH_BUSY ?
               "Arming..." : "Ready to arm";
    v.prompt = nullptr;
    if(g.st.alarm_phase == PH_BUSY) {
      v.a = "Please wait";
      v.b = nullptr;
    } else {
      v.a = "Arm home";
      v.b = "Arm away";
    }
    v.a_primary = false;
    v.b_primary = false;
  } else {
    v.status = g.st.alarm_phase == PH_BUSY ?
               "Disarming..." : "All sensors OK";
    if(g.st.alarm_phase == PH_CONFIRM) {
      v.prompt = "Disarm the alarm?";
      v.a = "Cancel";
      v.b = "Disarm";
      v.b_primary = true;
    } else {
      v.prompt = nullptr;
      v.a = g.st.alarm_phase == PH_BUSY ?
            "Please wait" : "Disarm";
      v.b = nullptr;
      v.b_primary = false;
    }
    v.a_primary = false;
  }
  return v;
}

static void refresh_security() {
  char time[16], a[48], b[48];
  snprintf(time, sizeof(time), "%02d:%02d", g.st.hour, g.st.minute);
  snprintf(a, sizeof(a), "Front door " "\xE2\x80\xA2" " %s", time);
  snprintf(b, sizeof(b), "Driveway " "\xE2\x80\xA2" " %s", time);
  refresh_camera(g.security_camera[0], a);
  refresh_camera(g.security_camera[1], b);
  for(int i = 0; i < 3; ++i) {
    ConfirmView v = guard_view(i);
    refresh_confirm(g.security_confirm[i],
                    g.page_styles[PG_SECURITY], v);
  }
  ConfirmView av = alarm_view();
  refresh_confirm(g.security_confirm[3],
                  g.page_styles[PG_SECURITY], av);
}

static void refresh_styles_page() {
  for(int i = 0; i < THEME_COUNT; ++i)
    refresh_swatch(g.style_swatch[i],
                   i != TH_GLASS && g.st.style_override == i,
                   i == TH_GLASS);
  set_checked(g.style_reset, g.st.style_override == -1);
  set_slider(g.style_brightness, g.st.backlight);
  char value[16];
  snprintf(value, sizeof(value), "%u%%", g.st.backlight);
  set_text(g.style_brightness_value, value);
}

// Finds the first and one-past-last inked column of each clock digit image, so
// the digits can be spaced by what is visible. A blank or missing image counts
// as inked across its whole width. Runs once, as the scan touches every pixel.
static void measure_digit_ink() {
  if(g.digit_ink_scanned) return;
  g.digit_ink_scanned = true;
  for(int i = 0; i < 11; ++i) {
    const lv_image_dsc_t *d = g.assets.digits[i];
    int w = d ? d->header.w : 0;
    int h = d ? d->header.h : 0;
    int stride = d ? d->header.stride : 0;
    g.digit_ink_left[i] = 0;
    g.digit_ink_right[i] = (int16_t)w;
    if(!d || !d->data || w <= 0 || h <= 0 || stride <= 0) continue;
    int left = w;
    int right = 0;
    for(int y = 0; y < h; ++y) {
      const uint8_t *row = d->data + y * stride;
      for(int x = 0; x < w; ++x) {
        if(row[x] != 0) {
          if(x < left) left = x;
          if(x + 1 > right) right = x + 1;
        }
      }
    }
    if(right > left) {
      g.digit_ink_left[i] = (int16_t)left;
      g.digit_ink_right[i] = (int16_t)right;
    }
  }
}

static void refresh_clock_digits() {
  if(!g.assets.digits[0]) return;
  int d[5] = {
    g.st.hour / 10, g.st.hour % 10, 10,
    g.st.minute / 10, g.st.minute % 10
  };
  bool changed = false;
  for(int i = 0; i < 5; ++i) {
    if(g.clock_digit[i] && g.shown_digit[i] != d[i] &&
       g.assets.digits[d[i]]) {
      lv_image_set_src(g.clock_digit[i], g.assets.digits[d[i]]);
      g.shown_digit[i] = (int8_t)d[i];
      changed = true;
    }
  }
  if(!changed) return;
  // Spacing by ink keeps the gaps even: fixed boxes leave a wide hole around a
  // narrow digit such as 1. Boxes may overlap, as their margins are transparent.
  int ink_width = 4 * 22;
  for(int i = 0; i < 5; ++i) {
    int digit = g.shown_digit[i];
    if(digit < 0 || digit >= 11) return;
    ink_width += g.digit_ink_right[digit] - g.digit_ink_left[digit];
  }
  int ink_x = (816 - ink_width) / 2;
  for(int i = 0; i < 5; ++i) {
    int digit = g.shown_digit[i];
    set_x(g.clock_digit[i], ink_x - g.digit_ink_left[digit]);
    ink_x += g.digit_ink_right[digit] - g.digit_ink_left[digit];
    if(i < 4) ink_x += 22;
  }
}

static void refresh_clock() {
  char date[64], value[32], sub[96], target[24], current[24], status[48];
  int wd = g.st.weekday >= 1 && g.st.weekday <= 7 ? g.st.weekday - 1 : 0;
  int mo = g.st.month >= 1 && g.st.month <= 12 ? g.st.month - 1 : 0;
  refresh_clock_digits();
  snprintf(date, sizeof(date), "%s %d %s",
           WD_LONG[wd], g.st.day, MONTH_LONG[mo]);
  set_text(g.clock_date, date);
  set_text(g.clock_greeting, greeting());
  for(int i = 0; i < 5; ++i)
    set_text(g.clock_forecast_day[i], WD_SHORT[(wd + i + 1) % 7]);

  int total = all_on();
  if(total) snprintf(value, sizeof(value), "%d on", total);
  else snprintf(value, sizeof(value), "All off");
  snprintf(sub, sizeof(sub), "Ground floor %d " "\xE2\x80\xA2"
           " Upstairs %d", floor_on(0), floor_on(1));
  refresh_glass(g.clock_glass[0], IC_LIGHTBULB_GROUP, value, sub);

  refresh_glass(g.clock_glass[1],
    g.st.lock_front.secured ? IC_LOCK : IC_LOCK_OPEN,
    g.st.lock_front.secured ? "Locked" : "Unlocked", alarm_name());

  fmt_tenths(current, sizeof(current), g.st.thermo_current);
  fmt_tenths(target, sizeof(target), g.st.thermo_target);
  thermostat_status(status, sizeof(status), lv_tick_get());
  snprintf(sub, sizeof(sub), "Target %s " "\xE2\x80\xA2" " %s",
           target, status);
  refresh_glass(g.clock_glass[2], IC_THERMOMETER, current, sub);

  refresh_glass(g.clock_glass[3], IC_MUSIC_NOTE,
                g.st.media_playing ? "Playing" : "Paused",
                TRACKS[g.st.media_track].title);
}

typedef void (*RefreshFn)();

static RefreshFn REFRESHERS[PAGE_COUNT] = {
  refresh_home, refresh_living, refresh_kitchen, refresh_bedroom,
  refresh_climate, refresh_media_page, refresh_security,
  refresh_styles_page, refresh_clock
};

static void refresh_page_internal(int p) {
  if(p < 0 || p >= PAGE_COUNT) return;
  refresh_header(p);
  REFRESHERS[p]();
}

static void build_header(int p) {
  Styles &s = g.page_styles[p];
  g.hdr_icon[p] = mk_label(g.page[p], 24, 16, 40, 40,
                           &fs.I40, &s.t_acc, nullptr,
                           PAGE_ICONS[p], LV_TEXT_ALIGN_CENTER);
  g.hdr_title[p] = mk_label(g.page[p], 76, 21, 700, 30,
                            &fs.F28, &s.tx, nullptr,
                            PAGE_TITLES[p], LV_TEXT_ALIGN_LEFT);
  g.hdr_date[p] = mk_label(g.page[p], 896, 28, 224, 18,
                           &fs.F16, &s.tx2, nullptr,
                           "", LV_TEXT_ALIGN_RIGHT);
  g.hdr_time[p] = mk_label(g.page[p], 1136, 21, 120, 30,
                           &fs.F28, &s.tx, nullptr,
                           "", LV_TEXT_ALIGN_RIGHT);
  int x = 546;
  for(int i = 0; i < PAGE_COUNT; ++i) {
    int w = i == p ? 28 : 10;
    lv_obj_t *dot = mk_obj(g.page[p], x, 766, w, 10);
    lv_obj_add_style(dot, i == p ? &s.dot_on : &s.dot, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    x += w + 10;
  }
}

static void build_home_page() {
  Styles &s = g.page_styles[PG_HOME];
  build_status_chip(g.home_chip[0], g.page[PG_HOME], s, 24, 80);
  build_status_chip(g.home_chip[1], g.page[PG_HOME], s, 336, 80);
  build_status_chip(g.home_chip[2], g.page[PG_HOME], s, 648, 80);
  build_status_chip(g.home_chip[3], g.page[PG_HOME], s, 960, 80);

  build_floor(g.home_floor[0], g.page[PG_HOME], s, 24, 152,
              IC_HOME_FLOOR_G, "Ground floor", on_floor_group,
              (void *)(intptr_t)0);
  build_floor(g.home_floor[1], g.page[PG_HOME], s, 24, 460,
              IC_HOME_FLOOR_1, "Upstairs", on_floor_group,
              (void *)(intptr_t)1);

  static const int x[ROOM_COUNT] = {336, 648, 960, 336, 648, 960};
  static const int y[ROOM_COUNT] = {152, 152, 152, 460, 460, 460};
  static const char *const icon[ROOM_COUNT] = {
    IC_SOFA, IC_SILVERWARE_FORK_KNIFE, IC_DOOR,
    IC_BED, IC_DESK, IC_SHOWER
  };
  static const char *const name[ROOM_COUNT] = {
    "Living room", "Kitchen", "Hallway",
    "Bedroom", "Office", "Bathroom"
  };
  static const int jump[ROOM_COUNT] = {
    PG_LIVING, PG_KITCHEN, PG_SECURITY, PG_BEDROOM, -1, -1
  };
  for(int i = 0; i < ROOM_COUNT; ++i) {
    bool has = jump[i] >= 0;
    build_room(g.home_room[i], g.page[PG_HOME], s, x[i], y[i],
               icon[i], name[i], has,
               has ? on_jump : nullptr,
               has ? (void *)(intptr_t)jump[i] : nullptr,
               on_room_group, (void *)(intptr_t)i);
  }
}

static void build_living_page() {
  Styles &s = g.page_styles[PG_LIVING];
  static const int x[4] = {24, 336, 24, 336};
  static const int y[4] = {80, 80, 252, 252};
  for(int i = 0; i < 4; ++i)
    build_light_tile(g.living_light[i], g.page[PG_LIVING], s,
                     x[i], y[i], g.st.lights[i].icon,
                     g.st.lights[i].name, on_light,
                     (void *)(intptr_t)i);
  build_group(g.living_group, g.page[PG_LIVING], s, 24, 424,
              on_group_slider, on_room_group,
              (void *)(intptr_t)RM_LIVING);
  build_scenes(g.living_scenes, g.page[PG_LIVING], s, 336, 424,
               LIVING_SCENES, on_scene, PG_LIVING * 4);
  build_thermostat(g.living_thermo, g.page[PG_LIVING], s, 648, 80,
                   false, on_thermo_minus, on_thermo_plus,
                   on_thermo_mode);
  build_spark(g.living_temp, g.page[PG_LIVING], s, 648, 510,
              296, 242, "Temperature, 24 h");
  build_media_mini(g.living_media, g.page[PG_LIVING], s, 960, 80,
                   on_media_prev, on_media_play, on_media_next);
  build_switch_tile(g.living_tv, g.page[PG_LIVING], s, 960, 338,
                    IC_TELEVISION, "TV", on_switch,
                    (void *)(intptr_t)0);
  build_sensor(g.living_humidity, g.page[PG_LIVING], s, 960, 510,
               242, "Humidity", IC_WATER_PERCENT);
}

static void build_kitchen_page() {
  Styles &s = g.page_styles[PG_KITCHEN];
  static const int x[3] = {24, 336, 24};
  static const int y[3] = {80, 80, 252};
  for(int i = 0; i < 3; ++i)
    build_light_tile(g.kitchen_light[i], g.page[PG_KITCHEN], s,
                     x[i], y[i], g.st.lights[4 + i].icon,
                     g.st.lights[4 + i].name, on_light,
                     (void *)(intptr_t)(4 + i));
  build_switch_tile(g.kitchen_coffee, g.page[PG_KITCHEN], s, 336, 252,
                    IC_COFFEE_MAKER, "Coffee maker", on_switch,
                    (void *)(intptr_t)1);
  build_group(g.kitchen_group, g.page[PG_KITCHEN], s, 648, 80,
              on_group_slider, on_room_group,
              (void *)(intptr_t)RM_KITCHEN);
  build_dish(g.kitchen_dish, g.page[PG_KITCHEN], s, 960, 80, on_dish);
  build_bars(g.kitchen_power, g.page[PG_KITCHEN], s, 24, 424,
             608, 328, "Kitchen power today", g.st.kitchen_power_hist);
  build_sensor(g.kitchen_fridge, g.page[PG_KITCHEN], s, 648, 424,
               156, "Fridge", IC_FRIDGE_OUTLINE);
  build_sensor(g.kitchen_oven, g.page[PG_KITCHEN], s, 648, 596,
               156, "Oven", IC_STOVE);
  build_scenes(g.kitchen_scenes, g.page[PG_KITCHEN], s, 960, 424,
               KITCHEN_SCENES, on_scene, PG_KITCHEN * 4);
}

static void build_bedroom_page() {
  Styles &s = g.page_styles[PG_BEDROOM];
  static const int x[3] = {24, 336, 24};
  static const int y[3] = {80, 80, 252};
  for(int i = 0; i < 3; ++i)
    build_light_tile(g.bedroom_light[i], g.page[PG_BEDROOM], s,
                     x[i], y[i], g.st.lights[8 + i].icon,
                     g.st.lights[8 + i].name, on_light,
                     (void *)(intptr_t)(8 + i));
  build_switch_tile(g.bedroom_fan, g.page[PG_BEDROOM], s, 336, 252,
                    IC_FAN, "Fan", on_switch, (void *)(intptr_t)2);
  build_group(g.bedroom_group, g.page[PG_BEDROOM], s, 24, 424,
              on_group_slider, on_room_group,
              (void *)(intptr_t)RM_BEDROOM);
  build_scenes(g.bedroom_scenes, g.page[PG_BEDROOM], s, 336, 424,
               BEDROOM_SCENES, on_scene, PG_BEDROOM * 4);
  build_cover(g.bedroom_cover, g.page[PG_BEDROOM], s, 648, 80,
              on_cover_up, on_cover_stop, on_cover_down);
  build_spark(g.bedroom_temp, g.page[PG_BEDROOM], s, 960, 80,
              296, 328, "Temperature, 24 h");
  build_sensor(g.bedroom_humidity, g.page[PG_BEDROOM], s, 960, 424,
               156, "Humidity", IC_WATER_PERCENT);
  build_switch_tile(g.bedroom_wake, g.page[PG_BEDROOM], s, 960, 596,
                    IC_ALARM, "Wake-up alarm", on_switch,
                    (void *)(intptr_t)3);
}

static void build_climate_page() {
  Styles &s = g.page_styles[PG_CLIMATE];
  build_thermostat(g.climate_thermo, g.page[PG_CLIMATE], s, 24, 80,
                   true, on_thermo_minus, on_thermo_plus,
                   on_thermo_mode);
  build_spark(g.climate_temp, g.page[PG_CLIMATE], s, 544, 80,
              400, 328, "Temperature, 24 h");
  build_bars(g.climate_power, g.page[PG_CLIMATE], s, 544, 424,
             400, 328, "Power today", g.st.power_hist);
  build_weather(g.climate_weather, g.page[PG_CLIMATE], s, 960, 80);
  build_rowlist(g.climate_air, g.page[PG_CLIMATE], s, 960, 424);
}

static void build_media_page() {
  Styles &s = g.page_styles[PG_MEDIA];
  build_now_playing(g.media_now, g.page[PG_MEDIA], s, 24, 80,
                    on_media_prev, on_media_play, on_media_next);
  build_speakers(g.media_speakers, g.page[PG_MEDIA], s, 856, 80,
                 on_speaker_living, on_speaker_kitchen);
  build_volume(g.media_volume, g.page[PG_MEDIA], s, 856, 252,
               on_volume);
  build_up_next(g.media_next, g.page[PG_MEDIA], s, 856, 424,
                on_up_next);
}

static void build_security_page() {
  Styles &s = g.page_styles[PG_SECURITY];
  build_camera(g.security_camera[0], g.page[PG_SECURITY], s, 24, 80);
  build_camera(g.security_camera[1], g.page[PG_SECURITY], s, 648, 80);
  static const int x[4] = {24, 336, 648, 960};
  for(int i = 0; i < 4; ++i)
    build_confirm(g.security_confirm[i], g.page[PG_SECURITY], s,
                  x[i], 424, on_confirm_a, on_confirm_b,
                  (void *)(intptr_t)i);
}

static void build_styles_page() {
  Styles &s = g.page_styles[PG_STYLES];
  static const int x[6] = {24, 440, 856, 24, 440, 856};
  static const int y[6] = {80, 80, 80, 372, 372, 372};
  static const char *const names[6] = {
    "Midnight", "Daylight", "Warm", "Soft", "Mono", "Glass"
  };
  for(int i = 0; i < THEME_COUNT; ++i)
    build_swatch(g.style_swatch[i], g.page[PG_STYLES],
                 g.swatch_styles[i], x[i], y[i], names[i],
                 i == TH_GLASS, on_swatch, (void *)(intptr_t)i);

  g.style_reset = mk_btn(g.page[PG_STYLES], s, 24, 664, 400, 88,
                         on_style_reset, nullptr);
  mk_btn_text(g.style_reset, s, &fs.F20, "Use per-page styles");

  lv_obj_t *card = mk_card(g.page[PG_STYLES], s, 440, 664,
                           816, 88, false, false);
  mk_label(card, 16, 30, 28, 28, &fs.I28, &s.t_acc2, nullptr,
           IC_BRIGHTNESS_6, LV_TEXT_ALIGN_CENTER);
  mk_label(card, 56, 33, 200, 22, &fs.F20, &s.tx, nullptr,
           "Screen brightness", LV_TEXT_ALIGN_LEFT);
  g.style_brightness = mk_slider(card, s, 280, 36, 420,
                                 10, 100, on_brightness, nullptr);
  g.style_brightness_value =
    mk_label(card, 716, 33, 84, 22, &fs.F20, &s.tx, nullptr,
             "100%", LV_TEXT_ALIGN_RIGHT);
}

static void build_clock_weather(Styles &s) {
  lv_obj_t *c = g.clock_weather;
  mk_label(c, 24, 24, 64, 64, &fs.I64, &s.t_acc, nullptr,
           IC_WEATHER_PARTLY_CLOUDY, LV_TEXT_ALIGN_CENTER);
  mk_label(c, 104, 30, 272, 52, &fs.F48, &s.tx, nullptr,
           "12" "\xC2\xB0", LV_TEXT_ALIGN_LEFT);
  mk_label(c, 24, 104, 352, 22, &fs.F20, &s.tx, nullptr,
           "Partly cloudy", LV_TEXT_ALIGN_LEFT);
  mk_label(c, 24, 134, 352, 18, &fs.F16, &s.tx2, nullptr,
           "High 14" "\xC2\xB0" " " "\xE2\x80\xA2"
           " Low 8" "\xC2\xB0", LV_TEXT_ALIGN_LEFT);
  static const char *const icons[5] = {
    IC_WEATHER_PARTLY_CLOUDY, IC_WEATHER_SUNNY, IC_WEATHER_CLOUDY,
    IC_WEATHER_RAINY, IC_WEATHER_PARTLY_CLOUDY
  };
  static const int high[5] = {14, 16, 13, 11, 12};
  static const int low[5] = {8, 9, 7, 6, 7};
  for(int i = 0; i < 5; ++i) {
    int x = 24 + 70 * i;
    char a[12], b[12];
    g.clock_forecast_day[i] =
      mk_label(c, x, 196, 70, 18, &fs.F16, &s.tx2, nullptr,
               "", LV_TEXT_ALIGN_CENTER);
    mk_label(c, x + 21, 222, 28, 28, &fs.I28, &s.t_acc2, nullptr,
             icons[i], LV_TEXT_ALIGN_CENTER);
    snprintf(a, sizeof(a), "%d" "\xC2\xB0", high[i]);
    snprintf(b, sizeof(b), "%d" "\xC2\xB0", low[i]);
    mk_label(c, x, 262, 70, 18, &fs.F16, &s.tx, nullptr,
             a, LV_TEXT_ALIGN_CENTER);
    mk_label(c, x, 284, 70, 18, &fs.F16, &s.tx2, nullptr,
             b, LV_TEXT_ALIGN_CENTER);
  }
  mk_label(c, 24, 362, 352, 18, &fs.F16, &s.tx2, nullptr,
           "Feels like 10" "\xC2\xB0" " " "\xE2\x80\xA2"
           " Wind 14 km/h", LV_TEXT_ALIGN_LEFT);
}

static void build_clock_page() {
  Styles &s = g.page_styles[PG_CLOCK];
  if(g.assets.wallpaper) {
    lv_obj_t *img = lv_image_create(g.page[PG_CLOCK]);
    lv_obj_remove_style_all(img);
    lv_obj_set_pos(img, 0, 0);
    lv_image_set_src(img, g.assets.wallpaper);
    lv_obj_remove_flag(img, (lv_obj_flag_t)
      (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  }

  const Rect &r0 = GLASS_RECTS[0];
  const Rect &r1 = GLASS_RECTS[1];
  g.clock_card = mk_card(g.page[PG_CLOCK], s,
                         r0.x, r0.y, r0.w, r0.h, false, false);
  g.clock_weather = mk_card(g.page[PG_CLOCK], s,
                            r1.x, r1.y, r1.w, r1.h, false, false);

  for(int i = 0; i < 5; ++i) {
    g.clock_digit[i] = nullptr;
    g.shown_digit[i] = -1;
  }
  measure_digit_ink();
  if(g.assets.digits[0] && g.assets.digits[10]) {
    int dw = g.assets.digits[0]->header.w;
    int dh = g.assets.digits[0]->header.h;
    int cw = g.assets.digits[10]->header.w;
    for(int i = 0; i < 5; ++i) {
      int w = i == 2 ? cw : dw;
      lv_obj_t *img = lv_image_create(g.clock_card);
      lv_obj_remove_style_all(img);
      lv_obj_set_pos(img, 0, 74);
      lv_obj_set_size(img, w, dh);
      lv_obj_add_style(img, &s.img_tx, 0);
      lv_obj_remove_flag(img, (lv_obj_flag_t)
        (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
      g.clock_digit[i] = img;
    }
  }

  g.clock_date = mk_label(g.clock_card, 16, 274, 784, 30,
                          &fs.F28, &s.tx, nullptr, "",
                          LV_TEXT_ALIGN_CENTER);
  g.clock_greeting = mk_label(g.clock_card, 16, 318, 784, 22,
                              &fs.F20, &s.tx2, nullptr, "",
                              LV_TEXT_ALIGN_CENTER);
  build_clock_weather(s);

  for(int i = 0; i < 4; ++i) {
    const Rect &r = GLASS_RECTS[i + 2];
    static const char *const icons[4] = {
      IC_LIGHTBULB_GROUP, IC_LOCK, IC_THERMOMETER, IC_MUSIC_NOTE
    };
    static const char *const captions[4] = {
      "Lights", "Front door", "Inside", "Music"
    };
    static const int dest[4] = {
      PG_HOME, PG_SECURITY, PG_CLIMATE, PG_MEDIA
    };
    build_glass(g.clock_glass[i], g.page[PG_CLOCK], s,
                r.x, r.y, icons[i], captions[i],
                on_jump, (void *)(intptr_t)dest[i]);
  }
}

static void init_state(uint32_t now) {
  static const uint8_t room[13] = {
    RM_LIVING, RM_LIVING, RM_LIVING, RM_LIVING,
    RM_KITCHEN, RM_KITCHEN, RM_KITCHEN, RM_HALLWAY,
    RM_BEDROOM, RM_BEDROOM, RM_BEDROOM, RM_OFFICE, RM_BATHROOM
  };
  static const char *const name[13] = {
    "Ceiling", "Floor lamp", "Shelf strip", "Wall lights",
    "Ceiling", "Counter strip", "Island pendants", "Hallway",
    "Ceiling", "Bedside left", "Bedside right", "Desk lamp", "Mirror"
  };
  static const char *const icon[13] = {
    IC_CEILING_LIGHT, IC_FLOOR_LAMP, IC_LED_STRIP_VARIANT,
    IC_WALL_SCONCE_FLAT, IC_CEILING_LIGHT, IC_LED_STRIP_VARIANT,
    IC_LAMP, IC_CEILING_LIGHT, IC_CEILING_LIGHT, IC_LAMP, IC_LAMP,
    IC_LAMP, IC_LIGHTBULB
  };
  static const bool on[13] = {
    true, true, false, false, true, true, false,
    false, false, false, false, true, false
  };
  static const uint8_t bri[13] = {
    80, 60, 50, 70, 100, 70, 60, 60, 60, 40, 40, 90, 80
  };
  for(int i = 0; i < 13; ++i) {
    g.st.lights[i] = {room[i], name[i], icon[i], on[i], bri[i]};
  }
  g.st.sw_tv = false;
  g.st.sw_coffee = false;
  g.st.sw_fan = false;
  g.st.sw_wake = true;
  g.st.thermo_target = 215;
  g.st.thermo_current = 208;
  g.st.thermo_mode = 0;
  g.st.thermo_pending_until = 0;
  int16_t rt[ROOM_COUNT] = {208, 221, 202, 198, 204, 226};
  for(int i = 0; i < ROOM_COUNT; ++i) g.st.room_temp[i] = rt[i];
  g.st.hum_living = 46;
  g.st.hum_bedroom = 51;
  g.st.hum_living_min = 41;
  g.st.hum_living_max = 52;
  g.st.co2 = 612;
  g.st.solar_x10 = 24;
  g.st.power_x10 = 18;
  g.st.fridge_x10 = 40;
  g.st.oven = 180;
  for(int i = 0; i < 24; ++i) {
    g.st.temp_hist[i] = TEMP_HIST_INIT[i];
    g.st.bed_temp_hist[i] = BED_HIST_INIT[i];
    g.st.power_hist[i] = POWER_HIST_INIT[i];
    g.st.kitchen_power_hist[i] = KITCHEN_HIST_INIT[i];
  }
  g.st.cover_pos = 100;
  g.st.cover_target = 100;
  g.st.cover_moving = false;
  g.st.media_playing = true;
  g.st.media_track = 0;
  g.st.media_elapsed = 134;
  g.st.media_volume = 64;
  g.st.spk_living = true;
  g.st.spk_kitchen = false;
  g.st.lock_front = {true, PH_IDLE, 0};
  g.st.lock_back = {true, PH_IDLE, 0};
  g.st.garage = {true, PH_IDLE, 0};
  g.st.alarm = 1;
  g.st.alarm_next = 1;
  g.st.alarm_phase = PH_IDLE;
  g.st.alarm_deadline = 0;
  g.st.dish_state = 0;
  g.st.dish_left = 42;
  g.st.scene_pulse_page = -1;
  g.st.scene_pulse_idx = -1;
  g.st.scene_pulse_until = 0;
  g.st.style_override = -1;
  g.st.backlight = 100;
  g.st.year = 2026;
  g.st.month = 11;
  g.st.day = 10;
  g.st.weekday = 3;
  g.st.hour = 18;
  g.st.minute = 42;
  g.st.minute_started_ms = now;
  g.st.clock_from_host = false;
  g.st.chart_version = 1;
}

static void log_line(const char *line) {
  if(g.assets.log) g.assets.log(line);
}

static void on_render(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_RENDER_START) {
    g.render_t0 = now_us_value();
    return;
  }
  if(code != LV_EVENT_RENDER_READY) return;
  uint32_t end = now_us_value();
  g.last_render = end - g.render_t0;
  if(g.log_page >= 0) {
    int p = g.log_page;
    char line[96];
    uint32_t t10 = (g.last_render + 50U) / 100U;
    snprintf(line, sizeof(line), "page %d/9 %s [%s] render %u.%u ms",
             p + 1, page_name(p), theme_name(page_theme(p)),
             t10 / 10, t10 % 10);
    log_line(line);
    if(g.tour_state != 0) {
      g.tour_sum_us += g.last_render;
      ++g.tour_samples;
      if(g.last_render > g.tour_max_us) {
        g.tour_max_us = g.last_render;
        g.tour_max_page = (int8_t)p;
      }
    }
    g.log_page = -1;
  }
}

static void on_indev_pressed(lv_event_t *e) {
  lv_obj_t *obj = (lv_obj_t *)lv_event_get_param(e);
  g.press_on_slider = obj && lv_obj_check_type(obj, &lv_slider_class);
  if(g.indev) lv_indev_get_point(g.indev, &g.start);
  g.tracking = true;
  g.swallowed = false;
  cancel_tour();
  g.refresh_soon = true;
}

static void on_indev_released(lv_event_t *) {
  g.refresh_soon = true;
}

static void find_indev() {
  if(g.indev_registered) return;
  lv_indev_t *d = nullptr;
  while((d = lv_indev_get_next(d)) != nullptr) {
    if(lv_indev_get_type(d) == LV_INDEV_TYPE_POINTER) {
      g.indev = d;
      lv_indev_add_event_cb(d, on_indev_pressed, LV_EVENT_PRESSED, nullptr);
      lv_indev_add_event_cb(d, on_indev_released, LV_EVENT_RELEASED, nullptr);
      g.indev_registered = true;
      break;
    }
  }
}

static void swipe_step() {
  find_indev();
  if(!g.indev) return;
  if(lv_indev_get_state(g.indev) != LV_INDEV_STATE_PRESSED) {
    g.tracking = false;
    return;
  }
  if(!g.tracking || g.swallowed || g.press_on_slider) return;
  lv_point_t p;
  lv_indev_get_point(g.indev, &p);
  int dx = p.x - g.start.x;
  int dy = p.y - g.start.y;
  int ax = dx < 0 ? -dx : dx;
  int ay = dy < 0 ? -dy : dy;
  if(ax >= 48 && ax >= 2 * ay) {
    g.swallowed = true;
    lv_indev_wait_release(g.indev);
    go_to(dx < 0 ? (g.current + 1) % PAGE_COUNT :
                   (g.current + PAGE_COUNT - 1) % PAGE_COUNT);
  }
}

static void tour_step(uint32_t now) {
  if(g.tour_state == 0 ||
     (int32_t)(now - g.tour_deadline) < 0) return;
  if(g.tour_state == 1) {
    log_line("tour start");
    g.tour_state = 2;
    g.tour_page = 0;
    g.tour_samples = 0;
    g.tour_sum_us = 0;
    g.tour_max_us = 0;
    g.tour_max_page = 0;
    lv_obj_invalidate(g.page[0]);
    g.log_page = 0;
    g.refresh_soon = true;
    g.tour_deadline = now + 800U;
  } else if(g.tour_page < 8) {
    ++g.tour_page;
    go_to(g.tour_page);
    g.tour_deadline = now + 800U;
  } else {
    uint32_t max10 = (g.tour_max_us + 50U) / 100U;
    uint32_t avg = g.tour_samples ?
      (uint32_t)(g.tour_sum_us / g.tour_samples) : 0;
    uint32_t avg10 = (avg + 50U) / 100U;
    char line[96];
    snprintf(line, sizeof(line),
             "tour done: 9 pages, max %u.%u ms (%s), avg %u.%u ms",
             max10 / 10, max10 % 10,
             page_name(g.tour_max_page),
             avg10 / 10, avg10 % 10);
    log_line(line);
    g.tour_state = 0;
    go_to(0);
  }
}

static void expire_guard(Guard &q, bool garage, uint32_t now) {
  if(q.phase == PH_IDLE || !q.deadline ||
     (int32_t)(now - q.deadline) < 0) return;
  if(q.phase == PH_CONFIRM) {
    q.phase = PH_IDLE;
  } else {
    q.secured = !q.secured;
    q.phase = PH_IDLE;
  }
  q.deadline = 0;
  request_model_refresh();
  (void)garage;
}

static void step_100(uint32_t now) {
  if((uint32_t)(now - g.last_100) < 100U) return;
  g.last_100 = now;
  if(g.st.cover_moving) {
    int p = g.st.cover_pos;
    if(g.st.cover_target > p) {
      p += 2;
      if(p >= g.st.cover_target) {
        p = g.st.cover_target;
        g.st.cover_moving = false;
      }
    } else {
      p -= 2;
      if(p <= g.st.cover_target) {
        p = g.st.cover_target;
        g.st.cover_moving = false;
      }
    }
    g.st.cover_pos = (uint8_t)p;
    request_model_refresh();
  }
  if(g.st.thermo_pending_until &&
     (int32_t)(now - g.st.thermo_pending_until) >= 0) {
    g.st.thermo_pending_until = 0;
    request_model_refresh();
  }
  if(g.st.scene_pulse_page >= 0 &&
     (int32_t)(now - g.st.scene_pulse_until) >= 0) {
    g.st.scene_pulse_page = -1;
    g.st.scene_pulse_idx = -1;
    g.st.scene_pulse_until = 0;
    request_model_refresh();
  }
  expire_guard(g.st.lock_front, false, now);
  expire_guard(g.st.lock_back, false, now);
  expire_guard(g.st.garage, true, now);
  if(g.st.alarm_phase != PH_IDLE && g.st.alarm_deadline &&
     (int32_t)(now - g.st.alarm_deadline) >= 0) {
    if(g.st.alarm_phase == PH_CONFIRM) {
      g.st.alarm_phase = PH_IDLE;
    } else {
      g.st.alarm = g.st.alarm_next;
      g.st.alarm_phase = PH_IDLE;
    }
    g.st.alarm_deadline = 0;
    request_model_refresh();
  }
}

static uint32_t random_value() {
  g.seed = g.seed * 1103515245U + 12345U;
  return g.seed;
}

static int bounded_step(int v, int lo, int hi, int delta) {
  int n = v + delta;
  if(n < lo) n = lo;
  if(n > hi) n = hi;
  return n;
}

static bool leap_year(int y) {
  return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in_month(int y, int m) {
  static const uint8_t days[12] =
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if(m == 2 && leap_year(y)) return 29;
  return days[m >= 1 && m <= 12 ? m - 1 : 0];
}

static void add_minute() {
  if(++g.st.minute < 60) return;
  g.st.minute = 0;
  if(++g.st.hour < 24) return;
  g.st.hour = 0;
  g.st.weekday = g.st.weekday % 7 + 1;
  if(++g.st.day <= days_in_month(g.st.year, g.st.month)) return;
  g.st.day = 1;
  if(++g.st.month <= 12) return;
  g.st.month = 1;
  ++g.st.year;
}

static void simulation_step(uint32_t now) {
  if((uint32_t)(now - g.last_media) >= 1000U) {
    g.last_media = now;
    if(g.st.media_playing) {
      ++g.st.media_elapsed;
      if(g.st.media_elapsed >= TRACKS[g.st.media_track].seconds) {
        g.st.media_track = (uint8_t)((g.st.media_track + 1) % 4);
        g.st.media_elapsed = 0;
      }
      request_model_refresh();
    }
  }
  if((uint32_t)(now - g.last_thermo) >= 4000U) {
    g.last_thermo = now;
    if(g.st.thermo_mode != 3) {
      if(g.st.thermo_current < g.st.thermo_target)
        ++g.st.thermo_current;
      else if(g.st.thermo_current > g.st.thermo_target)
        --g.st.thermo_current;
      g.st.room_temp[RM_LIVING] = g.st.thermo_current;
      request_model_refresh();
    }
  }
  if((uint32_t)(now - g.last_dish) >= 3000U) {
    g.last_dish = now;
    if(g.st.dish_state == 0 && g.st.dish_left) {
      --g.st.dish_left;
      if(!g.st.dish_left) g.st.dish_state = 2;
      request_model_refresh();
    }
  }
  if((uint32_t)(now - g.last_sensor) >= 5000U) {
    g.last_sensor = now;
    g.st.hum_living = (uint8_t)bounded_step(
      g.st.hum_living, 40, 60, (int)(random_value() % 3) - 1);
    if(g.st.hum_living < g.st.hum_living_min)
      g.st.hum_living_min = g.st.hum_living;
    if(g.st.hum_living > g.st.hum_living_max)
      g.st.hum_living_max = g.st.hum_living;
    g.st.hum_bedroom = (uint8_t)bounded_step(
      g.st.hum_bedroom, 40, 60, (int)(random_value() % 3) - 1);
    g.st.co2 = (uint16_t)bounded_step(
      g.st.co2, 450, 900, (int)(random_value() % 31) - 15);
    g.st.power_x10 = (uint8_t)bounded_step(
      g.st.power_x10, 5, 35, (int)(random_value() % 3) - 1);
    g.st.solar_x10 = (uint8_t)bounded_step(
      g.st.solar_x10, 0, 40, (int)(random_value() % 3) - 1);
    request_model_refresh();
  }
  if((uint32_t)(now - g.last_history) >= 10000U) {
    g.last_history = now;
    for(int i = 0; i < 23; ++i) {
      g.st.temp_hist[i] = g.st.temp_hist[i + 1];
      g.st.bed_temp_hist[i] = g.st.bed_temp_hist[i + 1];
    }
    g.st.temp_hist[23] = g.st.thermo_current;
    g.st.bed_temp_hist[23] = g.st.room_temp[RM_BEDROOM];
    if(g.st.hour >= 0 && g.st.hour < 24)
      g.st.power_hist[g.st.hour] = g.st.power_x10;
    ++g.st.chart_version;
    request_model_refresh();
  }
  if((uint32_t)(now - g.st.minute_started_ms) >= 60000U) {
    g.st.minute_started_ms += 60000U;
    add_minute();
    request_model_refresh();
  }
}

static void timer_cb(lv_timer_t *) {
  tick(lv_tick_get());
}

void build(lv_obj_t *parent, const Assets &assets) {
  g = {};
  g.assets = assets;
  g.current = 0;
  g.log_page = -1;
  g.seed = 0x41C64E6DU;
  uint32_t now = lv_tick_get();

  init_font_styles(assets);
  for(int i = 0; i < PAGE_COUNT; ++i) {
    init_styles(g.page_styles[i]);
    bool baked = i == PG_CLOCK && assets.wallpaper != nullptr;
    fill_styles(g.page_styles[i], PALETTES[DEFAULT_THEME[i]], baked);
  }
  for(int i = 0; i < THEME_COUNT; ++i) {
    init_styles(g.swatch_styles[i]);
    fill_styles(g.swatch_styles[i], PALETTES[i], false);
  }
  init_state(now);

  g.root = lv_obj_create(parent);
  lv_obj_remove_style_all(g.root);
  lv_obj_set_pos(g.root, 0, 0);
  lv_obj_set_size(g.root, 1280, 800);
  lv_obj_add_flag(g.root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(g.root, LV_OBJ_FLAG_SCROLLABLE);

  for(int i = 0; i < PAGE_COUNT; ++i) {
    g.page[i] = lv_obj_create(g.root);
    lv_obj_remove_style_all(g.page[i]);
    lv_obj_set_pos(g.page[i], 0, 0);
    lv_obj_set_size(g.page[i], 1280, 800);
    lv_obj_add_style(g.page[i], &g.page_styles[i].page, 0);
    lv_obj_add_flag(g.page[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g.page[i], LV_OBJ_FLAG_SCROLLABLE);
    if(i != 0) lv_obj_add_flag(g.page[i], LV_OBJ_FLAG_HIDDEN);
  }

  build_home_page();
  build_living_page();
  build_kitchen_page();
  build_bedroom_page();
  build_climate_page();
  build_media_page();
  build_security_page();
  build_styles_page();
  build_clock_page();

  for(int i = 0; i < PAGE_COUNT; ++i) build_header(i);
  for(int i = 0; i < PAGE_COUNT; ++i) refresh_page_internal(i);

  g.disp = lv_obj_get_display(g.root);
  if(g.disp) {
    lv_display_add_event_cb(g.disp, on_render,
                            LV_EVENT_RENDER_START, nullptr);
    lv_display_add_event_cb(g.disp, on_render,
                            LV_EVENT_RENDER_READY, nullptr);
  }
  find_indev();
  g.last_100 = now;
  g.last_media = now;
  g.last_thermo = now;
  g.last_dish = now;
  g.last_sensor = now;
  g.last_history = now;
  lv_timer_create(timer_cb, 5, nullptr);
  g.dirty = false;
  g.refresh_soon = true;
  start_tour();
}

void tick(uint32_t now_ms) {
  swipe_step();
  tour_step(now_ms);
  step_100(now_ms);
  simulation_step(now_ms);

  if(g.dirty) {
    g.dirty = false;
    refresh_page_internal(g.current);
    g.refresh_soon = true;
  }
  if(g.refresh_soon) {
    g.refresh_soon = false;
    if(g.disp) {
      lv_timer_t *t = lv_display_get_refr_timer(g.disp);
      if(t) lv_timer_ready(t);
    }
  }
}

void set_clock(int year, int month, int day, int weekday,
               int hour, int minute, int second) {
  g.st.year = year;
  g.st.month = month;
  g.st.day = day;
  g.st.weekday = weekday;
  g.st.hour = hour;
  g.st.minute = minute;
  g.st.minute_started_ms = lv_tick_get() - (uint32_t)second * 1000U;
  g.st.clock_from_host = true;
  request_model_refresh();
}

int page_count() {
  return PAGE_COUNT;
}

int current_page() {
  return g.current;
}

// Applies rewritten style values to one page. Hidden pages wait until they are
// next shown, because refreshing every page at once stalls the main loop.
static void flush_style(int page) {
  if(page < 0 || page >= PAGE_COUNT || !g.style_stale[page]) return;
  g.style_stale[page] = false;
  lv_obj_refresh_style(g.page[page], LV_PART_ANY, LV_STYLE_PROP_ANY);
}

void go_to(int page) {
  if(page < 0 || page >= PAGE_COUNT || page == g.current) return;
  flush_style(page);
  refresh_page_internal(page);
  int old = g.current;
  lv_obj_add_flag(g.page[old], LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(g.page[page], LV_OBJ_FLAG_HIDDEN);
  g.current = page;
  g.log_page = page;
  g.refresh_soon = true;
}

const char *page_name(int page) {
  return page >= 0 && page < PAGE_COUNT ? PAGE_NAMES[page] : "";
}

const char *theme_name(int theme) {
  return theme >= 0 && theme < THEME_COUNT ? THEME_NAMES[theme] : "";
}

int page_theme(int page) {
  if(page < 0 || page >= PAGE_COUNT) return -1;
  if(page == PG_CLOCK) return TH_GLASS;
  return g.st.style_override >= 0 ?
         g.st.style_override : DEFAULT_THEME[page];
}

void set_style_override(int theme) {
  if(theme != -1 && (theme < 0 || theme > TH_MONO)) return;
  g.st.style_override = (int8_t)theme;
  for(int i = 0; i < PAGE_COUNT; ++i) {
    if(i == PG_CLOCK) continue;
    int t = theme >= 0 ? theme : DEFAULT_THEME[i];
    fill_styles(g.page_styles[i], PALETTES[t], false);
    g.style_stale[i] = true;
  }
  flush_style(g.current);
  request_model_refresh();
}

int style_override() {
  return g.st.style_override;
}

void start_tour() {
  if(g.current != 0) go_to(0);
  g.tour_state = 1;
  g.tour_page = 0;
  g.tour_samples = 0;
  g.tour_sum_us = 0;
  g.tour_max_us = 0;
  g.tour_max_page = 0;
  g.tour_deadline = lv_tick_get() + 1500U;
}

void cancel_tour() {
  if(g.tour_state != 0) {
    g.tour_state = 0;
    log_line("tour cancelled");
  }
}

bool tour_active() {
  return g.tour_state != 0;
}

uint32_t last_render_us() {
  return g.last_render;
}

State &state() {
  return g.st;
}

void mark_dirty() {
  request_model_refresh();
}

lv_obj_t *page_obj(int page) {
  return page >= 0 && page < PAGE_COUNT ? g.page[page] : nullptr;
}

}  // namespace showcase
