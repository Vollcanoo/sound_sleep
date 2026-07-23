/**
 * cloud_upload.c — 腾讯 CloudBase 睡眠数据上传
 *
 * 将 ESP32 睡眠会话数据上传到腾讯 CloudBase SQL 数据库，
 * 与 Flutter App 共享同一后端。
 *
 * REST API 格式 (PostgREST 风格):
 *   - 插入: POST {baseUrl}/v1/rdb/rest/{table}
 *   - 查询: GET  {baseUrl}/v1/rdb/rest/{table}?limit=1&order=id.desc
 *   - 认证: Authorization: Bearer {accessToken}
 *
 * 上传顺序:
 *   1. sleep_records   → 获取自增 id
 *   2. ai_analyses     (record_id = 上一步的 id)
 *   3. snoring_events  (聚合为单条)
 *   4. posture_segments (主要姿态)
 *   5. pressure_segments (压力片段)
 */
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#include "cloud_upload.h"

static const char *TAG = "CLOUD_UP";

/* ── 响应缓冲区大小 ────────────────────────────────── */
#define MAX_RESPONSE_LEN  2048

/* ── URL 路径缓冲区 ────────────────────────────────── */
#define MAX_URL_LEN       256

/* ────────────────────────────────────────────────────
 *  HTTP 事件回调 — 将响应体累积到 user_data 缓冲区
 *  (与 cloud_llm_client.c 相同的模式)
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
 *  时间格式化工具
 *
 *  ESP32 没有 RTC，使用 esp_timer_get_time() 的毫秒时间戳。
 *  将 bed_time_ms 映射为一个基准 epoch，后续时间相对计算。
 *
 *  如果系统时间已通过 SNTP 同步 (time(NULL) > 2024)，
 *  则使用真实墙钟时间。
 * ──────────────────────────────────────────────────── */

/**
 * 将毫秒时间戳格式化为 ISO 8601 字符串。
 *
 * 策略:
 *  - 如果系统时钟有效 (SNTP 已同步)，计算真实时间:
 *    now_epoch + (target_ms - current_esp_ms) / 1000
 *  - 否则使用一个固定基准 epoch (2026-01-01 00:00:00 UTC)
 *    加上 target_ms 的偏移量
 *
 * @param ms_timestamp   ESP32 毫秒时间戳 (esp_timer 风格)
 * @param ref_ms         参考时间戳 (通常是 bed_time_ms)
 * @param out            输出缓冲区
 * @param out_size       缓冲区大小 (至少 25 字节)
 */
static void format_iso_time(int64_t ms_timestamp, int64_t ref_ms,
                            char *out, size_t out_size)
{
    time_t now_epoch = time(NULL);
    time_t target_epoch;

    if (now_epoch > 1700000000) {
        /* 系统时钟有效 — 计算目标时间的真实 epoch */
        int64_t now_ms = (int64_t)esp_log_timestamp();
        int64_t delta_s = (ms_timestamp - now_ms) / 1000;
        target_epoch = now_epoch + (time_t)delta_s;
    } else {
        /* 无 SNTP — 使用固定基准 2026-01-01T00:00:00Z */
        time_t base_epoch = 1767225600;  /* 2026-01-01 00:00:00 UTC */
        target_epoch = base_epoch + (time_t)(ms_timestamp / 1000);
    }

    struct tm timeinfo;
    gmtime_r(&target_epoch, &timeinfo);
    strftime(out, out_size, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
}

/**
 * 从毫秒时间戳中提取 ISO 日期 (YYYY-MM-DD)
 */
static void format_iso_date(int64_t ms_timestamp, int64_t ref_ms,
                            char *out, size_t out_size)
{
    time_t now_epoch = time(NULL);
    time_t target_epoch;

    if (now_epoch > 1700000000) {
        int64_t now_ms = (int64_t)esp_log_timestamp();
        int64_t delta_s = (ms_timestamp - now_ms) / 1000;
        target_epoch = now_epoch + (time_t)delta_s;
    } else {
        time_t base_epoch = 1767225600;
        target_epoch = base_epoch + (time_t)(ms_timestamp / 1000);
    }

    struct tm timeinfo;
    gmtime_r(&target_epoch, &timeinfo);
    strftime(out, out_size, "%Y-%m-%d", &timeinfo);
}

/* ────────────────────────────────────────────────────
 *  HTTP POST — 向 CloudBase 表插入一行
 * ──────────────────────────────────────────────────── */
static int cloudbase_post(const char *table, const char *json_body,
                          char *resp_out, int resp_out_len)
{
    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s/v1/rdb/rest/%s",
             CLOUDBASE_BASE_URL, table);

    char *resp_buffer = calloc(1, MAX_RESPONSE_LEN);
    if (!resp_buffer) {
        ESP_LOGE(TAG, "[%s] 分配响应缓冲区失败", table);
        return -3;
    }

    http_response_t resp_ctx = {
        .buffer     = resp_buffer,
        .buffer_len = MAX_RESPONSE_LEN,
        .data_len   = 0,
    };

    esp_http_client_config_t config = {
        .url               = url,
        .method            = HTTP_METHOD_POST,
        .event_handler     = http_event_handler,
        .user_data         = &resp_ctx,
        .timeout_ms        = 15000,
        .buffer_size       = 2048,
        .buffer_size_tx    = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp_buffer);
        ESP_LOGE(TAG, "[%s] HTTP 客户端初始化失败", table);
        return -1;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", CLOUDBASE_AUTH_HEADER);
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    ESP_LOGI(TAG, "[%s] POST %s (%d 字节)", table, url, (int)strlen(json_body));
    esp_err_t err = esp_http_client_perform(client);

    int result = -1;
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "[%s] HTTP %d, 响应 %d 字节", table, status, resp_ctx.data_len);

        if (status >= 200 && status < 300) {
            result = 0;
            /* 拷贝响应内容供调用方解析 */
            if (resp_out && resp_out_len > 0 && resp_ctx.data_len > 0) {
                int copy = resp_ctx.data_len < (resp_out_len - 1)
                           ? resp_ctx.data_len : (resp_out_len - 1);
                memcpy(resp_out, resp_buffer, copy);
                resp_out[copy] = '\0';
            }
        } else {
            ESP_LOGE(TAG, "[%s] 服务器返回 HTTP %d", table, status);
            if (resp_ctx.data_len > 0) {
                ESP_LOGE(TAG, "[%s] 错误响应: %.*s",
                         table, resp_ctx.data_len, resp_buffer);
            }
        }
    } else {
        ESP_LOGE(TAG, "[%s] HTTP 请求失败: %s", table, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(resp_buffer);
    return result;
}

/* ────────────────────────────────────────────────────
 *  HTTP GET — 查询 CloudBase 表
 * ──────────────────────────────────────────────────── */
static int cloudbase_get(const char *full_url,
                         char *resp_out, int resp_out_len)
{
    char *resp_buffer = calloc(1, MAX_RESPONSE_LEN);
    if (!resp_buffer) {
        ESP_LOGE(TAG, "分配响应缓冲区失败");
        return -3;
    }

    http_response_t resp_ctx = {
        .buffer     = resp_buffer,
        .buffer_len = MAX_RESPONSE_LEN,
        .data_len   = 0,
    };

    esp_http_client_config_t config = {
        .url               = full_url,
        .method            = HTTP_METHOD_GET,
        .event_handler     = http_event_handler,
        .user_data         = &resp_ctx,
        .timeout_ms        = 15000,
        .buffer_size       = 2048,
        .buffer_size_tx    = 512,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp_buffer);
        ESP_LOGE(TAG, "HTTP GET 客户端初始化失败");
        return -1;
    }

    esp_http_client_set_header(client, "Authorization", CLOUDBASE_AUTH_HEADER);

    ESP_LOGI(TAG, "GET %s", full_url);
    esp_err_t err = esp_http_client_perform(client);

    int result = -1;
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "GET HTTP %d, 响应 %d 字节", status, resp_ctx.data_len);

        if (status >= 200 && status < 300) {
            result = 0;
            if (resp_out && resp_out_len > 0 && resp_ctx.data_len > 0) {
                int copy = resp_ctx.data_len < (resp_out_len - 1)
                           ? resp_ctx.data_len : (resp_out_len - 1);
                memcpy(resp_out, resp_buffer, copy);
                resp_out[copy] = '\0';
            }
        } else {
            ESP_LOGE(TAG, "GET 服务器返回 HTTP %d", status);
            if (resp_ctx.data_len > 0) {
                ESP_LOGE(TAG, "错误响应: %.*s",
                         resp_ctx.data_len, resp_buffer);
            }
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET 请求失败: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(resp_buffer);
    return result;
}

/* ────────────────────────────────────────────────────
 *  Step 1: 插入 sleep_records 并查回自增 id
 * ──────────────────────────────────────────────────── */
static int upload_sleep_record(const sleep_session_summary_t *summary,
                               int *record_id_out)
{
    *record_id_out = -1;

    /* 格式化时间字符串 */
    char date_str[16];
    char bed_time_str[32];
    char wake_time_str[32];
    char created_at_str[32];

    format_iso_date(summary->bed_time_ms, summary->bed_time_ms,
                    date_str, sizeof(date_str));
    format_iso_time(summary->bed_time_ms, summary->bed_time_ms,
                    bed_time_str, sizeof(bed_time_str));
    format_iso_time(summary->wake_time_ms, summary->bed_time_ms,
                    wake_time_str, sizeof(wake_time_str));
    format_iso_time(summary->wake_time_ms, summary->bed_time_ms,
                    created_at_str, sizeof(created_at_str));

    /* 构造 JSON */
    cJSON *body = cJSON_CreateObject();
    if (!body) return -3;

    cJSON_AddStringToObject(body, "date",             date_str);
    cJSON_AddStringToObject(body, "bed_time",         bed_time_str);
    cJSON_AddStringToObject(body, "wake_time",        wake_time_str);
    cJSON_AddNumberToObject(body, "duration_minutes",  summary->duration_minutes);
    cJSON_AddNumberToObject(body, "sleep_score",       summary->sleep_score);
    cJSON_AddNumberToObject(body, "get_up_count",      summary->get_up_count);
    cJSON_AddStringToObject(body, "device_id",         CLOUDBASE_DEVICE_ID);
    cJSON_AddStringToObject(body, "created_at",        created_at_str);

    char *json_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json_str) return -3;

    ESP_LOGI(TAG, "sleep_records JSON: %s", json_str);

    /* POST 插入 */
    char resp_buf[MAX_RESPONSE_LEN];
    memset(resp_buf, 0, sizeof(resp_buf));
    int ret = cloudbase_post("sleep_records", json_str, resp_buf, sizeof(resp_buf));
    cJSON_free(json_str);

    if (ret != 0) {
        ESP_LOGE(TAG, "sleep_records 插入失败");
        return ret;
    }

    /* 尝试从 POST 响应中解析 id */
    if (resp_buf[0] != '\0') {
        cJSON *resp_json = cJSON_Parse(resp_buf);
        if (resp_json) {
            /* 响应可能是对象或数组 */
            cJSON *item = resp_json;
            if (cJSON_IsArray(resp_json)) {
                item = cJSON_GetArrayItem(resp_json, 0);
            }
            if (item) {
                cJSON *id_field = cJSON_GetObjectItem(item, "id");
                if (cJSON_IsNumber(id_field)) {
                    *record_id_out = id_field->valueint;
                    ESP_LOGI(TAG, "从 POST 响应解析到 record_id=%d", *record_id_out);
                }
            }
            cJSON_Delete(resp_json);
        }
    }

    /* 如果 POST 响应没有返回 id，查询最新插入的记录 */
    if (*record_id_out < 0) {
        ESP_LOGI(TAG, "POST 响应无 id，查询最新记录...");
        char query_url[512];
        snprintf(query_url, sizeof(query_url),
                 "%s/v1/rdb/rest/sleep_records"
                 "?limit=1&order=id.desc&device_id=eq.%s",
                 CLOUDBASE_BASE_URL, CLOUDBASE_DEVICE_ID);

        memset(resp_buf, 0, sizeof(resp_buf));
        ret = cloudbase_get(query_url, resp_buf, sizeof(resp_buf));

        if (ret == 0 && resp_buf[0] != '\0') {
            cJSON *arr = cJSON_Parse(resp_buf);
            if (arr) {
                cJSON *first = NULL;
                if (cJSON_IsArray(arr)) {
                    first = cJSON_GetArrayItem(arr, 0);
                } else {
                    first = arr;
                }
                if (first) {
                    cJSON *id_field = cJSON_GetObjectItem(first, "id");
                    if (cJSON_IsNumber(id_field)) {
                        *record_id_out = id_field->valueint;
                        ESP_LOGI(TAG, "查询到 record_id=%d", *record_id_out);
                    }
                }
                cJSON_Delete(arr);
            }
        }
    }

    if (*record_id_out < 0) {
        ESP_LOGW(TAG, "无法获取 record_id，后续表将跳过");
        return -2;
    }

    return 0;
}

/* ────────────────────────────────────────────────────
 *  Step 2: 插入 ai_analyses
 * ──────────────────────────────────────────────────── */
static int upload_ai_analysis(int record_id, const char *ai_report,
                              const sleep_session_summary_t *summary)
{
    if (!ai_report || ai_report[0] == '\0') {
        ESP_LOGI(TAG, "无 AI 报告，跳过 ai_analyses");
        return 0;
    }

    char created_at_str[32];
    time_t now = time(NULL);
    if (now > 1700000000) {
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        strftime(created_at_str, sizeof(created_at_str),
                 "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    } else {
        snprintf(created_at_str, sizeof(created_at_str),
                 "2026-01-01T00:00:00Z");
    }

    cJSON *suggestions = cJSON_CreateArray();
    if (summary->snore_minutes_per_hour >= 2.0f) {
        cJSON_AddItemToArray(suggestions,
            cJSON_CreateString("鼾声较频繁，建议侧卧睡眠以减轻气道阻塞"));
    }
    if (summary->dominant_posture == POSTURE_SUPINE &&
        summary->total_snore_minutes > 5.0f) {
        cJSON_AddItemToArray(suggestions,
            cJSON_CreateString("仰卧时间较长且有鼾声，尝试调整为侧卧位"));
    }
    if (summary->get_up_count > 2) {
        cJSON_AddItemToArray(suggestions,
            cJSON_CreateString("夜间起身较多，睡前减少饮水可改善连续性"));
    }
    if (summary->posture_change_count > 20) {
        cJSON_AddItemToArray(suggestions,
            cJSON_CreateString("翻身频繁，检查卧室温度和床垫舒适度"));
    }
    if (summary->duration_minutes < 420) {
        cJSON_AddItemToArray(suggestions,
            cJSON_CreateString("睡眠不足7小时，建议提前入睡保证充足休息"));
    }
    if (cJSON_GetArraySize(suggestions) == 0) {
        cJSON_AddItemToArray(suggestions,
            cJSON_CreateString("保持良好作息习惯，坚持规律的睡眠时间"));
    }
    char *suggestions_str = cJSON_PrintUnformatted(suggestions);
    cJSON_Delete(suggestions);

    cJSON *body = cJSON_CreateObject();
    if (!body) {
        if (suggestions_str) cJSON_free(suggestions_str);
        return -3;
    }

    cJSON_AddNumberToObject(body, "record_id",   record_id);
    cJSON_AddStringToObject(body, "summary",     ai_report);
    cJSON_AddStringToObject(body, "suggestions", suggestions_str ? suggestions_str : "[]");
    cJSON_AddStringToObject(body, "created_at",  created_at_str);

    if (suggestions_str) cJSON_free(suggestions_str);

    char *json_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json_str) return -3;

    ESP_LOGI(TAG, "ai_analyses JSON: %s", json_str);
    int ret = cloudbase_post("ai_analyses", json_str, NULL, 0);
    cJSON_free(json_str);

    if (ret != 0) {
        ESP_LOGE(TAG, "ai_analyses 插入失败");
    }
    return ret;
}

/* ────────────────────────────────────────────────────
 *  Step 3: 插入 snoring_events (聚合为单条)
 * ──────────────────────────────────────────────────── */
static int upload_snoring_event(int record_id,
                                const sleep_session_summary_t *summary)
{
    if (summary->total_snore_minutes < 0.1f) {
        ESP_LOGI(TAG, "无明显鼾声，跳过 snoring_events");
        return 0;
    }

    /* 鼾声作为一个聚合事件: 从 bed_time 到 wake_time */
    char start_str[32], end_str[32];
    format_iso_time(summary->bed_time_ms, summary->bed_time_ms,
                    start_str, sizeof(start_str));
    format_iso_time(summary->wake_time_ms, summary->bed_time_ms,
                    end_str, sizeof(end_str));

    /* 平均分贝用一个估算值 (ESP32 INMP441 无校准分贝) */
    float avg_decibel = summary->mean_snore_probability > 0.5f ? 55.0f : 40.0f;

    cJSON *body = cJSON_CreateObject();
    if (!body) return -3;

    cJSON_AddNumberToObject(body, "record_id",        record_id);
    cJSON_AddStringToObject(body, "start_time",       start_str);
    cJSON_AddStringToObject(body, "end_time",         end_str);
    cJSON_AddNumberToObject(body, "avg_decibel",      avg_decibel);
    cJSON_AddNumberToObject(body, "avg_probability",  summary->mean_snore_probability);

    char *json_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json_str) return -3;

    ESP_LOGI(TAG, "snoring_events JSON: %s", json_str);
    int ret = cloudbase_post("snoring_events", json_str, NULL, 0);
    cJSON_free(json_str);

    if (ret != 0) {
        ESP_LOGE(TAG, "snoring_events 插入失败");
    }
    return ret;
}

/* ────────────────────────────────────────────────────
 *  Step 4: 插入 posture_segments (主要姿态)
 * ──────────────────────────────────────────────────── */

/**
 * 将 posture_t 枚举转换为 Flutter 端使用的字符串
 */
static const char *posture_to_cloud_string(posture_t p)
{
    switch (p) {
        case POSTURE_SUPINE:     return "supine";
        case POSTURE_LEFT_SIDE:  return "leftSide";
        case POSTURE_RIGHT_SIDE: return "rightSide";
        case POSTURE_MOVING:     return "moving";
        case POSTURE_NO_HEAD:    return "noHead";
        default:                 return "noHead";
    }
}

static int upload_posture_segment(int record_id,
                                  const sleep_session_summary_t *summary)
{
    posture_t dominant = summary->dominant_posture;
    int dominant_seconds = summary->posture_seconds[dominant];

    if (dominant_seconds <= 0) {
        ESP_LOGI(TAG, "无有效姿态数据，跳过 posture_segments");
        return 0;
    }

    /* 主要姿态: 整段睡眠期间 */
    char start_str[32], end_str[32];
    format_iso_time(summary->bed_time_ms, summary->bed_time_ms,
                    start_str, sizeof(start_str));
    format_iso_time(summary->wake_time_ms, summary->bed_time_ms,
                    end_str, sizeof(end_str));

    /* 置信度: 主要姿态占总时间的比例 */
    int total_seconds = summary->duration_minutes * 60;
    float avg_confidence = (total_seconds > 0)
        ? (float)dominant_seconds / (float)total_seconds
        : 0.5f;
    if (avg_confidence > 1.0f) avg_confidence = 1.0f;

    cJSON *body = cJSON_CreateObject();
    if (!body) return -3;

    cJSON_AddNumberToObject(body, "record_id",       record_id);
    cJSON_AddStringToObject(body, "start_time",      start_str);
    cJSON_AddStringToObject(body, "end_time",        end_str);
    cJSON_AddStringToObject(body, "posture",         posture_to_cloud_string(dominant));
    cJSON_AddNumberToObject(body, "avg_confidence",  avg_confidence);

    char *json_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json_str) return -3;

    ESP_LOGI(TAG, "posture_segments JSON: %s", json_str);
    int ret = cloudbase_post("posture_segments", json_str, NULL, 0);
    cJSON_free(json_str);

    if (ret != 0) {
        ESP_LOGE(TAG, "posture_segments 插入失败");
    }
    return ret;
}

/* ────────────────────────────────────────────────────
 *  Step 5: pressure_segments — 压力片段
 *
 *  将整段睡眠作为一个 pressure segment（中等压力）上传，
 *  与 Flutter cloud_sync_service.dart 的 _uploadPressureSegments 对齐。
 * ──────────────────────────────────────────────────── */
static int upload_pressure_segment(int record_id,
                                   const sleep_session_summary_t *summary)
{
    char start_str[32], end_str[32];
    format_iso_time(summary->bed_time_ms, summary->bed_time_ms,
                    start_str, sizeof(start_str));
    format_iso_time(summary->wake_time_ms, summary->bed_time_ms,
                    end_str, sizeof(end_str));

    cJSON *body = cJSON_CreateObject();
    if (!body) return -3;

    cJSON_AddNumberToObject(body, "record_id",   record_id);
    cJSON_AddStringToObject(body, "start_time",  start_str);
    cJSON_AddStringToObject(body, "end_time",    end_str);
    cJSON_AddStringToObject(body, "level",       "medium");
    cJSON_AddNumberToObject(body, "avg_value",   0.0);

    char *json_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json_str) return -3;

    ESP_LOGI(TAG, "pressure_segments JSON: %s", json_str);
    int ret = cloudbase_post("pressure_segments", json_str, NULL, 0);
    cJSON_free(json_str);

    if (ret != 0) {
        ESP_LOGE(TAG, "pressure_segments 插入失败");
    }
    return ret;
}

/* ════════════════════════════════════════════════════
 *  公开接口: cloud_upload_sleep_record()
 * ════════════════════════════════════════════════════ */
int cloud_upload_sleep_record(const sleep_session_summary_t *summary,
                              const char *ai_report)
{
    if (!summary) {
        ESP_LOGE(TAG, "summary 为 NULL");
        return -3;
    }

    ESP_LOGI(TAG, "════════════════════════════════════════");
    ESP_LOGI(TAG, "开始上传睡眠数据到 CloudBase");
    ESP_LOGI(TAG, "  时长: %d 分钟, 评分: %d, 起身: %d 次",
             summary->duration_minutes, summary->sleep_score,
             summary->get_up_count);
    ESP_LOGI(TAG, "  鼾声: %.1f 分钟, 主要姿态: %s",
             summary->total_snore_minutes,
             posture_name(summary->dominant_posture));
    ESP_LOGI(TAG, "════════════════════════════════════════");

    int overall_result = 0;

    /* Step 1: 插入 sleep_records 并获取 record_id */
    int record_id = -1;
    int ret = upload_sleep_record(summary, &record_id);
    if (ret != 0) {
        ESP_LOGE(TAG, "sleep_records 上传失败 (ret=%d)，中止后续上传", ret);
        return ret;
    }
    ESP_LOGI(TAG, "✓ sleep_records 上传成功, record_id=%d", record_id);

    /* Step 2: 插入 ai_analyses */
    ret = upload_ai_analysis(record_id, ai_report, summary);
    if (ret != 0) {
        ESP_LOGW(TAG, "✗ ai_analyses 上传失败，继续...");
        overall_result = ret;
    } else {
        ESP_LOGI(TAG, "✓ ai_analyses 上传成功");
    }

    /* Step 3: 插入 snoring_events */
    ret = upload_snoring_event(record_id, summary);
    if (ret != 0) {
        ESP_LOGW(TAG, "✗ snoring_events 上传失败，继续...");
        if (overall_result == 0) overall_result = ret;
    } else {
        ESP_LOGI(TAG, "✓ snoring_events 上传成功");
    }

    /* Step 4: 插入 posture_segments */
    ret = upload_posture_segment(record_id, summary);
    if (ret != 0) {
        ESP_LOGW(TAG, "✗ posture_segments 上传失败，继续...");
        if (overall_result == 0) overall_result = ret;
    } else {
        ESP_LOGI(TAG, "✓ posture_segments 上传成功");
    }

    /* Step 5: 插入 pressure_segments */
    ret = upload_pressure_segment(record_id, summary);
    if (ret != 0) {
        ESP_LOGW(TAG, "✗ pressure_segments 上传失败，继续...");
        if (overall_result == 0) overall_result = ret;
    } else {
        ESP_LOGI(TAG, "✓ pressure_segments 上传成功");
    }

    ESP_LOGI(TAG, "════════════════════════════════════════");
    if (overall_result == 0) {
        ESP_LOGI(TAG, "全部上传完成 ✓");
    } else {
        ESP_LOGW(TAG, "上传部分失败 (result=%d)", overall_result);
    }
    ESP_LOGI(TAG, "════════════════════════════════════════");

    return overall_result;
}
