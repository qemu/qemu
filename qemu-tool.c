/*
 * Compatibility for qemu-img/qemu-nbd
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu-common.h"
#include "monitor.h"
#include "qemu-timer.h"
#include "qemu-log.h"
#include "migration.h"
#include "main-loop.h"
#include "sysemu.h"
#include "qemu_socket.h"
#include "slirp/libslirp.h"

#include <sys/time.h>

struct QEMUBH
{
    QEMUBHFunc *cb;
    void *opaque;
};

const char *qemu_get_vm_name(void)
{
    return NULL;
}

Monitor *cur_mon;

void vm_stop(RunState state)
{
    abort();
}

int monitor_cur_is_qmp(void)
{
    return 0;
}

void monitor_set_error(Monitor *mon, QError *qerror)
{
}

void monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
}

void monitor_printf(Monitor *mon, const char *fmt, ...)
{
}

void monitor_print_filename(Monitor *mon, const char *filename)
{
}

void monitor_protocol_event(MonitorEvent event, QObject *data)
{
}

int64_t cpu_get_clock(void)
{
    return get_clock_realtime();
}

int64_t cpu_get_icount(void)
{
    abort();
}

void qemu_mutex_lock_iothread(void)
{
}

void qemu_mutex_unlock_iothread(void)
{
}

int use_icount;

void qemu_clock_warp(QEMUClock *clock)
{
}

void slirp_update_timeout(uint32_t *timeout)
{
}

void slirp_select_fill(int *pnfds, fd_set *readfds,
                       fd_set *writefds, fd_set *xfds)
{
}

void slirp_select_poll(fd_set *readfds, fd_set *writefds,
                       fd_set *xfds, int select_error)
{
}

void migrate_add_blocker(Error *reason)
{
}

void migrate_del_blocker(Error *reason)
{
}
