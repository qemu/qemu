#include "qemu/osdep.h"
#include "sysemu/cpus.h"
#include "qemu/main-loop.h"

void qemu_timer_notify_cb(void *opaque, QEMUClockType type)
{
    qemu_notify_event();
}
