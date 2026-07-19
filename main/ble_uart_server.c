/**
 * ble_uart_server.c — NimBLE GATT Server 实现 (Nordic UART Service)
 *
 * 对接 frontier 分支 Flutter App (lib/services/ble_data_service.dart)
 * 使用 ESP-IDF NimBLE 栈实现 BLE 外设角色。
 *
 * 功能:
 *   - GAP 广播设备名 "SleepMonitor"
 *   - GATT 注册 Nordic UART Service (NUS)
 *   - TX Characteristic: Notify 推送 CSV 数据给手机
 *   - RX Characteristic: 接收手机写入的控制指令
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

/* NimBLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble_uart_server.h"

static const char *TAG = "BLE_UART";

/* ── 设备名称 ─────────────────────────────────────── */
#define DEVICE_NAME  "SleepMonitor"

/* ── Nordic UART Service UUIDs (128-bit) ──────────── */
static const ble_uuid128_t NUS_SERVICE_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

static const ble_uuid128_t NUS_RX_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

static const ble_uuid128_t NUS_TX_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

/* ── 连接状态 ─────────────────────────────────────── */
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_attr_handle = 0;
static bool     s_notify_enabled = false;

/* ────────────────────────────────────────────────────
 *  GATT Access 回调
 * ──────────────────────────────────────────────────── */

/* RX Characteristic — 手机写入数据到这里 */
static int nus_rx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 0 && len < 128) {
            char buf[128] = {0};
            uint16_t copied = 0;
            ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &copied);
            buf[copied] = '\0';
            ESP_LOGI(TAG, "RX 收到指令: %s (len=%d)", buf, copied);

            /* 处理已知指令 */
            if (buf[0] == 'b' || buf[0] == 'B') {
                ESP_LOGI(TAG, "收到校准指令 'b'");
                /* TODO: 设置全局标志触发传感器重校准 */
            }
        }
    }
    return 0;
}

/* TX Characteristic — 仅用于 Notify，不支持直接 Read */
static int nus_tx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* TX 特征不支持 Read/Write，只通过 Notify 推送 */
    return 0;
}

/* ── GATT Service 定义 ────────────────────────────── */
static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &NUS_SERVICE_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* RX Characteristic: 手机→ESP32 (Write / Write Without Response) */
                .uuid       = &NUS_RX_UUID.u,
                .access_cb  = nus_rx_access_cb,
                .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                /* TX Characteristic: ESP32→手机 (Notify) */
                .uuid       = &NUS_TX_UUID.u,
                .access_cb  = nus_tx_access_cb,
                .val_handle = &s_tx_attr_handle,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }, /* 终止标记 */
        },
    },
    { 0 }, /* 终止标记 */
};

/* ────────────────────────────────────────────────────
 *  GAP 事件回调
 * ──────────────────────────────────────────────────── */
static void start_advertising(void);

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "📱 客户端已连接 (handle=%d)", s_conn_handle);
        } else {
            ESP_LOGW(TAG, "连接失败, status=%d", event->connect.status);
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "📱 客户端已断开 (reason=%d)", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_notify_enabled = false;
        start_advertising();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_attr_handle) {
            s_notify_enabled = event->subscribe.cur_notify != 0;
            ESP_LOGI(TAG, "TX Notify %s", s_notify_enabled ? "已开启" : "已关闭");
        }
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU 更新: %d", event->mtu.value);
        break;

    default:
        break;
    }
    return 0;
}

/* ────────────────────────────────────────────────────
 *  广播配置
 * ──────────────────────────────────────────────────── */
static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};

    /* 广播内容 */
    fields.flags                 = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl            = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name                  = (uint8_t *)DEVICE_NAME;
    fields.name_len              = strlen(DEVICE_NAME);
    fields.name_is_complete      = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "设置广播字段失败: %d", rc);
        return;
    }

    /* 扫描响应中放 Service UUID */
    struct ble_hs_adv_fields rsp_fields = {0};
    rsp_fields.uuids128          = (ble_uuid128_t[]){ NUS_SERVICE_UUID };
    rsp_fields.num_uuids128      = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "设置扫描响应失败: %d", rc);
        return;
    }

    /* 广播参数: 可连接, 不定向 */
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "开始广播失败: %d", rc);
    } else {
        ESP_LOGI(TAG, "📡 BLE 广播已开始: \"%s\"", DEVICE_NAME);
    }
}

/* ────────────────────────────────────────────────────
 *  NimBLE Host 同步回调（栈就绪后开始广播）
 * ──────────────────────────────────────────────────── */
static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "确保 BLE 地址失败");
        return;
    }
    start_advertising();
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE 主机复位, reason=%d", reason);
}

/* ────────────────────────────────────────────────────
 *  NimBLE Host 任务
 * ──────────────────────────────────────────────────── */
static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE Host 任务启动");
    nimble_port_run();         /* 此函数不会返回 */
    nimble_port_freertos_deinit();
}

/* ════════════════════════════════════════════════════
 *  公开接口
 * ════════════════════════════════════════════════════ */

void ble_uart_server_init(void)
{
    ESP_LOGI(TAG, "初始化 NimBLE UART Server...");

    /* 初始化 NimBLE */
    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init() 失败: %d", rc);
        return;
    }

    /* 配置 NimBLE Host */
    ble_hs_cfg.reset_cb  = ble_on_reset;
    ble_hs_cfg.sync_cb   = ble_on_sync;

    /* 注册 GATT 服务 */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg 失败: %d", rc);
        return;
    }
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs 失败: %d", rc);
        return;
    }

    /* 设置设备名称 */
    ble_svc_gap_device_name_set(DEVICE_NAME);

    /* 启动 NimBLE Host 任务 */
    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "✅ BLE UART Server 初始化完成 (设备名: %s)", DEVICE_NAME);
}

void ble_uart_send(const char *data, size_t len)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_notify_enabled) {
        return;  /* 无连接或未订阅，静默丢弃 */
    }

    /* 获取当前 MTU 并分包发送 */
    uint16_t mtu = ble_att_mtu(s_conn_handle);
    if (mtu < 23) mtu = 23;           /* 最小 MTU */
    uint16_t chunk_size = mtu - 3;     /* ATT Notify 头部 3 字节 */

    size_t offset = 0;
    while (offset < len) {
        uint16_t send_len = (len - offset > chunk_size) ? chunk_size : (uint16_t)(len - offset);

        struct os_mbuf *om = ble_hs_mbuf_from_flat(data + offset, send_len);
        if (om == NULL) {
            ESP_LOGW(TAG, "mbuf 分配失败");
            break;
        }

        int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_attr_handle, om);
        if (rc != 0) {
            ESP_LOGW(TAG, "Notify 发送失败: %d (offset=%zu)", rc, offset);
            break;
        }

        offset += send_len;
    }
}

bool ble_uart_is_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE && s_notify_enabled;
}
