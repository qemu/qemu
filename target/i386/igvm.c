/*
 * QEMU IGVM, support for native x86 guests
 *
 * Copyright (C) 2026 Red Hat
 *
 * Authors:
 *  Gerd Hoffmann <kraxel@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/i386/e820_memory_layout.h"
#include "system/igvm.h"

/*
 * convert e820 table into igvm memory map
 */
int qigvm_x86_get_mem_map_entry(int index,
                                ConfidentialGuestMemoryMapEntry *entry,
                                Error **errp)
{
    struct e820_entry *table;
    int num_entries;

    num_entries = e820_get_table(&table);
    if ((index < 0) || (index >= num_entries)) {
        return 1;
    }
    entry->gpa = table[index].address;
    entry->size = table[index].length;
    switch (table[index].type) {
    case E820_RAM:
        entry->type = CGS_MEM_RAM;
        break;
    case E820_RESERVED:
        entry->type = CGS_MEM_RESERVED;
        break;
    default:
        /* should not happen */
        error_setg(errp, "unknown e820 type");
        return -1;
    }
    return 0;
}
