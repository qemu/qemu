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
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "chardev/char.h"

#ifdef _WIN32
#include "chardev/char-win.h"
#include "chardev/char-win-stdio.h"
#else
#include <termios.h>
#include "chardev/char-fd.h"
#endif

#ifndef _WIN32
/* init terminal so that we can grab keys */
static struct termios oldtty;
static int old_fd0_flags;
static int old_fd1_flags;
static bool stdio_in_use;
static bool stdio_allow_signal;
static bool stdio_echo_state;

static void term_exit(void)
{
    if (stdio_in_use) {
        tcsetattr(0, TCSANOW, &oldtty);
        fcntl(0, F_SETFL, old_fd0_flags);
        fcntl(1, F_SETFL, old_fd1_flags);
        stdio_in_use = false;
    }
}

static void qemu_chr_set_echo_stdio(Chardev *chr, bool echo)
{
    struct termios tty;

    stdio_echo_state = echo;
    tty = oldtty;
    if (!echo) {
        tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                         | INLCR | IGNCR | ICRNL | IXON);
        tty.c_oflag |= OPOST;
        tty.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
        tty.c_cflag &= ~(CSIZE | PARENB);
        tty.c_cflag |= CS8;
        tty.c_cc[VMIN] = 1;
        tty.c_cc[VTIME] = 0;
    }
    if (!stdio_allow_signal) {
        tty.c_lflag &= ~ISIG;
    }

    tcsetattr(0, TCSANOW, &tty);
}

static void term_stdio_handler(int sig)
{
    /* restore echo after resume from suspend. */
    qemu_chr_set_echo_stdio(NULL, stdio_echo_state);
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
    old_fd1_flags = fcntl(1, F_GETFL);
    tcgetattr(0, &oldtty);
    if (!qemu_set_blocking(0, false, errp)) {
        return;
    }

    if (!qemu_chr_open_fd(chr, 0, 1, errp)) {
        return;
    }

    atexit(term_exit);

    memset(&act, 0, sizeof(act));
    act.sa_handler = term_stdio_handler;
    sigaction(SIGCONT, &act, NULL);

    stdio_allow_signal = !opts->has_signal || opts->signal;
    qemu_chr_set_echo_stdio(chr, false);
}
#endif

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

static void char_stdio_class_init(ObjectClass *oc, const void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_stdio;
#ifndef _WIN32
    cc->open = qemu_chr_open_stdio;
    cc->chr_set_echo = qemu_chr_set_echo_stdio;
#endif
}

static void char_stdio_finalize(Object *obj)
{
#ifndef _WIN32
    term_exit();
#endif
}

static const TypeInfo char_stdio_type_info = {
    .name = TYPE_CHARDEV_STDIO,
#ifdef _WIN32
    .parent = TYPE_CHARDEV_WIN_STDIO,
#else
    .parent = TYPE_CHARDEV_FD,
#endif
    .instance_finalize = char_stdio_finalize,
    .class_init = char_stdio_class_init,
};

static void register_types(void)
{
    type_register_static(&char_stdio_type_info);
}

type_init(register_types);
