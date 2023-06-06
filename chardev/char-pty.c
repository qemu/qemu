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
#include "qapi/error.h"
#include "chardev/char.h"
#include "io/channel-file.h"
#include "qemu/sockets.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/qemu-print.h"

#include "chardev/char-io.h"
#include "qom/object.h"

struct PtyChardev {
    Chardev parent;
    QIOChannel *ioc;
    int read_bytes;

    int connected;
    GSource *timer_src;
};
typedef struct PtyChardev PtyChardev;

DECLARE_INSTANCE_CHECKER(PtyChardev, PTY_CHARDEV,
                         TYPE_CHARDEV_PTY)

static void pty_chr_state(Chardev *chr, int connected);

static void pty_chr_timer_cancel(PtyChardev *s)
{
    if (s->timer_src) {
        g_source_destroy(s->timer_src);
        g_source_unref(s->timer_src);
        s->timer_src = NULL;
    }
}

static gboolean pty_chr_timer(gpointer opaque)
{
    struct Chardev *chr = CHARDEV(opaque);
    PtyChardev *s = PTY_CHARDEV(opaque);

    pty_chr_timer_cancel(s);
    if (!s->connected) {
        /* Next poll ... */
        qemu_chr_be_update_read_handlers(chr, chr->gcontext);
    }
    return FALSE;
}

static void pty_chr_rearm_timer(Chardev *chr, int ms)
{
    PtyChardev *s = PTY_CHARDEV(chr);
    char *name;

    pty_chr_timer_cancel(s);
    name = g_strdup_printf("pty-timer-%s", chr->label);
    s->timer_src = qemu_chr_timeout_add_ms(chr, ms, pty_chr_timer, chr);
    g_source_set_name(s->timer_src, name);
    g_free(name);
}

static void pty_chr_update_read_handler(Chardev *chr)
{
    PtyChardev *s = PTY_CHARDEV(chr);
    GPollFD pfd;
    int rc;
    QIOChannelFile *fioc = QIO_CHANNEL_FILE(s->ioc);

    pfd.fd = fioc->fd;
    pfd.events = G_IO_OUT;
    pfd.revents = 0;
    rc = RETRY_ON_EINTR(g_poll(&pfd, 1, 0));
    assert(rc >= 0);

    if (pfd.revents & G_IO_HUP) {
        pty_chr_state(chr, 0);
    } else {
        pty_chr_state(chr, 1);
    }
}

static int char_pty_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    PtyChardev *s = PTY_CHARDEV(chr);

    if (!s->connected) {
        return len;
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
    if (len > s->read_bytes) {
        len = s->read_bytes;
    }
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

static void pty_chr_state(Chardev *chr, int connected)
{
    PtyChardev *s = PTY_CHARDEV(chr);

    if (!connected) {
        remove_fd_in_watch(chr);
        s->connected = 0;
        /* (re-)connect poll interval for idle guests: once per second.
         * We check more frequently in case the guests sends data to
         * the virtual device linked to our pty. */
        pty_chr_rearm_timer(chr, 1000);
    } else {
        pty_chr_timer_cancel(s);
        if (!s->connected) {
            s->connected = 1;
            qemu_chr_be_event(chr, CHR_EVENT_OPENED);
        }
        if (!chr->gsource) {
            chr->gsource = io_add_watch_poll(chr, s->ioc,
                                               pty_chr_read_poll,
                                               pty_chr_read,
                                               chr, chr->gcontext);
        }
    }
}

static void char_pty_finalize(Object *obj)
{
    Chardev *chr = CHARDEV(obj);
    PtyChardev *s = PTY_CHARDEV(obj);

    pty_chr_state(chr, 0);
    object_unref(OBJECT(s->ioc));
    pty_chr_timer_cancel(s);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

#if defined HAVE_PTY_H
# include <pty.h>
#elif defined CONFIG_BSD
# include <termios.h>
# if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__DragonFly__)
#  include <libutil.h>
# else
#  include <util.h>
# endif
#elif defined CONFIG_SOLARIS
# include <termios.h>
# include <stropts.h>
#else
# include <termios.h>
#endif

#ifdef __sun__

#if !defined(HAVE_OPENPTY)
/* Once illumos has openpty(), this is going to be removed. */
static int openpty(int *amaster, int *aslave, char *name,
                   struct termios *termp, struct winsize *winp)
{
    const char *slave;
    int mfd = -1, sfd = -1;

    *amaster = *aslave = -1;

    mfd = open("/dev/ptmx", O_RDWR | O_NOCTTY);
    if (mfd < 0) {
        goto err;
    }

    if (grantpt(mfd) == -1 || unlockpt(mfd) == -1) {
        goto err;
    }

    if ((slave = ptsname(mfd)) == NULL) {
        goto err;
    }

    if ((sfd = open(slave, O_RDONLY | O_NOCTTY)) == -1) {
        goto err;
    }

    if (ioctl(sfd, I_PUSH, "ptem") == -1 ||
        (termp != NULL && tcgetattr(sfd, termp) < 0)) {
        goto err;
    }

    *amaster = mfd;
    *aslave = sfd;

    if (winp) {
        ioctl(sfd, TIOCSWINSZ, winp);
    }

    return 0;

err:
    if (sfd != -1) {
        close(sfd);
    }
    close(mfd);
    return -1;
}
#endif

static void cfmakeraw (struct termios *termios_p)
{
    termios_p->c_iflag &=
        ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    termios_p->c_oflag &= ~OPOST;
    termios_p->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    termios_p->c_cflag &= ~(CSIZE | PARENB);
    termios_p->c_cflag |= CS8;

    termios_p->c_cc[VMIN] = 0;
    termios_p->c_cc[VTIME] = 0;
}
#endif

/* like openpty() but also makes it raw; return master fd */
static int qemu_openpty_raw(int *aslave, char *pty_name)
{
    int amaster;
    struct termios tty;
#if defined(__OpenBSD__) || defined(__DragonFly__)
    char pty_buf[PATH_MAX];
#define q_ptsname(x) pty_buf
#else
    char *pty_buf = NULL;
#define q_ptsname(x) ptsname(x)
#endif

    if (openpty(&amaster, aslave, pty_buf, NULL, NULL) < 0) {
        return -1;
    }

    /* Set raw attributes on the pty. */
    tcgetattr(*aslave, &tty);
    cfmakeraw(&tty);
    tcsetattr(*aslave, TCSAFLUSH, &tty);

    if (pty_name) {
        strcpy(pty_name, q_ptsname(amaster));
    }

    return amaster;
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
    if (!g_unix_set_fd_nonblocking(master_fd, true, NULL)) {
        error_setg_errno(errp, errno, "Failed to set FD nonblocking");
        return;
    }

    chr->filename = g_strdup_printf("pty:%s", pty_name);
    qemu_printf("char device redirected to %s (label %s)\n",
                pty_name, chr->label);

    s = PTY_CHARDEV(chr);
    s->ioc = QIO_CHANNEL(qio_channel_file_new_fd(master_fd));
    name = g_strdup_printf("chardev-pty-%s", chr->label);
    qio_channel_set_name(s->ioc, name);
    g_free(name);
    s->timer_src = NULL;
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

static void register_types(void)
{
    type_register_static(&char_pty_type_info);
}

type_init(register_types);
