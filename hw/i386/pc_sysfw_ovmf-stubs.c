/*
 * QEMU PC System Firmware (OVMF stubs)
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
#include "hw/i386/pc.h"

bool pc_system_ovmf_table_find(const char *entry, uint8_t **data, int *data_len)
{
    g_assert_not_reached();
}

void pc_system_parse_ovmf_flash(uint8_t *flash_ptr, size_t flash_size)
{
    g_assert_not_reached();
}
