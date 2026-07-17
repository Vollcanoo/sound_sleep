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
    POSTURE_PRONE      = 5,   /* 俯卧 (识别较弱, 置信度 ~0.45) */
    POSTURE_UNCERTAIN  = 6,   /* 无法判定 */
} posture_t;

/**
 * 睡姿数据 — 从 Posture_Recognition 分支获取
 */
typedef struct {
    posture_t posture;           /* 睡姿分类 */
    float     confidence;        /* 分类置信度 0.0-1.0 */
    float     x_center_cm;       /* 压力重心 X 偏移 (cm), 负=偏左, 正=偏右 */
    float     y_center_cm;       /* 压力重心 Y 偏移 (cm), 正=偏头侧, 负=偏肩侧 */
} posture_data_t;

/**
 * 鼾声模型输出结果 — 严格对齐 snore_model_output.template.json
 *
 * 数据来源:
 *   - Snore_Det_esp 分支: INMP441 麦克风 (GPIO14/15/32 I2S)
 *     → PhysicsSnoreEdgeModel 推理 → probability per 5s window
 *   - windows[].probability 是模型直接输出（唯一核心字段）
 *   - summary 所有字段都是基于 probability + threshold 直接计算
 *   - decision_threshold: Snore_Det_esp 实测 ≈0.44, 训练端定为 0.46
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
 *       // 鼾声数据 (Snore_Det_esp 推理结果汇总)
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
 *           .posture    = POSTURE_SUPINE,
 *           .confidence = 0.85,
 *           .x_center_cm = 0.12,
 *           .y_center_cm = 0.31,
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
        case POSTURE_PRONE:      return "PRONE";
        default:                 return "UNCERTAIN";
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
        case POSTURE_PRONE:      return "俯卧";
        default:                 return "未知";
    }
}

#endif /* SNORE_FEATURE_H */
