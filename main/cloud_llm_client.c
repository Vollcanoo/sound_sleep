/**
 * cloud_llm_client.c — 火山引擎边缘智能 API 客户端
 *
 * 严格对齐 snore_model_output.template.json:
 *   - summary 字段: window_count, mean_probability, max_probability,
 *     positive_window_count, positive_window_ratio,
 *     positive_duration_seconds/minutes, snore_detected, snore_minutes_per_hour
 *   - 顶层参数: window_seconds, hop_seconds, decision_threshold
 *   - 不包含 events、设备信息、患者ID 等业务字段
 *
 * 核心职责：
 *   1. 将 snore_features_t 格式化为结构化文本
 *   2. 构造 OpenAI 兼容 JSON 请求体
 *   3. HTTPS POST 到火山引擎
 *   4. 解析双层 JSON 响应 (OpenAI 信封 → LLM 内容)
 */
#include <string.h>
#include <stdio.h>
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
    "你是专业的睡眠健康分析AI。根据用户提供的鼾声模型输出数据，完成两项任务：\n"
    "1. 输出睡眠分析报告（中文，100字左右）\n"
    "2. 输出气泵控制指令\n\n"
    "你必须严格按照以下JSON格式回复，不要附加任何其他文本：\n"
    "{\"report\":\"分析报告文本\","
    "\"command\":{\"action\":\"inflate|deflate|hold\","
    "\"zone\":\"head|shoulder|waist\","
    "\"intensity\":0到100的整数,"
    "\"duration_sec\":5到30的整数}}\n\n"
    "数据说明：\n"
    "- mean_probability: 所有时间窗的平均鼾声概率（最接近模型直接输出）\n"
    "- max_probability: 所有时间窗中的最大鼾声概率\n"
    "- positive_window_count: 概率超过阈值的正窗口数\n"
    "- positive_window_ratio: 正窗口占比\n"
    "- snore_detected: 是否存在至少一个正窗口\n"
    "- snore_minutes_per_hour: 每小时鼾声分钟数\n\n"
    "决策规则：\n"
    "- snore_detected=false → action=hold（保持当前状态）\n"
    "- snore_detected=true 且 snore_minutes_per_hour>=4 → action=inflate, zone=shoulder（促使侧卧，鼾声较严重）\n"
    "- snore_detected=true 且 snore_minutes_per_hour 2-4 → action=inflate, zone=shoulder, intensity较低(30-50)\n"
    "- snore_detected=true 且 snore_minutes_per_hour<2 → action=hold（轻微，暂不干预）\n"
    "- max_probability>0.9 且 positive_window_ratio>0.1 → 鼾声非常严重，zone=head（抬高头部），intensity 70-80\n"
    "- intensity 根据 mean_probability 和 snore_minutes_per_hour 在 30-80 区间调节\n"
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
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client)) {
            int copy_len = evt->data_len;
            if (resp->data_len + copy_len >= resp->buffer_len) {
                copy_len = resp->buffer_len - resp->data_len - 1;
            }
            if (copy_len > 0) {
                memcpy(resp->buffer + resp->data_len, evt->data, copy_len);
                resp->data_len += copy_len;
                resp->buffer[resp->data_len] = '\0';
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
    case HTTP_EVENT_DISCONNECTED:
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* ────────────────────────────────────────────────────
 *  构造请求 JSON — 对齐 snore_model_output.template.json
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

    /* User 消息 — 填入 summary 数据 */
    char user_content[512];
    snprintf(user_content, sizeof(user_content),
        "鼾声模型输出（snore_model_output.template.json 格式）：\n\n"
        "【参数】\n"
        "窗口时长: %.1f 秒\n"
        "步长: %.1f 秒\n"
        "决策阈值: %.4f\n\n"
        "【Summary 统计】\n"
        "是否检测到鼾声: %s\n"
        "窗口总数: %d\n"
        "平均鼾声概率(mean_probability): %.4f\n"
        "最大鼾声概率(max_probability): %.4f\n"
        "正窗口数: %d\n"
        "正窗口占比: %.4f (%.2f%%)\n"
        "鼾声累计时长: %.1f 秒 (%.1f 分钟)\n"
        "每小时鼾声分钟数: %.2f\n",
        feat->window_seconds,
        feat->hop_seconds,
        feat->decision_threshold,
        feat->snore_detected ? "是" : "否",
        feat->window_count,
        feat->mean_probability,
        feat->max_probability,
        feat->positive_window_count,
        feat->positive_window_ratio,
        feat->positive_window_ratio * 100.0f,
        feat->positive_duration_seconds,
        feat->positive_duration_minutes,
        feat->snore_minutes_per_hour);

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
            strncpy(cmd_out->zone, "head", sizeof(cmd_out->zone) - 1);
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
        strncpy(cmd_out->zone, "head", sizeof(cmd_out->zone) - 1);
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
