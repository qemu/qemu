/*
 * QEMU Plugin user-mode helpers
 *
 * Helpers that are specific to user-mode.
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (C) 2019-2025, Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/plugin.h"
#include "plugin.h"

void qemu_plugin_fillin_mode_info(qemu_info_t *info)
{
    info->system_emulation = false;
}
