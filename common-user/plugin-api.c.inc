/*
 * QEMU Plugin API - *-user-mode only implementations
 *
 * Common user-mode only APIs are in plugins/api-user. These helpers
 * are only specific to the *-user frontends.
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (C) 2019-2025, Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/plugin.h"
#include "qemu.h"

/*
 * Binary path, start and end locations. Host specific due to TaskState.
 */
const char *qemu_plugin_path_to_binary(void)
{
    TaskState *ts = get_task_state(current_cpu);
    return g_strdup(ts->bprm->filename);
}

uint64_t qemu_plugin_start_code(void)
{
    TaskState *ts = get_task_state(current_cpu);
    return ts->info->start_code;
}

uint64_t qemu_plugin_end_code(void)
{
    TaskState *ts = get_task_state(current_cpu);
    return ts->info->end_code;
}

uint64_t qemu_plugin_entry_code(void)
{
    TaskState *ts = get_task_state(current_cpu);
    return ts->info->entry;
}
