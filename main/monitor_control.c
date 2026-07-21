#include "monitor_control.h"

static volatile bool s_monitoring_enabled;

bool monitor_control_is_enabled(void)
{
    return s_monitoring_enabled;
}

void monitor_control_set_enabled(bool enabled)
{
    s_monitoring_enabled = enabled;
}
