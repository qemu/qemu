#include "qemu/osdep.h"
#include "qemu-common.h"
#include "monitor/monitor.h"

Monitor *cur_mon;

bool monitor_cur_is_qmp(void)
{
    return false;
}
