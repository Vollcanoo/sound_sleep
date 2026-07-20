/**
 * wifi_manager.h — Wi-Fi STA 连接管理
 *
 * 分两步使用:
 *   1. wifi_manager_init() — 初始化协议栈（NVS/netif/event loop/驱动），不连接
 *   2. wifi_manager_connect(ssid, pass) — 使用指定凭据连接，阻塞等待结果
 *
 * WiFi 断开时自动重试，重试耗尽后调用回调通知上层重新进入 BLE 配网模式。
 */
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

#define WIFI_MAX_RETRY  10

/**
 * WiFi 断开回调 — 重试耗尽后调用。
 * 上层（main.c）可在此回调中清除 NVS 凭据并重新开启 BLE 广播。
 */
typedef void (*wifi_disconnect_cb_t)(void);

/**
 * 初始化 Wi-Fi 子系统（NVS、TCP/IP 栈、事件循环、Wi-Fi 驱动）。
 * 不会连接任何网络 — 需要后续调用 wifi_manager_connect()。
 *
 * @return ESP_OK 初始化成功
 */
esp_err_t wifi_manager_init(void);

/**
 * 以 STA 模式连接到指定 Wi-Fi 热点。
 * 此函数会阻塞直到获取到 IP 地址或重试次数耗尽。
 *
 * @param ssid     WiFi 名称 (仅 2.4GHz)
 * @param password WiFi 密码
 * @return ESP_OK 连接成功，ESP_FAIL 连接失败
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * 查询当前 WiFi 是否已连接
 */
bool wifi_manager_is_connected(void);

/**
 * 注册 WiFi 断开回调（重试耗尽后触发）
 */
void wifi_manager_set_disconnect_cb(wifi_disconnect_cb_t cb);

#endif /* WIFI_MANAGER_H */
