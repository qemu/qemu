/*
 * libqos fw_cfg support
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_FW_CFG_H
#define LIBQOS_FW_CFG_H

#include <stdint.h>
#include <sys/types.h>

typedef struct QFWCFG QFWCFG;

struct QFWCFG
{
    uint64_t base;
    void (*select)(QFWCFG *fw_cfg, uint16_t key);
    void (*read)(QFWCFG *fw_cfg, void *data, size_t len);
};

void qfw_cfg_select(QFWCFG *fw_cfg, uint16_t key);
void qfw_cfg_read_data(QFWCFG *fw_cfg, void *data, size_t len);
void qfw_cfg_get(QFWCFG *fw_cfg, uint16_t key, void *data, size_t len);
uint16_t qfw_cfg_get_u16(QFWCFG *fw_cfg, uint16_t key);
uint32_t qfw_cfg_get_u32(QFWCFG *fw_cfg, uint16_t key);
uint64_t qfw_cfg_get_u64(QFWCFG *fw_cfg, uint16_t key);

QFWCFG *mm_fw_cfg_init(uint64_t base);
QFWCFG *io_fw_cfg_init(uint16_t base);

static inline QFWCFG *pc_fw_cfg_init(void)
{
    return io_fw_cfg_init(0x510);
}

#endif
