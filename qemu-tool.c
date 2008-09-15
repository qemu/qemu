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
#include "console.h"
#include "sysemu.h"
#include "qemu-timer.h"

#include <sys/time.h>

QEMUClock *rt_clock;

struct QEMUBH
{
    QEMUBHFunc *cb;
    void *opaque;
};

void term_printf(const char *fmt, ...)
{
}

void term_print_filename(const char *filename)
{
}

QEMUBH *qemu_bh_new(QEMUBHFunc *cb, void *opaque)
{
    QEMUBH *bh;

    bh = qemu_malloc(sizeof(*bh));
    if (bh) {
        bh->cb = cb;
        bh->opaque = opaque;
    }

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
                         IOCanRWHandler *fd_read_poll,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque)
{
    return 0;
}

int64_t qemu_get_clock(QEMUClock *clock)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000000000LL + (tv.tv_usec * 1000)) / 1000000;
}
