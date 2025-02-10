#include "qemu/osdep.h"
#include "system/cpu-timers.h"
#include "qemu/main-loop.h"

int64_t cpus_get_virtual_clock(void)
{
    return cpu_get_clock();
}

void cpus_set_virtual_clock(int64_t new_time)
{
    /* do nothing */
}
