/*
 * QEMU BIOS e820 routines
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "e820_memory_layout.h"

static size_t e820_entries;
static struct e820_entry *e820_table;
static gboolean e820_done;

void e820_add_entry(uint64_t address, uint64_t length, uint32_t type)
{
    assert(!e820_done);

    /* new "etc/e820" file -- include ram and reserved entries */
    e820_table = g_renew(struct e820_entry, e820_table, e820_entries + 1);
    e820_table[e820_entries].address = cpu_to_le64(address);
    e820_table[e820_entries].length = cpu_to_le64(length);
    e820_table[e820_entries].type = cpu_to_le32(type);
    e820_entries++;
}

int e820_get_table(struct e820_entry **table)
{
    e820_done = true;

    if (table) {
        *table = e820_table;
    }

    return e820_entries;
}

bool e820_get_entry(int idx, uint32_t type, uint64_t *address, uint64_t *length)
{
    if (idx < e820_entries && e820_table[idx].type == cpu_to_le32(type)) {
        *address = le64_to_cpu(e820_table[idx].address);
        *length = le64_to_cpu(e820_table[idx].length);
        return true;
    }
    return false;
}
