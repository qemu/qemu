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
 */

#include "qemu-common.h"
#include "monitor.h"
#include "qemu-timer.h"
#include "qemu-log.h"
#include "qemu-error.h"

#include <sys/time.h>

QEMUClock *rt_clock;

FILE *logfile;

struct QEMUBH
{
    QEMUBHFunc *cb;
    void *opaque;
};

void qemu_service_io(void)
{
}

void monitor_printf(Monitor *mon, const char *fmt, ...)
{
}

void monitor_print_filename(Monitor *mon, const char *filename)
{
}

void async_context_push(void)
{
}

void async_context_pop(void)
{
}

int get_async_context_id(void)
{
    return 0;
}

void monitor_protocol_event(MonitorEvent event, QObject *data)
{
}

QEMUBH *qemu_bh_new(QEMUBHFunc *cb, void *opaque)
{
    QEMUBH *bh;

    bh = qemu_malloc(sizeof(*bh));
    bh->cb = cb;
    bh->opaque = opaque;

    return bh;
}

int qemu_bh_poll(void)
{
    return 0;
}

void qemu_bh_schedule(QEMUBH *bh)
{
    bh->cb(bh->opaque);
}

void qemu_bh_cancel(QEMUBH *bh)
{
}

void qemu_bh_delete(QEMUBH *bh)
{
    qemu_free(bh);
}

int qemu_set_fd_handler2(int fd,
                         IOCanReadHandler *fd_read_poll,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque)
{
    return 0;
}

int64_t qemu_get_clock(QEMUClock *clock)
{
    qemu_timeval tv;
    qemu_gettimeofday(&tv);
    return (tv.tv_sec * 1000000000LL + (tv.tv_usec * 1000)) / 1000000;
}

Location *loc_push_restore(Location *loc)
{
    return loc;
}

Location *loc_push_none(Location *loc)
{
    return loc;
}

Location *loc_pop(Location *loc)
{
    return loc;
}

Location *loc_save(Location *loc)
{
    return loc;
}

void loc_restore(Location *loc)
{
}

void error_report(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
