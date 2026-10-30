/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hmx_config.h"

void hmx_init_config(HexagonCPU *cpu)
{
    HexagonVersion ver = cpu->cfg.hex_def->hex_version;

    cpu->hmx_cfg.hmx_present = ver >= HEX_VER_V68;
}
