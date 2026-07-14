/**
 * wifi_manager.h — Wi-Fi STA 连接管理
 */
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"

/* Wi-Fi 配置 — 根据实际路由器修改 */
#define WIFI_SSID       "OnePlus 12 from C137"
#define WIFI_PASSWORD   "062627Li!"
#define WIFI_MAX_RETRY  10

/**
 * 初始化 Wi-Fi 并以 STA 模式连接到路由器。
 * 此函数会阻塞直到获取到 IP 地址或重试次数耗尽。
 *
 * 内部流程: NVS init → event loop → netif → Wi-Fi driver → connect
 *
 * @return ESP_OK 连接成功，ESP_FAIL 连接失败
 */
esp_err_t wifi_manager_init(void);

#endif /* WIFI_MANAGER_H */
