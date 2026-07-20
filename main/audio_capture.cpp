#include "audio_capture.hpp"

#include <cstdint>

#include "driver/i2s.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

#ifndef CONFIG_SNORE_I2S_BCLK_GPIO
#define CONFIG_SNORE_I2S_BCLK_GPIO 16
#define CONFIG_SNORE_I2S_WS_GPIO 15
#define CONFIG_SNORE_I2S_DOUT_GPIO 17
#define CONFIG_SNORE_I2S_USE_RIGHT_CHANNEL 1
#endif

namespace snore {
namespace {

constexpr int kFramesPerRead = 1024;
constexpr int kStartupDrainBlocks = 4;
constexpr float kDcBlockAlpha = 0.995F;
constexpr i2s_port_t kI2sPort = I2S_NUM_0;

alignas(4) int32_t s_i2s_buffer[kFramesPerRead * 2];

}  // namespace

esp_err_t AudioCapture::initialize()
{
    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate = kSampleRate;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format =
        static_cast<i2s_comm_format_t>(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 8;
    config.dma_buf_len = 256;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = CONFIG_SNORE_I2S_BCLK_GPIO;
    pins.ws_io_num = CONFIG_SNORE_I2S_WS_GPIO;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = CONFIG_SNORE_I2S_DOUT_GPIO;

    ESP_RETURN_ON_ERROR(i2s_driver_install(kI2sPort, &config, 0, nullptr),
                        "snore_audio", "install I2S driver");
    ESP_RETURN_ON_ERROR(i2s_set_pin(kI2sPort, &pins),
                        "snore_audio", "configure I2S pins");
    ESP_RETURN_ON_ERROR(i2s_zero_dma_buffer(kI2sPort),
                        "snore_audio", "clear I2S DMA");

    for (int block = 0; block < kStartupDrainBlocks; ++block) {
        size_t bytes_read = 0;
        ESP_RETURN_ON_ERROR(i2s_read(kI2sPort, s_i2s_buffer, sizeof(s_i2s_buffer),
                                     &bytes_read, portMAX_DELAY),
                            "snore_audio", "discard startup I2S frames");
    }
    return ESP_OK;
}

esp_err_t AudioCapture::capture(float *samples, int sample_count)
{
    int written = 0;
    const int slot = CONFIG_SNORE_I2S_USE_RIGHT_CHANNEL ? 1 : 0;
    dc_last_input_ = 0.0F;
    dc_last_output_ = 0.0F;

    while (written < sample_count) {
        size_t bytes_read = 0;
        ESP_RETURN_ON_ERROR(i2s_read(kI2sPort, s_i2s_buffer, sizeof(s_i2s_buffer),
                                     &bytes_read, portMAX_DELAY),
                            "snore_audio", "read I2S samples");
        const int frames = bytes_read / (sizeof(int32_t) * 2);
        for (int frame = 0; frame < frames && written < sample_count; ++frame) {
            const float input = static_cast<float>(s_i2s_buffer[frame * 2 + slot]) /
                                2147483648.0F;
            const float filtered = input - dc_last_input_ + kDcBlockAlpha * dc_last_output_;
            dc_last_input_ = input;
            dc_last_output_ = filtered;
            samples[written++] = filtered;
        }
    }
    return ESP_OK;
}

}  // namespace snore
