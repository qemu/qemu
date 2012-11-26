#include "qemu-common.h"
#include "qemu/timer.h"

int64_t cpu_get_clock(void)
{
    return get_clock_realtime();
}
