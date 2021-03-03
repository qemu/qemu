/*
 * Guest Loader
 *
 * Copyright (C) 2020 Linaro
 * Written by Alex Bennée <alex.bennee@linaro.org>
 * (based on the generic-loader by Li Guang <lig.fnst@cn.fujitsu.com>)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef GUEST_LOADER_H
#define GUEST_LOADER_H

#include "hw/qdev-core.h"
#include "qom/object.h"

struct GuestLoaderState {
    /* <private> */
    DeviceState parent_obj;

    /* <public> */
    uint64_t addr;
    char *kernel;
    char *args;
    char *initrd;
};

#define TYPE_GUEST_LOADER "guest-loader"
OBJECT_DECLARE_SIMPLE_TYPE(GuestLoaderState, GUEST_LOADER)

#endif
