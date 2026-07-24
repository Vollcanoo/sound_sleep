#include <cmath>
#include <cstdio>
#include <cstring>

#include "audio_capture.hpp"
#include "ble_uart_server.h"
#include "dl_model_base.hpp"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "log_mel.hpp"
#include "snore_detector.h"

extern const uint8_t snoring_esp32_int8_espdl[]
    asm("_binary_snoring_esp32_int8_espdl_start");

namespace {

constexpr char kTag[] = "SNORE";
constexpr int kFeatureSize = 64 * 64;
QueueHandle_t s_result_queue;
bool s_started;

class SnoreModel {
public:
    SnoreModel()
        : model_(reinterpret_cast<const char *>(snoring_esp32_int8_espdl),
                 fbs::MODEL_LOCATION_IN_FLASH_RODATA)
    {
        input_ = model_.get_inputs().begin()->second;
        output_ = model_.get_outputs().begin()->second;
    }

    float probability(const float *log_mel)
    {
        dl::TensorBase feature({1, 1, 64, 64}, nullptr, 0, dl::DATA_TYPE_FLOAT);
        std::memcpy(feature.data, log_mel, kFeatureSize * sizeof(float));
        input_->assign(&feature);
        model_.run();

        dl::TensorBase logits({1, 2}, nullptr, 0, dl::DATA_TYPE_FLOAT);
        logits.assign(output_);
        const auto *values = static_cast<const float *>(logits.data);
        const float maximum = values[0] > values[1] ? values[0] : values[1];
        const float non_snore = expf(values[0] - maximum);
        const float snore = expf(values[1] - maximum);
        return snore / (non_snore + snore);
    }

private:
    dl::Model model_;
    dl::TensorBase *input_ = nullptr;
    dl::TensorBase *output_ = nullptr;
};

void snore_task(void *arg)
{
    snore::AudioCapture microphone;
    static snore::LogMelExtractor extractor;
    ESP_ERROR_CHECK(microphone.initialize());
    ESP_ERROR_CHECK(extractor.initialize());
    static SnoreModel model;

    auto *pcm = static_cast<float *>(heap_caps_malloc(
        snore::kCaptureSamples * sizeof(float), MALLOC_CAP_SPIRAM));
    auto *log_mel = static_cast<float *>(heap_caps_malloc(
        kFeatureSize * sizeof(float), MALLOC_CAP_SPIRAM));
    if (pcm == nullptr || log_mel == nullptr) {
        ESP_LOGE(kTag, "Not enough PSRAM for snore inference");
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        // Run inference in both automatic and app-controlled monitoring modes.
        // The feature aggregator resets its snore counters when a new
        // pressure-present session begins, so windows collected before the
        // user lies down do not contribute to that session's summary.
        if (microphone.capture(pcm, snore::kCaptureSamples) == ESP_OK &&
            extractor.extract(pcm, snore::kCaptureSamples, log_mel) == ESP_OK) {

            // Compute RMS from PCM (normalized [-1,1]) → dB FS
            // INMP441 sensitivity: -26 dBFS = 94 dB SPL → offset ≈ 120
            float sum_sq = 0.0F;
            for (int i = 0; i < snore::kCaptureSamples; ++i) {
                sum_sq += pcm[i] * pcm[i];
            }
            float rms = sqrtf(sum_sq / snore::kCaptureSamples);
            float rms_dbfs = (rms > 1e-10F) ? 20.0F * log10f(rms) : -100.0F;
            float rms_db_spl = rms_dbfs + 120.0F; // approximate dB SPL

            snore_reading_t reading = {
                .probability = model.probability(log_mel),
                .window_seconds = static_cast<float>(snore::kCaptureSamples) / snore::kSampleRate,
                .rms_db = rms_db_spl,
                .detected = false,
            };
            reading.detected = reading.probability >= 0.5F;
            xQueueOverwrite(s_result_queue, &reading);
            if (ble_uart_is_connected()) {
                char message[128];
                const int length = snprintf(
                    message, sizeof(message),
                    "{\"type\":\"snore\",\"probability\":%.4f,\"detected\":%s,\"window_seconds\":%.3f,\"rms_db\":%.1f}\n",
                    reading.probability,
                    reading.detected ? "true" : "false",
                    reading.window_seconds,
                    reading.rms_db);
                if (length > 0 && static_cast<size_t>(length) < sizeof(message)) {
                    ble_uart_send(message, static_cast<size_t>(length));
                }
            }
            ESP_LOGI(kTag, "probability=%.3f detected=%s rms_db=%.1f",
                     reading.probability,
                     reading.detected ? "yes" : "no",
                     reading.rms_db);
        }
    }
}

}  // namespace

extern "C" esp_err_t snore_detector_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    s_result_queue = xQueueCreate(1, sizeof(snore_reading_t));
    if (s_result_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(snore_task, "snore", 8192, nullptr, 5, nullptr) != pdPASS) {
        vQueueDelete(s_result_queue);
        s_result_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}

extern "C" bool snore_detector_receive(snore_reading_t *reading, TickType_t timeout)
{
    return s_result_queue != nullptr &&
           xQueueReceive(s_result_queue, reading, timeout) == pdTRUE;
}

extern "C" bool snore_detector_is_available(void)
{
    return true;
}
