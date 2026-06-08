#include "board_config.h"

#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include <lvgl.h>
#include <string.h>

#include "main.h"

static lv_obj_t *s_title_label = nullptr;
static lv_obj_t *s_status_label = nullptr;
static lv_obj_t *s_mic_label = nullptr;
static lv_obj_t *s_mic_bar = nullptr;

static constexpr int kScreenWidth =
    DISPLAY_SWAP_XY ? DISPLAY_HEIGHT : DISPLAY_WIDTH;
static constexpr int kScreenHeight =
    DISPLAY_SWAP_XY ? DISPLAY_WIDTH : DISPLAY_HEIGHT;

static bool is_mic_line(const char *text) {
  return strncmp(text, "Mic ", 4) == 0;
}

static bool is_speaker_line(const char *text) {
  return strncmp(text, "Speaker", 7) == 0 || strncmp(text, "BEEP", 4) == 0;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font,
                            lv_align_t align, int y) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_align(label, align, 0, y);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  return label;
}

static lv_obj_t *make_bar(lv_obj_t *parent) {
  lv_obj_t *bar = lv_bar_create(parent);
  lv_obj_set_size(bar, 108, 8);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_color(bar, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
  return bar;
}

void pipecat_init_screen() {
  ESP_LOGI(LOG_TAG, "Initialize SSD1306 OLED");

  i2c_master_bus_handle_t i2c_bus = nullptr;
  i2c_master_bus_config_t bus_config = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = DISPLAY_SDA_PIN,
      .scl_io_num = DISPLAY_SCL_PIN,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .intr_priority = 0,
      .trans_queue_depth = 0,
      .flags = {.enable_internal_pullup = 1},
  };
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

  esp_lcd_panel_io_handle_t io_handle = nullptr;
  esp_lcd_panel_io_i2c_config_t io_config = {
      .dev_addr = 0x3C,
      .on_color_trans_done = nullptr,
      .user_ctx = nullptr,
      .control_phase_bytes = 1,
      .dc_bit_offset = 6,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .flags = {.dc_low_on_data = 0, .disable_control_phase = 0},
      .scl_speed_hz = 400 * 1000,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(i2c_bus, &io_config, &io_handle));

  esp_lcd_panel_handle_t panel_handle = nullptr;
  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = -1,
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
      .color_space = ESP_LCD_COLOR_SPACE_MONOCHROME,
#endif
      .bits_per_pixel = 1,
  };
  esp_lcd_panel_ssd1306_config_t ssd1306_config = {
      .height = DISPLAY_HEIGHT,
  };
  panel_config.vendor_config = &ssd1306_config;

  ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

  const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

  const lvgl_port_display_cfg_t disp_cfg = {
      .io_handle = io_handle,
      .panel_handle = panel_handle,
      .buffer_size = DISPLAY_WIDTH * DISPLAY_HEIGHT,
      .double_buffer = false,
      .hres = kScreenWidth,
      .vres = kScreenHeight,
      .monochrome = true,
      .rotation = {
          .swap_xy = DISPLAY_SWAP_XY,
          .mirror_x = DISPLAY_MIRROR_X,
          .mirror_y = DISPLAY_MIRROR_Y,
      },
#if LVGL_VERSION_MAJOR >= 9
      .color_format = LV_COLOR_FORMAT_RGB565,
#endif
      .flags = {
          .buff_dma = false,
          .buff_spiram = false,
          .sw_rotate = false,
#if LVGL_VERSION_MAJOR >= 9
          .swap_bytes = false,
#endif
          .full_refresh = false,
          .direct_mode = false,
      },
  };
  lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);

  if (lvgl_port_lock(0)) {
    lv_disp_set_rotation(disp, LV_DISPLAY_ROTATION_0);
    lv_obj_t *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    s_title_label = make_label(root, &lv_font_montserrat_14, LV_ALIGN_TOP_MID, 0);
    s_status_label = make_label(root, &lv_font_montserrat_14, LV_ALIGN_TOP_MID, 16);
    s_mic_label = make_label(root, &lv_font_montserrat_14, LV_ALIGN_TOP_MID, 33);
    s_mic_bar = make_bar(root);

    lv_label_set_text(s_title_label, "PIPECAT");
    lv_label_set_text(s_status_label, "OLED OK");
    lv_label_set_text(s_mic_label, "MIC WAIT");
    lvgl_port_unlock();
  }
}

void pipecat_screen_system_log(const char *text) {
  if (!s_status_label) {
    ESP_LOGI(LOG_TAG, "%s", text);
    return;
  }

  char line[48];
  strlcpy(line, text, sizeof(line));
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
    line[--len] = '\0';
  }

  if (lvgl_port_lock(0)) {
    if (is_mic_line(line)) {
      int rms = 0;
      int peak = 0;
      sscanf(line, "Mic rms=%d peak=%d", &rms, &peak);
      char mic_line[24];
      snprintf(mic_line, sizeof(mic_line), "MIC %d", rms);
      lv_label_set_text(s_mic_label, mic_line);
      if (s_mic_bar) {
        int value = rms / 20;
        if (value > 100) {
          value = 100;
        }
        lv_bar_set_value(s_mic_bar, value, LV_ANIM_OFF);
      }
    } else if (is_speaker_line(line)) {
      lv_label_set_text(s_status_label, "BEEP OK");
    } else {
      lv_label_set_text(s_status_label, line);
    }
    lvgl_port_unlock();
  }
}

void pipecat_screen_new_log() {
}

void pipecat_screen_log(const char *text) {
  pipecat_screen_system_log(text);
}
