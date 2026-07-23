#ifndef MONITOR_CONTROL_H
#define MONITOR_CONTROL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 手动模式（BLE CSV 推送）：默认 false，手机 monitor_start/stop 控制 */
bool monitor_control_manual_enabled(void);
void monitor_control_set_manual(bool enabled);

/* 自动模式（cloud_task session）：默认 true，手动模式开启时自动关闭 */
bool monitor_control_auto_enabled(void);

/* 气泵控制模式 */
typedef enum {
    PUMP_MODE_LLM = 0,
    PUMP_MODE_LOCAL = 1,
} pump_mode_t;

pump_mode_t monitor_control_get_pump_mode(void);
void monitor_control_set_pump_mode(pump_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* MONITOR_CONTROL_H */
