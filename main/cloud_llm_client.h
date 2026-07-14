
#ifndef CLOUD_LLM_CLIENT_H
#define CLOUD_LLM_CLIENT_H

#include "snore_feature.h"

/*
 * ============================================================
 *  火山引擎边缘智能 API 配置
 *  API 完全兼容 OpenAI /v1/chat/completions 格式
 * ============================================================
 *  申请方式: 比赛发放 api_key, 每个 license 1350K tokens
 *  修改下面三个宏为你实际获取的值
 */
#define VOLCENGINE_API_URL   "https://ark.cn-beijing.volces.com/api/v3/chat/completions"
#define VOLCENGINE_API_KEY   "your-api-key-here"  // 替换为你的 API Key
#define VOLCENGINE_MODEL     "deepseek-v4-pro-260425"

/**
 * 气泵控制指令 — 从云端 LLM 响应中解析出
 */
typedef struct {
    char action[16];      // "inflate" = 充气, "deflate" = 放气, "hold" = 保持
    char zone[16];        // "head" = 头部, "shoulder" = 肩部, "waist" = 腰部
    int  intensity;       // 强度 0-100
    int  duration_sec;    // 动作持续时间 (秒)
} pump_command_t;

/**
 * 将鼾声模型输出 (对齐 snore_model_output.template.json) 发送给云端 LLM，
 * 获取睡眠分析报告和气泵控制指令。
 *
 * @param feat        输入: 模型输出 summary (9 个统计字段 + 3 个顶层参数)
 * @param report_out  输出: 睡眠分析报告文本缓冲区
 * @param report_size 输出缓冲区大小
 * @param cmd_out     输出: 解析出的气泵控制指令
 *
 * @return  0 = 成功, -1 = 网络错误, -2 = JSON 解析错误, -3 = 内存不足
 */
int cloud_llm_analyze(const snore_features_t *feat,
                      char *report_out, size_t report_size,
                      pump_command_t *cmd_out);

#endif /* CLOUD_LLM_CLIENT_H */
