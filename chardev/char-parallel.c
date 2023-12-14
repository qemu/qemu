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
#include "chardev/char.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include <sys/ioctl.h>

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
#endif

#include "chardev/char-fd.h"
#include "chardev/char-parallel.h"

#if defined(__linux__)

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
        if (ioctl(s->fd, PPSETMODE, &m) < 0) {
            return 0;
        }
        s->mode = mode;
    }
    return 1;
}

static int pp_ioctl(Chardev *chr, int cmd, void *arg)
{
    ParallelChardev *drv = PARALLEL_CHARDEV(chr);
    int fd = drv->fd;
    uint8_t b;

    switch (cmd) {
    case CHR_IOCTL_PP_READ_DATA:
        if (ioctl(fd, PPRDATA, &b) < 0) {
            return -ENOTSUP;
        }
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_WRITE_DATA:
        b = *(uint8_t *)arg;
        if (ioctl(fd, PPWDATA, &b) < 0) {
            return -ENOTSUP;
        }
        break;
    case CHR_IOCTL_PP_READ_CONTROL:
        if (ioctl(fd, PPRCONTROL, &b) < 0) {
            return -ENOTSUP;
        }
        /* Linux gives only the lowest bits, and no way to know data
           direction! For better compatibility set the fixed upper
           bits. */
        *(uint8_t *)arg = b | 0xc0;
        break;
    case CHR_IOCTL_PP_WRITE_CONTROL:
        b = *(uint8_t *)arg;
        if (ioctl(fd, PPWCONTROL, &b) < 0) {
            return -ENOTSUP;
        }
        break;
    case CHR_IOCTL_PP_READ_STATUS:
        if (ioctl(fd, PPRSTATUS, &b) < 0) {
            return -ENOTSUP;
        }
        *(uint8_t *)arg = b;
        break;
    case CHR_IOCTL_PP_DATA_DIR:
        if (ioctl(fd, PPDATADIR, (int *)arg) < 0) {
            return -ENOTSUP;
        }
        break;
    case CHR_IOCTL_PP_EPP_READ_ADDR:
        if (pp_hw_mode(drv, IEEE1284_MODE_EPP | IEEE1284_ADDR)) {
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
        if (pp_hw_mode(drv, IEEE1284_MODE_EPP | IEEE1284_ADDR)) {
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

static void qemu_chr_parse_parallel(QemuOpts *opts, ChardevBackend *backend,
                                    Error **errp)
{
    const char *device = qemu_opt_get(opts, "path");
    ChardevHostdev *parallel;

    if (device == NULL) {
        error_setg(errp, "chardev: parallel: no device path given");
        return;
    }
    backend->type = CHARDEV_BACKEND_KIND_PARALLEL;
    parallel = backend->u.parallel.data = g_new0(ChardevHostdev, 1);
    qemu_chr_parse_common(opts, qapi_ChardevHostdev_base(parallel));
    parallel->device = g_strdup(device);
}

static void char_parallel_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = qemu_chr_parse_parallel;
    cc->open = qmp_chardev_open_parallel;
    cc->chr_ioctl = pp_ioctl;
}

static void char_parallel_finalize(Object *obj)
{
    Chardev *chr = CHARDEV(obj);
    ParallelChardev *drv = PARALLEL_CHARDEV(chr);
    int fd = drv->fd;

#if defined(__linux__)
    pp_hw_mode(drv, IEEE1284_MODE_COMPAT);
    ioctl(fd, PPRELEASE);
#endif
    close(fd);
    qemu_chr_be_event(chr, CHR_EVENT_CLOSED);
}

static const TypeInfo char_parallel_type_info = {
    .name = TYPE_CHARDEV_PARALLEL,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(ParallelChardev),
    .instance_finalize = char_parallel_finalize,
    .class_init = char_parallel_class_init,
};

static void register_types(void)
{
    type_register_static(&char_parallel_type_info);
}

type_init(register_types);
