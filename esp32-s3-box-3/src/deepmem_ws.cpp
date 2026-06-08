#include "main.h"

#include <errno.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef DEEPMEM_WS_HOST
#define DEEPMEM_WS_HOST "47.110.246.245"
#endif

#ifndef DEEPMEM_WS_PORT
#define DEEPMEM_WS_PORT 8443
#endif

#ifndef DEEPMEM_WS_PATH
#define DEEPMEM_WS_PATH "/esp32/ws/esp32-206d994a?token=IJwlqkSnj8yuDYQlDZTM76xl"
#endif

static void send_text(esp_transport_handle_t ws, const char *text) {
  int written = esp_transport_ws_send_raw(
      ws, WS_TRANSPORT_OPCODES_TEXT, text, strlen(text), 5000);
  if (written < 0) {
    ESP_LOGW(LOG_TAG, "WebSocket text send failed: errno=%d", errno);
  } else {
    ESP_LOGI(LOG_TAG, "> %s", text);
  }
}

static void send_pong(esp_transport_handle_t ws, const char *data, int len) {
  esp_transport_ws_send_raw(ws, WS_TRANSPORT_OPCODES_PONG, data, len, 5000);
}

static void deepmem_ws_task(void *pv) {
  (void)pv;

  while (true) {
    esp_transport_handle_t parent = esp_transport_ssl_init();
    esp_transport_handle_t ws = parent ? esp_transport_ws_init(parent) : nullptr;

    if (!parent || !ws) {
      ESP_LOGE(LOG_TAG, "Failed to create WSS transport");
      if (ws) {
        esp_transport_destroy(ws);
      }
      if (parent) {
        esp_transport_destroy(parent);
      }
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    esp_transport_ssl_crt_bundle_attach(parent, esp_crt_bundle_attach);
    esp_transport_ssl_skip_common_name_check(parent);
    static const char *alpn_protos[] = {"http/1.1", nullptr};
    esp_transport_ssl_set_alpn_protocol(parent, alpn_protos);
    esp_transport_ssl_set_tls_version(parent, ESP_TLS_VER_TLS_1_3);

    esp_transport_ws_config_t ws_cfg = {
        .ws_path = DEEPMEM_WS_PATH,
        .user_agent = "pipecat-esp32-deepmem/0.1",
        .propagate_control_frames = true,
    };
    ESP_ERROR_CHECK(esp_transport_ws_set_config(ws, &ws_cfg));

    ESP_LOGI(LOG_TAG, "Connecting WSS %s:%d%s", DEEPMEM_WS_HOST,
             DEEPMEM_WS_PORT, DEEPMEM_WS_PATH);
    pipecat_screen_system_log("Deepmem WSS connecting\n");

    int err = esp_transport_connect(ws, DEEPMEM_WS_HOST, DEEPMEM_WS_PORT, 15000);
    if (err != 0) {
      ESP_LOGE(LOG_TAG, "Deepmem WSS connect failed: http_status=%d errno=%d",
               esp_transport_ws_get_upgrade_request_status(ws), errno);
      pipecat_screen_system_log("Deepmem WSS failed\n");
      esp_transport_destroy(ws);
      esp_transport_destroy(parent);
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    ESP_LOGI(LOG_TAG, "Deepmem WSS connected, HTTP status=%d",
             esp_transport_ws_get_upgrade_request_status(ws));
    pipecat_screen_system_log("Deepmem WSS connected\n");
    send_text(ws,
              "{\"type\":\"status\",\"protocol\":\"esp32-audio-v1\","
              "\"firmware\":\"pipecat-esp32-deepmem-0.1\","
              "\"audio\":\"not-wired-yet\"}");

    char rx[2048];
    while (true) {
      int len = esp_transport_read(ws, rx, sizeof(rx) - 1, 1000);
      if (len < 0) {
        ESP_LOGE(LOG_TAG, "Deepmem WSS read failed: errno=%d", errno);
        break;
      }

      if (len == 0) {
        continue;
      }

      ws_transport_opcodes_t opcode = esp_transport_ws_get_read_opcode(ws);
      if (opcode == WS_TRANSPORT_OPCODES_TEXT) {
        rx[len] = '\0';
        ESP_LOGI(LOG_TAG, "< %s", rx);
      } else if (opcode == WS_TRANSPORT_OPCODES_BINARY) {
        ESP_LOGI(LOG_TAG, "< binary %d bytes", len);
      } else if (opcode == WS_TRANSPORT_OPCODES_PING) {
        send_pong(ws, rx, len);
      } else if (opcode == WS_TRANSPORT_OPCODES_CLOSE) {
        ESP_LOGW(LOG_TAG, "Deepmem WSS close frame received");
        break;
      } else {
        ESP_LOGI(LOG_TAG, "< opcode=%d len=%d", opcode, len);
      }
    }

    esp_transport_close(ws);
    esp_transport_destroy(ws);
    esp_transport_destroy(parent);
    pipecat_screen_system_log("Deepmem WSS reconnecting\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

void pipecat_init_deepmem_ws() {
  xTaskCreate(deepmem_ws_task, "deepmem_ws", 8192, nullptr, 5, nullptr);
}
