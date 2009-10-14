/*
 * QEMU IDE Emulation: PCI Bus support.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <hw/hw.h>
#include <hw/pc.h>
#include <hw/pci.h>
#include <hw/isa.h>
#include "block.h"
#include "block_int.h"
#include "sysemu.h"
#include "dma.h"

#include <hw/ide/pci.h>

void bmdma_cmd_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    if (!(val & BM_CMD_START)) {
        /* XXX: do it better */
        ide_dma_cancel(bm);
        bm->cmd = val & 0x09;
    } else {
        if (!(bm->status & BM_STATUS_DMAING)) {
            bm->status |= BM_STATUS_DMAING;
            /* start dma transfer if possible */
            if (bm->dma_cb)
                bm->dma_cb(bm, 0);
        }
        bm->cmd = val & 0x09;
    }
}

uint32_t bmdma_addr_readb(void *opaque, uint32_t addr)
{
    BMDMAState *bm = opaque;
    uint32_t val;
    val = (bm->addr >> ((addr & 3) * 8)) & 0xff;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    return val;
}

void bmdma_addr_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
    int shift = (addr & 3) * 8;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    bm->addr &= ~(0xFF << shift);
    bm->addr |= ((val & 0xFF) << shift) & ~3;
    bm->cur_addr = bm->addr;
}

uint32_t bmdma_addr_readw(void *opaque, uint32_t addr)
{
    BMDMAState *bm = opaque;
    uint32_t val;
    val = (bm->addr >> ((addr & 3) * 8)) & 0xffff;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    return val;
}

void bmdma_addr_writew(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
    int shift = (addr & 3) * 8;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    bm->addr &= ~(0xFFFF << shift);
    bm->addr |= ((val & 0xFFFF) << shift) & ~3;
    bm->cur_addr = bm->addr;
}

uint32_t bmdma_addr_readl(void *opaque, uint32_t addr)
{
    BMDMAState *bm = opaque;
    uint32_t val;
    val = bm->addr;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    return val;
}

void bmdma_addr_writel(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    bm->addr = val & ~3;
    bm->cur_addr = bm->addr;
}

void pci_ide_save(QEMUFile* f, void *opaque)
{
    PCIIDEState *d = opaque;
    int i;

    pci_device_save(&d->dev, f);

    for(i = 0; i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];
        uint8_t ifidx;
        qemu_put_8s(f, &bm->cmd);
        qemu_put_8s(f, &bm->status);
        qemu_put_be32s(f, &bm->addr);
        qemu_put_sbe64s(f, &bm->sector_num);
        qemu_put_be32s(f, &bm->nsector);
        ifidx = bm->unit + 2*i;
        qemu_put_8s(f, &ifidx);
        /* XXX: if a transfer is pending, we do not save it yet */
    }

    /* per IDE interface data */
    for(i = 0; i < 2; i++) {
        idebus_save(f, d->bus+i);
    }

    /* per IDE drive data */
    for(i = 0; i < 2; i++) {
        ide_save(f, &d->bus[i].ifs[0]);
        ide_save(f, &d->bus[i].ifs[1]);
    }
}

int pci_ide_load(QEMUFile* f, void *opaque, int version_id)
{
    PCIIDEState *d = opaque;
    int ret, i;

    if (version_id != 2 && version_id != 3)
        return -EINVAL;
    ret = pci_device_load(&d->dev, f);
    if (ret < 0)
        return ret;

    for(i = 0; i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];
        uint8_t ifidx;
        qemu_get_8s(f, &bm->cmd);
        qemu_get_8s(f, &bm->status);
        qemu_get_be32s(f, &bm->addr);
        qemu_get_sbe64s(f, &bm->sector_num);
        qemu_get_be32s(f, &bm->nsector);
        qemu_get_8s(f, &ifidx);
        bm->unit = ifidx & 1;
        /* XXX: if a transfer is pending, we do not save it yet */
    }

    /* per IDE interface data */
    for(i = 0; i < 2; i++) {
        idebus_load(f, d->bus+i, version_id);
    }

    /* per IDE drive data */
    for(i = 0; i < 2; i++) {
        ide_load(f, &d->bus[i].ifs[0], version_id);
        ide_load(f, &d->bus[i].ifs[1], version_id);
    }
    return 0;
}

void pci_ide_create_devs(PCIDevice *dev, DriveInfo **hd_table)
{
    PCIIDEState *d = DO_UPCAST(PCIIDEState, dev, dev);
    static const int bus[4]  = { 0, 0, 1, 1 };
    static const int unit[4] = { 0, 1, 0, 1 };
    int i;

    for (i = 0; i < 4; i++) {
        if (hd_table[i] == NULL)
            continue;
        ide_create_drive(d->bus+bus[i], unit[i], hd_table[i]);
    }
}
