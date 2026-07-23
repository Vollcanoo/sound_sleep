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

#ifdef __cplusplus
}
#endif

#endif /* MONITOR_CONTROL_H */
