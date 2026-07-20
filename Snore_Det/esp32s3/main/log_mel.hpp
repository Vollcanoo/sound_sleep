#pragma once

#include "esp_err.h"

namespace snore {

constexpr int kFftSize = 1024;
constexpr int kHopLength = 512;
constexpr int kMelBins = 64;
constexpr int kFrames = 64;

class LogMelExtractor {
public:
    esp_err_t initialize();
    esp_err_t extract(const float *samples, int sample_count, float *feature);

private:
    float window_[kFftSize] = {};
    float fft_[kFftSize * 2] = {};
    bool initialized_ = false;
};

}  // namespace snore
