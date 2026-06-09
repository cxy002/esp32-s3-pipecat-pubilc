#pragma once

#include <stdint.h>

struct PipecatVadResult {
  uint8_t speech;
  uint32_t inference_us;
  uint32_t rms;
  uint16_t zcr;
  uint16_t peak;
};

PipecatVadResult pipecat_vad_process_20ms(const int16_t *pcm, int samples);
