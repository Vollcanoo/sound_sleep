#include "log_mel.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio_capture.hpp"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_dsp.h"

namespace snore {
namespace {

constexpr float kMinMelHz = 20.0F;
constexpr float kMaxMelHz = 8000.0F;
constexpr float kFSp = 200.0F / 3.0F;
constexpr float kMinLogHz = 1000.0F;
constexpr float kMinLogMel = kMinLogHz / kFSp;
constexpr float kLogStep = 1.8562979903656263F / 27.0F;

float hz_to_mel(float hz)
{
    return hz < kMinLogHz ? hz / kFSp
                           : kMinLogMel + logf(hz / kMinLogHz) / kLogStep;
}

float mel_to_hz(float mel)
{
    return mel < kMinLogMel ? mel * kFSp
                             : kMinLogHz * expf(kLogStep * (mel - kMinLogMel));
}

}  // namespace

esp_err_t LogMelExtractor::initialize()
{
    if (initialized_) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(dsps_fft2r_init_fc32(nullptr, kFftSize),
                        "snore_mel", "initialize FFT");
    mel_filter_ = static_cast<float *>(heap_caps_malloc(
        kMelBins * (kFftSize / 2 + 1) * sizeof(float), MALLOC_CAP_SPIRAM));
    if (mel_filter_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    constexpr float kPi = 3.14159265358979323846F;
    for (int index = 0; index < kFftSize; ++index) {
        window_[index] = 0.5F * (1.0F - cosf(2.0F * kPi * index / kFftSize));
    }

    const float min_mel = hz_to_mel(kMinMelHz);
    const float max_mel = hz_to_mel(kMaxMelHz);
    float mel_points[kMelBins + 2] = {};
    for (int point = 0; point < kMelBins + 2; ++point) {
        const float fraction = static_cast<float>(point) / (kMelBins + 1);
        mel_points[point] = mel_to_hz(min_mel + fraction * (max_mel - min_mel));
    }

    for (int mel = 0; mel < kMelBins; ++mel) {
        const float left = mel_points[mel];
        const float center = mel_points[mel + 1];
        const float right = mel_points[mel + 2];
        const float norm = 2.0F / (right - left);
        for (int bin = 0; bin <= kFftSize / 2; ++bin) {
            const float frequency = static_cast<float>(bin) * kSampleRate / kFftSize;
            const float lower = (frequency - left) / (center - left);
            const float upper = (right - frequency) / (right - center);
            mel_filter_[mel * (kFftSize / 2 + 1) + bin] =
                std::max(0.0F, std::min(lower, upper)) * norm;
        }
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
        ESP_RETURN_ON_ERROR(dsps_fft2r_fc32(fft_, kFftSize), "snore_mel", "run FFT");
        ESP_RETURN_ON_ERROR(dsps_bit_rev_fc32(fft_, kFftSize), "snore_mel", "reorder FFT");

        for (int mel = 0; mel < kMelBins; ++mel) {
            float energy = 0.0F;
            const float *filter = mel_filter_ + mel * (kFftSize / 2 + 1);
            for (int bin = 0; bin <= kFftSize / 2; ++bin) {
                const float real = fft_[bin * 2];
                const float imaginary = fft_[bin * 2 + 1];
                energy += filter[bin] * (real * real + imaginary * imaginary);
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
