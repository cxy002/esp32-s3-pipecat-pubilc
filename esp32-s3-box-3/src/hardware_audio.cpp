#include "main.h"

#include "board_config.h"

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <vector>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2s_chan_handle_t s_tx_handle = nullptr;
static i2s_chan_handle_t s_rx_handle = nullptr;

static int write_speaker(const int16_t *data, int samples) {
  if (!s_tx_handle) {
    return 0;
  }

  std::vector<int32_t> tx(samples);
  for (int i = 0; i < samples; ++i) {
    tx[i] = static_cast<int32_t>(data[i]) << 14;
  }

  size_t bytes_written = 0;
  esp_err_t err = i2s_channel_write(s_tx_handle, tx.data(),
                                    tx.size() * sizeof(int32_t),
                                    &bytes_written, pdMS_TO_TICKS(500));
  if (err != ESP_OK) {
    ESP_LOGW(LOG_TAG, "I2S speaker write failed: %s", esp_err_to_name(err));
    return 0;
  }
  return bytes_written / sizeof(int32_t);
}

static int read_mic(int16_t *dest, int samples) {
  if (!s_rx_handle) {
    return 0;
  }

  std::vector<int32_t> rx(samples);
  size_t bytes_read = 0;
  esp_err_t err = i2s_channel_read(s_rx_handle, rx.data(),
                                   rx.size() * sizeof(int32_t), &bytes_read,
                                   pdMS_TO_TICKS(200));
  if (err != ESP_OK) {
    return 0;
  }

  int got = bytes_read / sizeof(int32_t);
  for (int i = 0; i < got; ++i) {
    int32_t value = rx[i] >> 12;
    if (value > INT16_MAX) {
      value = INT16_MAX;
    } else if (value < INT16_MIN) {
      value = INT16_MIN;
    }
    dest[i] = static_cast<int16_t>(value);
  }
  return got;
}

static void play_beep() {
  constexpr int kDurationMs = 260;
  constexpr int kSamples = AUDIO_OUTPUT_SAMPLE_RATE * kDurationMs / 1000;
  constexpr float kFreq = 880.0f;
  std::vector<int16_t> tone(kSamples);

  for (int i = 0; i < kSamples; ++i) {
    float t = static_cast<float>(i) / AUDIO_OUTPUT_SAMPLE_RATE;
    float envelope = 1.0f;
    if (i < 240) {
      envelope = static_cast<float>(i) / 240.0f;
    } else if (i > kSamples - 240) {
      envelope = static_cast<float>(kSamples - i) / 240.0f;
    }
    tone[i] = static_cast<int16_t>(sinf(2.0f * static_cast<float>(M_PI) *
                                        kFreq * t) *
                                   9000.0f * envelope);
  }

  write_speaker(tone.data(), tone.size());
}

static void audio_self_test_task(void *pv) {
  (void)pv;

  std::vector<int16_t> mic_frame(512);
  int64_t last_beep = 0;
  int64_t last_log = 0;

  ESP_LOGI(LOG_TAG, "Audio self-test started");
  pipecat_screen_system_log("Audio self-test\n");

  while (true) {
    int64_t now = xTaskGetTickCount();
    if ((now - last_beep) * portTICK_PERIOD_MS > 5000) {
      ESP_LOGI(LOG_TAG, "Speaker beep");
      pipecat_screen_system_log("BEEP OK\n");
      play_beep();
      last_beep = now;
    }

    int got = read_mic(mic_frame.data(), mic_frame.size());
    if (got > 0 && (now - last_log) * portTICK_PERIOD_MS > 1000) {
      int64_t sum = 0;
      int16_t peak = 0;
      for (int i = 0; i < got; ++i) {
        int32_t v = mic_frame[i];
        sum += static_cast<int64_t>(v) * v;
        int16_t abs_v = v < 0 ? -v : v;
        if (abs_v > peak) {
          peak = abs_v;
        }
      }
      int rms = static_cast<int>(sqrt(static_cast<double>(sum) / got));
      ESP_LOGI(LOG_TAG, "Mic samples=%d rms=%d peak=%d", got, rms, peak);

      char line[64];
      snprintf(line, sizeof(line), "Mic rms=%d peak=%d\n", rms, peak);
      pipecat_screen_system_log(line);
      last_log = now;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void pipecat_init_hardware_audio() {
  ESP_LOGI(LOG_TAG, "Initialize I2S speaker and microphone");

  i2s_chan_config_t tx_chan_cfg = {
      .id = I2S_NUM_0,
      .role = I2S_ROLE_MASTER,
      .dma_desc_num = 6,
      .dma_frame_num = 240,
      .auto_clear_after_cb = true,
      .auto_clear_before_cb = false,
      .intr_priority = 0,
  };
  ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &s_tx_handle, nullptr));

  i2s_std_config_t tx_std_cfg = {
      .clk_cfg = {
          .sample_rate_hz = AUDIO_OUTPUT_SAMPLE_RATE,
          .clk_src = I2S_CLK_SRC_DEFAULT,
          .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#ifdef I2S_HW_VERSION_2
          .ext_clk_freq_hz = 0,
#endif
      },
      .slot_cfg = {
          .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
          .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
          .slot_mode = I2S_SLOT_MODE_MONO,
          .slot_mask = I2S_STD_SLOT_LEFT,
          .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
          .ws_pol = false,
          .bit_shift = true,
#ifdef I2S_HW_VERSION_2
          .left_align = true,
          .big_endian = false,
          .bit_order_lsb = false,
#endif
      },
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = AUDIO_I2S_SPK_GPIO_BCLK,
          .ws = AUDIO_I2S_SPK_GPIO_LRCK,
          .dout = AUDIO_I2S_SPK_GPIO_DOUT,
          .din = I2S_GPIO_UNUSED,
          .invert_flags = {
              .mclk_inv = false,
              .bclk_inv = false,
              .ws_inv = false,
          },
      },
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_handle, &tx_std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(s_tx_handle));

  i2s_chan_config_t rx_chan_cfg = {
      .id = I2S_NUM_1,
      .role = I2S_ROLE_MASTER,
      .dma_desc_num = 6,
      .dma_frame_num = 240,
      .auto_clear_after_cb = true,
      .auto_clear_before_cb = false,
      .intr_priority = 0,
  };
  ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, nullptr, &s_rx_handle));

  i2s_std_config_t rx_std_cfg = tx_std_cfg;
  rx_std_cfg.clk_cfg.sample_rate_hz = AUDIO_INPUT_SAMPLE_RATE;
  rx_std_cfg.gpio_cfg.bclk = AUDIO_I2S_MIC_GPIO_SCK;
  rx_std_cfg.gpio_cfg.ws = AUDIO_I2S_MIC_GPIO_WS;
  rx_std_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
  rx_std_cfg.gpio_cfg.din = AUDIO_I2S_MIC_GPIO_DIN;
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_handle, &rx_std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(s_rx_handle));

  ESP_LOGI(LOG_TAG, "I2S audio initialized");
  xTaskCreate(audio_self_test_task, "audio_self_test", 4096, nullptr, 4, nullptr);
}
