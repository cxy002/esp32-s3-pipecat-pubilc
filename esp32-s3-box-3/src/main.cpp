#include "main.h"

#include "board_config.h"

#include <esp_event.h>
#include <esp_log.h>

#ifndef LINUX_BUILD
#include "nvs_flash.h"

extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  pipecat_init_screen();

#if ENABLE_SDCARD_TEST
  if (pipecat_init_sdcard()) {
#if ENABLE_AUDIO_STORAGE_TEST
    ESP_LOGI("main", "Starting audio storage test");
    pipecat_screen_system_log("Audio store start\n");
    pipecat_init_audio_storage();
#endif
    pipecat_init_hardware_audio();
    pipecat_init_song_player();
  }
#endif

#if ENABLE_VAD_SERIAL_TEST
  pipecat_init_vad_serial_test();
#else
  pipecat_init_cellular();

  pipecat_screen_system_log("4G init\n");
#endif

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}
#else
int main(void) {
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}
#endif
