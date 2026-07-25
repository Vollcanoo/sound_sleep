/**
 * cloud_llm_client.c — 火山引擎边缘智能 API 客户端
 *
 * 全流程整合:
 *   - 鼾声数据: Snore_Det 分支 (INMP441 → 模型推理 → summary)
 *   - 睡姿数据: Posture_Recognition 分支 (FSR×3 → 规则分类)
 *   - 气泵控制: airbag-hardware 分支 (左/右独立, GPIO7/8/9/10)
 *
 * 核心职责：
 *   1. 将 snore_features_t (鼾声+睡姿) 格式化为结构化文本
 *   2. 构造 OpenAI 兼容 JSON 请求体
 *   3. HTTPS POST 到火山引擎
 *   4. 解析双层 JSON 响应 (OpenAI 信封 → LLM 内容 JSON)
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#include "cloud_llm_client.h"

static const char *TAG = "CLOUD_LLM";

/* ── 响应缓冲区大小 ────────────────────────────────── */
#define MAX_RESPONSE_LEN  4096

/* ── System Prompt ──────────────────────────────────── */
static const char *SYSTEM_PROMPT =
    "你是专业的睡眠健康分析AI。根据用户提供的鼾声模型输出和睡姿传感器数据，完成两项任务：\n"
    "1. 输出睡眠分析报告（中文，100字左右）\n"
    "2. 输出气泵控制指令\n\n"
    "你必须严格按照以下JSON格式回复，不要附加任何其他文本：\n"
    "{\"report\":\"分析报告文本\","
    "\"command\":{\"action\":\"inflate|deflate|hold\","
    "\"zone\":\"left|right|both\","
    "\"intensity\":0到100的整数,"
    "\"duration_sec\":5到30的整数}}\n\n"
    "硬件说明：\n"
    "- 枕头内置左/右两个独立气囊，可分别充气/放气\n"
    "- 充气某一侧会抬高该侧，促使用户头部偏向另一侧\n"
    "- zone=left 充气左侧气囊, zone=right 充气右侧气囊, zone=both 双侧同时\n\n"
    "鼾声数据说明：\n"
    "- mean_probability: 所有时间窗的平均鼾声概率\n"
    "- max_probability: 最大鼾声概率\n"
    "- positive_window_ratio: 正窗口(概率超阈值)占比\n"
    "- snore_detected: 是否检测到鼾声\n"
    "- snore_minutes_per_hour: 每小时鼾声分钟数\n\n"
    "睡姿数据说明：\n"
    "- posture: 当前睡姿 (SUPINE=仰卧, LEFT_SIDE=左侧卧, RIGHT_SIDE=右侧卧, MOVING=翻身中, NO_HEAD=不在枕上)\n"
    "- confidence: 睡姿判定置信度 (0-1)\n"
    "- x_center_cm: 头部左右偏移 (负=偏左, 正=偏右)\n\n"
    "决策规则：\n"
    "- snore_detected=false → action=hold\n"
    "- 仰卧(SUPINE) + 鼾声严重(snore_minutes_per_hour>=4) → action=inflate, zone=right（充气右侧促使左侧卧），intensity 60-80\n"
    "- 仰卧 + 鼾声中等(2-4分钟/时) → action=inflate, zone=right, intensity 30-50\n"
    "- 仰卧 + 鼾声轻微(<2分钟/时) → action=hold\n"
    "- 左侧卧 + 鼾声严重 → action=inflate, zone=left（充气左侧促使右侧卧或仰卧），intensity 50-70\n"
    "- 右侧卧 + 鼾声严重 → action=inflate, zone=right（充气右侧促使左侧卧），intensity 50-70\n"
    "- max_probability>0.9 且 positive_window_ratio>0.1 → 鼾声非常严重，intensity 70-80\n"
    "- 翻身中(MOVING) → action=hold（等待稳定）\n"
    "- 头不在枕上(NO_HEAD) → action=hold\n"
    "- confidence<0.5 → 睡姿不确定，保守处理，降低 intensity\n"
    "- duration_sec 按 intensity 比例在 5-20 秒区间调节";

/* ────────────────────────────────────────────────────
 *  HTTP 事件回调 — 将响应体累积到 user_data 缓冲区
 * ──────────────────────────────────────────────────── */
typedef struct {
    char  *buffer;
    int    buffer_len;
    int    data_len;
} http_response_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *resp = (http_response_t *)evt->user_data;
    if (resp == NULL) return ESP_OK;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA: {
        int copy_len = evt->data_len;
        if (resp->data_len + copy_len >= resp->buffer_len) {
            copy_len = resp->buffer_len - resp->data_len - 1;
        }
        if (copy_len > 0) {
            memcpy(resp->buffer + resp->data_len, evt->data, copy_len);
            resp->data_len += copy_len;
            resp->buffer[resp->data_len] = '\0';
        }
        break;
    }
    case HTTP_EVENT_ON_FINISH:
    case HTTP_EVENT_DISCONNECTED:
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* ────────────────────────────────────────────────────
 *  构造请求 JSON — 鼾声 + 睡姿综合数据
 * ──────────────────────────────────────────────────── */
static char *build_request_json(const snore_features_t *feat)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "model", VOLCENGINE_MODEL);
    cJSON_AddNumberToObject(root, "temperature", 0.3);
    cJSON_AddNumberToObject(root, "max_tokens", 500);

    cJSON *messages = cJSON_AddArrayToObject(root, "messages");

    /* System 消息 */
    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", SYSTEM_PROMPT);
    cJSON_AddItemToArray(messages, sys_msg);

    /* User 消息 — 鼾声 + 睡姿 */
    char user_content[768];
    snprintf(user_content, sizeof(user_content),
        "传感器综合数据：\n\n"
        "【鼾声检测】(Snore_Det: INMP441麦克风 → ESP-DL模型)\n"
        "窗口: %.1f秒, 步长: %.1f秒, 阈值: %.4f\n"
        "是否检测到鼾声: %s\n"
        "窗口总数: %d\n"
        "平均鼾声概率: %.4f\n"
        "最大鼾声概率: %.4f\n"
        "正窗口: %d/%d (占比%.4f, %.2f%%)\n"
        "鼾声累计: %.1f秒 (%.1f分钟)\n"
        "每小时鼾声: %.2f 分钟\n\n"
        "【睡姿识别】(Posture_Recognition: FSR×3压力传感器)\n"
        "当前睡姿: %s (%s)\n"
        "置信度: %.2f\n"
        "头部偏移: X=%.2fcm (负=偏左, 正=偏右)\n",
        feat->window_seconds,
        feat->hop_seconds,
        feat->decision_threshold,
        feat->snore_detected ? "是" : "否",
        feat->window_count,
        feat->mean_probability,
        feat->max_probability,
        feat->positive_window_count, feat->window_count,
        feat->positive_window_ratio,
        feat->positive_window_ratio * 100.0f,
        feat->positive_duration_seconds,
        feat->positive_duration_minutes,
        feat->snore_minutes_per_hour,
        posture_name(feat->posture.posture),
        posture_name_cn(feat->posture.posture),
        feat->posture.confidence,
        feat->posture.x_center_cm);

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_content);
    cJSON_AddItemToArray(messages, user_msg);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;  /* 调用方须 cJSON_free() */
}

/* ────────────────────────────────────────────────────
 *  解析响应 JSON（双层: OpenAI 信封 → LLM 内容 JSON）
 * ──────────────────────────────────────────────────── */
static int parse_response_json(const char *response_body,
                               char *report_out, size_t report_size,
                               pump_command_t *cmd_out)
{
    int ret = -2;

    cJSON *root = cJSON_Parse(response_body);
    if (!root) {
        ESP_LOGE(TAG, "响应 JSON 解析失败");
        return -2;
    }

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        ESP_LOGE(TAG, "响应中无 choices 数组");
        cJSON_Delete(root);
        return -2;
    }

    cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(first_choice, "message");
    cJSON *content = cJSON_GetObjectItem(message, "content");

    if (!cJSON_IsString(content) || content->valuestring == NULL) {
        ESP_LOGE(TAG, "响应 content 为空");
        cJSON_Delete(root);
        return -2;
    }

    ESP_LOGI(TAG, "LLM 原始回复: %s", content->valuestring);

    /* 解析 LLM 生成的 JSON */
    cJSON *result = cJSON_Parse(content->valuestring);
    if (!result) {
        ESP_LOGE(TAG, "LLM 内容 JSON 解析失败，尝试提取...");
        const char *start = strchr(content->valuestring, '{');
        const char *end   = strrchr(content->valuestring, '}');
        if (start && end && end > start) {
            int len = (int)(end - start + 1);
            char *trimmed = malloc(len + 1);
            if (trimmed) {
                memcpy(trimmed, start, len);
                trimmed[len] = '\0';
                result = cJSON_Parse(trimmed);
                free(trimmed);
            }
        }
        if (!result) {
            ESP_LOGE(TAG, "无法提取有效 JSON");
            cJSON_Delete(root);
            return -2;
        }
    }

    /* 提取 report */
    cJSON *report = cJSON_GetObjectItem(result, "report");
    if (cJSON_IsString(report) && report->valuestring) {
        strncpy(report_out, report->valuestring, report_size - 1);
        report_out[report_size - 1] = '\0';
    } else {
        strncpy(report_out, "（报告解析失败）", report_size - 1);
    }

    /* 提取 command */
    cJSON *cmd = cJSON_GetObjectItem(result, "command");
    if (cmd) {
        cJSON *action = cJSON_GetObjectItem(cmd, "action");
        cJSON *zone   = cJSON_GetObjectItem(cmd, "zone");
        cJSON *inten  = cJSON_GetObjectItem(cmd, "intensity");
        cJSON *dur    = cJSON_GetObjectItem(cmd, "duration_sec");

        if (cJSON_IsString(action)) {
            strncpy(cmd_out->action, action->valuestring, sizeof(cmd_out->action) - 1);
        } else {
            strncpy(cmd_out->action, "hold", sizeof(cmd_out->action) - 1);
        }

        if (cJSON_IsString(zone)) {
            strncpy(cmd_out->zone, zone->valuestring, sizeof(cmd_out->zone) - 1);
        } else {
            strncpy(cmd_out->zone, "right", sizeof(cmd_out->zone) - 1);
        }

        cmd_out->intensity    = cJSON_IsNumber(inten) ? inten->valueint : 0;
        cmd_out->duration_sec = cJSON_IsNumber(dur)   ? dur->valueint   : 10;

        if (cmd_out->intensity < 0)    cmd_out->intensity = 0;
        if (cmd_out->intensity > 100)  cmd_out->intensity = 100;
        if (cmd_out->duration_sec < 1) cmd_out->duration_sec = 1;
        if (cmd_out->duration_sec > 60) cmd_out->duration_sec = 60;

        ret = 0;
    } else {
        strncpy(cmd_out->action, "hold", sizeof(cmd_out->action) - 1);
        strncpy(cmd_out->zone, "right", sizeof(cmd_out->zone) - 1);
        cmd_out->intensity = 0;
        cmd_out->duration_sec = 0;
        ret = 0;
    }

    cJSON_Delete(result);
    cJSON_Delete(root);
    return ret;
}

/* ────────────────────────────────────────────────────
 *  公开接口: cloud_llm_analyze()
 * ──────────────────────────────────────────────────── */
int cloud_llm_analyze(const snore_features_t *feat,
                      char *report_out, size_t report_size,
                      pump_command_t *cmd_out)
{
    if (strlen(VOLCENGINE_API_KEY) == 0 ||
        strcmp(VOLCENGINE_API_KEY, "your-volcengine-api-key-here") == 0) {
        ESP_LOGW(TAG, "LLM skipped: no valid API key configured in secrets.h");
        return -1;
    }

    char *post_data = build_request_json(feat);
    if (!post_data) {
        ESP_LOGE(TAG, "构造请求 JSON 失败: 内存不足");
        return -3;
    }
    ESP_LOGI(TAG, "请求体大小: %d 字节", (int)strlen(post_data));

    char *resp_buffer = calloc(1, MAX_RESPONSE_LEN);
    if (!resp_buffer) {
        cJSON_free(post_data);
        ESP_LOGE(TAG, "分配响应缓冲区失败");
        return -3;
    }

    http_response_t resp_ctx = {
        .buffer     = resp_buffer,
        .buffer_len = MAX_RESPONSE_LEN,
        .data_len   = 0,
    };

    esp_http_client_config_t config = {
        .url              = VOLCENGINE_API_URL,
        .method           = HTTP_METHOD_POST,
        .event_handler    = http_event_handler,
        .user_data        = &resp_ctx,
        .timeout_ms       = 30000,
        .buffer_size      = 2048,
        .buffer_size_tx   = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        cJSON_free(post_data);
        free(resp_buffer);
        ESP_LOGE(TAG, "HTTP 客户端初始化失败");
        return -1;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");

    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", VOLCENGINE_API_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    ESP_LOGI(TAG, "发送请求到 %s ...", VOLCENGINE_API_URL);
    esp_err_t err = esp_http_client_perform(client);

    int result = -1;

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP 状态码: %d, 响应长度: %d",
                 status_code, resp_ctx.data_len);

        if (status_code == 200 && resp_ctx.data_len > 0) {
            result = parse_response_json(resp_buffer, report_out,
                                         report_size, cmd_out);
        } else {
            ESP_LOGE(TAG, "API 返回错误: HTTP %d", status_code);
            if (resp_ctx.data_len > 0) {
                ESP_LOGE(TAG, "错误响应: %.*s", resp_ctx.data_len, resp_buffer);
            }
            result = -1;
        }
    } else {
        ESP_LOGE(TAG, "HTTP 请求失败: %s", esp_err_to_name(err));
        result = -1;
    }

    esp_http_client_cleanup(client);
    cJSON_free(post_data);
    free(resp_buffer);

    return result;
}

/* ────────────────────────────────────────────────────
 *  睡眠结束后 — 用整晚汇总调用 LLM 生成分析报告
 * ──────────────────────────────────────────────────── */

static const char *SUMMARY_SYSTEM_PROMPT =
    "你是专业的睡眠健康分析AI。根据用户提供的整晚睡眠汇总数据，生成一份睡眠分析报告。\n\n"
    "你必须严格按照以下JSON格式回复，不要附加任何其他文本：\n"
    "{\"report\":\"分析报告文本(中文,150字左右)\","
    "\"suggestions\":[\"建议1\",\"建议2\",\"建议3\"]}\n\n"
    "数据说明：\n"
    "- duration_minutes: 总睡眠时长(分钟)\n"
    "- get_up_count: 起身次数\n"
    "- total_snore_minutes: 累计鼾声时长(分钟)\n"
    "- snore_minutes_per_hour: 每小时鼾声分钟数\n"
    "- dominant_posture: 主要睡姿\n"
    "- posture_change_count: 翻身次数\n"
    "- sleep_score: 睡眠评分(0-100)\n\n"
    "分析要点：\n"
    "- 评价总体睡眠质量\n"
    "- 分析鼾声严重程度及可能原因\n"
    "- 分析睡姿对呼吸的影响\n"
    "- 给出 3 条具体改善建议";

static char *build_summary_request_json(const sleep_session_summary_t *summary)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "model", VOLCENGINE_MODEL);
    cJSON_AddNumberToObject(root, "temperature", 0.3);
    cJSON_AddNumberToObject(root, "max_tokens", 600);

    cJSON *messages = cJSON_AddArrayToObject(root, "messages");

    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", SUMMARY_SYSTEM_PROMPT);
    cJSON_AddItemToArray(messages, sys_msg);

    char user_content[512];
    snprintf(user_content, sizeof(user_content),
        "整晚睡眠汇总数据：\n"
        "睡眠时长: %d 分钟 (%.1f 小时)\n"
        "起身次数: %d\n"
        "累计鼾声: %.1f 分钟\n"
        "每小时鼾声: %.2f 分钟\n"
        "最大鼾声概率: %.2f\n"
        "平均鼾声概率: %.2f\n"
        "鼾声事件次数: %d\n"
        "主要睡姿: %s\n"
        "翻身次数: %d\n"
        "睡眠评分: %d\n",
        summary->duration_minutes,
        summary->duration_minutes / 60.0f,
        summary->get_up_count,
        summary->total_snore_minutes,
        summary->snore_minutes_per_hour,
        summary->max_snore_probability,
        summary->mean_snore_probability,
        summary->snore_event_count,
        posture_name(summary->dominant_posture),
        summary->posture_change_count,
        summary->sleep_score);

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_content);
    cJSON_AddItemToArray(messages, user_msg);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

int cloud_llm_analyze_summary(const sleep_session_summary_t *summary,
                              char *report_out, size_t report_size,
                              char **suggestions_out)
{
    if (!summary || !report_out || report_size == 0) return -1;

    if (suggestions_out) *suggestions_out = NULL;

    if (strlen(VOLCENGINE_API_KEY) == 0 ||
        strcmp(VOLCENGINE_API_KEY, "your-volcengine-api-key-here") == 0) {
        ESP_LOGW(TAG, "LLM summary skipped: no valid API key configured in secrets.h");
        return -1;
    }

    char *post_data = build_summary_request_json(summary);
    if (!post_data) return -3;

    char *resp_buffer = calloc(1, MAX_RESPONSE_LEN);
    if (!resp_buffer) {
        cJSON_free(post_data);
        return -3;
    }

    http_response_t resp_ctx = {
        .buffer     = resp_buffer,
        .buffer_len = MAX_RESPONSE_LEN,
        .data_len   = 0,
    };

    esp_http_client_config_t config = {
        .url            = VOLCENGINE_API_URL,
        .method         = HTTP_METHOD_POST,
        .event_handler  = http_event_handler,
        .user_data      = &resp_ctx,
        .timeout_ms     = 30000,
        .buffer_size    = 2048,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        cJSON_free(post_data);
        free(resp_buffer);
        return -1;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", VOLCENGINE_API_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    int result = -1;

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200 && resp_ctx.data_len > 0) {
            cJSON *root = cJSON_Parse(resp_buffer);
            if (root) {
                cJSON *choices = cJSON_GetObjectItem(root, "choices");
                if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                    cJSON *msg = cJSON_GetObjectItem(
                        cJSON_GetArrayItem(choices, 0), "message");
                    cJSON *content = cJSON_GetObjectItem(msg, "content");
                    if (cJSON_IsString(content)) {
                        cJSON *inner = cJSON_Parse(content->valuestring);
                        if (inner) {
                            cJSON *report = cJSON_GetObjectItem(inner, "report");
                            if (cJSON_IsString(report)) {
                                snprintf(report_out, report_size, "%s",
                                         report->valuestring);
                                result = 0;
                            }
                            cJSON *sugg = cJSON_GetObjectItem(inner, "suggestions");
                            if (suggestions_out && cJSON_IsArray(sugg)) {
                                *suggestions_out = cJSON_PrintUnformatted(sugg);
                            }
                            cJSON_Delete(inner);
                        }
                        if (result != 0) {
                            snprintf(report_out, report_size, "%s",
                                     content->valuestring);
                            result = 0;
                        }
                    }
                }
                cJSON_Delete(root);
            }
        } else {
            ESP_LOGE(TAG, "Summary API error: HTTP %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "Summary HTTP failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    cJSON_free(post_data);
    free(resp_buffer);
    return result;
}

/* ────────────────────────────────────────────────────
 *  5 分钟窗口 LLM 分析 — 只返回气泵控制指令
 * ──────────────────────────────────────────────────── */

static const char *WINDOW_SYSTEM_PROMPT =
    "你是睡眠气囊控制AI。根据过去5分钟的传感器聚合数据，决定气泵控制指令。\n\n"
    "你必须严格按照以下JSON格式回复，不要附加任何其他文本：\n"
    "{\"command\":{\"action\":\"inflate|deflate|hold\","
    "\"zone\":\"left|right|both\","
    "\"intensity\":0到100的整数,"
    "\"duration_sec\":5到30的整数}}\n\n"
    "硬件说明：\n"
    "- 枕头内置左/右两个独立气囊\n"
    "- 充气某一侧会抬高该侧，促使用户头部偏向另一侧\n\n"
    "决策规则：\n"
    "- 无鼾声(snore_detected_ratio<0.1) → hold\n"
    "- 仰卧为主 + 鼾声严重(snore_minutes_per_hour>=4) → inflate right, intensity 60-80\n"
    "- 仰卧为主 + 鼾声中等(2-4分钟/时) → inflate right, intensity 30-50\n"
    "- 仰卧为主 + 鼾声轻微(<2分钟/时) → hold\n"
    "- 左侧卧为主 + 鼾声严重 → inflate left, intensity 50-70\n"
    "- 右侧卧为主 + 鼾声严重 → inflate right, intensity 50-70\n"
    "- max_probability>0.9 且 snore_detected_ratio>0.3 → 非常严重, intensity 70-80\n"
    "- 当前翻身中(MOVING) → hold\n"
    "- confidence<0.5 → 保守处理，降低intensity\n"
    "- duration_sec 按 intensity 比例在 5-20 秒区间调节";

static char *build_window_request_json(const llm_window_summary_t *window)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "model", VOLCENGINE_MODEL);
    cJSON_AddNumberToObject(root, "temperature", 0.3);
    cJSON_AddNumberToObject(root, "max_tokens", 200);

    cJSON *messages = cJSON_AddArrayToObject(root, "messages");

    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", WINDOW_SYSTEM_PROMPT);
    cJSON_AddItemToArray(messages, sys_msg);

    float snore_detected_ratio = window->frame_count > 0
        ? (float)window->snore_detected_frames / window->frame_count : 0.0f;

    char user_content[512];
    snprintf(user_content, sizeof(user_content),
        "过去5分钟传感器聚合数据：\n"
        "采样帧数: %d (约%.0f秒)\n"
        "平均鼾声概率: %.4f\n"
        "最大鼾声概率: %.4f\n"
        "平均正窗占比: %.4f\n"
        "每小时鼾声分钟: %.2f\n"
        "鼾声检出帧占比: %.4f (%d/%d)\n"
        "姿势分布(秒): 仰卧=%d, 左侧=%d, 右侧=%d, 翻身=%d, 离枕=%d\n"
        "主要姿势: %s\n"
        "当前姿势: %s (置信度%.2f)\n",
        window->frame_count,
        (float)window->frame_count,
        window->avg_mean_probability,
        window->max_probability,
        window->avg_positive_ratio,
        window->snore_minutes_per_hour,
        snore_detected_ratio,
        window->snore_detected_frames, window->frame_count,
        window->posture_seconds[POSTURE_SUPINE],
        window->posture_seconds[POSTURE_LEFT_SIDE],
        window->posture_seconds[POSTURE_RIGHT_SIDE],
        window->posture_seconds[POSTURE_MOVING],
        window->posture_seconds[POSTURE_NO_HEAD],
        posture_name(window->dominant_posture),
        posture_name(window->last_posture),
        window->last_confidence);

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_content);
    cJSON_AddItemToArray(messages, user_msg);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

static int parse_window_response_json(const char *response_body,
                                      pump_command_t *cmd_out)
{
    cJSON *root = cJSON_Parse(response_body);
    if (!root) {
        ESP_LOGE(TAG, "Window 响应 JSON 解析失败");
        return -2;
    }

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        cJSON_Delete(root);
        return -2;
    }

    cJSON *message = cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message");
    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (!cJSON_IsString(content) || content->valuestring == NULL) {
        cJSON_Delete(root);
        return -2;
    }

    ESP_LOGI(TAG, "Window LLM 回复: %s", content->valuestring);

    cJSON *result = cJSON_Parse(content->valuestring);
    if (!result) {
        const char *start = strchr(content->valuestring, '{');
        const char *end   = strrchr(content->valuestring, '}');
        if (start && end && end > start) {
            int len = (int)(end - start + 1);
            char *trimmed = malloc(len + 1);
            if (trimmed) {
                memcpy(trimmed, start, len);
                trimmed[len] = '\0';
                result = cJSON_Parse(trimmed);
                free(trimmed);
            }
        }
        if (!result) {
            cJSON_Delete(root);
            return -2;
        }
    }

    cJSON *cmd = cJSON_GetObjectItem(result, "command");
    if (cmd) {
        cJSON *action = cJSON_GetObjectItem(cmd, "action");
        cJSON *zone   = cJSON_GetObjectItem(cmd, "zone");
        cJSON *inten  = cJSON_GetObjectItem(cmd, "intensity");
        cJSON *dur    = cJSON_GetObjectItem(cmd, "duration_sec");

        if (cJSON_IsString(action)) {
            strncpy(cmd_out->action, action->valuestring, sizeof(cmd_out->action) - 1);
            cmd_out->action[sizeof(cmd_out->action) - 1] = '\0';
        } else {
            strncpy(cmd_out->action, "hold", sizeof(cmd_out->action) - 1);
        }

        if (cJSON_IsString(zone)) {
            strncpy(cmd_out->zone, zone->valuestring, sizeof(cmd_out->zone) - 1);
            cmd_out->zone[sizeof(cmd_out->zone) - 1] = '\0';
        } else {
            strncpy(cmd_out->zone, "right", sizeof(cmd_out->zone) - 1);
        }

        cmd_out->intensity    = cJSON_IsNumber(inten) ? inten->valueint : 0;
        cmd_out->duration_sec = cJSON_IsNumber(dur)   ? dur->valueint   : 10;

        if (cmd_out->intensity < 0)    cmd_out->intensity = 0;
        if (cmd_out->intensity > 100)  cmd_out->intensity = 100;
        if (cmd_out->duration_sec < 1) cmd_out->duration_sec = 1;
        if (cmd_out->duration_sec > 60) cmd_out->duration_sec = 60;
    } else {
        strncpy(cmd_out->action, "hold", sizeof(cmd_out->action) - 1);
        strncpy(cmd_out->zone, "both", sizeof(cmd_out->zone) - 1);
        cmd_out->intensity = 0;
        cmd_out->duration_sec = 0;
    }

    cJSON_Delete(result);
    cJSON_Delete(root);
    return 0;
}

int cloud_llm_analyze_window(const llm_window_summary_t *window,
                             pump_command_t *cmd_out)
{
    if (!window || !cmd_out) return -1;

    if (strlen(VOLCENGINE_API_KEY) == 0 ||
        strcmp(VOLCENGINE_API_KEY, "your-volcengine-api-key-here") == 0) {
        ESP_LOGW(TAG, "Window LLM skipped: no valid API key");
        return -1;
    }

    char *post_data = build_window_request_json(window);
    if (!post_data) return -3;

    char *resp_buffer = calloc(1, MAX_RESPONSE_LEN);
    if (!resp_buffer) {
        cJSON_free(post_data);
        return -3;
    }

    http_response_t resp_ctx = {
        .buffer     = resp_buffer,
        .buffer_len = MAX_RESPONSE_LEN,
        .data_len   = 0,
    };

    esp_http_client_config_t config = {
        .url            = VOLCENGINE_API_URL,
        .method         = HTTP_METHOD_POST,
        .event_handler  = http_event_handler,
        .user_data      = &resp_ctx,
        .timeout_ms     = 30000,
        .buffer_size    = 2048,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        cJSON_free(post_data);
        free(resp_buffer);
        return -1;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", VOLCENGINE_API_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    ESP_LOGI(TAG, "Window LLM 请求发送...");
    esp_err_t err = esp_http_client_perform(client);
    int ret = -1;

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200 && resp_ctx.data_len > 0) {
            ret = parse_window_response_json(resp_buffer, cmd_out);
        } else {
            ESP_LOGE(TAG, "Window API error: HTTP %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "Window HTTP failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    cJSON_free(post_data);
    free(resp_buffer);
    return ret;
}
