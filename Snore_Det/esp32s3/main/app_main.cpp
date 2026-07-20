#include <cstring>
#include <map>
#include <string>

#include "audio_capture.hpp"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "log_mel.hpp"
#include "dl_model_base.hpp"

extern const uint8_t snoring_esp32_int8_espdl[] asm("_binary_snoring_esp32_int8_espdl_start");

namespace {

constexpr char kTag[] = "snoring";
constexpr int kFeatureSize = 64 * 64;

class SnoreDetector {
public:
    SnoreDetector()
        : model_(reinterpret_cast<const char *>(snoring_esp32_int8_espdl), fbs::MODEL_LOCATION_IN_FLASH_RODATA)
    {
        input_ = model_.get_inputs().begin()->second;
        output_ = model_.get_outputs().begin()->second;
    }

    bool detect(const float *log_mel, float *snore_score)
    {
        dl::TensorBase feature({1, 1, 64, 64}, nullptr, 0, dl::DATA_TYPE_FLOAT);
        std::memcpy(feature.data, log_mel, kFeatureSize * sizeof(float));
        input_->assign(&feature);
        model_.run();

        dl::TensorBase logits({1, 2}, nullptr, 0, dl::DATA_TYPE_FLOAT);
        logits.assign(output_);
        const auto *values = static_cast<const float *>(logits.data);
        *snore_score = values[1];
        return values[1] > values[0];
    }

private:
    dl::Model model_;
    dl::TensorBase *input_ = nullptr;
    dl::TensorBase *output_ = nullptr;
};

}  // namespace

extern "C" void app_main(void)
{
    snore::AudioCapture microphone;
    // The FFT workspace is about 12 KB and must not live on the main task stack.
    static snore::LogMelExtractor extractor;
    ESP_ERROR_CHECK(microphone.initialize());
    ESP_ERROR_CHECK(extractor.initialize());
    static SnoreDetector detector;

    auto *pcm = static_cast<float *>(heap_caps_malloc(snore::kCaptureSamples * sizeof(float), MALLOC_CAP_SPIRAM));
    auto *log_mel = static_cast<float *>(heap_caps_malloc(kFeatureSize * sizeof(float), MALLOC_CAP_SPIRAM));
    if (pcm == nullptr || log_mel == nullptr) {
        ESP_LOGE(kTag, "not enough PSRAM for the audio frontend");
        return;
    }

    while (true) {
        ESP_ERROR_CHECK(microphone.capture(pcm, snore::kCaptureSamples));
        ESP_ERROR_CHECK(extractor.extract(pcm, snore::kCaptureSamples, log_mel));

        float snore_score = 0.0F;
        const bool is_snore = detector.detect(log_mel, &snore_score);
        ESP_LOGI(kTag, "prediction=%s score=%f", is_snore ? "snore" : "non-snore", snore_score);
    }
}
