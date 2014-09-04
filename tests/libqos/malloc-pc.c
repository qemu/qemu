/*
 * libqos malloc support for PC
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "libqos/malloc-pc.h"
#include "libqos/fw_cfg.h"

#define NO_QEMU_PROTOS
#include "hw/nvram/fw_cfg.h"

#include "qemu-common.h"
#include <glib.h>

#define PAGE_SIZE (4096)

typedef struct PCAlloc
{
    QGuestAllocator alloc;

    uint64_t start;
    uint64_t end;
} PCAlloc;

static uint64_t pc_alloc(QGuestAllocator *allocator, size_t size)
{
    PCAlloc *s = container_of(allocator, PCAlloc, alloc);
    uint64_t addr;


    size += (PAGE_SIZE - 1);
    size &= PAGE_SIZE;

    g_assert_cmpint((s->start + size), <=, s->end);

    addr = s->start;
    s->start += size;

    return addr;
}

static void pc_free(QGuestAllocator *allocator, uint64_t addr)
{
}

QGuestAllocator *pc_alloc_init(void)
{
    PCAlloc *s = g_malloc0(sizeof(*s));
    uint64_t ram_size;
    QFWCFG *fw_cfg = pc_fw_cfg_init();

    s->alloc.alloc = pc_alloc;
    s->alloc.free = pc_free;

    ram_size = qfw_cfg_get_u64(fw_cfg, FW_CFG_RAM_SIZE);

    /* Start at 1MB */
    s->start = 1 << 20;

    /* Respect PCI hole */
    s->end = MIN(ram_size, 0xE0000000);

    return &s->alloc;
}
