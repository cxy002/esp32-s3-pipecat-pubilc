#include "main.h"

#include "board_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

namespace {
const char *TAG = "sdcard";

sdmmc_card_t *g_card = nullptr;

esp_err_t write_test_file() {
  const char *path = SDCARD_MOUNT_POINT "/test.txt";
  FILE *f = fopen(path, "w");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open %s for writing", path);
    return ESP_FAIL;
  }
  fprintf(f, "ESP32 SDMMC 1-bit test OK\n");
  fclose(f);

  f = fopen(path, "r");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open %s for reading", path);
    return ESP_FAIL;
  }
  char line[64] = {};
  fgets(line, sizeof(line), f);
  fclose(f);
  ESP_LOGI(TAG, "Read back: %s", line);
  return strncmp(line, "ESP32 SDMMC 1-bit test OK", 25) == 0 ? ESP_OK : ESP_FAIL;
}
}  // namespace

bool pipecat_init_sdcard() {
  if (g_card) {
    return true;
  }

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024,
  };

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 4;
  slot_config.clk = SDCARD_CLK_PIN;
  slot_config.cmd = SDCARD_CMD_PIN;
  slot_config.d0 = SDCARD_D0_PIN;
  slot_config.d1 = SDCARD_D1_PIN;
  slot_config.d2 = SDCARD_D2_PIN;
  slot_config.d3 = SDCARD_D3_PIN;
  slot_config.gpio_cd = GPIO_NUM_NC;
  slot_config.gpio_wp = GPIO_NUM_NC;
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  ESP_LOGI(TAG, "Mounting SD card 4-bit: CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d",
           SDCARD_CLK_PIN, SDCARD_CMD_PIN, SDCARD_D0_PIN,
           SDCARD_D1_PIN, SDCARD_D2_PIN, SDCARD_D3_PIN);
  esp_err_t ret = esp_vfs_fat_sdmmc_mount(SDCARD_MOUNT_POINT, &host, &slot_config,
                                          &mount_config, &g_card);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
    pipecat_screen_system_log("SD mount failed\n");
    return false;
  }

  sdmmc_card_print_info(stdout, g_card);
  if (write_test_file() != ESP_OK) {
    ESP_LOGE(TAG, "SD read/write test failed");
    pipecat_screen_system_log("SD test failed\n");
    return false;
  }

  ESP_LOGI(TAG, "SD read/write test passed");
  ESP_LOGI(TAG, "SD mounted at %s", SDCARD_MOUNT_POINT);
  pipecat_screen_system_log("SD OK 4-bit\n");
  return true;
}
