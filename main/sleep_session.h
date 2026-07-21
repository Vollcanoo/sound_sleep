/**
 * sleep_session.h — 睡眠会话管理
 *
 * 管理一次完整睡眠会话（从头部放上枕头到最终离开）的数据累积和结束检测。
 *
 * 工作原理:
 *   - 每次收到传感器数据时调用 session_on_data()
 *   - 通过压力传感器总值判断人是否在枕上 (> PRESSURE_PRESENT_THRESHOLD)
 *   - 压力从无→有: 开始新会话
 *   - 压力持续: 累积鼾声/姿态统计
 *   - 压力短暂消失后恢复: 计为一次"起身"
 *   - 压力长时间消失 (> SESSION_END_TIMEOUT_MS): 判定睡眠结束
 */
#ifndef SLEEP_SESSION_H
#define SLEEP_SESSION_H

#include <stdbool.h>
#include <stdint.h>
#include "snore_feature.h"

/* 压力检测阈值 — 较低值确保轻压力也能检测到 */
#define PRESSURE_PRESENT_THRESHOLD  50
#define SESSION_GET_UP_TIMEOUT_MS   (15 * 1000)
#define SESSION_END_TIMEOUT_MS      (5 * 60 * 1000)
#define SESSION_MIN_REPORT_MS       (5 * 60 * 1000)

/**
 * 睡眠会话汇总数据 — 用于 LLM prompt 和云端上传
 */
typedef struct {
    int64_t  bed_time_ms;           /* 首次检测到压力（上床）*/
    int64_t  wake_time_ms;          /* 最终压力消失（离床）*/
    int      duration_minutes;      /* 总时长（分钟）*/
    int      get_up_count;          /* 起身次数 */
    int      sleep_score;           /* 睡眠评分 0-100 */

    /* 鼾声统计 */
    float    total_snore_minutes;   /* 累计鼾声时长（分钟）*/
    float    max_snore_probability; /* 最大鼾声概率 */
    float    mean_snore_probability;/* 平均鼾声概率 */
    int      snore_event_count;     /* 鼾声事件次数 */
    float    snore_minutes_per_hour;/* 每小时鼾声分钟数 */

    /* 姿态分布 */
    int      posture_seconds[POSTURE_COUNT]; /* Seconds per supported posture */
    int      posture_change_count;  /* 姿态变化次数（翻身）*/
    posture_t dominant_posture;     /* 占比最大的姿态 */

    /* 最后一次采样的原始特征（参考用）*/
    snore_features_t last_features;
} sleep_session_summary_t;

/**
 * 初始化睡眠会话管理器（清空所有状态）
 */
void session_init(void);

/**
 * 每次收到传感器数据时调用。
 * 内部自动管理会话状态（开始/累积/起身/结束检测）。
 *
 * @param feat  传感器综合数据
 */
void session_on_data(const snore_features_t *feat);

/**
 * 检查当前会话是否已结束（压力消失超过 SESSION_END_TIMEOUT_MS）
 */
bool session_is_ended(void);

/**
 * 检查当前是否有活跃的睡眠会话
 */
bool session_is_active(void);

/** Returns true only for a long enough completed session. */
bool session_is_reportable(void);

/**
 * 获取当前会话的汇总数据（用于 LLM 分析和云端上传）。
 * 仅在 session_is_ended() 返回 true 后调用有意义。
 */
sleep_session_summary_t session_get_summary(void);

/**
 * 重置会话（清空所有累积数据，准备下一次睡眠）
 */
void session_reset(void);

#endif /* SLEEP_SESSION_H */
