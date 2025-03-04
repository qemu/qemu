/*
 * QEMU Plugin system-emulation helpers
 *
 * Helpers that are specific to system emulation.
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (C) 2019-2025, Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/plugin.h"
#include "hw/boards.h"

#include "plugin.h"

void qemu_plugin_fillin_mode_info(qemu_info_t *info)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    info->system_emulation = true;
    info->system.smp_vcpus = ms->smp.cpus;
    info->system.max_vcpus = ms->smp.max_cpus;
}
