/*
 * Block driver for RAW files (win32)
 *
 * Copyright (c) 2006 Fabrice Bellard
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
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/timer.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "block/aio.h"
#include "block/raw-aio.h"
#include "qemu/event_notifier.h"
#include "qemu/iov.h"
#include <windows.h>
#include <winioctl.h>

#define FTYPE_FILE 0
#define FTYPE_CD     1
#define FTYPE_HARDDISK 2

struct QEMUWin32AIOState {
    HANDLE hIOCP;
    EventNotifier e;
    int count;
    AioContext *aio_ctx;
};

typedef struct QEMUWin32AIOCB {
    BlockAIOCB common;
    struct QEMUWin32AIOState *ctx;
    int nbytes;
    OVERLAPPED ov;
    QEMUIOVector *qiov;
    void *buf;
    bool is_read;
    bool is_linear;
} QEMUWin32AIOCB;

/*
 * Completes an AIO request (calls the callback and frees the ACB).
 */
static void win32_aio_process_completion(QEMUWin32AIOState *s,
    QEMUWin32AIOCB *waiocb, DWORD count)
{
    int ret;
    s->count--;

    if (waiocb->ov.Internal != 0) {
        ret = -EIO;
    } else {
        ret = 0;
        if (count < waiocb->nbytes) {
            /* Short reads mean EOF, pad with zeros. */
            if (waiocb->is_read) {
                qemu_iovec_memset(waiocb->qiov, count, 0,
                    waiocb->qiov->size - count);
            } else {
                ret = -EINVAL;
            }
       }
    }

    if (!waiocb->is_linear) {
        if (ret == 0 && waiocb->is_read) {
            QEMUIOVector *qiov = waiocb->qiov;
            iov_from_buf(qiov->iov, qiov->niov, 0, waiocb->buf, qiov->size);
        }
        qemu_vfree(waiocb->buf);
    }

    waiocb->common.cb(waiocb->common.opaque, ret);
    qemu_aio_unref(waiocb);
}

static void win32_aio_completion_cb(EventNotifier *e)
{
    QEMUWin32AIOState *s = container_of(e, QEMUWin32AIOState, e);
    DWORD count;
    ULONG_PTR key;
    OVERLAPPED *ov;

    event_notifier_test_and_clear(&s->e);
    while (GetQueuedCompletionStatus(s->hIOCP, &count, &key, &ov, 0)) {
        QEMUWin32AIOCB *waiocb = container_of(ov, QEMUWin32AIOCB, ov);

        win32_aio_process_completion(s, waiocb, count);
    }
}

static const AIOCBInfo win32_aiocb_info = {
    .aiocb_size         = sizeof(QEMUWin32AIOCB),
};

BlockAIOCB *win32_aio_submit(BlockDriverState *bs,
        QEMUWin32AIOState *aio, HANDLE hfile,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque, int type)
{
    struct QEMUWin32AIOCB *waiocb;
    uint64_t offset = sector_num * 512;
    DWORD rc;

    waiocb = qemu_aio_get(&win32_aiocb_info, bs, cb, opaque);
    waiocb->nbytes = nb_sectors * 512;
    waiocb->qiov = qiov;
    waiocb->is_read = (type == QEMU_AIO_READ);

    if (qiov->niov > 1) {
        waiocb->buf = qemu_try_blockalign(bs, qiov->size);
        if (waiocb->buf == NULL) {
            goto out;
        }
        if (type & QEMU_AIO_WRITE) {
            iov_to_buf(qiov->iov, qiov->niov, 0, waiocb->buf, qiov->size);
        }
        waiocb->is_linear = false;
    } else {
        waiocb->buf = qiov->iov[0].iov_base;
        waiocb->is_linear = true;
    }

    memset(&waiocb->ov, 0, sizeof(waiocb->ov));
    waiocb->ov.Offset = (DWORD)offset;
    waiocb->ov.OffsetHigh = (DWORD)(offset >> 32);
    waiocb->ov.hEvent = event_notifier_get_handle(&aio->e);

    aio->count++;

    if (type & QEMU_AIO_READ) {
        rc = ReadFile(hfile, waiocb->buf, waiocb->nbytes, NULL, &waiocb->ov);
    } else {
        rc = WriteFile(hfile, waiocb->buf, waiocb->nbytes, NULL, &waiocb->ov);
    }
    if(rc == 0 && GetLastError() != ERROR_IO_PENDING) {
        goto out_dec_count;
    }
    return &waiocb->common;

out_dec_count:
    aio->count--;
out:
    qemu_aio_unref(waiocb);
    return NULL;
}

int win32_aio_attach(QEMUWin32AIOState *aio, HANDLE hfile)
{
    if (CreateIoCompletionPort(hfile, aio->hIOCP, (ULONG_PTR) 0, 0) == NULL) {
        return -EINVAL;
    } else {
        return 0;
    }
}

void win32_aio_detach_aio_context(QEMUWin32AIOState *aio,
                                  AioContext *old_context)
{
    aio_set_event_notifier(old_context, &aio->e, false, NULL, NULL);
    aio->aio_ctx = NULL;
}

void win32_aio_attach_aio_context(QEMUWin32AIOState *aio,
                                  AioContext *new_context)
{
    aio->aio_ctx = new_context;
    aio_set_event_notifier(new_context, &aio->e, false,
                           win32_aio_completion_cb, NULL);
}

QEMUWin32AIOState *win32_aio_init(void)
{
    QEMUWin32AIOState *s;

    s = g_malloc0(sizeof(*s));
    if (event_notifier_init(&s->e, false) < 0) {
        goto out_free_state;
    }

    s->hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (s->hIOCP == NULL) {
        goto out_close_efd;
    }

    return s;

out_close_efd:
    event_notifier_cleanup(&s->e);
out_free_state:
    g_free(s);
    return NULL;
}

void win32_aio_cleanup(QEMUWin32AIOState *aio)
{
    assert(!aio->aio_ctx);
    CloseHandle(aio->hIOCP);
    event_notifier_cleanup(&aio->e);
    g_free(aio);
}
