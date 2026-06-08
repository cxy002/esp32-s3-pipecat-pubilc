#include "main.h"

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
  pipecat_init_cellular();

  pipecat_screen_system_log("4G init\n");

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
