/**
 * wifi_provision.h — BLE WiFi 配网
 *
 * 通过 BLE Nordic UART Service 接收手机发送的 WiFi 凭据，
 * 解析 JSON 并持久化存储到 NVS Flash。
 *
 * JSON 协议:
 *   手机→设备: {"cmd":"wifi_config","ssid":"MyNetwork","pass":"MyPassword"}
 *   设备→手机: {"status":"ok","msg":"credentials_saved"}
 *              {"status":"error","msg":"invalid_json"}
 *
 * NVS 存储:
 *   Namespace: "wifi_cred"
 *   Keys: "ssid" (max 32B), "pass" (max 64B)
 */
#ifndef WIFI_PROVISION_H
#define WIFI_PROVISION_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/* EventGroup bit — 收到 WiFi 凭据后设置 */
#define PROVISION_CRED_RECEIVED_BIT  BIT0

/* 全局事件组 — 由 main.c 创建，wifi_provision 设置 bit */
extern EventGroupHandle_t g_provision_event_group;

/**
 * 处理从 BLE RX 收到的数据。
 * 如果是有效的 wifi_config JSON，解析并保存到 NVS，
 * 然后设置 PROVISION_CRED_RECEIVED_BIT。
 * 通过 BLE TX 回复处理结果给手机。
 *
 * @param data  收到的 UTF-8 字符串
 * @param len   数据长度
 * @return ESP_OK 凭据已保存, ESP_FAIL 解析失败或非配网指令
 */
esp_err_t wifi_provision_handle_ble_data(const char *data, uint16_t len);

/**
 * 检查 NVS 中是否有已保存的 WiFi 凭据
 */
bool wifi_provision_has_credentials(void);

/**
 * 从 NVS 读取已保存的凭据
 *
 * @param ssid      SSID 输出缓冲区
 * @param ssid_size 缓冲区大小 (建议 33)
 * @param pass      密码输出缓冲区
 * @param pass_size 缓冲区大小 (建议 65)
 */
esp_err_t wifi_provision_load_credentials(char *ssid, size_t ssid_size,
                                          char *pass, size_t pass_size);

/**
 * 保存 WiFi 凭据到 NVS
 */
esp_err_t wifi_provision_save_credentials(const char *ssid, const char *pass);

/**
 * 清除 NVS 中的 WiFi 凭据（用于重置/重新配网）
 */
esp_err_t wifi_provision_clear_credentials(void);

#endif /* WIFI_PROVISION_H */
