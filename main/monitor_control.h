#ifndef MONITOR_CONTROL_H
#define MONITOR_CONTROL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool monitor_control_is_enabled(void);
void monitor_control_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* MONITOR_CONTROL_H */
