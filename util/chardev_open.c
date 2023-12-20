/*
 * Copyright (c) 2019, Mellanox Technologies. All rights reserved.
 * Copyright (C) 2023 Intel Corporation.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *      Redistribution and use in source and binary forms, with or
 *      without modification, are permitted provided that the following
 *      conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *
 * Copied from
 * https://github.com/linux-rdma/rdma-core/blob/master/util/open_cdev.c
 *
 */

#include "qemu/osdep.h"
#include "qemu/chardev_open.h"

static int open_cdev_internal(const char *path, dev_t cdev)
{
    struct stat st;
    int fd;

    fd = qemu_open_old(path, O_RDWR);
    if (fd == -1) {
        return -1;
    }
    if (fstat(fd, &st) || !S_ISCHR(st.st_mode) ||
        (cdev != 0 && st.st_rdev != cdev)) {
        close(fd);
        return -1;
    }
    return fd;
}

static int open_cdev_robust(dev_t cdev)
{
    g_autofree char *devpath = NULL;

    /*
     * This assumes that udev is being used and is creating the /dev/char/
     * symlinks.
     */
    devpath = g_strdup_printf("/dev/char/%u:%u", major(cdev), minor(cdev));
    return open_cdev_internal(devpath, cdev);
}

int open_cdev(const char *devpath, dev_t cdev)
{
    int fd;

    fd = open_cdev_internal(devpath, cdev);
    if (fd == -1 && cdev != 0) {
        return open_cdev_robust(cdev);
    }
    return fd;
}
