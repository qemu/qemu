#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/timer.h"
#include "sysemu/cpus.h"

int use_icount;

int64_t cpu_get_icount(void)
{
    abort();
}
