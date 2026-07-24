/**
 * llm_periodic.c — 周期性 LLM 气泵控制
 *
 * 每 5 分钟聚合传感器数据，调用 LLM 获取气泵控制指令。
 * 两种模式（自动/手动）下均运行。
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "llm_periodic.h"
#include "snore_feature.h"
#include "cloud_llm_client.h"

static const char *TAG = "LLM_PERIODIC";

#define LLM_WINDOW_FRAMES    300          /* 5min * 60s * 1Hz */
#define LLM_OVERRIDE_MS      (5*60*1000)  /* 5 分钟有效期 */
#define LLM_TASK_STACK       16384

/* ── 窗口统计（running stats）──────────────────────── */
typedef struct {
    int   frame_count;
    float sum_mean_prob;
    float max_prob;
    float sum_positive_ratio;
    int   snore_detected_count;
    int   posture_seconds[POSTURE_COUNT];
    posture_t last_posture;
    float last_confidence;
    float sum_snore_min_per_hour;
} window_stats_t;

static window_stats_t s_stats;
static QueueHandle_t s_cmd_queue;
static volatile int64_t s_override_expire_ms = 0;

/* ── One-shot LLM 调用任务 ─────────────────────────── */
static void llm_analyze_task(void *arg)
{
    llm_window_summary_t *summary = (llm_window_summary_t *)arg;
    pump_command_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    int ret = cloud_llm_analyze_window(summary, &cmd);

    if (ret == 0) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        s_override_expire_ms = now_ms + LLM_OVERRIDE_MS;

        if (strcmp(cmd.action, "hold") != 0) {
            ESP_LOGI(TAG, "LLM 指令: %s %s %d%% %ds",
                     cmd.action, cmd.zone, cmd.intensity, cmd.duration_sec);
            xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(1000));
        } else {
            ESP_LOGI(TAG, "LLM 指令: hold (override 5min)");
        }
    } else {
        ESP_LOGW(TAG, "LLM 调用失败 (err=%d)，本轮无指令", ret);
    }

    free(summary);
    vTaskDelete(NULL);
}

/* ── 公开接口 ──────────────────────────────────────── */

void llm_periodic_init(QueueHandle_t cmd_queue)
{
    s_cmd_queue = cmd_queue;
    memset(&s_stats, 0, sizeof(s_stats));
    s_override_expire_ms = 0;
    ESP_LOGI(TAG, "LLM 周期控制已初始化 (间隔 %d 帧 = 5min)", LLM_WINDOW_FRAMES);
}

void llm_periodic_on_frame(const snore_features_t *feat)
{
    if (feat == NULL) return;

    /* 累积统计 */
    s_stats.frame_count++;
    s_stats.sum_mean_prob += feat->mean_probability;
    if (feat->max_probability > s_stats.max_prob) {
        s_stats.max_prob = feat->max_probability;
    }
    s_stats.sum_positive_ratio += feat->positive_window_ratio;
    s_stats.sum_snore_min_per_hour += feat->snore_minutes_per_hour;
    if (feat->snore_detected) {
        s_stats.snore_detected_count++;
    }

    posture_t p = feat->posture.posture;
    if (p < POSTURE_COUNT) {
        s_stats.posture_seconds[p]++;
    }
    s_stats.last_posture = p;
    s_stats.last_confidence = feat->posture.confidence;

    /* 达到 5 分钟窗口 */
    if (s_stats.frame_count >= LLM_WINDOW_FRAMES) {
        int fc = s_stats.frame_count;

        /* 堆分配 summary 给 one-shot task */
        llm_window_summary_t *summary = malloc(sizeof(llm_window_summary_t));
        if (summary == NULL) {
            ESP_LOGE(TAG, "malloc failed, 跳过本轮 LLM");
            memset(&s_stats, 0, sizeof(s_stats));
            return;
        }

        summary->frame_count = fc;
        summary->avg_mean_probability = s_stats.sum_mean_prob / fc;
        summary->max_probability = s_stats.max_prob;
        summary->avg_positive_ratio = s_stats.sum_positive_ratio / fc;
        summary->snore_minutes_per_hour = s_stats.sum_snore_min_per_hour / fc;
        summary->snore_detected_frames = s_stats.snore_detected_count;
        memcpy(summary->posture_seconds, s_stats.posture_seconds,
               sizeof(s_stats.posture_seconds));
        summary->last_posture = s_stats.last_posture;
        summary->last_confidence = s_stats.last_confidence;

        /* 找主要姿势 */
        int max_sec = 0;
        summary->dominant_posture = POSTURE_SUPINE;
        for (int i = 0; i < POSTURE_COUNT; i++) {
            if (summary->posture_seconds[i] > max_sec) {
                max_sec = summary->posture_seconds[i];
                summary->dominant_posture = (posture_t)i;
            }
        }

        /* 启动 one-shot task */
        BaseType_t ok = xTaskCreate(llm_analyze_task, "llm_call",
                                    LLM_TASK_STACK, summary, 2, NULL);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "xTaskCreate failed, 跳过本轮 LLM");
            free(summary);
        }

        /* 重置窗口 */
        memset(&s_stats, 0, sizeof(s_stats));
    }
}

bool llm_periodic_override_active(void)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    return now_ms < s_override_expire_ms;
}

void llm_periodic_reset(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_override_expire_ms = 0;
    ESP_LOGI(TAG, "LLM 周期控制已重置");
}
