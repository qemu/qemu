/*
 * QEMU Chardev Helper
 *
 * Copyright (C) 2023 Intel Corporation.
 *
 * Authors: Yi Liu <yi.l.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef QEMU_CHARDEV_OPEN_H
#define QEMU_CHARDEV_OPEN_H

int open_cdev(const char *devpath, dev_t cdev);
#endif
