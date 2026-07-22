/**
 * pump_controller.c — 双气囊 GPIO 控制
 *
 * 对齐 airbag-hardware 分支:
 *
 * GPIO7/8 → 左/右气泵
 *            (MOS驱动模块, HIGH=开泵)
 *
 * GPIO9/10 → 左/右泄气阀
 *            (MOS驱动模块, HIGH=开阀放气)
 *
 * 所有执行器均使用MOS驱动模块
 *
 * 安全约束:
 *   同侧泵 + 阀禁止同时开启
 */

#include <string.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "pump_controller.h"


static const char *TAG = "PUMP";


void pump_controller_init(void)
{
    /*
     * 配置所有执行器控制GPIO
     *
     * GPIO7/8:
     *   气泵MOS模块 High Trigger+
     *
     * GPIO9/10:
     *   泄气阀MOS模块 High Trigger+
     */
    gpio_config_t io_conf = {
        .pin_bit_mask =
            (1ULL << GPIO_PUMP_LEFT)   |
            (1ULL << GPIO_PUMP_RIGHT)  |
            (1ULL << GPIO_VALVE_LEFT)  |
            (1ULL << GPIO_VALVE_RIGHT),

        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));


    /*
     * 初始状态:
     *
     * LOW:
     *   气泵关闭
     *   泄气阀关闭
     */
    gpio_set_level(GPIO_PUMP_LEFT,   0);
    gpio_set_level(GPIO_PUMP_RIGHT,  0);

    gpio_set_level(GPIO_VALVE_LEFT,  0);
    gpio_set_level(GPIO_VALVE_RIGHT, 0);


    ESP_LOGI(TAG, "双气囊控制器初始化完成");

    ESP_LOGI(TAG,
             "气泵MOS模块: left=GPIO%d right=GPIO%d",
             GPIO_PUMP_LEFT,
             GPIO_PUMP_RIGHT);

    ESP_LOGI(TAG,
             "泄气阀MOS模块: left=GPIO%d right=GPIO%d",
             GPIO_VALVE_LEFT,
             GPIO_VALVE_RIGHT);
}


/*
 * 单侧充气
 */
static void inflate_side(int pump_gpio,
                         int valve_gpio,
                         const char *side_name,
                         int intensity,
                         int duration_sec)
{
    /*
     * 安全:
     * 充气前关闭泄气阀
     */
    gpio_set_level(valve_gpio, 0);


    ESP_LOGI(TAG,
             "充气: %s侧 强度=%d%% 持续=%ds",
             side_name,
             intensity,
             duration_sec);


    /*
     * HIGH:
     * 打开气泵MOS模块
     */
    gpio_set_level(pump_gpio, 1);


    int actual_ms = duration_sec * intensity * 10;

    if (actual_ms < 1000)
        actual_ms = 1000;


    vTaskDelay(pdMS_TO_TICKS(actual_ms));


    gpio_set_level(pump_gpio, 0);


    ESP_LOGI(TAG,
             "充气完成: %s侧 (%d ms)",
             side_name,
             actual_ms);
}


/*
 * 单侧放气
 */
static void deflate_side(int pump_gpio,
                         int valve_gpio,
                         const char *side_name,
                         int duration_sec)
{
    /*
     * 安全:
     * 放气前关闭气泵
     */
    gpio_set_level(pump_gpio, 0);


    ESP_LOGI(TAG,
             "放气: %s侧 持续=%ds",
             side_name,
             duration_sec);


    /*
     * HIGH:
     * 打开泄气阀MOS模块
     */
    gpio_set_level(valve_gpio, 1);


    vTaskDelay(pdMS_TO_TICKS(duration_sec * 1000));


    /*
     * LOW:
     * 关闭泄气阀
     */
    gpio_set_level(valve_gpio, 0);


    ESP_LOGI(TAG,
             "放气完成: %s侧",
             side_name);
}

void pump_execute_command(const pump_command_t *cmd)
{
    /* hold → 不操作 */
    if (strcmp(cmd->action, "hold") == 0) {
        ESP_LOGI(TAG, "保持当前状态 (hold)");
        return;
    }

    if (strcmp(cmd->action, "inflate") == 0) {
        if (strcmp(cmd->zone, "left") == 0) {
            inflate_side(GPIO_PUMP_LEFT, GPIO_VALVE_LEFT, "左",
                        cmd->intensity, cmd->duration_sec);
        } else if (strcmp(cmd->zone, "right") == 0) {
            inflate_side(GPIO_PUMP_RIGHT, GPIO_VALVE_RIGHT, "右",
                        cmd->intensity, cmd->duration_sec);
        } else if (strcmp(cmd->zone, "both") == 0) {
            /* 两侧同时充气 */
            gpio_set_level(GPIO_VALVE_LEFT, 0);
            gpio_set_level(GPIO_VALVE_RIGHT, 0);
            gpio_set_level(GPIO_PUMP_LEFT, 1);
            gpio_set_level(GPIO_PUMP_RIGHT, 1);

            int actual_ms = cmd->duration_sec * cmd->intensity * 10;
            if (actual_ms < 1000) actual_ms = 1000;
            vTaskDelay(pdMS_TO_TICKS(actual_ms));

            gpio_set_level(GPIO_PUMP_LEFT, 0);
            gpio_set_level(GPIO_PUMP_RIGHT, 0);
            ESP_LOGI(TAG, "双侧充气完成 (%d ms)", actual_ms);
        } else {
            ESP_LOGW(TAG, "未知区域: %s, 跳过", cmd->zone);
        }

    } else if (strcmp(cmd->action, "deflate") == 0) {
        if (strcmp(cmd->zone, "left") == 0) {
            deflate_side(GPIO_PUMP_LEFT, GPIO_VALVE_LEFT, "左",
                        cmd->duration_sec);
        } else if (strcmp(cmd->zone, "right") == 0) {
            deflate_side(GPIO_PUMP_RIGHT, GPIO_VALVE_RIGHT, "右",
                        cmd->duration_sec);
        } else if (strcmp(cmd->zone, "both") == 0) {
            gpio_set_level(GPIO_PUMP_LEFT, 0);
            gpio_set_level(GPIO_PUMP_RIGHT, 0);
            gpio_set_level(GPIO_VALVE_LEFT, 1);
            gpio_set_level(GPIO_VALVE_RIGHT, 1);
            vTaskDelay(pdMS_TO_TICKS(cmd->duration_sec * 1000));
            gpio_set_level(GPIO_VALVE_LEFT, 0);
            gpio_set_level(GPIO_VALVE_RIGHT, 0);
            ESP_LOGI(TAG, "双侧放气完成");
        } else {
            ESP_LOGW(TAG, "未知区域: %s, 跳过", cmd->zone);
        }

    } else {
        ESP_LOGW(TAG, "未知动作: %s", cmd->action);
    }
} 