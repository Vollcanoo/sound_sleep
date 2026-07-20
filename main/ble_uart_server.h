/**
 * ble_uart_server.h — NimBLE GATT Server (Nordic UART Service)
 *
 * 对接 frontier 分支 Flutter App (BleDataService):
 *   Service UUID: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
 *   TX (ESP32→手机 Notify): 6e400003-b5a3-f393-e0a9-e50e24dcca9e
 *   RX (手机→ESP32 Write):  6e400002-b5a3-f393-e0a9-e50e24dcca9e
 *
 * 功能:
 *   1. WiFi 配网: 接收手机发送的 WiFi 凭据 JSON
 *   2. 数据推送: CSV 行发送姿态数据给手机 (可选，配网后 BLE 停止)
 *   3. 控制指令: 接收手机发送的控制指令 (如 'b' 校准)
 *
 * 广播名称: "SleepMonitor"
 */
#ifndef BLE_UART_SERVER_H
#define BLE_UART_SERVER_H

#include <stdbool.h>
#include <stddef.h>

/**
 * 初始化 NimBLE 栈，注册 NUS Service，开始 GAP 广播。
 * 调用一次即可，之后自动广播等待手机连接。
 */
void ble_uart_server_init(void);

/**
 * 停止 BLE 广播并断开连接。
 * WiFi 连接成功后调用以节省功耗。
 */
void ble_uart_server_stop(void);

/**
 * 重新开启 BLE 广播。
 * WiFi 断开后调用，让手机可以重新发现设备进行配网。
 */
void ble_uart_server_restart(void);

/**
 * 通过 BLE Notify 发送数据。
 * 如果无客户端连接，静默丢弃。
 * 如果数据超过 MTU，自动分包发送。
 *
 * @param data  要发送的数据（UTF-8 字符串）
 * @param len   数据长度（字节）
 */
void ble_uart_send(const char *data, size_t len);

/**
 * 查询是否有客户端已连接。
 */
bool ble_uart_is_connected(void);

#endif /* BLE_UART_SERVER_H */
