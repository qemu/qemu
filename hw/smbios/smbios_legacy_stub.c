/*
 * IPMI SMBIOS firmware handling
 *
 * Copyright (c) 2024 Igor Mammedov, Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/firmware/smbios.h"

void smbios_add_usr_blob_size(size_t size)
{
}

uint8_t *smbios_get_table_legacy(size_t *length, Error **errp)
{
    g_assert_not_reached();
}
