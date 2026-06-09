#include "vad.h"

#include <math.h>
#include <stdint.h>

#include "esp_timer.h"
#include "esp_vad.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
constexpr int kExpectedSamples = 320;
constexpr int kSampleRateHz = 16000;
constexpr int kFrameMs = 20;

vad_handle_t g_vad = nullptr;
SemaphoreHandle_t g_vad_mutex = nullptr;

bool ensure_vad_initialized() {
  if (!g_vad_mutex) {
    g_vad_mutex = xSemaphoreCreateMutex();
  }
  if (!g_vad) {
    g_vad = vad_create_with_param(VAD_MODE_0, kSampleRateHz, kFrameMs, kFrameMs, 100);
  }
  return g_vad_mutex && g_vad;
}

uint16_t abs16(int16_t value) {
  return value == INT16_MIN ? 32768 : static_cast<uint16_t>(value < 0 ? -value : value);
}
}  // namespace

PipecatVadResult pipecat_vad_process_20ms(const int16_t *pcm, int samples) {
  int64_t start_us = esp_timer_get_time();
  PipecatVadResult result = {};

  if (!pcm || samples <= 0) {
    result.inference_us = static_cast<uint32_t>(esp_timer_get_time() - start_us);
    return result;
  }

  if (samples > kExpectedSamples) {
    samples = kExpectedSamples;
  }

  int64_t sum = 0;
  uint16_t peak = 0;
  for (int i = 0; i < samples; ++i) {
    sum += pcm[i];
    uint16_t abs_v = abs16(pcm[i]);
    if (abs_v > peak) {
      peak = abs_v;
    }
  }

  int32_t mean = static_cast<int32_t>(sum / samples);
  uint64_t energy = 0;
  uint16_t zcr = 0;
  int32_t prev = static_cast<int32_t>(pcm[0]) - mean;
  for (int i = 0; i < samples; ++i) {
    int32_t value = static_cast<int32_t>(pcm[i]) - mean;
    energy += static_cast<uint64_t>(static_cast<int64_t>(value) * value);
    if (i > 0 && ((prev < 0 && value >= 0) || (prev >= 0 && value < 0))) {
      ++zcr;
    }
    prev = value;
  }

  uint32_t rms = static_cast<uint32_t>(sqrt(static_cast<double>(energy) / samples));

  result.rms = rms;
  result.zcr = zcr;
  result.peak = peak;

  if (ensure_vad_initialized()) {
    xSemaphoreTake(g_vad_mutex, portMAX_DELAY);
    vad_state_t state = vad_process(g_vad, const_cast<int16_t *>(pcm), kSampleRateHz, kFrameMs);
    xSemaphoreGive(g_vad_mutex);
    result.speech = state == VAD_SPEECH ? 1 : 0;
  }

  result.inference_us = static_cast<uint32_t>(esp_timer_get_time() - start_us);
  return result;
}
