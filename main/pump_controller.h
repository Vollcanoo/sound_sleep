/**
 * pump_controller.h — 双气囊 GPIO 控制
 *
 * 对齐 airbag-hardware 分支硬件设计:
 *
 *   - 左/右两路独立气泵 + 两路独立放气电磁阀
 *
 *   - GPIO7/8  = 左/右气泵
 *                (MOS驱动模块, 高电平触发)
 *
 *   - GPIO9/10 = 左/右电磁阀
 *                (MOS驱动模块, 高电平触发)
 *
 *   - 双电池分轨供电, 共地
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


#include "cloud_llm_client.h"   /* pump_command_t 定义在此 */


/*
 * GPIO 引脚分配 — 对齐 airbag-hardware 分支
 *
 * 硬件连接:
 *
 *   GPIO7
 *       ↓
 *   左气泵 MOS驱动模块 High Trigger+
 *       ↓
 *   左侧气泵 (5V DC)
 *
 *
 *   GPIO8
 *       ↓
 *   右气泵 MOS驱动模块 High Trigger+
 *       ↓
 *   右侧气泵 (5V DC)
 *
 *
 *   GPIO9
 *       ↓
 *   左泄气阀 MOS驱动模块 High Trigger+
 *       ↓
 *   左侧电磁阀 (常闭, 5V)
 *
 *
 *   GPIO10
 *       ↓
 *   右泄气阀 MOS驱动模块 High Trigger+
 *       ↓
 *   右侧电磁阀 (常闭, 5V)
 */


#define GPIO_PUMP_LEFT       7
#define GPIO_PUMP_RIGHT      8

#define GPIO_VALVE_LEFT      9
#define GPIO_VALVE_RIGHT    10


/*
 * GPIO状态定义:
 *
 * 气泵:
 *   HIGH = 启动充气
 *   LOW  = 停止
 *
 * 泄气阀:
 *   HIGH = 打开放气
 *   LOW  = 关闭
 */


/**
 * 初始化气泵/泄气阀控制 GPIO
 *
 * 默认状态:
 *   - 气泵关闭
 *   - 泄气阀关闭
 *
 * 安全约束:
 *   - 同侧气泵 + 泄气阀禁止同时开启
 *     (无法建立压力, 增加功耗)
 *
 *   - 左充气 + 右放气允许
 *   - 右充气 + 左放气允许
 */
void pump_controller_init();


/**
 * 执行一条气泵控制指令
 *
 * zone 映射:
 *
 *   "left"
 *       → 左侧气泵 / 左侧泄气阀
 *
 *   "right"
 *       → 右侧气泵 / 右侧泄气阀
 *
 *   "both"
 *       → 左右同时操作
 *
 *
 * action 映射:
 *
 *   "inflate"
 *       → 开启对应区域气泵
 *          持续 duration_sec 秒后关闭
 *
 *   "deflate"
 *       → 开启对应区域泄气阀
 *          持续 duration_sec 秒后关闭
 *
 *   "hold"
 *       → 保持当前状态
 *
 *
 * 注意:
 *   此函数会阻塞当前任务
 *   (vTaskDelay)
 *
 *   应在独立 pump_task 中调用
 */
void pump_execute_command(const pump_command_t *cmd);


#endif /* PUMP_CONTROLLER_H */