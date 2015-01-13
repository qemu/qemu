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

/*
 * Mostly for valgrind happiness, but it does offer
 * a chokepoint for debugging guest memory leaks, too.
 */
void pc_alloc_uninit(QGuestAllocator *allocator)
{
    alloc_uninit(allocator);
}

QGuestAllocator *pc_alloc_init_flags(QAllocOpts flags)
{
    QGuestAllocator *s = g_malloc0(sizeof(*s));
    uint64_t ram_size;
    QFWCFG *fw_cfg = pc_fw_cfg_init();
    MemBlock *node;

    s->opts = flags;
    s->page_size = PAGE_SIZE;

    ram_size = qfw_cfg_get_u64(fw_cfg, FW_CFG_RAM_SIZE);

    /* Start at 1MB */
    s->start = 1 << 20;

    /* Respect PCI hole */
    s->end = MIN(ram_size, 0xE0000000);

    /* clean-up */
    g_free(fw_cfg);

    QTAILQ_INIT(&s->used);
    QTAILQ_INIT(&s->free);

    node = mlist_new(s->start, s->end - s->start);
    QTAILQ_INSERT_HEAD(&s->free, node, MLIST_ENTNAME);

    return s;
}

inline QGuestAllocator *pc_alloc_init(void)
{
    return pc_alloc_init_flags(ALLOC_NO_FLAGS);
}
