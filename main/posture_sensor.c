#include <string.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "posture_sensor.h"

static const char *TAG = "POSTURE";

#define SAMPLE_INTERVAL_MS 100
#define OUTPUT_INTERVAL_MS 1000
#define CALIBRATION_SAMPLES 100
#define WINDOW_SIZE 31

#define FSR_LEFT_CHANNEL ADC_CHANNEL_3   /* GPIO4 */
#define FSR_CENTER_CHANNEL ADC_CHANNEL_4 /* GPIO5 */
#define FSR_RIGHT_CHANNEL ADC_CHANNEL_5  /* GPIO6 */

static const bool kPressureIncreasesWithForce = true;
static const bool kMirrorLeftRight = false;

static const float kSensorXLeftCm = -5.0f;
static const float kSensorXCenterCm = 0.0f;
static const float kSensorXRightCm = 5.0f;

static const float kNoHeadTotalThreshold = 180.0f;
static const float kMovementRangeRatioThreshold = 0.35f;
static const float kMovementTotalRangeThreshold = 220.0f;
static const float kSideXThresholdCm = 1.35f;
static const float kSideDominantRatio = 0.42f;

typedef struct {
    int left;
    int center;
    int right;
} sensor_raw_t;

typedef struct {
    float left;
    float center;
    float right;
    float left_range;
    float center_range;
    float right_range;
} window_stats_t;

static adc_oneshot_unit_handle_t s_adc_handle;
static QueueHandle_t s_result_queue;
static bool s_started;
static volatile bool s_recalibration_requested;

static float s_baseline_left;
static float s_baseline_center;
static float s_baseline_right;
static float s_left_window[WINDOW_SIZE];
static float s_center_window[WINDOW_SIZE];
static float s_right_window[WINDOW_SIZE];
static int s_window_index;
static int s_window_count;

static float max_float(float a, float b)
{
    return a > b ? a : b;
}

static float min_float(float a, float b)
{
    return a < b ? a : b;
}

static float abs_float(float value)
{
    return value < 0.0f ? -value : value;
}

static float clamp_float(float value, float minimum, float maximum)
{
    return max_float(minimum, min_float(value, maximum));
}

static float ratio(float value, float total)
{
    return total > 0.0f ? value / total : 0.0f;
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void reset_window(void)
{
    memset(s_left_window, 0, sizeof(s_left_window));
    memset(s_center_window, 0, sizeof(s_center_window));
    memset(s_right_window, 0, sizeof(s_right_window));
    s_window_index = 0;
    s_window_count = 0;
}

static esp_err_t read_raw_sensors(sensor_raw_t *raw)
{
    esp_err_t err = adc_oneshot_read(s_adc_handle, FSR_LEFT_CHANNEL, &raw->left);
    if (err != ESP_OK) {
        return err;
    }

    err = adc_oneshot_read(s_adc_handle, FSR_CENTER_CHANNEL, &raw->center);
    if (err != ESP_OK) {
        return err;
    }

    return adc_oneshot_read(s_adc_handle, FSR_RIGHT_CHANNEL, &raw->right);
}

static esp_err_t calibrate_baseline(void)
{
    int64_t sum_left = 0;
    int64_t sum_center = 0;
    int64_t sum_right = 0;

    ESP_LOGI(TAG, "Calibrating baseline; keep the pillow unloaded");

    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
        sensor_raw_t raw;
        esp_err_t err = read_raw_sensors(&raw);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADC calibration read failed: %s", esp_err_to_name(err));
            return err;
        }

        sum_left += raw.left;
        sum_center += raw.center;
        sum_right += raw.right;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    s_baseline_left = (float)sum_left / CALIBRATION_SAMPLES;
    s_baseline_center = (float)sum_center / CALIBRATION_SAMPLES;
    s_baseline_right = (float)sum_right / CALIBRATION_SAMPLES;
    reset_window();

    ESP_LOGI(TAG, "Baseline raw L,C,R=%.1f,%.1f,%.1f",
             s_baseline_left, s_baseline_center, s_baseline_right);
    return ESP_OK;
}

static float pressure_from_raw(int raw, float baseline)
{
    float delta = kPressureIncreasesWithForce ? raw - baseline : baseline - raw;
    return max_float(0.0f, delta);
}

static void add_pressure_sample(const sensor_raw_t *raw)
{
    float left = pressure_from_raw(raw->left, s_baseline_left);
    float center = pressure_from_raw(raw->center, s_baseline_center);
    float right = pressure_from_raw(raw->right, s_baseline_right);

    if (kMirrorLeftRight) {
        float temp = left;
        left = right;
        right = temp;
    }

    s_left_window[s_window_index] = left;
    s_center_window[s_window_index] = center;
    s_right_window[s_window_index] = right;
    s_window_index = (s_window_index + 1) % WINDOW_SIZE;
    if (s_window_count < WINDOW_SIZE) {
        s_window_count++;
    }
}

static void sort_values(float values[], int count)
{
    for (int i = 1; i < count; i++) {
        float key = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > key) {
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = key;
    }
}

static float median_of_window(const float values[], int count)
{
    float copy[WINDOW_SIZE];
    memcpy(copy, values, count * sizeof(float));
    sort_values(copy, count);
    return copy[count / 2];
}

static float range_of_window(const float values[], int count)
{
    float minimum = values[0];
    float maximum = values[0];
    for (int i = 1; i < count; i++) {
        minimum = min_float(minimum, values[i]);
        maximum = max_float(maximum, values[i]);
    }
    return maximum - minimum;
}

static window_stats_t calculate_window_stats(void)
{
    window_stats_t stats = {
        .left = median_of_window(s_left_window, s_window_count),
        .center = median_of_window(s_center_window, s_window_count),
        .right = median_of_window(s_right_window, s_window_count),
        .left_range = range_of_window(s_left_window, s_window_count),
        .center_range = range_of_window(s_center_window, s_window_count),
        .right_range = range_of_window(s_right_window, s_window_count),
    };
    return stats;
}

static posture_data_t classify_posture(const sensor_raw_t *raw,
                                       const window_stats_t *stats)
{
    posture_data_t result = {
        .raw_left = raw->left,
        .raw_center = raw->center,
        .raw_right = raw->right,
        .median_left = stats->left,
        .median_center = stats->center,
        .median_right = stats->right,
    };

    result.total_pressure = stats->left + stats->center + stats->right;
    result.left_ratio = ratio(stats->left, result.total_pressure);
    result.center_ratio = ratio(stats->center, result.total_pressure);
    result.right_ratio = ratio(stats->right, result.total_pressure);
    result.x_center_cm = (
        stats->left * kSensorXLeftCm +
        stats->center * kSensorXCenterCm +
        stats->right * kSensorXRightCm
    ) / max_float(result.total_pressure, 1.0f);

    float max_range = max_float(stats->left_range,
                                max_float(stats->center_range, stats->right_range));
    result.moving = max_range > kMovementTotalRangeThreshold &&
                    ratio(max_range, max_float(result.total_pressure, 1.0f)) >
                        kMovementRangeRatioThreshold;

    if (result.total_pressure < kNoHeadTotalThreshold) {
        result.posture = POSTURE_NO_HEAD;
        result.confidence = 0.95f;
    } else if (result.moving) {
        result.posture = POSTURE_MOVING;
        result.confidence = 0.80f;
    } else if (result.x_center_cm < -kSideXThresholdCm &&
               result.left_ratio > kSideDominantRatio) {
        result.posture = POSTURE_LEFT_SIDE;
        result.confidence = clamp_float(0.55f + abs_float(result.x_center_cm) / 5.0f,
                                        0.0f, 0.95f);
    } else if (result.x_center_cm > kSideXThresholdCm &&
               result.right_ratio > kSideDominantRatio) {
        result.posture = POSTURE_RIGHT_SIDE;
        result.confidence = clamp_float(0.55f + abs_float(result.x_center_cm) / 5.0f,
                                        0.0f, 0.95f);
    } else {
        result.posture = POSTURE_SUPINE;
        result.confidence = clamp_float(0.55f + result.center_ratio * 0.35f,
                                        0.0f, 0.90f);
    }

    return result;
}

static void posture_sensor_task(void *arg)
{
    if (calibrate_baseline() != ESP_OK) {
        ESP_LOGE(TAG, "Initial calibration failed; posture task stopped");
        vTaskDelete(NULL);
        return;
    }

    int64_t last_output_ms = 0;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        if (s_recalibration_requested) {
            s_recalibration_requested = false;
            calibrate_baseline();
            last_output_ms = 0;
        }

        sensor_raw_t raw;
        esp_err_t err = read_raw_sensors(&raw);
        if (err == ESP_OK) {
            add_pressure_sample(&raw);
            int64_t timestamp = now_ms();
            if (s_window_count >= WINDOW_SIZE &&
                timestamp - last_output_ms >= OUTPUT_INTERVAL_MS) {
                last_output_ms = timestamp;
                window_stats_t stats = calculate_window_stats();
                posture_data_t result = classify_posture(&raw, &stats);
                xQueueOverwrite(s_result_queue, &result);
                ESP_LOGD(TAG, "raw=%d,%d,%d total=%.1f posture=%s confidence=%.2f",
                         result.raw_left, result.raw_center, result.raw_right,
                         result.total_pressure, posture_name(result.posture),
                         result.confidence);
            }
        } else {
            ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(err));
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }
}

esp_err_t posture_sensor_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_config, &s_adc_handle);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_oneshot_config_channel(s_adc_handle, FSR_LEFT_CHANNEL, &channel_config);
    if (err == ESP_OK) {
        err = adc_oneshot_config_channel(s_adc_handle, FSR_CENTER_CHANNEL, &channel_config);
    }
    if (err == ESP_OK) {
        err = adc_oneshot_config_channel(s_adc_handle, FSR_RIGHT_CHANNEL, &channel_config);
    }
    if (err != ESP_OK) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return err;
    }

    s_result_queue = xQueueCreate(1, sizeof(posture_data_t));
    if (s_result_queue == NULL) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    BaseType_t created = xTaskCreate(posture_sensor_task, "posture", 4096,
                                     NULL, 4, NULL);
    if (created != pdPASS) {
        vQueueDelete(s_result_queue);
        s_result_queue = NULL;
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    return ESP_OK;
}

bool posture_sensor_receive(posture_data_t *result, TickType_t timeout)
{
    return s_result_queue != NULL &&
           xQueueReceive(s_result_queue, result, timeout) == pdTRUE;
}

void posture_sensor_request_calibration(void)
{
    s_recalibration_requested = true;
}
