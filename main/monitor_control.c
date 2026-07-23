#include "monitor_control.h"

static volatile bool s_manual_enabled = false;

bool monitor_control_manual_enabled(void)
{
    return s_manual_enabled;
}

void monitor_control_set_manual(bool enabled)
{
    s_manual_enabled = enabled;
}

bool monitor_control_auto_enabled(void)
{
    return !s_manual_enabled;
}
