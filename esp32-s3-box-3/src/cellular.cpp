#include "main.h"

#include "board_config.h"

#include <memory>
#include <string>

#include "at_modem.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static std::unique_ptr<AtModem> s_modem;
static bool s_cellular_ready = false;

static void cellular_task(void *pv) {
  (void)pv;

  vTaskDelay(pdMS_TO_TICKS(8000));
  pipecat_screen_system_log("4G detecting\n");
  ESP_LOGI(LOG_TAG, "Detecting 4G modem on TX=%d RX=%d", ML307_TX_PIN,
           ML307_RX_PIN);

  while (!s_modem) {
    s_modem = AtModem::Detect(ML307_TX_PIN, ML307_RX_PIN, ML307_DTR_PIN,
                              921600, 10000);

    if (!s_modem) {
      ESP_LOGW(LOG_TAG, "4G modem not detected, retrying");
      pipecat_screen_system_log("4G retry\n");
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }

  ESP_LOGI(LOG_TAG, "4G modem detected");
  pipecat_screen_system_log("4G modem OK\n");
  auto at = s_modem->GetAtUart();
  at->SendCommand("AT+MLPMCFG=\"sleepmode\",0,0", 3000);
  at->SendCommand("AT+CFUN=0", 5000);
  vTaskDelay(pdMS_TO_TICKS(1000));
  at->SendCommand("AT+CFUN=1", 5000);
  pipecat_screen_system_log("4G radio on\n");
  vTaskDelay(pdMS_TO_TICKS(5000));

  ESP_LOGI(LOG_TAG, "4G IMEI: %s", s_modem->GetImei().c_str());
  ESP_LOGI(LOG_TAG, "4G ICCID: %s", s_modem->GetIccid().c_str());

  s_modem->OnNetworkStateChanged([](bool ready) {
    s_cellular_ready = ready;
    pipecat_screen_system_log(ready ? "4G connected\n" : "4G lost\n");
    ESP_LOGI(LOG_TAG, "4G network state: %s", ready ? "ready" : "lost");
  });

  while (!s_cellular_ready) {
    pipecat_screen_system_log("4G registering\n");
    NetworkStatus status = s_modem->WaitForNetworkReady(15000);
    if (status == NetworkStatus::Ready) {
      s_cellular_ready = true;
      break;
    }

    int csq = s_modem->GetCsq();
    CeregState cereg = s_modem->GetRegistrationState();
    ESP_LOGW(LOG_TAG, "4G registration failed: %d, csq=%d, cereg=%s",
             static_cast<int>(status), csq, cereg.ToString().c_str());
    pipecat_screen_system_log("4G reg fail\n");
    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  std::string revision = s_modem->GetModuleRevision();
  std::string imei = s_modem->GetImei();
  std::string iccid = s_modem->GetIccid();
  int csq = s_modem->GetCsq();

  ESP_LOGI(LOG_TAG, "4G connected");
  ESP_LOGI(LOG_TAG, "4G Revision: %s", revision.c_str());
  ESP_LOGI(LOG_TAG, "4G IMEI: %s", imei.c_str());
  ESP_LOGI(LOG_TAG, "4G ICCID: %s", iccid.c_str());
  ESP_LOGI(LOG_TAG, "4G CSQ: %d", csq);

  char line[32];
  snprintf(line, sizeof(line), "4G CSQ %d\n", csq);
  pipecat_screen_system_log(line);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

void pipecat_init_cellular() {
  xTaskCreate(cellular_task, "cellular", 6144, nullptr, 5, nullptr);
}

bool pipecat_cellular_ready() {
  return s_cellular_ready;
}
