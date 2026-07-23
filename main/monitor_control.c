#include "monitor_control.h"

static volatile bool s_manual_enabled = false;
static volatile pump_mode_t s_pump_mode = PUMP_MODE_LLM;

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

pump_mode_t monitor_control_get_pump_mode(void)
{
    return s_pump_mode;
}

void monitor_control_set_pump_mode(pump_mode_t mode)
{
    s_pump_mode = mode;
}
