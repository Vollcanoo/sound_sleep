/**
 * pump_rules.c — 本地气泵决策规则
 *
 * 将 cloud_llm_client.c 的 LLM system prompt 中的决策规则
 * 硬编码为 C 函数，用于实时气泵控制（不调用 LLM API）。
 *
 * 数据来源:
 *   - 鼾声: Snore_Det_esp 分支 (INMP441 → PhysicsSnoreEdgeModel → probability)
 *   - 睡姿: Posture_Recognition 分支 (FSR×3 → 规则分类 → posture + confidence)
 *   - 气泵: airbag-hardware 分支 (左/右独立气泵+电磁阀, GPIO7/8/9/10)
 */
#include <string.h>
#include "esp_log.h"
#include "pump_rules.h"

static const char *TAG = "PUMP_RULE";

/* ── 鼾声严重程度分级 ─────────────────────────────────── */
typedef enum {
    SNORE_MILD,           /* < 2 分钟/时 */
    SNORE_MODERATE,       /* 2 ~ 4 分钟/时 */
    SNORE_SEVERE,         /* >= 4 分钟/时 */
    SNORE_VERY_SEVERE,    /* max_prob > 0.9 && ratio > 0.1 */
} snore_severity_t;

/* ── 辅助: 设置 hold 指令 ─────────────────────────────── */
static void cmd_hold(pump_command_t *cmd)
{
    strncpy(cmd->action, "hold", sizeof(cmd->action) - 1);
    cmd->action[sizeof(cmd->action) - 1] = '\0';
    strncpy(cmd->zone, "both", sizeof(cmd->zone) - 1);
    cmd->zone[sizeof(cmd->zone) - 1] = '\0';
    cmd->intensity    = 0;
    cmd->duration_sec = 0;
}

/* ── 辅助: 设置 inflate 指令 ──────────────────────────── */
static void cmd_inflate(pump_command_t *cmd, const char *zone, int intensity)
{
    strncpy(cmd->action, "inflate", sizeof(cmd->action) - 1);
    cmd->action[sizeof(cmd->action) - 1] = '\0';
    strncpy(cmd->zone, zone, sizeof(cmd->zone) - 1);
    cmd->zone[sizeof(cmd->zone) - 1] = '\0';
    cmd->intensity = intensity;
    /* duration_sec 按 intensity 比例在 5-20 秒区间调节 */
    cmd->duration_sec = (int)(5.0f + (intensity / 100.0f) * 15.0f);
}

/* ────────────────────────────────────────────────────────
 *  公开接口: pump_evaluate_local_rule()
 *
 *  决策规则与 cloud_llm_client.c SYSTEM_PROMPT 完全对齐
 * ──────────────────────────────────────────────────────── */
void pump_evaluate_local_rule(const snore_features_t *feat, pump_command_t *cmd_out)
{
    /* ── 1. 默认 hold ─────────────────────────────────── */
    cmd_hold(cmd_out);

    /* ── 2. 未检测到鼾声 → hold ──────────────────────── */
    if (!feat->snore_detected) {
        ESP_LOGI(TAG, "未检测到鼾声 → hold");
        return;
    }

    /* ── 3. 不可操作的睡姿 → hold ────────────────────── */
    posture_t p = feat->posture.posture;
    if (p == POSTURE_MOVING) {
        ESP_LOGI(TAG, "翻身中(MOVING) → hold (等待稳定)");
        return;
    }
    if (p == POSTURE_NO_HEAD) {
        ESP_LOGI(TAG, "头不在枕上(NO_HEAD) → hold");
        return;
    }

    /* ── 4. 鼾声严重程度分级 ──────────────────────────── */
    snore_severity_t severity;
    if (feat->max_probability > 0.9f && feat->positive_window_ratio > 0.1f) {
        severity = SNORE_VERY_SEVERE;
    } else if (feat->snore_minutes_per_hour >= 4.0f) {
        severity = SNORE_SEVERE;
    } else if (feat->snore_minutes_per_hour >= 2.0f) {
        severity = SNORE_MODERATE;
    } else {
        severity = SNORE_MILD;
    }

    /* ── 鼾声轻微 → hold (任何姿势) ─────────────────── */
    if (severity == SNORE_MILD) {
        ESP_LOGI(TAG, "%s + 鼾声轻微(%.1f分钟/时) → hold",
                 posture_name(p), feat->snore_minutes_per_hour);
        return;
    }

    /* ── 5. 根据睡姿 + 严重程度决策 ──────────────────── */
    switch (p) {
    case POSTURE_SUPINE:
        switch (severity) {
        case SNORE_MODERATE:
            /* 仰卧 + 中等: inflate right, intensity 40 */
            cmd_inflate(cmd_out, "right", 40);
            break;
        case SNORE_SEVERE:
            /* 仰卧 + 严重: inflate right, intensity 70 */
            cmd_inflate(cmd_out, "right", 70);
            break;
        case SNORE_VERY_SEVERE:
            /* 仰卧 + 非常严重: inflate right, intensity 80 */
            cmd_inflate(cmd_out, "right", 80);
            break;
        default:
            break;
        }
        break;

    case POSTURE_LEFT_SIDE:
        if (severity >= SNORE_SEVERE) {
            /* 左侧卧 + 严重: inflate left, intensity 60 */
            cmd_inflate(cmd_out, "left", 60);
        }
        break;

    case POSTURE_RIGHT_SIDE:
        if (severity >= SNORE_SEVERE) {
            /* 右侧卧 + 严重: inflate right, intensity 60 */
            cmd_inflate(cmd_out, "right", 60);
        }
        break;

    default:
        /* 其他姿势已在上面过滤 */
        break;
    }

    /* ── 6. 低置信度 → 降低 intensity (×0.7) ─────────── */
    if (feat->posture.confidence < 0.5f && cmd_out->intensity > 0) {
        int reduced = (int)(cmd_out->intensity * 0.7f);
        ESP_LOGI(TAG, "置信度低(%.2f<0.5) → intensity %d → %d",
                 feat->posture.confidence, cmd_out->intensity, reduced);
        cmd_out->intensity = reduced;
        /* 重新计算 duration_sec */
        cmd_out->duration_sec = (int)(5.0f + (reduced / 100.0f) * 15.0f);
    }

    /* ── 日志输出 ─────────────────────────────────────── */
    ESP_LOGI(TAG, "决策: 睡姿=%s(置信度%.2f), 鼾声=%.1f分钟/时, "
             "max_prob=%.2f, ratio=%.2f → %s zone=%s intensity=%d duration=%ds",
             posture_name(p), feat->posture.confidence,
             feat->snore_minutes_per_hour,
             feat->max_probability, feat->positive_window_ratio,
             cmd_out->action, cmd_out->zone,
             cmd_out->intensity, cmd_out->duration_sec);
}
