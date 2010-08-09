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
        /*
         * We can't cancel Scatter Gather DMA in the middle of the
         * operation or a partial (not full) DMA transfer would reach
         * the storage so we wait for completion instead (we beahve
         * like if the DMA was completed by the time the guest trying
         * to cancel dma with bmdma_cmd_writeb with BM_CMD_START not
         * set).
         *
         * In the future we'll be able to safely cancel the I/O if the
         * whole DMA operation will be submitted to disk with a single
         * aio operation with preadv/pwritev.
         */
        if (bm->aiocb) {
            qemu_aio_flush();
#ifdef DEBUG_IDE
            if (bm->aiocb)
                printf("ide_dma_cancel: aiocb still pending");
            if (bm->status & BM_STATUS_DMAING)
                printf("ide_dma_cancel: BM_STATUS_DMAING still pending");
#endif
        }
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

static bool ide_bmdma_current_needed(void *opaque)
{
    BMDMAState *bm = opaque;

    return (bm->cur_prd_len != 0);
}

static const VMStateDescription vmstate_bmdma_current = {
    .name = "ide bmdma_current",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(cur_addr, BMDMAState),
        VMSTATE_UINT32(cur_prd_last, BMDMAState),
        VMSTATE_UINT32(cur_prd_addr, BMDMAState),
        VMSTATE_UINT32(cur_prd_len, BMDMAState),
        VMSTATE_END_OF_LIST()
    }
};


static const VMStateDescription vmstate_bmdma = {
    .name = "ide bmdma",
    .version_id = 3,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField []) {
        VMSTATE_UINT8(cmd, BMDMAState),
        VMSTATE_UINT8(status, BMDMAState),
        VMSTATE_UINT32(addr, BMDMAState),
        VMSTATE_INT64(sector_num, BMDMAState),
        VMSTATE_UINT32(nsector, BMDMAState),
        VMSTATE_UINT8(unit, BMDMAState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (VMStateSubsection []) {
        {
            .vmsd = &vmstate_bmdma_current,
            .needed = ide_bmdma_current_needed,
        }, {
            /* empty */
        }
    }
};

static int ide_pci_post_load(void *opaque, int version_id)
{
    PCIIDEState *d = opaque;
    int i;

    for(i = 0; i < 2; i++) {
        /* current versions always store 0/1, but older version
           stored bigger values. We only need last bit */
        d->bmdma[i].unit &= 1;
    }
    return 0;
}

const VMStateDescription vmstate_ide_pci = {
    .name = "ide",
    .version_id = 3,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .post_load = ide_pci_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(dev, PCIIDEState),
        VMSTATE_STRUCT_ARRAY(bmdma, PCIIDEState, 2, 0,
                             vmstate_bmdma, BMDMAState),
        VMSTATE_IDE_BUS_ARRAY(bus, PCIIDEState, 2),
        VMSTATE_IDE_DRIVES(bus[0].ifs, PCIIDEState),
        VMSTATE_IDE_DRIVES(bus[1].ifs, PCIIDEState),
        VMSTATE_END_OF_LIST()
    }
};

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
