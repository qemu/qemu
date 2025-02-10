#include "qemu/osdep.h"
#include "system/cpu-timers.h"
#include "qemu/main-loop.h"

int64_t cpu_get_clock(void)
{
    return get_clock_realtime();
}
