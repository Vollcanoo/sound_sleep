/**
 * pump_controller.h — 双气囊 GPIO 控制
 *
 * 对齐 airbag-hardware 分支硬件设计:
 *   - 左/右两路独立气泵 + 两路独立放气电磁阀
 *   - GPIO7/8 = 左/右气泵 (MOS 驱动模块, 高电平触发)
 *   - GPIO9/10 = 左/右电磁阀 (AO3400A MOSFET, 高电平开阀放气)
 *   - 双电池分轨供电, 共地
 *
 * 注意: GPIO4/5/6 已被 Posture_Recognition 分支占用 (FSR 压力传感器 ADC)
 *       GPIO16/15/17 已被 Snore_Det 分支占用 (INMP441 I2S 麦克风)
 */
#ifndef PUMP_CONTROLLER_H
#define PUMP_CONTROLLER_H

#include "cloud_llm_client.h"   /* pump_command_t 定义在此 */

/*
 * GPIO 引脚分配 — 对齐 airbag-hardware 分支
 *
 * 硬件连接:
 *   GPIO7  → MOS驱动模块 → 左侧气泵 (5V DC)
 *   GPIO8  → MOS驱动模块 → 右侧气泵 (5V DC)
 *   GPIO9  → AO3400A+100Ω+10kΩ → 左侧电磁阀 (常闭, 5V)
 *   GPIO10 → AO3400A+100Ω+10kΩ → 右侧电磁阀 (常闭, 5V)
 */
#define GPIO_PUMP_LEFT       7    /* 左侧气泵 (HIGH=充气) */
#define GPIO_PUMP_RIGHT      8    /* 右侧气泵 (HIGH=充气) */
#define GPIO_VALVE_LEFT      9    /* 左侧放气阀 (HIGH=放气) */
#define GPIO_VALVE_RIGHT    10    /* 右侧放气阀 (HIGH=放气) */

/**
 * 初始化气泵控制 GPIO 为输出模式，默认全部关闭
 *
 * 安全约束 (来自 airbag-hardware 文档):
 *   - 同侧气泵 + 电磁阀 禁止同时开启 (无法建压, 过热风险)
 *   - 左充 + 右放 (或反之) 允许
 */
void pump_controller_init(void);

/**
 * 执行一条气泵控制指令
 *
 * zone 映射:
 *   "left"  → 左侧气泵/阀
 *   "right" → 右侧气泵/阀
 *   "both"  → 左右同时操作
 *
 * action 映射:
 *   "inflate"  → 开启对应区域气泵, 持续 duration_sec 秒后关闭
 *   "deflate"  → 开启对应区域电磁阀, 持续 duration_sec 秒后关闭
 *   "hold"     → 不做任何操作
 *
 * 注意: 此函数会阻塞当前任务 (vTaskDelay)，应在独立的 pump_task 中调用
 */
void pump_execute_command(const pump_command_t *cmd);

#endif /* PUMP_CONTROLLER_H */
