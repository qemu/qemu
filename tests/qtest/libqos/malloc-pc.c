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
#include "malloc-pc.h"
#include "fw_cfg.h"

#include "standard-headers/linux/qemu_fw_cfg.h"

#define ALLOC_PAGE_SIZE (4096)

void pc_alloc_init(QGuestAllocator *s, QTestState *qts, QAllocOpts flags)
{
    uint64_t ram_size;
    QFWCFG *fw_cfg = pc_fw_cfg_init(qts);

    ram_size = qfw_cfg_get_u64(fw_cfg, FW_CFG_RAM_SIZE);
    alloc_init(s, flags, 1 << 20, MIN(ram_size, 0xE0000000), ALLOC_PAGE_SIZE);

    /* clean-up */
    pc_fw_cfg_uninit(fw_cfg);
}
