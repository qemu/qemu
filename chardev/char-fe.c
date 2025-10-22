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
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "system/replay.h"

#include "chardev/char-fe.h"
#include "chardev/char-io.h"
#include "chardev-internal.h"

int qemu_chr_fe_write(CharFrontend *c, const uint8_t *buf, int len)
{
    Chardev *s = c->chr;

    if (!s) {
        return 0;
    }

    return qemu_chr_write(s, buf, len, false);
}

int qemu_chr_fe_write_all(CharFrontend *c, const uint8_t *buf, int len)
{
    Chardev *s = c->chr;

    if (!s) {
        return 0;
    }

    return qemu_chr_write(s, buf, len, true);
}

int qemu_chr_fe_read_all(CharFrontend *c, uint8_t *buf, int len)
{
    Chardev *s = c->chr;
    int offset = 0;
    int res;

    if (!s || !CHARDEV_GET_CLASS(s)->chr_sync_read) {
        return 0;
    }

    if (qemu_chr_replay(s) && replay_mode == REPLAY_MODE_PLAY) {
        return replay_char_read_all_load(buf);
    }

    while (offset < len) {
    retry:
        res = CHARDEV_GET_CLASS(s)->chr_sync_read(s, buf + offset,
                                                  len - offset);
        if (res == -1 && errno == EAGAIN) {
            g_usleep(100);
            goto retry;
        }

        if (res == 0) {
            break;
        }

        if (res < 0) {
            if (qemu_chr_replay(s) && replay_mode == REPLAY_MODE_RECORD) {
                replay_char_read_all_save_error(res);
            }
            return res;
        }

        offset += res;
    }

    if (qemu_chr_replay(s) && replay_mode == REPLAY_MODE_RECORD) {
        replay_char_read_all_save_buf(buf, offset);
    }
    return offset;
}

int qemu_chr_fe_ioctl(CharFrontend *c, int cmd, void *arg)
{
    Chardev *s = c->chr;
    int res;

    if (!s || !CHARDEV_GET_CLASS(s)->chr_ioctl || qemu_chr_replay(s)) {
        res = -ENOTSUP;
    } else {
        res = CHARDEV_GET_CLASS(s)->chr_ioctl(s, cmd, arg);
    }

    return res;
}

int qemu_chr_fe_get_msgfd(CharFrontend *c)
{
    Chardev *s = c->chr;
    int fd;
    int res = (qemu_chr_fe_get_msgfds(c, &fd, 1) == 1) ? fd : -1;
    if (s && qemu_chr_replay(s)) {
        error_report("Replay: get msgfd is not supported "
                     "for serial devices yet");
        exit(1);
    }
    return res;
}

int qemu_chr_fe_get_msgfds(CharFrontend *c, int *fds, int len)
{
    Chardev *s = c->chr;

    if (!s) {
        return -1;
    }

    return CHARDEV_GET_CLASS(s)->get_msgfds ?
        CHARDEV_GET_CLASS(s)->get_msgfds(s, fds, len) : -1;
}

int qemu_chr_fe_set_msgfds(CharFrontend *c, int *fds, int num)
{
    Chardev *s = c->chr;

    if (!s) {
        return -1;
    }

    return CHARDEV_GET_CLASS(s)->set_msgfds ?
        CHARDEV_GET_CLASS(s)->set_msgfds(s, fds, num) : -1;
}

void qemu_chr_fe_accept_input(CharFrontend *c)
{
    Chardev *s = c->chr;

    if (!s) {
        return;
    }

    if (CHARDEV_GET_CLASS(s)->chr_accept_input) {
        CHARDEV_GET_CLASS(s)->chr_accept_input(s);
    }
    qemu_notify_event();
}

void qemu_chr_fe_printf(CharFrontend *c, const char *fmt, ...)
{
    char buf[CHR_READ_BUF_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    /* XXX this blocks entire thread. Rewrite to use
     * qemu_chr_fe_write and background I/O callbacks */
    qemu_chr_fe_write_all(c, (uint8_t *)buf, strlen(buf));
    va_end(ap);
}

Chardev *qemu_chr_fe_get_driver(CharFrontend *c)
{
    /* this is unsafe for the users that support chardev hotswap */
    assert(c->chr_be_change == NULL);
    return c->chr;
}

bool qemu_chr_fe_backend_connected(CharFrontend *c)
{
    return !!c->chr;
}

bool qemu_chr_fe_backend_open(CharFrontend *c)
{
    return c->chr && c->chr->be_open;
}

bool qemu_chr_fe_init(CharFrontend *c, Chardev *s, Error **errp)
{
    unsigned int tag = 0;

    if (s) {
        if (CHARDEV_IS_MUX(s)) {
            MuxChardev *d = MUX_CHARDEV(s);

            if (!mux_chr_attach_frontend(d, c, &tag, errp)) {
                return false;
            }
        } else if (s->fe) {
            error_setg(errp, "chardev '%s' is already in use", s->label);
            return false;
        } else {
            s->fe = c;
        }
    }

    c->fe_is_open = false;
    c->tag = tag;
    c->chr = s;
    return true;
}

void qemu_chr_fe_deinit(CharFrontend *c, bool del)
{
    assert(c);

    if (c->chr) {
        qemu_chr_fe_set_handlers(c, NULL, NULL, NULL, NULL, NULL, NULL, true);
        if (c->chr->fe == c) {
            c->chr->fe = NULL;
        }
        if (CHARDEV_IS_MUX(c->chr)) {
            MuxChardev *d = MUX_CHARDEV(c->chr);
            mux_chr_detach_frontend(d, c->tag);
        }
        if (del) {
            Object *obj = OBJECT(c->chr);
            if (obj->parent) {
                object_unparent(obj);
            } else {
                object_unref(obj);
            }
        }
        c->chr = NULL;
    }
}

void qemu_chr_fe_set_handlers_full(CharFrontend *c,
                                   IOCanReadHandler *fd_can_read,
                                   IOReadHandler *fd_read,
                                   IOEventHandler *fd_event,
                                   BackendChangeHandler *be_change,
                                   void *opaque,
                                   GMainContext *context,
                                   bool set_open,
                                   bool sync_state)
{
    Chardev *s;
    bool fe_open;

    s = c->chr;
    if (!s) {
        return;
    }

    if (!opaque && !fd_can_read && !fd_read && !fd_event) {
        fe_open = false;
        remove_fd_in_watch(s);
    } else {
        fe_open = true;
    }
    c->chr_can_read = fd_can_read;
    c->chr_read = fd_read;
    c->chr_event = fd_event;
    c->chr_be_change = be_change;
    c->opaque = opaque;

    qemu_chr_be_update_read_handlers(s, context);

    if (set_open) {
        qemu_chr_fe_set_open(c, fe_open);
    }

    if (fe_open) {
        qemu_chr_fe_take_focus(c);
        /* We're connecting to an already opened device, so let's make sure we
           also get the open event */
        if (sync_state && s->be_open) {
            qemu_chr_be_event(s, CHR_EVENT_OPENED);
        }
    }
}

void qemu_chr_fe_set_handlers(CharFrontend *c,
                              IOCanReadHandler *fd_can_read,
                              IOReadHandler *fd_read,
                              IOEventHandler *fd_event,
                              BackendChangeHandler *be_change,
                              void *opaque,
                              GMainContext *context,
                              bool set_open)
{
    qemu_chr_fe_set_handlers_full(c, fd_can_read, fd_read, fd_event, be_change,
                                  opaque, context, set_open,
                                  true);
}

void qemu_chr_fe_take_focus(CharFrontend *c)
{
    if (!c->chr) {
        return;
    }

    if (CHARDEV_IS_MUX(c->chr)) {
        mux_set_focus(c->chr, c->tag);
    }
}

int qemu_chr_fe_wait_connected(CharFrontend *c, Error **errp)
{
    if (!c->chr) {
        error_setg(errp, "missing associated backend");
        return -1;
    }

    return qemu_chr_wait_connected(c->chr, errp);
}

void qemu_chr_fe_set_echo(CharFrontend *c, bool echo)
{
    Chardev *chr = c->chr;

    if (chr && CHARDEV_GET_CLASS(chr)->chr_set_echo) {
        CHARDEV_GET_CLASS(chr)->chr_set_echo(chr, echo);
    }
}

void qemu_chr_fe_set_open(CharFrontend *c, bool is_open)
{
    Chardev *chr = c->chr;

    if (!chr) {
        return;
    }

    if (c->fe_is_open == is_open) {
        return;
    }
    c->fe_is_open = is_open;
    if (CHARDEV_GET_CLASS(chr)->chr_set_fe_open) {
        CHARDEV_GET_CLASS(chr)->chr_set_fe_open(chr, is_open);
    }
}

guint qemu_chr_fe_add_watch(CharFrontend *c, GIOCondition cond,
                            FEWatchFunc func, void *user_data)
{
    Chardev *s = c->chr;
    GSource *src;
    guint tag;

    if (!s || CHARDEV_GET_CLASS(s)->chr_add_watch == NULL) {
        return 0;
    }

    src = CHARDEV_GET_CLASS(s)->chr_add_watch(s, cond);
    if (!src) {
        return 0;
    }

    g_source_set_callback(src, (GSourceFunc)func, user_data, NULL);
    tag = g_source_attach(src, s->gcontext);
    g_source_unref(src);

    return tag;
}

void qemu_chr_fe_disconnect(CharFrontend *c)
{
    Chardev *chr = c->chr;

    if (chr && CHARDEV_GET_CLASS(chr)->chr_disconnect) {
        CHARDEV_GET_CLASS(chr)->chr_disconnect(chr);
    }
}
