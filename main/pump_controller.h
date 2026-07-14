/**
 * pump_controller.h — 气泵 GPIO 继电器控制
 */
#ifndef PUMP_CONTROLLER_H
#define PUMP_CONTROLLER_H

#include "cloud_llm_client.h"   /* pump_command_t 定义在此 */

/*
 * GPIO 引脚分配 — 根据你的实际接线修改
 * 每个 GPIO 连接一个继电器模块，继电器控制对应区域的气泵
 */
#define GPIO_PUMP_HEAD      4    /* 头部区域气泵继电器 */
#define GPIO_PUMP_SHOULDER  5    /* 肩部区域气泵继电器 */
#define GPIO_PUMP_WAIST     6    /* 腰部区域气泵继电器 */
#define GPIO_VALVE_DEFLATE  7    /* 放气电磁阀 */

/**
 * 初始化气泵控制 GPIO 为输出模式，默认全部关闭
 */
void pump_controller_init(void);

/**
 * 执行一条气泵控制指令
 *
 * - inflate: 开启对应区域气泵，持续 duration_sec 秒后关闭
 * - deflate: 开启放气电磁阀，持续 duration_sec 秒后关闭
 * - hold:    不做任何操作
 *
 * 注意: 此函数会阻塞当前任务（vTaskDelay），应在独立的 pump_task 中调用
 *
 * @param cmd 气泵控制指令（从云端 LLM 解析得到）
 */
void pump_execute_command(const pump_command_t *cmd);

#endif /* PUMP_CONTROLLER_H */
