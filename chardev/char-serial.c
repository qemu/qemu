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
#include "io/channel-file.h"
#include "qapi/error.h"

#ifdef _WIN32
#include "chardev/char-win.h"
#else
#include <sys/ioctl.h>
#include <termios.h>
#include "chardev/char-fd.h"
#endif

#include "chardev/char-serial.h"

#ifdef _WIN32

static void qmp_chardev_open_serial(Chardev *chr,
                                    ChardevBackend *backend,
                                    bool *be_opened,
                                    Error **errp)
{
    ChardevHostdev *serial = backend->u.serial.data;

    win_chr_serial_init(chr, serial->device, errp);
}

#elif defined(__linux__) || defined(__sun__) || defined(__FreeBSD__)      \
    || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) \
    || defined(__GLIBC__)

static void tty_serial_init(int fd, int speed,
                            int parity, int data_bits, int stop_bits)
{
    struct termios tty;
    speed_t spd;

#if 0
    printf("tty_serial_init: speed=%d parity=%c data=%d stop=%d\n",
           speed, parity, data_bits, stop_bits);
#endif
    tcgetattr(fd, &tty);

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

    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                     | INLCR | IGNCR | ICRNL | IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN | ISIG);
    tty.c_cflag &= ~(CSIZE | PARENB | PARODD | CRTSCTS | CSTOPB);
    switch (data_bits) {
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
    switch (parity) {
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
    if (stop_bits == 2) {
        tty.c_cflag |= CSTOPB;
    }

    tcsetattr(fd, TCSANOW, &tty);
}

static int tty_serial_ioctl(Chardev *chr, int cmd, void *arg)
{
    FDChardev *s = FD_CHARDEV(chr);
    QIOChannelFile *fioc = QIO_CHANNEL_FILE(s->ioc_in);

    switch (cmd) {
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
            if (sarg & TIOCM_CTS) {
                *targ |= CHR_TIOCM_CTS;
            }
            if (sarg & TIOCM_CAR) {
                *targ |= CHR_TIOCM_CAR;
            }
            if (sarg & TIOCM_DSR) {
                *targ |= CHR_TIOCM_DSR;
            }
            if (sarg & TIOCM_RI) {
                *targ |= CHR_TIOCM_RI;
            }
            if (sarg & TIOCM_DTR) {
                *targ |= CHR_TIOCM_DTR;
            }
            if (sarg & TIOCM_RTS) {
                *targ |= CHR_TIOCM_RTS;
            }
        }
        break;
    case CHR_IOCTL_SERIAL_SET_TIOCM:
        {
            int sarg = *(int *)arg;
            int targ = 0;
            ioctl(fioc->fd, TIOCMGET, &targ);
            targ &= ~(CHR_TIOCM_CTS | CHR_TIOCM_CAR | CHR_TIOCM_DSR
                     | CHR_TIOCM_RI | CHR_TIOCM_DTR | CHR_TIOCM_RTS);
            if (sarg & CHR_TIOCM_CTS) {
                targ |= TIOCM_CTS;
            }
            if (sarg & CHR_TIOCM_CAR) {
                targ |= TIOCM_CAR;
            }
            if (sarg & CHR_TIOCM_DSR) {
                targ |= TIOCM_DSR;
            }
            if (sarg & CHR_TIOCM_RI) {
                targ |= TIOCM_RI;
            }
            if (sarg & CHR_TIOCM_DTR) {
                targ |= TIOCM_DTR;
            }
            if (sarg & CHR_TIOCM_RTS) {
                targ |= TIOCM_RTS;
            }
            ioctl(fioc->fd, TIOCMSET, &targ);
        }
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

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
#endif /* __linux__ || __sun__ */

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
    backend->type = CHARDEV_BACKEND_KIND_SERIAL;
    serial = backend->u.serial.data = g_new0(ChardevHostdev, 1);
    qemu_chr_parse_common(opts, qapi_ChardevHostdev_base(serial));
    serial->device = g_strdup(device);
}

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

static void register_types(void)
{
    type_register_static(&char_serial_type_info);
}

type_init(register_types);

#endif
