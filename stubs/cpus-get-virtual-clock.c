#include "qemu/osdep.h"
#include "sysemu/cpu-timers.h"
#include "qemu/main-loop.h"

int64_t cpus_get_virtual_clock(void)
{
    return cpu_get_clock();
}
