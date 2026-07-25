/**
 * pump_controller.c — 双气囊 GPIO 控制
 *
 * 硬件实际测试逻辑:
 *
 * GPIO7/8:
 *   左/右气泵 MOS驱动模块
 *   LOW  = 开泵 (PUMP_ON_LEVEL = 0)
 *   HIGH = 关泵 (PUMP_OFF_LEVEL = 1)
 *
 * GPIO9/10:
 *   左/右泄气阀 MOS驱动模块
 *   HIGH = 开阀
 *   LOW  = 关阀
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
static volatile bool s_cancel_requested = false;

/* GPIO 有效电平定义见 pump_controller.h */

/* 硬件安全上限：任何单次命令最多运行 60 秒 */
#define PUMP_MAX_DURATION_SEC  60



void pump_controller_stop_all(void)
{
    s_cancel_requested = true;
    gpio_set_level(GPIO_PUMP_LEFT, PUMP_OFF_LEVEL);
    gpio_set_level(GPIO_PUMP_RIGHT, PUMP_OFF_LEVEL);
    gpio_set_level(GPIO_VALVE_LEFT, VALVE_CLOSE_LEVEL);
    gpio_set_level(GPIO_VALVE_RIGHT, VALVE_CLOSE_LEVEL);
}

void pump_controller_init(void)
{
    /* Pre-set pump pins HIGH (off) before configuring as output to avoid
     * a brief LOW glitch that would momentarily activate the pumps. */
    gpio_set_level(GPIO_PUMP_LEFT, PUMP_OFF_LEVEL);
    gpio_set_level(GPIO_PUMP_RIGHT, PUMP_OFF_LEVEL);
    gpio_set_level(GPIO_VALVE_LEFT, VALVE_CLOSE_LEVEL);
    gpio_set_level(GPIO_VALVE_RIGHT, VALVE_CLOSE_LEVEL);

    /* 气泵引脚: pull-up 确保硬复位期间 GPIO 不浮空到 LOW (= 泵启动)。
     * 泄气阀引脚: 浮空 LOW = 阀关闭，安全，无需 pull。 */
    gpio_config_t pump_conf = {
        .pin_bit_mask =
            (1ULL << GPIO_PUMP_LEFT) |
            (1ULL << GPIO_PUMP_RIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config_t valve_conf = {
        .pin_bit_mask =
            (1ULL << GPIO_VALVE_LEFT) |
            (1ULL << GPIO_VALVE_RIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&pump_conf));
    ESP_ERROR_CHECK(gpio_config(&valve_conf));



    /*
     * 初始关闭所有执行器
     */

    pump_controller_stop_all();



    ESP_LOGI(TAG,
             "双气囊控制器初始化完成");


    ESP_LOGI(TAG,
             "Pump: GPIO%d GPIO%d (LOW=ON)",
             GPIO_PUMP_LEFT,
             GPIO_PUMP_RIGHT);


    ESP_LOGI(TAG,
             "Valve: GPIO%d GPIO%d (HIGH=OPEN)",
             GPIO_VALVE_LEFT,
             GPIO_VALVE_RIGHT);

}



/* 分段延时，每 100ms 检查 cancel flag */
static bool delay_with_cancel(int ms)
{
    const int step = 100;
    while (ms > 0 && !s_cancel_requested) {
        int chunk = (ms > step) ? step : ms;
        vTaskDelay(pdMS_TO_TICKS(chunk));
        ms -= chunk;
    }
    return s_cancel_requested;
}

/*
 * 单侧充气
 */
static void inflate_side(
        int pump_gpio,
        int valve_gpio,
        const char *side_name,
        int intensity,
        int duration_sec)
{
    if (duration_sec > PUMP_MAX_DURATION_SEC) {
        ESP_LOGW(TAG, "充气时长 %ds 超限，截断为 %ds", duration_sec, PUMP_MAX_DURATION_SEC);
        duration_sec = PUMP_MAX_DURATION_SEC;
    }

    gpio_set_level(valve_gpio, VALVE_CLOSE_LEVEL);

    ESP_LOGI(TAG, "充气: %s侧 强度=%d%% 持续=%ds",
             side_name, intensity, duration_sec);

    gpio_set_level(pump_gpio, PUMP_ON_LEVEL);

    int actual_ms = duration_sec * 1000;
    if (actual_ms < 1000)
        actual_ms = 1000;

    bool cancelled = delay_with_cancel(actual_ms);

    gpio_set_level(pump_gpio, PUMP_OFF_LEVEL);

    if (cancelled) {
        ESP_LOGW(TAG, "充气被中断: %s侧", side_name);
    } else {
        ESP_LOGI(TAG, "充气完成: %s侧 (%d ms)", side_name, actual_ms);
    }
}



/*
 * 单侧放气
 */
static void deflate_side(
        int pump_gpio,
        int valve_gpio,
        const char *side_name,
        int duration_sec)
{
    if (duration_sec > PUMP_MAX_DURATION_SEC) {
        ESP_LOGW(TAG, "放气时长 %ds 超限，截断为 %ds", duration_sec, PUMP_MAX_DURATION_SEC);
        duration_sec = PUMP_MAX_DURATION_SEC;
    }

    gpio_set_level(pump_gpio, PUMP_OFF_LEVEL);

    ESP_LOGI(TAG, "放气: %s侧 持续=%ds", side_name, duration_sec);

    gpio_set_level(valve_gpio, VALVE_OPEN_LEVEL);

    bool cancelled = delay_with_cancel(duration_sec * 1000);

    gpio_set_level(valve_gpio, VALVE_CLOSE_LEVEL);

    if (cancelled) {
        ESP_LOGW(TAG, "放气被中断: %s侧", side_name);
    } else {
        ESP_LOGI(TAG, "放气完成: %s侧", side_name);
    }
}





void pump_execute_command(
        const pump_command_t *cmd)
{
    s_cancel_requested = false;

    if(strcmp(cmd->action,"hold")==0)
    {

        ESP_LOGI(TAG,
                 "保持当前状态");

        return;

    }




    /*
     * ======================
     * inflate
     * ======================
     */

    if(strcmp(cmd->action,"inflate")==0)
    {


        if(strcmp(cmd->zone,"left")==0)
        {


            inflate_side(
                GPIO_PUMP_LEFT,
                GPIO_VALVE_LEFT,
                "左",
                cmd->intensity,
                cmd->duration_sec
            );


        }


        else if(strcmp(cmd->zone,"right")==0)
        {


            inflate_side(
                GPIO_PUMP_RIGHT,
                GPIO_VALVE_RIGHT,
                "右",
                cmd->intensity,
                cmd->duration_sec
            );


        }


        else if(strcmp(cmd->zone,"both")==0)
        {


            /*
             * 双侧充气
             */

            gpio_set_level(GPIO_VALVE_LEFT, VALVE_CLOSE_LEVEL);
            gpio_set_level(GPIO_VALVE_RIGHT, VALVE_CLOSE_LEVEL);

            gpio_set_level(GPIO_PUMP_LEFT, PUMP_ON_LEVEL);
            gpio_set_level(GPIO_PUMP_RIGHT, PUMP_ON_LEVEL);

            int capped_sec = cmd->duration_sec;
            if (capped_sec > PUMP_MAX_DURATION_SEC)
                capped_sec = PUMP_MAX_DURATION_SEC;

            int actual_ms = capped_sec * 1000;
            if (actual_ms < 1000)
                actual_ms = 1000;

            bool cancelled = delay_with_cancel(actual_ms);

            gpio_set_level(GPIO_PUMP_LEFT, PUMP_OFF_LEVEL);
            gpio_set_level(GPIO_PUMP_RIGHT, PUMP_OFF_LEVEL);

            if (cancelled) {
                ESP_LOGW(TAG, "双侧充气被中断");
            } else {
                ESP_LOGI(TAG, "双侧充气完成");
            }

        }

        else
        {
            ESP_LOGW(TAG, "充气: 未知 zone '%s'，忽略", cmd->zone);
        }

    }



    /*
     * ======================
     * deflate
     * ======================
     */

    else if(strcmp(cmd->action,"deflate")==0)
    {


        if(strcmp(cmd->zone,"left")==0)
        {


            deflate_side(
                GPIO_PUMP_LEFT,
                GPIO_VALVE_LEFT,
                "左",
                cmd->duration_sec
            );


        }


        else if(strcmp(cmd->zone,"right")==0)
        {


            deflate_side(
                GPIO_PUMP_RIGHT,
                GPIO_VALVE_RIGHT,
                "右",
                cmd->duration_sec
            );


        }


        else if(strcmp(cmd->zone,"both")==0)
        {

            gpio_set_level(GPIO_PUMP_LEFT, PUMP_OFF_LEVEL);
            gpio_set_level(GPIO_PUMP_RIGHT, PUMP_OFF_LEVEL);

            int deflate_sec = cmd->duration_sec;
            if (deflate_sec > PUMP_MAX_DURATION_SEC)
                deflate_sec = PUMP_MAX_DURATION_SEC;

            gpio_set_level(GPIO_VALVE_LEFT, VALVE_OPEN_LEVEL);
            gpio_set_level(GPIO_VALVE_RIGHT, VALVE_OPEN_LEVEL);

            bool cancelled = delay_with_cancel(deflate_sec * 1000);

            gpio_set_level(GPIO_VALVE_LEFT, VALVE_CLOSE_LEVEL);
            gpio_set_level(GPIO_VALVE_RIGHT, VALVE_CLOSE_LEVEL);

            if (cancelled) {
                ESP_LOGW(TAG, "双侧放气被中断");
            } else {
                ESP_LOGI(TAG, "双侧放气完成");
            }

        }

        else
        {
            ESP_LOGW(TAG, "放气: 未知 zone '%s'，忽略", cmd->zone);
        }


    }


    else
    {

        ESP_LOGW(TAG,
                 "未知动作: %s",
                 cmd->action);

    }

}
