/*
 * QOM stubs
 *
 * Copyright (c) 2021 Red Hat, Inc.
 *
 * Author:
 *   Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/qapi-events-qdev.h"

void qapi_event_send_device_deleted(bool has_device,
                                    const char *device,
                                    const char *path)
{
    /* Nothing to do. */
}

void qapi_event_send_device_unplug_guest_error(bool has_device,
                                               const char *device,
                                               const char *path)
{
    /* Nothing to do. */
}
