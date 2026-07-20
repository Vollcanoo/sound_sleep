#include "esp_log.h"
#include "snore_detector.h"

static const char *TAG = "SNORE";

esp_err_t snore_detector_start(void)
{
    ESP_LOGW(TAG, "Snore model is missing; posture monitoring will continue without snore inference");
    return ESP_ERR_NOT_SUPPORTED;
}

bool snore_detector_receive(snore_reading_t *reading, TickType_t timeout)
{
    (void)reading;
    (void)timeout;
    return false;
}

bool snore_detector_is_available(void)
{
    return false;
}
