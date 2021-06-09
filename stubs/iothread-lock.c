#include "qemu/osdep.h"
#include "qemu/main-loop.h"

bool qemu_mutex_iothread_locked(void)
{
    return false;
}

void qemu_mutex_lock_iothread_impl(const char *file, int line)
{
}

void qemu_mutex_unlock_iothread(void)
{
}
