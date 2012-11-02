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
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu-common.h"
#include "hw/hw.h"
#include "qemu-timer.h"
#include "qemu-char.h"
#include "buffered_file.h"

//#define DEBUG_BUFFERED_FILE

typedef struct QEMUFileBuffered
{
    MigrationState *migration_state;
    QEMUFile *file;
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
        DPRINTF("increasing buffer capacity from %zu by %zu\n",
                s->buffer_capacity, size + 1024);

        s->buffer_capacity += size + 1024;

        s->buffer = g_realloc(s->buffer, s->buffer_capacity);
    }

    memcpy(s->buffer + s->buffer_size, buf, size);
    s->buffer_size += size;
}

static ssize_t buffered_flush(QEMUFileBuffered *s)
{
    size_t offset = 0;
    ssize_t ret = 0;

    DPRINTF("flushing %zu byte(s) of data\n", s->buffer_size);

    while (s->bytes_xfer < s->xfer_limit && offset < s->buffer_size) {

        ret = migrate_fd_put_buffer(s->migration_state, s->buffer + offset,
                                    s->buffer_size - offset);
        if (ret == -EAGAIN) {
            DPRINTF("backend not ready, freezing\n");
            ret = 0;
            s->freeze_output = 1;
            break;
        }

        if (ret <= 0) {
            DPRINTF("error flushing data, %zd\n", ret);
            break;
        } else {
            DPRINTF("flushed %zd byte(s)\n", ret);
            offset += ret;
            s->bytes_xfer += ret;
        }
    }

    DPRINTF("flushed %zu of %zu byte(s)\n", offset, s->buffer_size);
    memmove(s->buffer, s->buffer + offset, s->buffer_size - offset);
    s->buffer_size -= offset;

    if (ret < 0) {
        return ret;
    }
    return offset;
}

static int buffered_put_buffer(void *opaque, const uint8_t *buf, int64_t pos, int size)
{
    QEMUFileBuffered *s = opaque;
    ssize_t error;

    DPRINTF("putting %d bytes at %" PRId64 "\n", size, pos);

    error = qemu_file_get_error(s->file);
    if (error) {
        DPRINTF("flush when error, bailing: %s\n", strerror(-error));
        return error;
    }

    DPRINTF("unfreezing output\n");
    s->freeze_output = 0;

    if (size > 0) {
        DPRINTF("buffering %d bytes\n", size - offset);
        buffered_append(s, buf, size);
    }

    error = buffered_flush(s);
    if (error < 0) {
        DPRINTF("buffered flush error. bailing: %s\n", strerror(-error));
        return error;
    }

    if (pos == 0 && size == 0) {
        DPRINTF("file is ready\n");
        if (!s->freeze_output && s->bytes_xfer < s->xfer_limit) {
            DPRINTF("notifying client\n");
            migrate_fd_put_ready(s->migration_state);
        }
    }

    return size;
}

static int buffered_close(void *opaque)
{
    QEMUFileBuffered *s = opaque;
    ssize_t ret = 0;
    int ret2;

    DPRINTF("closing\n");

    s->xfer_limit = INT_MAX;
    while (!qemu_file_get_error(s->file) && s->buffer_size) {
        ret = buffered_flush(s);
        if (ret < 0) {
            break;
        }
        if (s->freeze_output) {
            ret = migrate_fd_wait_for_unfreeze(s->migration_state);
            if (ret < 0) {
                break;
            }
        }
    }

    ret2 = migrate_fd_close(s->migration_state);
    if (ret >= 0) {
        ret = ret2;
    }
    qemu_del_timer(s->timer);
    qemu_free_timer(s->timer);
    g_free(s->buffer);
    g_free(s);

    return ret;
}

/*
 * The meaning of the return values is:
 *   0: We can continue sending
 *   1: Time to stop
 *   negative: There has been an error
 */
static int buffered_get_fd(void *opaque)
{
    QEMUFileBuffered *s = opaque;

    return qemu_get_fd(s->file);
}

static int buffered_rate_limit(void *opaque)
{
    QEMUFileBuffered *s = opaque;
    int ret;

    ret = qemu_file_get_error(s->file);
    if (ret) {
        return ret;
    }
    if (s->freeze_output)
        return 1;

    if (s->bytes_xfer > s->xfer_limit)
        return 1;

    return 0;
}

static int64_t buffered_set_rate_limit(void *opaque, int64_t new_rate)
{
    QEMUFileBuffered *s = opaque;
    if (qemu_file_get_error(s->file)) {
        goto out;
    }
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

    if (qemu_file_get_error(s->file)) {
        buffered_close(s);
        return;
    }

    qemu_mod_timer(s->timer, qemu_get_clock_ms(rt_clock) + 100);

    if (s->freeze_output)
        return;

    s->bytes_xfer = 0;

    buffered_put_buffer(s, NULL, 0, 0);
}

static const QEMUFileOps buffered_file_ops = {
    .get_fd =         buffered_get_fd,
    .put_buffer =     buffered_put_buffer,
    .close =          buffered_close,
    .rate_limit =     buffered_rate_limit,
    .get_rate_limit = buffered_get_rate_limit,
    .set_rate_limit = buffered_set_rate_limit,
};

QEMUFile *qemu_fopen_ops_buffered(MigrationState *migration_state)
{
    QEMUFileBuffered *s;

    s = g_malloc0(sizeof(*s));

    s->migration_state = migration_state;
    s->xfer_limit = migration_state->bandwidth_limit / 10;

    s->file = qemu_fopen_ops(s, &buffered_file_ops);

    s->timer = qemu_new_timer_ms(rt_clock, buffered_rate_tick, s);

    qemu_mod_timer(s->timer, qemu_get_clock_ms(rt_clock) + 100);

    return s->file;
}
