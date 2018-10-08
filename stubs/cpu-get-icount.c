#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/timer.h"
#include "sysemu/cpus.h"
#include "qemu/main-loop.h"

int use_icount;

int64_t cpu_get_icount(void)
{
    abort();
}

int64_t cpu_get_icount_raw(void)
{
    abort();
}

void qemu_timer_notify_cb(void *opaque, QEMUClockType type)
{
    qemu_notify_event();
}
