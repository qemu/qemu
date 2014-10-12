/*
 * Copyright (c) 2010-2011 IBM
 *
 * Authors:
 *         Chunqiang Tang <ctang@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

/*=============================================================================
 *  A short description: this module implements the qemu-tool functions that
 *  are related to time. In the simulation mode (see block/sim.c), these
 *  functions are implemented differently in qemu-test.c because they have to
 *  work with the simulation engine block/sim.c
 *============================================================================*/

#include "qemu/timer.h"
#include "sysemu.h"

struct QEMUBH {
    QEMUBHFunc *cb;
    void *opaque;
};

#if 1
int64_t qemu_get_clock (QEMUClock * clock)
{
    qemu_timeval tv;
    qemu_gettimeofday (&tv);
    return (tv.tv_sec * 1000000000LL + (tv.tv_usec * 1000)) / 1000000;
}
#endif

QEMUBH *qemu_bh_new (QEMUBHFunc * cb, void *opaque)
{
    QEMUBH *bh;

    bh = g_malloc(sizeof(*bh));
    bh->cb = cb;
    bh->opaque = opaque;

    return bh;
}

int qemu_bh_poll (void)
{
    return 0;
}

void qemu_bh_schedule (QEMUBH * bh)
{
    bh->cb (bh->opaque);
}

void qemu_bh_cancel (QEMUBH * bh)
{
}

void qemu_bh_delete (QEMUBH * bh)
{
    g_free(bh);
}

void timer_mod(QEMUTimer * ts, int64_t expire_time)
{
    fprintf (stderr, "timer_mod() should not be invoked in qemu-tool\n");
    exit (1);
}

QEMUTimer *qemu_new_timer (QEMUClock * clock, QEMUTimerCB * cb, void *opaque)
{
    fprintf (stderr, "qemu_new_timer() should not be invoked in qemu-tool\n");
    exit (1);
    return NULL;
}

void timer_free(QEMUTimer * ts)
{
    fprintf (stderr, "timer_free() should not be invoked in qemu-tool\n");
    exit (1);
}

void timer_del(QEMUTimer * ts)
{
    fprintf (stderr, "timer_del() should not be invoked in qemu-tool\n");
    exit (1);
}
