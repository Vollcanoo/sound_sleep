
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
#define VOLCENGINE_MODEL     "deepseek-v4-pro-260425"

/*
 * API Key 从 secrets.h 读取 (不纳入 git):
 *   #define VOLCENGINE_API_KEY "ark-xxxxx"
 * 首次使用请复制 main/secrets.h.example → main/secrets.h 并填入你的 key
 */
#include "secrets.h"

/**
 * 气泵控制指令 — 从云端 LLM 响应中解析出
 *
 * 对齐 airbag-hardware 分支:
 *   zone: "left" / "right" / "both" (双气囊左右独立)
 *   action: "inflate" / "deflate" / "hold"
 */
typedef struct {
    char action[16];      /* "inflate" = 充气, "deflate" = 放气, "hold" = 保持 */
    char zone[16];        /* "left" = 左侧, "right" = 右侧, "both" = 双侧 */
    int  intensity;       /* 强度 0-100 */
    int  duration_sec;    /* 动作持续时间 (秒) */
} pump_command_t;

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

#endif /* CLOUD_LLM_CLIENT_H */
