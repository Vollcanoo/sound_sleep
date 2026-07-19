/**
 * ble_uart_server.h — NimBLE GATT Server (Nordic UART Service)
 *
 * 对接 frontier 分支 Flutter App (BleDataService):
 *   Service UUID: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
 *   TX (ESP32→手机 Notify): 6e400003-b5a3-f393-e0a9-e50e24dcca9e
 *   RX (手机→ESP32 Write):  6e400002-b5a3-f393-e0a9-e50e24dcca9e
 *
 * 数据格式: UTF-8 CSV 行，'\n' 结尾，15 字段
 *   raw_left,raw_center,raw_right,
 *   median_left,median_center,median_right,
 *   total_pressure,
 *   left_ratio,center_ratio,right_ratio,
 *   x_center_cm,y_center_cm,
 *   moving(0/1),posture(SUPINE/LEFT_SIDE/...),confidence
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
