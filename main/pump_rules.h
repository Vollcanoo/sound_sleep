/**
 * pump_rules.h — 本地气泵决策规则
 *
 * 将 cloud_llm_client.c 的 LLM system prompt 中的决策规则
 * 硬编码为 C 函数，用于实时气泵控制（不调用 LLM API）。
 *
 * 规则对齐 LLM 的决策逻辑:
 *   - snore_detected=false → hold
 *   - 仰卧 + 鼾声严重(>=4分钟/时) → inflate right, intensity 60-80
 *   - 仰卧 + 鼾声中等(2-4分钟/时) → inflate right, intensity 30-50
 *   - 仰卧 + 鼾声轻微(<2分钟/时) → hold
 *   - 左侧卧 + 鼾声严重 → inflate left, intensity 50-70
 *   - 右侧卧 + 鼾声严重 → inflate right, intensity 50-70
 *   - max_prob>0.9 且 ratio>0.1 → 非常严重, intensity 70-80
 *   - 翻身中(MOVING) → hold
 *   - 不在枕上 → hold
 *   - confidence<0.5 → 保守, 降低 intensity
 */
#ifndef PUMP_RULES_H
#define PUMP_RULES_H

#include "snore_feature.h"
#include "pump_controller.h"

/**
 * 根据当前传感器数据，使用本地规则评估是否需要气泵操作。
 *
 * @param feat    当前传感器综合数据
 * @param cmd_out 输出: 气泵控制指令
 */
void pump_evaluate_local_rule(const snore_features_t *feat, pump_command_t *cmd_out);

void pump_rules_record_queued_command(const pump_command_t *cmd);
void pump_rules_reset(void);

#endif /* PUMP_RULES_H */
