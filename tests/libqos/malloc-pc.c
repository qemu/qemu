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

#include "qemu/osdep.h"
#include "libqos/malloc-pc.h"
#include "libqos/fw_cfg.h"

#include "hw/nvram/fw_cfg_keys.h"

#include "qemu-common.h"

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
    QGuestAllocator *s;
    uint64_t ram_size;
    QFWCFG *fw_cfg = pc_fw_cfg_init();

    ram_size = qfw_cfg_get_u64(fw_cfg, FW_CFG_RAM_SIZE);
    s = alloc_init_flags(flags, 1 << 20, MIN(ram_size, 0xE0000000));
    alloc_set_page_size(s, PAGE_SIZE);

    /* clean-up */
    g_free(fw_cfg);

    return s;
}

inline QGuestAllocator *pc_alloc_init(void)
{
    return pc_alloc_init_flags(ALLOC_NO_FLAGS);
}
