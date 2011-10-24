/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu-common.h"
#include "qemu-aio.h"
#include "main-loop.h"

/* Anchor of the list of Bottom Halves belonging to the context */
static struct QEMUBH *first_bh;

/***********************************************************/
/* bottom halves (can be seen as timers which expire ASAP) */

struct QEMUBH {
    QEMUBHFunc *cb;
    void *opaque;
    int scheduled;
    int idle;
    int deleted;
    QEMUBH *next;
};

QEMUBH *qemu_bh_new(QEMUBHFunc *cb, void *opaque)
{
    QEMUBH *bh;
    bh = g_malloc0(sizeof(QEMUBH));
    bh->cb = cb;
    bh->opaque = opaque;
    bh->next = first_bh;
    first_bh = bh;
    return bh;
}

int qemu_bh_poll(void)
{
    QEMUBH *bh, **bhp, *next;
    int ret;
    static int nesting = 0;

    nesting++;

    ret = 0;
    for (bh = first_bh; bh; bh = next) {
        next = bh->next;
        if (!bh->deleted && bh->scheduled) {
            bh->scheduled = 0;
            if (!bh->idle)
                ret = 1;
            bh->idle = 0;
            bh->cb(bh->opaque);
        }
    }

    nesting--;

    /* remove deleted bhs */
    if (!nesting) {
        bhp = &first_bh;
        while (*bhp) {
            bh = *bhp;
            if (bh->deleted) {
                *bhp = bh->next;
                g_free(bh);
            } else {
                bhp = &bh->next;
            }
        }
    }

    return ret;
}

void qemu_bh_schedule_idle(QEMUBH *bh)
{
    if (bh->scheduled)
        return;
    bh->scheduled = 1;
    bh->idle = 1;
}

void qemu_bh_schedule(QEMUBH *bh)
{
    if (bh->scheduled)
        return;
    bh->scheduled = 1;
    bh->idle = 0;
    /* stop the currently executing CPU to execute the BH ASAP */
    qemu_notify_event();
}

void qemu_bh_cancel(QEMUBH *bh)
{
    bh->scheduled = 0;
}

void qemu_bh_delete(QEMUBH *bh)
{
    bh->scheduled = 0;
    bh->deleted = 1;
}

void qemu_bh_update_timeout(int *timeout)
{
    QEMUBH *bh;

    for (bh = first_bh; bh; bh = bh->next) {
        if (!bh->deleted && bh->scheduled) {
            if (bh->idle) {
                /* idle bottom halves will be polled at least
                 * every 10ms */
                *timeout = MIN(10, *timeout);
            } else {
                /* non-idle bottom halves will be executed
                 * immediately */
                *timeout = 0;
                break;
            }
        }
    }
}

