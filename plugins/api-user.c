/*
 * QEMU Plugin API - user-mode only implementations
 *
 * This provides the APIs that have a user-mode specific
 * implementations or are only relevant to user-mode.
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (C) 2019-2025, Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/plugin.h"
#include "exec/log.h"

/*
 * Virtual Memory queries - these are all NOPs for user-mode which
 * only ever has visibility of virtual addresses.
 */

struct qemu_plugin_hwaddr *qemu_plugin_get_hwaddr(qemu_plugin_meminfo_t info,
                                                  uint64_t vaddr)
{
    return NULL;
}

bool qemu_plugin_hwaddr_is_io(const struct qemu_plugin_hwaddr *haddr)
{
    return false;
}

uint64_t qemu_plugin_hwaddr_phys_addr(const struct qemu_plugin_hwaddr *haddr)
{
    return 0;
}

const char *qemu_plugin_hwaddr_device_name(const struct qemu_plugin_hwaddr *h)
{
    return g_intern_static_string("Invalid");
}

/*
 * Time control - for user mode the only real time is wall clock time
 * so realistically all you can do in user mode is slow down execution
 * which doesn't require the ability to mess with the clock.
 */

const void *qemu_plugin_request_time_control(void)
{
    return NULL;
}

void qemu_plugin_update_ns(const void *handle, int64_t new_time)
{
    qemu_log_mask(LOG_UNIMP, "user-mode can't control time");
}
