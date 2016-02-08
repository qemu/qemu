/*
 * libqos fw_cfg support
 *
 * Copyright IBM, Corp. 2012-2013
 * Copyright (C) 2013 Red Hat Inc.
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>
#include "libqos/fw_cfg.h"
#include "libqtest.h"
#include "qemu/bswap.h"

void qfw_cfg_select(QFWCFG *fw_cfg, uint16_t key)
{
    fw_cfg->select(fw_cfg, key);
}

void qfw_cfg_read_data(QFWCFG *fw_cfg, void *data, size_t len)
{
    fw_cfg->read(fw_cfg, data, len);
}

void qfw_cfg_get(QFWCFG *fw_cfg, uint16_t key, void *data, size_t len)
{
    qfw_cfg_select(fw_cfg, key);
    qfw_cfg_read_data(fw_cfg, data, len);
}

uint16_t qfw_cfg_get_u16(QFWCFG *fw_cfg, uint16_t key)
{
    uint16_t value;
    qfw_cfg_get(fw_cfg, key, &value, sizeof(value));
    return le16_to_cpu(value);
}

uint32_t qfw_cfg_get_u32(QFWCFG *fw_cfg, uint16_t key)
{
    uint32_t value;
    qfw_cfg_get(fw_cfg, key, &value, sizeof(value));
    return le32_to_cpu(value);
}

uint64_t qfw_cfg_get_u64(QFWCFG *fw_cfg, uint16_t key)
{
    uint64_t value;
    qfw_cfg_get(fw_cfg, key, &value, sizeof(value));
    return le64_to_cpu(value);
}

static void mm_fw_cfg_select(QFWCFG *fw_cfg, uint16_t key)
{
    writew(fw_cfg->base, key);
}

static void mm_fw_cfg_read(QFWCFG *fw_cfg, void *data, size_t len)
{
    uint8_t *ptr = data;
    int i;

    for (i = 0; i < len; i++) {
        ptr[i] = readb(fw_cfg->base + 2);
    }
}

QFWCFG *mm_fw_cfg_init(uint64_t base)
{
    QFWCFG *fw_cfg = g_malloc0(sizeof(*fw_cfg));

    fw_cfg->base = base;
    fw_cfg->select = mm_fw_cfg_select;
    fw_cfg->read = mm_fw_cfg_read;

    return fw_cfg;
}

static void io_fw_cfg_select(QFWCFG *fw_cfg, uint16_t key)
{
    outw(fw_cfg->base, key);
}

static void io_fw_cfg_read(QFWCFG *fw_cfg, void *data, size_t len)
{
    uint8_t *ptr = data;
    int i;

    for (i = 0; i < len; i++) {
        ptr[i] = inb(fw_cfg->base + 1);
    }
}

QFWCFG *io_fw_cfg_init(uint16_t base)
{
    QFWCFG *fw_cfg = g_malloc0(sizeof(*fw_cfg));

    fw_cfg->base = base;
    fw_cfg->select = io_fw_cfg_select;
    fw_cfg->read = io_fw_cfg_read;

    return fw_cfg;
}
