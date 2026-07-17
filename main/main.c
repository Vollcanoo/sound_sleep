/**
 * main.c — ESP32 智能防鼾睡姿调节系统 主程序
 *
 * 全流程整合:
 *   1. Snore_Det_esp: INMP441麦克风(GPIO14/15/32) → 鼾声模型推理 → probability
 *   2. Posture_Recognition: FSR×3压力传感器(GPIO4/5/6) → 睡姿分类
 *   3. sleep_llm (本模块): 综合数据 → 云端LLM分析 → 气泵控制指令
 *   4. airbag-hardware: 左/右双气囊(GPIO7/8泵, GPIO9/10阀) → 枕头高度调节
 *
 * GPIO 分配总览:
 *   GPIO4/5/6   - FSR 压力传感器 ADC (Posture_Recognition)
 *   GPIO7/8     - 左/右气泵 MOS驱动 (airbag-hardware)
 *   GPIO9/10    - 左/右电磁阀 AO3400A (airbag-hardware)
 *   GPIO14/15/32 - INMP441 I2S 麦克风 (Snore_Det_esp)
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

    ESP_LOGI(TAG, "cloud_task 已启动，等待传感器数据...");

    while (1) {
        if (xQueueReceive(g_feature_queue, &feat, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "═══════════════════════════════════════");
            ESP_LOGI(TAG, "收到传感器综合数据:");
            ESP_LOGI(TAG, "  【鼾声】detected=%s mean=%.4f max=%.4f %.2f分钟/时",
                     feat.snore_detected ? "是" : "否",
                     feat.mean_probability,
                     feat.max_probability,
                     feat.snore_minutes_per_hour);
            ESP_LOGI(TAG, "  【睡姿】%s (%s) 置信度=%.2f X=%.2fcm",
                     posture_name(feat.posture.posture),
                     posture_name_cn(feat.posture.posture),
                     feat.posture.confidence,
                     feat.posture.x_center_cm);

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
 *  4 个场景覆盖全流程:
 *    鼾声(Snore_Det_esp) + 睡姿(Posture_Recognition) → LLM → 气泵(airbag-hardware)
 *  组员代码接入后设 ENABLE_MOCK_DATA=0
 * ──────────────────────────────────────────────────── */
#define ENABLE_MOCK_DATA  1

#if ENABLE_MOCK_DATA
static void mock_data_task(void *arg)
{
    ESP_LOGW(TAG, "⚠️  模拟数据模式（组员代码接入后请关闭 ENABLE_MOCK_DATA）");

    /* 场景1: 正常 — 侧卧无鼾声 → hold */
    snore_features_t scene1 = {
        .window_seconds         = 5.0f,
        .hop_seconds            = 5.0f,
        .decision_threshold     = 0.44f,
        .window_count           = 3985,
        .mean_probability       = 0.08f,
        .max_probability        = 0.35f,
        .positive_window_count  = 0,
        .positive_window_ratio  = 0.0f,
        .positive_duration_seconds = 0.0f,
        .positive_duration_minutes = 0.0f,
        .snore_detected         = false,
        .snore_minutes_per_hour = 0.0f,
        .posture = {
            .posture      = POSTURE_LEFT_SIDE,
            .confidence   = 0.85f,
            .x_center_cm  = -2.1f,
            .y_center_cm  = 0.15f,
        },
    };

    /* 场景2: 仰卧轻度鼾声 — snore<2分钟/时 → hold */
    snore_features_t scene2 = {
        .window_seconds         = 5.0f,
        .hop_seconds            = 5.0f,
        .decision_threshold     = 0.44f,
        .window_count           = 3985,
        .mean_probability       = 0.15f,
        .max_probability        = 0.72f,
        .positive_window_count  = 60,
        .positive_window_ratio  = 0.015f,
        .positive_duration_seconds = 300.0f,
        .positive_duration_minutes = 5.0f,
        .snore_detected         = true,
        .snore_minutes_per_hour = 0.90f,
        .posture = {
            .posture      = POSTURE_SUPINE,
            .confidence   = 0.82f,
            .x_center_cm  = 0.12f,
            .y_center_cm  = 0.31f,
        },
    };

    /* 场景3: 仰卧中度鼾声 — snore≈4分钟/时 → inflate right */
    snore_features_t scene3 = {
        .window_seconds         = 5.0f,
        .hop_seconds            = 5.0f,
        .decision_threshold     = 0.44f,
        .window_count           = 3985,
        .mean_probability       = 0.2143f,
        .max_probability        = 0.951f,
        .positive_window_count  = 270,
        .positive_window_ratio  = 0.06775f,
        .positive_duration_seconds = 1350.0f,
        .positive_duration_minutes = 22.5f,
        .snore_detected         = true,
        .snore_minutes_per_hour = 4.0659f,
        .posture = {
            .posture      = POSTURE_SUPINE,
            .confidence   = 0.88f,
            .x_center_cm  = 0.05f,
            .y_center_cm  = 0.28f,
        },
    };

    /* 场景4: 仰卧严重鼾声 — max>0.9 且 ratio>0.1 → inflate right 高强度 */
    snore_features_t scene4 = {
        .window_seconds         = 5.0f,
        .hop_seconds            = 5.0f,
        .decision_threshold     = 0.44f,
        .window_count           = 3985,
        .mean_probability       = 0.35f,
        .max_probability        = 0.97f,
        .positive_window_count  = 500,
        .positive_window_ratio  = 0.1255f,
        .positive_duration_seconds = 2500.0f,
        .positive_duration_minutes = 41.67f,
        .snore_detected         = true,
        .snore_minutes_per_hour = 7.53f,
        .posture = {
            .posture      = POSTURE_SUPINE,
            .confidence   = 0.91f,
            .x_center_cm  = -0.08f,
            .y_center_cm  = 0.35f,
        },
    };

    const snore_features_t *scenarios[] = { &scene1, &scene2, &scene3, &scene4 };
    int num_scenarios = sizeof(scenarios) / sizeof(scenarios[0]);
    int scenario_idx = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        snore_features_t feat = *scenarios[scenario_idx];
        feat.timestamp_ms = esp_timer_get_time() / 1000;

        ESP_LOGI(TAG, "📡 模拟场景 %d/%d: snore=%s %.2f分/时 posture=%s",
                 scenario_idx + 1, num_scenarios,
                 feat.snore_detected ? "是" : "否",
                 feat.snore_minutes_per_hour,
                 posture_name(feat.posture.posture));

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
    ESP_LOGI(TAG, "║  ESP32 智能防鼾睡姿调节系统 v3.0    ║");
    ESP_LOGI(TAG, "╠══════════════════════════════════════╣");
    ESP_LOGI(TAG, "║  鼾声: Snore_Det_esp (INMP441)      ║");
    ESP_LOGI(TAG, "║  睡姿: Posture_Recognition (FSR×3)  ║");
    ESP_LOGI(TAG, "║  分析: sleep_llm (VolcEngine API)   ║");
    ESP_LOGI(TAG, "║  执行: airbag-hardware (双气囊)     ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════╝");
    ESP_LOGI(TAG, "");

    ESP_LOGI(TAG, "GPIO 分配:");
    ESP_LOGI(TAG, "  传感: FSR ADC=GPIO4/5/6, INMP441 I2S=GPIO14/15/32");
    ESP_LOGI(TAG, "  执行: 泵=GPIO7/8, 阀=GPIO9/10");

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
    ESP_LOGI(TAG, "✅ 双气囊控制器已初始化");

    xTaskCreate(cloud_task, "cloud", 16384, NULL, 3, NULL);
    xTaskCreate(pump_task,  "pump",  4096,  NULL, 4, NULL);
    ESP_LOGI(TAG, "✅ cloud_task 和 pump_task 已启动");

#if ENABLE_MOCK_DATA
    xTaskCreate(mock_data_task, "mock", 4096, NULL, 2, NULL);
    ESP_LOGW(TAG, "⚠️  模拟数据模式 — 每30秒发送测试数据");
#else
    ESP_LOGI(TAG, "等待组员通过 g_feature_queue 发送传感器数据...");
#endif

    /*
     * 组员集成方式 (两人分工):
     *
     * [鼾声组员] — 参考 Snore_Det_esp 分支
     *   每 5 秒汇总一次 decision_window 结果，累积到 summary 统计
     *
     * [睡姿组员] — 参考 Posture_Recognition 分支
     *   每秒读取 FSR 传感器，分类出 posture + confidence
     *
     * [合并发送]:
     *   #include "snore_feature.h"
     *   extern QueueHandle_t g_feature_queue;
     *
     *   snore_features_t feat = {
     *       // 鼾声 (从 Snore_Det_esp 的 FiveSecondDecisionAggregator 累积)
     *       .window_seconds = 5.0, .hop_seconds = 5.0,
     *       .decision_threshold = 0.44,
     *       .window_count = ..., .mean_probability = ..., ...
     *       // 睡姿 (从 Posture_Recognition 的 classifyPosture 获取)
     *       .posture = { .posture = POSTURE_SUPINE, .confidence = 0.85, ... },
     *   };
     *   xQueueSend(g_feature_queue, &feat, portMAX_DELAY);
     */

    ESP_LOGI(TAG, "🚀 系统已就绪！");
}
