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
#define DPRINTF(fmt, ...) \
    do { printf("buffered-file: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

static void buffered_append(QEMUFileBuffered *s,
                            const uint8_t *buf, size_t size)
{
    if (size > (s->buffer_capacity - s->buffer_size)) {
        void *tmp;

        DPRINTF("increasing buffer capacity from %zu by %zu\n",
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
        DPRINTF("flush when error, bailing\n");
        return;
    }

    DPRINTF("flushing %zu byte(s) of data\n", s->buffer_size);

    while (offset < s->buffer_size) {
        ssize_t ret;

        ret = s->put_buffer(s->opaque, s->buffer + offset,
                            s->buffer_size - offset);
        if (ret == -EAGAIN) {
            DPRINTF("backend not ready, freezing\n");
            s->freeze_output = 1;
            break;
        }

        if (ret <= 0) {
            DPRINTF("error flushing data, %zd\n", ret);
            s->has_error = 1;
            break;
        } else {
            DPRINTF("flushed %zd byte(s)\n", ret);
            offset += ret;
        }
    }

    DPRINTF("flushed %zu of %zu byte(s)\n", offset, s->buffer_size);
    memmove(s->buffer, s->buffer + offset, s->buffer_size - offset);
    s->buffer_size -= offset;
}

static int buffered_put_buffer(void *opaque, const uint8_t *buf, int64_t pos, int size)
{
    QEMUFileBuffered *s = opaque;
    int offset = 0;
    ssize_t ret;

    DPRINTF("putting %d bytes at %" PRId64 "\n", size, pos);

    if (s->has_error) {
        DPRINTF("flush when error, bailing\n");
        return -EINVAL;
    }

    DPRINTF("unfreezing output\n");
    s->freeze_output = 0;

    buffered_flush(s);

    while (!s->freeze_output && offset < size) {
        if (s->bytes_xfer > s->xfer_limit) {
            DPRINTF("transfer limit exceeded when putting\n");
            break;
        }

        ret = s->put_buffer(s->opaque, buf + offset, size - offset);
        if (ret == -EAGAIN) {
            DPRINTF("backend not ready, freezing\n");
            s->freeze_output = 1;
            break;
        }

        if (ret <= 0) {
            DPRINTF("error putting\n");
            s->has_error = 1;
            offset = -EINVAL;
            break;
        }

        DPRINTF("put %zd byte(s)\n", ret);
        offset += ret;
        s->bytes_xfer += ret;
    }

    if (offset >= 0) {
        DPRINTF("buffering %d bytes\n", size - offset);
        buffered_append(s, buf + offset, size - offset);
        offset = size;
    }

    if (pos == 0 && size == 0) {
        DPRINTF("file is ready\n");
        if (s->bytes_xfer <= s->xfer_limit) {
            DPRINTF("notifying client\n");
            s->put_ready(s->opaque);
        }
    }

    return offset;
}

static int buffered_close(void *opaque)
{
    QEMUFileBuffered *s = opaque;
    int ret;

    DPRINTF("closing\n");

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

static int64_t buffered_set_rate_limit(void *opaque, int64_t new_rate)
{
    QEMUFileBuffered *s = opaque;
    if (s->has_error)
        goto out;

    if (new_rate > SIZE_MAX) {
        new_rate = SIZE_MAX;
    }

    s->xfer_limit = new_rate / 10;
    
out:
    return s->xfer_limit;
}

static int64_t buffered_get_rate_limit(void *opaque)
{
    QEMUFileBuffered *s = opaque;
  
    return s->xfer_limit;
}

static void buffered_rate_tick(void *opaque)
{
    QEMUFileBuffered *s = opaque;

    if (s->has_error) {
        buffered_close(s);
        return;
    }

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

    s->opaque = opaque;
    s->xfer_limit = bytes_per_sec / 10;
    s->put_buffer = put_buffer;
    s->put_ready = put_ready;
    s->wait_for_unfreeze = wait_for_unfreeze;
    s->close = close;

    s->file = qemu_fopen_ops(s, buffered_put_buffer, NULL,
                             buffered_close, buffered_rate_limit,
                             buffered_set_rate_limit,
			     buffered_get_rate_limit);

    s->timer = qemu_new_timer(rt_clock, buffered_rate_tick, s);

    qemu_mod_timer(s->timer, qemu_get_clock(rt_clock) + 100);

    return s->file;
}
