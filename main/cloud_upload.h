/**
 * cloud_upload.h — 腾讯 CloudBase 睡眠数据上传
 *
 * 将 ESP32 睡眠会话数据 + AI 分析报告上传到腾讯 CloudBase SQL,
 * 与 Flutter App 共享同一 REST API 和数据库。
 *
 * REST API 格式 (PostgREST 风格):
 *   - 插入: POST {baseUrl}/v1/rdb/rest/{table}  + JSON body
 *   - 查询: GET  {baseUrl}/v1/rdb/rest/{table}?limit=1&order=id.desc&...
 *   - 认证: Authorization: Bearer {accessToken}
 *   - 行级安全: 每行必须包含 _openid 字段
 */
#ifndef CLOUD_UPLOAD_H
#define CLOUD_UPLOAD_H

#include "sleep_session.h"

/* ============================================================
 *  腾讯 CloudBase 配置 — 与 Flutter App 共用同一环境
 * ============================================================ */
#define CLOUDBASE_ENV_ID       "sleepmonitor-d0g3037pfb475a0f9"
#define CLOUDBASE_BASE_URL     "https://sleepmonitor-d0g3037pfb475a0f9.api.tcloudbasegateway.com"

/* 匿名访问令牌 (PublishableKey, 长期有效) */
#define CLOUDBASE_ACCESS_TOKEN \
    "eyJhbGciOiJSUzI1NiIsImtpZCI6IjlkMWRjMzFlLWI0ZDAtNDQ4Yi1hNzZmLWIwY2M2M2Q4MTQ5OCJ9" \
    ".eyJpc3MiOiJodHRwczovL3NsZWVwbW9uaXRvci1kMGczMDM3cGZiNDc1YTBmOS5hcC1zaGFuZ2hhaS50" \
    "Y2ItYXBpLnRlbmNlbnRjbG91ZGFwaS5jb20iLCJzdWIiOiJhbm9uIiwiYXVkIjoic2xlZXBtb25pdG9y" \
    "LWQwZzMwMzdwZmI0NzVhMGY5IiwiZXhwIjo0MDg4MTQyOTcxLCJpYXQiOjE3ODQ0NTk3NzEsIm5vbmNl" \
    "IjoiUXprRGFTTzNRYnlQWWo1b2s4XzczUSIsImF0X2hhc2giOiJRemtEYVNPM1FieVBZajVvazhfNzNRIi" \
    "wibmFtZSI6IkFub255bW91cyIsInNjb3BlIjoiYW5vbnltb3VzIiwicHJvamVjdF9pZCI6InNsZWVwbW9u" \
    "aXRvci1kMGczMDM3cGZiNDc1YTBmOSIsIm1ldGEiOnsicGxhdGZvcm0iOiJQdWJsaXNoYWJsZUtleSJ9" \
    "LCJ1c2VyX3R5cGUiOiIiLCJjbGllbnRfdHlwZSI6ImNsaWVudF91c2VyIiwiaXNfc3lzdGVtX2FkbWlu" \
    "IjpmYWxzZX0" \
    ".SgsiLubj64j9hg90CZpBaelCHsNjTJ-0FeAU3iUlE64i9FKvdMnnmSmffnL6hW7RDsnMNKXd-qFPE_vK" \
    "qTyMKDgQkuOY1riPl1q3ck5660Bhkua_KpCxApjQh6SMvDFdGbAI6CIROzFA_aqCBrgYndC1O_jZQViZ_q" \
    "oc6jYOF-KojhdHxb7iw3TM6XWmN-FunUFu101kQu9kE5d-OHDSuL2PGFveb-qB1kHBQ3psWVHNHQwKutC" \
    "yHJLHNfclIJjxOjcSPgI3JPSk8fcpYcmPcchLwBZV2BP_oF0pTmzd3JVLb2ULSP4DgFyjNvIn95jI3bj_l" \
    "51zcXcPJTqyaKSaIw"

/* 编译期拼接 "Bearer " + token，避免运行时 800+ 字节的 snprintf */
#define CLOUDBASE_AUTH_HEADER  "Bearer " CLOUDBASE_ACCESS_TOKEN

/* 行级安全 _openid 和设备标识 */
#define CLOUDBASE_OPENID       "device_esp32"
#define CLOUDBASE_DEVICE_ID    "device_esp32"

/**
 * 上传一次完整睡眠记录到 CloudBase。
 *
 * 顺序写入五张表:
 *   1. sleep_records   — 睡眠主记录
 *   2. ai_analyses     — AI 分析报告
 *   3. snoring_events  — 鼾声事件 (聚合为单条)
 *   4. posture_segments — 睡姿片段 (主要姿态)
 *   5. pressure_segments — 压力片段
 *
 * 函数为非阻塞设计: 任何一步失败仅 ESP_LOGE 记录，不会 crash。
 *
 * @param summary         睡眠会话汇总数据 (来自 sleep_session.h)
 * @param ai_report       AI 分析报告文本 (来自 LLM), 可为 NULL
 * @param llm_suggestions LLM 返回的建议 JSON 数组字符串, 可为 NULL
 *
 * @return  0 = 全部成功, -1 = 网络错误, -2 = 解析错误, -3 = 内存不足
 */
int cloud_upload_sleep_record(const sleep_session_summary_t *summary,
                              const char *ai_report,
                              const char *llm_suggestions);

#endif /* CLOUD_UPLOAD_H */
