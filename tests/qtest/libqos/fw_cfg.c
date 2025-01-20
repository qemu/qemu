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
#include "fw_cfg.h"
#include "malloc-pc.h"
#include "libqos-malloc.h"
#include "../libqtest.h"
#include "qemu/bswap.h"
#include "hw/nvram/fw_cfg.h"

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
    qtest_writew(fw_cfg->qts, fw_cfg->base, key);
}

static void qfw_cfg_dma_transfer(QFWCFG *fw_cfg, QOSState *qs, void *address,
                                 uint32_t length, uint32_t control)
{
    FWCfgDmaAccess access;
    uint32_t addr;
    uint64_t guest_access_addr;
    uint64_t gaddr;

    /* create a data buffer in guest memory */
    gaddr = guest_alloc(&qs->alloc, length);

    if (control & FW_CFG_DMA_CTL_WRITE) {
        qtest_bufwrite(fw_cfg->qts, gaddr, address, length);
    }
    access.address = cpu_to_be64(gaddr);
    access.length = cpu_to_be32(length);
    access.control = cpu_to_be32(control);

    /* now create a separate buffer in guest memory for 'access' */
    guest_access_addr = guest_alloc(&qs->alloc, sizeof(access));
    qtest_bufwrite(fw_cfg->qts, guest_access_addr, &access, sizeof(access));

    /* write lower 32 bits of address */
    addr = cpu_to_be32((uint32_t)(uintptr_t)guest_access_addr);
    qtest_outl(fw_cfg->qts, fw_cfg->base + 8, addr);

    /* write upper 32 bits of address */
    addr = cpu_to_be32((uint32_t)(uintptr_t)(guest_access_addr >> 32));
    qtest_outl(fw_cfg->qts, fw_cfg->base + 4, addr);

    g_assert(!(be32_to_cpu(access.control) & FW_CFG_DMA_CTL_ERROR));

    if (control & FW_CFG_DMA_CTL_READ) {
        qtest_bufread(fw_cfg->qts, gaddr, address, length);
    }

    guest_free(&qs->alloc, guest_access_addr);
    guest_free(&qs->alloc, gaddr);
}

static void qfw_cfg_write_entry(QFWCFG *fw_cfg, QOSState *qs, uint16_t key,
                                void *buf, uint32_t len)
{
    qfw_cfg_select(fw_cfg, key);
    qfw_cfg_dma_transfer(fw_cfg, qs, buf, len, FW_CFG_DMA_CTL_WRITE);
}

static void qfw_cfg_read_entry(QFWCFG *fw_cfg, QOSState *qs, uint16_t key,
                               void *buf, uint32_t len)
{
    qfw_cfg_select(fw_cfg, key);
    qfw_cfg_dma_transfer(fw_cfg, qs, buf, len, FW_CFG_DMA_CTL_READ);
}

static bool find_pdir_entry(QFWCFG *fw_cfg, const char *filename,
                            uint16_t *sel, uint32_t *size)
{
    g_autofree unsigned char *filesbuf = NULL;
    uint32_t count;
    size_t dsize;
    FWCfgFile *pdir_entry;
    uint32_t i;
    bool found = false;

    *size = 0;
    *sel = 0;

    qfw_cfg_get(fw_cfg, FW_CFG_FILE_DIR, &count, sizeof(count));
    count = be32_to_cpu(count);
    dsize = sizeof(uint32_t) + count * sizeof(struct fw_cfg_file);
    filesbuf = g_malloc(dsize);
    qfw_cfg_get(fw_cfg, FW_CFG_FILE_DIR, filesbuf, dsize);
    pdir_entry = (FWCfgFile *)(filesbuf + sizeof(uint32_t));
    for (i = 0; i < count; ++i, ++pdir_entry) {
        if (!strcmp(pdir_entry->name, filename)) {
            *size = be32_to_cpu(pdir_entry->size);
            *sel = be16_to_cpu(pdir_entry->select);
            found = true;
            break;
        }
    }

    return found;
}

/*
 * The caller need check the return value. When the return value is
 * nonzero, it means that some bytes have been transferred.
 *
 * If the fw_cfg file in question is smaller than the allocated & passed-in
 * buffer, then the buffer has been populated only in part.
 *
 * If the fw_cfg file in question is larger than the passed-in
 * buffer, then the return value explains how much room would have been
 * necessary in total. And, while the caller's buffer has been fully
 * populated, it has received only a starting slice of the fw_cfg file.
 */
size_t qfw_cfg_get_file(QFWCFG *fw_cfg, const char *filename,
                        void *data, size_t buflen)
{
    size_t filesize = 0;
    uint32_t len;
    uint16_t sel;

    if (find_pdir_entry(fw_cfg, filename, &sel, &len)) {
        filesize = len;
        if (len > buflen) {
            len = buflen;
        }
        qfw_cfg_get(fw_cfg, sel, data, len);
    }

    return filesize;
}

/*
 * The caller need check the return value. When the return value is
 * nonzero, it means that some bytes have been transferred.
 *
 * If the fw_cfg file in question is smaller than the allocated & passed-in
 * buffer, then the first len bytes were read.
 *
 * If the fw_cfg file in question is larger than the passed-in
 * buffer, then the return value explains how much was actually read.
 *
 * It is illegal to call this function if fw_cfg does not support DMA
 * interface. The caller should ensure that DMA is supported before
 * calling this function.
 *
 * Passed QOSState pointer qs must be initialized. qs->alloc must also be
 * properly initialized.
 */
size_t qfw_cfg_read_file(QFWCFG *fw_cfg, QOSState *qs, const char *filename,
                         void *data, size_t buflen)
{
    uint32_t len = 0;
    uint16_t sel;
    uint32_t id;

    g_assert(qs);
    g_assert(filename);
    g_assert(data);
    g_assert(buflen);
    /* check if DMA is supported since we use DMA for read */
    id = qfw_cfg_get_u32(fw_cfg, FW_CFG_ID);
    g_assert(id & FW_CFG_VERSION_DMA);

    if (find_pdir_entry(fw_cfg, filename, &sel, &len)) {
        if (len > buflen) {
            len = buflen;
        }
        qfw_cfg_read_entry(fw_cfg, qs, sel, data, len);
    }

    return len;
}

/*
 * The caller need check the return value. When the return value is
 * nonzero, it means that some bytes have been transferred.
 *
 * If the fw_cfg file in question is smaller than the allocated & passed-in
 * buffer, then the buffer has been partially written.
 *
 * If the fw_cfg file in question is larger than the passed-in
 * buffer, then the return value explains how much was actually written.
 *
 * It is illegal to call this function if fw_cfg does not support DMA
 * interface. The caller should ensure that DMA is supported before
 * calling this function.
 *
 * Passed QOSState pointer qs must be initialized. qs->alloc must also be
 * properly initialized.
 */
size_t qfw_cfg_write_file(QFWCFG *fw_cfg, QOSState *qs, const char *filename,
                          void *data, size_t buflen)
{
    uint32_t len = 0;
    uint16_t sel;
    uint32_t id;

    g_assert(qs);
    g_assert(filename);
    g_assert(data);
    g_assert(buflen);
    /* write operation is only valid if DMA is supported */
    id = qfw_cfg_get_u32(fw_cfg, FW_CFG_ID);
    g_assert(id & FW_CFG_VERSION_DMA);

    if (find_pdir_entry(fw_cfg, filename, &sel, &len)) {
        if (len > buflen) {
            len = buflen;
        }
        qfw_cfg_write_entry(fw_cfg, qs, sel, data, len);
    }
    return len;
}

static void mm_fw_cfg_read(QFWCFG *fw_cfg, void *data, size_t len)
{
    uint8_t *ptr = data;
    int i;

    for (i = 0; i < len; i++) {
        ptr[i] = qtest_readb(fw_cfg->qts, fw_cfg->base + 2);
    }
}

QFWCFG *mm_fw_cfg_init(QTestState *qts, uint64_t base)
{
    QFWCFG *fw_cfg = g_malloc0(sizeof(*fw_cfg));

    fw_cfg->base = base;
    fw_cfg->qts = qts;
    fw_cfg->select = mm_fw_cfg_select;
    fw_cfg->read = mm_fw_cfg_read;

    return fw_cfg;
}

void mm_fw_cfg_uninit(QFWCFG *fw_cfg)
{
    g_free(fw_cfg);
}

static void io_fw_cfg_select(QFWCFG *fw_cfg, uint16_t key)
{
    qtest_outw(fw_cfg->qts, fw_cfg->base, key);
}

static void io_fw_cfg_read(QFWCFG *fw_cfg, void *data, size_t len)
{
    uint8_t *ptr = data;
    int i;

    for (i = 0; i < len; i++) {
        ptr[i] = qtest_inb(fw_cfg->qts, fw_cfg->base + 1);
    }
}

QFWCFG *io_fw_cfg_init(QTestState *qts, uint16_t base)
{
    QFWCFG *fw_cfg = g_malloc0(sizeof(*fw_cfg));

    fw_cfg->base = base;
    fw_cfg->qts = qts;
    fw_cfg->select = io_fw_cfg_select;
    fw_cfg->read = io_fw_cfg_read;

    return fw_cfg;
}

void io_fw_cfg_uninit(QFWCFG *fw_cfg)
{
    g_free(fw_cfg);
}
