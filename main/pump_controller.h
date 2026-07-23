/**
 * pump_controller.h — 双气囊 GPIO 控制
 *
 * 对齐 airbag-hardware 分支硬件设计:
 *
 *   - 左/右两路独立气泵
 *   - 左/右两路独立放气电磁阀
 *
 *
 * 实际硬件测试逻辑:
 *
 *   GPIO7/8  = 左/右气泵
 *                (MOS驱动模块)
 *
 *                LOW  = 开启气泵
 *                HIGH = 关闭气泵
 *
 *
 *   GPIO9/10 = 左/右泄气阀
 *                (MOS驱动模块)
 *
 *                HIGH = 打开放气
 *                LOW  = 关闭泄气阀
 *
 *
 *   双电池分轨供电
 *   ESP32 与 MOS 模块共地
 *
 *
 * 注意:
 *   GPIO4/5/6 已被 Posture_Recognition 分支占用
 *   (FSR 压力传感器 ADC)
 *
 *   GPIO16/15/17 已被 Snore_Det 分支占用
 *   (INMP441 I2S 麦克风)
 */


#ifndef PUMP_CONTROLLER_H
#define PUMP_CONTROLLER_H



#include "cloud_llm_client.h"


/*
 * ================================
 * GPIO 引脚分配
 * ================================
 *
 *
 * 左气泵:
 *
 * ESP32 GPIO7
 *      |
 *      ↓
 * 左气泵 MOS驱动模块 Trigger
 *      |
 *      ↓
 * 左气泵
 *
 *
 *
 * 右气泵:
 *
 * ESP32 GPIO8
 *      |
 *      ↓
 * 右气泵 MOS驱动模块 Trigger
 *      |
 *      ↓
 * 右气泵
 *
 *
 *
 * 左泄气阀:
 *
 * ESP32 GPIO9
 *      |
 *      ↓
 * 左泄气阀 MOS驱动模块 Trigger
 *      |
 *      ↓
 * 左电磁阀
 *
 *
 *
 * 右泄气阀:
 *
 * ESP32 GPIO10
 *      |
 *      ↓
 * 右泄气阀 MOS驱动模块 Trigger
 *      |
 *      ↓
 * 右电磁阀
 *
 */


#define GPIO_PUMP_LEFT       7
#define GPIO_PUMP_RIGHT      8


#define GPIO_VALVE_LEFT      9
#define GPIO_VALVE_RIGHT     10



/*
 * ================================
 * GPIO有效电平定义
 * ================================
 *
 * 根据实际测试确定:
 *
 *
 * 气泵 MOS 模块:
 *
 *      LOW
 *        ↓
 *      气泵开启
 *
 *      HIGH
 *        ↓
 *      气泵关闭
 *
 *
 *
 * 泄气阀 MOS 模块:
 *
 *      HIGH
 *        ↓
 *      泄气阀打开
 *
 *      LOW
 *        ↓
 *      泄气阀关闭
 *
 */


#define PUMP_ON_LEVEL          0
#define PUMP_OFF_LEVEL         1


#define VALVE_OPEN_LEVEL       1
#define VALVE_CLOSE_LEVEL      0




/**
 * 初始化气泵/泄气阀控制 GPIO
 *
 *
 * 默认状态:
 *
 *   - 左右气泵关闭
 *
 *   - 左右泄气阀关闭
 *
 *
 *
 * 安全约束:
 *
 *   1.
 *   同侧气泵 + 泄气阀禁止同时开启
 *
 *      左气泵 + 左阀
 *
 *      右气泵 + 右阀
 *
 *
 *   2.
 *
 *      左充气 + 右放气
 *
 *      右充气 + 左放气
 *
 *      允许
 *
 */
void pump_controller_init(void);





/**
 * 执行一条气囊控制指令
 *
 *
 * zone:
 *
 *   "left"
 *       左侧气泵 / 左侧泄气阀
 *
 *
 *   "right"
 *       右侧气泵 / 右侧泄气阀
 *
 *
 *   "both"
 *       左右同时操作
 *
 *
 *
 *
 * action:
 *
 *
 *   "inflate"
 *
 *       开启对应区域气泵
 *
 *       实际:
 *          GPIO LOW
 *
 *       持续 duration_sec 秒
 *
 *       自动关闭
 *
 *
 *
 *   "deflate"
 *
 *       开启对应区域泄气阀
 *
 *       实际:
 *          GPIO HIGH
 *
 *       持续 duration_sec 秒
 *
 *       自动关闭
 *
 *
 *
 *   "hold"
 *
 *       保持当前状态
 *
 *
 *
 *
 * 注意:
 *
 *   此函数内部使用 vTaskDelay()
 *
 *   会阻塞当前任务
 *
 *   建议:
 *
 *      在独立 pump_task 中调用
 *
 */
void pump_execute_command(
        const pump_command_t *cmd
);



#endif /* PUMP_CONTROLLER_H */