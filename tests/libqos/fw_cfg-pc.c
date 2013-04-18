/*
 * libqos fw_cfg support for PC
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "libqos/fw_cfg-pc.h"
#include "libqtest.h"
#include <glib.h>

static void pc_fw_cfg_select(QFWCFG *fw_cfg, uint16_t key)
{
    outw(0x510, key);
}

static void pc_fw_cfg_read(QFWCFG *fw_cfg, void *data, size_t len)
{
    uint8_t *ptr = data;
    int i;

    for (i = 0; i < len; i++) {
        ptr[i] = inb(0x511);
    }
}

QFWCFG *pc_fw_cfg_init(void)
{
    QFWCFG *fw_cfg = g_malloc0(sizeof(*fw_cfg));

    fw_cfg->select = pc_fw_cfg_select;
    fw_cfg->read = pc_fw_cfg_read;

    return fw_cfg;
}
