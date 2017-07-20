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
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "chardev/char.h"
#include "io/channel-file.h"

#include "chardev/char-fd.h"
#include "chardev/char-io.h"

/* Called with chr_write_lock held.  */
static int fd_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    FDChardev *s = FD_CHARDEV(chr);

    return io_channel_send(s->ioc_out, buf, len);
}

static gboolean fd_chr_read(QIOChannel *chan, GIOCondition cond, void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    FDChardev *s = FD_CHARDEV(opaque);
    int len;
    uint8_t buf[CHR_READ_BUF_LEN];
    ssize_t ret;

    len = sizeof(buf);
    if (len > s->max_size) {
        len = s->max_size;
    }
    if (len == 0) {
        return TRUE;
    }

    ret = qio_channel_read(
        chan, (gchar *)buf, len, NULL);
    if (ret == 0) {
        remove_fd_in_watch(chr);
        qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
        return FALSE;
    }
    if (ret > 0) {
        qemu_chr_be_write(chr, buf, ret);
    }

    return TRUE;
}

static int fd_chr_read_poll(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    FDChardev *s = FD_CHARDEV(opaque);

    s->max_size = qemu_chr_be_can_write(chr);
    return s->max_size;
}

static GSource *fd_chr_add_watch(Chardev *chr, GIOCondition cond)
{
    FDChardev *s = FD_CHARDEV(chr);
    return qio_channel_create_watch(s->ioc_out, cond);
}

static void fd_chr_update_read_handler(Chardev *chr,
                                       GMainContext *context)
{
    FDChardev *s = FD_CHARDEV(chr);

    remove_fd_in_watch(chr);
    if (s->ioc_in) {
        chr->gsource = io_add_watch_poll(chr, s->ioc_in,
                                           fd_chr_read_poll,
                                           fd_chr_read, chr,
                                           context);
    }
}

static void char_fd_finalize(Object *obj)
{
    Chardev *chr = CHARDEV(obj);
    FDChardev *s = FD_CHARDEV(obj);

    remove_fd_in_watch(chr);
    if (s->ioc_in) {
        object_unref(OBJECT(s->ioc_in));
    }
    if (s->ioc_out) {
        object_unref(OBJECT(s->ioc_out));
    }

    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

int qmp_chardev_open_file_source(char *src, int flags, Error **errp)
{
    int fd = -1;

    TFR(fd = qemu_open(src, flags, 0666));
    if (fd == -1) {
        error_setg_file_open(errp, errno, src);
    }
    return fd;
}

/* open a character device to a unix fd */
void qemu_chr_open_fd(Chardev *chr,
                      int fd_in, int fd_out)
{
    FDChardev *s = FD_CHARDEV(chr);
    char *name;

    s->ioc_in = QIO_CHANNEL(qio_channel_file_new_fd(fd_in));
    name = g_strdup_printf("chardev-file-in-%s", chr->label);
    qio_channel_set_name(QIO_CHANNEL(s->ioc_in), name);
    g_free(name);
    s->ioc_out = QIO_CHANNEL(qio_channel_file_new_fd(fd_out));
    name = g_strdup_printf("chardev-file-out-%s", chr->label);
    qio_channel_set_name(QIO_CHANNEL(s->ioc_out), name);
    g_free(name);
    qemu_set_nonblock(fd_out);
}

static void char_fd_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->chr_add_watch = fd_chr_add_watch;
    cc->chr_write = fd_chr_write;
    cc->chr_update_read_handler = fd_chr_update_read_handler;
}

static const TypeInfo char_fd_type_info = {
    .name = TYPE_CHARDEV_FD,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(FDChardev),
    .instance_finalize = char_fd_finalize,
    .class_init = char_fd_class_init,
    .abstract = true,
};

static void register_types(void)
{
    type_register_static(&char_fd_type_info);
}

type_init(register_types);
