/**
 * main.c — ESP32 智能防鼾睡姿调节系统 主程序
 *
 * 数据合约: snore_model_output.template.json (最终定稿)
 *   - 顶层: window_seconds, hop_seconds, decision_threshold
 *   - summary: 9 个统计字段 (基于 windows[].probability 计算)
 *   - windows[]: 核心数据层 (模型直接输出 probability)
 *   - ESP32 只传 summary 给 LLM (windows 太大)
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "snore_feature.h"
#include "wifi_manager.h"
#include "cloud_llm_client.h"
#include "pump_controller.h"

static const char *TAG = "MAIN";

/* ── 全局队列 ──────────────────────────────────────── */
QueueHandle_t g_feature_queue = NULL;   /* 组员写入 → cloud_task 读取 */
static QueueHandle_t s_cmd_queue = NULL; /* cloud_task 写入 → pump_task 读取 */

/* ────────────────────────────────────────────────────
 *  cloud_task — 云端分析任务
 * ──────────────────────────────────────────────────── */
static void cloud_task(void *arg)
{
    snore_features_t feat;
    char report[512];
    pump_command_t cmd;

    ESP_LOGI(TAG, "cloud_task 已启动，等待模型输出数据...");

    while (1) {
        if (xQueueReceive(g_feature_queue, &feat, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "═══════════════════════════════════════");
            ESP_LOGI(TAG, "收到鼾声模型输出:");
            ESP_LOGI(TAG, "  snore_detected: %s", feat.snore_detected ? "是" : "否");
            ESP_LOGI(TAG, "  mean_probability: %.4f", feat.mean_probability);
            ESP_LOGI(TAG, "  max_probability: %.4f", feat.max_probability);
            ESP_LOGI(TAG, "  正窗口: %d/%d (%.2f%%)",
                     feat.positive_window_count, feat.window_count,
                     feat.positive_window_ratio * 100.0f);
            ESP_LOGI(TAG, "  鼾声时长: %.1f 秒 (%.2f 分钟/小时)",
                     feat.positive_duration_seconds,
                     feat.snore_minutes_per_hour);

            memset(report, 0, sizeof(report));
            memset(&cmd, 0, sizeof(cmd));

            int ret = cloud_llm_analyze(&feat, report, sizeof(report), &cmd);

            if (ret == 0) {
                ESP_LOGI(TAG, "───────────────────────────────────");
                ESP_LOGI(TAG, "📋 分析报告: %s", report);
                ESP_LOGI(TAG, "🎮 控制指令: action=%s zone=%s intensity=%d duration=%ds",
                         cmd.action, cmd.zone, cmd.intensity, cmd.duration_sec);
                ESP_LOGI(TAG, "═══════════════════════════════════════");

                if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
                    ESP_LOGW(TAG, "气泵指令队列已满，丢弃本次指令");
                }
            } else {
                ESP_LOGE(TAG, "云端 API 调用失败 (错误码: %d)", ret);
            }
        }
    }
}

/* ────────────────────────────────────────────────────
 *  pump_task — 气泵控制任务
 * ──────────────────────────────────────────────────── */
static void pump_task(void *arg)
{
    pump_command_t cmd;

    ESP_LOGI(TAG, "pump_task 已启动，等待控制指令...");

    while (1) {
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "🔧 执行气泵指令: %s %s (强度%d%%, %ds)",
                     cmd.action, cmd.zone, cmd.intensity, cmd.duration_sec);
            pump_execute_command(&cmd);
        }
    }
}

/* ────────────────────────────────────────────────────
 *  mock_data_task — 模拟测试数据
 *
 *  数据完全对齐 snore_model_output.template.json 的 summary
 *  组员代码接入后设 ENABLE_MOCK_DATA=0
 * ──────────────────────────────────────────────────── */
#define ENABLE_MOCK_DATA  1

#if ENABLE_MOCK_DATA
static void mock_data_task(void *arg)
{
    ESP_LOGW(TAG, "⚠️  模拟数据模式（组员代码接入后请关闭 ENABLE_MOCK_DATA）");

    /* 场景1: 正常 — 无鼾声 */
    snore_features_t scene1 = {
        .window_seconds         = 5.0f,
        .hop_seconds            = 5.0f,
        .decision_threshold     = 0.46f,
        .window_count           = 3985,
        .mean_probability       = 0.08f,
        .max_probability        = 0.35f,
        .positive_window_count  = 0,
        .positive_window_ratio  = 0.0f,
        .positive_duration_seconds = 0.0f,
        .positive_duration_minutes = 0.0f,
        .snore_detected         = false,
        .snore_minutes_per_hour = 0.0f,
    };

    /* 场景2: 轻度 — snore_minutes_per_hour < 2 → hold */
    snore_features_t scene2 = {
        .window_seconds         = 5.0f,
        .hop_seconds            = 5.0f,
        .decision_threshold     = 0.46f,
        .window_count           = 3985,
        .mean_probability       = 0.15f,
        .max_probability        = 0.72f,
        .positive_window_count  = 60,
        .positive_window_ratio  = 0.015f,
        .positive_duration_seconds = 300.0f,
        .positive_duration_minutes = 5.0f,
        .snore_detected         = true,
        .snore_minutes_per_hour = 0.90f,
    };

    /* 场景3: 中度 — snore_minutes_per_hour ≈ 4 → inflate shoulder */
    snore_features_t scene3 = {
        .window_seconds         = 5.0f,
        .hop_seconds            = 5.0f,
        .decision_threshold     = 0.46f,
        .window_count           = 3985,
        .mean_probability       = 0.2143f,
        .max_probability        = 0.951f,
        .positive_window_count  = 270,
        .positive_window_ratio  = 0.06775f,
        .positive_duration_seconds = 1350.0f,
        .positive_duration_minutes = 22.5f,
        .snore_detected         = true,
        .snore_minutes_per_hour = 4.0659f,
    };

    /* 场景4: 严重 — max>0.9 且 ratio>0.1 → inflate head */
    snore_features_t scene4 = {
        .window_seconds         = 5.0f,
        .hop_seconds            = 5.0f,
        .decision_threshold     = 0.46f,
        .window_count           = 3985,
        .mean_probability       = 0.35f,
        .max_probability        = 0.97f,
        .positive_window_count  = 500,
        .positive_window_ratio  = 0.1255f,
        .positive_duration_seconds = 2500.0f,
        .positive_duration_minutes = 41.67f,
        .snore_detected         = true,
        .snore_minutes_per_hour = 7.53f,
    };

    const snore_features_t *scenarios[] = { &scene1, &scene2, &scene3, &scene4 };
    int num_scenarios = sizeof(scenarios) / sizeof(scenarios[0]);
    int scenario_idx = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        snore_features_t feat = *scenarios[scenario_idx];
        feat.timestamp_ms = esp_timer_get_time() / 1000;

        ESP_LOGI(TAG, "📡 模拟场景 %d/%d: detected=%s mean=%.4f max=%.4f %.2f分钟/时",
                 scenario_idx + 1, num_scenarios,
                 feat.snore_detected ? "是" : "否",
                 feat.mean_probability,
                 feat.max_probability,
                 feat.snore_minutes_per_hour);

        xQueueSend(g_feature_queue, &feat, pdMS_TO_TICKS(1000));
        scenario_idx = (scenario_idx + 1) % num_scenarios;
    }
}
#endif

/* ────────────────────────────────────────────────────
 *  app_main — 系统入口
 * ──────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   ESP32 智能防鼾睡姿调节系统 v2.0   ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════╝");
    ESP_LOGI(TAG, "");

    g_feature_queue = xQueueCreate(4, sizeof(snore_features_t));
    s_cmd_queue     = xQueueCreate(4, sizeof(pump_command_t));
    if (!g_feature_queue || !s_cmd_queue) {
        ESP_LOGE(TAG, "队列创建失败！");
        return;
    }

    ESP_LOGI(TAG, "正在连接 Wi-Fi...");
    esp_err_t wifi_ret = wifi_manager_init();
    if (wifi_ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi 连接失败，系统无法启动");
        return;
    }
    ESP_LOGI(TAG, "✅ Wi-Fi 已连接");

    pump_controller_init();
    ESP_LOGI(TAG, "✅ 气泵控制器已初始化");

    xTaskCreate(cloud_task, "cloud", 16384, NULL, 3, NULL);
    xTaskCreate(pump_task,  "pump",  4096,  NULL, 4, NULL);
    ESP_LOGI(TAG, "✅ cloud_task 和 pump_task 已启动");

#if ENABLE_MOCK_DATA
    xTaskCreate(mock_data_task, "mock", 4096, NULL, 2, NULL);
    ESP_LOGW(TAG, "⚠️  模拟数据模式 — 每30秒发送测试数据");
#else
    ESP_LOGI(TAG, "等待组员通过 g_feature_queue 发送模型输出...");
#endif

    /*
     * 组员集成方式:
     *   #include "snore_feature.h"
     *   extern QueueHandle_t g_feature_queue;
     *
     *   snore_features_t feat = {
     *       .window_seconds = 5.0, .hop_seconds = 5.0, .decision_threshold = 0.46,
     *       .window_count = ..., .mean_probability = ..., .max_probability = ...,
     *       .positive_window_count = ..., .positive_window_ratio = ...,
     *       .positive_duration_seconds = ..., .positive_duration_minutes = ...,
     *       .snore_detected = ..., .snore_minutes_per_hour = ...,
     *   };
     *   xQueueSend(g_feature_queue, &feat, portMAX_DELAY);
     */

    ESP_LOGI(TAG, "🚀 系统已就绪！");
}
