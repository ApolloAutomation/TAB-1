#pragma once
// Stand-ins for what fast_display.h takes from ESPHome, ESP-IDF, FreeRTOS and
// LVGL, so the whole file builds and runs on a computer: flush_cb, setup and
// set_enabled too, not only the rotation. Each header fast_display.h includes
// has a one-line file of the same name in this folder that includes this one.
// Only tests/test_flush_cb.cpp builds with this folder.
//
// The names, argument orders and enum values are those of ESPHome 2026.10.2,
// ESP-IDF 5.5 and LVGL 9.5.0. Each type holds just what fast_display.h reads.
// The functions that are only declared here are the test's: it records every
// call the flush makes. Look at them again when the pinned ESPHome version
// changes.
#include <cstdarg>
#include <cstddef>
#include <cstdint>

// ---- esphome/core/log.h
void sim_log(char level, const char *tag, const char *format, ...) __attribute__((format(printf, 3, 4)));
#define ESP_LOGW(tag, ...) sim_log('W', tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) sim_log('I', tag, __VA_ARGS__)

// ---- ESP-IDF and FreeRTOS
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
struct esp_lcd_panel_t;
typedef struct esp_lcd_panel_t *esp_lcd_panel_handle_t;
struct QueueDefinition;
typedef struct QueueDefinition *SemaphoreHandle_t;
typedef uint32_t TickType_t;
#define portMAX_DELAY ((TickType_t) 0xffffffffUL)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_INTERNAL (1 << 11)
void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps);
// Starts a copy of x_start..x_end-1, y_start..y_end-1 and returns. The panel
// gives its semaphore when the copy is done.
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x_start, int y_start, int x_end, int y_end,
                                    const void *color_data);
int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);

// ---- lvgl.h and src/display/lv_display_private.h
struct lv_area_t {
  int32_t x1, y1, x2, y2;
};
static inline int32_t lv_area_get_width(const lv_area_t *area) { return area->x2 - area->x1 + 1; }
static inline int32_t lv_area_get_height(const lv_area_t *area) { return area->y2 - area->y1 + 1; }

struct lv_display_t;
typedef void (*lv_display_flush_cb_t)(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
struct lv_display_t {
  int32_t hor_res, ver_res;
  lv_display_flush_cb_t flush_cb;
  void *user_data;
};
lv_display_t *lv_display_get_default();
void lv_display_flush_ready(lv_display_t *disp);
static inline void *lv_display_get_user_data(lv_display_t *disp) { return disp->user_data; }
static inline int32_t lv_display_get_horizontal_resolution(const lv_display_t *disp) { return disp->hor_res; }
static inline int32_t lv_display_get_vertical_resolution(const lv_display_t *disp) { return disp->ver_res; }
static inline void lv_display_set_flush_cb(lv_display_t *disp, lv_display_flush_cb_t flush_cb) {
  disp->flush_cb = flush_cb;
}

enum lv_indev_type_t {
  LV_INDEV_TYPE_NONE,
  LV_INDEV_TYPE_POINTER,
  LV_INDEV_TYPE_KEYPAD,
  LV_INDEV_TYPE_BUTTON,
  LV_INDEV_TYPE_ENCODER,
};
struct lv_timer_t {
  uint32_t period;
};
struct lv_indev_t {
  lv_indev_type_t type;
  uint8_t gesture_min_distance;
  lv_timer_t *read_timer;  // null when the device is not polled
  lv_indev_t *next;
};
// The device after `indev`; the first one for null.
lv_indev_t *lv_indev_get_next(lv_indev_t *indev);
static inline lv_indev_type_t lv_indev_get_type(const lv_indev_t *indev) { return indev->type; }
static inline void lv_indev_set_gesture_min_distance(lv_indev_t *indev, uint8_t min_distance) {
  indev->gesture_min_distance = min_distance;
}
static inline lv_timer_t *lv_indev_get_read_timer(lv_indev_t *indev) { return indev->read_timer; }
static inline void lv_timer_set_period(lv_timer_t *timer, uint32_t period) { timer->period = period; }

namespace esphome {

// ---- esphome/components/display/display.h
namespace display {
enum DisplayRotation {
  DISPLAY_ROTATION_0_DEGREES = 0,
  DISPLAY_ROTATION_90_DEGREES = 90,
  DISPLAY_ROTATION_180_DEGREES = 180,
  DISPLAY_ROTATION_270_DEGREES = 270,
};
enum ColorOrder : uint8_t { COLOR_ORDER_RGB = 0, COLOR_ORDER_BGR = 1, COLOR_ORDER_GRB = 2 };
enum ColorBitness : uint8_t { COLOR_BITNESS_888 = 0, COLOR_BITNESS_565 = 1, COLOR_BITNESS_332 = 2 };
}  // namespace display

// ---- esphome/components/lvgl/lvgl_esphome.h
namespace lvgl {
static const display::ColorBitness LV_BITNESS = display::ColorBitness::COLOR_BITNESS_565;  // LV_COLOR_DEPTH 16
class LvglComponent final {
 public:
  bool is_paused() const { return this->paused_; }
  size_t draw_rounding{2};
  display::DisplayRotation get_rotation() const { return this->rotation_; }

  // For the test: what set_rotation() and pause() leave behind.
  void sim_set(display::DisplayRotation rotation, bool paused) {
    this->rotation_ = rotation;
    this->paused_ = paused;
  }

 protected:
  bool paused_{};
  display::DisplayRotation rotation_{display::DISPLAY_ROTATION_0_DEGREES};
};
}  // namespace lvgl

// ---- esphome/components/mipi_dsi/mipi_dsi.h
namespace mipi_dsi {
class MipiDsi final {
 public:
  MipiDsi(size_t width, size_t height, display::ColorBitness color_depth)
      : width_(width), height_(height), color_depth_(color_depth) {}
  // Returns when the pixels are on the panel.
  void draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                      display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad);
  int get_native_width() { return (int) this->width_; }
  int get_native_height() { return (int) this->height_; }

  // For the test: the two handles the real setup() makes.
  void sim_set(esp_lcd_panel_handle_t handle, SemaphoreHandle_t io_lock) {
    this->handle_ = handle;
    this->io_lock_ = io_lock;
  }

 protected:
  size_t width_{};
  size_t height_{};
  display::ColorBitness color_depth_;
  esp_lcd_panel_handle_t handle_{};
  SemaphoreHandle_t io_lock_{};
};
}  // namespace mipi_dsi

}  // namespace esphome
