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
#include "qemu-common.h"
#include "monitor/monitor.h"
#include "sysemu/sysemu.h"
#include "qemu/timer.h"
#include "sysemu/char.h"
#include "hw/usb.h"
#include "qmp-commands.h"

#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <zlib.h>

#ifndef _WIN32
#include <sys/times.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/select.h>
#ifdef CONFIG_BSD
#include <sys/stat.h>
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
#include <sys/stat.h>
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

/***********************************************************/
/* character device */

static QTAILQ_HEAD(CharDriverStateHead, CharDriverState) chardevs =
    QTAILQ_HEAD_INITIALIZER(chardevs);

CharDriverState *qemu_chr_alloc(void)
{
    CharDriverState *chr = g_malloc0(sizeof(CharDriverState));
    qemu_mutex_init(&chr->chr_write_lock);
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

int qemu_chr_fe_write(CharDriverState *s, const uint8_t *buf, int len)
{
    int ret;

    qemu_mutex_lock(&s->chr_write_lock);
    ret = s->chr_write(s, buf, len);
    qemu_mutex_unlock(&s->chr_write_lock);
    return ret;
}

int qemu_chr_fe_write_all(CharDriverState *s, const uint8_t *buf, int len)
{
    int offset = 0;
    int res = 0;

    qemu_mutex_lock(&s->chr_write_lock);
    while (offset < len) {
        do {
            res = s->chr_write(s, buf + offset, len - offset);
            if (res == -1 && errno == EAGAIN) {
                g_usleep(100);
            }
        } while (res == -1 && errno == EAGAIN);

        if (res <= 0) {
            break;
        }

        offset += res;
    }
    qemu_mutex_unlock(&s->chr_write_lock);

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

    while (offset < len) {
        do {
            res = s->chr_sync_read(s, buf + offset, len - offset);
            if (res == -1 && errno == EAGAIN) {
                g_usleep(100);
            }
        } while (res == -1 && errno == EAGAIN);

        if (res == 0) {
            break;
        }

        if (res < 0) {
            return res;
        }

        offset += res;

        if (!counter--) {
            break;
        }
    }

    return offset;
}

int qemu_chr_fe_ioctl(CharDriverState *s, int cmd, void *arg)
{
    if (!s->chr_ioctl)
        return -ENOTSUP;
    return s->chr_ioctl(s, cmd, arg);
}

int qemu_chr_be_can_write(CharDriverState *s)
{
    if (!s->chr_can_read)
        return 0;
    return s->chr_can_read(s->handler_opaque);
}

void qemu_chr_be_write(CharDriverState *s, uint8_t *buf, int len)
{
    if (s->chr_read) {
        s->chr_read(s->handler_opaque, buf, len);
    }
}

int qemu_chr_fe_get_msgfd(CharDriverState *s)
{
    int fd;
    return (qemu_chr_fe_get_msgfds(s, &fd, 1) == 1) ? fd : -1;
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

static CharDriverState *qemu_chr_open_null(void)
{
    CharDriverState *chr;

    chr = qemu_chr_alloc();
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
    "% t    toggle console timestamps\n\r"
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
            bdrv_commit_all();
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

static CharDriverState *qemu_chr_open_mux(CharDriverState *drv)
{
    CharDriverState *chr;
    MuxDriver *d;

    chr = qemu_chr_alloc();
    d = g_malloc0(sizeof(MuxDriver));

    chr->opaque = d;
    d->drv = drv;
    d->focus = -1;
    chr->chr_write = mux_chr_write;
    chr->chr_update_read_handler = mux_chr_update_read_handler;
    chr->chr_accept_input = mux_chr_accept_input;
    /* Frontend guest-open / -close notification is not support with muxes */
    chr->chr_set_fe_open = NULL;
    /* only default to opened state if we've realized the initial
     * set of muxes
     */
    chr->explicit_be_open = muxes_realized ? 0 : 1;
    chr->is_mux = 1;

    return chr;
}


#ifdef _WIN32
int send_all(int fd, const void *buf, int len1)
{
    int ret, len;

    len = len1;
    while (len > 0) {
        ret = send(fd, buf, len, 0);
        if (ret < 0) {
            errno = WSAGetLastError();
            if (errno != WSAEWOULDBLOCK) {
                return -1;
            }
        } else if (ret == 0) {
            break;
        } else {
            buf += ret;
            len -= ret;
        }
    }
    return len1 - len;
}

#else

int send_all(int fd, const void *_buf, int len1)
{
    int ret, len;
    const uint8_t *buf = _buf;

    len = len1;
    while (len > 0) {
        ret = write(fd, buf, len);
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN)
                return -1;
        } else if (ret == 0) {
            break;
        } else {
            buf += ret;
            len -= ret;
        }
    }
    return len1 - len;
}

int recv_all(int fd, void *_buf, int len1, bool single_read)
{
    int ret, len;
    uint8_t *buf = _buf;

    len = len1;
    while ((len > 0) && (ret = read(fd, buf, len)) != 0) {
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                return -1;
            }
            continue;
        } else {
            if (single_read) {
                return ret;
            }
            buf += ret;
            len -= ret;
        }
    }
    return len1 - len;
}

#endif /* !_WIN32 */

typedef struct IOWatchPoll
{
    GSource parent;

    GIOChannel *channel;
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
        iwp->src = g_io_create_watch(iwp->channel, G_IO_IN | G_IO_ERR | G_IO_HUP);
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
static guint io_add_watch_poll(GIOChannel *channel,
                               IOCanReadHandler *fd_can_read,
                               GIOFunc fd_read,
                               gpointer user_data)
{
    IOWatchPoll *iwp;
    int tag;

    iwp = (IOWatchPoll *) g_source_new(&io_watch_poll_funcs, sizeof(IOWatchPoll));
    iwp->fd_can_read = fd_can_read;
    iwp->opaque = user_data;
    iwp->channel = channel;
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

#ifndef _WIN32
static GIOChannel *io_channel_from_fd(int fd)
{
    GIOChannel *chan;

    if (fd == -1) {
        return NULL;
    }

    chan = g_io_channel_unix_new(fd);

    g_io_channel_set_encoding(chan, NULL, NULL);
    g_io_channel_set_buffered(chan, FALSE);

    return chan;
}
#endif

static GIOChannel *io_channel_from_socket(int fd)
{
    GIOChannel *chan;

    if (fd == -1) {
        return NULL;
    }

#ifdef _WIN32
    chan = g_io_channel_win32_new_socket(fd);
#else
    chan = g_io_channel_unix_new(fd);
#endif

    g_io_channel_set_encoding(chan, NULL, NULL);
    g_io_channel_set_buffered(chan, FALSE);

    return chan;
}

static int io_channel_send(GIOChannel *fd, const void *buf, size_t len)
{
    size_t offset = 0;
    GIOStatus status = G_IO_STATUS_NORMAL;

    while (offset < len && status == G_IO_STATUS_NORMAL) {
        gsize bytes_written = 0;

        status = g_io_channel_write_chars(fd, buf + offset, len - offset,
                                          &bytes_written, NULL);
        offset += bytes_written;
    }

    if (offset > 0) {
        return offset;
    }
    switch (status) {
    case G_IO_STATUS_NORMAL:
        g_assert(len == 0);
        return 0;
    case G_IO_STATUS_AGAIN:
        errno = EAGAIN;
        return -1;
    default:
        break;
    }
    errno = EINVAL;
    return -1;
}

#ifndef _WIN32

typedef struct FDCharDriver {
    CharDriverState *chr;
    GIOChannel *fd_in, *fd_out;
    int max_size;
    QTAILQ_ENTRY(FDCharDriver) node;
} FDCharDriver;

/* Called with chr_write_lock held.  */
static int fd_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    FDCharDriver *s = chr->opaque;
    
    return io_channel_send(s->fd_out, buf, len);
}

static gboolean fd_chr_read(GIOChannel *chan, GIOCondition cond, void *opaque)
{
    CharDriverState *chr = opaque;
    FDCharDriver *s = chr->opaque;
    int len;
    uint8_t buf[READ_BUF_LEN];
    GIOStatus status;
    gsize bytes_read;

    len = sizeof(buf);
    if (len > s->max_size) {
        len = s->max_size;
    }
    if (len == 0) {
        return TRUE;
    }

    status = g_io_channel_read_chars(chan, (gchar *)buf,
                                     len, &bytes_read, NULL);
    if (status == G_IO_STATUS_EOF) {
        remove_fd_in_watch(chr);
        qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
        return FALSE;
    }
    if (status == G_IO_STATUS_NORMAL) {
        qemu_chr_be_write(chr, buf, bytes_read);
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
    return g_io_create_watch(s->fd_out, cond);
}

static void fd_chr_update_read_handler(CharDriverState *chr)
{
    FDCharDriver *s = chr->opaque;

    remove_fd_in_watch(chr);
    if (s->fd_in) {
        chr->fd_in_tag = io_add_watch_poll(s->fd_in, fd_chr_read_poll,
                                           fd_chr_read, chr);
    }
}

static void fd_chr_close(struct CharDriverState *chr)
{
    FDCharDriver *s = chr->opaque;

    remove_fd_in_watch(chr);
    if (s->fd_in) {
        g_io_channel_unref(s->fd_in);
    }
    if (s->fd_out) {
        g_io_channel_unref(s->fd_out);
    }

    g_free(s);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

/* open a character device to a unix fd */
static CharDriverState *qemu_chr_open_fd(int fd_in, int fd_out)
{
    CharDriverState *chr;
    FDCharDriver *s;

    chr = qemu_chr_alloc();
    s = g_malloc0(sizeof(FDCharDriver));
    s->fd_in = io_channel_from_fd(fd_in);
    s->fd_out = io_channel_from_fd(fd_out);
    fcntl(fd_out, F_SETFL, O_NONBLOCK);
    s->chr = chr;
    chr->opaque = s;
    chr->chr_add_watch = fd_chr_add_watch;
    chr->chr_write = fd_chr_write;
    chr->chr_update_read_handler = fd_chr_update_read_handler;
    chr->chr_close = fd_chr_close;

    return chr;
}

static CharDriverState *qemu_chr_open_pipe(ChardevHostdev *opts)
{
    int fd_in, fd_out;
    char filename_in[256], filename_out[256];
    const char *filename = opts->device;

    if (filename == NULL) {
        fprintf(stderr, "chardev: pipe: no filename given\n");
        return NULL;
    }

    snprintf(filename_in, 256, "%s.in", filename);
    snprintf(filename_out, 256, "%s.out", filename);
    TFR(fd_in = qemu_open(filename_in, O_RDWR | O_BINARY));
    TFR(fd_out = qemu_open(filename_out, O_RDWR | O_BINARY));
    if (fd_in < 0 || fd_out < 0) {
	if (fd_in >= 0)
	    close(fd_in);
	if (fd_out >= 0)
	    close(fd_out);
        TFR(fd_in = fd_out = qemu_open(filename, O_RDWR | O_BINARY));
        if (fd_in < 0) {
            return NULL;
        }
    }
    return qemu_chr_open_fd(fd_in, fd_out);
}

/* init terminal so that we can grab keys */
static struct termios oldtty;
static int old_fd0_flags;
static bool stdio_allow_signal;

static void term_exit(void)
{
    tcsetattr (0, TCSANOW, &oldtty);
    fcntl(0, F_SETFL, old_fd0_flags);
}

static void qemu_chr_set_echo_stdio(CharDriverState *chr, bool echo)
{
    struct termios tty;

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

static CharDriverState *qemu_chr_open_stdio(ChardevStdio *opts)
{
    CharDriverState *chr;

    if (is_daemonized()) {
        error_report("cannot use stdio with -daemonize");
        return NULL;
    }
    old_fd0_flags = fcntl(0, F_GETFL);
    tcgetattr (0, &oldtty);
    fcntl(0, F_SETFL, O_NONBLOCK);
    atexit(term_exit);

    chr = qemu_chr_open_fd(0, 1);
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

#define HAVE_CHARDEV_TTY 1

typedef struct {
    GIOChannel *fd;
    int read_bytes;

    /* Protected by the CharDriverState chr_write_lock.  */
    int connected;
    guint timer_tag;
} PtyCharDriver;

static void pty_chr_update_read_handler_locked(CharDriverState *chr);
static void pty_chr_state(CharDriverState *chr, int connected);

static gboolean pty_chr_timer(gpointer opaque)
{
    struct CharDriverState *chr = opaque;
    PtyCharDriver *s = chr->opaque;

    qemu_mutex_lock(&chr->chr_write_lock);
    s->timer_tag = 0;
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

    pfd.fd = g_io_channel_unix_get_fd(s->fd);
    pfd.events = G_IO_OUT;
    pfd.revents = 0;
    g_poll(&pfd, 1, 0);
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
        return 0;
    }
    return io_channel_send(s->fd, buf, len);
}

static GSource *pty_chr_add_watch(CharDriverState *chr, GIOCondition cond)
{
    PtyCharDriver *s = chr->opaque;
    return g_io_create_watch(s->fd, cond);
}

static int pty_chr_read_poll(void *opaque)
{
    CharDriverState *chr = opaque;
    PtyCharDriver *s = chr->opaque;

    s->read_bytes = qemu_chr_be_can_write(chr);
    return s->read_bytes;
}

static gboolean pty_chr_read(GIOChannel *chan, GIOCondition cond, void *opaque)
{
    CharDriverState *chr = opaque;
    PtyCharDriver *s = chr->opaque;
    gsize size, len;
    uint8_t buf[READ_BUF_LEN];
    GIOStatus status;

    len = sizeof(buf);
    if (len > s->read_bytes)
        len = s->read_bytes;
    if (len == 0) {
        return TRUE;
    }
    status = g_io_channel_read_chars(s->fd, (gchar *)buf, len, &size, NULL);
    if (status != G_IO_STATUS_NORMAL) {
        pty_chr_state(chr, 0);
        return FALSE;
    } else {
        pty_chr_state(chr, 1);
        qemu_chr_be_write(chr, buf, size);
    }
    return TRUE;
}

/* Called with chr_write_lock held.  */
static void pty_chr_state(CharDriverState *chr, int connected)
{
    PtyCharDriver *s = chr->opaque;

    if (!connected) {
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
            s->connected = 1;
            qemu_chr_be_generic_open(chr);
        }
        if (!chr->fd_in_tag) {
            chr->fd_in_tag = io_add_watch_poll(s->fd, pty_chr_read_poll,
                                               pty_chr_read, chr);
        }
    }
}

static void pty_chr_close(struct CharDriverState *chr)
{
    PtyCharDriver *s = chr->opaque;
    int fd;

    remove_fd_in_watch(chr);
    fd = g_io_channel_unix_get_fd(s->fd);
    g_io_channel_unref(s->fd);
    close(fd);
    if (s->timer_tag) {
        g_source_remove(s->timer_tag);
        s->timer_tag = 0;
    }
    g_free(s);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

static CharDriverState *qemu_chr_open_pty(const char *id,
                                          ChardevReturn *ret)
{
    CharDriverState *chr;
    PtyCharDriver *s;
    int master_fd, slave_fd;
    char pty_name[PATH_MAX];

    master_fd = qemu_openpty_raw(&slave_fd, pty_name);
    if (master_fd < 0) {
        return NULL;
    }

    close(slave_fd);

    chr = qemu_chr_alloc();

    chr->filename = g_strdup_printf("pty:%s", pty_name);
    ret->pty = g_strdup(pty_name);
    ret->has_pty = true;

    fprintf(stderr, "char device redirected to %s (label %s)\n",
            pty_name, id);

    s = g_malloc0(sizeof(PtyCharDriver));
    chr->opaque = s;
    chr->chr_write = pty_chr_write;
    chr->chr_update_read_handler = pty_chr_update_read_handler;
    chr->chr_close = pty_chr_close;
    chr->chr_add_watch = pty_chr_add_watch;
    chr->explicit_be_open = true;

    s->fd = io_channel_from_fd(master_fd);
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

    switch(cmd) {
    case CHR_IOCTL_SERIAL_SET_PARAMS:
        {
            QEMUSerialSetParams *ssp = arg;
            tty_serial_init(g_io_channel_unix_get_fd(s->fd_in),
                            ssp->speed, ssp->parity,
                            ssp->data_bits, ssp->stop_bits);
        }
        break;
    case CHR_IOCTL_SERIAL_SET_BREAK:
        {
            int enable = *(int *)arg;
            if (enable) {
                tcsendbreak(g_io_channel_unix_get_fd(s->fd_in), 1);
            }
        }
        break;
    case CHR_IOCTL_SERIAL_GET_TIOCM:
        {
            int sarg = 0;
            int *targ = (int *)arg;
            ioctl(g_io_channel_unix_get_fd(s->fd_in), TIOCMGET, &sarg);
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
            ioctl(g_io_channel_unix_get_fd(s->fd_in), TIOCMGET, &targ);
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
            ioctl(g_io_channel_unix_get_fd(s->fd_in), TIOCMSET, &targ);
        }
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static void qemu_chr_close_tty(CharDriverState *chr)
{
    FDCharDriver *s = chr->opaque;
    int fd = -1;

    if (s) {
        fd = g_io_channel_unix_get_fd(s->fd_in);
    }

    fd_chr_close(chr);

    if (fd >= 0) {
        close(fd);
    }
}

static CharDriverState *qemu_chr_open_tty_fd(int fd)
{
    CharDriverState *chr;

    tty_serial_init(fd, 115200, 'N', 8, 1);
    chr = qemu_chr_open_fd(fd, fd);
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

static CharDriverState *qemu_chr_open_pp_fd(int fd)
{
    CharDriverState *chr;
    ParallelCharDriver *drv;

    if (ioctl(fd, PPCLAIM) < 0) {
        close(fd);
        return NULL;
    }

    drv = g_malloc0(sizeof(ParallelCharDriver));
    drv->fd = fd;
    drv->mode = IEEE1284_MODE_COMPAT;

    chr = qemu_chr_alloc();
    chr->chr_write = null_chr_write;
    chr->chr_ioctl = pp_ioctl;
    chr->chr_close = pp_close;
    chr->opaque = drv;

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

static CharDriverState *qemu_chr_open_pp_fd(int fd)
{
    CharDriverState *chr;

    chr = qemu_chr_alloc();
    chr->opaque = (void *)(intptr_t)fd;
    chr->chr_write = null_chr_write;
    chr->chr_ioctl = pp_ioctl;
    chr->explicit_be_open = true;
    return chr;
}
#endif

#else /* _WIN32 */

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

static int win_chr_init(CharDriverState *chr, const char *filename)
{
    WinCharState *s = chr->opaque;
    COMMCONFIG comcfg;
    COMMTIMEOUTS cto = { 0, 0, 0, 0, 0};
    COMSTAT comstat;
    DWORD size;
    DWORD err;

    s->hsend = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hsend) {
        fprintf(stderr, "Failed CreateEvent\n");
        goto fail;
    }
    s->hrecv = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hrecv) {
        fprintf(stderr, "Failed CreateEvent\n");
        goto fail;
    }

    s->hcom = CreateFile(filename, GENERIC_READ|GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
    if (s->hcom == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed CreateFile (%lu)\n", GetLastError());
        s->hcom = NULL;
        goto fail;
    }

    if (!SetupComm(s->hcom, NRECVBUF, NSENDBUF)) {
        fprintf(stderr, "Failed SetupComm\n");
        goto fail;
    }

    ZeroMemory(&comcfg, sizeof(COMMCONFIG));
    size = sizeof(COMMCONFIG);
    GetDefaultCommConfig(filename, &comcfg, &size);
    comcfg.dcb.DCBlength = sizeof(DCB);
    CommConfigDialog(filename, NULL, &comcfg);

    if (!SetCommState(s->hcom, &comcfg.dcb)) {
        fprintf(stderr, "Failed SetCommState\n");
        goto fail;
    }

    if (!SetCommMask(s->hcom, EV_ERR)) {
        fprintf(stderr, "Failed SetCommMask\n");
        goto fail;
    }

    cto.ReadIntervalTimeout = MAXDWORD;
    if (!SetCommTimeouts(s->hcom, &cto)) {
        fprintf(stderr, "Failed SetCommTimeouts\n");
        goto fail;
    }

    if (!ClearCommError(s->hcom, &err, &comstat)) {
        fprintf(stderr, "Failed ClearCommError\n");
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

static CharDriverState *qemu_chr_open_win_path(const char *filename)
{
    CharDriverState *chr;
    WinCharState *s;

    chr = qemu_chr_alloc();
    s = g_malloc0(sizeof(WinCharState));
    chr->opaque = s;
    chr->chr_write = win_chr_write;
    chr->chr_close = win_chr_close;

    if (win_chr_init(chr, filename) < 0) {
        g_free(s);
        g_free(chr);
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

static int win_chr_pipe_init(CharDriverState *chr, const char *filename)
{
    WinCharState *s = chr->opaque;
    OVERLAPPED ov;
    int ret;
    DWORD size;
    char openname[256];

    s->fpipe = TRUE;

    s->hsend = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hsend) {
        fprintf(stderr, "Failed CreateEvent\n");
        goto fail;
    }
    s->hrecv = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!s->hrecv) {
        fprintf(stderr, "Failed CreateEvent\n");
        goto fail;
    }

    snprintf(openname, sizeof(openname), "\\\\.\\pipe\\%s", filename);
    s->hcom = CreateNamedPipe(openname, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE |
                              PIPE_WAIT,
                              MAXCONNECT, NSENDBUF, NRECVBUF, NTIMEOUT, NULL);
    if (s->hcom == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed CreateNamedPipe (%lu)\n", GetLastError());
        s->hcom = NULL;
        goto fail;
    }

    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    ret = ConnectNamedPipe(s->hcom, &ov);
    if (ret) {
        fprintf(stderr, "Failed ConnectNamedPipe\n");
        goto fail;
    }

    ret = GetOverlappedResult(s->hcom, &ov, &size, TRUE);
    if (!ret) {
        fprintf(stderr, "Failed GetOverlappedResult\n");
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


static CharDriverState *qemu_chr_open_pipe(ChardevHostdev *opts)
{
    const char *filename = opts->device;
    CharDriverState *chr;
    WinCharState *s;

    chr = qemu_chr_alloc();
    s = g_malloc0(sizeof(WinCharState));
    chr->opaque = s;
    chr->chr_write = win_chr_write;
    chr->chr_close = win_chr_close;

    if (win_chr_pipe_init(chr, filename) < 0) {
        g_free(s);
        g_free(chr);
        return NULL;
    }
    return chr;
}

static CharDriverState *qemu_chr_open_win_file(HANDLE fd_out)
{
    CharDriverState *chr;
    WinCharState *s;

    chr = qemu_chr_alloc();
    s = g_malloc0(sizeof(WinCharState));
    s->hcom = fd_out;
    chr->opaque = s;
    chr->chr_write = win_chr_write;
    return chr;
}

static CharDriverState *qemu_chr_open_win_con(void)
{
    return qemu_chr_open_win_file(GetStdHandle(STD_OUTPUT_HANDLE));
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

static CharDriverState *qemu_chr_open_stdio(ChardevStdio *opts)
{
    CharDriverState   *chr;
    WinStdioCharState *stdio;
    DWORD              dwMode;
    int                is_console = 0;

    chr   = qemu_chr_alloc();
    stdio = g_malloc0(sizeof(WinStdioCharState));

    stdio->hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    if (stdio->hStdIn == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "cannot open stdio: invalid handle\n");
        exit(1);
    }

    is_console = GetConsoleMode(stdio->hStdIn, &dwMode) != 0;

    chr->opaque    = stdio;
    chr->chr_write = win_stdio_write;
    chr->chr_close = win_stdio_close;

    if (is_console) {
        if (qemu_add_wait_object(stdio->hStdIn,
                                 win_stdio_wait_func, chr)) {
            fprintf(stderr, "qemu_add_wait_object: failed\n");
        }
    } else {
        DWORD   dwId;
            
        stdio->hInputReadyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        stdio->hInputDoneEvent  = CreateEvent(NULL, FALSE, FALSE, NULL);
        stdio->hInputThread     = CreateThread(NULL, 0, win_stdio_thread,
                                               chr, 0, &dwId);

        if (stdio->hInputThread == INVALID_HANDLE_VALUE
            || stdio->hInputReadyEvent == INVALID_HANDLE_VALUE
            || stdio->hInputDoneEvent == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "cannot create stdio thread or event\n");
            exit(1);
        }
        if (qemu_add_wait_object(stdio->hInputReadyEvent,
                                 win_stdio_thread_wait_func, chr)) {
            fprintf(stderr, "qemu_add_wait_object: failed\n");
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
}
#endif /* !_WIN32 */


/***********************************************************/
/* UDP Net console */

typedef struct {
    int fd;
    GIOChannel *chan;
    uint8_t buf[READ_BUF_LEN];
    int bufcnt;
    int bufptr;
    int max_size;
} NetCharDriver;

/* Called with chr_write_lock held.  */
static int udp_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    NetCharDriver *s = chr->opaque;
    gsize bytes_written;
    GIOStatus status;

    status = g_io_channel_write_chars(s->chan, (const gchar *)buf, len, &bytes_written, NULL);
    if (status == G_IO_STATUS_EOF) {
        return 0;
    } else if (status != G_IO_STATUS_NORMAL) {
        return -1;
    }

    return bytes_written;
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

static gboolean udp_chr_read(GIOChannel *chan, GIOCondition cond, void *opaque)
{
    CharDriverState *chr = opaque;
    NetCharDriver *s = chr->opaque;
    gsize bytes_read = 0;
    GIOStatus status;

    if (s->max_size == 0) {
        return TRUE;
    }
    status = g_io_channel_read_chars(s->chan, (gchar *)s->buf, sizeof(s->buf),
                                     &bytes_read, NULL);
    s->bufcnt = bytes_read;
    s->bufptr = s->bufcnt;
    if (status != G_IO_STATUS_NORMAL) {
        remove_fd_in_watch(chr);
        return FALSE;
    }

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
    if (s->chan) {
        chr->fd_in_tag = io_add_watch_poll(s->chan, udp_chr_read_poll,
                                           udp_chr_read, chr);
    }
}

static void udp_chr_close(CharDriverState *chr)
{
    NetCharDriver *s = chr->opaque;

    remove_fd_in_watch(chr);
    if (s->chan) {
        g_io_channel_unref(s->chan);
        closesocket(s->fd);
    }
    g_free(s);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

static CharDriverState *qemu_chr_open_udp_fd(int fd)
{
    CharDriverState *chr = NULL;
    NetCharDriver *s = NULL;

    chr = qemu_chr_alloc();
    s = g_malloc0(sizeof(NetCharDriver));

    s->fd = fd;
    s->chan = io_channel_from_socket(s->fd);
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

static CharDriverState *qemu_chr_open_udp(QemuOpts *opts)
{
    Error *local_err = NULL;
    int fd = -1;

    fd = inet_dgram_opts(opts, &local_err);
    if (fd < 0) {
        qerror_report_err(local_err);
        error_free(local_err);
        return NULL;
    }
    return qemu_chr_open_udp_fd(fd);
}

/***********************************************************/
/* TCP Net console */

typedef struct {

    GIOChannel *chan, *listen_chan;
    guint listen_tag;
    int fd, listen_fd;
    int connected;
    int max_size;
    int do_telnetopt;
    int do_nodelay;
    int is_unix;
    int *read_msgfds;
    int read_msgfds_num;
    int *write_msgfds;
    int write_msgfds_num;
} TCPCharDriver;

static gboolean tcp_chr_accept(GIOChannel *chan, GIOCondition cond, void *opaque);

#ifndef _WIN32
static int unix_send_msgfds(CharDriverState *chr, const uint8_t *buf, int len)
{
    TCPCharDriver *s = chr->opaque;
    struct msghdr msgh;
    struct iovec iov;
    int r;

    size_t fd_size = s->write_msgfds_num * sizeof(int);
    char control[CMSG_SPACE(fd_size)];
    struct cmsghdr *cmsg;

    memset(&msgh, 0, sizeof(msgh));
    memset(control, 0, sizeof(control));

    /* set the payload */
    iov.iov_base = (uint8_t *) buf;
    iov.iov_len = len;

    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;

    msgh.msg_control = control;
    msgh.msg_controllen = sizeof(control);

    cmsg = CMSG_FIRSTHDR(&msgh);

    cmsg->cmsg_len = CMSG_LEN(fd_size);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    memcpy(CMSG_DATA(cmsg), s->write_msgfds, fd_size);

    do {
        r = sendmsg(s->fd, &msgh, 0);
    } while (r < 0 && errno == EINTR);

    /* free the written msgfds, no matter what */
    if (s->write_msgfds_num) {
        g_free(s->write_msgfds);
        s->write_msgfds = 0;
        s->write_msgfds_num = 0;
    }

    return r;
}
#endif

/* Called with chr_write_lock held.  */
static int tcp_chr_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    TCPCharDriver *s = chr->opaque;
    if (s->connected) {
#ifndef _WIN32
        if (s->is_unix && s->write_msgfds_num) {
            return unix_send_msgfds(chr, buf, len);
        } else
#endif
        {
            return io_channel_send(s->chan, buf, len);
        }
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

    /* clear old pending fd array */
    if (s->write_msgfds) {
        g_free(s->write_msgfds);
    }

    if (num) {
        s->write_msgfds = g_malloc(num * sizeof(int));
        memcpy(s->write_msgfds, fds, num * sizeof(int));
    }

    s->write_msgfds_num = num;

    return 0;
}

#ifndef _WIN32
static void unix_process_msgfd(CharDriverState *chr, struct msghdr *msg)
{
    TCPCharDriver *s = chr->opaque;
    struct cmsghdr *cmsg;

    for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
        int fd_size, i;

        if (cmsg->cmsg_len < CMSG_LEN(sizeof(int)) ||
            cmsg->cmsg_level != SOL_SOCKET ||
            cmsg->cmsg_type != SCM_RIGHTS) {
            continue;
        }

        fd_size = cmsg->cmsg_len - CMSG_LEN(0);

        if (!fd_size) {
            continue;
        }

        /* close and clean read_msgfds */
        for (i = 0; i < s->read_msgfds_num; i++) {
            close(s->read_msgfds[i]);
        }

        if (s->read_msgfds_num) {
            g_free(s->read_msgfds);
        }

        s->read_msgfds_num = fd_size / sizeof(int);
        s->read_msgfds = g_malloc(fd_size);
        memcpy(s->read_msgfds, CMSG_DATA(cmsg), fd_size);

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
    }
}

static ssize_t tcp_chr_recv(CharDriverState *chr, char *buf, size_t len)
{
    TCPCharDriver *s = chr->opaque;
    struct msghdr msg = { NULL, };
    struct iovec iov[1];
    union {
        struct cmsghdr cmsg;
        char control[CMSG_SPACE(sizeof(int))];
    } msg_control;
    int flags = 0;
    ssize_t ret;

    iov[0].iov_base = buf;
    iov[0].iov_len = len;

    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_control = &msg_control;
    msg.msg_controllen = sizeof(msg_control);

#ifdef MSG_CMSG_CLOEXEC
    flags |= MSG_CMSG_CLOEXEC;
#endif
    ret = recvmsg(s->fd, &msg, flags);
    if (ret > 0 && s->is_unix) {
        unix_process_msgfd(chr, &msg);
    }

    return ret;
}
#else
static ssize_t tcp_chr_recv(CharDriverState *chr, char *buf, size_t len)
{
    TCPCharDriver *s = chr->opaque;
    return qemu_recv(s->fd, buf, len, 0);
}
#endif

static GSource *tcp_chr_add_watch(CharDriverState *chr, GIOCondition cond)
{
    TCPCharDriver *s = chr->opaque;
    return g_io_create_watch(s->chan, cond);
}

static void tcp_chr_disconnect(CharDriverState *chr)
{
    TCPCharDriver *s = chr->opaque;

    s->connected = 0;
    if (s->listen_chan) {
        s->listen_tag = g_io_add_watch(s->listen_chan, G_IO_IN,
                                       tcp_chr_accept, chr);
    }
    remove_fd_in_watch(chr);
    g_io_channel_unref(s->chan);
    s->chan = NULL;
    closesocket(s->fd);
    s->fd = -1;
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

static gboolean tcp_chr_read(GIOChannel *chan, GIOCondition cond, void *opaque)
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
    if (size == 0) {
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

#ifndef _WIN32
CharDriverState *qemu_chr_open_eventfd(int eventfd)
{
    CharDriverState *chr = qemu_chr_open_fd(eventfd, eventfd);

    if (chr) {
        chr->avail_connections = 1;
    }

    return chr;
}
#endif

static gboolean tcp_chr_chan_close(GIOChannel *channel, GIOCondition cond,
                                   void *opaque)
{
    CharDriverState *chr = opaque;

    if (cond != G_IO_HUP) {
        return FALSE;
    }

    /* connection closed */
    tcp_chr_disconnect(chr);
    if (chr->fd_hup_tag) {
        g_source_remove(chr->fd_hup_tag);
        chr->fd_hup_tag = 0;
    }

    return TRUE;
}

static void tcp_chr_connect(void *opaque)
{
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;

    s->connected = 1;
    if (s->chan) {
        chr->fd_in_tag = io_add_watch_poll(s->chan, tcp_chr_read_poll,
                                           tcp_chr_read, chr);
        chr->fd_hup_tag = g_io_add_watch(s->chan, G_IO_HUP, tcp_chr_chan_close,
                                         chr);
    }
    qemu_chr_be_generic_open(chr);
}

static void tcp_chr_update_read_handler(CharDriverState *chr)
{
    TCPCharDriver *s = chr->opaque;

    remove_fd_in_watch(chr);
    if (s->chan) {
        chr->fd_in_tag = io_add_watch_poll(s->chan, tcp_chr_read_poll,
                                           tcp_chr_read, chr);
    }
}

#define IACSET(x,a,b,c) x[0] = a; x[1] = b; x[2] = c;
static void tcp_chr_telnet_init(int fd)
{
    char buf[3];
    /* Send the telnet negotion to put telnet in binary, no echo, single char mode */
    IACSET(buf, 0xff, 0xfb, 0x01);  /* IAC WILL ECHO */
    send(fd, (char *)buf, 3, 0);
    IACSET(buf, 0xff, 0xfb, 0x03);  /* IAC WILL Suppress go ahead */
    send(fd, (char *)buf, 3, 0);
    IACSET(buf, 0xff, 0xfb, 0x00);  /* IAC WILL Binary */
    send(fd, (char *)buf, 3, 0);
    IACSET(buf, 0xff, 0xfd, 0x00);  /* IAC DO Binary */
    send(fd, (char *)buf, 3, 0);
}

static int tcp_chr_add_client(CharDriverState *chr, int fd)
{
    TCPCharDriver *s = chr->opaque;
    if (s->fd != -1)
	return -1;

    qemu_set_nonblock(fd);
    if (s->do_nodelay)
        socket_set_nodelay(fd);
    s->fd = fd;
    s->chan = io_channel_from_socket(fd);
    if (s->listen_tag) {
        g_source_remove(s->listen_tag);
        s->listen_tag = 0;
    }
    tcp_chr_connect(chr);

    return 0;
}

static gboolean tcp_chr_accept(GIOChannel *channel, GIOCondition cond, void *opaque)
{
    CharDriverState *chr = opaque;
    TCPCharDriver *s = chr->opaque;
    struct sockaddr_in saddr;
#ifndef _WIN32
    struct sockaddr_un uaddr;
#endif
    struct sockaddr *addr;
    socklen_t len;
    int fd;

    for(;;) {
#ifndef _WIN32
	if (s->is_unix) {
	    len = sizeof(uaddr);
	    addr = (struct sockaddr *)&uaddr;
	} else
#endif
	{
	    len = sizeof(saddr);
	    addr = (struct sockaddr *)&saddr;
	}
        fd = qemu_accept(s->listen_fd, addr, &len);
        if (fd < 0 && errno != EINTR) {
            s->listen_tag = 0;
            return FALSE;
        } else if (fd >= 0) {
            if (s->do_telnetopt)
                tcp_chr_telnet_init(fd);
            break;
        }
    }
    if (tcp_chr_add_client(chr, fd) < 0)
	close(fd);

    return TRUE;
}

static void tcp_chr_close(CharDriverState *chr)
{
    TCPCharDriver *s = chr->opaque;
    int i;
    if (s->fd >= 0) {
        remove_fd_in_watch(chr);
        if (s->chan) {
            g_io_channel_unref(s->chan);
        }
        closesocket(s->fd);
    }
    if (s->listen_fd >= 0) {
        if (s->listen_tag) {
            g_source_remove(s->listen_tag);
            s->listen_tag = 0;
        }
        if (s->listen_chan) {
            g_io_channel_unref(s->listen_chan);
        }
        closesocket(s->listen_fd);
    }
    if (s->read_msgfds_num) {
        for (i = 0; i < s->read_msgfds_num; i++) {
            close(s->read_msgfds[i]);
        }
        g_free(s->read_msgfds);
    }
    if (s->write_msgfds_num) {
        g_free(s->write_msgfds);
    }
    g_free(s);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

static CharDriverState *qemu_chr_open_socket_fd(int fd, bool do_nodelay,
                                                bool is_listen, bool is_telnet,
                                                bool is_waitconnect,
                                                Error **errp)
{
    CharDriverState *chr = NULL;
    TCPCharDriver *s = NULL;
    char host[NI_MAXHOST], serv[NI_MAXSERV];
    const char *left = "", *right = "";
    struct sockaddr_storage ss;
    socklen_t ss_len = sizeof(ss);

    memset(&ss, 0, ss_len);
    if (getsockname(fd, (struct sockaddr *) &ss, &ss_len) != 0) {
        error_setg_errno(errp, errno, "getsockname");
        return NULL;
    }

    chr = qemu_chr_alloc();
    s = g_malloc0(sizeof(TCPCharDriver));

    s->connected = 0;
    s->fd = -1;
    s->listen_fd = -1;
    s->read_msgfds = 0;
    s->read_msgfds_num = 0;
    s->write_msgfds = 0;
    s->write_msgfds_num = 0;

    chr->filename = g_malloc(256);
    switch (ss.ss_family) {
#ifndef _WIN32
    case AF_UNIX:
        s->is_unix = 1;
        snprintf(chr->filename, 256, "unix:%s%s",
                 ((struct sockaddr_un *)(&ss))->sun_path,
                 is_listen ? ",server" : "");
        break;
#endif
    case AF_INET6:
        left  = "[";
        right = "]";
        /* fall through */
    case AF_INET:
        s->do_nodelay = do_nodelay;
        getnameinfo((struct sockaddr *) &ss, ss_len, host, sizeof(host),
                    serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
        snprintf(chr->filename, 256, "%s:%s%s%s:%s%s",
                 is_telnet ? "telnet" : "tcp",
                 left, host, right, serv,
                 is_listen ? ",server" : "");
        break;
    }

    chr->opaque = s;
    chr->chr_write = tcp_chr_write;
    chr->chr_sync_read = tcp_chr_sync_read;
    chr->chr_close = tcp_chr_close;
    chr->get_msgfds = tcp_get_msgfds;
    chr->set_msgfds = tcp_set_msgfds;
    chr->chr_add_client = tcp_chr_add_client;
    chr->chr_add_watch = tcp_chr_add_watch;
    chr->chr_update_read_handler = tcp_chr_update_read_handler;
    /* be isn't opened until we get a connection */
    chr->explicit_be_open = true;

    if (is_listen) {
        s->listen_fd = fd;
        s->listen_chan = io_channel_from_socket(s->listen_fd);
        s->listen_tag = g_io_add_watch(s->listen_chan, G_IO_IN, tcp_chr_accept, chr);
        if (is_telnet) {
            s->do_telnetopt = 1;
        }
    } else {
        s->connected = 1;
        s->fd = fd;
        socket_set_nodelay(fd);
        s->chan = io_channel_from_socket(s->fd);
        tcp_chr_connect(chr);
    }

    if (is_listen && is_waitconnect) {
        fprintf(stderr, "QEMU waiting for connection on: %s\n",
                chr->filename);
        tcp_chr_accept(s->listen_chan, G_IO_IN, chr);
        qemu_set_nonblock(s->listen_fd);
    }
    return chr;
}

static CharDriverState *qemu_chr_open_socket(QemuOpts *opts)
{
    CharDriverState *chr = NULL;
    Error *local_err = NULL;
    int fd = -1;

    bool is_listen      = qemu_opt_get_bool(opts, "server", false);
    bool is_waitconnect = is_listen && qemu_opt_get_bool(opts, "wait", true);
    bool is_telnet      = qemu_opt_get_bool(opts, "telnet", false);
    bool do_nodelay     = !qemu_opt_get_bool(opts, "delay", true);
    bool is_unix        = qemu_opt_get(opts, "path") != NULL;

    if (is_unix) {
        if (is_listen) {
            fd = unix_listen_opts(opts, &local_err);
        } else {
            fd = unix_connect_opts(opts, &local_err, NULL, NULL);
        }
    } else {
        if (is_listen) {
            fd = inet_listen_opts(opts, 0, &local_err);
        } else {
            fd = inet_connect_opts(opts, &local_err, NULL, NULL);
        }
    }
    if (fd < 0) {
        goto fail;
    }

    if (!is_waitconnect)
        qemu_set_nonblock(fd);

    chr = qemu_chr_open_socket_fd(fd, do_nodelay, is_listen, is_telnet,
                                  is_waitconnect, &local_err);
    if (local_err) {
        goto fail;
    }
    return chr;


 fail:
    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
    }
    if (fd >= 0) {
        closesocket(fd);
    }
    if (chr) {
        g_free(chr->opaque);
        g_free(chr);
    }
    return NULL;
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

static CharDriverState *qemu_chr_open_ringbuf(ChardevRingbuf *opts,
                                              Error **errp)
{
    CharDriverState *chr;
    RingBufCharDriver *d;

    chr = qemu_chr_alloc();
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
    g_free(chr);
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
        write_data = g_base64_decode(data, &write_count);
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
        qerror_report_err(local_err);
        error_free(local_err);
        return NULL;
    }

    if (strstart(filename, "mon:", &p)) {
        filename = p;
        qemu_opt_set(opts, "mux", "on");
        if (strcmp(filename, "stdio") == 0) {
            /* Monitor is muxed to stdio: do not exit on Ctrl+C by default
             * but pass it to the guest.  Handle this only for compat syntax,
             * for -chardev syntax we have special option for this.
             * This is what -nographic did, redirecting+muxing serial+monitor
             * to stdio causing Ctrl+C to be passed to guest. */
            qemu_opt_set(opts, "signal", "off");
        }
    }

    if (strcmp(filename, "null")    == 0 ||
        strcmp(filename, "pty")     == 0 ||
        strcmp(filename, "msmouse") == 0 ||
        strcmp(filename, "braille") == 0 ||
        strcmp(filename, "stdio")   == 0) {
        qemu_opt_set(opts, "backend", filename);
        return opts;
    }
    if (strstart(filename, "vc", &p)) {
        qemu_opt_set(opts, "backend", "vc");
        if (*p == ':') {
            if (sscanf(p+1, "%7[0-9]x%7[0-9]", width, height) == 2) {
                /* pixels */
                qemu_opt_set(opts, "width", width);
                qemu_opt_set(opts, "height", height);
            } else if (sscanf(p+1, "%7[0-9]Cx%7[0-9]C", width, height) == 2) {
                /* chars */
                qemu_opt_set(opts, "cols", width);
                qemu_opt_set(opts, "rows", height);
            } else {
                goto fail;
            }
        }
        return opts;
    }
    if (strcmp(filename, "con:") == 0) {
        qemu_opt_set(opts, "backend", "console");
        return opts;
    }
    if (strstart(filename, "COM", NULL)) {
        qemu_opt_set(opts, "backend", "serial");
        qemu_opt_set(opts, "path", filename);
        return opts;
    }
    if (strstart(filename, "file:", &p)) {
        qemu_opt_set(opts, "backend", "file");
        qemu_opt_set(opts, "path", p);
        return opts;
    }
    if (strstart(filename, "pipe:", &p)) {
        qemu_opt_set(opts, "backend", "pipe");
        qemu_opt_set(opts, "path", p);
        return opts;
    }
    if (strstart(filename, "tcp:", &p) ||
        strstart(filename, "telnet:", &p)) {
        if (sscanf(p, "%64[^:]:%32[^,]%n", host, port, &pos) < 2) {
            host[0] = 0;
            if (sscanf(p, ":%32[^,]%n", port, &pos) < 1)
                goto fail;
        }
        qemu_opt_set(opts, "backend", "socket");
        qemu_opt_set(opts, "host", host);
        qemu_opt_set(opts, "port", port);
        if (p[pos] == ',') {
            if (qemu_opts_do_parse(opts, p+pos+1, NULL) != 0)
                goto fail;
        }
        if (strstart(filename, "telnet:", &p))
            qemu_opt_set(opts, "telnet", "on");
        return opts;
    }
    if (strstart(filename, "udp:", &p)) {
        qemu_opt_set(opts, "backend", "udp");
        if (sscanf(p, "%64[^:]:%32[^@,]%n", host, port, &pos) < 2) {
            host[0] = 0;
            if (sscanf(p, ":%32[^@,]%n", port, &pos) < 1) {
                goto fail;
            }
        }
        qemu_opt_set(opts, "host", host);
        qemu_opt_set(opts, "port", port);
        if (p[pos] == '@') {
            p += pos + 1;
            if (sscanf(p, "%64[^:]:%32[^,]%n", host, port, &pos) < 2) {
                host[0] = 0;
                if (sscanf(p, ":%32[^,]%n", port, &pos) < 1) {
                    goto fail;
                }
            }
            qemu_opt_set(opts, "localaddr", host);
            qemu_opt_set(opts, "localport", port);
        }
        return opts;
    }
    if (strstart(filename, "unix:", &p)) {
        qemu_opt_set(opts, "backend", "socket");
        if (qemu_opts_do_parse(opts, p, "path") != 0)
            goto fail;
        return opts;
    }
    if (strstart(filename, "/dev/parport", NULL) ||
        strstart(filename, "/dev/ppi", NULL)) {
        qemu_opt_set(opts, "backend", "parport");
        qemu_opt_set(opts, "path", filename);
        return opts;
    }
    if (strstart(filename, "/dev/", NULL)) {
        qemu_opt_set(opts, "backend", "tty");
        qemu_opt_set(opts, "path", filename);
        return opts;
    }

fail:
    qemu_opts_del(opts);
    return NULL;
}

static void qemu_chr_parse_file_out(QemuOpts *opts, ChardevBackend *backend,
                                    Error **errp)
{
    const char *path = qemu_opt_get(opts, "path");

    if (path == NULL) {
        error_setg(errp, "chardev: file: no filename given");
        return;
    }
    backend->file = g_new0(ChardevFile, 1);
    backend->file->out = g_strdup(path);
}

static void qemu_chr_parse_stdio(QemuOpts *opts, ChardevBackend *backend,
                                 Error **errp)
{
    backend->stdio = g_new0(ChardevStdio, 1);
    backend->stdio->has_signal = true;
    backend->stdio->signal = qemu_opt_get_bool(opts, "signal", true);
}

static void qemu_chr_parse_serial(QemuOpts *opts, ChardevBackend *backend,
                                  Error **errp)
{
    const char *device = qemu_opt_get(opts, "path");

    if (device == NULL) {
        error_setg(errp, "chardev: serial/tty: no device path given");
        return;
    }
    backend->serial = g_new0(ChardevHostdev, 1);
    backend->serial->device = g_strdup(device);
}

static void qemu_chr_parse_parallel(QemuOpts *opts, ChardevBackend *backend,
                                    Error **errp)
{
    const char *device = qemu_opt_get(opts, "path");

    if (device == NULL) {
        error_setg(errp, "chardev: parallel: no device path given");
        return;
    }
    backend->parallel = g_new0(ChardevHostdev, 1);
    backend->parallel->device = g_strdup(device);
}

static void qemu_chr_parse_pipe(QemuOpts *opts, ChardevBackend *backend,
                                Error **errp)
{
    const char *device = qemu_opt_get(opts, "path");

    if (device == NULL) {
        error_setg(errp, "chardev: pipe: no device path given");
        return;
    }
    backend->pipe = g_new0(ChardevHostdev, 1);
    backend->pipe->device = g_strdup(device);
}

static void qemu_chr_parse_ringbuf(QemuOpts *opts, ChardevBackend *backend,
                                   Error **errp)
{
    int val;

    backend->ringbuf = g_new0(ChardevRingbuf, 1);

    val = qemu_opt_get_size(opts, "size", 0);
    if (val != 0) {
        backend->ringbuf->has_size = true;
        backend->ringbuf->size = val;
    }
}

static void qemu_chr_parse_mux(QemuOpts *opts, ChardevBackend *backend,
                               Error **errp)
{
    const char *chardev = qemu_opt_get(opts, "chardev");

    if (chardev == NULL) {
        error_setg(errp, "chardev: mux: no chardev given");
        return;
    }
    backend->mux = g_new0(ChardevMux, 1);
    backend->mux->chardev = g_strdup(chardev);
}

typedef struct CharDriver {
    const char *name;
    /* old, pre qapi */
    CharDriverState *(*open)(QemuOpts *opts);
    /* new, qapi-based */
    ChardevBackendKind kind;
    void (*parse)(QemuOpts *opts, ChardevBackend *backend, Error **errp);
} CharDriver;

static GSList *backends;

void register_char_driver(const char *name, CharDriverState *(*open)(QemuOpts *))
{
    CharDriver *s;

    s = g_malloc0(sizeof(*s));
    s->name = g_strdup(name);
    s->open = open;

    backends = g_slist_append(backends, s);
}

void register_char_driver_qapi(const char *name, ChardevBackendKind kind,
        void (*parse)(QemuOpts *opts, ChardevBackend *backend, Error **errp))
{
    CharDriver *s;

    s = g_malloc0(sizeof(*s));
    s->name = g_strdup(name);
    s->kind = kind;
    s->parse = parse;

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

    if (qemu_opts_id(opts) == NULL) {
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

    if (!cd->open) {
        /* using new, qapi init */
        ChardevBackend *backend = g_new0(ChardevBackend, 1);
        ChardevReturn *ret = NULL;
        const char *id = qemu_opts_id(opts);
        char *bid = NULL;

        if (qemu_opt_get_bool(opts, "mux", 0)) {
            bid = g_strdup_printf("%s-base", id);
        }

        chr = NULL;
        backend->kind = cd->kind;
        if (cd->parse) {
            cd->parse(opts, backend, &local_err);
            if (local_err) {
                error_propagate(errp, local_err);
                goto qapi_out;
            }
        }
        ret = qmp_chardev_add(bid ? bid : id, backend, errp);
        if (!ret) {
            goto qapi_out;
        }

        if (bid) {
            qapi_free_ChardevBackend(backend);
            qapi_free_ChardevReturn(ret);
            backend = g_new0(ChardevBackend, 1);
            backend->mux = g_new0(ChardevMux, 1);
            backend->kind = CHARDEV_BACKEND_KIND_MUX;
            backend->mux->chardev = g_strdup(bid);
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
    }

    chr = cd->open(opts);
    if (!chr) {
        error_setg(errp, "chardev: opening backend \"%s\" failed",
                   qemu_opt_get(opts, "backend"));
        goto err;
    }

    if (!chr->filename)
        chr->filename = g_strdup(qemu_opt_get(opts, "backend"));
    chr->init = init;
    /* if we didn't create the chardev via qmp_chardev_add, we
     * need to send the OPENED event here
     */
    if (!chr->explicit_be_open) {
        qemu_chr_be_event(chr, CHR_EVENT_OPENED);
    }
    QTAILQ_INSERT_TAIL(&chardevs, chr, next);

    if (qemu_opt_get_bool(opts, "mux", 0)) {
        CharDriverState *base = chr;
        int len = strlen(qemu_opts_id(opts)) + 6;
        base->label = g_malloc(len);
        snprintf(base->label, len, "%s-base", qemu_opts_id(opts));
        chr = qemu_chr_open_mux(base);
        chr->filename = base->filename;
        chr->avail_connections = MAX_MUX;
        QTAILQ_INSERT_TAIL(&chardevs, chr, next);
    } else {
        chr->avail_connections = 1;
    }
    chr->label = g_strdup(qemu_opts_id(opts));
    chr->opts = opts;
    return chr;

err:
    qemu_opts_del(opts);
    return NULL;
}

CharDriverState *qemu_chr_new(const char *label, const char *filename, void (*init)(struct CharDriverState *s))
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
        error_report("%s", error_get_pretty(err));
        error_free(err);
    }
    if (chr && qemu_opt_get_bool(opts, "mux", 0)) {
        qemu_chr_fe_claim_no_fail(chr);
        monitor_init(chr, MONITOR_USE_READLINE);
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

void qemu_chr_delete(CharDriverState *chr)
{
    QTAILQ_REMOVE(&chardevs, chr, next);
    if (chr->chr_close) {
        chr->chr_close(chr);
    }
    g_free(chr->filename);
    g_free(chr->label);
    if (chr->opts) {
        qemu_opts_del(chr->opts);
    }
    g_free(chr);
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

/* Get a character (serial) device interface.  */
CharDriverState *qemu_char_get_next_serial(void)
{
    static int next_serial;
    CharDriverState *chr;

    /* FIXME: This function needs to go away: use chardev properties!  */

    while (next_serial < MAX_SERIAL_PORTS && serial_hds[next_serial]) {
        chr = serial_hds[next_serial++];
        qemu_chr_fe_claim_no_fail(chr);
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
            .name = "telnet",
            .type = QEMU_OPT_BOOL,
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
        },
        { /* end of list */ }
    },
};

#ifdef _WIN32

static CharDriverState *qmp_chardev_open_file(ChardevFile *file, Error **errp)
{
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
    return qemu_chr_open_win_file(out);
}

static CharDriverState *qmp_chardev_open_serial(ChardevHostdev *serial,
                                                Error **errp)
{
    return qemu_chr_open_win_path(serial->device);
}

static CharDriverState *qmp_chardev_open_parallel(ChardevHostdev *parallel,
                                                  Error **errp)
{
    error_setg(errp, "character device backend type 'parallel' not supported");
    return NULL;
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

static CharDriverState *qmp_chardev_open_file(ChardevFile *file, Error **errp)
{
    int flags, in = -1, out;

    flags = O_WRONLY | O_TRUNC | O_CREAT | O_BINARY;
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

    return qemu_chr_open_fd(in, out);
}

static CharDriverState *qmp_chardev_open_serial(ChardevHostdev *serial,
                                                Error **errp)
{
#ifdef HAVE_CHARDEV_TTY
    int fd;

    fd = qmp_chardev_open_file_source(serial->device, O_RDWR, errp);
    if (fd < 0) {
        return NULL;
    }
    qemu_set_nonblock(fd);
    return qemu_chr_open_tty_fd(fd);
#else
    error_setg(errp, "character device backend type 'serial' not supported");
    return NULL;
#endif
}

static CharDriverState *qmp_chardev_open_parallel(ChardevHostdev *parallel,
                                                  Error **errp)
{
#ifdef HAVE_CHARDEV_PARPORT
    int fd;

    fd = qmp_chardev_open_file_source(parallel->device, O_RDWR, errp);
    if (fd < 0) {
        return NULL;
    }
    return qemu_chr_open_pp_fd(fd);
#else
    error_setg(errp, "character device backend type 'parallel' not supported");
    return NULL;
#endif
}

#endif /* WIN32 */

static CharDriverState *qmp_chardev_open_socket(ChardevSocket *sock,
                                                Error **errp)
{
    SocketAddress *addr = sock->addr;
    bool do_nodelay     = sock->has_nodelay ? sock->nodelay : false;
    bool is_listen      = sock->has_server  ? sock->server  : true;
    bool is_telnet      = sock->has_telnet  ? sock->telnet  : false;
    bool is_waitconnect = sock->has_wait    ? sock->wait    : false;
    int fd;

    if (is_listen) {
        fd = socket_listen(addr, errp);
    } else {
        fd = socket_connect(addr, errp, NULL, NULL);
    }
    if (fd < 0) {
        return NULL;
    }
    return qemu_chr_open_socket_fd(fd, do_nodelay, is_listen,
                                   is_telnet, is_waitconnect, errp);
}

static CharDriverState *qmp_chardev_open_udp(ChardevUdp *udp,
                                             Error **errp)
{
    int fd;

    fd = socket_dgram(udp->remote, udp->local, errp);
    if (fd < 0) {
        return NULL;
    }
    return qemu_chr_open_udp_fd(fd);
}

ChardevReturn *qmp_chardev_add(const char *id, ChardevBackend *backend,
                               Error **errp)
{
    ChardevReturn *ret = g_new0(ChardevReturn, 1);
    CharDriverState *base, *chr = NULL;

    chr = qemu_chr_find(id);
    if (chr) {
        error_setg(errp, "Chardev '%s' already exists", id);
        g_free(ret);
        return NULL;
    }

    switch (backend->kind) {
    case CHARDEV_BACKEND_KIND_FILE:
        chr = qmp_chardev_open_file(backend->file, errp);
        break;
    case CHARDEV_BACKEND_KIND_SERIAL:
        chr = qmp_chardev_open_serial(backend->serial, errp);
        break;
    case CHARDEV_BACKEND_KIND_PARALLEL:
        chr = qmp_chardev_open_parallel(backend->parallel, errp);
        break;
    case CHARDEV_BACKEND_KIND_PIPE:
        chr = qemu_chr_open_pipe(backend->pipe);
        break;
    case CHARDEV_BACKEND_KIND_SOCKET:
        chr = qmp_chardev_open_socket(backend->socket, errp);
        break;
    case CHARDEV_BACKEND_KIND_UDP:
        chr = qmp_chardev_open_udp(backend->udp, errp);
        break;
#ifdef HAVE_CHARDEV_TTY
    case CHARDEV_BACKEND_KIND_PTY:
        chr = qemu_chr_open_pty(id, ret);
        break;
#endif
    case CHARDEV_BACKEND_KIND_NULL:
        chr = qemu_chr_open_null();
        break;
    case CHARDEV_BACKEND_KIND_MUX:
        base = qemu_chr_find(backend->mux->chardev);
        if (base == NULL) {
            error_setg(errp, "mux: base chardev %s not found",
                       backend->mux->chardev);
            break;
        }
        chr = qemu_chr_open_mux(base);
        break;
    case CHARDEV_BACKEND_KIND_MSMOUSE:
        chr = qemu_chr_open_msmouse();
        break;
#ifdef CONFIG_BRLAPI
    case CHARDEV_BACKEND_KIND_BRAILLE:
        chr = chr_baum_init();
        break;
#endif
    case CHARDEV_BACKEND_KIND_STDIO:
        chr = qemu_chr_open_stdio(backend->stdio);
        break;
#ifdef _WIN32
    case CHARDEV_BACKEND_KIND_CONSOLE:
        chr = qemu_chr_open_win_con();
        break;
#endif
#ifdef CONFIG_SPICE
    case CHARDEV_BACKEND_KIND_SPICEVMC:
        chr = qemu_chr_open_spice_vmc(backend->spicevmc->type);
        break;
    case CHARDEV_BACKEND_KIND_SPICEPORT:
        chr = qemu_chr_open_spice_port(backend->spiceport->fqdn);
        break;
#endif
    case CHARDEV_BACKEND_KIND_VC:
        chr = vc_init(backend->vc);
        break;
    case CHARDEV_BACKEND_KIND_RINGBUF:
    case CHARDEV_BACKEND_KIND_MEMORY:
        chr = qemu_chr_open_ringbuf(backend->ringbuf, errp);
        break;
    default:
        error_setg(errp, "unknown chardev backend (%d)", backend->kind);
        break;
    }

    /*
     * Character backend open hasn't been fully converted to the Error
     * API.  Some opens fail without setting an error.  Set a generic
     * error then.
     * TODO full conversion to Error API
     */
    if (chr == NULL && errp && !*errp) {
        error_setg(errp, "Failed to create chardev");
    }
    if (chr) {
        chr->label = g_strdup(id);
        chr->avail_connections =
            (backend->kind == CHARDEV_BACKEND_KIND_MUX) ? MAX_MUX : 1;
        if (!chr->filename) {
            chr->filename = g_strdup(ChardevBackendKind_lookup[backend->kind]);
        }
        if (!chr->explicit_be_open) {
            qemu_chr_be_event(chr, CHR_EVENT_OPENED);
        }
        QTAILQ_INSERT_TAIL(&chardevs, chr, next);
        return ret;
    } else {
        g_free(ret);
        return NULL;
    }
}

void qmp_chardev_remove(const char *id, Error **errp)
{
    CharDriverState *chr;

    chr = qemu_chr_find(id);
    if (NULL == chr) {
        error_setg(errp, "Chardev '%s' not found", id);
        return;
    }
    if (chr->chr_can_read || chr->chr_read ||
        chr->chr_event || chr->handler_opaque) {
        error_setg(errp, "Chardev '%s' is busy", id);
        return;
    }
    qemu_chr_delete(chr);
}

static void register_types(void)
{
    register_char_driver_qapi("null", CHARDEV_BACKEND_KIND_NULL, NULL);
    register_char_driver("socket", qemu_chr_open_socket);
    register_char_driver("udp", qemu_chr_open_udp);
    register_char_driver_qapi("ringbuf", CHARDEV_BACKEND_KIND_RINGBUF,
                              qemu_chr_parse_ringbuf);
    register_char_driver_qapi("file", CHARDEV_BACKEND_KIND_FILE,
                              qemu_chr_parse_file_out);
    register_char_driver_qapi("stdio", CHARDEV_BACKEND_KIND_STDIO,
                              qemu_chr_parse_stdio);
    register_char_driver_qapi("serial", CHARDEV_BACKEND_KIND_SERIAL,
                              qemu_chr_parse_serial);
    register_char_driver_qapi("tty", CHARDEV_BACKEND_KIND_SERIAL,
                              qemu_chr_parse_serial);
    register_char_driver_qapi("parallel", CHARDEV_BACKEND_KIND_PARALLEL,
                              qemu_chr_parse_parallel);
    register_char_driver_qapi("parport", CHARDEV_BACKEND_KIND_PARALLEL,
                              qemu_chr_parse_parallel);
    register_char_driver_qapi("pty", CHARDEV_BACKEND_KIND_PTY, NULL);
    register_char_driver_qapi("console", CHARDEV_BACKEND_KIND_CONSOLE, NULL);
    register_char_driver_qapi("pipe", CHARDEV_BACKEND_KIND_PIPE,
                              qemu_chr_parse_pipe);
    register_char_driver_qapi("mux", CHARDEV_BACKEND_KIND_MUX,
                              qemu_chr_parse_mux);
    /* Bug-compatibility: */
    register_char_driver_qapi("memory", CHARDEV_BACKEND_KIND_MEMORY,
                              qemu_chr_parse_ringbuf);
    /* this must be done after machine init, since we register FEs with muxes
     * as part of realize functions like serial_isa_realizefn when -nographic
     * is specified
     */
    qemu_add_machine_init_done_notifier(&muxes_realize_notify);
}

type_init(register_types);
