
#ifndef CLOUD_LLM_CLIENT_H
#define CLOUD_LLM_CLIENT_H

#include "snore_feature.h"
#include "sleep_session.h"
#include "pump_controller.h"

/*
 * ============================================================
 *  火山引擎边缘智能 API 配置
 *  API 完全兼容 OpenAI /v1/chat/completions 格式
 * ============================================================
 *  申请方式: 比赛发放 api_key, 每个 license 1350K tokens
 *  修改下面三个宏为你实际获取的值
 */
#define VOLCENGINE_API_URL   "https://ark.cn-beijing.volces.com/api/v3/chat/completions"
#define VOLCENGINE_MODEL     "deepseek-v4-pro-260425"

/*
 * API Key 从 secrets.h 读取 (不纳入 git):
 *   #define VOLCENGINE_API_KEY "ark-xxxxx"
 * 首次使用请复制 main/secrets.h.example → main/secrets.h 并填入你的 key
 */
#include "secrets.h"

/**
 * 将鼾声+睡姿综合数据发送给云端 LLM，获取睡眠分析报告和气泵控制指令。
 *
 * 数据来源:
 *   - 鼾声: Snore_Det 分支 (INMP441 → ESP-DL → probability)
 *   - 睡姿: Posture_Recognition 分支 (FSR×3 → 规则分类 → posture + confidence)
 *   - 气泵: airbag-hardware 分支 (左/右独立气泵+电磁阀, GPIO7/8/9/10)
 *
 * @param feat        输入: 鼾声模型输出 + 睡姿数据
 * @param report_out  输出: 睡眠分析报告文本缓冲区
 * @param report_size 输出缓冲区大小
 * @param cmd_out     输出: 解析出的气泵控制指令
 *
 * @return  0 = 成功, -1 = 网络错误, -2 = JSON 解析错误, -3 = 内存不足
 */
int cloud_llm_analyze(const snore_features_t *feat,
                      char *report_out, size_t report_size,
                      pump_command_t *cmd_out);

/**
 * 睡眠结束后调用 — 用整晚汇总数据生成睡眠分析报告。
 *
 * @param summary          整晚睡眠会话汇总
 * @param report_out       输出: 睡眠分析报告文本
 * @param report_size      输出缓冲区大小
 * @param suggestions_out  输出: LLM 建议 JSON 数组字符串 (调用者 free), 可为 NULL
 * @return  0 = 成功, -1 = 网络错误, -2 = JSON 解析错误, -3 = 内存不足
 */
int cloud_llm_analyze_summary(const sleep_session_summary_t *summary,
                              char *report_out, size_t report_size,
                              char **suggestions_out);

/**
 * 5 分钟窗口聚合数据 — 用于周期性 LLM 气泵控制
 */
typedef struct {
    int   frame_count;
    float avg_mean_probability;
    float max_probability;
    float avg_positive_ratio;
    float snore_minutes_per_hour;
    int   snore_detected_frames;
    int   posture_seconds[POSTURE_COUNT];
    posture_t dominant_posture;
    posture_t last_posture;
    float last_confidence;
} llm_window_summary_t;

/**
 * 5 分钟窗口 LLM 分析 — 只返回气泵控制指令，不生成报告。
 *
 * @param window   5 分钟聚合数据
 * @param cmd_out  输出: 气泵控制指令
 * @return  0 = 成功, -1 = 网络/API key 错误, -2 = JSON 解析错误, -3 = 内存不足
 */
int cloud_llm_analyze_window(const llm_window_summary_t *window,
                             pump_command_t *cmd_out);

#endif /* CLOUD_LLM_CLIENT_H */
