/**
 * sleep_session.c — 睡眠会话管理实现
 *
 * 跟踪一次完整睡眠（从头部放上枕头到离开），累积鼾声和姿态统计。
 * 压力从有→无，立刻判定睡眠结束，触发 LLM 分析和上云。
 */

#include <string.h>
#include "sleep_session.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "SESSION";

/* ── 内部状态 ────────────────────────────────────────── */

/** 会话是否已经开始（至少检测到一次压力） */
static bool     s_active;
/** 会话是否已结束（压力从有→无） */
static bool     s_ended;

/** 当前是否有压力（人在枕上） */
static bool     s_pressure_present;

/** 上床时间（毫秒） */
static int64_t  s_bed_time_ms;
/** 离床时间（毫秒） */
static int64_t  s_wake_time_ms;
/** 最后一次数据到达的时间（毫秒） */
static int64_t  s_last_data_ms;
static int64_t  s_last_pressure_ms;
static int64_t  s_pressure_lost_ms;
static int      s_get_up_count;

/* ── 鼾声累积 ────────────────────────────────────────── */

/** 累计鼾声时长（分钟），来自 feat->positive_duration_minutes */
static float    s_total_snore_minutes;
/** 最大鼾声概率 */
static float    s_max_snore_prob;
/** 鼾声概率累加（用于计算均值） */
static double   s_sum_snore_prob;
/** 数据采样次数（用于计算鼾声概率均值） */
static int      s_sample_count;
/** 鼾声事件计数（snore_detected == true 的采样数） */
static int      s_snore_event_count;
/** 最大 RMS 分贝值 */
static float    s_max_rms_db;
/** 鼾声帧 RMS 分贝累加值和计数 */
static double   s_sum_rms_db;
static int      s_rms_db_count;

/* ── 姿态累积 ────────────────────────────────────────── */

/** 各姿态累计秒数，索引对应 posture_t */
static int      s_posture_seconds[POSTURE_COUNT];
/** 姿态变化次数 */
static int      s_posture_change_count;
/** 上一次有效姿态（用于检测翻身） */
static posture_t s_last_posture;
/** 是否已经记录过第一次姿态 */
static bool     s_has_last_posture;

/** 最后一帧特征数据 */
static snore_features_t s_last_features;

/** 起身事件记录 */
static struct {
    int64_t leave_ms;
    int64_t return_ms;
} s_get_up_events[MAX_GET_UP_EVENTS];

/* ── 辅助函数 ────────────────────────────────────────── */

/** 获取当前时间（毫秒） */
static inline int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/** 计算总压力值 */
static inline float total_pressure(const snore_features_t *feat)
{
    return feat->posture.total_pressure;
}

/** 计算睡眠评分（简化版，对齐 Flutter sleep_record_generator.dart） */
static int compute_sleep_score(const sleep_session_summary_t *s)
{
    int score = 85;

    /* 时长评分 */
    float hours = s->duration_minutes / 60.0f;
    if (hours >= 7.0f && hours <= 9.0f) {
        score += 5;
    } else if (hours < 6.0f) {
        score -= 10;
    } else if (hours < 7.0f) {
        score -= 5;
    }
    /* 9h 以上不加不减 */

    /* 鼾声扣分: -3 per 10min */
    int snore_penalty = (int)(s->total_snore_minutes / 10.0f) * 3;
    score -= snore_penalty;
    if (s->mean_snore_probability > 0.7f) {
        score -= 5;
    }

    /* 翻身过多扣分 */
    if (s->posture_change_count > 20) {
        score -= 5;
    }

    /* 钳位 [30, 98] */
    if (score < 30) score = 30;
    if (score > 98) score = 98;

    return score;
}

/* ── 公共 API ────────────────────────────────────────── */

void session_init(void)
{
    session_reset();
    ESP_LOGI(TAG, "Session manager initialized");
}

void session_on_data(const snore_features_t *feat)
{
    int64_t now = now_ms();
    bool pressure_now = total_pressure(feat) > PRESSURE_PRESENT_THRESHOLD;

    /* 保存最后一帧 */
    s_last_features = *feat;
    s_last_data_ms  = now;

    /* ── 状态机 ──────────────────────────────────────── */

    if (!s_active) {
        /* 未启动会话 */
        if (pressure_now) {
            /* 压力从无→有: 开始新会话 */
            s_active           = true;
            s_ended            = false;
            s_pressure_present = true;
            s_bed_time_ms      = now;
            s_last_pressure_ms = now;
            s_pressure_lost_ms = 0;
            s_has_last_posture = false;
            ESP_LOGI(TAG, "Session started — bed_time=%lld ms, pressure=%.1f",
                     (long long)s_bed_time_ms, total_pressure(feat));
        }
        return;   /* 无压力且无会话，忽略 */
    }

    /* 会话已结束，不再处理 */
    if (s_ended) {
        return;
    }

    /* ── 活跃会话处理 ────────────────────────────────── */

    if (!pressure_now) {
        if (s_pressure_present) {
            s_pressure_present = false;
            s_pressure_lost_ms = now;
            ESP_LOGI(TAG, "Pressure lost; waiting before ending session");
        }

        if (now - s_pressure_lost_ms >= SESSION_END_TIMEOUT_MS) {
            s_wake_time_ms = s_last_pressure_ms;
            s_ended = true;
            ESP_LOGI(TAG, "Session ended after %.1f min without pressure",
                     (now - s_pressure_lost_ms) / 60000.0f);
        }
        return;
    }

    if (!s_pressure_present) {
        const int64_t absence_ms = now - s_pressure_lost_ms;
        if (absence_ms >= SESSION_GET_UP_TIMEOUT_MS) {
            s_get_up_count++;
            if (s_get_up_count <= MAX_GET_UP_EVENTS) {
                int idx = s_get_up_count - 1;
                s_get_up_events[idx].leave_ms  = s_pressure_lost_ms;
                s_get_up_events[idx].return_ms = now;
            }
        }
        s_pressure_present = true;
        s_pressure_lost_ms = 0;
        ESP_LOGI(TAG, "Pressure restored after %.1f s", absence_ms / 1000.0f);
    }

    s_last_pressure_ms = now;

    /* ── 压力存在时累积数据 ──────────────────────────── */

    s_sample_count++;

    /* 姿态累积（传感器 1 Hz，每次约 1 秒） */
    posture_t cur_posture = feat->posture.posture;
    if (cur_posture >= 0 && cur_posture < POSTURE_COUNT) {
        s_posture_seconds[cur_posture]++;
    }

    /* 检测翻身 */
    if (s_has_last_posture) {
        if (cur_posture != s_last_posture
            && cur_posture != POSTURE_NO_HEAD
            && cur_posture != POSTURE_MOVING
            && s_last_posture != POSTURE_NO_HEAD
            && s_last_posture != POSTURE_MOVING) {
            s_posture_change_count++;
        }
    }
    s_last_posture     = cur_posture;
    s_has_last_posture = true;

    /* 鼾声累积 */
    float prob = feat->mean_probability;
    s_sum_snore_prob += prob;
    if (prob > s_max_snore_prob) {
        s_max_snore_prob = prob;
    }
    if (feat->snore_detected) {
        s_snore_event_count++;
    }
    s_total_snore_minutes = feat->positive_duration_minutes;
    if (feat->max_rms_db > s_max_rms_db) {
        s_max_rms_db = feat->max_rms_db;
    }
    if (feat->snore_detected && feat->latest_rms_db > 0.0f) {
        s_sum_rms_db += feat->latest_rms_db;
        s_rms_db_count++;
    }
}

bool session_is_ended(void)
{
    return s_ended;
}

bool session_is_active(void)
{
    return s_active && !s_ended;
}

bool session_is_reportable(void)
{
    return s_ended &&
           (s_last_pressure_ms - s_bed_time_ms) >= SESSION_MIN_REPORT_MS;
}

sleep_session_summary_t session_get_summary(void)
{
    sleep_session_summary_t summary;
    memset(&summary, 0, sizeof(summary));

    summary.bed_time_ms  = s_bed_time_ms;
    summary.wake_time_ms = s_wake_time_ms > 0 ? s_wake_time_ms : s_last_data_ms;
    summary.get_up_count = s_get_up_count;

    /* 时长 */
    int64_t duration_ms  = summary.wake_time_ms - summary.bed_time_ms;
    summary.duration_minutes = (int)(duration_ms / 60000);

    /* 鼾声统计 */
    summary.total_snore_minutes  = s_total_snore_minutes;
    summary.max_snore_probability = s_max_snore_prob;
    summary.mean_snore_probability = (s_sample_count > 0)
        ? (float)(s_sum_snore_prob / s_sample_count)
        : 0.0f;
    summary.snore_event_count = s_snore_event_count;
    summary.max_rms_db = s_max_rms_db;
    summary.mean_rms_db = (s_rms_db_count > 0)
        ? (float)(s_sum_rms_db / s_rms_db_count)
        : 0.0f;

    float duration_hours = summary.duration_minutes / 60.0f;
    summary.snore_minutes_per_hour = (duration_hours > 0.0f)
        ? s_total_snore_minutes / duration_hours
        : 0.0f;

    /* 姿态分布 */
    memcpy(summary.posture_seconds, s_posture_seconds, sizeof(s_posture_seconds));
    summary.posture_change_count = s_posture_change_count;

    /* 主要姿态 = 累计秒数最多的 */
    int max_sec = 0;
    summary.dominant_posture = POSTURE_NO_HEAD;
    for (int i = 0; i < POSTURE_COUNT; i++) {
        if (s_posture_seconds[i] > max_sec) {
            max_sec = s_posture_seconds[i];
            summary.dominant_posture = (posture_t)i;
        }
    }

    /* 最后一帧 */
    summary.last_features = s_last_features;

    /* 起身事件 */
    int event_count = s_get_up_count < MAX_GET_UP_EVENTS
                    ? s_get_up_count : MAX_GET_UP_EVENTS;
    summary.get_up_event_count = event_count;
    for (int i = 0; i < event_count; i++) {
        summary.get_up_events[i].leave_ms  = s_get_up_events[i].leave_ms;
        summary.get_up_events[i].return_ms = s_get_up_events[i].return_ms;
    }

    /* 睡眠评分 */
    summary.sleep_score = compute_sleep_score(&summary);

    return summary;
}

void session_reset(void)
{
    s_active           = false;
    s_ended            = false;
    s_pressure_present = false;
    s_bed_time_ms      = 0;
    s_wake_time_ms     = 0;
    s_last_data_ms     = 0;
    s_last_pressure_ms = 0;
    s_pressure_lost_ms = 0;
    s_get_up_count     = 0;

    s_total_snore_minutes = 0.0f;
    s_max_snore_prob      = 0.0f;
    s_sum_snore_prob      = 0.0;
    s_sample_count        = 0;
    s_snore_event_count   = 0;
    s_max_rms_db          = 0.0f;
    s_sum_rms_db          = 0.0;
    s_rms_db_count        = 0;

    memset(s_posture_seconds, 0, sizeof(s_posture_seconds));
    s_posture_change_count = 0;
    s_last_posture         = POSTURE_NO_HEAD;
    s_has_last_posture     = false;

    memset(&s_last_features, 0, sizeof(s_last_features));
    memset(s_get_up_events, 0, sizeof(s_get_up_events));

    ESP_LOGI(TAG, "Session reset");
}
