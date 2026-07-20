/**
 * wifi_manager.c — Wi-Fi STA 连接管理
 *
 * 基于 ESP-IDF 官方 station 示例，使用事件驱动模型。
 * 分两步: init (仅初始化协议栈) + connect (使用指定凭据连接)。
 * 支持断线自动重连，重试耗尽后通过回调通知上层。
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"

static const char *TAG = "WIFI";

/* FreeRTOS 事件组用于通知连接状态 */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static int s_retry_num = 0;
static bool s_is_connected = false;
static bool s_initialized = false;
static wifi_disconnect_cb_t s_disconnect_cb = NULL;

/* Wi-Fi 和 IP 事件处理回调 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_connected = false;
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "重连 Wi-Fi... 第 %d 次", s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Wi-Fi 连接失败，已重试 %d 次", WIFI_MAX_RETRY);
            /* 通知上层：WiFi 彻底断开，需要重新配网 */
            if (s_disconnect_cb) {
                s_disconnect_cb();
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "获取到 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        s_is_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Wi-Fi 已初始化，跳过");
        return ESP_OK;
    }

    /* 初始化 NVS — Wi-Fi 驱动需要 */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();

    /* 初始化 TCP/IP 协议栈 */
    ESP_ERROR_CHECK(esp_netif_init());

    /* 创建默认事件循环 */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 创建默认 Wi-Fi STA 网络接口 */
    esp_netif_create_default_wifi_sta();

    /* 初始化 Wi-Fi 驱动 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 注册事件处理函数 */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    s_initialized = true;
    ESP_LOGI(TAG, "Wi-Fi 子系统初始化完成（未连接）");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Wi-Fi 未初始化，请先调用 wifi_manager_init()");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "正在连接 Wi-Fi: SSID=%s", ssid);

    /* 重置状态 */
    s_retry_num = 0;
    s_is_connected = false;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    /* 配置 Wi-Fi STA 参数 */
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    /* 复制 SSID 和密码 */
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi STA 已启动，正在连接 %s ...", ssid);

    /* 阻塞等待连接结果 */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "✅ Wi-Fi 连接成功: SSID=%s", ssid);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "❌ Wi-Fi 连接失败: SSID=%s", ssid);
        return ESP_FAIL;
    }
}

bool wifi_manager_is_connected(void)
{
    return s_is_connected;
}

void wifi_manager_set_disconnect_cb(wifi_disconnect_cb_t cb)
{
    s_disconnect_cb = cb;
}
