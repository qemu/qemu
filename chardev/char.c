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
#include "char-io.h"

#define TCP_MAX_FDS 16

/***********************************************************/
/* Socket address helpers */

static char *SocketAddress_to_str(const char *prefix, SocketAddress *addr,
                                  bool is_listen, bool is_telnet)
{
    switch (addr->type) {
    case SOCKET_ADDRESS_KIND_INET:
        return g_strdup_printf("%s%s:%s:%s%s", prefix,
                               is_telnet ? "telnet" : "tcp",
                               addr->u.inet.data->host,
                               addr->u.inet.data->port,
                               is_listen ? ",server" : "");
        break;
    case SOCKET_ADDRESS_KIND_UNIX:
        return g_strdup_printf("%sunix:%s%s", prefix,
                               addr->u.q_unix.data->path,
                               is_listen ? ",server" : "");
        break;
    case SOCKET_ADDRESS_KIND_FD:
        return g_strdup_printf("%sfd:%s%s", prefix, addr->u.fd.data->str,
                               is_listen ? ",server" : "");
        break;
    default:
        abort();
    }
}

static char *sockaddr_to_str(struct sockaddr_storage *ss, socklen_t ss_len,
                             struct sockaddr_storage *ps, socklen_t ps_len,
                             bool is_listen, bool is_telnet)
{
    char shost[NI_MAXHOST], sserv[NI_MAXSERV];
    char phost[NI_MAXHOST], pserv[NI_MAXSERV];
    const char *left = "", *right = "";

    switch (ss->ss_family) {
#ifndef _WIN32
    case AF_UNIX:
        return g_strdup_printf("unix:%s%s",
                               ((struct sockaddr_un *)(ss))->sun_path,
                               is_listen ? ",server" : "");
#endif
    case AF_INET6:
        left  = "[";
        right = "]";
        /* fall through */
    case AF_INET:
        getnameinfo((struct sockaddr *) ss, ss_len, shost, sizeof(shost),
                    sserv, sizeof(sserv), NI_NUMERICHOST | NI_NUMERICSERV);
        getnameinfo((struct sockaddr *) ps, ps_len, phost, sizeof(phost),
                    pserv, sizeof(pserv), NI_NUMERICHOST | NI_NUMERICSERV);
        return g_strdup_printf("%s:%s%s%s:%s%s <-> %s%s%s:%s",
                               is_telnet ? "telnet" : "tcp",
                               left, shost, right, sserv,
                               is_listen ? ",server" : "",
                               left, phost, right, pserv);

    default:
        return g_strdup_printf("unknown");
    }
}

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
typedef struct FDChardev {
    Chardev parent;
    Chardev *chr;
    QIOChannel *ioc_in, *ioc_out;
    int max_size;
} FDChardev;

#define TYPE_CHARDEV_FD "chardev-fd"
#define FD_CHARDEV(obj) OBJECT_CHECK(FDChardev, (obj), TYPE_CHARDEV_FD)

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
        chr->fd_in_tag = io_add_watch_poll(chr, s->ioc_in,
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

/* open a character device to a unix fd */
static void qemu_chr_open_fd(Chardev *chr,
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
    s->chr = chr;
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

/* init terminal so that we can grab keys */
static struct termios oldtty;
static int old_fd0_flags;
static bool stdio_in_use;
static bool stdio_allow_signal;
static bool stdio_echo_state;

static void qemu_chr_set_echo_stdio(Chardev *chr, bool echo);

static void term_exit(void)
{
    tcsetattr (0, TCSANOW, &oldtty);
    fcntl(0, F_SETFL, old_fd0_flags);
}

static void term_stdio_handler(int sig)
{
    /* restore echo after resume from suspend. */
    qemu_chr_set_echo_stdio(NULL, stdio_echo_state);
}

static void qemu_chr_set_echo_stdio(Chardev *chr, bool echo)
{
    struct termios tty;

    stdio_echo_state = echo;
    tty = oldtty;
    if (!echo) {
        tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
        tty.c_oflag |= OPOST;
        tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
        tty.c_cflag &= ~(CSIZE|PARENB);
        tty.c_cflag |= CS8;
        tty.c_cc[VMIN] = 1;
        tty.c_cc[VTIME] = 0;
    }
    if (!stdio_allow_signal)
        tty.c_lflag &= ~ISIG;

    tcsetattr (0, TCSANOW, &tty);
}

static void char_stdio_finalize(Object *obj)
{
    term_exit();
}

static void qemu_chr_open_stdio(Chardev *chr,
                                ChardevBackend *backend,
                                bool *be_opened,
                                Error **errp)
{
    ChardevStdio *opts = backend->u.stdio.data;
    struct sigaction act;

    if (is_daemonized()) {
        error_setg(errp, "cannot use stdio with -daemonize");
        return;
    }

    if (stdio_in_use) {
        error_setg(errp, "cannot use stdio by multiple character devices");
        return;
    }

    stdio_in_use = true;
    old_fd0_flags = fcntl(0, F_GETFL);
    tcgetattr(0, &oldtty);
    qemu_set_nonblock(0);
    atexit(term_exit);

    memset(&act, 0, sizeof(act));
    act.sa_handler = term_stdio_handler;
    sigaction(SIGCONT, &act, NULL);

    qemu_chr_open_fd(chr, 0, 1);

    if (opts->has_signal) {
        stdio_allow_signal = opts->signal;
    }
    qemu_chr_set_echo_stdio(chr, false);
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

typedef struct {
    Chardev parent;
    int max_size;
    HANDLE hcom, hrecv, hsend;
    OVERLAPPED orecv;
    BOOL fpipe;
    DWORD len;

    /* Protected by the Chardev chr_write_lock.  */
    OVERLAPPED osend;
    /* FIXME: file/console do not finalize */
    bool skip_free;
} WinChardev;

#define TYPE_CHARDEV_WIN "chardev-win"
#define WIN_CHARDEV(obj) OBJECT_CHECK(WinChardev, (obj), TYPE_CHARDEV_WIN)

typedef struct {
    Chardev parent;
    HANDLE  hStdIn;
    HANDLE  hInputReadyEvent;
    HANDLE  hInputDoneEvent;
    HANDLE  hInputThread;
    uint8_t win_stdio_buf;
} WinStdioChardev;

#define TYPE_CHARDEV_WIN_STDIO "chardev-win-stdio"
#define WIN_STDIO_CHARDEV(obj)                                  \
    OBJECT_CHECK(WinStdioChardev, (obj), TYPE_CHARDEV_WIN_STDIO)

#define NSENDBUF 2048
#define NRECVBUF 2048
#define MAXCONNECT 1
#define NTIMEOUT 5000

static int win_chr_poll(void *opaque);
static int win_chr_pipe_poll(void *opaque);

static void char_win_finalize(Object *obj)
{
    Chardev *chr = CHARDEV(obj);
    WinChardev *s = WIN_CHARDEV(chr);

    if (s->skip_free) {
        return;
    }

    if (s->hsend) {
        CloseHandle(s->hsend);
    }
    if (s->hrecv) {
        CloseHandle(s->hrecv);
    }
    if (s->hcom) {
        CloseHandle(s->hcom);
    }
    if (s->fpipe)
        qemu_del_polling_cb(win_chr_pipe_poll, chr);
    else
        qemu_del_polling_cb(win_chr_poll, chr);

    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

static int win_chr_init(Chardev *chr, const char *filename, Error **errp)
{
    WinChardev *s = WIN_CHARDEV(chr);
    COMMCONFIG comcfg;
    COMMTIMEOUTS cto = { 0, 0, 0, 0, 0};
    COMSTAT comstat;
    DWORD size;
    DWORD err;

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

    s->hcom = CreateFile(filename, GENERIC_READ|GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
    if (s->hcom == INVALID_HANDLE_VALUE) {
        error_setg(errp, "Failed CreateFile (%lu)", GetLastError());
        s->hcom = NULL;
        goto fail;
    }

    if (!SetupComm(s->hcom, NRECVBUF, NSENDBUF)) {
        error_setg(errp, "Failed SetupComm");
        goto fail;
    }

    ZeroMemory(&comcfg, sizeof(COMMCONFIG));
    size = sizeof(COMMCONFIG);
    GetDefaultCommConfig(filename, &comcfg, &size);
    comcfg.dcb.DCBlength = sizeof(DCB);
    CommConfigDialog(filename, NULL, &comcfg);

    if (!SetCommState(s->hcom, &comcfg.dcb)) {
        error_setg(errp, "Failed SetCommState");
        goto fail;
    }

    if (!SetCommMask(s->hcom, EV_ERR)) {
        error_setg(errp, "Failed SetCommMask");
        goto fail;
    }

    cto.ReadIntervalTimeout = MAXDWORD;
    if (!SetCommTimeouts(s->hcom, &cto)) {
        error_setg(errp, "Failed SetCommTimeouts");
        goto fail;
    }

    if (!ClearCommError(s->hcom, &err, &comstat)) {
        error_setg(errp, "Failed ClearCommError");
        goto fail;
    }
    qemu_add_polling_cb(win_chr_poll, chr);
    return 0;

 fail:
    return -1;
}

/* Called with chr_write_lock held.  */
static int win_chr_write(Chardev *chr, const uint8_t *buf, int len1)
{
    WinChardev *s = WIN_CHARDEV(chr);
    DWORD len, ret, size, err;

    len = len1;
    ZeroMemory(&s->osend, sizeof(s->osend));
    s->osend.hEvent = s->hsend;
    while (len > 0) {
        if (s->hsend)
            ret = WriteFile(s->hcom, buf, len, &size, &s->osend);
        else
            ret = WriteFile(s->hcom, buf, len, &size, NULL);
        if (!ret) {
            err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                ret = GetOverlappedResult(s->hcom, &s->osend, &size, TRUE);
                if (ret) {
                    buf += size;
                    len -= size;
                } else {
                    break;
                }
            } else {
                break;
            }
        } else {
            buf += size;
            len -= size;
        }
    }
    return len1 - len;
}

static int win_chr_read_poll(Chardev *chr)
{
    WinChardev *s = WIN_CHARDEV(chr);

    s->max_size = qemu_chr_be_can_write(chr);
    return s->max_size;
}

static void win_chr_readfile(Chardev *chr)
{
    WinChardev *s = WIN_CHARDEV(chr);

    int ret, err;
    uint8_t buf[CHR_READ_BUF_LEN];
    DWORD size;

    ZeroMemory(&s->orecv, sizeof(s->orecv));
    s->orecv.hEvent = s->hrecv;
    ret = ReadFile(s->hcom, buf, s->len, &size, &s->orecv);
    if (!ret) {
        err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            ret = GetOverlappedResult(s->hcom, &s->orecv, &size, TRUE);
        }
    }

    if (size > 0) {
        qemu_chr_be_write(chr, buf, size);
    }
}

static void win_chr_read(Chardev *chr)
{
    WinChardev *s = WIN_CHARDEV(chr);

    if (s->len > s->max_size)
        s->len = s->max_size;
    if (s->len == 0)
        return;

    win_chr_readfile(chr);
}

static int win_chr_poll(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    WinChardev *s = WIN_CHARDEV(opaque);
    COMSTAT status;
    DWORD comerr;

    ClearCommError(s->hcom, &comerr, &status);
    if (status.cbInQue > 0) {
        s->len = status.cbInQue;
        win_chr_read_poll(chr);
        win_chr_read(chr);
        return 1;
    }
    return 0;
}

static int win_chr_pipe_poll(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    WinChardev *s = WIN_CHARDEV(opaque);
    DWORD size;

    PeekNamedPipe(s->hcom, NULL, 0, NULL, &size, NULL);
    if (size > 0) {
        s->len = size;
        win_chr_read_poll(chr);
        win_chr_read(chr);
        return 1;
    }
    return 0;
}

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

static void qemu_chr_open_win_file(Chardev *chr, HANDLE fd_out)
{
    WinChardev *s = WIN_CHARDEV(chr);

    s->skip_free = true;
    s->hcom = fd_out;
}

static void char_win_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->chr_write = win_chr_write;
}

static const TypeInfo char_win_type_info = {
    .name = TYPE_CHARDEV_WIN,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(WinChardev),
    .instance_finalize = char_win_finalize,
    .class_init = char_win_class_init,
    .abstract = true,
};

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

static int win_stdio_write(Chardev *chr, const uint8_t *buf, int len)
{
    HANDLE  hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD   dwSize;
    int     len1;

    len1 = len;

    while (len1 > 0) {
        if (!WriteFile(hStdOut, buf, len1, &dwSize, NULL)) {
            break;
        }
        buf  += dwSize;
        len1 -= dwSize;
    }

    return len - len1;
}

static void win_stdio_wait_func(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    WinStdioChardev *stdio = WIN_STDIO_CHARDEV(opaque);
    INPUT_RECORD       buf[4];
    int                ret;
    DWORD              dwSize;
    int                i;

    ret = ReadConsoleInput(stdio->hStdIn, buf, ARRAY_SIZE(buf), &dwSize);

    if (!ret) {
        /* Avoid error storm */
        qemu_del_wait_object(stdio->hStdIn, NULL, NULL);
        return;
    }

    for (i = 0; i < dwSize; i++) {
        KEY_EVENT_RECORD *kev = &buf[i].Event.KeyEvent;

        if (buf[i].EventType == KEY_EVENT && kev->bKeyDown) {
            int j;
            if (kev->uChar.AsciiChar != 0) {
                for (j = 0; j < kev->wRepeatCount; j++) {
                    if (qemu_chr_be_can_write(chr)) {
                        uint8_t c = kev->uChar.AsciiChar;
                        qemu_chr_be_write(chr, &c, 1);
                    }
                }
            }
        }
    }
}

static DWORD WINAPI win_stdio_thread(LPVOID param)
{
    WinStdioChardev *stdio = WIN_STDIO_CHARDEV(param);
    int                ret;
    DWORD              dwSize;

    while (1) {

        /* Wait for one byte */
        ret = ReadFile(stdio->hStdIn, &stdio->win_stdio_buf, 1, &dwSize, NULL);

        /* Exit in case of error, continue if nothing read */
        if (!ret) {
            break;
        }
        if (!dwSize) {
            continue;
        }

        /* Some terminal emulator returns \r\n for Enter, just pass \n */
        if (stdio->win_stdio_buf == '\r') {
            continue;
        }

        /* Signal the main thread and wait until the byte was eaten */
        if (!SetEvent(stdio->hInputReadyEvent)) {
            break;
        }
        if (WaitForSingleObject(stdio->hInputDoneEvent, INFINITE)
            != WAIT_OBJECT_0) {
            break;
        }
    }

    qemu_del_wait_object(stdio->hInputReadyEvent, NULL, NULL);
    return 0;
}

static void win_stdio_thread_wait_func(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    WinStdioChardev *stdio = WIN_STDIO_CHARDEV(opaque);

    if (qemu_chr_be_can_write(chr)) {
        qemu_chr_be_write(chr, &stdio->win_stdio_buf, 1);
    }

    SetEvent(stdio->hInputDoneEvent);
}

static void qemu_chr_set_echo_win_stdio(Chardev *chr, bool echo)
{
    WinStdioChardev *stdio = WIN_STDIO_CHARDEV(chr);
    DWORD              dwMode = 0;

    GetConsoleMode(stdio->hStdIn, &dwMode);

    if (echo) {
        SetConsoleMode(stdio->hStdIn, dwMode | ENABLE_ECHO_INPUT);
    } else {
        SetConsoleMode(stdio->hStdIn, dwMode & ~ENABLE_ECHO_INPUT);
    }
}

static void char_win_stdio_finalize(Object *obj)
{
    WinStdioChardev *stdio = WIN_STDIO_CHARDEV(obj);

    if (stdio->hInputReadyEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(stdio->hInputReadyEvent);
    }
    if (stdio->hInputDoneEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(stdio->hInputDoneEvent);
    }
    if (stdio->hInputThread != INVALID_HANDLE_VALUE) {
        TerminateThread(stdio->hInputThread, 0);
    }
}

static const TypeInfo char_win_stdio_type_info = {
    .name = TYPE_CHARDEV_WIN_STDIO,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(WinStdioChardev),
    .instance_finalize = char_win_stdio_finalize,
    .abstract = true,
};

static void qemu_chr_open_stdio(Chardev *chr,
                                ChardevBackend *backend,
                                bool *be_opened,
                                Error **errp)
{
    WinStdioChardev *stdio = WIN_STDIO_CHARDEV(chr);
    DWORD              dwMode;
    int                is_console = 0;

    stdio->hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    if (stdio->hStdIn == INVALID_HANDLE_VALUE) {
        error_setg(errp, "cannot open stdio: invalid handle");
        return;
    }

    is_console = GetConsoleMode(stdio->hStdIn, &dwMode) != 0;

    if (is_console) {
        if (qemu_add_wait_object(stdio->hStdIn,
                                 win_stdio_wait_func, chr)) {
            error_setg(errp, "qemu_add_wait_object: failed");
            goto err1;
        }
    } else {
        DWORD   dwId;
            
        stdio->hInputReadyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        stdio->hInputDoneEvent  = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (stdio->hInputReadyEvent == INVALID_HANDLE_VALUE
            || stdio->hInputDoneEvent == INVALID_HANDLE_VALUE) {
            error_setg(errp, "cannot create event");
            goto err2;
        }
        if (qemu_add_wait_object(stdio->hInputReadyEvent,
                                 win_stdio_thread_wait_func, chr)) {
            error_setg(errp, "qemu_add_wait_object: failed");
            goto err2;
        }
        stdio->hInputThread     = CreateThread(NULL, 0, win_stdio_thread,
                                               chr, 0, &dwId);

        if (stdio->hInputThread == INVALID_HANDLE_VALUE) {
            error_setg(errp, "cannot create stdio thread");
            goto err3;
        }
    }

    dwMode |= ENABLE_LINE_INPUT;

    if (is_console) {
        /* set the terminal in raw mode */
        /* ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS */
        dwMode |= ENABLE_PROCESSED_INPUT;
    }

    SetConsoleMode(stdio->hStdIn, dwMode);

    qemu_chr_set_echo_win_stdio(chr, false);

    return;

err3:
    qemu_del_wait_object(stdio->hInputReadyEvent, NULL, NULL);
err2:
    CloseHandle(stdio->hInputReadyEvent);
    CloseHandle(stdio->hInputDoneEvent);
err1:
    qemu_del_wait_object(stdio->hStdIn, NULL, NULL);
}
#endif /* !_WIN32 */

/***********************************************************/
/* UDP Net console */

typedef struct {
    Chardev parent;
    QIOChannel *ioc;
    uint8_t buf[CHR_READ_BUF_LEN];
    int bufcnt;
    int bufptr;
    int max_size;
} UdpChardev;

#define UDP_CHARDEV(obj) OBJECT_CHECK(UdpChardev, (obj), TYPE_CHARDEV_UDP)

/* Called with chr_write_lock held.  */
static int udp_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    UdpChardev *s = UDP_CHARDEV(chr);

    return qio_channel_write(
        s->ioc, (const char *)buf, len, NULL);
}

static int udp_chr_read_poll(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    UdpChardev *s = UDP_CHARDEV(opaque);

    s->max_size = qemu_chr_be_can_write(chr);

    /* If there were any stray characters in the queue process them
     * first
     */
    while (s->max_size > 0 && s->bufptr < s->bufcnt) {
        qemu_chr_be_write(chr, &s->buf[s->bufptr], 1);
        s->bufptr++;
        s->max_size = qemu_chr_be_can_write(chr);
    }
    return s->max_size;
}

static gboolean udp_chr_read(QIOChannel *chan, GIOCondition cond, void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    UdpChardev *s = UDP_CHARDEV(opaque);
    ssize_t ret;

    if (s->max_size == 0) {
        return TRUE;
    }
    ret = qio_channel_read(
        s->ioc, (char *)s->buf, sizeof(s->buf), NULL);
    if (ret <= 0) {
        remove_fd_in_watch(chr);
        return FALSE;
    }
    s->bufcnt = ret;

    s->bufptr = 0;
    while (s->max_size > 0 && s->bufptr < s->bufcnt) {
        qemu_chr_be_write(chr, &s->buf[s->bufptr], 1);
        s->bufptr++;
        s->max_size = qemu_chr_be_can_write(chr);
    }

    return TRUE;
}

static void udp_chr_update_read_handler(Chardev *chr,
                                        GMainContext *context)
{
    UdpChardev *s = UDP_CHARDEV(chr);

    remove_fd_in_watch(chr);
    if (s->ioc) {
        chr->fd_in_tag = io_add_watch_poll(chr, s->ioc,
                                           udp_chr_read_poll,
                                           udp_chr_read, chr,
                                           context);
    }
}

static void char_udp_finalize(Object *obj)
{
    Chardev *chr = CHARDEV(obj);
    UdpChardev *s = UDP_CHARDEV(obj);

    remove_fd_in_watch(chr);
    if (s->ioc) {
        object_unref(OBJECT(s->ioc));
    }
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

/***********************************************************/
/* TCP Net console */

typedef struct {
    Chardev parent;
    QIOChannel *ioc; /* Client I/O channel */
    QIOChannelSocket *sioc; /* Client master channel */
    QIOChannelSocket *listen_ioc;
    guint listen_tag;
    QCryptoTLSCreds *tls_creds;
    int connected;
    int max_size;
    int do_telnetopt;
    int do_nodelay;
    int is_unix;
    int *read_msgfds;
    size_t read_msgfds_num;
    int *write_msgfds;
    size_t write_msgfds_num;

    SocketAddress *addr;
    bool is_listen;
    bool is_telnet;

    guint reconnect_timer;
    int64_t reconnect_time;
    bool connect_err_reported;
} SocketChardev;

#define SOCKET_CHARDEV(obj)                                     \
    OBJECT_CHECK(SocketChardev, (obj), TYPE_CHARDEV_SOCKET)

static gboolean socket_reconnect_timeout(gpointer opaque);

static void qemu_chr_socket_restart_timer(Chardev *chr)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    char *name;

    assert(s->connected == 0);
    s->reconnect_timer = g_timeout_add_seconds(s->reconnect_time,
                                               socket_reconnect_timeout, chr);
    name = g_strdup_printf("chardev-socket-reconnect-%s", chr->label);
    g_source_set_name_by_id(s->reconnect_timer, name);
    g_free(name);
}

static void check_report_connect_error(Chardev *chr,
                                       Error *err)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);

    if (!s->connect_err_reported) {
        error_report("Unable to connect character device %s: %s",
                     chr->label, error_get_pretty(err));
        s->connect_err_reported = true;
    }
    qemu_chr_socket_restart_timer(chr);
}

static gboolean tcp_chr_accept(QIOChannel *chan,
                               GIOCondition cond,
                               void *opaque);

/* Called with chr_write_lock held.  */
static int tcp_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);

    if (s->connected) {
        int ret =  io_channel_send_full(s->ioc, buf, len,
                                        s->write_msgfds,
                                        s->write_msgfds_num);

        /* free the written msgfds, no matter what */
        if (s->write_msgfds_num) {
            g_free(s->write_msgfds);
            s->write_msgfds = 0;
            s->write_msgfds_num = 0;
        }

        return ret;
    } else {
        /* XXX: indicate an error ? */
        return len;
    }
}

static int tcp_chr_read_poll(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    SocketChardev *s = SOCKET_CHARDEV(opaque);
    if (!s->connected)
        return 0;
    s->max_size = qemu_chr_be_can_write(chr);
    return s->max_size;
}

#define IAC 255
#define IAC_BREAK 243
static void tcp_chr_process_IAC_bytes(Chardev *chr,
                                      SocketChardev *s,
                                      uint8_t *buf, int *size)
{
    /* Handle any telnet client's basic IAC options to satisfy char by
     * char mode with no echo.  All IAC options will be removed from
     * the buf and the do_telnetopt variable will be used to track the
     * state of the width of the IAC information.
     *
     * IAC commands come in sets of 3 bytes with the exception of the
     * "IAC BREAK" command and the double IAC.
     */

    int i;
    int j = 0;

    for (i = 0; i < *size; i++) {
        if (s->do_telnetopt > 1) {
            if ((unsigned char)buf[i] == IAC && s->do_telnetopt == 2) {
                /* Double IAC means send an IAC */
                if (j != i)
                    buf[j] = buf[i];
                j++;
                s->do_telnetopt = 1;
            } else {
                if ((unsigned char)buf[i] == IAC_BREAK && s->do_telnetopt == 2) {
                    /* Handle IAC break commands by sending a serial break */
                    qemu_chr_be_event(chr, CHR_EVENT_BREAK);
                    s->do_telnetopt++;
                }
                s->do_telnetopt++;
            }
            if (s->do_telnetopt >= 4) {
                s->do_telnetopt = 1;
            }
        } else {
            if ((unsigned char)buf[i] == IAC) {
                s->do_telnetopt = 2;
            } else {
                if (j != i)
                    buf[j] = buf[i];
                j++;
            }
        }
    }
    *size = j;
}

static int tcp_get_msgfds(Chardev *chr, int *fds, int num)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);

    int to_copy = (s->read_msgfds_num < num) ? s->read_msgfds_num : num;

    assert(num <= TCP_MAX_FDS);

    if (to_copy) {
        int i;

        memcpy(fds, s->read_msgfds, to_copy * sizeof(int));

        /* Close unused fds */
        for (i = to_copy; i < s->read_msgfds_num; i++) {
            close(s->read_msgfds[i]);
        }

        g_free(s->read_msgfds);
        s->read_msgfds = 0;
        s->read_msgfds_num = 0;
    }

    return to_copy;
}

static int tcp_set_msgfds(Chardev *chr, int *fds, int num)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);

    /* clear old pending fd array */
    g_free(s->write_msgfds);
    s->write_msgfds = NULL;
    s->write_msgfds_num = 0;

    if (!s->connected ||
        !qio_channel_has_feature(s->ioc,
                                 QIO_CHANNEL_FEATURE_FD_PASS)) {
        return -1;
    }

    if (num) {
        s->write_msgfds = g_new(int, num);
        memcpy(s->write_msgfds, fds, num * sizeof(int));
    }

    s->write_msgfds_num = num;

    return 0;
}

static ssize_t tcp_chr_recv(Chardev *chr, char *buf, size_t len)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    struct iovec iov = { .iov_base = buf, .iov_len = len };
    int ret;
    size_t i;
    int *msgfds = NULL;
    size_t msgfds_num = 0;

    if (qio_channel_has_feature(s->ioc, QIO_CHANNEL_FEATURE_FD_PASS)) {
        ret = qio_channel_readv_full(s->ioc, &iov, 1,
                                     &msgfds, &msgfds_num,
                                     NULL);
    } else {
        ret = qio_channel_readv_full(s->ioc, &iov, 1,
                                     NULL, NULL,
                                     NULL);
    }

    if (ret == QIO_CHANNEL_ERR_BLOCK) {
        errno = EAGAIN;
        ret = -1;
    } else if (ret == -1) {
        errno = EIO;
    }

    if (msgfds_num) {
        /* close and clean read_msgfds */
        for (i = 0; i < s->read_msgfds_num; i++) {
            close(s->read_msgfds[i]);
        }

        if (s->read_msgfds_num) {
            g_free(s->read_msgfds);
        }

        s->read_msgfds = msgfds;
        s->read_msgfds_num = msgfds_num;
    }

    for (i = 0; i < s->read_msgfds_num; i++) {
        int fd = s->read_msgfds[i];
        if (fd < 0) {
            continue;
        }

        /* O_NONBLOCK is preserved across SCM_RIGHTS so reset it */
        qemu_set_block(fd);

#ifndef MSG_CMSG_CLOEXEC
        qemu_set_cloexec(fd);
#endif
    }

    return ret;
}

static GSource *tcp_chr_add_watch(Chardev *chr, GIOCondition cond)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    return qio_channel_create_watch(s->ioc, cond);
}

static void tcp_chr_free_connection(Chardev *chr)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    int i;

    if (!s->connected) {
        return;
    }

    if (s->read_msgfds_num) {
        for (i = 0; i < s->read_msgfds_num; i++) {
            close(s->read_msgfds[i]);
        }
        g_free(s->read_msgfds);
        s->read_msgfds = NULL;
        s->read_msgfds_num = 0;
    }

    tcp_set_msgfds(chr, NULL, 0);
    remove_fd_in_watch(chr);
    object_unref(OBJECT(s->sioc));
    s->sioc = NULL;
    object_unref(OBJECT(s->ioc));
    s->ioc = NULL;
    g_free(chr->filename);
    chr->filename = NULL;
    s->connected = 0;
}

static void tcp_chr_disconnect(Chardev *chr)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);

    if (!s->connected) {
        return;
    }

    tcp_chr_free_connection(chr);

    if (s->listen_ioc) {
        s->listen_tag = qio_channel_add_watch(
            QIO_CHANNEL(s->listen_ioc), G_IO_IN, tcp_chr_accept, chr, NULL);
    }
    chr->filename = SocketAddress_to_str("disconnected:", s->addr,
                                         s->is_listen, s->is_telnet);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
    if (s->reconnect_time) {
        qemu_chr_socket_restart_timer(chr);
    }
}

static gboolean tcp_chr_read(QIOChannel *chan, GIOCondition cond, void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    SocketChardev *s = SOCKET_CHARDEV(opaque);
    uint8_t buf[CHR_READ_BUF_LEN];
    int len, size;

    if (!s->connected || s->max_size <= 0) {
        return TRUE;
    }
    len = sizeof(buf);
    if (len > s->max_size)
        len = s->max_size;
    size = tcp_chr_recv(chr, (void *)buf, len);
    if (size == 0 || size == -1) {
        /* connection closed */
        tcp_chr_disconnect(chr);
    } else if (size > 0) {
        if (s->do_telnetopt)
            tcp_chr_process_IAC_bytes(chr, s, buf, &size);
        if (size > 0)
            qemu_chr_be_write(chr, buf, size);
    }

    return TRUE;
}

static int tcp_chr_sync_read(Chardev *chr, const uint8_t *buf, int len)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    int size;

    if (!s->connected) {
        return 0;
    }

    size = tcp_chr_recv(chr, (void *) buf, len);
    if (size == 0) {
        /* connection closed */
        tcp_chr_disconnect(chr);
    }

    return size;
}

static void tcp_chr_connect(void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    SocketChardev *s = SOCKET_CHARDEV(opaque);

    g_free(chr->filename);
    chr->filename = sockaddr_to_str(
        &s->sioc->localAddr, s->sioc->localAddrLen,
        &s->sioc->remoteAddr, s->sioc->remoteAddrLen,
        s->is_listen, s->is_telnet);

    s->connected = 1;
    if (s->ioc) {
        chr->fd_in_tag = io_add_watch_poll(chr, s->ioc,
                                           tcp_chr_read_poll,
                                           tcp_chr_read,
                                           chr, NULL);
    }
    qemu_chr_be_generic_open(chr);
}

static void tcp_chr_update_read_handler(Chardev *chr,
                                        GMainContext *context)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);

    if (!s->connected) {
        return;
    }

    remove_fd_in_watch(chr);
    if (s->ioc) {
        chr->fd_in_tag = io_add_watch_poll(chr, s->ioc,
                                           tcp_chr_read_poll,
                                           tcp_chr_read, chr,
                                           context);
    }
}

typedef struct {
    Chardev *chr;
    char buf[12];
    size_t buflen;
} TCPChardevTelnetInit;

static gboolean tcp_chr_telnet_init_io(QIOChannel *ioc,
                                       GIOCondition cond G_GNUC_UNUSED,
                                       gpointer user_data)
{
    TCPChardevTelnetInit *init = user_data;
    ssize_t ret;

    ret = qio_channel_write(ioc, init->buf, init->buflen, NULL);
    if (ret < 0) {
        if (ret == QIO_CHANNEL_ERR_BLOCK) {
            ret = 0;
        } else {
            tcp_chr_disconnect(init->chr);
            return FALSE;
        }
    }
    init->buflen -= ret;

    if (init->buflen == 0) {
        tcp_chr_connect(init->chr);
        return FALSE;
    }

    memmove(init->buf, init->buf + ret, init->buflen);

    return TRUE;
}

static void tcp_chr_telnet_init(Chardev *chr)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    TCPChardevTelnetInit *init = g_new0(TCPChardevTelnetInit, 1);
    size_t n = 0;

    init->chr = chr;
    init->buflen = 12;

#define IACSET(x, a, b, c)                      \
    do {                                        \
        x[n++] = a;                             \
        x[n++] = b;                             \
        x[n++] = c;                             \
    } while (0)

    /* Prep the telnet negotion to put telnet in binary,
     * no echo, single char mode */
    IACSET(init->buf, 0xff, 0xfb, 0x01);  /* IAC WILL ECHO */
    IACSET(init->buf, 0xff, 0xfb, 0x03);  /* IAC WILL Suppress go ahead */
    IACSET(init->buf, 0xff, 0xfb, 0x00);  /* IAC WILL Binary */
    IACSET(init->buf, 0xff, 0xfd, 0x00);  /* IAC DO Binary */

#undef IACSET

    qio_channel_add_watch(
        s->ioc, G_IO_OUT,
        tcp_chr_telnet_init_io,
        init, NULL);
}


static void tcp_chr_tls_handshake(QIOTask *task,
                                  gpointer user_data)
{
    Chardev *chr = user_data;
    SocketChardev *s = user_data;

    if (qio_task_propagate_error(task, NULL)) {
        tcp_chr_disconnect(chr);
    } else {
        if (s->do_telnetopt) {
            tcp_chr_telnet_init(chr);
        } else {
            tcp_chr_connect(chr);
        }
    }
}


static void tcp_chr_tls_init(Chardev *chr)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    QIOChannelTLS *tioc;
    Error *err = NULL;
    gchar *name;

    if (s->is_listen) {
        tioc = qio_channel_tls_new_server(
            s->ioc, s->tls_creds,
            NULL, /* XXX Use an ACL */
            &err);
    } else {
        tioc = qio_channel_tls_new_client(
            s->ioc, s->tls_creds,
            s->addr->u.inet.data->host,
            &err);
    }
    if (tioc == NULL) {
        error_free(err);
        tcp_chr_disconnect(chr);
        return;
    }
    name = g_strdup_printf("chardev-tls-%s-%s",
                           s->is_listen ? "server" : "client",
                           chr->label);
    qio_channel_set_name(QIO_CHANNEL(tioc), name);
    g_free(name);
    object_unref(OBJECT(s->ioc));
    s->ioc = QIO_CHANNEL(tioc);

    qio_channel_tls_handshake(tioc,
                              tcp_chr_tls_handshake,
                              chr,
                              NULL);
}


static void tcp_chr_set_client_ioc_name(Chardev *chr,
                                        QIOChannelSocket *sioc)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    char *name;
    name = g_strdup_printf("chardev-tcp-%s-%s",
                           s->is_listen ? "server" : "client",
                           chr->label);
    qio_channel_set_name(QIO_CHANNEL(sioc), name);
    g_free(name);

}

static int tcp_chr_new_client(Chardev *chr, QIOChannelSocket *sioc)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);

    if (s->ioc != NULL) {
	return -1;
    }

    s->ioc = QIO_CHANNEL(sioc);
    object_ref(OBJECT(sioc));
    s->sioc = sioc;
    object_ref(OBJECT(sioc));

    qio_channel_set_blocking(s->ioc, false, NULL);

    if (s->do_nodelay) {
        qio_channel_set_delay(s->ioc, false);
    }
    if (s->listen_tag) {
        g_source_remove(s->listen_tag);
        s->listen_tag = 0;
    }

    if (s->tls_creds) {
        tcp_chr_tls_init(chr);
    } else {
        if (s->do_telnetopt) {
            tcp_chr_telnet_init(chr);
        } else {
            tcp_chr_connect(chr);
        }
    }

    return 0;
}


static int tcp_chr_add_client(Chardev *chr, int fd)
{
    int ret;
    QIOChannelSocket *sioc;

    sioc = qio_channel_socket_new_fd(fd, NULL);
    if (!sioc) {
        return -1;
    }
    tcp_chr_set_client_ioc_name(chr, sioc);
    ret = tcp_chr_new_client(chr, sioc);
    object_unref(OBJECT(sioc));
    return ret;
}

static gboolean tcp_chr_accept(QIOChannel *channel,
                               GIOCondition cond,
                               void *opaque)
{
    Chardev *chr = CHARDEV(opaque);
    QIOChannelSocket *sioc;

    sioc = qio_channel_socket_accept(QIO_CHANNEL_SOCKET(channel),
                                     NULL);
    if (!sioc) {
        return TRUE;
    }

    tcp_chr_new_client(chr, sioc);

    object_unref(OBJECT(sioc));

    return TRUE;
}

static int tcp_chr_wait_connected(Chardev *chr, Error **errp)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    QIOChannelSocket *sioc;

    /* It can't wait on s->connected, since it is set asynchronously
     * in TLS and telnet cases, only wait for an accepted socket */
    while (!s->ioc) {
        if (s->is_listen) {
            error_report("QEMU waiting for connection on: %s",
                         chr->filename);
            qio_channel_set_blocking(QIO_CHANNEL(s->listen_ioc), true, NULL);
            tcp_chr_accept(QIO_CHANNEL(s->listen_ioc), G_IO_IN, chr);
            qio_channel_set_blocking(QIO_CHANNEL(s->listen_ioc), false, NULL);
        } else {
            sioc = qio_channel_socket_new();
            tcp_chr_set_client_ioc_name(chr, sioc);
            if (qio_channel_socket_connect_sync(sioc, s->addr, errp) < 0) {
                object_unref(OBJECT(sioc));
                return -1;
            }
            tcp_chr_new_client(chr, sioc);
            object_unref(OBJECT(sioc));
        }
    }

    return 0;
}

static int qemu_chr_wait_connected(Chardev *chr, Error **errp)
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

static void char_socket_finalize(Object *obj)
{
    Chardev *chr = CHARDEV(obj);
    SocketChardev *s = SOCKET_CHARDEV(obj);

    tcp_chr_free_connection(chr);

    if (s->reconnect_timer) {
        g_source_remove(s->reconnect_timer);
        s->reconnect_timer = 0;
    }
    qapi_free_SocketAddress(s->addr);
    if (s->listen_tag) {
        g_source_remove(s->listen_tag);
        s->listen_tag = 0;
    }
    if (s->listen_ioc) {
        object_unref(OBJECT(s->listen_ioc));
    }
    if (s->tls_creds) {
        object_unref(OBJECT(s->tls_creds));
    }

    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}


static void qemu_chr_socket_connected(QIOTask *task, void *opaque)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(qio_task_get_source(task));
    Chardev *chr = CHARDEV(opaque);
    SocketChardev *s = SOCKET_CHARDEV(chr);
    Error *err = NULL;

    if (qio_task_propagate_error(task, &err)) {
        check_report_connect_error(chr, err);
        error_free(err);
        goto cleanup;
    }

    s->connect_err_reported = false;
    tcp_chr_new_client(chr, sioc);

 cleanup:
    object_unref(OBJECT(sioc));
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


static void qemu_chr_parse_file_out(QemuOpts *opts, ChardevBackend *backend,
                                    Error **errp)
{
    const char *path = qemu_opt_get(opts, "path");
    ChardevFile *file;

    backend->type = CHARDEV_BACKEND_KIND_FILE;
    if (path == NULL) {
        error_setg(errp, "chardev: file: no filename given");
        return;
    }
    file = backend->u.file.data = g_new0(ChardevFile, 1);
    qemu_chr_parse_common(opts, qapi_ChardevFile_base(file));
    file->out = g_strdup(path);

    file->has_append = true;
    file->append = qemu_opt_get_bool(opts, "append", false);
}

static void qemu_chr_parse_stdio(QemuOpts *opts, ChardevBackend *backend,
                                 Error **errp)
{
    ChardevStdio *stdio;

    backend->type = CHARDEV_BACKEND_KIND_STDIO;
    stdio = backend->u.stdio.data = g_new0(ChardevStdio, 1);
    qemu_chr_parse_common(opts, qapi_ChardevStdio_base(stdio));
    stdio->has_signal = true;
    stdio->signal = qemu_opt_get_bool(opts, "signal", true);
}

static void char_stdio_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_stdio;
    cc->open = qemu_chr_open_stdio;
#ifdef _WIN32
    cc->chr_write = win_stdio_write;
    cc->chr_set_echo = qemu_chr_set_echo_win_stdio;
#else
    cc->chr_set_echo = qemu_chr_set_echo_stdio;
#endif
}

static const TypeInfo char_stdio_type_info = {
    .name = TYPE_CHARDEV_STDIO,
#ifdef _WIN32
    .parent = TYPE_CHARDEV_WIN_STDIO,
#else
    .parent = TYPE_CHARDEV_FD,
    .instance_finalize = char_stdio_finalize,
#endif
    .class_init = char_stdio_class_init,
};

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

static void qemu_chr_parse_socket(QemuOpts *opts, ChardevBackend *backend,
                                  Error **errp)
{
    bool is_listen      = qemu_opt_get_bool(opts, "server", false);
    bool is_waitconnect = is_listen && qemu_opt_get_bool(opts, "wait", true);
    bool is_telnet      = qemu_opt_get_bool(opts, "telnet", false);
    bool do_nodelay     = !qemu_opt_get_bool(opts, "delay", true);
    int64_t reconnect   = qemu_opt_get_number(opts, "reconnect", 0);
    const char *path = qemu_opt_get(opts, "path");
    const char *host = qemu_opt_get(opts, "host");
    const char *port = qemu_opt_get(opts, "port");
    const char *tls_creds = qemu_opt_get(opts, "tls-creds");
    SocketAddress *addr;
    ChardevSocket *sock;

    backend->type = CHARDEV_BACKEND_KIND_SOCKET;
    if (!path) {
        if (!host) {
            error_setg(errp, "chardev: socket: no host given");
            return;
        }
        if (!port) {
            error_setg(errp, "chardev: socket: no port given");
            return;
        }
    } else {
        if (tls_creds) {
            error_setg(errp, "TLS can only be used over TCP socket");
            return;
        }
    }

    sock = backend->u.socket.data = g_new0(ChardevSocket, 1);
    qemu_chr_parse_common(opts, qapi_ChardevSocket_base(sock));

    sock->has_nodelay = true;
    sock->nodelay = do_nodelay;
    sock->has_server = true;
    sock->server = is_listen;
    sock->has_telnet = true;
    sock->telnet = is_telnet;
    sock->has_wait = true;
    sock->wait = is_waitconnect;
    sock->has_reconnect = true;
    sock->reconnect = reconnect;
    sock->tls_creds = g_strdup(tls_creds);

    addr = g_new0(SocketAddress, 1);
    if (path) {
        UnixSocketAddress *q_unix;
        addr->type = SOCKET_ADDRESS_KIND_UNIX;
        q_unix = addr->u.q_unix.data = g_new0(UnixSocketAddress, 1);
        q_unix->path = g_strdup(path);
    } else {
        addr->type = SOCKET_ADDRESS_KIND_INET;
        addr->u.inet.data = g_new(InetSocketAddress, 1);
        *addr->u.inet.data = (InetSocketAddress) {
            .host = g_strdup(host),
            .port = g_strdup(port),
            .has_to = qemu_opt_get(opts, "to"),
            .to = qemu_opt_get_number(opts, "to", 0),
            .has_ipv4 = qemu_opt_get(opts, "ipv4"),
            .ipv4 = qemu_opt_get_bool(opts, "ipv4", 0),
            .has_ipv6 = qemu_opt_get(opts, "ipv6"),
            .ipv6 = qemu_opt_get_bool(opts, "ipv6", 0),
        };
    }
    sock->addr = addr;
}

static void qemu_chr_parse_udp(QemuOpts *opts, ChardevBackend *backend,
                               Error **errp)
{
    const char *host = qemu_opt_get(opts, "host");
    const char *port = qemu_opt_get(opts, "port");
    const char *localaddr = qemu_opt_get(opts, "localaddr");
    const char *localport = qemu_opt_get(opts, "localport");
    bool has_local = false;
    SocketAddress *addr;
    ChardevUdp *udp;

    backend->type = CHARDEV_BACKEND_KIND_UDP;
    if (host == NULL || strlen(host) == 0) {
        host = "localhost";
    }
    if (port == NULL || strlen(port) == 0) {
        error_setg(errp, "chardev: udp: remote port not specified");
        return;
    }
    if (localport == NULL || strlen(localport) == 0) {
        localport = "0";
    } else {
        has_local = true;
    }
    if (localaddr == NULL || strlen(localaddr) == 0) {
        localaddr = "";
    } else {
        has_local = true;
    }

    udp = backend->u.udp.data = g_new0(ChardevUdp, 1);
    qemu_chr_parse_common(opts, qapi_ChardevUdp_base(udp));

    addr = g_new0(SocketAddress, 1);
    addr->type = SOCKET_ADDRESS_KIND_INET;
    addr->u.inet.data = g_new(InetSocketAddress, 1);
    *addr->u.inet.data = (InetSocketAddress) {
        .host = g_strdup(host),
        .port = g_strdup(port),
        .has_ipv4 = qemu_opt_get(opts, "ipv4"),
        .ipv4 = qemu_opt_get_bool(opts, "ipv4", 0),
        .has_ipv6 = qemu_opt_get(opts, "ipv6"),
        .ipv6 = qemu_opt_get_bool(opts, "ipv6", 0),
    };
    udp->remote = addr;

    if (has_local) {
        udp->has_local = true;
        addr = g_new0(SocketAddress, 1);
        addr->type = SOCKET_ADDRESS_KIND_INET;
        addr->u.inet.data = g_new(InetSocketAddress, 1);
        *addr->u.inet.data = (InetSocketAddress) {
            .host = g_strdup(localaddr),
            .port = g_strdup(localport),
        };
        udp->local = addr;
    }
}

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

static void qmp_chardev_open_file(Chardev *chr,
                                  ChardevBackend *backend,
                                  bool *be_opened,
                                  Error **errp)
{
    ChardevFile *file = backend->u.file.data;
    HANDLE out;
    DWORD accessmode;
    DWORD flags;

    if (file->has_in) {
        error_setg(errp, "input file not supported");
        return;
    }

    if (file->has_append && file->append) {
        /* Append to file if it already exists. */
        accessmode = FILE_GENERIC_WRITE & ~FILE_WRITE_DATA;
        flags = OPEN_ALWAYS;
    } else {
        /* Truncate file if it already exists. */
        accessmode = GENERIC_WRITE;
        flags = CREATE_ALWAYS;
    }

    out = CreateFile(file->out, accessmode, FILE_SHARE_READ, NULL, flags,
                     FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) {
        error_setg(errp, "open %s failed", file->out);
        return;
    }

    qemu_chr_open_win_file(chr, out);
}

static void qmp_chardev_open_serial(Chardev *chr,
                                    ChardevBackend *backend,
                                    bool *be_opened,
                                    Error **errp)
{
    ChardevHostdev *serial = backend->u.serial.data;

    win_chr_init(chr, serial->device, errp);
}

#else /* WIN32 */

static int qmp_chardev_open_file_source(char *src, int flags,
                                        Error **errp)
{
    int fd = -1;

    TFR(fd = qemu_open(src, flags, 0666));
    if (fd == -1) {
        error_setg_file_open(errp, errno, src);
    }
    return fd;
}

static void qmp_chardev_open_file(Chardev *chr,
                                  ChardevBackend *backend,
                                  bool *be_opened,
                                  Error **errp)
{
    ChardevFile *file = backend->u.file.data;
    int flags, in = -1, out;

    flags = O_WRONLY | O_CREAT | O_BINARY;
    if (file->has_append && file->append) {
        flags |= O_APPEND;
    } else {
        flags |= O_TRUNC;
    }

    out = qmp_chardev_open_file_source(file->out, flags, errp);
    if (out < 0) {
        return;
    }

    if (file->has_in) {
        flags = O_RDONLY;
        in = qmp_chardev_open_file_source(file->in, flags, errp);
        if (in < 0) {
            qemu_close(out);
            return;
        }
    }

    qemu_chr_open_fd(chr, in, out);
}

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

static void char_file_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_file_out;
    cc->open = qmp_chardev_open_file;
}

static const TypeInfo char_file_type_info = {
    .name = TYPE_CHARDEV_FILE,
#ifdef _WIN32
    .parent = TYPE_CHARDEV_WIN,
#else
    .parent = TYPE_CHARDEV_FD,
#endif
    .class_init = char_file_class_init,
};

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

static gboolean socket_reconnect_timeout(gpointer opaque)
{
    Chardev *chr = CHARDEV(opaque);
    SocketChardev *s = SOCKET_CHARDEV(opaque);
    QIOChannelSocket *sioc;

    s->reconnect_timer = 0;

    if (chr->be_open) {
        return false;
    }

    sioc = qio_channel_socket_new();
    tcp_chr_set_client_ioc_name(chr, sioc);
    qio_channel_socket_connect_async(sioc, s->addr,
                                     qemu_chr_socket_connected,
                                     chr, NULL);

    return false;
}

static void qmp_chardev_open_socket(Chardev *chr,
                                    ChardevBackend *backend,
                                    bool *be_opened,
                                    Error **errp)
{
    SocketChardev *s = SOCKET_CHARDEV(chr);
    ChardevSocket *sock = backend->u.socket.data;
    SocketAddress *addr = sock->addr;
    bool do_nodelay     = sock->has_nodelay ? sock->nodelay : false;
    bool is_listen      = sock->has_server  ? sock->server  : true;
    bool is_telnet      = sock->has_telnet  ? sock->telnet  : false;
    bool is_waitconnect = sock->has_wait    ? sock->wait    : false;
    int64_t reconnect   = sock->has_reconnect ? sock->reconnect : 0;
    QIOChannelSocket *sioc = NULL;

    s->is_unix = addr->type == SOCKET_ADDRESS_KIND_UNIX;
    s->is_listen = is_listen;
    s->is_telnet = is_telnet;
    s->do_nodelay = do_nodelay;
    if (sock->tls_creds) {
        Object *creds;
        creds = object_resolve_path_component(
            object_get_objects_root(), sock->tls_creds);
        if (!creds) {
            error_setg(errp, "No TLS credentials with id '%s'",
                       sock->tls_creds);
            goto error;
        }
        s->tls_creds = (QCryptoTLSCreds *)
            object_dynamic_cast(creds,
                                TYPE_QCRYPTO_TLS_CREDS);
        if (!s->tls_creds) {
            error_setg(errp, "Object with id '%s' is not TLS credentials",
                       sock->tls_creds);
            goto error;
        }
        object_ref(OBJECT(s->tls_creds));
        if (is_listen) {
            if (s->tls_creds->endpoint != QCRYPTO_TLS_CREDS_ENDPOINT_SERVER) {
                error_setg(errp, "%s",
                           "Expected TLS credentials for server endpoint");
                goto error;
            }
        } else {
            if (s->tls_creds->endpoint != QCRYPTO_TLS_CREDS_ENDPOINT_CLIENT) {
                error_setg(errp, "%s",
                           "Expected TLS credentials for client endpoint");
                goto error;
            }
        }
    }

    s->addr = QAPI_CLONE(SocketAddress, sock->addr);

    qemu_chr_set_feature(chr, QEMU_CHAR_FEATURE_RECONNECTABLE);
    if (s->is_unix) {
        qemu_chr_set_feature(chr, QEMU_CHAR_FEATURE_FD_PASS);
    }

    /* be isn't opened until we get a connection */
    *be_opened = false;

    chr->filename = SocketAddress_to_str("disconnected:",
                                         addr, is_listen, is_telnet);

    if (is_listen) {
        if (is_telnet) {
            s->do_telnetopt = 1;
        }
    } else if (reconnect > 0) {
        s->reconnect_time = reconnect;
    }

    if (s->reconnect_time) {
        sioc = qio_channel_socket_new();
        tcp_chr_set_client_ioc_name(chr, sioc);
        qio_channel_socket_connect_async(sioc, s->addr,
                                         qemu_chr_socket_connected,
                                         chr, NULL);
    } else {
        if (s->is_listen) {
            char *name;
            sioc = qio_channel_socket_new();

            name = g_strdup_printf("chardev-tcp-listener-%s", chr->label);
            qio_channel_set_name(QIO_CHANNEL(sioc), name);
            g_free(name);

            if (qio_channel_socket_listen_sync(sioc, s->addr, errp) < 0) {
                goto error;
            }
            s->listen_ioc = sioc;
            if (is_waitconnect &&
                qemu_chr_wait_connected(chr, errp) < 0) {
                return;
            }
            if (!s->ioc) {
                s->listen_tag = qio_channel_add_watch(
                    QIO_CHANNEL(s->listen_ioc), G_IO_IN,
                    tcp_chr_accept, chr, NULL);
            }
        } else if (qemu_chr_wait_connected(chr, errp) < 0) {
            goto error;
        }
    }

    return;

error:
    if (sioc) {
        object_unref(OBJECT(sioc));
    }
}

static void char_socket_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_socket;
    cc->open = qmp_chardev_open_socket;
    cc->chr_wait_connected = tcp_chr_wait_connected;
    cc->chr_write = tcp_chr_write;
    cc->chr_sync_read = tcp_chr_sync_read;
    cc->chr_disconnect = tcp_chr_disconnect;
    cc->get_msgfds = tcp_get_msgfds;
    cc->set_msgfds = tcp_set_msgfds;
    cc->chr_add_client = tcp_chr_add_client;
    cc->chr_add_watch = tcp_chr_add_watch;
    cc->chr_update_read_handler = tcp_chr_update_read_handler;
}

static const TypeInfo char_socket_type_info = {
    .name = TYPE_CHARDEV_SOCKET,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(SocketChardev),
    .instance_finalize = char_socket_finalize,
    .class_init = char_socket_class_init,
};

static void qmp_chardev_open_udp(Chardev *chr,
                                 ChardevBackend *backend,
                                 bool *be_opened,
                                 Error **errp)
{
    ChardevUdp *udp = backend->u.udp.data;
    QIOChannelSocket *sioc = qio_channel_socket_new();
    char *name;
    UdpChardev *s = UDP_CHARDEV(chr);

    if (qio_channel_socket_dgram_sync(sioc,
                                      udp->local, udp->remote,
                                      errp) < 0) {
        object_unref(OBJECT(sioc));
        return;
    }

    name = g_strdup_printf("chardev-udp-%s", chr->label);
    qio_channel_set_name(QIO_CHANNEL(sioc), name);
    g_free(name);

    s->ioc = QIO_CHANNEL(sioc);
    /* be isn't opened until we get a connection */
    *be_opened = false;
}

static void char_udp_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_udp;
    cc->open = qmp_chardev_open_udp;
    cc->chr_write = udp_chr_write;
    cc->chr_update_read_handler = udp_chr_update_read_handler;
}

static const TypeInfo char_udp_type_info = {
    .name = TYPE_CHARDEV_UDP,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(UdpChardev),
    .instance_finalize = char_udp_finalize,
    .class_init = char_udp_class_init,
};

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
#ifndef _WIN32
    type_register_static(&char_fd_type_info);
#else
    type_register_static(&char_win_type_info);
    type_register_static(&char_win_stdio_type_info);
#endif
    type_register_static(&char_socket_type_info);
    type_register_static(&char_udp_type_info);
    type_register_static(&char_file_type_info);
    type_register_static(&char_stdio_type_info);
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
