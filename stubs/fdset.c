#include "qemu/osdep.h"
#include "qapi/error.h"
#include "monitor/monitor.h"
#include "../monitor/monitor-internal.h"

int monitor_fdset_dup_fd_add(int64_t fdset_id, int flags)
{
    errno = ENOSYS;
    return -1;
}

int64_t monitor_fdset_dup_fd_find(int dup_fd)
{
    return -1;
}

void monitor_fdset_dup_fd_remove(int dupfd)
{
}

void monitor_fdsets_cleanup(void)
{
}
