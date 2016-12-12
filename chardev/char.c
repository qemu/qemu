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
#include "qemu-common.h"
#include "qemu/cutils.h"
#include "monitor/monitor.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "sysemu/char.h"
#include "hw/usb.h"
#include "qmp-commands.h"
#include "qapi/clone-visitor.h"
#include "qapi-visit.h"
#include "qemu/base64.h"
#include "io/channel-socket.h"
#include "io/channel-file.h"
#include "io/channel-tls.h"
#include "sysemu/replay.h"
#include "qemu/help_option.h"

#include <zlib.h>

#ifndef _WIN32
#include <sys/times.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#ifdef CONFIG_BSD
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <dev/ppbus/ppi.h>
#include <dev/ppbus/ppbconf.h>
#elif defined(__DragonFly__)
#include <dev/misc/ppi/ppi.h>
#include <bus/ppbus/ppbconf.h>
#endif
#else
#ifdef __linux__
#include <linux/ppdev.h>
#include <linux/parport.h>
#endif
#ifdef __sun__
#include <sys/ethernet.h>
#include <sys/sockio.h>
#include <netinet/arp.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h> // must come after ip.h
#include <netinet/udp.h>
#include <netinet/tcp.h>
#endif
#endif
#endif

#include "qemu/sockets.h"
#include "ui/qemu-spice.h"

#include "char-mux.h"
#include "char-fd.h"
#include "char-io.h"
#ifdef _WIN32
#include "char-win.h"
#endif

/***********************************************************/
/* character device */

static QTAILQ_HEAD(ChardevHead, Chardev) chardevs =
    QTAILQ_HEAD_INITIALIZER(chardevs);

void qemu_chr_be_event(Chardev *s, int event)
{
    CharBackend *be = s->be;

    /* Keep track if the char device is open */
    switch (event) {
        case CHR_EVENT_OPENED:
            s->be_open = 1;
            break;
        case CHR_EVENT_CLOSED:
            s->be_open = 0;
            break;
    }

    if (!be || !be->chr_event) {
        return;
    }

    be->chr_event(be->opaque, event);
}

void qemu_chr_be_generic_open(Chardev *s)
{
    qemu_chr_be_event(s, CHR_EVENT_OPENED);
}


/* Not reporting errors from writing to logfile, as logs are
 * defined to be "best effort" only */
static void qemu_chr_fe_write_log(Chardev *s,
                                  const uint8_t *buf, size_t len)
{
    size_t done = 0;
    ssize_t ret;

    if (s->logfd < 0) {
        return;
    }

    while (done < len) {
    retry:
        ret = write(s->logfd, buf + done, len - done);
        if (ret == -1 && errno == EAGAIN) {
            g_usleep(100);
            goto retry;
        }

        if (ret <= 0) {
            return;
        }
        done += ret;
    }
}

static int qemu_chr_fe_write_buffer(Chardev *s,
                                    const uint8_t *buf, int len, int *offset)
{
    ChardevClass *cc = CHARDEV_GET_CLASS(s);
    int res = 0;
    *offset = 0;

    qemu_mutex_lock(&s->chr_write_lock);
    while (*offset < len) {
    retry:
        res = cc->chr_write(s, buf + *offset, len - *offset);
        if (res < 0 && errno == EAGAIN) {
            g_usleep(100);
            goto retry;
        }

        if (res <= 0) {
            break;
        }

        *offset += res;
    }
    if (*offset > 0) {
        qemu_chr_fe_write_log(s, buf, *offset);
    }
    qemu_mutex_unlock(&s->chr_write_lock);

    return res;
}

static bool qemu_chr_replay(Chardev *chr)
{
    return qemu_chr_has_feature(chr, QEMU_CHAR_FEATURE_REPLAY);
}

int qemu_chr_fe_write(CharBackend *be, const uint8_t *buf, int len)
{
    Chardev *s = be->chr;
    ChardevClass *cc;
    int ret;

    if (!s) {
        return 0;
    }

    if (qemu_chr_replay(s) && replay_mode == REPLAY_MODE_PLAY) {
        int offset;
        replay_char_write_event_load(&ret, &offset);
        assert(offset <= len);
        qemu_chr_fe_write_buffer(s, buf, offset, &offset);
        return ret;
    }

    cc = CHARDEV_GET_CLASS(s);
    qemu_mutex_lock(&s->chr_write_lock);
    ret = cc->chr_write(s, buf, len);

    if (ret > 0) {
        qemu_chr_fe_write_log(s, buf, ret);
    }

    qemu_mutex_unlock(&s->chr_write_lock);
    
    if (qemu_chr_replay(s) && replay_mode == REPLAY_MODE_RECORD) {
        replay_char_write_event_save(ret, ret < 0 ? 0 : ret);
    }
    
    return ret;
}

int qemu_chr_write_all(Chardev *s, const uint8_t *buf, int len)
{
    int offset;
    int res;

    if (qemu_chr_replay(s) && replay_mode == REPLAY_MODE_PLAY) {
        replay_char_write_event_load(&res, &offset);
        assert(offset <= len);
        qemu_chr_fe_write_buffer(s, buf, offset, &offset);
        return res;
    }

    res = qemu_chr_fe_write_buffer(s, buf, len, &offset);

    if (qemu_chr_replay(s) && replay_mode == REPLAY_MODE_RECORD) {
        replay_char_write_event_save(res, offset);
    }

    if (res < 0) {
        return res;
    }
    return offset;
}

int qemu_chr_fe_write_all(CharBackend *be, const uint8_t *buf, int len)
{
    Chardev *s = be->chr;

    if (!s) {
        return 0;
    }

    return qemu_chr_write_all(s, buf, len);
}

int qemu_chr_fe_read_all(CharBackend *be, uint8_t *buf, int len)
{
    Chardev *s = be->chr;
    int offset = 0, counter = 10;
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

        if (!counter--) {
            break;
        }
    }

    if (qemu_chr_replay(s) && replay_mode == REPLAY_MODE_RECORD) {
        replay_char_read_all_save_buf(buf, offset);
    }
    return offset;
}

int qemu_chr_fe_ioctl(CharBackend *be, int cmd, void *arg)
{
    Chardev *s = be->chr;
    int res;

    if (!s || !CHARDEV_GET_CLASS(s)->chr_ioctl || qemu_chr_replay(s)) {
        res = -ENOTSUP;
    } else {
        res = CHARDEV_GET_CLASS(s)->chr_ioctl(s, cmd, arg);
    }

    return res;
}

int qemu_chr_be_can_write(Chardev *s)
{
    CharBackend *be = s->be;

    if (!be || !be->chr_can_read) {
        return 0;
    }

    return be->chr_can_read(be->opaque);
}

void qemu_chr_be_write_impl(Chardev *s, uint8_t *buf, int len)
{
    CharBackend *be = s->be;

    if (be && be->chr_read) {
        be->chr_read(be->opaque, buf, len);
    }
}

void qemu_chr_be_write(Chardev *s, uint8_t *buf, int len)
{
    if (qemu_chr_replay(s)) {
        if (replay_mode == REPLAY_MODE_PLAY) {
            return;
        }
        replay_chr_be_write(s, buf, len);
    } else {
        qemu_chr_be_write_impl(s, buf, len);
    }
}

int qemu_chr_fe_get_msgfd(CharBackend *be)
{
    Chardev *s = be->chr;
    int fd;
    int res = (qemu_chr_fe_get_msgfds(be, &fd, 1) == 1) ? fd : -1;
    if (s && qemu_chr_replay(s)) {
        error_report("Replay: get msgfd is not supported "
                     "for serial devices yet");
        exit(1);
    }
    return res;
}

int qemu_chr_fe_get_msgfds(CharBackend *be, int *fds, int len)
{
    Chardev *s = be->chr;

    if (!s) {
        return -1;
    }

    return CHARDEV_GET_CLASS(s)->get_msgfds ?
        CHARDEV_GET_CLASS(s)->get_msgfds(s, fds, len) : -1;
}

int qemu_chr_fe_set_msgfds(CharBackend *be, int *fds, int num)
{
    Chardev *s = be->chr;

    if (!s) {
        return -1;
    }

    return CHARDEV_GET_CLASS(s)->set_msgfds ?
        CHARDEV_GET_CLASS(s)->set_msgfds(s, fds, num) : -1;
}

int qemu_chr_add_client(Chardev *s, int fd)
{
    return CHARDEV_GET_CLASS(s)->chr_add_client ?
        CHARDEV_GET_CLASS(s)->chr_add_client(s, fd) : -1;
}

void qemu_chr_fe_accept_input(CharBackend *be)
{
    Chardev *s = be->chr;

    if (!s) {
        return;
    }

    if (CHARDEV_GET_CLASS(s)->chr_accept_input) {
        CHARDEV_GET_CLASS(s)->chr_accept_input(s);
    }
    qemu_notify_event();
}

void qemu_chr_fe_printf(CharBackend *be, const char *fmt, ...)
{
    char buf[CHR_READ_BUF_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    /* XXX this blocks entire thread. Rewrite to use
     * qemu_chr_fe_write and background I/O callbacks */
    qemu_chr_fe_write_all(be, (uint8_t *)buf, strlen(buf));
    va_end(ap);
}

static void qemu_char_open(Chardev *chr, ChardevBackend *backend,
                           bool *be_opened, Error **errp)
{
    ChardevClass *cc = CHARDEV_GET_CLASS(chr);
    /* Any ChardevCommon member would work */
    ChardevCommon *common = backend ? backend->u.null.data : NULL;

    if (common && common->has_logfile) {
        int flags = O_WRONLY | O_CREAT;
        if (common->has_logappend &&
            common->logappend) {
            flags |= O_APPEND;
        } else {
            flags |= O_TRUNC;
        }
        chr->logfd = qemu_open(common->logfile, flags, 0666);
        if (chr->logfd < 0) {
            error_setg_errno(errp, errno,
                             "Unable to open logfile %s",
                             common->logfile);
            return;
        }
    }

    if (cc->open) {
        cc->open(chr, backend, be_opened, errp);
    }
}

static void char_init(Object *obj)
{
    Chardev *chr = CHARDEV(obj);

    chr->logfd = -1;
    qemu_mutex_init(&chr->chr_write_lock);
}

static int null_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    return len;
}

static void char_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->chr_write = null_chr_write;
}

static void char_finalize(Object *obj)
{
    Chardev *chr = CHARDEV(obj);

    if (chr->be) {
        chr->be->chr = NULL;
    }
    g_free(chr->filename);
    g_free(chr->label);
    if (chr->logfd != -1) {
        close(chr->logfd);
    }
    qemu_mutex_destroy(&chr->chr_write_lock);
}

static const TypeInfo char_type_info = {
    .name = TYPE_CHARDEV,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(Chardev),
    .instance_init = char_init,
    .instance_finalize = char_finalize,
    .abstract = true,
    .class_size = sizeof(ChardevClass),
    .class_init = char_class_init,
};

/**
 * Called after processing of default and command-line-specified
 * chardevs to deliver CHR_EVENT_OPENED events to any FEs attached
 * to a mux chardev. This is done here to ensure that
 * output/prompts/banners are only displayed for the FE that has
 * focus when initial command-line processing/machine init is
 * completed.
 *
 * After this point, any new FE attached to any new or existing
 * mux will receive CHR_EVENT_OPENED notifications for the BE
 * immediately.
 */
static void muxes_realize_done(Notifier *notifier, void *unused)
{
    Chardev *chr;

    QTAILQ_FOREACH(chr, &chardevs, next) {
        if (CHARDEV_IS_MUX(chr)) {
            MuxChardev *d = MUX_CHARDEV(chr);
            int i;

            /* send OPENED to all already-attached FEs */
            for (i = 0; i < d->mux_cnt; i++) {
                mux_chr_send_event(d, i, CHR_EVENT_OPENED);
            }
            /* mark mux as OPENED so any new FEs will immediately receive
             * OPENED event
             */
            qemu_chr_be_generic_open(chr);
        }
    }
    muxes_realized = true;
}

static Notifier muxes_realize_notify = {
    .notify = muxes_realize_done,
};

Chardev *qemu_chr_fe_get_driver(CharBackend *be)
{
    return be->chr;
}

bool qemu_chr_fe_init(CharBackend *b, Chardev *s, Error **errp)
{
    int tag = 0;

    if (CHARDEV_IS_MUX(s)) {
        MuxChardev *d = MUX_CHARDEV(s);

        if (d->mux_cnt >= MAX_MUX) {
            goto unavailable;
        }

        d->backends[d->mux_cnt] = b;
        tag = d->mux_cnt++;
    } else if (s->be) {
        goto unavailable;
    } else {
        s->be = b;
    }

    b->fe_open = false;
    b->tag = tag;
    b->chr = s;
    return true;

unavailable:
    error_setg(errp, QERR_DEVICE_IN_USE, s->label);
    return false;
}

static bool qemu_chr_is_busy(Chardev *s)
{
    if (CHARDEV_IS_MUX(s)) {
        MuxChardev *d = MUX_CHARDEV(s);
        return d->mux_cnt >= 0;
    } else {
        return s->be != NULL;
    }
}

void qemu_chr_fe_deinit(CharBackend *b)
{
    assert(b);

    if (b->chr) {
        qemu_chr_fe_set_handlers(b, NULL, NULL, NULL, NULL, NULL, true);
        if (b->chr->be == b) {
            b->chr->be = NULL;
        }
        if (CHARDEV_IS_MUX(b->chr)) {
            MuxChardev *d = MUX_CHARDEV(b->chr);
            d->backends[b->tag] = NULL;
        }
        b->chr = NULL;
    }
}

void qemu_chr_fe_set_handlers(CharBackend *b,
                              IOCanReadHandler *fd_can_read,
                              IOReadHandler *fd_read,
                              IOEventHandler *fd_event,
                              void *opaque,
                              GMainContext *context,
                              bool set_open)
{
    Chardev *s;
    ChardevClass *cc;
    int fe_open;

    s = b->chr;
    if (!s) {
        return;
    }

    cc = CHARDEV_GET_CLASS(s);
    if (!opaque && !fd_can_read && !fd_read && !fd_event) {
        fe_open = 0;
        remove_fd_in_watch(s);
    } else {
        fe_open = 1;
    }
    b->chr_can_read = fd_can_read;
    b->chr_read = fd_read;
    b->chr_event = fd_event;
    b->opaque = opaque;
    if (cc->chr_update_read_handler) {
        cc->chr_update_read_handler(s, context);
    }

    if (set_open) {
        qemu_chr_fe_set_open(b, fe_open);
    }

    if (fe_open) {
        qemu_chr_fe_take_focus(b);
        /* We're connecting to an already opened device, so let's make sure we
           also get the open event */
        if (s->be_open) {
            qemu_chr_be_generic_open(s);
        }
    }

    if (CHARDEV_IS_MUX(s)) {
        mux_chr_set_handlers(s, context);
    }
}

void qemu_chr_fe_take_focus(CharBackend *b)
{
    if (!b->chr) {
        return;
    }

    if (CHARDEV_IS_MUX(b->chr)) {
        mux_set_focus(b->chr, b->tag);
    }
}

#ifndef _WIN32
static void qemu_chr_open_pipe(Chardev *chr,
                               ChardevBackend *backend,
                               bool *be_opened,
                               Error **errp)
{
    ChardevHostdev *opts = backend->u.pipe.data;
    int fd_in, fd_out;
    char *filename_in;
    char *filename_out;
    const char *filename = opts->device;

    filename_in = g_strdup_printf("%s.in", filename);
    filename_out = g_strdup_printf("%s.out", filename);
    TFR(fd_in = qemu_open(filename_in, O_RDWR | O_BINARY));
    TFR(fd_out = qemu_open(filename_out, O_RDWR | O_BINARY));
    g_free(filename_in);
    g_free(filename_out);
    if (fd_in < 0 || fd_out < 0) {
	if (fd_in >= 0)
	    close(fd_in);
	if (fd_out >= 0)
	    close(fd_out);
        TFR(fd_in = fd_out = qemu_open(filename, O_RDWR | O_BINARY));
        if (fd_in < 0) {
            error_setg_file_open(errp, errno, filename);
            return;
        }
    }
    qemu_chr_open_fd(chr, fd_in, fd_out);
}

#if defined(__linux__) || defined(__sun__) || defined(__FreeBSD__) \
    || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) \
    || defined(__GLIBC__)

#define HAVE_CHARDEV_SERIAL 1
#define HAVE_CHARDEV_PTY 1

typedef struct {
    Chardev parent;
    QIOChannel *ioc;
    int read_bytes;

    /* Protected by the Chardev chr_write_lock.  */
    int connected;
    guint timer_tag;
    guint open_tag;
} PtyChardev;

#define PTY_CHARDEV(obj) OBJECT_CHECK(PtyChardev, (obj), TYPE_CHARDEV_PTY)

static void pty_chr_update_read_handler_locked(Chardev *chr);
static void pty_chr_state(Chardev *chr, int connected);

static gboolean pty_chr_timer(gpointer opaque)
{
    struct Chardev *chr = CHARDEV(opaque);
    PtyChardev *s = PTY_CHARDEV(opaque);

    qemu_mutex_lock(&chr->chr_write_lock);
    s->timer_tag = 0;
    s->open_tag = 0;
    if (!s->connected) {
        /* Next poll ... */
        pty_chr_update_read_handler_locked(chr);
    }
    qemu_mutex_unlock(&chr->chr_write_lock);
    return FALSE;
}

/* Called with chr_write_lock held.  */
static void pty_chr_rearm_timer(Chardev *chr, int ms)
{
    PtyChardev *s = PTY_CHARDEV(chr);
    char *name;

    if (s->timer_tag) {
        g_source_remove(s->timer_tag);
        s->timer_tag = 0;
    }

    if (ms == 1000) {
        name = g_strdup_printf("pty-timer-secs-%s", chr->label);
        s->timer_tag = g_timeout_add_seconds(1, pty_chr_timer, chr);
    } else {
        name = g_strdup_printf("pty-timer-ms-%s", chr->label);
        s->timer_tag = g_timeout_add(ms, pty_chr_timer, chr);
    }
    g_source_set_name_by_id(s->timer_tag, name);
    g_free(name);
}

/* Called with chr_write_lock held.  */
static void pty_chr_update_read_handler_locked(Chardev *chr)
{
    PtyChardev *s = PTY_CHARDEV(chr);
    GPollFD pfd;
    int rc;
    QIOChannelFile *fioc = QIO_CHANNEL_FILE(s->ioc);

    pfd.fd = fioc->fd;
    pfd.events = G_IO_OUT;
    pfd.revents = 0;
    do {
        rc = g_poll(&pfd, 1, 0);
    } while (rc == -1 && errno == EINTR);
    assert(rc >= 0);

    if (pfd.revents & G_IO_HUP) {
        pty_chr_state(chr, 0);
    } else {
        pty_chr_state(chr, 1);
    }
}

static void pty_chr_update_read_handler(Chardev *chr,
                                        GMainContext *context)
{
    qemu_mutex_lock(&chr->chr_write_lock);
    pty_chr_update_read_handler_locked(chr);
    qemu_mutex_unlock(&chr->chr_write_lock);
}

/* Called with chr_write_lock held.  */
static int char_pty_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    PtyChardev *s = PTY_CHARDEV(chr);

    if (!s->connected) {
        /* guest sends data, check for (re-)connect */
        pty_chr_update_read_handler_locked(chr);
        if (!s->connected) {
            return 0;
        }
    }
    return io_channel_send(s->ioc, buf, len);
}

static GSource *pty_chr_add_watch(Chardev *chr, GIOCondition cond)
{
    PtyChardev *s = PTY_CHARDEV(chr);
    if (!s->connected) {
        return NULL;
    }
    return qio_channel_create_watch(s->ioc, cond);
}

static int pty_chr_read_poll(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    PtyChardev *s = PTY_CHARDEV(opaque);

    s->read_bytes = qemu_chr_be_can_write(chr);
    return s->read_bytes;
}

static gboolean pty_chr_read(QIOChannel *chan, GIOCondition cond, void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    PtyChardev *s = PTY_CHARDEV(opaque);
    gsize len;
    uint8_t buf[CHR_READ_BUF_LEN];
    ssize_t ret;

    len = sizeof(buf);
    if (len > s->read_bytes)
        len = s->read_bytes;
    if (len == 0) {
        return TRUE;
    }
    ret = qio_channel_read(s->ioc, (char *)buf, len, NULL);
    if (ret <= 0) {
        pty_chr_state(chr, 0);
        return FALSE;
    } else {
        pty_chr_state(chr, 1);
        qemu_chr_be_write(chr, buf, ret);
    }
    return TRUE;
}

static gboolean qemu_chr_be_generic_open_func(gpointer opaque)
{
    Chardev *chr = CHARDEV(opaque);
    PtyChardev *s = PTY_CHARDEV(opaque);

    s->open_tag = 0;
    qemu_chr_be_generic_open(chr);
    return FALSE;
}

/* Called with chr_write_lock held.  */
static void pty_chr_state(Chardev *chr, int connected)
{
    PtyChardev *s = PTY_CHARDEV(chr);

    if (!connected) {
        if (s->open_tag) {
            g_source_remove(s->open_tag);
            s->open_tag = 0;
        }
        remove_fd_in_watch(chr);
        s->connected = 0;
        /* (re-)connect poll interval for idle guests: once per second.
         * We check more frequently in case the guests sends data to
         * the virtual device linked to our pty. */
        pty_chr_rearm_timer(chr, 1000);
    } else {
        if (s->timer_tag) {
            g_source_remove(s->timer_tag);
            s->timer_tag = 0;
        }
        if (!s->connected) {
            g_assert(s->open_tag == 0);
            s->connected = 1;
            s->open_tag = g_idle_add(qemu_chr_be_generic_open_func, chr);
        }
        if (!chr->fd_in_tag) {
            chr->fd_in_tag = io_add_watch_poll(chr, s->ioc,
                                               pty_chr_read_poll,
                                               pty_chr_read,
                                               chr, NULL);
        }
    }
}

static void char_pty_finalize(Object *obj)
{
    Chardev *chr = CHARDEV(obj);
    PtyChardev *s = PTY_CHARDEV(obj);

    qemu_mutex_lock(&chr->chr_write_lock);
    pty_chr_state(chr, 0);
    object_unref(OBJECT(s->ioc));
    if (s->timer_tag) {
        g_source_remove(s->timer_tag);
        s->timer_tag = 0;
    }
    qemu_mutex_unlock(&chr->chr_write_lock);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

static void char_pty_open(Chardev *chr,
                          ChardevBackend *backend,
                          bool *be_opened,
                          Error **errp)
{
    PtyChardev *s;
    int master_fd, slave_fd;
    char pty_name[PATH_MAX];
    char *name;

    master_fd = qemu_openpty_raw(&slave_fd, pty_name);
    if (master_fd < 0) {
        error_setg_errno(errp, errno, "Failed to create PTY");
        return;
    }

    close(slave_fd);
    qemu_set_nonblock(master_fd);

    chr->filename = g_strdup_printf("pty:%s", pty_name);
    error_report("char device redirected to %s (label %s)",
                 pty_name, chr->label);

    s = PTY_CHARDEV(chr);
    s->ioc = QIO_CHANNEL(qio_channel_file_new_fd(master_fd));
    name = g_strdup_printf("chardev-pty-%s", chr->label);
    qio_channel_set_name(QIO_CHANNEL(s->ioc), name);
    g_free(name);
    s->timer_tag = 0;
    *be_opened = false;
}

static void char_pty_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->open = char_pty_open;
    cc->chr_write = char_pty_chr_write;
    cc->chr_update_read_handler = pty_chr_update_read_handler;
    cc->chr_add_watch = pty_chr_add_watch;
}

static const TypeInfo char_pty_type_info = {
    .name = TYPE_CHARDEV_PTY,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(PtyChardev),
    .instance_finalize = char_pty_finalize,
    .class_init = char_pty_class_init,
};

static void tty_serial_init(int fd, int speed,
                            int parity, int data_bits, int stop_bits)
{
    struct termios tty;
    speed_t spd;

#if 0
    printf("tty_serial_init: speed=%d parity=%c data=%d stop=%d\n",
           speed, parity, data_bits, stop_bits);
#endif
    tcgetattr (fd, &tty);

#define check_speed(val) if (speed <= val) { spd = B##val; break; }
    speed = speed * 10 / 11;
    do {
        check_speed(50);
        check_speed(75);
        check_speed(110);
        check_speed(134);
        check_speed(150);
        check_speed(200);
        check_speed(300);
        check_speed(600);
        check_speed(1200);
        check_speed(1800);
        check_speed(2400);
        check_speed(4800);
        check_speed(9600);
        check_speed(19200);
        check_speed(38400);
        /* Non-Posix values follow. They may be unsupported on some systems. */
        check_speed(57600);
        check_speed(115200);
#ifdef B230400
        check_speed(230400);
#endif
#ifdef B460800
        check_speed(460800);
#endif
#ifdef B500000
        check_speed(500000);
#endif
#ifdef B576000
        check_speed(576000);
#endif
#ifdef B921600
        check_speed(921600);
#endif
#ifdef B1000000
        check_speed(1000000);
#endif
#ifdef B1152000
        check_speed(1152000);
#endif
#ifdef B1500000
        check_speed(1500000);
#endif
#ifdef B2000000
        check_speed(2000000);
#endif
#ifdef B2500000
        check_speed(2500000);
#endif
#ifdef B3000000
        check_speed(3000000);
#endif
#ifdef B3500000
        check_speed(3500000);
#endif
#ifdef B4000000
        check_speed(4000000);
#endif
        spd = B115200;
    } while (0);

    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN|ISIG);
    tty.c_cflag &= ~(CSIZE|PARENB|PARODD|CRTSCTS|CSTOPB);
    switch(data_bits) {
    default:
    case 8:
        tty.c_cflag |= CS8;
        break;
    case 7:
        tty.c_cflag |= CS7;
        break;
    case 6:
        tty.c_cflag |= CS6;
        break;
    case 5:
        tty.c_cflag |= CS5;
        break;
    }
    switch(parity) {
    default:
    case 'N':
        break;
    case 'E':
        tty.c_cflag |= PARENB;
        break;
    case 'O':
        tty.c_cflag |= PARENB | PARODD;
        break;
    }
    if (stop_bits == 2)
        tty.c_cflag |= CSTOPB;

    tcsetattr (fd, TCSANOW, &tty);
}

static int tty_serial_ioctl(Chardev *chr, int cmd, void *arg)
{
    FDChardev *s = FD_CHARDEV(chr);
    QIOChannelFile *fioc = QIO_CHANNEL_FILE(s->ioc_in);

    switch(cmd) {
    case CHR_IOCTL_SERIAL_SET_PARAMS:
        {
            QEMUSerialSetParams *ssp = arg;
            tty_serial_init(fioc->fd,
                            ssp->speed, ssp->parity,
                            ssp->data_bits, ssp->stop_bits);
        }
        break;
    case CHR_IOCTL_SERIAL_SET_BREAK:
        {
            int enable = *(int *)arg;
            if (enable) {
                tcsendbreak(fioc->fd, 1);
            }
        }
        break;
    case CHR_IOCTL_SERIAL_GET_TIOCM:
        {
            int sarg = 0;
            int *targ = (int *)arg;
            ioctl(fioc->fd, TIOCMGET, &sarg);
            *targ = 0;
            if (sarg & TIOCM_CTS)
                *targ |= CHR_TIOCM_CTS;
            if (sarg & TIOCM_CAR)
                *targ |= CHR_TIOCM_CAR;
            if (sarg & TIOCM_DSR)
                *targ |= CHR_TIOCM_DSR;
            if (sarg & TIOCM_RI)
                *targ |= CHR_TIOCM_RI;
            if (sarg & TIOCM_DTR)
                *targ |= CHR_TIOCM_DTR;
            if (sarg & TIOCM_RTS)
                *targ |= CHR_TIOCM_RTS;
        }
        break;
    case CHR_IOCTL_SERIAL_SET_TIOCM:
        {
            int sarg = *(int *)arg;
            int targ = 0;
            ioctl(fioc->fd, TIOCMGET, &targ);
            targ &= ~(CHR_TIOCM_CTS | CHR_TIOCM_CAR | CHR_TIOCM_DSR
                     | CHR_TIOCM_RI | CHR_TIOCM_DTR | CHR_TIOCM_RTS);
            if (sarg & CHR_TIOCM_CTS)
                targ |= TIOCM_CTS;
            if (sarg & CHR_TIOCM_CAR)
                targ |= TIOCM_CAR;
            if (sarg & CHR_TIOCM_DSR)
                targ |= TIOCM_DSR;
            if (sarg & CHR_TIOCM_RI)
                targ |= TIOCM_RI;
            if (sarg & CHR_TIOCM_DTR)
                targ |= TIOCM_DTR;
            if (sarg & CHR_TIOCM_RTS)
                targ |= TIOCM_RTS;
            ioctl(fioc->fd, TIOCMSET, &targ);
        }
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}
#endif /* __linux__ || __sun__ */

#if defined(__linux__)

#define HAVE_CHARDEV_PARPORT 1

typedef struct {
    Chardev parent;
    int fd;
    int mode;
} ParallelChardev;

#define PARALLEL_CHARDEV(obj) \
    OBJECT_CHECK(ParallelChardev, (obj), TYPE_CHARDEV_PARALLEL)

static int pp_hw_mode(ParallelChardev *s, uint16_t mode)
{
    if (s->mode != mode) {
	int m = mode;
        if (ioctl(s->fd, PPSETMODE, &m) < 0)
            return 0;
	s->mode = mode;
    }
    return 1;
}

static int pp_ioctl(Chardev *chr, int cmd, void *arg)
{
    ParallelChardev *drv = PARALLEL_CHARDEV(chr);
    int fd = drv->fd;
    uint8_t b;

    switch(cmd) {
    case CHR_IOCTL_PP_READ_DATA:
        if (ioctl(fd, PPRDATA, &b) < 0)
            return -ENOTSUP;
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_WRITE_DATA:
        b = *(uint8_t *)arg;
        if (ioctl(fd, PPWDATA, &b) < 0)
            return -ENOTSUP;
        break;
    case CHR_IOCTL_PP_READ_CONTROL:
        if (ioctl(fd, PPRCONTROL, &b) < 0)
            return -ENOTSUP;
	/* Linux gives only the lowest bits, and no way to know data
	   direction! For better compatibility set the fixed upper
	   bits. */
        *(uint8_t *)arg = b | 0xc0;
        break;
    case CHR_IOCTL_PP_WRITE_CONTROL:
        b = *(uint8_t *)arg;
        if (ioctl(fd, PPWCONTROL, &b) < 0)
            return -ENOTSUP;
        break;
    case CHR_IOCTL_PP_READ_STATUS:
        if (ioctl(fd, PPRSTATUS, &b) < 0)
            return -ENOTSUP;
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_DATA_DIR:
        if (ioctl(fd, PPDATADIR, (int *)arg) < 0)
            return -ENOTSUP;
        break;
    case CHR_IOCTL_PP_EPP_READ_ADDR:
	if (pp_hw_mode(drv, IEEE1284_MODE_EPP|IEEE1284_ADDR)) {
	    struct ParallelIOArg *parg = arg;
	    int n = read(fd, parg->buffer, parg->count);
	    if (n != parg->count) {
		return -EIO;
	    }
	}
        break;
    case CHR_IOCTL_PP_EPP_READ:
	if (pp_hw_mode(drv, IEEE1284_MODE_EPP)) {
	    struct ParallelIOArg *parg = arg;
	    int n = read(fd, parg->buffer, parg->count);
	    if (n != parg->count) {
		return -EIO;
	    }
	}
        break;
    case CHR_IOCTL_PP_EPP_WRITE_ADDR:
	if (pp_hw_mode(drv, IEEE1284_MODE_EPP|IEEE1284_ADDR)) {
	    struct ParallelIOArg *parg = arg;
	    int n = write(fd, parg->buffer, parg->count);
	    if (n != parg->count) {
		return -EIO;
	    }
	}
        break;
    case CHR_IOCTL_PP_EPP_WRITE:
	if (pp_hw_mode(drv, IEEE1284_MODE_EPP)) {
	    struct ParallelIOArg *parg = arg;
	    int n = write(fd, parg->buffer, parg->count);
	    if (n != parg->count) {
		return -EIO;
	    }
	}
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static void qemu_chr_open_pp_fd(Chardev *chr,
                                int fd,
                                bool *be_opened,
                                Error **errp)
{
    ParallelChardev *drv = PARALLEL_CHARDEV(chr);

    if (ioctl(fd, PPCLAIM) < 0) {
        error_setg_errno(errp, errno, "not a parallel port");
        close(fd);
        return;
    }

    drv->fd = fd;
    drv->mode = IEEE1284_MODE_COMPAT;
}
#endif /* __linux__ */

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__)

#define HAVE_CHARDEV_PARPORT 1

typedef struct {
    Chardev parent;
    int fd;
} ParallelChardev;

#define PARALLEL_CHARDEV(obj)                                   \
    OBJECT_CHECK(ParallelChardev, (obj), TYPE_CHARDEV_PARALLEL)

static int pp_ioctl(Chardev *chr, int cmd, void *arg)
{
    ParallelChardev *drv = PARALLEL_CHARDEV(chr);
    uint8_t b;

    switch (cmd) {
    case CHR_IOCTL_PP_READ_DATA:
        if (ioctl(drv->fd, PPIGDATA, &b) < 0) {
            return -ENOTSUP;
        }
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_WRITE_DATA:
        b = *(uint8_t *)arg;
        if (ioctl(drv->fd, PPISDATA, &b) < 0) {
            return -ENOTSUP;
        }
        break;
    case CHR_IOCTL_PP_READ_CONTROL:
        if (ioctl(drv->fd, PPIGCTRL, &b) < 0) {
            return -ENOTSUP;
        }
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_WRITE_CONTROL:
        b = *(uint8_t *)arg;
        if (ioctl(drv->fd, PPISCTRL, &b) < 0) {
            return -ENOTSUP;
        }
        break;
    case CHR_IOCTL_PP_READ_STATUS:
        if (ioctl(drv->fd, PPIGSTATUS, &b) < 0) {
            return -ENOTSUP;
        }
        *(uint8_t *)arg = b;
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static void qemu_chr_open_pp_fd(Chardev *chr,
                                int fd,
                                bool *be_opened,
                                Error **errp)
{
    ParallelChardev *drv = PARALLEL_CHARDEV(chr);
    drv->fd = fd;
    *be_opened = false;
}
#endif

#else /* _WIN32 */

#define HAVE_CHARDEV_SERIAL 1

#define MAXCONNECT 1
#define NTIMEOUT 5000

static int win_chr_pipe_init(Chardev *chr, const char *filename,
                             Error **errp)
{
    WinChardev *s = WIN_CHARDEV(chr);
    OVERLAPPED ov;
    int ret;
    DWORD size;
    char *openname;

    s->fpipe = TRUE;

    s->hsend = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hsend) {
        error_setg(errp, "Failed CreateEvent");
        goto fail;
    }
    s->hrecv = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hrecv) {
        error_setg(errp, "Failed CreateEvent");
        goto fail;
    }

    openname = g_strdup_printf("\\\\.\\pipe\\%s", filename);
    s->hcom = CreateNamedPipe(openname, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE |
                              PIPE_WAIT,
                              MAXCONNECT, NSENDBUF, NRECVBUF, NTIMEOUT, NULL);
    g_free(openname);
    if (s->hcom == INVALID_HANDLE_VALUE) {
        error_setg(errp, "Failed CreateNamedPipe (%lu)", GetLastError());
        s->hcom = NULL;
        goto fail;
    }

    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    ret = ConnectNamedPipe(s->hcom, &ov);
    if (ret) {
        error_setg(errp, "Failed ConnectNamedPipe");
        goto fail;
    }

    ret = GetOverlappedResult(s->hcom, &ov, &size, TRUE);
    if (!ret) {
        error_setg(errp, "Failed GetOverlappedResult");
        if (ov.hEvent) {
            CloseHandle(ov.hEvent);
            ov.hEvent = NULL;
        }
        goto fail;
    }

    if (ov.hEvent) {
        CloseHandle(ov.hEvent);
        ov.hEvent = NULL;
    }
    qemu_add_polling_cb(win_chr_pipe_poll, chr);
    return 0;

 fail:
    return -1;
}


static void qemu_chr_open_pipe(Chardev *chr,
                               ChardevBackend *backend,
                               bool *be_opened,
                               Error **errp)
{
    ChardevHostdev *opts = backend->u.pipe.data;
    const char *filename = opts->device;

    if (win_chr_pipe_init(chr, filename, errp) < 0) {
        return;
    }
}

static void qemu_chr_open_win_con(Chardev *chr,
                                  ChardevBackend *backend,
                                  bool *be_opened,
                                  Error **errp)
{
    qemu_chr_open_win_file(chr, GetStdHandle(STD_OUTPUT_HANDLE));
}

static void char_console_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->open = qemu_chr_open_win_con;
}

static const TypeInfo char_console_type_info = {
    .name = TYPE_CHARDEV_CONSOLE,
    .parent = TYPE_CHARDEV_WIN,
    .class_init = char_console_class_init,
};

#endif /* !_WIN32 */

int qemu_chr_wait_connected(Chardev *chr, Error **errp)
{
    ChardevClass *cc = CHARDEV_GET_CLASS(chr);

    if (cc->chr_wait_connected) {
        return cc->chr_wait_connected(chr, errp);
    }

    return 0;
}

int qemu_chr_fe_wait_connected(CharBackend *be, Error **errp)
{
    if (!be->chr) {
        error_setg(errp, "missing associated backend");
        return -1;
    }

    return qemu_chr_wait_connected(be->chr, errp);
}

QemuOpts *qemu_chr_parse_compat(const char *label, const char *filename)
{
    char host[65], port[33], width[8], height[8];
    int pos;
    const char *p;
    QemuOpts *opts;
    Error *local_err = NULL;

    opts = qemu_opts_create(qemu_find_opts("chardev"), label, 1, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return NULL;
    }

    if (strstart(filename, "mon:", &p)) {
        filename = p;
        qemu_opt_set(opts, "mux", "on", &error_abort);
        if (strcmp(filename, "stdio") == 0) {
            /* Monitor is muxed to stdio: do not exit on Ctrl+C by default
             * but pass it to the guest.  Handle this only for compat syntax,
             * for -chardev syntax we have special option for this.
             * This is what -nographic did, redirecting+muxing serial+monitor
             * to stdio causing Ctrl+C to be passed to guest. */
            qemu_opt_set(opts, "signal", "off", &error_abort);
        }
    }

    if (strcmp(filename, "null")    == 0 ||
        strcmp(filename, "pty")     == 0 ||
        strcmp(filename, "msmouse") == 0 ||
        strcmp(filename, "braille") == 0 ||
        strcmp(filename, "testdev") == 0 ||
        strcmp(filename, "stdio")   == 0) {
        qemu_opt_set(opts, "backend", filename, &error_abort);
        return opts;
    }
    if (strstart(filename, "vc", &p)) {
        qemu_opt_set(opts, "backend", "vc", &error_abort);
        if (*p == ':') {
            if (sscanf(p+1, "%7[0-9]x%7[0-9]", width, height) == 2) {
                /* pixels */
                qemu_opt_set(opts, "width", width, &error_abort);
                qemu_opt_set(opts, "height", height, &error_abort);
            } else if (sscanf(p+1, "%7[0-9]Cx%7[0-9]C", width, height) == 2) {
                /* chars */
                qemu_opt_set(opts, "cols", width, &error_abort);
                qemu_opt_set(opts, "rows", height, &error_abort);
            } else {
                goto fail;
            }
        }
        return opts;
    }
    if (strcmp(filename, "con:") == 0) {
        qemu_opt_set(opts, "backend", "console", &error_abort);
        return opts;
    }
    if (strstart(filename, "COM", NULL)) {
        qemu_opt_set(opts, "backend", "serial", &error_abort);
        qemu_opt_set(opts, "path", filename, &error_abort);
        return opts;
    }
    if (strstart(filename, "file:", &p)) {
        qemu_opt_set(opts, "backend", "file", &error_abort);
        qemu_opt_set(opts, "path", p, &error_abort);
        return opts;
    }
    if (strstart(filename, "pipe:", &p)) {
        qemu_opt_set(opts, "backend", "pipe", &error_abort);
        qemu_opt_set(opts, "path", p, &error_abort);
        return opts;
    }
    if (strstart(filename, "tcp:", &p) ||
        strstart(filename, "telnet:", &p)) {
        if (sscanf(p, "%64[^:]:%32[^,]%n", host, port, &pos) < 2) {
            host[0] = 0;
            if (sscanf(p, ":%32[^,]%n", port, &pos) < 1)
                goto fail;
        }
        qemu_opt_set(opts, "backend", "socket", &error_abort);
        qemu_opt_set(opts, "host", host, &error_abort);
        qemu_opt_set(opts, "port", port, &error_abort);
        if (p[pos] == ',') {
            qemu_opts_do_parse(opts, p+pos+1, NULL, &local_err);
            if (local_err) {
                error_report_err(local_err);
                goto fail;
            }
        }
        if (strstart(filename, "telnet:", &p))
            qemu_opt_set(opts, "telnet", "on", &error_abort);
        return opts;
    }
    if (strstart(filename, "udp:", &p)) {
        qemu_opt_set(opts, "backend", "udp", &error_abort);
        if (sscanf(p, "%64[^:]:%32[^@,]%n", host, port, &pos) < 2) {
            host[0] = 0;
            if (sscanf(p, ":%32[^@,]%n", port, &pos) < 1) {
                goto fail;
            }
        }
        qemu_opt_set(opts, "host", host, &error_abort);
        qemu_opt_set(opts, "port", port, &error_abort);
        if (p[pos] == '@') {
            p += pos + 1;
            if (sscanf(p, "%64[^:]:%32[^,]%n", host, port, &pos) < 2) {
                host[0] = 0;
                if (sscanf(p, ":%32[^,]%n", port, &pos) < 1) {
                    goto fail;
                }
            }
            qemu_opt_set(opts, "localaddr", host, &error_abort);
            qemu_opt_set(opts, "localport", port, &error_abort);
        }
        return opts;
    }
    if (strstart(filename, "unix:", &p)) {
        qemu_opt_set(opts, "backend", "socket", &error_abort);
        qemu_opts_do_parse(opts, p, "path", &local_err);
        if (local_err) {
            error_report_err(local_err);
            goto fail;
        }
        return opts;
    }
    if (strstart(filename, "/dev/parport", NULL) ||
        strstart(filename, "/dev/ppi", NULL)) {
        qemu_opt_set(opts, "backend", "parport", &error_abort);
        qemu_opt_set(opts, "path", filename, &error_abort);
        return opts;
    }
    if (strstart(filename, "/dev/", NULL)) {
        qemu_opt_set(opts, "backend", "tty", &error_abort);
        qemu_opt_set(opts, "path", filename, &error_abort);
        return opts;
    }

fail:
    qemu_opts_del(opts);
    return NULL;
}

void qemu_chr_parse_common(QemuOpts *opts, ChardevCommon *backend)
{
    const char *logfile = qemu_opt_get(opts, "logfile");

    backend->has_logfile = logfile != NULL;
    backend->logfile = logfile ? g_strdup(logfile) : NULL;

    backend->has_logappend = true;
    backend->logappend = qemu_opt_get_bool(opts, "logappend", false);
}

#ifdef HAVE_CHARDEV_SERIAL
static void qemu_chr_parse_serial(QemuOpts *opts, ChardevBackend *backend,
                                  Error **errp)
{
    const char *device = qemu_opt_get(opts, "path");
    ChardevHostdev *serial;

    backend->type = CHARDEV_BACKEND_KIND_SERIAL;
    if (device == NULL) {
        error_setg(errp, "chardev: serial/tty: no device path given");
        return;
    }
    serial = backend->u.serial.data = g_new0(ChardevHostdev, 1);
    qemu_chr_parse_common(opts, qapi_ChardevHostdev_base(serial));
    serial->device = g_strdup(device);
}
#endif

#ifdef HAVE_CHARDEV_PARPORT
static void qemu_chr_parse_parallel(QemuOpts *opts, ChardevBackend *backend,
                                    Error **errp)
{
    const char *device = qemu_opt_get(opts, "path");
    ChardevHostdev *parallel;

    backend->type = CHARDEV_BACKEND_KIND_PARALLEL;
    if (device == NULL) {
        error_setg(errp, "chardev: parallel: no device path given");
        return;
    }
    parallel = backend->u.parallel.data = g_new0(ChardevHostdev, 1);
    qemu_chr_parse_common(opts, qapi_ChardevHostdev_base(parallel));
    parallel->device = g_strdup(device);
}
#endif

static void qemu_chr_parse_pipe(QemuOpts *opts, ChardevBackend *backend,
                                Error **errp)
{
    const char *device = qemu_opt_get(opts, "path");
    ChardevHostdev *dev;

    backend->type = CHARDEV_BACKEND_KIND_PIPE;
    if (device == NULL) {
        error_setg(errp, "chardev: pipe: no device path given");
        return;
    }
    dev = backend->u.pipe.data = g_new0(ChardevHostdev, 1);
    qemu_chr_parse_common(opts, qapi_ChardevHostdev_base(dev));
    dev->device = g_strdup(device);
}

static void char_pipe_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_pipe;
    cc->open = qemu_chr_open_pipe;
}

static const TypeInfo char_pipe_type_info = {
    .name = TYPE_CHARDEV_PIPE,
#ifdef _WIN32
    .parent = TYPE_CHARDEV_WIN,
#else
    .parent = TYPE_CHARDEV_FD,
#endif
    .class_init = char_pipe_class_init,
};

static const ChardevClass *char_get_class(const char *driver, Error **errp)
{
    ObjectClass *oc;
    const ChardevClass *cc;
    char *typename = g_strdup_printf("chardev-%s", driver);

    oc = object_class_by_name(typename);
    g_free(typename);

    if (!object_class_dynamic_cast(oc, TYPE_CHARDEV)) {
        error_setg(errp, "'%s' is not a valid char driver name", driver);
        return NULL;
    }

    if (object_class_is_abstract(oc)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "driver",
                   "abstract device type");
        return NULL;
    }

    cc = CHARDEV_CLASS(oc);
    if (cc->internal) {
        error_setg(errp, "'%s' is not a valid char driver name", driver);
        return NULL;
    }

    return cc;
}

static Chardev *qemu_chardev_add(const char *id, const char *typename,
                                 ChardevBackend *backend, Error **errp)
{
    Chardev *chr;

    chr = qemu_chr_find(id);
    if (chr) {
        error_setg(errp, "Chardev '%s' already exists", id);
        return NULL;
    }

    chr = qemu_chardev_new(id, typename, backend, errp);
    if (!chr) {
        return NULL;
    }

    QTAILQ_INSERT_TAIL(&chardevs, chr, next);
    return chr;
}

static const struct ChardevAlias {
    const char *typename;
    const char *alias;
} chardev_alias_table[] = {
#ifdef HAVE_CHARDEV_PARPORT
    { "parallel", "parport" },
#endif
#ifdef HAVE_CHARDEV_SERIAL
    { "serial", "tty" },
#endif
};

typedef struct ChadevClassFE {
    void (*fn)(const char *name, void *opaque);
    void *opaque;
} ChadevClassFE;

static void
chardev_class_foreach(ObjectClass *klass, void *opaque)
{
    ChadevClassFE *fe = opaque;

    assert(g_str_has_prefix(object_class_get_name(klass), "chardev-"));
    if (CHARDEV_CLASS(klass)->internal) {
        return;
    }

    fe->fn(object_class_get_name(klass) + 8, fe->opaque);
}

static void
chardev_name_foreach(void (*fn)(const char *name, void *opaque), void *opaque)
{
    ChadevClassFE fe = { .fn = fn, .opaque = opaque };
    int i;

    object_class_foreach(chardev_class_foreach, TYPE_CHARDEV, false, &fe);

    for (i = 0; i < ARRAY_SIZE(chardev_alias_table); i++) {
        fn(chardev_alias_table[i].alias, opaque);
    }
}

static void
help_string_append(const char *name, void *opaque)
{
    GString *str = opaque;

    g_string_append_printf(str, "\n%s", name);
}

Chardev *qemu_chr_new_from_opts(QemuOpts *opts,
                                Error **errp)
{
    Error *local_err = NULL;
    const ChardevClass *cc;
    Chardev *chr;
    int i;
    ChardevBackend *backend = NULL;
    const char *name = qemu_opt_get(opts, "backend");
    const char *id = qemu_opts_id(opts);
    char *bid = NULL;

    if (name == NULL) {
        error_setg(errp, "chardev: \"%s\" missing backend",
                   qemu_opts_id(opts));
        return NULL;
    }

    if (is_help_option(name)) {
        GString *str = g_string_new("");

        chardev_name_foreach(help_string_append, str);

        error_report("Available chardev backend types: %s", str->str);
        g_string_free(str, true);
        exit(0);
    }

    if (id == NULL) {
        error_setg(errp, "chardev: no id specified");
        return NULL;
    }

    for (i = 0; i < ARRAY_SIZE(chardev_alias_table); i++) {
        if (g_strcmp0(chardev_alias_table[i].alias, name) == 0) {
            name = chardev_alias_table[i].typename;
            break;
        }
    }

    cc = char_get_class(name, errp);
    if (cc == NULL) {
        return NULL;
    }

    backend = g_new0(ChardevBackend, 1);
    backend->type = CHARDEV_BACKEND_KIND_NULL;

    if (qemu_opt_get_bool(opts, "mux", 0)) {
        bid = g_strdup_printf("%s-base", id);
    }

    chr = NULL;
    if (cc->parse) {
        cc->parse(opts, backend, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            goto out;
        }
    } else {
        ChardevCommon *ccom = g_new0(ChardevCommon, 1);
        qemu_chr_parse_common(opts, ccom);
        backend->u.null.data = ccom; /* Any ChardevCommon member would work */
    }

    chr = qemu_chardev_add(bid ? bid : id,
                           object_class_get_name(OBJECT_CLASS(cc)),
                           backend, errp);
    if (chr == NULL) {
        goto out;
    }

    if (bid) {
        Chardev *mux;
        qapi_free_ChardevBackend(backend);
        backend = g_new0(ChardevBackend, 1);
        backend->type = CHARDEV_BACKEND_KIND_MUX;
        backend->u.mux.data = g_new0(ChardevMux, 1);
        backend->u.mux.data->chardev = g_strdup(bid);
        mux = qemu_chardev_add(id, TYPE_CHARDEV_MUX, backend, errp);
        if (mux == NULL) {
            qemu_chr_delete(chr);
            chr = NULL;
            goto out;
        }
        chr = mux;
    }

out:
    qapi_free_ChardevBackend(backend);
    g_free(bid);
    return chr;
}

Chardev *qemu_chr_new_noreplay(const char *label, const char *filename)
{
    const char *p;
    Chardev *chr;
    QemuOpts *opts;
    Error *err = NULL;

    if (strstart(filename, "chardev:", &p)) {
        return qemu_chr_find(p);
    }

    opts = qemu_chr_parse_compat(label, filename);
    if (!opts)
        return NULL;

    chr = qemu_chr_new_from_opts(opts, &err);
    if (err) {
        error_report_err(err);
    }
    if (chr && qemu_opt_get_bool(opts, "mux", 0)) {
        monitor_init(chr, MONITOR_USE_READLINE);
    }
    qemu_opts_del(opts);
    return chr;
}

Chardev *qemu_chr_new(const char *label, const char *filename)
{
    Chardev *chr;
    chr = qemu_chr_new_noreplay(label, filename);
    if (chr) {
        if (replay_mode != REPLAY_MODE_NONE) {
            qemu_chr_set_feature(chr, QEMU_CHAR_FEATURE_REPLAY);
        }
        if (qemu_chr_replay(chr) && CHARDEV_GET_CLASS(chr)->chr_ioctl) {
            error_report("Replay: ioctl is not supported "
                         "for serial devices yet");
        }
        replay_register_char_driver(chr);
    }
    return chr;
}

void qemu_chr_fe_set_echo(CharBackend *be, bool echo)
{
    Chardev *chr = be->chr;

    if (chr && CHARDEV_GET_CLASS(chr)->chr_set_echo) {
        CHARDEV_GET_CLASS(chr)->chr_set_echo(chr, echo);
    }
}

void qemu_chr_fe_set_open(CharBackend *be, int fe_open)
{
    Chardev *chr = be->chr;

    if (!chr) {
        return;
    }

    if (be->fe_open == fe_open) {
        return;
    }
    be->fe_open = fe_open;
    if (CHARDEV_GET_CLASS(chr)->chr_set_fe_open) {
        CHARDEV_GET_CLASS(chr)->chr_set_fe_open(chr, fe_open);
    }
}

guint qemu_chr_fe_add_watch(CharBackend *be, GIOCondition cond,
                            GIOFunc func, void *user_data)
{
    Chardev *s = be->chr;
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
    tag = g_source_attach(src, NULL);
    g_source_unref(src);

    return tag;
}

void qemu_chr_fe_disconnect(CharBackend *be)
{
    Chardev *chr = be->chr;

    if (chr && CHARDEV_GET_CLASS(chr)->chr_disconnect) {
        CHARDEV_GET_CLASS(chr)->chr_disconnect(chr);
    }
}

void qemu_chr_delete(Chardev *chr)
{
    QTAILQ_REMOVE(&chardevs, chr, next);
    object_unref(OBJECT(chr));
}

ChardevInfoList *qmp_query_chardev(Error **errp)
{
    ChardevInfoList *chr_list = NULL;
    Chardev *chr;

    QTAILQ_FOREACH(chr, &chardevs, next) {
        ChardevInfoList *info = g_malloc0(sizeof(*info));
        info->value = g_malloc0(sizeof(*info->value));
        info->value->label = g_strdup(chr->label);
        info->value->filename = g_strdup(chr->filename);
        info->value->frontend_open = chr->be && chr->be->fe_open;

        info->next = chr_list;
        chr_list = info;
    }

    return chr_list;
}

static void
qmp_prepend_backend(const char *name, void *opaque)
{
    ChardevBackendInfoList **list = opaque;
    ChardevBackendInfoList *info = g_malloc0(sizeof(*info));

    info->value = g_malloc0(sizeof(*info->value));
    info->value->name = g_strdup(name);
    info->next = *list;
    *list = info;
}

ChardevBackendInfoList *qmp_query_chardev_backends(Error **errp)
{
    ChardevBackendInfoList *backend_list = NULL;

    chardev_name_foreach(qmp_prepend_backend, &backend_list);

    return backend_list;
}

Chardev *qemu_chr_find(const char *name)
{
    Chardev *chr;

    QTAILQ_FOREACH(chr, &chardevs, next) {
        if (strcmp(chr->label, name) != 0)
            continue;
        return chr;
    }
    return NULL;
}

QemuOptsList qemu_chardev_opts = {
    .name = "chardev",
    .implied_opt_name = "backend",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_chardev_opts.head),
    .desc = {
        {
            .name = "backend",
            .type = QEMU_OPT_STRING,
        },{
            .name = "path",
            .type = QEMU_OPT_STRING,
        },{
            .name = "host",
            .type = QEMU_OPT_STRING,
        },{
            .name = "port",
            .type = QEMU_OPT_STRING,
        },{
            .name = "localaddr",
            .type = QEMU_OPT_STRING,
        },{
            .name = "localport",
            .type = QEMU_OPT_STRING,
        },{
            .name = "to",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "ipv4",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "ipv6",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "wait",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "server",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "delay",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "reconnect",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "telnet",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "tls-creds",
            .type = QEMU_OPT_STRING,
        },{
            .name = "width",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "height",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "cols",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "rows",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "mux",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "signal",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "name",
            .type = QEMU_OPT_STRING,
        },{
            .name = "debug",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "size",
            .type = QEMU_OPT_SIZE,
        },{
            .name = "chardev",
            .type = QEMU_OPT_STRING,
        },{
            .name = "append",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "logfile",
            .type = QEMU_OPT_STRING,
        },{
            .name = "logappend",
            .type = QEMU_OPT_BOOL,
        },
        { /* end of list */ }
    },
};

#ifdef _WIN32

static void qmp_chardev_open_serial(Chardev *chr,
                                    ChardevBackend *backend,
                                    bool *be_opened,
                                    Error **errp)
{
    ChardevHostdev *serial = backend->u.serial.data;

    win_chr_init(chr, serial->device, errp);
}

#else /* WIN32 */

#ifdef HAVE_CHARDEV_SERIAL
static void qmp_chardev_open_serial(Chardev *chr,
                                    ChardevBackend *backend,
                                    bool *be_opened,
                                    Error **errp)
{
    ChardevHostdev *serial = backend->u.serial.data;
    int fd;

    fd = qmp_chardev_open_file_source(serial->device, O_RDWR, errp);
    if (fd < 0) {
        return;
    }
    qemu_set_nonblock(fd);
    tty_serial_init(fd, 115200, 'N', 8, 1);

    qemu_chr_open_fd(chr, fd, fd);
}
#endif

#ifdef HAVE_CHARDEV_PARPORT
static void qmp_chardev_open_parallel(Chardev *chr,
                                      ChardevBackend *backend,
                                      bool *be_opened,
                                      Error **errp)
{
    ChardevHostdev *parallel = backend->u.parallel.data;
    int fd;

    fd = qmp_chardev_open_file_source(parallel->device, O_RDWR, errp);
    if (fd < 0) {
        return;
    }
    qemu_chr_open_pp_fd(chr, fd, be_opened, errp);
}

static void char_parallel_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_parallel;
    cc->open = qmp_chardev_open_parallel;
#if defined(__linux__)
    cc->chr_ioctl = pp_ioctl;
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__)
    cc->chr_ioctl = pp_ioctl;
#endif
}

static void char_parallel_finalize(Object *obj)
{
#if defined(__linux__)
    Chardev *chr = CHARDEV(obj);
    ParallelChardev *drv = PARALLEL_CHARDEV(chr);
    int fd = drv->fd;

    pp_hw_mode(drv, IEEE1284_MODE_COMPAT);
    ioctl(fd, PPRELEASE);
    close(fd);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__)
    /* FIXME: close fd? */
#endif
}

static const TypeInfo char_parallel_type_info = {
    .name = TYPE_CHARDEV_PARALLEL,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(ParallelChardev),
    .instance_finalize = char_parallel_finalize,
    .class_init = char_parallel_class_init,
};
#endif

#endif /* WIN32 */

#ifdef HAVE_CHARDEV_SERIAL

static void char_serial_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_serial;
    cc->open = qmp_chardev_open_serial;
#ifndef _WIN32
    cc->chr_ioctl = tty_serial_ioctl;
#endif
}

static const TypeInfo char_serial_type_info = {
    .name = TYPE_CHARDEV_SERIAL,
#ifdef _WIN32
    .parent = TYPE_CHARDEV_WIN,
#else
    .parent = TYPE_CHARDEV_FD,
#endif
    .class_init = char_serial_class_init,
};
#endif

bool qemu_chr_has_feature(Chardev *chr,
                          ChardevFeature feature)
{
    return test_bit(feature, chr->features);
}

void qemu_chr_set_feature(Chardev *chr,
                           ChardevFeature feature)
{
    return set_bit(feature, chr->features);
}

Chardev *qemu_chardev_new(const char *id, const char *typename,
                          ChardevBackend *backend, Error **errp)
{
    Chardev *chr = NULL;
    Error *local_err = NULL;
    bool be_opened = true;

    assert(g_str_has_prefix(typename, "chardev-"));

    chr = CHARDEV(object_new(typename));
    chr->label = g_strdup(id);

    qemu_char_open(chr, backend, &be_opened, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        object_unref(OBJECT(chr));
        return NULL;
    }

    if (!chr->filename) {
        chr->filename = g_strdup(typename + 8);
    }
    if (be_opened) {
        qemu_chr_be_event(chr, CHR_EVENT_OPENED);
    }

    return chr;
}

ChardevReturn *qmp_chardev_add(const char *id, ChardevBackend *backend,
                               Error **errp)
{
    const ChardevClass *cc;
    ChardevReturn *ret;
    Chardev *chr;

    cc = char_get_class(ChardevBackendKind_lookup[backend->type], errp);
    if (!cc) {
        return NULL;
    }

    chr = qemu_chardev_add(id, object_class_get_name(OBJECT_CLASS(cc)),
                           backend, errp);
    if (!chr) {
        return NULL;
    }

    ret = g_new0(ChardevReturn, 1);
    if (CHARDEV_IS_PTY(chr)) {
        ret->pty = g_strdup(chr->filename + 4);
        ret->has_pty = true;
    }

    return ret;
}

void qmp_chardev_remove(const char *id, Error **errp)
{
    Chardev *chr;

    chr = qemu_chr_find(id);
    if (chr == NULL) {
        error_setg(errp, "Chardev '%s' not found", id);
        return;
    }
    if (qemu_chr_is_busy(chr)) {
        error_setg(errp, "Chardev '%s' is busy", id);
        return;
    }
    if (qemu_chr_replay(chr)) {
        error_setg(errp,
            "Chardev '%s' cannot be unplugged in record/replay mode", id);
        return;
    }
    qemu_chr_delete(chr);
}

void qemu_chr_cleanup(void)
{
    Chardev *chr, *tmp;

    QTAILQ_FOREACH_SAFE(chr, &chardevs, next, tmp) {
        qemu_chr_delete(chr);
    }
}

static void register_types(void)
{
    type_register_static(&char_type_info);
#ifdef HAVE_CHARDEV_SERIAL
    type_register_static(&char_serial_type_info);
#endif
#ifdef HAVE_CHARDEV_PARPORT
    type_register_static(&char_parallel_type_info);
#endif
#ifdef HAVE_CHARDEV_PTY
    type_register_static(&char_pty_type_info);
#endif
#ifdef _WIN32
    type_register_static(&char_console_type_info);
#endif
    type_register_static(&char_pipe_type_info);

    /* this must be done after machine init, since we register FEs with muxes
     * as part of realize functions like serial_isa_realizefn when -nographic
     * is specified
     */
    qemu_add_machine_init_done_notifier(&muxes_realize_notify);
}

type_init(register_types);
