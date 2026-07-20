/**
 * main.c — ESP32 智能防鼾睡姿调节系统 主程序
 *
 * 全流程整合:
 *   1. BLE WiFi 配网: 开机 BLE 广播 → 手机发送 WiFi 凭据 → 连接 WiFi
 *   2. Snore_Det: INMP441麦克风(GPIO16/15/17) → 鼾声模型推理 → probability
 *   3. Posture_Recognition: FSR×3压力传感器(GPIO4/5/6) → 睡姿分类
 *   4. 睡眠会话管理: 检测压力出现/消失 → 累积整晚数据
 *   5. 实时气泵控制: 本地规则判断 → 充气/放气
 *   6. 睡眠结束: 云端 LLM 分析 → 数据上云 (CloudBase)
 *   7. airbag-hardware: 左/右双气囊(GPIO7/8泵, GPIO9/10阀) → 枕头高度调节
 *
 * 启动流程:
 *   1. 初始化 WiFi 子系统（不连接）
 *   2. 检查 NVS 是否有已保存的 WiFi 凭据
 *      - 有 → 直接连接 WiFi
 *      - 无 → 开启 BLE 广播，等待手机配网
 *   3. WiFi 连接成功 → 停止 BLE → 启动传感器任务
 *   4. WiFi 断开 → 重新开启 BLE 广播等待重新配网
 *
 * GPIO 分配总览:
 *   GPIO4/5/6   - FSR 压力传感器 ADC (Posture_Recognition)
 *   GPIO7/8     - 左/右气泵 MOS驱动 (airbag-hardware)
 *   GPIO9/10    - 左/右电磁阀 AO3400A (airbag-hardware)
 *   GPIO16/15/17 - INMP441 I2S 麦克风 (Snore_Det)
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "snore_feature.h"
#include "wifi_manager.h"
#include "wifi_provision.h"
#include "cloud_llm_client.h"
#include "cloud_upload.h"
#include "pump_controller.h"
#include "pump_rules.h"
#include "ble_uart_server.h"
#include "posture_sensor.h"
#include "snore_detector.h"
#include "sleep_session.h"

static const char *TAG = "MAIN";

/* ── 全局队列与事件组 ──────────────────────────────── */
QueueHandle_t g_feature_queue = NULL;   /* 组员写入 → cloud_task 读取 */
static QueueHandle_t s_cmd_queue = NULL; /* cloud_task 写入 → pump_task 读取 */
EventGroupHandle_t g_provision_event_group = NULL; /* WiFi 配网事件 */

/* ────────────────────────────────────────────────────
 *  WiFi 断开回调 — 重试耗尽后重新进入 BLE 配网模式
 * ──────────────────────────────────────────────────── */
static void on_wifi_disconnect(void)
{
    ESP_LOGW(TAG, "WiFi 连接丢失且重试耗尽，重新进入 BLE 配网模式");
    wifi_provision_clear_credentials();
    ble_uart_server_restart();
}

/* ────────────────────────────────────────────────────
 *  cloud_task — 睡眠会话管理 + 实时气泵 + 睡眠结束 LLM 分析
 * ──────────────────────────────────────────────────── */
static void cloud_task(void *arg)
{
    snore_features_t feat;
    pump_command_t cmd;

    ESP_LOGI(TAG, "cloud_task 已启动，等待传感器数据...");

    /* 初始化睡眠会话管理器 */
    session_init();

    while (1) {
        if (xQueueReceive(g_feature_queue, &feat, portMAX_DELAY) == pdTRUE) {
            ESP_LOGD(TAG, "收到传感器数据: posture=%s snore=%s %.2f分/时",
                     posture_name(feat.posture.posture),
                     feat.snore_detected ? "是" : "否",
                     feat.snore_minutes_per_hour);

            /* ── 1. 累积到睡眠会话 ───────────────── */
            session_on_data(&feat);

            /* ── 2. 实时气泵控制（本地规则，不调 LLM）── */
            memset(&cmd, 0, sizeof(cmd));
            pump_evaluate_local_rule(&feat, &cmd);

            if (strcmp(cmd.action, "hold") != 0) {
                ESP_LOGI(TAG, "🎮 气泵指令: %s %s (强度%d%%, %ds)",
                         cmd.action, cmd.zone, cmd.intensity, cmd.duration_sec);
                if (xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
                    ESP_LOGW(TAG, "气泵指令队列已满，丢弃本次指令");
                }
            }

            /* ── 3. 检查睡眠是否结束 ─────────────── */
            if (session_is_ended()) {
                ESP_LOGI(TAG, "═══════════════════════════════════════");
                ESP_LOGI(TAG, "💤 睡眠会话结束！开始生成分析报告...");

                sleep_session_summary_t summary = session_get_summary();

                ESP_LOGI(TAG, "  时长: %d 分钟", summary.duration_minutes);
                ESP_LOGI(TAG, "  起身: %d 次", summary.get_up_count);
                ESP_LOGI(TAG, "  鼾声: %.1f 分钟 (%.2f 分钟/时)",
                         summary.total_snore_minutes,
                         summary.snore_minutes_per_hour);
                ESP_LOGI(TAG, "  评分: %d", summary.sleep_score);
                ESP_LOGI(TAG, "  主要睡姿: %s",
                         posture_name(summary.dominant_posture));

                /* ── 调用 LLM 生成整晚分析报告 ──── */
                char report[512] = {0};
                int ret = cloud_llm_analyze(&summary.last_features,
                                            report, sizeof(report), &cmd);
                if (ret == 0) {
                    ESP_LOGI(TAG, "📋 AI 分析报告: %s", report);
                } else {
                    ESP_LOGW(TAG, "LLM 分析失败 (err=%d)，使用默认报告", ret);
                    snprintf(report, sizeof(report),
                             "睡眠时长%d分钟，起身%d次，鼾声%.1f分钟，评分%d分。",
                             summary.duration_minutes,
                             summary.get_up_count,
                             summary.total_snore_minutes,
                             summary.sleep_score);
                }

                /* ── 上传到 CloudBase ─────────── */
                ESP_LOGI(TAG, "☁️  上传睡眠数据到云端...");
                int upload_ret = cloud_upload_sleep_record(&summary, report);
                if (upload_ret == 0) {
                    ESP_LOGI(TAG, "✅ 云端上传成功");
                } else {
                    ESP_LOGE(TAG, "❌ 云端上传失败 (err=%d)", upload_ret);
                }

                ESP_LOGI(TAG, "═══════════════════════════════════════");

                /* 重置会话，等待下一次睡眠 */
                session_reset();
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

/*
 * The snore module will later replace the zero-valued snore fields with its
 * five-second aggregation. Until then, every verified posture frame still
 * reaches the session manager through the same production queue.
 */
static void feature_aggregator_task(void *arg)
{
    posture_data_t posture;
    snore_reading_t latest_snore = { .window_seconds = 2.04f };
    bool has_snore_window = false;
    bool was_on_pillow = false;
    int window_count = 0;
    int positive_window_count = 0;
    float probability_sum = 0.0f;
    float max_probability = 0.0f;

    while (true) {
        if (!posture_sensor_receive(&posture, portMAX_DELAY)) {
            continue;
        }

        snore_reading_t reading;
        while (snore_detector_receive(&reading, 0)) {
            latest_snore = reading;
            has_snore_window = true;
            window_count++;
            probability_sum += reading.probability;
            if (reading.probability > max_probability) {
                max_probability = reading.probability;
            }
            if (reading.detected) {
                positive_window_count++;
            }
        }

        const bool on_pillow = posture.total_pressure >= 50.0f;
        if (on_pillow && !was_on_pillow) {
            window_count = 0;
            positive_window_count = 0;
            probability_sum = 0.0f;
            max_probability = 0.0f;
            has_snore_window = false;
        }
        was_on_pillow = on_pillow;

        const float total_window_seconds = window_count * latest_snore.window_seconds;
        const float positive_duration_seconds =
            positive_window_count * latest_snore.window_seconds;
        const float positive_duration_minutes = positive_duration_seconds / 60.0f;
        const float elapsed_hours = total_window_seconds / 3600.0f;

        snore_features_t feature = {
            .window_seconds = latest_snore.window_seconds,
            .hop_seconds = latest_snore.window_seconds,
            .decision_threshold = 0.5f,
            .window_count = window_count,
            .mean_probability = window_count > 0 ? probability_sum / window_count : 0.0f,
            .max_probability = max_probability,
            .positive_window_count = positive_window_count,
            .positive_window_ratio = window_count > 0
                ? (float)positive_window_count / window_count : 0.0f,
            .positive_duration_seconds = positive_duration_seconds,
            .positive_duration_minutes = positive_duration_minutes,
            .snore_detected = has_snore_window && latest_snore.detected,
            .snore_minutes_per_hour = elapsed_hours > 0.0f
                ? positive_duration_minutes / elapsed_hours : 0.0f,
            .posture = posture,
            .timestamp_ms = esp_timer_get_time() / 1000,
        };

        if (xQueueSend(g_feature_queue, &feature, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "Feature queue full; posture frame dropped");
        }
    }
}

/* ────────────────────────────────────────────────────
 *  ble_provision_loop — BLE 配网等待循环
 *
 *  阻塞等待手机通过 BLE 发送 WiFi 凭据，
 *  收到后尝试连接 WiFi，失败则回复错误并继续等待。
 *  返回 ESP_OK 表示 WiFi 已成功连接。
 * ──────────────────────────────────────────────────── */
static esp_err_t ble_provision_loop(void)
{
    char ssid[33] = {0};
    char pass[65] = {0};

    while (1) {
        ESP_LOGI(TAG, "📡 等待手机通过 BLE 发送 WiFi 配置...");

        /* 阻塞等待凭据到来 */
        xEventGroupWaitBits(g_provision_event_group,
                            PROVISION_CRED_RECEIVED_BIT,
                            pdTRUE,   /* 收到后清除 bit */
                            pdFALSE,
                            portMAX_DELAY);

        ESP_LOGI(TAG, "收到 WiFi 凭据，正在连接...");

        /* 读取凭据 */
        if (wifi_provision_load_credentials(ssid, sizeof(ssid),
                                            pass, sizeof(pass)) != ESP_OK) {
            ESP_LOGE(TAG, "读取 NVS 凭据失败");
            continue;
        }

        /* 尝试连接 */
        esp_err_t ret = wifi_manager_connect(ssid, pass);
        if (ret == ESP_OK) {
            /* 连接成功，通知手机 */
            const char *ok_msg = "{\"status\":\"ok\",\"msg\":\"wifi_connected\"}";
            ble_uart_send(ok_msg, strlen(ok_msg));
            return ESP_OK;
        }

        /* 连接失败，通知手机 */
        ESP_LOGW(TAG, "WiFi 连接失败，等待重新配网...");
        const char *err_msg = "{\"status\":\"error\",\"msg\":\"connect_failed\"}";
        ble_uart_send(err_msg, strlen(err_msg));
        wifi_provision_clear_credentials();
        /* 继续等待下一次配网 */
    }
}

/* ────────────────────────────────────────────────────
 *  app_main — 系统入口
 * ──────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  ESP32 智能防鼾睡姿调节系统 v5.0    ║");
    ESP_LOGI(TAG, "╠══════════════════════════════════════╣");
    ESP_LOGI(TAG, "║  配网: BLE WiFi Provisioning        ║");
    ESP_LOGI(TAG, "║  鼾声: Snore_Det (INMP441)          ║");
    ESP_LOGI(TAG, "║  睡姿: Posture_Recognition (FSR×3)  ║");
    ESP_LOGI(TAG, "║  控制: 本地规则实时气泵             ║");
    ESP_LOGI(TAG, "║  分析: 睡眠结束后 LLM (VolcEngine)  ║");
    ESP_LOGI(TAG, "║  上云: 腾讯云 CloudBase              ║");
    ESP_LOGI(TAG, "║  执行: airbag-hardware (双气囊)     ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════╝");
    ESP_LOGI(TAG, "");

    ESP_LOGI(TAG, "GPIO 分配:");
    ESP_LOGI(TAG, "  传感: FSR ADC=GPIO4/5/6, INMP441 I2S=GPIO16/15/17");
    ESP_LOGI(TAG, "  执行: 泵=GPIO7/8, 阀=GPIO9/10");

    /* ── 1. 初始化 WiFi 子系统（不连接）────────────── */
    ESP_LOGI(TAG, "初始化 WiFi 子系统...");
    esp_err_t ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 子系统初始化失败！");
        return;
    }
    wifi_manager_set_disconnect_cb(on_wifi_disconnect);

    /* ── 2. 初始化气泵控制器 ──────────────────────── */
    pump_controller_init();
    ESP_LOGI(TAG, "✅ 双气囊控制器已初始化");

    /* ── 3. 创建配网事件组 ────────────────────────── */
    g_provision_event_group = xEventGroupCreate();
    if (!g_provision_event_group) {
        ESP_LOGE(TAG, "配网事件组创建失败！");
        return;
    }

    /* ── 4. 尝试使用 NVS 中已保存的 WiFi 凭据 ───── */
    bool wifi_connected = false;

    if (wifi_provision_has_credentials()) {
        char ssid[33] = {0};
        char pass[65] = {0};

        ESP_LOGI(TAG, "NVS 中找到已保存的 WiFi 凭据，尝试连接...");
        if (wifi_provision_load_credentials(ssid, sizeof(ssid),
                                            pass, sizeof(pass)) == ESP_OK) {
            ret = wifi_manager_connect(ssid, pass);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ WiFi 连接成功（使用已保存的凭据）");
                wifi_connected = true;
            } else {
                ESP_LOGW(TAG, "已保存的凭据连接失败，清除并进入 BLE 配网模式");
                wifi_provision_clear_credentials();
            }
        }
    }

    /* ── 5. 如果需要 BLE 配网 ────────────────────── */
    if (!wifi_connected) {
        ESP_LOGI(TAG, "进入 BLE 配网模式...");
        ble_uart_server_init();
        ESP_LOGI(TAG, "✅ BLE 广播已开启 (设备名: SleepMonitor)");

        ret = ble_provision_loop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "BLE 配网失败！");
            return;
        }
        ESP_LOGI(TAG, "✅ WiFi 连接成功（通过 BLE 配网）");
    }

    /* ── 6. WiFi 已连接 → 停止 BLE，启动工作任务 ── */
    ble_uart_server_stop();
    ESP_LOGI(TAG, "🔇 BLE 已停止（节省功耗）");

    /* 创建队列 */
    g_feature_queue = xQueueCreate(4, sizeof(snore_features_t));
    s_cmd_queue     = xQueueCreate(4, sizeof(pump_command_t));
    if (!g_feature_queue || !s_cmd_queue) {
        ESP_LOGE(TAG, "队列创建失败！");
        return;
    }

    /* 启动工作任务 */
    xTaskCreate(cloud_task, "cloud", 16384, NULL, 3, NULL);
    xTaskCreate(pump_task,  "pump",  4096,  NULL, 4, NULL);
    ret = posture_sensor_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FSR posture sensor initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = snore_detector_start();
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Snore model unavailable; running posture-only mode");
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Snore detector initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    xTaskCreate(feature_aggregator_task, "feature_aggregator", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "✅ cloud, pump, posture, and feature tasks started");

    /*
     * 传感器数据来源 (两人分工):
     *
     * [鼾声组员] — Snore_Det 分支
     *   INMP441 麦克风 (GPIO16/15/17) → 模型推理 → probability
     *   每 5 秒汇总一次，累积到 summary 统计
     *
     * [睡姿组员] — Posture_Recognition 分支
     *   FSR×3 压力传感器 (GPIO4/5/6) → 中值滤波 → 规则分类
     *   每秒读取并分类出 posture + confidence
     *
     * [合并发送]:
     *   #include "snore_feature.h"
     *   extern QueueHandle_t g_feature_queue;
     *
     *   snore_features_t feat = { ... };
     *   xQueueSend(g_feature_queue, &feat, portMAX_DELAY);
     */

    ESP_LOGI(TAG, "🚀 系统已就绪！等待传感器数据...");
}
