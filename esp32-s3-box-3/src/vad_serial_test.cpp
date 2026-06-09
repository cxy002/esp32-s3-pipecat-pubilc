#include "main.h"

#include "board_config.h"
#include "vad.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr uart_port_t kPort = UART_NUM_0;
constexpr int kFrameSamples = 320;
constexpr int kFrameBytes = kFrameSamples * sizeof(int16_t);
constexpr uint8_t kMagic[4] = {'V', 'A', 'D', 'F'};

bool read_exact(uint8_t *data, size_t len) {
  size_t got = 0;
  while (got < len) {
    int ret = uart_read_bytes(kPort, data + got, len - got, pdMS_TO_TICKS(1000));
    if (ret < 0) {
      return false;
    }
    if (ret == 0) {
      continue;
    }
    got += ret;
  }
  return true;
}

uint32_t read_le32(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

bool wait_magic() {
  uint8_t window[4] = {};
  size_t count = 0;
  while (true) {
    uint8_t ch = 0;
    int ret = uart_read_bytes(kPort, &ch, 1, pdMS_TO_TICKS(1000));
    if (ret <= 0) {
      continue;
    }
    if (count < sizeof(window)) {
      window[count++] = ch;
    } else {
      memmove(window, window + 1, sizeof(window) - 1);
      window[sizeof(window) - 1] = ch;
    }
    if (count == sizeof(window) && memcmp(window, kMagic, sizeof(kMagic)) == 0) {
      return true;
    }
  }
}

void vad_serial_task(void *arg) {
  (void)arg;

  int16_t frame[kFrameSamples];
  uint8_t frame_id_bytes[4];
  char line[96];

  uart_write_bytes(kPort, "VAD_READY\n", strlen("VAD_READY\n"));

  while (true) {
    wait_magic();
    if (!read_exact(frame_id_bytes, sizeof(frame_id_bytes))) {
      continue;
    }
    if (!read_exact(reinterpret_cast<uint8_t *>(frame), kFrameBytes)) {
      continue;
    }

    uint32_t frame_id = read_le32(frame_id_bytes);
    PipecatVadResult result = pipecat_vad_process_20ms(frame, kFrameSamples);

    int len = snprintf(line, sizeof(line), "VADR,%lu,%u,%lu,%lu,%u,%u\n",
                       static_cast<unsigned long>(frame_id), result.speech,
                       static_cast<unsigned long>(result.inference_us),
                       static_cast<unsigned long>(result.rms), result.zcr,
                       result.peak);
    uart_write_bytes(kPort, line, len);
  }
}
}  // namespace

void pipecat_init_vad_serial_test() {
  esp_log_level_set("*", ESP_LOG_NONE);

  uart_driver_delete(kPort);

  uart_config_t uart_config = {
      .baud_rate = VAD_SERIAL_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 0,
      .source_clk = UART_SCLK_DEFAULT,
      .flags = {
          .allow_pd = false,
      },
  };
  ESP_ERROR_CHECK(uart_param_config(kPort, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(kPort, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(kPort, 8192, 2048, 0, nullptr, 0));

  xTaskCreate(vad_serial_task, "vad_serial", 4096, nullptr, 10, nullptr);
}
