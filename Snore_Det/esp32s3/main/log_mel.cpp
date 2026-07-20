#include "log_mel.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio_capture.hpp"
#include "esp_check.h"
#include "esp_dsp.h"
#include "mel_filter_data.h"

namespace snore {

esp_err_t LogMelExtractor::initialize()
{
    if (initialized_) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(dsps_fft2r_init_fc32(nullptr, CONFIG_DSP_MAX_FFT_SIZE), "snoring_mel", "initialize FFT");
    // librosa uses scipy's periodic Hann window for STFT, not ESP-DSP's symmetric Hann window.
    constexpr float kPi = 3.14159265358979323846F;
    for (int index = 0; index < kFftSize; ++index) {
        window_[index] = 0.5F * (1.0F - cosf(2.0F * kPi * index / kFftSize));
    }
    initialized_ = true;
    return ESP_OK;
}

esp_err_t LogMelExtractor::extract(const float *samples, int sample_count, float *feature)
{
    if (!initialized_ || sample_count < kCaptureSamples) {
        return ESP_ERR_INVALID_ARG;
    }

    constexpr float kMinimumPower = 1e-10F;
    float max_db = -INFINITY;
    for (int frame = 0; frame < kFrames; ++frame) {
        const int start = frame * kHopLength - kFftSize / 2;
        std::memset(fft_, 0, sizeof(fft_));
        for (int index = 0; index < kFftSize; ++index) {
            const int source = start + index;
            const float sample = (source >= 0 && source < sample_count) ? samples[source] : 0.0F;
            fft_[index * 2] = sample * window_[index];
        }
        ESP_RETURN_ON_ERROR(dsps_fft2r_fc32(fft_, kFftSize), "snoring_mel", "run FFT");
        ESP_RETURN_ON_ERROR(dsps_bit_rev_fc32(fft_, kFftSize), "snoring_mel", "reorder FFT");

        for (int mel = 0; mel < kMelBins; ++mel) {
            float energy = 0.0F;
            for (int bin = 0; bin <= kFftSize / 2; ++bin) {
                const float real = fft_[bin * 2];
                const float imaginary = fft_[bin * 2 + 1];
                energy += kMelFilter[mel][bin] * (real * real + imaginary * imaginary);
            }
            const float db = 10.0F * log10f(std::max(energy, kMinimumPower));
            feature[mel * kFrames + frame] = db;
            max_db = std::max(max_db, db);
        }
    }

    const float floor_db = max_db - 80.0F;
    float sum = 0.0F;
    for (int index = 0; index < kMelBins * kFrames; ++index) {
        feature[index] = std::max(feature[index], floor_db);
        sum += feature[index];
    }
    const float mean = sum / static_cast<float>(kMelBins * kFrames);
    float squared_error = 0.0F;
    for (int index = 0; index < kMelBins * kFrames; ++index) {
        const float centered = feature[index] - mean;
        squared_error += centered * centered;
    }
    const float standard_deviation = sqrtf(squared_error / static_cast<float>(kMelBins * kFrames));
    for (int index = 0; index < kMelBins * kFrames; ++index) {
        feature[index] = (feature[index] - mean) / (standard_deviation + 1e-6F);
    }
    return ESP_OK;
}

}  // namespace snore
