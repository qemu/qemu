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
#include "qapi/qmp-input-visitor.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi-visit.h"
#include "qemu/base64.h"
#include "io/channel-socket.h"
#include "io/channel-file.h"
#include "io/channel-tls.h"
#include "sysemu/replay.h"

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

#define READ_BUF_LEN 4096
#define READ_RETRIES 10
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

static QTAILQ_HEAD(CharDriverStateHead, CharDriverState) chardevs =
    QTAILQ_HEAD_INITIALIZER(chardevs);

static void qemu_chr_free_common(CharDriverState *chr);

CharDriverState *qemu_chr_alloc(ChardevCommon *backend, Error **errp)
{
    CharDriverState *chr = g_malloc0(sizeof(CharDriverState));
    qemu_mutex_init(&chr->chr_write_lock);

    if (backend->has_logfile) {
        int flags = O_WRONLY | O_CREAT;
        if (backend->has_logappend &&
            backend->logappend) {
            flags |= O_APPEND;
        } else {
            flags |= O_TRUNC;
        }
        chr->logfd = qemu_open(backend->logfile, flags, 0666);
        if (chr->logfd < 0) {
            error_setg_errno(errp, errno,
                             "Unable to open logfile %s",
                             backend->logfile);
            g_free(chr);
            return NULL;
        }
    } else {
        chr->logfd = -1;
    }

    return chr;
}

void qemu_chr_be_event(CharDriverState *s, int event)
{
    /* Keep track if the char device is open */
    switch (event) {
        case CHR_EVENT_OPENED:
            s->be_open = 1;
            break;
        case CHR_EVENT_CLOSED:
            s->be_open = 0;
            break;
    }

    if (!s->chr_event)
        return;
    s->chr_event(s->handler_opaque, event);
}

void qemu_chr_be_generic_open(CharDriverState *s)
{
    qemu_chr_be_event(s, CHR_EVENT_OPENED);
}


/* Not reporting errors from writing to logfile, as logs are
 * defined to be "best effort" only */
static void qemu_chr_fe_write_log(CharDriverState *s,
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

static int qemu_chr_fe_write_buffer(CharDriverState *s, const uint8_t *buf, int len, int *offset)
{
    int res = 0;
    *offset = 0;

    qemu_mutex_lock(&s->chr_write_lock);
    while (*offset < len) {
    retry:
        res = s->chr_write(s, buf + *offset, len - *offset);
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

int qemu_chr_fe_write(CharDriverState *s, const uint8_t *buf, int len)
{
    int ret;

    if (s->replay && replay_mode == REPLAY_MODE_PLAY) {
        int offset;
        replay_char_write_event_load(&ret, &offset);
        assert(offset <= len);
        qemu_chr_fe_write_buffer(s, buf, offset, &offset);
        return ret;
    }

    qemu_mutex_lock(&s->chr_write_lock);
    ret = s->chr_write(s, buf, len);

    if (ret > 0) {
        qemu_chr_fe_write_log(s, buf, ret);
    }

    qemu_mutex_unlock(&s->chr_write_lock);
    
    if (s->replay && replay_mode == REPLAY_MODE_RECORD) {
        replay_char_write_event_save(ret, ret < 0 ? 0 : ret);
    }
    
    return ret;
}

int qemu_chr_fe_write_all(CharDriverState *s, const uint8_t *buf, int len)
{
    int offset;
    int res;

    if (s->replay && replay_mode == REPLAY_MODE_PLAY) {
        replay_char_write_event_load(&res, &offset);
        assert(offset <= len);
        qemu_chr_fe_write_buffer(s, buf, offset, &offset);
        return res;
    }

    res = qemu_chr_fe_write_buffer(s, buf, len, &offset);

    if (s->replay && replay_mode == REPLAY_MODE_RECORD) {
        replay_char_write_event_save(res, offset);
    }

    if (res < 0) {
        return res;
    }
    return offset;
}

int qemu_chr_fe_read_all(CharDriverState *s, uint8_t *buf, int len)
{
    int offset = 0, counter = 10;
    int res;

    if (!s->chr_sync_read) {
        return 0;
    }
    
    if (s->replay && replay_mode == REPLAY_MODE_PLAY) {
        return replay_char_read_all_load(buf);
    }

    while (offset < len) {
    retry:
        res = s->chr_sync_read(s, buf + offset, len - offset);
        if (res == -1 && errno == EAGAIN) {
            g_usleep(100);
            goto retry;
        }

        if (res == 0) {
            break;
        }

        if (res < 0) {
            if (s->replay && replay_mode == REPLAY_MODE_RECORD) {
                replay_char_read_all_save_error(res);
            }
            return res;
        }

        offset += res;

        if (!counter--) {
            break;
        }
    }

    if (s->replay && replay_mode == REPLAY_MODE_RECORD) {
        replay_char_read_all_save_buf(buf, offset);
    }
    return offset;
}

int qemu_chr_fe_ioctl(CharDriverState *s, int cmd, void *arg)
{
    int res;
    if (!s->chr_ioctl || s->replay) {
        res = -ENOTSUP;
    } else {
        res = s->chr_ioctl(s, cmd, arg);
    }

    return res;
}

int qemu_chr_be_can_write(CharDriverState *s)
{
    if (!s->chr_can_read)
        return 0;
    return s->chr_can_read(s->handler_opaque);
}

void qemu_chr_be_write_impl(CharDriverState *s, uint8_t *buf, int len)
{
    if (s->chr_read) {
        s->chr_read(s->handler_opaque, buf, len);
    }
}

void qemu_chr_be_write(CharDriverState *s, uint8_t *buf, int len)
{
    if (s->replay) {
        if (replay_mode == REPLAY_MODE_PLAY) {
            return;
        }
        replay_chr_be_write(s, buf, len);
    } else {
        qemu_chr_be_write_impl(s, buf, len);
    }
}

int qemu_chr_fe_get_msgfd(CharDriverState *s)
{
    int fd;
    int res = (qemu_chr_fe_get_msgfds(s, &fd, 1) == 1) ? fd : -1;
    if (s->replay) {
        fprintf(stderr,
                "Replay: get msgfd is not supported for serial devices yet\n");
        exit(1);
    }
    return res;
}

int qemu_chr_fe_get_msgfds(CharDriverState *s, int *fds, int len)
{
    return s->get_msgfds ? s->get_msgfds(s, fds, len) : -1;
}

int qemu_chr_fe_set_msgfds(CharDriverState *s, int *fds, int num)
{
    return s->set_msgfds ? s->set_msgfds(s, fds, num) : -1;
}

int qemu_chr_add_client(CharDriverState *s, int fd)
{
    return s->chr_add_client ? s->chr_add_client(s, fd) : -1;
}

void qemu_chr_accept_input(CharDriverState *s)
{
    if (s->chr_accept_input)
        s->chr_accept_input(s);
    qemu_notify_event();
}

void qemu_chr_fe_printf(CharDriverState *s, const char *fmt, ...)
{
    char buf[READ_BUF_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    qemu_chr_fe_write(s, (uint8_t *)buf, strlen(buf));
    va_end(ap);
}

static void remove_fd_in_watch(CharDriverState *chr);

void qemu_chr_add_handlers(CharDriverState *s,
                           IOCanReadHandler *fd_can_read,
                           IOReadHandler *fd_read,
                           IOEventHandler *fd_event,
                           void *opaque)
{
    int fe_open;

    if (!opaque && !fd_can_read && !fd_read && !fd_event) {
        fe_open = 0;
        remove_fd_in_watch(s);
    } else {
        fe_open = 1;
    }
    s->chr_can_read = fd_can_read;
    s->chr_read = fd_read;
    s->chr_event = fd_event;
    s->handler_opaque = opaque;
    if (fe_open && s->chr_update_read_handler)
        s->chr_update_read_handler(s);

    if (!s->explicit_fe_open) {
        qemu_chr_fe_set_open(s, fe_open);
    }

    /* We're connecting to an already opened device, so let's make sure we
       also get the open event */
    if (fe_open && s->be_open) {
        qemu_chr_be_generic_open(s);
    }
}

static int null_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    return len;
}

static CharDriverState *qemu_chr_open_null(const char *id,
                                           ChardevBackend *backend,
                                           ChardevReturn *ret,
                                           Error **errp)
{
    CharDriverState *chr;
    ChardevCommon *common = backend->u.null.data;

    chr = qemu_chr_alloc(common, errp);
    if (!chr) {
        return NULL;
    }
    chr->chr_write = null_chr_write;
    chr->explicit_be_open = true;
    return chr;
}

/* MUX driver for serial I/O splitting */
#define MAX_MUX 4
#define MUX_BUFFER_SIZE 32	/* Must be a power of 2.  */
#define MUX_BUFFER_MASK (MUX_BUFFER_SIZE - 1)
typedef struct {
    IOCanReadHandler *chr_can_read[MAX_MUX];
    IOReadHandler *chr_read[MAX_MUX];
    IOEventHandler *chr_event[MAX_MUX];
    void *ext_opaque[MAX_MUX];
    CharDriverState *drv;
    int focus;
    int mux_cnt;
    int term_got_escape;
    int max_size;
    /* Intermediate input buffer allows to catch escape sequences even if the
       currently active device is not accepting any input - but only until it
       is full as well. */
    unsigned char buffer[MAX_MUX][MUX_BUFFER_SIZE];
    int prod[MAX_MUX];
    int cons[MAX_MUX];
    int timestamps;

    /* Protected by the CharDriverState chr_write_lock.  */
    int linestart;
    int64_t timestamps_start;
} MuxDriver;


/* Called with chr_write_lock held.  */
static int mux_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    MuxDriver *d = chr->opaque;
    int ret;
    if (!d->timestamps) {
        ret = qemu_chr_fe_write(d->drv, buf, len);
    } else {
        int i;

        ret = 0;
        for (i = 0; i < len; i++) {
            if (d->linestart) {
                char buf1[64];
                int64_t ti;
                int secs;

                ti = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
                if (d->timestamps_start == -1)
                    d->timestamps_start = ti;
                ti -= d->timestamps_start;
                secs = ti / 1000;
                snprintf(buf1, sizeof(buf1),
                         "[%02d:%02d:%02d.%03d] ",
                         secs / 3600,
                         (secs / 60) % 60,
                         secs % 60,
                         (int)(ti % 1000));
                qemu_chr_fe_write(d->drv, (uint8_t *)buf1, strlen(buf1));
                d->linestart = 0;
            }
            ret += qemu_chr_fe_write(d->drv, buf+i, 1);
            if (buf[i] == '\n') {
                d->linestart = 1;
            }
        }
    }
    return ret;
}

static const char * const mux_help[] = {
    "% h    print this help\n\r",
    "% x    exit emulator\n\r",
    "% s    save disk data back to file (if -snapshot)\n\r",
    "% t    toggle console timestamps\n\r",
    "% b    send break (magic sysrq)\n\r",
    "% c    switch between console and monitor\n\r",
    "% %  sends %\n\r",
    NULL
};

int term_escape_char = 0x01; /* ctrl-a is used for escape */
static void mux_print_help(CharDriverState *chr)
{
    int i, j;
    char ebuf[15] = "Escape-Char";
    char cbuf[50] = "\n\r";

    if (term_escape_char > 0 && term_escape_char < 26) {
        snprintf(cbuf, sizeof(cbuf), "\n\r");
        snprintf(ebuf, sizeof(ebuf), "C-%c", term_escape_char - 1 + 'a');
    } else {
        snprintf(cbuf, sizeof(cbuf),
                 "\n\rEscape-Char set to Ascii: 0x%02x\n\r\n\r",
                 term_escape_char);
    }
    qemu_chr_fe_write(chr, (uint8_t *)cbuf, strlen(cbuf));
    for (i = 0; mux_help[i] != NULL; i++) {
        for (j=0; mux_help[i][j] != '\0'; j++) {
            if (mux_help[i][j] == '%')
                qemu_chr_fe_write(chr, (uint8_t *)ebuf, strlen(ebuf));
            else
                qemu_chr_fe_write(chr, (uint8_t *)&mux_help[i][j], 1);
        }
    }
}

static void mux_chr_send_event(MuxDriver *d, int mux_nr, int event)
{
    if (d->chr_event[mux_nr])
        d->chr_event[mux_nr](d->ext_opaque[mux_nr], event);
}

static int mux_proc_byte(CharDriverState *chr, MuxDriver *d, int ch)
{
    if (d->term_got_escape) {
        d->term_got_escape = 0;
        if (ch == term_escape_char)
            goto send_char;
        switch(ch) {
        case '?':
        case 'h':
            mux_print_help(chr);
            break;
        case 'x':
            {
                 const char *term =  "QEMU: Terminated\n\r";
                 qemu_chr_fe_write(chr, (uint8_t *)term, strlen(term));
                 exit(0);
                 break;
            }
        case 's':
            blk_commit_all();
            break;
        case 'b':
            qemu_chr_be_event(chr, CHR_EVENT_BREAK);
            break;
        case 'c':
            /* Switch to the next registered device */
            mux_chr_send_event(d, d->focus, CHR_EVENT_MUX_OUT);
            d->focus++;
            if (d->focus >= d->mux_cnt)
                d->focus = 0;
            mux_chr_send_event(d, d->focus, CHR_EVENT_MUX_IN);
            break;
        case 't':
            d->timestamps = !d->timestamps;
            d->timestamps_start = -1;
            d->linestart = 0;
            break;
        }
    } else if (ch == term_escape_char) {
        d->term_got_escape = 1;
    } else {
    send_char:
        return 1;
    }
    return 0;
}

static void mux_chr_accept_input(CharDriverState *chr)
{
    MuxDriver *d = chr->opaque;
    int m = d->focus;

    while (d->prod[m] != d->cons[m] &&
           d->chr_can_read[m] &&
           d->chr_can_read[m](d->ext_opaque[m])) {
        d->chr_read[m](d->ext_opaque[m],
                       &d->buffer[m][d->cons[m]++ & MUX_BUFFER_MASK], 1);
    }
}

static int mux_chr_can_read(void *opaque)
{
    CharDriverState *chr = opaque;
    MuxDriver *d = chr->opaque;
    int m = d->focus;

    if ((d->prod[m] - d->cons[m]) < MUX_BUFFER_SIZE)
        return 1;
    if (d->chr_can_read[m])
        return d->chr_can_read[m](d->ext_opaque[m]);
    return 0;
}

static void mux_chr_read(void *opaque, const uint8_t *buf, int size)
{
    CharDriverState *chr = opaque;
    MuxDriver *d = chr->opaque;
    int m = d->focus;
    int i;

    mux_chr_accept_input (opaque);

    for(i = 0; i < size; i++)
        if (mux_proc_byte(chr, d, buf[i])) {
            if (d->prod[m] == d->cons[m] &&
                d->chr_can_read[m] &&
                d->chr_can_read[m](d->ext_opaque[m]))
                d->chr_read[m](d->ext_opaque[m], &buf[i], 1);
            else
                d->buffer[m][d->prod[m]++ & MUX_BUFFER_MASK] = buf[i];
        }
}

static void mux_chr_event(void *opaque, int event)
{
    CharDriverState *chr = opaque;
    MuxDriver *d = chr->opaque;
    int i;

    /* Send the event to all registered listeners */
    for (i = 0; i < d->mux_cnt; i++)
        mux_chr_send_event(d, i, event);
}

static void mux_chr_update_read_handler(CharDriverState *chr)
{
    MuxDriver *d = chr->opaque;

    if (d->mux_cnt >= MAX_MUX) {
        fprintf(stderr, "Cannot add I/O handlers, MUX array is full\n");
        return;
    }
    d->ext_opaque[d->mux_cnt] = chr->handler_opaque;
    d->chr_can_read[d->mux_cnt] = chr->chr_can_read;
    d->chr_read[d->mux_cnt] = chr->chr_read;
    d->chr_event[d->mux_cnt] = chr->chr_event;
    /* Fix up the real driver with mux routines */
    if (d->mux_cnt == 0) {
        qemu_chr_add_handlers(d->drv, mux_chr_can_read, mux_chr_read,
                              mux_chr_event, chr);
    }
    if (d->focus != -1) {
        mux_chr_send_event(d, d->focus, CHR_EVENT_MUX_OUT);
    }
    d->focus = d->mux_cnt;
    d->mux_cnt++;
    mux_chr_send_event(d, d->focus, CHR_EVENT_MUX_IN);
}

static bool muxes_realized;

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
    CharDriverState *chr;

    QTAILQ_FOREACH(chr, &chardevs, next) {
        if (chr->is_mux) {
            MuxDriver *d = chr->opaque;
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

static GSource *mux_chr_add_watch(CharDriverState *s, GIOCondition cond)
{
    MuxDriver *d = s->opaque;
    return d->drv->chr_add_watch(d->drv, cond);
}

static CharDriverState *qemu_chr_open_mux(const char *id,
                                          ChardevBackend *backend,
                                          ChardevReturn *ret, Error **errp)
{
    ChardevMux *mux = backend->u.mux.data;
    CharDriverState *chr, *drv;
    MuxDriver *d;
    ChardevCommon *common = qapi_ChardevMux_base(mux);

    drv = qemu_chr_find(mux->chardev);
    if (drv == NULL) {
        error_setg(errp, "mux: base chardev %s not found", mux->chardev);
        return NULL;
    }

    chr = qemu_chr_alloc(common, errp);
    if (!chr) {
        return NULL;
    }
    d = g_new0(MuxDriver, 1);

    chr->opaque = d;
    d->drv = drv;
    d->focus = -1;
    chr->chr_write = mux_chr_write;
    chr->chr_update_read_handler = mux_chr_update_read_handler;
    chr->chr_accept_input = mux_chr_accept_input;
    /* Frontend guest-open / -close notification is not support with muxes */
    chr->chr_set_fe_open = NULL;
    if (drv->chr_add_watch) {
        chr->chr_add_watch = mux_chr_add_watch;
    }
    /* only default to opened state if we've realized the initial
     * set of muxes
     */
    chr->explicit_be_open = muxes_realized ? 0 : 1;
    chr->is_mux = 1;

    return chr;
}


typedef struct IOWatchPoll
{
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

static gboolean io_watch_poll_prepare(GSource *source, gint *timeout_)
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
        g_source_attach(iwp->src, NULL);
    } else {
        g_source_destroy(iwp->src);
        g_source_unref(iwp->src);
        iwp->src = NULL;
    }
    return FALSE;
}

static gboolean io_watch_poll_check(GSource *source)
{
    return FALSE;
}

static gboolean io_watch_poll_dispatch(GSource *source, GSourceFunc callback,
                                       gpointer user_data)
{
    abort();
}

static void io_watch_poll_finalize(GSource *source)
{
    /* Due to a glib bug, removing the last reference to a source
     * inside a finalize callback causes recursive locking (and a
     * deadlock).  This is not a problem inside other callbacks,
     * including dispatch callbacks, so we call io_remove_watch_poll
     * to remove this source.  At this point, iwp->src must
     * be NULL, or we would leak it.
     *
     * This would be solved much more elegantly by child sources,
     * but we support older glib versions that do not have them.
     */
    IOWatchPoll *iwp = io_watch_poll_from_source(source);
    assert(iwp->src == NULL);
}

static GSourceFuncs io_watch_poll_funcs = {
    .prepare = io_watch_poll_prepare,
    .check = io_watch_poll_check,
    .dispatch = io_watch_poll_dispatch,
    .finalize = io_watch_poll_finalize,
};

/* Can only be used for read */
static guint io_add_watch_poll(QIOChannel *ioc,
                               IOCanReadHandler *fd_can_read,
                               QIOChannelFunc fd_read,
                               gpointer user_data)
{
    IOWatchPoll *iwp;
    int tag;

    iwp = (IOWatchPoll *) g_source_new(&io_watch_poll_funcs, sizeof(IOWatchPoll));
    iwp->fd_can_read = fd_can_read;
    iwp->opaque = user_data;
    iwp->ioc = ioc;
    iwp->fd_read = (GSourceFunc) fd_read;
    iwp->src = NULL;

    tag = g_source_attach(&iwp->parent, NULL);
    g_source_unref(&iwp->parent);
    return tag;
}

static void io_remove_watch_poll(guint tag)
{
    GSource *source;
    IOWatchPoll *iwp;

    g_return_if_fail (tag > 0);

    source = g_main_context_find_source_by_id(NULL, tag);
    g_return_if_fail (source != NULL);

    iwp = io_watch_poll_from_source(source);
    if (iwp->src) {
        g_source_destroy(iwp->src);
        g_source_unref(iwp->src);
        iwp->src = NULL;
    }
    g_source_destroy(&iwp->parent);
}

static void remove_fd_in_watch(CharDriverState *chr)
{
    if (chr->fd_in_tag) {
        io_remove_watch_poll(chr->fd_in_tag);
        chr->fd_in_tag = 0;
    }
}


static int io_channel_send_full(QIOChannel *ioc,
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


#ifndef _WIN32
static int io_channel_send(QIOChannel *ioc, const void *buf, size_t len)
{
    return io_channel_send_full(ioc, buf, len, NULL, 0);
}


typedef struct FDCharDriver {
    CharDriverState *chr;
    QIOChannel *ioc_in, *ioc_out;
    int max_size;
} FDCharDriver;

/* Called with chr_write_lock held.  */
static int fd_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    FDCharDriver *s = chr->opaque;
    
    return io_channel_send(s->ioc_out, buf, len);
}

static gboolean fd_chr_read(QIOChannel *chan, GIOCondition cond, void *opaque)
{
    CharDriverState *chr = opaque;
    FDCharDriver *s = chr->opaque;
    int len;
    uint8_t buf[READ_BUF_LEN];
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
    CharDriverState *chr = opaque;
    FDCharDriver *s = chr->opaque;

    s->max_size = qemu_chr_be_can_write(chr);
    return s->max_size;
}

static GSource *fd_chr_add_watch(CharDriverState *chr, GIOCondition cond)
{
    FDCharDriver *s = chr->opaque;
    return qio_channel_create_watch(s->ioc_out, cond);
}

static void fd_chr_update_read_handler(CharDriverState *chr)
{
    FDCharDriver *s = chr->opaque;

    remove_fd_in_watch(chr);
    if (s->ioc_in) {
        chr->fd_in_tag = io_add_watch_poll(s->ioc_in,
                                           fd_chr_read_poll,
                                           fd_chr_read, chr);
    }
}

static void fd_chr_close(struct CharDriverState *chr)
{
    FDCharDriver *s = chr->opaque;

    remove_fd_in_watch(chr);
    if (s->ioc_in) {
        object_unref(OBJECT(s->ioc_in));
    }
    if (s->ioc_out) {
        object_unref(OBJECT(s->ioc_out));
    }

    g_free(s);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

/* open a character device to a unix fd */
static CharDriverState *qemu_chr_open_fd(int fd_in, int fd_out,
                                         ChardevCommon *backend, Error **errp)
{
    CharDriverState *chr;
    FDCharDriver *s;

    chr = qemu_chr_alloc(backend, errp);
    if (!chr) {
        return NULL;
    }
    s = g_new0(FDCharDriver, 1);
    s->ioc_in = QIO_CHANNEL(qio_channel_file_new_fd(fd_in));
    s->ioc_out = QIO_CHANNEL(qio_channel_file_new_fd(fd_out));
    qemu_set_nonblock(fd_out);
    s->chr = chr;
    chr->opaque = s;
    chr->chr_add_watch = fd_chr_add_watch;
    chr->chr_write = fd_chr_write;
    chr->chr_update_read_handler = fd_chr_update_read_handler;
    chr->chr_close = fd_chr_close;

    return chr;
}

static CharDriverState *qemu_chr_open_pipe(const char *id,
                                           ChardevBackend *backend,
                                           ChardevReturn *ret,
                                           Error **errp)
{
    ChardevHostdev *opts = backend->u.pipe.data;
    int fd_in, fd_out;
    char *filename_in;
    char *filename_out;
    const char *filename = opts->device;
    ChardevCommon *common = qapi_ChardevHostdev_base(opts);


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
            return NULL;
        }
    }
    return qemu_chr_open_fd(fd_in, fd_out, common, errp);
}

/* init terminal so that we can grab keys */
static struct termios oldtty;
static int old_fd0_flags;
static bool stdio_in_use;
static bool stdio_allow_signal;
static bool stdio_echo_state;

static void qemu_chr_set_echo_stdio(CharDriverState *chr, bool echo);

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

static void qemu_chr_set_echo_stdio(CharDriverState *chr, bool echo)
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

static void qemu_chr_close_stdio(struct CharDriverState *chr)
{
    term_exit();
    fd_chr_close(chr);
}

static CharDriverState *qemu_chr_open_stdio(const char *id,
                                            ChardevBackend *backend,
                                            ChardevReturn *ret,
                                            Error **errp)
{
    ChardevStdio *opts = backend->u.stdio.data;
    CharDriverState *chr;
    struct sigaction act;
    ChardevCommon *common = qapi_ChardevStdio_base(opts);

    if (is_daemonized()) {
        error_setg(errp, "cannot use stdio with -daemonize");
        return NULL;
    }

    if (stdio_in_use) {
        error_setg(errp, "cannot use stdio by multiple character devices");
        return NULL;
    }

    stdio_in_use = true;
    old_fd0_flags = fcntl(0, F_GETFL);
    tcgetattr(0, &oldtty);
    qemu_set_nonblock(0);
    atexit(term_exit);

    memset(&act, 0, sizeof(act));
    act.sa_handler = term_stdio_handler;
    sigaction(SIGCONT, &act, NULL);

    chr = qemu_chr_open_fd(0, 1, common, errp);
    chr->chr_close = qemu_chr_close_stdio;
    chr->chr_set_echo = qemu_chr_set_echo_stdio;
    if (opts->has_signal) {
        stdio_allow_signal = opts->signal;
    }
    qemu_chr_fe_set_echo(chr, false);

    return chr;
}

#if defined(__linux__) || defined(__sun__) || defined(__FreeBSD__) \
    || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) \
    || defined(__GLIBC__)

#define HAVE_CHARDEV_SERIAL 1
#define HAVE_CHARDEV_PTY 1

typedef struct {
    QIOChannel *ioc;
    int read_bytes;

    /* Protected by the CharDriverState chr_write_lock.  */
    int connected;
    guint timer_tag;
    guint open_tag;
} PtyCharDriver;

static void pty_chr_update_read_handler_locked(CharDriverState *chr);
static void pty_chr_state(CharDriverState *chr, int connected);

static gboolean pty_chr_timer(gpointer opaque)
{
    struct CharDriverState *chr = opaque;
    PtyCharDriver *s = chr->opaque;

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
static void pty_chr_rearm_timer(CharDriverState *chr, int ms)
{
    PtyCharDriver *s = chr->opaque;

    if (s->timer_tag) {
        g_source_remove(s->timer_tag);
        s->timer_tag = 0;
    }

    if (ms == 1000) {
        s->timer_tag = g_timeout_add_seconds(1, pty_chr_timer, chr);
    } else {
        s->timer_tag = g_timeout_add(ms, pty_chr_timer, chr);
    }
}

/* Called with chr_write_lock held.  */
static void pty_chr_update_read_handler_locked(CharDriverState *chr)
{
    PtyCharDriver *s = chr->opaque;
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

static void pty_chr_update_read_handler(CharDriverState *chr)
{
    qemu_mutex_lock(&chr->chr_write_lock);
    pty_chr_update_read_handler_locked(chr);
    qemu_mutex_unlock(&chr->chr_write_lock);
}

/* Called with chr_write_lock held.  */
static int pty_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    PtyCharDriver *s = chr->opaque;

    if (!s->connected) {
        /* guest sends data, check for (re-)connect */
        pty_chr_update_read_handler_locked(chr);
        if (!s->connected) {
            return 0;
        }
    }
    return io_channel_send(s->ioc, buf, len);
}

static GSource *pty_chr_add_watch(CharDriverState *chr, GIOCondition cond)
{
    PtyCharDriver *s = chr->opaque;
    if (!s->connected) {
        return NULL;
    }
    return qio_channel_create_watch(s->ioc, cond);
}

static int pty_chr_read_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    PtyCharDriver *s = chr->opaque;

    s->read_bytes = qemu_chr_be_can_write(chr);
    return s->read_bytes;
}

static gboolean pty_chr_read(QIOChannel *chan, GIOCondition cond, void *opaque)
{
    CharDriverState *chr = opaque;
    PtyCharDriver *s = chr->opaque;
    gsize len;
    uint8_t buf[READ_BUF_LEN];
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
    CharDriverState *chr = opaque;
    PtyCharDriver *s = chr->opaque;

    s->open_tag = 0;
    qemu_chr_be_generic_open(chr);
    return FALSE;
}

/* Called with chr_write_lock held.  */
static void pty_chr_state(CharDriverState *chr, int connected)
{
    PtyCharDriver *s = chr->opaque;

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
            chr->fd_in_tag = io_add_watch_poll(s->ioc,
                                               pty_chr_read_poll,
                                               pty_chr_read, chr);
        }
    }
}

static void pty_chr_close(struct CharDriverState *chr)
{
    PtyCharDriver *s = chr->opaque;

    qemu_mutex_lock(&chr->chr_write_lock);
    pty_chr_state(chr, 0);
    object_unref(OBJECT(s->ioc));
    if (s->timer_tag) {
        g_source_remove(s->timer_tag);
        s->timer_tag = 0;
    }
    qemu_mutex_unlock(&chr->chr_write_lock);
    g_free(s);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

static CharDriverState *qemu_chr_open_pty(const char *id,
                                          ChardevBackend *backend,
                                          ChardevReturn *ret,
                                          Error **errp)
{
    CharDriverState *chr;
    PtyCharDriver *s;
    int master_fd, slave_fd;
    char pty_name[PATH_MAX];
    ChardevCommon *common = backend->u.pty.data;

    master_fd = qemu_openpty_raw(&slave_fd, pty_name);
    if (master_fd < 0) {
        error_setg_errno(errp, errno, "Failed to create PTY");
        return NULL;
    }

    close(slave_fd);
    qemu_set_nonblock(master_fd);

    chr = qemu_chr_alloc(common, errp);
    if (!chr) {
        close(master_fd);
        return NULL;
    }

    chr->filename = g_strdup_printf("pty:%s", pty_name);
    ret->pty = g_strdup(pty_name);
    ret->has_pty = true;

    fprintf(stderr, "char device redirected to %s (label %s)\n",
            pty_name, id);

    s = g_new0(PtyCharDriver, 1);
    chr->opaque = s;
    chr->chr_write = pty_chr_write;
    chr->chr_update_read_handler = pty_chr_update_read_handler;
    chr->chr_close = pty_chr_close;
    chr->chr_add_watch = pty_chr_add_watch;
    chr->explicit_be_open = true;

    s->ioc = QIO_CHANNEL(qio_channel_file_new_fd(master_fd));
    s->timer_tag = 0;

    return chr;
}

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

static int tty_serial_ioctl(CharDriverState *chr, int cmd, void *arg)
{
    FDCharDriver *s = chr->opaque;
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

static void qemu_chr_close_tty(CharDriverState *chr)
{
    fd_chr_close(chr);
}

static CharDriverState *qemu_chr_open_tty_fd(int fd,
                                             ChardevCommon *backend,
                                             Error **errp)
{
    CharDriverState *chr;

    tty_serial_init(fd, 115200, 'N', 8, 1);
    chr = qemu_chr_open_fd(fd, fd, backend, errp);
    chr->chr_ioctl = tty_serial_ioctl;
    chr->chr_close = qemu_chr_close_tty;
    return chr;
}
#endif /* __linux__ || __sun__ */

#if defined(__linux__)

#define HAVE_CHARDEV_PARPORT 1

typedef struct {
    int fd;
    int mode;
} ParallelCharDriver;

static int pp_hw_mode(ParallelCharDriver *s, uint16_t mode)
{
    if (s->mode != mode) {
	int m = mode;
        if (ioctl(s->fd, PPSETMODE, &m) < 0)
            return 0;
	s->mode = mode;
    }
    return 1;
}

static int pp_ioctl(CharDriverState *chr, int cmd, void *arg)
{
    ParallelCharDriver *drv = chr->opaque;
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

static void pp_close(CharDriverState *chr)
{
    ParallelCharDriver *drv = chr->opaque;
    int fd = drv->fd;

    pp_hw_mode(drv, IEEE1284_MODE_COMPAT);
    ioctl(fd, PPRELEASE);
    close(fd);
    g_free(drv);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

static CharDriverState *qemu_chr_open_pp_fd(int fd,
                                            ChardevCommon *backend,
                                            Error **errp)
{
    CharDriverState *chr;
    ParallelCharDriver *drv;

    if (ioctl(fd, PPCLAIM) < 0) {
        error_setg_errno(errp, errno, "not a parallel port");
        close(fd);
        return NULL;
    }

    chr = qemu_chr_alloc(backend, errp);
    if (!chr) {
        return NULL;
    }

    drv = g_new0(ParallelCharDriver, 1);
    chr->opaque = drv;
    chr->chr_write = null_chr_write;
    chr->chr_ioctl = pp_ioctl;
    chr->chr_close = pp_close;

    drv->fd = fd;
    drv->mode = IEEE1284_MODE_COMPAT;

    return chr;
}
#endif /* __linux__ */

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__)

#define HAVE_CHARDEV_PARPORT 1

static int pp_ioctl(CharDriverState *chr, int cmd, void *arg)
{
    int fd = (int)(intptr_t)chr->opaque;
    uint8_t b;

    switch(cmd) {
    case CHR_IOCTL_PP_READ_DATA:
        if (ioctl(fd, PPIGDATA, &b) < 0)
            return -ENOTSUP;
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_WRITE_DATA:
        b = *(uint8_t *)arg;
        if (ioctl(fd, PPISDATA, &b) < 0)
            return -ENOTSUP;
        break;
    case CHR_IOCTL_PP_READ_CONTROL:
        if (ioctl(fd, PPIGCTRL, &b) < 0)
            return -ENOTSUP;
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_WRITE_CONTROL:
        b = *(uint8_t *)arg;
        if (ioctl(fd, PPISCTRL, &b) < 0)
            return -ENOTSUP;
        break;
    case CHR_IOCTL_PP_READ_STATUS:
        if (ioctl(fd, PPIGSTATUS, &b) < 0)
            return -ENOTSUP;
        *(uint8_t *)arg = b;
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static CharDriverState *qemu_chr_open_pp_fd(int fd,
                                            ChardevCommon *backend,
                                            Error **errp)
{
    CharDriverState *chr;

    chr = qemu_chr_alloc(backend, errp);
    if (!chr) {
        return NULL;
    }
    chr->opaque = (void *)(intptr_t)fd;
    chr->chr_write = null_chr_write;
    chr->chr_ioctl = pp_ioctl;
    chr->explicit_be_open = true;
    return chr;
}
#endif

#else /* _WIN32 */

#define HAVE_CHARDEV_SERIAL 1

typedef struct {
    int max_size;
    HANDLE hcom, hrecv, hsend;
    OVERLAPPED orecv;
    BOOL fpipe;
    DWORD len;

    /* Protected by the CharDriverState chr_write_lock.  */
    OVERLAPPED osend;
} WinCharState;

typedef struct {
    HANDLE  hStdIn;
    HANDLE  hInputReadyEvent;
    HANDLE  hInputDoneEvent;
    HANDLE  hInputThread;
    uint8_t win_stdio_buf;
} WinStdioCharState;

#define NSENDBUF 2048
#define NRECVBUF 2048
#define MAXCONNECT 1
#define NTIMEOUT 5000

static int win_chr_poll(void *opaque);
static int win_chr_pipe_poll(void *opaque);

static void win_chr_close(CharDriverState *chr)
{
    WinCharState *s = chr->opaque;

    if (s->hsend) {
        CloseHandle(s->hsend);
        s->hsend = NULL;
    }
    if (s->hrecv) {
        CloseHandle(s->hrecv);
        s->hrecv = NULL;
    }
    if (s->hcom) {
        CloseHandle(s->hcom);
        s->hcom = NULL;
    }
    if (s->fpipe)
        qemu_del_polling_cb(win_chr_pipe_poll, chr);
    else
        qemu_del_polling_cb(win_chr_poll, chr);

    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

static int win_chr_init(CharDriverState *chr, const char *filename, Error **errp)
{
    WinCharState *s = chr->opaque;
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
    win_chr_close(chr);
    return -1;
}

/* Called with chr_write_lock held.  */
static int win_chr_write(CharDriverState *chr, const uint8_t *buf, int len1)
{
    WinCharState *s = chr->opaque;
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

static int win_chr_read_poll(CharDriverState *chr)
{
    WinCharState *s = chr->opaque;

    s->max_size = qemu_chr_be_can_write(chr);
    return s->max_size;
}

static void win_chr_readfile(CharDriverState *chr)
{
    WinCharState *s = chr->opaque;
    int ret, err;
    uint8_t buf[READ_BUF_LEN];
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

static void win_chr_read(CharDriverState *chr)
{
    WinCharState *s = chr->opaque;

    if (s->len > s->max_size)
        s->len = s->max_size;
    if (s->len == 0)
        return;

    win_chr_readfile(chr);
}

static int win_chr_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    WinCharState *s = chr->opaque;
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

static CharDriverState *qemu_chr_open_win_path(const char *filename,
                                               ChardevCommon *backend,
                                               Error **errp)
{
    CharDriverState *chr;
    WinCharState *s;

    chr = qemu_chr_alloc(backend, errp);
    if (!chr) {
        return NULL;
    }
    s = g_new0(WinCharState, 1);
    chr->opaque = s;
    chr->chr_write = win_chr_write;
    chr->chr_close = win_chr_close;

    if (win_chr_init(chr, filename, errp) < 0) {
        g_free(s);
        qemu_chr_free_common(chr);
        return NULL;
    }
    return chr;
}

static int win_chr_pipe_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    WinCharState *s = chr->opaque;
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

static int win_chr_pipe_init(CharDriverState *chr, const char *filename,
                             Error **errp)
{
    WinCharState *s = chr->opaque;
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
    win_chr_close(chr);
    return -1;
}


static CharDriverState *qemu_chr_open_pipe(const char *id,
                                           ChardevBackend *backend,
                                           ChardevReturn *ret,
                                           Error **errp)
{
    ChardevHostdev *opts = backend->u.pipe.data;
    const char *filename = opts->device;
    CharDriverState *chr;
    WinCharState *s;
    ChardevCommon *common = qapi_ChardevHostdev_base(opts);

    chr = qemu_chr_alloc(common, errp);
    if (!chr) {
        return NULL;
    }
    s = g_new0(WinCharState, 1);
    chr->opaque = s;
    chr->chr_write = win_chr_write;
    chr->chr_close = win_chr_close;

    if (win_chr_pipe_init(chr, filename, errp) < 0) {
        g_free(s);
        qemu_chr_free_common(chr);
        return NULL;
    }
    return chr;
}

static CharDriverState *qemu_chr_open_win_file(HANDLE fd_out,
                                               ChardevCommon *backend,
                                               Error **errp)
{
    CharDriverState *chr;
    WinCharState *s;

    chr = qemu_chr_alloc(backend, errp);
    if (!chr) {
        return NULL;
    }
    s = g_new0(WinCharState, 1);
    s->hcom = fd_out;
    chr->opaque = s;
    chr->chr_write = win_chr_write;
    return chr;
}

static CharDriverState *qemu_chr_open_win_con(const char *id,
                                              ChardevBackend *backend,
                                              ChardevReturn *ret,
                                              Error **errp)
{
    ChardevCommon *common = backend->u.console.data;
    return qemu_chr_open_win_file(GetStdHandle(STD_OUTPUT_HANDLE),
                                  common, errp);
}

static int win_stdio_write(CharDriverState *chr, const uint8_t *buf, int len)
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
    CharDriverState   *chr   = opaque;
    WinStdioCharState *stdio = chr->opaque;
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
    CharDriverState   *chr   = param;
    WinStdioCharState *stdio = chr->opaque;
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
    CharDriverState   *chr   = opaque;
    WinStdioCharState *stdio = chr->opaque;

    if (qemu_chr_be_can_write(chr)) {
        qemu_chr_be_write(chr, &stdio->win_stdio_buf, 1);
    }

    SetEvent(stdio->hInputDoneEvent);
}

static void qemu_chr_set_echo_win_stdio(CharDriverState *chr, bool echo)
{
    WinStdioCharState *stdio  = chr->opaque;
    DWORD              dwMode = 0;

    GetConsoleMode(stdio->hStdIn, &dwMode);

    if (echo) {
        SetConsoleMode(stdio->hStdIn, dwMode | ENABLE_ECHO_INPUT);
    } else {
        SetConsoleMode(stdio->hStdIn, dwMode & ~ENABLE_ECHO_INPUT);
    }
}

static void win_stdio_close(CharDriverState *chr)
{
    WinStdioCharState *stdio = chr->opaque;

    if (stdio->hInputReadyEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(stdio->hInputReadyEvent);
    }
    if (stdio->hInputDoneEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(stdio->hInputDoneEvent);
    }
    if (stdio->hInputThread != INVALID_HANDLE_VALUE) {
        TerminateThread(stdio->hInputThread, 0);
    }

    g_free(chr->opaque);
    g_free(chr);
}

static CharDriverState *qemu_chr_open_stdio(const char *id,
                                            ChardevBackend *backend,
                                            ChardevReturn *ret,
                                            Error **errp)
{
    CharDriverState   *chr;
    WinStdioCharState *stdio;
    DWORD              dwMode;
    int                is_console = 0;
    ChardevCommon *common = qapi_ChardevStdio_base(backend->u.stdio.data);

    chr   = qemu_chr_alloc(common, errp);
    if (!chr) {
        return NULL;
    }
    stdio = g_new0(WinStdioCharState, 1);

    stdio->hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    if (stdio->hStdIn == INVALID_HANDLE_VALUE) {
        error_setg(errp, "cannot open stdio: invalid handle");
        return NULL;
    }

    is_console = GetConsoleMode(stdio->hStdIn, &dwMode) != 0;

    chr->opaque    = stdio;
    chr->chr_write = win_stdio_write;
    chr->chr_close = win_stdio_close;

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

    chr->chr_set_echo = qemu_chr_set_echo_win_stdio;
    qemu_chr_fe_set_echo(chr, false);

    return chr;

err3:
    qemu_del_wait_object(stdio->hInputReadyEvent, NULL, NULL);
err2:
    CloseHandle(stdio->hInputReadyEvent);
    CloseHandle(stdio->hInputDoneEvent);
err1:
    qemu_del_wait_object(stdio->hStdIn, NULL, NULL);
    return NULL;
}
#endif /* !_WIN32 */


/***********************************************************/
/* UDP Net console */

typedef struct {
    QIOChannel *ioc;
    uint8_t buf[READ_BUF_LEN];
    int bufcnt;
    int bufptr;
    int max_size;
} NetCharDriver;

/* Called with chr_write_lock held.  */
static int udp_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    NetCharDriver *s = chr->opaque;

    return qio_channel_write(
        s->ioc, (const char *)buf, len, NULL);
}

static int udp_chr_read_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    NetCharDriver *s = chr->opaque;

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
    CharDriverState *chr = opaque;
    NetCharDriver *s = chr->opaque;
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

static void udp_chr_update_read_handler(CharDriverState *chr)
{
    NetCharDriver *s = chr->opaque;

    remove_fd_in_watch(chr);
    if (s->ioc) {
        chr->fd_in_tag = io_add_watch_poll(s->ioc,
                                           udp_chr_read_poll,
                                           udp_chr_read, chr);
    }
}

static void udp_chr_close(CharDriverState *chr)
{
    NetCharDriver *s = chr->opaque;

    remove_fd_in_watch(chr);
    if (s->ioc) {
        object_unref(OBJECT(s->ioc));
    }
    g_free(s);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

static CharDriverState *qemu_chr_open_udp(QIOChannelSocket *sioc,
                                          ChardevCommon *backend,
                                          Error **errp)
{
    CharDriverState *chr = NULL;
    NetCharDriver *s = NULL;

    chr = qemu_chr_alloc(backend, errp);
    if (!chr) {
        return NULL;
    }
    s = g_new0(NetCharDriver, 1);

    s->ioc = QIO_CHANNEL(sioc);
    s->bufcnt = 0;
    s->bufptr = 0;
    chr->opaque = s;
    chr->chr_write = udp_chr_write;
    chr->chr_update_read_handler = udp_chr_update_read_handler;
    chr->chr_close = udp_chr_close;
    /* be isn't opened until we get a connection */
    chr->explicit_be_open = true;
    return chr;
}

/***********************************************************/
/* TCP Net console */

typedef struct {
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
} TCPCharDriver;

static gboolean socket_reconnect_timeout(gpointer opaque);

static void qemu_chr_socket_restart_timer(CharDriverState *chr)
{
    TCPCharDriver *s = chr->opaque;
    assert(s->connected == 0);
    s->reconnect_timer = g_timeout_add_seconds(s->reconnect_time,
                                               socket_reconnect_timeout, chr);
}

static void check_report_connect_error(CharDriverState *chr,
                                       Error *err)
{
    TCPCharDriver *s = chr->opaque;

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
static int tcp_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    TCPCharDriver *s = chr->opaque;
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
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;
    if (!s->connected)
        return 0;
    s->max_size = qemu_chr_be_can_write(chr);
    return s->max_size;
}

#define IAC 255
#define IAC_BREAK 243
static void tcp_chr_process_IAC_bytes(CharDriverState *chr,
                                      TCPCharDriver *s,
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

static int tcp_get_msgfds(CharDriverState *chr, int *fds, int num)
{
    TCPCharDriver *s = chr->opaque;
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

static int tcp_set_msgfds(CharDriverState *chr, int *fds, int num)
{
    TCPCharDriver *s = chr->opaque;

    if (!qio_channel_has_feature(s->ioc,
                                 QIO_CHANNEL_FEATURE_FD_PASS)) {
        return -1;
    }
    /* clear old pending fd array */
    g_free(s->write_msgfds);
    s->write_msgfds = NULL;

    if (num) {
        s->write_msgfds = g_new(int, num);
        memcpy(s->write_msgfds, fds, num * sizeof(int));
    }

    s->write_msgfds_num = num;

    return 0;
}

static ssize_t tcp_chr_recv(CharDriverState *chr, char *buf, size_t len)
{
    TCPCharDriver *s = chr->opaque;
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

static GSource *tcp_chr_add_watch(CharDriverState *chr, GIOCondition cond)
{
    TCPCharDriver *s = chr->opaque;
    return qio_channel_create_watch(s->ioc, cond);
}

static void tcp_chr_disconnect(CharDriverState *chr)
{
    TCPCharDriver *s = chr->opaque;

    if (!s->connected) {
        return;
    }

    s->connected = 0;
    if (s->listen_ioc) {
        s->listen_tag = qio_channel_add_watch(
            QIO_CHANNEL(s->listen_ioc), G_IO_IN, tcp_chr_accept, chr, NULL);
    }
    tcp_set_msgfds(chr, NULL, 0);
    remove_fd_in_watch(chr);
    object_unref(OBJECT(s->sioc));
    s->sioc = NULL;
    object_unref(OBJECT(s->ioc));
    s->ioc = NULL;
    g_free(chr->filename);
    chr->filename = SocketAddress_to_str("disconnected:", s->addr,
                                         s->is_listen, s->is_telnet);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
    if (s->reconnect_time) {
        qemu_chr_socket_restart_timer(chr);
    }
}

static gboolean tcp_chr_read(QIOChannel *chan, GIOCondition cond, void *opaque)
{
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;
    uint8_t buf[READ_BUF_LEN];
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

static int tcp_chr_sync_read(CharDriverState *chr, const uint8_t *buf, int len)
{
    TCPCharDriver *s = chr->opaque;
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
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;

    g_free(chr->filename);
    chr->filename = sockaddr_to_str(
        &s->sioc->localAddr, s->sioc->localAddrLen,
        &s->sioc->remoteAddr, s->sioc->remoteAddrLen,
        s->is_listen, s->is_telnet);

    s->connected = 1;
    if (s->ioc) {
        chr->fd_in_tag = io_add_watch_poll(s->ioc,
                                           tcp_chr_read_poll,
                                           tcp_chr_read, chr);
    }
    qemu_chr_be_generic_open(chr);
}

static void tcp_chr_update_read_handler(CharDriverState *chr)
{
    TCPCharDriver *s = chr->opaque;

    if (!s->connected) {
        return;
    }

    remove_fd_in_watch(chr);
    if (s->ioc) {
        chr->fd_in_tag = io_add_watch_poll(s->ioc,
                                           tcp_chr_read_poll,
                                           tcp_chr_read, chr);
    }
}

typedef struct {
    CharDriverState *chr;
    char buf[12];
    size_t buflen;
} TCPCharDriverTelnetInit;

static gboolean tcp_chr_telnet_init_io(QIOChannel *ioc,
                                       GIOCondition cond G_GNUC_UNUSED,
                                       gpointer user_data)
{
    TCPCharDriverTelnetInit *init = user_data;
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

static void tcp_chr_telnet_init(CharDriverState *chr)
{
    TCPCharDriver *s = chr->opaque;
    TCPCharDriverTelnetInit *init =
        g_new0(TCPCharDriverTelnetInit, 1);
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


static void tcp_chr_tls_handshake(Object *source,
                                  Error *err,
                                  gpointer user_data)
{
    CharDriverState *chr = user_data;
    TCPCharDriver *s = chr->opaque;

    if (err) {
        tcp_chr_disconnect(chr);
    } else {
        if (s->do_telnetopt) {
            tcp_chr_telnet_init(chr);
        } else {
            tcp_chr_connect(chr);
        }
    }
}


static void tcp_chr_tls_init(CharDriverState *chr)
{
    TCPCharDriver *s = chr->opaque;
    QIOChannelTLS *tioc;
    Error *err = NULL;

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
    }
    object_unref(OBJECT(s->ioc));
    s->ioc = QIO_CHANNEL(tioc);

    qio_channel_tls_handshake(tioc,
                              tcp_chr_tls_handshake,
                              chr,
                              NULL);
}


static int tcp_chr_new_client(CharDriverState *chr, QIOChannelSocket *sioc)
{
    TCPCharDriver *s = chr->opaque;
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


static int tcp_chr_add_client(CharDriverState *chr, int fd)
{
    int ret;
    QIOChannelSocket *sioc;

    sioc = qio_channel_socket_new_fd(fd, NULL);
    if (!sioc) {
        return -1;
    }
    ret = tcp_chr_new_client(chr, sioc);
    object_unref(OBJECT(sioc));
    return ret;
}

static gboolean tcp_chr_accept(QIOChannel *channel,
                               GIOCondition cond,
                               void *opaque)
{
    CharDriverState *chr = opaque;
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

static void tcp_chr_close(CharDriverState *chr)
{
    TCPCharDriver *s = chr->opaque;
    int i;

    if (s->reconnect_timer) {
        g_source_remove(s->reconnect_timer);
        s->reconnect_timer = 0;
    }
    qapi_free_SocketAddress(s->addr);
    remove_fd_in_watch(chr);
    if (s->ioc) {
        object_unref(OBJECT(s->ioc));
    }
    if (s->listen_tag) {
        g_source_remove(s->listen_tag);
        s->listen_tag = 0;
    }
    if (s->listen_ioc) {
        object_unref(OBJECT(s->listen_ioc));
    }
    if (s->read_msgfds_num) {
        for (i = 0; i < s->read_msgfds_num; i++) {
            close(s->read_msgfds[i]);
        }
        g_free(s->read_msgfds);
    }
    if (s->tls_creds) {
        object_unref(OBJECT(s->tls_creds));
    }
    if (s->write_msgfds_num) {
        g_free(s->write_msgfds);
    }
    g_free(s);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}


static void qemu_chr_socket_connected(Object *src, Error *err, void *opaque)
{
    QIOChannelSocket *sioc = QIO_CHANNEL_SOCKET(src);
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;

    if (err) {
        check_report_connect_error(chr, err);
        object_unref(src);
        return;
    }

    s->connect_err_reported = false;
    tcp_chr_new_client(chr, sioc);
    object_unref(OBJECT(sioc));
}


/*********************************************************/
/* Ring buffer chardev */

typedef struct {
    size_t size;
    size_t prod;
    size_t cons;
    uint8_t *cbuf;
} RingBufCharDriver;

static size_t ringbuf_count(const CharDriverState *chr)
{
    const RingBufCharDriver *d = chr->opaque;

    return d->prod - d->cons;
}

/* Called with chr_write_lock held.  */
static int ringbuf_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    RingBufCharDriver *d = chr->opaque;
    int i;

    if (!buf || (len < 0)) {
        return -1;
    }

    for (i = 0; i < len; i++ ) {
        d->cbuf[d->prod++ & (d->size - 1)] = buf[i];
        if (d->prod - d->cons > d->size) {
            d->cons = d->prod - d->size;
        }
    }

    return 0;
}

static int ringbuf_chr_read(CharDriverState *chr, uint8_t *buf, int len)
{
    RingBufCharDriver *d = chr->opaque;
    int i;

    qemu_mutex_lock(&chr->chr_write_lock);
    for (i = 0; i < len && d->cons != d->prod; i++) {
        buf[i] = d->cbuf[d->cons++ & (d->size - 1)];
    }
    qemu_mutex_unlock(&chr->chr_write_lock);

    return i;
}

static void ringbuf_chr_close(struct CharDriverState *chr)
{
    RingBufCharDriver *d = chr->opaque;

    g_free(d->cbuf);
    g_free(d);
    chr->opaque = NULL;
}

static CharDriverState *qemu_chr_open_ringbuf(const char *id,
                                              ChardevBackend *backend,
                                              ChardevReturn *ret,
                                              Error **errp)
{
    ChardevRingbuf *opts = backend->u.ringbuf.data;
    ChardevCommon *common = qapi_ChardevRingbuf_base(opts);
    CharDriverState *chr;
    RingBufCharDriver *d;

    chr = qemu_chr_alloc(common, errp);
    if (!chr) {
        return NULL;
    }
    d = g_malloc(sizeof(*d));

    d->size = opts->has_size ? opts->size : 65536;

    /* The size must be power of 2 */
    if (d->size & (d->size - 1)) {
        error_setg(errp, "size of ringbuf chardev must be power of two");
        goto fail;
    }

    d->prod = 0;
    d->cons = 0;
    d->cbuf = g_malloc0(d->size);

    chr->opaque = d;
    chr->chr_write = ringbuf_chr_write;
    chr->chr_close = ringbuf_chr_close;

    return chr;

fail:
    g_free(d);
    qemu_chr_free_common(chr);
    return NULL;
}

bool chr_is_ringbuf(const CharDriverState *chr)
{
    return chr->chr_write == ringbuf_chr_write;
}

void qmp_ringbuf_write(const char *device, const char *data,
                       bool has_format, enum DataFormat format,
                       Error **errp)
{
    CharDriverState *chr;
    const uint8_t *write_data;
    int ret;
    gsize write_count;

    chr = qemu_chr_find(device);
    if (!chr) {
        error_setg(errp, "Device '%s' not found", device);
        return;
    }

    if (!chr_is_ringbuf(chr)) {
        error_setg(errp,"%s is not a ringbuf device", device);
        return;
    }

    if (has_format && (format == DATA_FORMAT_BASE64)) {
        write_data = qbase64_decode(data, -1,
                                    &write_count,
                                    errp);
        if (!write_data) {
            return;
        }
    } else {
        write_data = (uint8_t *)data;
        write_count = strlen(data);
    }

    ret = ringbuf_chr_write(chr, write_data, write_count);

    if (write_data != (uint8_t *)data) {
        g_free((void *)write_data);
    }

    if (ret < 0) {
        error_setg(errp, "Failed to write to device %s", device);
        return;
    }
}

char *qmp_ringbuf_read(const char *device, int64_t size,
                       bool has_format, enum DataFormat format,
                       Error **errp)
{
    CharDriverState *chr;
    uint8_t *read_data;
    size_t count;
    char *data;

    chr = qemu_chr_find(device);
    if (!chr) {
        error_setg(errp, "Device '%s' not found", device);
        return NULL;
    }

    if (!chr_is_ringbuf(chr)) {
        error_setg(errp,"%s is not a ringbuf device", device);
        return NULL;
    }

    if (size <= 0) {
        error_setg(errp, "size must be greater than zero");
        return NULL;
    }

    count = ringbuf_count(chr);
    size = size > count ? count : size;
    read_data = g_malloc(size + 1);

    ringbuf_chr_read(chr, read_data, size);

    if (has_format && (format == DATA_FORMAT_BASE64)) {
        data = g_base64_encode(read_data, size);
        g_free(read_data);
    } else {
        /*
         * FIXME should read only complete, valid UTF-8 characters up
         * to @size bytes.  Invalid sequences should be replaced by a
         * suitable replacement character.  Except when (and only
         * when) ring buffer lost characters since last read, initial
         * continuation characters should be dropped.
         */
        read_data[size] = 0;
        data = (char *)read_data;
    }

    return data;
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

    stdio = backend->u.stdio.data = g_new0(ChardevStdio, 1);
    qemu_chr_parse_common(opts, qapi_ChardevStdio_base(stdio));
    stdio->has_signal = true;
    stdio->signal = qemu_opt_get_bool(opts, "signal", true);
}

#ifdef HAVE_CHARDEV_SERIAL
static void qemu_chr_parse_serial(QemuOpts *opts, ChardevBackend *backend,
                                  Error **errp)
{
    const char *device = qemu_opt_get(opts, "path");
    ChardevHostdev *serial;

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

    if (device == NULL) {
        error_setg(errp, "chardev: pipe: no device path given");
        return;
    }
    dev = backend->u.pipe.data = g_new0(ChardevHostdev, 1);
    qemu_chr_parse_common(opts, qapi_ChardevHostdev_base(dev));
    dev->device = g_strdup(device);
}

static void qemu_chr_parse_ringbuf(QemuOpts *opts, ChardevBackend *backend,
                                   Error **errp)
{
    int val;
    ChardevRingbuf *ringbuf;

    ringbuf = backend->u.ringbuf.data = g_new0(ChardevRingbuf, 1);
    qemu_chr_parse_common(opts, qapi_ChardevRingbuf_base(ringbuf));

    val = qemu_opt_get_size(opts, "size", 0);
    if (val != 0) {
        ringbuf->has_size = true;
        ringbuf->size = val;
    }
}

static void qemu_chr_parse_mux(QemuOpts *opts, ChardevBackend *backend,
                               Error **errp)
{
    const char *chardev = qemu_opt_get(opts, "chardev");
    ChardevMux *mux;

    if (chardev == NULL) {
        error_setg(errp, "chardev: mux: no chardev given");
        return;
    }
    mux = backend->u.mux.data = g_new0(ChardevMux, 1);
    qemu_chr_parse_common(opts, qapi_ChardevMux_base(mux));
    mux->chardev = g_strdup(chardev);
}

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

typedef struct CharDriver {
    const char *name;
    ChardevBackendKind kind;
    void (*parse)(QemuOpts *opts, ChardevBackend *backend, Error **errp);
    CharDriverState *(*create)(const char *id, ChardevBackend *backend,
                               ChardevReturn *ret, Error **errp);
} CharDriver;

static GSList *backends;

void register_char_driver(const char *name, ChardevBackendKind kind,
        void (*parse)(QemuOpts *opts, ChardevBackend *backend, Error **errp),
        CharDriverState *(*create)(const char *id, ChardevBackend *backend,
                                   ChardevReturn *ret, Error **errp))
{
    CharDriver *s;

    s = g_malloc0(sizeof(*s));
    s->name = g_strdup(name);
    s->kind = kind;
    s->parse = parse;
    s->create = create;

    backends = g_slist_append(backends, s);
}

CharDriverState *qemu_chr_new_from_opts(QemuOpts *opts,
                                    void (*init)(struct CharDriverState *s),
                                    Error **errp)
{
    Error *local_err = NULL;
    CharDriver *cd;
    CharDriverState *chr;
    GSList *i;
    ChardevReturn *ret = NULL;
    ChardevBackend *backend;
    const char *id = qemu_opts_id(opts);
    char *bid = NULL;

    if (id == NULL) {
        error_setg(errp, "chardev: no id specified");
        goto err;
    }

    if (qemu_opt_get(opts, "backend") == NULL) {
        error_setg(errp, "chardev: \"%s\" missing backend",
                   qemu_opts_id(opts));
        goto err;
    }
    for (i = backends; i; i = i->next) {
        cd = i->data;

        if (strcmp(cd->name, qemu_opt_get(opts, "backend")) == 0) {
            break;
        }
    }
    if (i == NULL) {
        error_setg(errp, "chardev: backend \"%s\" not found",
                   qemu_opt_get(opts, "backend"));
        goto err;
    }

    backend = g_new0(ChardevBackend, 1);

    if (qemu_opt_get_bool(opts, "mux", 0)) {
        bid = g_strdup_printf("%s-base", id);
    }

    chr = NULL;
    backend->type = cd->kind;
    if (cd->parse) {
        cd->parse(opts, backend, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            goto qapi_out;
        }
    } else {
        ChardevCommon *cc = g_new0(ChardevCommon, 1);
        qemu_chr_parse_common(opts, cc);
        backend->u.null.data = cc; /* Any ChardevCommon member would work */
    }

    ret = qmp_chardev_add(bid ? bid : id, backend, errp);
    if (!ret) {
        goto qapi_out;
    }

    if (bid) {
        qapi_free_ChardevBackend(backend);
        qapi_free_ChardevReturn(ret);
        backend = g_new0(ChardevBackend, 1);
        backend->u.mux.data = g_new0(ChardevMux, 1);
        backend->type = CHARDEV_BACKEND_KIND_MUX;
        backend->u.mux.data->chardev = g_strdup(bid);
        ret = qmp_chardev_add(id, backend, errp);
        if (!ret) {
            chr = qemu_chr_find(bid);
            qemu_chr_delete(chr);
            chr = NULL;
            goto qapi_out;
        }
    }

    chr = qemu_chr_find(id);
    chr->opts = opts;

qapi_out:
    qapi_free_ChardevBackend(backend);
    qapi_free_ChardevReturn(ret);
    g_free(bid);
    return chr;

err:
    qemu_opts_del(opts);
    return NULL;
}

CharDriverState *qemu_chr_new_noreplay(const char *label, const char *filename,
                                       void (*init)(struct CharDriverState *s))
{
    const char *p;
    CharDriverState *chr;
    QemuOpts *opts;
    Error *err = NULL;

    if (strstart(filename, "chardev:", &p)) {
        return qemu_chr_find(p);
    }

    opts = qemu_chr_parse_compat(label, filename);
    if (!opts)
        return NULL;

    chr = qemu_chr_new_from_opts(opts, init, &err);
    if (err) {
        error_report_err(err);
    }
    if (chr && qemu_opt_get_bool(opts, "mux", 0)) {
        qemu_chr_fe_claim_no_fail(chr);
        monitor_init(chr, MONITOR_USE_READLINE);
    }
    return chr;
}

CharDriverState *qemu_chr_new(const char *label, const char *filename, void (*init)(struct CharDriverState *s))
{
    CharDriverState *chr;
    chr = qemu_chr_new_noreplay(label, filename, init);
    if (chr) {
        chr->replay = replay_mode != REPLAY_MODE_NONE;
        if (chr->replay && chr->chr_ioctl) {
            fprintf(stderr,
                    "Replay: ioctl is not supported for serial devices yet\n");
        }
        replay_register_char_driver(chr);
    }
    return chr;
}

void qemu_chr_fe_set_echo(struct CharDriverState *chr, bool echo)
{
    if (chr->chr_set_echo) {
        chr->chr_set_echo(chr, echo);
    }
}

void qemu_chr_fe_set_open(struct CharDriverState *chr, int fe_open)
{
    if (chr->fe_open == fe_open) {
        return;
    }
    chr->fe_open = fe_open;
    if (chr->chr_set_fe_open) {
        chr->chr_set_fe_open(chr, fe_open);
    }
}

void qemu_chr_fe_event(struct CharDriverState *chr, int event)
{
    if (chr->chr_fe_event) {
        chr->chr_fe_event(chr, event);
    }
}

int qemu_chr_fe_add_watch(CharDriverState *s, GIOCondition cond,
                          GIOFunc func, void *user_data)
{
    GSource *src;
    guint tag;

    if (s->chr_add_watch == NULL) {
        return -ENOSYS;
    }

    src = s->chr_add_watch(s, cond);
    if (!src) {
        return -EINVAL;
    }

    g_source_set_callback(src, (GSourceFunc)func, user_data, NULL);
    tag = g_source_attach(src, NULL);
    g_source_unref(src);

    return tag;
}

int qemu_chr_fe_claim(CharDriverState *s)
{
    if (s->avail_connections < 1) {
        return -1;
    }
    s->avail_connections--;
    return 0;
}

void qemu_chr_fe_claim_no_fail(CharDriverState *s)
{
    if (qemu_chr_fe_claim(s) != 0) {
        fprintf(stderr, "%s: error chardev \"%s\" already used\n",
                __func__, s->label);
        exit(1);
    }
}

void qemu_chr_fe_release(CharDriverState *s)
{
    s->avail_connections++;
}

void qemu_chr_disconnect(CharDriverState *chr)
{
    if (chr->chr_disconnect) {
        chr->chr_disconnect(chr);
    }
}

static void qemu_chr_free_common(CharDriverState *chr)
{
    g_free(chr->filename);
    g_free(chr->label);
    qemu_opts_del(chr->opts);
    if (chr->logfd != -1) {
        close(chr->logfd);
    }
    qemu_mutex_destroy(&chr->chr_write_lock);
    g_free(chr);
}

void qemu_chr_free(CharDriverState *chr)
{
    if (chr->chr_close) {
        chr->chr_close(chr);
    }
    qemu_chr_free_common(chr);
}

void qemu_chr_delete(CharDriverState *chr)
{
    QTAILQ_REMOVE(&chardevs, chr, next);
    qemu_chr_free(chr);
}

ChardevInfoList *qmp_query_chardev(Error **errp)
{
    ChardevInfoList *chr_list = NULL;
    CharDriverState *chr;

    QTAILQ_FOREACH(chr, &chardevs, next) {
        ChardevInfoList *info = g_malloc0(sizeof(*info));
        info->value = g_malloc0(sizeof(*info->value));
        info->value->label = g_strdup(chr->label);
        info->value->filename = g_strdup(chr->filename);
        info->value->frontend_open = chr->fe_open;

        info->next = chr_list;
        chr_list = info;
    }

    return chr_list;
}

ChardevBackendInfoList *qmp_query_chardev_backends(Error **errp)
{
    ChardevBackendInfoList *backend_list = NULL;
    CharDriver *c = NULL;
    GSList *i = NULL;

    for (i = backends; i; i = i->next) {
        ChardevBackendInfoList *info = g_malloc0(sizeof(*info));
        c = i->data;
        info->value = g_malloc0(sizeof(*info->value));
        info->value->name = g_strdup(c->name);

        info->next = backend_list;
        backend_list = info;
    }

    return backend_list;
}

CharDriverState *qemu_chr_find(const char *name)
{
    CharDriverState *chr;

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

static CharDriverState *qmp_chardev_open_file(const char *id,
                                              ChardevBackend *backend,
                                              ChardevReturn *ret,
                                              Error **errp)
{
    ChardevFile *file = backend->u.file.data;
    ChardevCommon *common = qapi_ChardevFile_base(file);
    HANDLE out;

    if (file->has_in) {
        error_setg(errp, "input file not supported");
        return NULL;
    }

    out = CreateFile(file->out, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                     OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) {
        error_setg(errp, "open %s failed", file->out);
        return NULL;
    }
    return qemu_chr_open_win_file(out, common, errp);
}

static CharDriverState *qmp_chardev_open_serial(const char *id,
                                                ChardevBackend *backend,
                                                ChardevReturn *ret,
                                                Error **errp)
{
    ChardevHostdev *serial = backend->u.serial.data;
    ChardevCommon *common = qapi_ChardevHostdev_base(serial);
    return qemu_chr_open_win_path(serial->device, common, errp);
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

static CharDriverState *qmp_chardev_open_file(const char *id,
                                              ChardevBackend *backend,
                                              ChardevReturn *ret,
                                              Error **errp)
{
    ChardevFile *file = backend->u.file.data;
    ChardevCommon *common = qapi_ChardevFile_base(file);
    int flags, in = -1, out;

    flags = O_WRONLY | O_CREAT | O_BINARY;
    if (file->has_append && file->append) {
        flags |= O_APPEND;
    } else {
        flags |= O_TRUNC;
    }

    out = qmp_chardev_open_file_source(file->out, flags, errp);
    if (out < 0) {
        return NULL;
    }

    if (file->has_in) {
        flags = O_RDONLY;
        in = qmp_chardev_open_file_source(file->in, flags, errp);
        if (in < 0) {
            qemu_close(out);
            return NULL;
        }
    }

    return qemu_chr_open_fd(in, out, common, errp);
}

#ifdef HAVE_CHARDEV_SERIAL
static CharDriverState *qmp_chardev_open_serial(const char *id,
                                                ChardevBackend *backend,
                                                ChardevReturn *ret,
                                                Error **errp)
{
    ChardevHostdev *serial = backend->u.serial.data;
    ChardevCommon *common = qapi_ChardevHostdev_base(serial);
    int fd;

    fd = qmp_chardev_open_file_source(serial->device, O_RDWR, errp);
    if (fd < 0) {
        return NULL;
    }
    qemu_set_nonblock(fd);
    return qemu_chr_open_tty_fd(fd, common, errp);
}
#endif

#ifdef HAVE_CHARDEV_PARPORT
static CharDriverState *qmp_chardev_open_parallel(const char *id,
                                                  ChardevBackend *backend,
                                                  ChardevReturn *ret,
                                                  Error **errp)
{
    ChardevHostdev *parallel = backend->u.parallel.data;
    ChardevCommon *common = qapi_ChardevHostdev_base(parallel);
    int fd;

    fd = qmp_chardev_open_file_source(parallel->device, O_RDWR, errp);
    if (fd < 0) {
        return NULL;
    }
    return qemu_chr_open_pp_fd(fd, common, errp);
}
#endif

#endif /* WIN32 */

static gboolean socket_reconnect_timeout(gpointer opaque)
{
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;
    QIOChannelSocket *sioc;

    s->reconnect_timer = 0;

    if (chr->be_open) {
        return false;
    }

    sioc = qio_channel_socket_new();
    qio_channel_socket_connect_async(sioc, s->addr,
                                     qemu_chr_socket_connected,
                                     chr, NULL);

    return false;
}

static CharDriverState *qmp_chardev_open_socket(const char *id,
                                                ChardevBackend *backend,
                                                ChardevReturn *ret,
                                                Error **errp)
{
    CharDriverState *chr;
    TCPCharDriver *s;
    ChardevSocket *sock = backend->u.socket.data;
    SocketAddress *addr = sock->addr;
    bool do_nodelay     = sock->has_nodelay ? sock->nodelay : false;
    bool is_listen      = sock->has_server  ? sock->server  : true;
    bool is_telnet      = sock->has_telnet  ? sock->telnet  : false;
    bool is_waitconnect = sock->has_wait    ? sock->wait    : false;
    int64_t reconnect   = sock->has_reconnect ? sock->reconnect : 0;
    ChardevCommon *common = qapi_ChardevSocket_base(sock);
    QIOChannelSocket *sioc = NULL;

    chr = qemu_chr_alloc(common, errp);
    if (!chr) {
        return NULL;
    }
    s = g_new0(TCPCharDriver, 1);

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

    qapi_copy_SocketAddress(&s->addr, sock->addr);

    chr->opaque = s;
    chr->chr_write = tcp_chr_write;
    chr->chr_sync_read = tcp_chr_sync_read;
    chr->chr_close = tcp_chr_close;
    chr->chr_disconnect = tcp_chr_disconnect;
    chr->get_msgfds = tcp_get_msgfds;
    chr->set_msgfds = tcp_set_msgfds;
    chr->chr_add_client = tcp_chr_add_client;
    chr->chr_add_watch = tcp_chr_add_watch;
    chr->chr_update_read_handler = tcp_chr_update_read_handler;
    /* be isn't opened until we get a connection */
    chr->explicit_be_open = true;

    chr->filename = SocketAddress_to_str("disconnected:",
                                         addr, is_listen, is_telnet);

    if (is_listen) {
        if (is_telnet) {
            s->do_telnetopt = 1;
        }
    } else if (reconnect > 0) {
        s->reconnect_time = reconnect;
    }

    sioc = qio_channel_socket_new();
    if (s->reconnect_time) {
        qio_channel_socket_connect_async(sioc, s->addr,
                                         qemu_chr_socket_connected,
                                         chr, NULL);
    } else if (s->is_listen) {
        if (qio_channel_socket_listen_sync(sioc, s->addr, errp) < 0) {
            goto error;
        }
        s->listen_ioc = sioc;
        if (is_waitconnect) {
            fprintf(stderr, "QEMU waiting for connection on: %s\n",
                    chr->filename);
            tcp_chr_accept(QIO_CHANNEL(s->listen_ioc), G_IO_IN, chr);
        }
        qio_channel_set_blocking(QIO_CHANNEL(s->listen_ioc), false, NULL);
        if (!s->ioc) {
            s->listen_tag = qio_channel_add_watch(
                QIO_CHANNEL(s->listen_ioc), G_IO_IN, tcp_chr_accept, chr, NULL);
        }
    } else {
        if (qio_channel_socket_connect_sync(sioc, s->addr, errp) < 0) {
            goto error;
        }
        tcp_chr_new_client(chr, sioc);
        object_unref(OBJECT(sioc));
    }

    return chr;

 error:
    if (sioc) {
        object_unref(OBJECT(sioc));
    }
    if (s->tls_creds) {
        object_unref(OBJECT(s->tls_creds));
    }
    g_free(s);
    qemu_chr_free_common(chr);
    return NULL;
}

static CharDriverState *qmp_chardev_open_udp(const char *id,
                                             ChardevBackend *backend,
                                             ChardevReturn *ret,
                                             Error **errp)
{
    ChardevUdp *udp = backend->u.udp.data;
    ChardevCommon *common = qapi_ChardevUdp_base(udp);
    QIOChannelSocket *sioc = qio_channel_socket_new();

    if (qio_channel_socket_dgram_sync(sioc,
                                      udp->local, udp->remote,
                                      errp) < 0) {
        object_unref(OBJECT(sioc));
        return NULL;
    }
    return qemu_chr_open_udp(sioc, common, errp);
}

ChardevReturn *qmp_chardev_add(const char *id, ChardevBackend *backend,
                               Error **errp)
{
    ChardevReturn *ret = g_new0(ChardevReturn, 1);
    CharDriverState *chr = NULL;
    Error *local_err = NULL;
    GSList *i;
    CharDriver *cd;

    chr = qemu_chr_find(id);
    if (chr) {
        error_setg(errp, "Chardev '%s' already exists", id);
        g_free(ret);
        return NULL;
    }

    for (i = backends; i; i = i->next) {
        cd = i->data;

        if (cd->kind == backend->type) {
            chr = cd->create(id, backend, ret, &local_err);
            if (local_err) {
                error_propagate(errp, local_err);
                goto out_error;
            }
            break;
        }
    }

    if (chr == NULL) {
        assert(!i);
        error_setg(errp, "chardev backend not available");
        goto out_error;
    }

    chr->label = g_strdup(id);
    chr->avail_connections =
        (backend->type == CHARDEV_BACKEND_KIND_MUX) ? MAX_MUX : 1;
    if (!chr->filename) {
        chr->filename = g_strdup(ChardevBackendKind_lookup[backend->type]);
    }
    if (!chr->explicit_be_open) {
        qemu_chr_be_event(chr, CHR_EVENT_OPENED);
    }
    QTAILQ_INSERT_TAIL(&chardevs, chr, next);
    return ret;

out_error:
    g_free(ret);
    return NULL;
}

void qmp_chardev_remove(const char *id, Error **errp)
{
    CharDriverState *chr;

    chr = qemu_chr_find(id);
    if (chr == NULL) {
        error_setg(errp, "Chardev '%s' not found", id);
        return;
    }
    if (chr->chr_can_read || chr->chr_read ||
        chr->chr_event || chr->handler_opaque) {
        error_setg(errp, "Chardev '%s' is busy", id);
        return;
    }
    if (chr->replay) {
        error_setg(errp,
            "Chardev '%s' cannot be unplugged in record/replay mode", id);
        return;
    }
    qemu_chr_delete(chr);
}

static void register_types(void)
{
    register_char_driver("null", CHARDEV_BACKEND_KIND_NULL, NULL,
                         qemu_chr_open_null);
    register_char_driver("socket", CHARDEV_BACKEND_KIND_SOCKET,
                         qemu_chr_parse_socket, qmp_chardev_open_socket);
    register_char_driver("udp", CHARDEV_BACKEND_KIND_UDP, qemu_chr_parse_udp,
                         qmp_chardev_open_udp);
    register_char_driver("ringbuf", CHARDEV_BACKEND_KIND_RINGBUF,
                         qemu_chr_parse_ringbuf, qemu_chr_open_ringbuf);
    register_char_driver("file", CHARDEV_BACKEND_KIND_FILE,
                         qemu_chr_parse_file_out, qmp_chardev_open_file);
    register_char_driver("stdio", CHARDEV_BACKEND_KIND_STDIO,
                         qemu_chr_parse_stdio, qemu_chr_open_stdio);
#if defined HAVE_CHARDEV_SERIAL
    register_char_driver("serial", CHARDEV_BACKEND_KIND_SERIAL,
                         qemu_chr_parse_serial, qmp_chardev_open_serial);
    register_char_driver("tty", CHARDEV_BACKEND_KIND_SERIAL,
                         qemu_chr_parse_serial, qmp_chardev_open_serial);
#endif
#ifdef HAVE_CHARDEV_PARPORT
    register_char_driver("parallel", CHARDEV_BACKEND_KIND_PARALLEL,
                         qemu_chr_parse_parallel, qmp_chardev_open_parallel);
    register_char_driver("parport", CHARDEV_BACKEND_KIND_PARALLEL,
                         qemu_chr_parse_parallel, qmp_chardev_open_parallel);
#endif
#ifdef HAVE_CHARDEV_PTY
    register_char_driver("pty", CHARDEV_BACKEND_KIND_PTY, NULL,
                         qemu_chr_open_pty);
#endif
#ifdef _WIN32
    register_char_driver("console", CHARDEV_BACKEND_KIND_CONSOLE, NULL,
                         qemu_chr_open_win_con);
#endif
    register_char_driver("pipe", CHARDEV_BACKEND_KIND_PIPE,
                         qemu_chr_parse_pipe, qemu_chr_open_pipe);
    register_char_driver("mux", CHARDEV_BACKEND_KIND_MUX, qemu_chr_parse_mux,
                         qemu_chr_open_mux);
    /* Bug-compatibility: */
    register_char_driver("memory", CHARDEV_BACKEND_KIND_MEMORY,
                         qemu_chr_parse_ringbuf, qemu_chr_open_ringbuf);
    /* this must be done after machine init, since we register FEs with muxes
     * as part of realize functions like serial_isa_realizefn when -nographic
     * is specified
     */
    qemu_add_machine_init_done_notifier(&muxes_realize_notify);
}

type_init(register_types);
