/**
 * pump_controller.c — 气泵 GPIO 继电器控制
 *
 * 通过 GPIO 高电平驱动继电器，继电器控制 12V/24V 气泵和电磁阀。
 * 三路独立气泵对应头部/肩部/腰部气囊，一路放气电磁阀。
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
    /* 配置所有气泵/阀门 GPIO 为推挽输出 */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_PUMP_HEAD)     |
                        (1ULL << GPIO_PUMP_SHOULDER)  |
                        (1ULL << GPIO_PUMP_WAIST)     |
                        (1ULL << GPIO_VALVE_DEFLATE),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* 初始状态: 全部关闭（低电平 = 继电器断开） */
    gpio_set_level(GPIO_PUMP_HEAD,     0);
    gpio_set_level(GPIO_PUMP_SHOULDER, 0);
    gpio_set_level(GPIO_PUMP_WAIST,    0);
    gpio_set_level(GPIO_VALVE_DEFLATE, 0);

    ESP_LOGI(TAG, "气泵控制器初始化完成 (GPIO: head=%d shoulder=%d waist=%d valve=%d)",
             GPIO_PUMP_HEAD, GPIO_PUMP_SHOULDER, GPIO_PUMP_WAIST, GPIO_VALVE_DEFLATE);
}

void pump_execute_command(const pump_command_t *cmd)
{
    /* hold → 不操作 */
    if (strcmp(cmd->action, "hold") == 0) {
        ESP_LOGI(TAG, "保持当前状态 (hold)");
        return;
    }

    /* 确定目标区域对应的 GPIO */
    int gpio_pin = -1;
    if (strcmp(cmd->zone, "head") == 0) {
        gpio_pin = GPIO_PUMP_HEAD;
    } else if (strcmp(cmd->zone, "shoulder") == 0) {
        gpio_pin = GPIO_PUMP_SHOULDER;
    } else if (strcmp(cmd->zone, "waist") == 0) {
        gpio_pin = GPIO_PUMP_WAIST;
    } else {
        ESP_LOGW(TAG, "未知区域: %s, 跳过", cmd->zone);
        return;
    }

    if (strcmp(cmd->action, "inflate") == 0) {
        /* 充气: 开启对应区域气泵 */
        ESP_LOGI(TAG, "充气: 区域=%s 强度=%d%% 持续=%ds",
                 cmd->zone, cmd->intensity, cmd->duration_sec);

        gpio_set_level(gpio_pin, 1);   /* 开泵 */

        /*
         * 简单控制策略: 用占空比模拟强度
         * intensity 0-100 映射为充气持续时间的比例
         * 例: intensity=60, duration=15s → 实际充气 9s
         *
         * 若需更精确控制，可改用 LEDC PWM 驱动 MOSFET
         */
        int actual_ms = cmd->duration_sec * cmd->intensity * 10;  /* ms */
        if (actual_ms < 1000) actual_ms = 1000;   /* 最少 1 秒 */
        vTaskDelay(pdMS_TO_TICKS(actual_ms));

        gpio_set_level(gpio_pin, 0);   /* 关泵 */
        ESP_LOGI(TAG, "充气完成: %s (实际 %d ms)", cmd->zone, actual_ms);

    } else if (strcmp(cmd->action, "deflate") == 0) {
        /* 放气: 开启电磁阀 */
        ESP_LOGI(TAG, "放气: 区域=%s 持续=%ds", cmd->zone, cmd->duration_sec);

        gpio_set_level(GPIO_VALVE_DEFLATE, 1);   /* 开阀放气 */
        vTaskDelay(pdMS_TO_TICKS(cmd->duration_sec * 1000));
        gpio_set_level(GPIO_VALVE_DEFLATE, 0);   /* 关阀 */

        ESP_LOGI(TAG, "放气完成");

    } else {
        ESP_LOGW(TAG, "未知动作: %s", cmd->action);
    }
}
