#ifndef SNORE_FEATURE_H
#define SNORE_FEATURE_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * 睡姿分类枚举 — 对齐 Posture_Recognition 分支
 *
 * 来源: posture_recognition.ino 的 classifyPosture()
 *   - 3 个 FSR 压力传感器 (GPIO4/5/6 ADC)
 *   - 31 样本中值滤波 + 规则分类
 *   - 1 Hz CSV 输出
 */
typedef enum {
    POSTURE_NO_HEAD    = 0,   /* 头不在枕上 */
    POSTURE_MOVING     = 1,   /* 翻身中 (窗口不稳定) */
    POSTURE_LEFT_SIDE  = 2,   /* 左侧卧 */
    POSTURE_RIGHT_SIDE = 3,   /* 右侧卧 */
    POSTURE_SUPINE     = 4,   /* 仰卧 */
    POSTURE_COUNT      = 5,
} posture_t;

/**
 * 睡姿数据 — 从 Posture_Recognition 分支获取
 *
 * Field names and ordering match the 14-column Posture_Recognition CSV.
 */
typedef struct {
    int       raw_left;          /* FSR 左 ADC 原始值 (0-4095) */
    int       raw_center;        /* FSR 中 ADC 原始值 */
    int       raw_right;         /* FSR 右 ADC 原始值 */
    float     median_left;       /* Baseline-subtracted median pressure */
    float     median_center;
    float     median_right;
    float     total_pressure;
    float     left_ratio;
    float     center_ratio;
    float     right_ratio;
    float     x_center_cm;       /* Pressure center: negative left, positive right */
    bool      moving;
    posture_t posture;
    float     confidence;
} posture_data_t;

/**
 * 鼾声模型输出结果 — 严格对齐 snore_model_output.template.json
 *
 * 数据来源:
 *   - Snore_Det 分支: INMP441 麦克风 (GPIO16/15/17 I2S)
 *     → PhysicsSnoreEdgeModel 推理 → probability per 5s window
 *   - windows[].probability 是模型直接输出（唯一核心字段）
 *   - summary 所有字段都是基于 probability + threshold 直接计算
 *   - decision_threshold: softmax snore probability >= 0.5
 */
typedef struct {
    /* ── 顶层计算参数 ─────────────────────────── */
    float window_seconds;             /* 窗口时长 (秒), 固定 5.0 */
    float hop_seconds;                /* 步长 (秒), 固定 5.0 */
    float decision_threshold;         /* 阈值, 0.44~0.46 */

    /* ── summary 字段（与 JSON 完全一致）──────── */
    int   window_count;               /* 窗口总数 */
    float mean_probability;           /* 所有窗口平均鼾声概率 */
    float max_probability;            /* 所有窗口最大鼾声概率 */
    int   positive_window_count;      /* probability >= threshold 的窗口数 */
    float positive_window_ratio;      /* 正窗占比 */
    float positive_duration_seconds;  /* 正窗累计时长 (秒) */
    float positive_duration_minutes;  /* 正窗累计时长 (分钟) */
    bool  snore_detected;             /* 是否至少存在一个正窗 */
    float snore_minutes_per_hour;     /* 平均每小时鼾声分钟数 */

    /* ── 睡姿数据 (来自 Posture_Recognition) ──── */
    posture_data_t posture;           /* 当前睡姿 */

    /* ── 时间戳 (ESP32 附加) ──────────────────── */
    int64_t timestamp_ms;             /* ESP32 采集时间戳 (毫秒) */
} snore_features_t;

/**
 * 全局特征队列
 *
 * 组员发送方式:
 *   #include "snore_feature.h"
 *   extern QueueHandle_t g_feature_queue;
 *
 *   snore_features_t feat = {
 *       // 鼾声数据 (Snore_Det 推理结果汇总)
 *       .window_seconds         = 5.0,
 *       .hop_seconds            = 5.0,
 *       .decision_threshold     = 0.44,
 *       .window_count           = ...,
 *       .mean_probability       = ...,
 *       .max_probability        = ...,
 *       .positive_window_count  = ...,
 *       .positive_window_ratio  = ...,
 *       .positive_duration_seconds = ...,
 *       .positive_duration_minutes = ...,
 *       .snore_detected         = ...,
 *       .snore_minutes_per_hour = ...,
 *       // 睡姿数据 (Posture_Recognition 最新一帧)
 *       .posture = {
 *           .raw_left = 320, .raw_center = 650, .raw_right = 310,
 *           .median_left = 315.0f, .median_center = 645.0f,
 *           .median_right = 305.0f, .total_pressure = 1265.0f,
 *           .left_ratio = 0.249f, .center_ratio = 0.510f,
 *           .right_ratio = 0.241f, .x_center_cm = 0.12f,
 *           .moving = false, .posture = POSTURE_SUPINE,
 *           .confidence = 0.85f,
 *       },
 *   };
 *   xQueueSend(g_feature_queue, &feat, portMAX_DELAY);
 */
extern QueueHandle_t g_feature_queue;

/**
 * 睡姿名称字符串 (用于日志和 LLM 输入)
 */
static inline const char *posture_name(posture_t p) {
    switch (p) {
        case POSTURE_NO_HEAD:    return "NO_HEAD";
        case POSTURE_MOVING:     return "MOVING";
        case POSTURE_LEFT_SIDE:  return "LEFT_SIDE";
        case POSTURE_RIGHT_SIDE: return "RIGHT_SIDE";
        case POSTURE_SUPINE:     return "SUPINE";
        default:                 return "NO_HEAD";
    }
}

/**
 * 睡姿中文名 (用于 LLM prompt)
 */
static inline const char *posture_name_cn(posture_t p) {
    switch (p) {
        case POSTURE_NO_HEAD:    return "头不在枕上";
        case POSTURE_MOVING:     return "翻身中";
        case POSTURE_LEFT_SIDE:  return "左侧卧";
        case POSTURE_RIGHT_SIDE: return "右侧卧";
        case POSTURE_SUPINE:     return "仰卧";
        default:                 return "头不在枕上";
    }
}

#endif /* SNORE_FEATURE_H */
