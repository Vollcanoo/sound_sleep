/**
 * wifi_provision.c — BLE WiFi 配网实现
 *
 * 解析手机通过 BLE NUS 发送的 WiFi 凭据 JSON，
 * 持久化到 NVS Flash，并通过 BLE 回复结果。
 */
#include "wifi_provision.h"

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "ble_uart_server.h"

static const char *TAG = "PROVISION";

#define NVS_NAMESPACE   "wifi_cred"
#define NVS_KEY_SSID    "ssid"
#define NVS_KEY_PASS    "pass"

#define WPA2_MIN_PASS_LEN  8

/* ---- 内部辅助 ---------------------------------------------------------- */

/**
 * 通过 BLE 发送 JSON 格式的应答
 */
static void reply_ble(const char *status, const char *msg)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create reply JSON");
        return;
    }
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddStringToObject(root, "msg", msg);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        ble_uart_send(json_str, strlen(json_str));
        free(json_str);
    }
    cJSON_Delete(root);
}

/* ---- 公开 API ---------------------------------------------------------- */

esp_err_t wifi_provision_handle_ble_data(const char *data, uint16_t len)
{
    /* 安全拷贝到 null-terminated 缓冲区 */
    char buf[256];
    if (len >= sizeof(buf)) {
        ESP_LOGE(TAG, "BLE data too long (%u bytes)", len);
        reply_ble("error", "payload_too_long");
        return ESP_FAIL;
    }
    memcpy(buf, data, len);
    buf[len] = '\0';

    ESP_LOGI(TAG, "BLE RX (%u bytes): %s", len, buf);

    /* 解析 JSON */
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        reply_ble("error", "invalid_json");
        return ESP_FAIL;
    }

    /* 检查 cmd 字段 */
    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd) || strcmp(cmd->valuestring, "wifi_config") != 0) {
        ESP_LOGW(TAG, "Unknown or missing cmd");
        reply_ble("error", "unknown_cmd");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    /* 提取 ssid 和 pass */
    cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass_item = cJSON_GetObjectItem(root, "pass");

    if (!cJSON_IsString(ssid_item) || !cJSON_IsString(pass_item)) {
        ESP_LOGE(TAG, "Missing ssid or pass field");
        reply_ble("error", "missing_fields");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    const char *ssid = ssid_item->valuestring;
    const char *pass = pass_item->valuestring;

    /* 验证: SSID 非空 */
    if (strlen(ssid) == 0) {
        ESP_LOGE(TAG, "SSID is empty");
        reply_ble("error", "ssid_empty");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    /* 验证: 密码长度 >= 8 (WPA2 minimum) */
    if (strlen(pass) < WPA2_MIN_PASS_LEN) {
        ESP_LOGE(TAG, "Password too short (%d < %d)", (int)strlen(pass), WPA2_MIN_PASS_LEN);
        reply_ble("error", "pass_too_short");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    /* 保存到 NVS */
    esp_err_t err = wifi_provision_save_credentials(ssid, pass);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        reply_ble("error", "nvs_write_failed");
        return err;
    }

    ESP_LOGI(TAG, "WiFi credentials saved — SSID: %s", ssid);
    reply_ble("ok", "credentials_saved");

    /* 通知主任务凭据已就绪 */
    if (g_provision_event_group) {
        xEventGroupSetBits(g_provision_event_group, PROVISION_CRED_RECEIVED_BIT);
    }

    return ESP_OK;
}

bool wifi_provision_has_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t required = 0;
    err = nvs_get_str(handle, NVS_KEY_SSID, NULL, &required);
    nvs_close(handle);

    return (err == ESP_OK && required > 1);   /* required 含 '\0' */
}

esp_err_t wifi_provision_load_credentials(char *ssid, size_t ssid_size,
                                          char *pass, size_t pass_size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t len = ssid_size;
    err = nvs_get_str(handle, NVS_KEY_SSID, ssid, &len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS read ssid failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    len = pass_size;
    err = nvs_get_str(handle, NVS_KEY_PASS, pass, &len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS read pass failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Credentials loaded — SSID: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_provision_save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS write ssid failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_set_str(handle, NVS_KEY_PASS, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS write pass failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t wifi_provision_clear_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_erase_key(handle, NVS_KEY_SSID);
    nvs_erase_key(handle, NVS_KEY_PASS);

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "WiFi credentials cleared");
    return err;
}
