#include "qemu/osdep.h"
#include "qemu-common.h"
#include "monitor/monitor.h"

int monitor_fdset_dup_fd_add(int64_t fdset_id, int dup_fd)
{
    return -1;
}

int monitor_fdset_dup_fd_find(int dup_fd)
{
    return -1;
}

int monitor_fdset_get_fd(int64_t fdset_id, int flags)
{
    return -1;
}

void monitor_fdset_dup_fd_remove(int dupfd)
{
}
