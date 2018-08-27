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
#include "qemu/osdep.h"
#include "chardev/char-io.h"

typedef struct IOWatchPoll {
    GSource parent;

    QIOChannel *ioc;
    GSource *src;

    IOCanReadHandler *fd_can_read;
    GSourceFunc fd_read;
    void *opaque;
} IOWatchPoll;

static IOWatchPoll *io_watch_poll_from_source(GSource *source)
{
    return container_of(source, IOWatchPoll, parent);
}

static gboolean io_watch_poll_prepare(GSource *source,
                                      gint *timeout)
{
    IOWatchPoll *iwp = io_watch_poll_from_source(source);
    bool now_active = iwp->fd_can_read(iwp->opaque) > 0;
    bool was_active = iwp->src != NULL;
    if (was_active == now_active) {
        return FALSE;
    }

    if (now_active) {
        iwp->src = qio_channel_create_watch(
            iwp->ioc, G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL);
        g_source_set_callback(iwp->src, iwp->fd_read, iwp->opaque, NULL);
        g_source_add_child_source(source, iwp->src);
        g_source_unref(iwp->src);
    } else {
        g_source_remove_child_source(source, iwp->src);
        iwp->src = NULL;
    }
    return FALSE;
}

static gboolean io_watch_poll_dispatch(GSource *source, GSourceFunc callback,
                                       gpointer user_data)
{
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs io_watch_poll_funcs = {
    .prepare = io_watch_poll_prepare,
    .dispatch = io_watch_poll_dispatch,
};

GSource *io_add_watch_poll(Chardev *chr,
                        QIOChannel *ioc,
                        IOCanReadHandler *fd_can_read,
                        QIOChannelFunc fd_read,
                        gpointer user_data,
                        GMainContext *context)
{
    IOWatchPoll *iwp;
    char *name;

    iwp = (IOWatchPoll *) g_source_new(&io_watch_poll_funcs,
                                       sizeof(IOWatchPoll));
    iwp->fd_can_read = fd_can_read;
    iwp->opaque = user_data;
    iwp->ioc = ioc;
    iwp->fd_read = (GSourceFunc) fd_read;
    iwp->src = NULL;

    name = g_strdup_printf("chardev-iowatch-%s", chr->label);
    g_source_set_name((GSource *)iwp, name);
    g_free(name);

    g_source_attach(&iwp->parent, context);
    g_source_unref(&iwp->parent);
    return (GSource *)iwp;
}

void remove_fd_in_watch(Chardev *chr)
{
    if (chr->gsource) {
        g_source_destroy(chr->gsource);
        chr->gsource = NULL;
    }
}

int io_channel_send_full(QIOChannel *ioc,
                         const void *buf, size_t len,
                         int *fds, size_t nfds)
{
    size_t offset = 0;

    while (offset < len) {
        ssize_t ret = 0;
        struct iovec iov = { .iov_base = (char *)buf + offset,
                             .iov_len = len - offset };

        ret = qio_channel_writev_full(
            ioc, &iov, 1,
            fds, nfds, NULL);
        if (ret == QIO_CHANNEL_ERR_BLOCK) {
            if (offset) {
                return offset;
            }

            errno = EAGAIN;
            return -1;
        } else if (ret < 0) {
            errno = EINVAL;
            return -1;
        }

        offset += ret;
    }

    return offset;
}

int io_channel_send(QIOChannel *ioc, const void *buf, size_t len)
{
    return io_channel_send_full(ioc, buf, len, NULL, 0);
}
