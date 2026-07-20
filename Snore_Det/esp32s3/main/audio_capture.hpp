#pragma once

#include "driver/i2s.h"
#include "esp_err.h"

namespace snore {

constexpr int kSampleRate = 16000;
constexpr int kCaptureSamples = 32640;

class AudioCapture {
public:
    esp_err_t initialize();
    esp_err_t capture(float *samples, int sample_count);

private:
    float dc_last_input_ = 0.0F;
    float dc_last_output_ = 0.0F;
};

}  // namespace snore
