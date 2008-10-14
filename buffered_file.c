/*
 * QEMU buffered QEMUFile
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
#include "hw/hw.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "qemu-char.h"
#include "buffered_file.h"

//#define DEBUG_BUFFERED_FILE

typedef struct QEMUFileBuffered
{
    BufferedPutFunc *put_buffer;
    BufferedPutReadyFunc *put_ready;
    BufferedWaitForUnfreezeFunc *wait_for_unfreeze;
    BufferedCloseFunc *close;
    void *opaque;
    QEMUFile *file;
    int has_error;
    int freeze_output;
    size_t bytes_xfer;
    size_t xfer_limit;
    uint8_t *buffer;
    size_t buffer_size;
    size_t buffer_capacity;
    QEMUTimer *timer;
} QEMUFileBuffered;

#ifdef DEBUG_BUFFERED_FILE
#define dprintf(fmt, ...) \
    do { printf("buffered-file: " fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

static void buffered_append(QEMUFileBuffered *s,
                            const uint8_t *buf, size_t size)
{
    if (size > (s->buffer_capacity - s->buffer_size)) {
        void *tmp;

        dprintf("increasing buffer capacity from %ld by %ld\n",
                s->buffer_capacity, size + 1024);

        s->buffer_capacity += size + 1024;

        tmp = qemu_realloc(s->buffer, s->buffer_capacity);
        if (tmp == NULL) {
            fprintf(stderr, "qemu file buffer expansion failed\n");
            exit(1);
        }

        s->buffer = tmp;
    }

    memcpy(s->buffer + s->buffer_size, buf, size);
    s->buffer_size += size;
}

static void buffered_flush(QEMUFileBuffered *s)
{
    size_t offset = 0;

    if (s->has_error) {
        dprintf("flush when error, bailing\n");
        return;
    }

    dprintf("flushing %ld byte(s) of data\n", s->buffer_size);

    while (offset < s->buffer_size) {
        ssize_t ret;

        ret = s->put_buffer(s->opaque, s->buffer + offset,
                            s->buffer_size - offset);
        if (ret == -EAGAIN) {
            dprintf("backend not ready, freezing\n");
            s->freeze_output = 1;
            break;
        }

        if (ret <= 0) {
            dprintf("error flushing data, %ld\n", ret);
            s->has_error = 1;
            break;
        } else {
            dprintf("flushed %ld byte(s)\n", ret);
            offset += ret;
        }
    }

    dprintf("flushed %ld of %ld byte(s)\n", offset, s->buffer_size);
    memmove(s->buffer, s->buffer + offset, s->buffer_size - offset);
    s->buffer_size -= offset;
}

static int buffered_put_buffer(void *opaque, const uint8_t *buf, int64_t pos, int size)
{
    QEMUFileBuffered *s = opaque;
    int offset = 0;
    ssize_t ret;

    dprintf("putting %ld bytes at %Ld\n", size, pos);

    if (s->has_error) {
        dprintf("flush when error, bailing\n");
        return -EINVAL;
    }

    dprintf("unfreezing output\n");
    s->freeze_output = 0;

    buffered_flush(s);

    while (!s->freeze_output && offset < size) {
        if (s->bytes_xfer > s->xfer_limit) {
            dprintf("transfer limit exceeded when putting\n");
            break;
        }

        ret = s->put_buffer(s->opaque, buf + offset, size - offset);
        if (ret == -EAGAIN) {
            dprintf("backend not ready, freezing\n");
            s->freeze_output = 1;
            break;
        }

        if (ret <= 0) {
            dprintf("error putting\n");
            s->has_error = 1;
            offset = -EINVAL;
            break;
        }

        dprintf("put %ld byte(s)\n", ret);
        offset += ret;
        s->bytes_xfer += ret;
    }

    if (offset >= 0) {
        dprintf("buffering %ld bytes\n", size - offset);
        buffered_append(s, buf + offset, size - offset);
        offset = size;
    }

    return offset;
}

static int buffered_close(void *opaque)
{
    QEMUFileBuffered *s = opaque;
    int ret;

    dprintf("closing\n");

    while (!s->has_error && s->buffer_size) {
        buffered_flush(s);
        if (s->freeze_output)
            s->wait_for_unfreeze(s);
    }

    ret = s->close(s->opaque);

    qemu_del_timer(s->timer);
    qemu_free_timer(s->timer);
    qemu_free(s->buffer);
    qemu_free(s);

    return ret;
}

static int buffered_rate_limit(void *opaque)
{
    QEMUFileBuffered *s = opaque;

    if (s->has_error)
        return 0;

    if (s->freeze_output)
        return 1;

    if (s->bytes_xfer > s->xfer_limit)
        return 1;

    return 0;
}

static void buffered_rate_tick(void *opaque)
{
    QEMUFileBuffered *s = opaque;

    if (s->has_error)
        return;

    qemu_mod_timer(s->timer, qemu_get_clock(rt_clock) + 100);

    if (s->freeze_output)
        return;

    s->bytes_xfer = 0;

    buffered_flush(s);

    /* Add some checks around this */
    s->put_ready(s->opaque);
}

QEMUFile *qemu_fopen_ops_buffered(void *opaque,
                                  size_t bytes_per_sec,
                                  BufferedPutFunc *put_buffer,
                                  BufferedPutReadyFunc *put_ready,
                                  BufferedWaitForUnfreezeFunc *wait_for_unfreeze,
                                  BufferedCloseFunc *close)
{
    QEMUFileBuffered *s;

    s = qemu_mallocz(sizeof(*s));
    if (s == NULL)
        return NULL;

    s->opaque = opaque;
    s->xfer_limit = bytes_per_sec / 10;
    s->put_buffer = put_buffer;
    s->put_ready = put_ready;
    s->wait_for_unfreeze = wait_for_unfreeze;
    s->close = close;

    s->file = qemu_fopen_ops(s, buffered_put_buffer, NULL,
                             buffered_close, buffered_rate_limit);

    s->timer = qemu_new_timer(rt_clock, buffered_rate_tick, s);

    qemu_mod_timer(s->timer, qemu_get_clock(rt_clock) + 100);

    return s->file;
}

