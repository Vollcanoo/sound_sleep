#ifndef SNORE_FEATURE_H
#define SNORE_FEATURE_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * 鼾声模型输出结果 — 严格对齐 snore_model_output.template.json
 *
 * 数据来源原则 (参见 esp32_snore_field_source_map.md):
 *   - windows[].probability 是模型直接输出（唯一核心字段）
 *   - summary 所有字段都是基于 probability + threshold 直接计算
 *   - 不包含设备信息、患者ID、文件路径等业务字段
 *
 * ESP32 端只传 summary 给 LLM (windows 数组太大不适合传)
 */
typedef struct {
    /* ── 顶层计算参数 ─────────────────────────── */
    float window_seconds;             // 窗口时长 (秒), 固定 5.0
    float hop_seconds;                // 步长 (秒), 固定 5.0
    float decision_threshold;         // 阈值, 固定 0.46

    /* ── summary 字段（与 JSON 完全一致）──────── */
    int   window_count;               // 窗口总数
    float mean_probability;           // 所有窗口平均鼾声概率
    float max_probability;            // 所有窗口最大鼾声概率
    int   positive_window_count;      // probability >= threshold 的窗口数
    float positive_window_ratio;      // 正窗占比
    float positive_duration_seconds;  // 正窗累计时长 (秒)
    float positive_duration_minutes;  // 正窗累计时长 (分钟)
    bool  snore_detected;             // 是否至少存在一个正窗
    float snore_minutes_per_hour;     // 平均每小时鼾声分钟数

    /* ── 时间戳 (ESP32 附加) ──────────────────── */
    int64_t timestamp_ms;             // ESP32 采集时间戳 (毫秒)
} snore_features_t;

/**
 * 全局特征队列
 *
 * 组员发送 (从 snore_model_output.template.json 填入):
 *   snore_features_t feat = {
 *       .window_seconds         = 5.0,
 *       .hop_seconds            = 5.0,
 *       .decision_threshold     = 0.46,
 *       .window_count           = summary.window_count,
 *       .mean_probability       = summary.mean_probability,
 *       .max_probability        = summary.max_probability,
 *       .positive_window_count  = summary.positive_window_count,
 *       .positive_window_ratio  = summary.positive_window_ratio,
 *       .positive_duration_seconds = summary.positive_duration_seconds,
 *       .positive_duration_minutes = summary.positive_duration_minutes,
 *       .snore_detected         = summary.snore_detected,
 *       .snore_minutes_per_hour = summary.snore_minutes_per_hour,
 *   };
 *   xQueueSend(g_feature_queue, &feat, portMAX_DELAY);
 */
extern QueueHandle_t g_feature_queue;

#endif /* SNORE_FEATURE_H */
