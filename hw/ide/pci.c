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
#include <hw/i386/pc.h>
#include <hw/pci/pci.h>
#include <hw/isa/isa.h>
#include "block/block.h"
#include "sysemu/dma.h"

#include <hw/ide/pci.h>

#define BMDMA_PAGE_SIZE 4096

static void bmdma_start_dma(IDEDMA *dma, IDEState *s,
                            BlockDriverCompletionFunc *dma_cb)
{
    BMDMAState *bm = DO_UPCAST(BMDMAState, dma, dma);

    bm->unit = s->unit;
    bm->dma_cb = dma_cb;
    bm->cur_prd_last = 0;
    bm->cur_prd_addr = 0;
    bm->cur_prd_len = 0;
    bm->sector_num = ide_get_sector(s);
    bm->nsector = s->nsector;

    if (bm->status & BM_STATUS_DMAING) {
        bm->dma_cb(bmdma_active_if(bm), 0);
    }
}

/* return 0 if buffer completed */
static int bmdma_prepare_buf(IDEDMA *dma, int is_write)
{
    BMDMAState *bm = DO_UPCAST(BMDMAState, dma, dma);
    IDEState *s = bmdma_active_if(bm);
    PCIDevice *pci_dev = PCI_DEVICE(bm->pci_dev);
    struct {
        uint32_t addr;
        uint32_t size;
    } prd;
    int l, len;

    pci_dma_sglist_init(&s->sg, pci_dev,
                        s->nsector / (BMDMA_PAGE_SIZE / 512) + 1);
    s->io_buffer_size = 0;
    for(;;) {
        if (bm->cur_prd_len == 0) {
            /* end of table (with a fail safe of one page) */
            if (bm->cur_prd_last ||
                (bm->cur_addr - bm->addr) >= BMDMA_PAGE_SIZE)
                return s->io_buffer_size != 0;
            pci_dma_read(pci_dev, bm->cur_addr, &prd, 8);
            bm->cur_addr += 8;
            prd.addr = le32_to_cpu(prd.addr);
            prd.size = le32_to_cpu(prd.size);
            len = prd.size & 0xfffe;
            if (len == 0)
                len = 0x10000;
            bm->cur_prd_len = len;
            bm->cur_prd_addr = prd.addr;
            bm->cur_prd_last = (prd.size & 0x80000000);
        }
        l = bm->cur_prd_len;
        if (l > 0) {
            qemu_sglist_add(&s->sg, bm->cur_prd_addr, l);
            bm->cur_prd_addr += l;
            bm->cur_prd_len -= l;
            s->io_buffer_size += l;
        }
    }
    return 1;
}

/* return 0 if buffer completed */
static int bmdma_rw_buf(IDEDMA *dma, int is_write)
{
    BMDMAState *bm = DO_UPCAST(BMDMAState, dma, dma);
    IDEState *s = bmdma_active_if(bm);
    PCIDevice *pci_dev = PCI_DEVICE(bm->pci_dev);
    struct {
        uint32_t addr;
        uint32_t size;
    } prd;
    int l, len;

    for(;;) {
        l = s->io_buffer_size - s->io_buffer_index;
        if (l <= 0)
            break;
        if (bm->cur_prd_len == 0) {
            /* end of table (with a fail safe of one page) */
            if (bm->cur_prd_last ||
                (bm->cur_addr - bm->addr) >= BMDMA_PAGE_SIZE)
                return 0;
            pci_dma_read(pci_dev, bm->cur_addr, &prd, 8);
            bm->cur_addr += 8;
            prd.addr = le32_to_cpu(prd.addr);
            prd.size = le32_to_cpu(prd.size);
            len = prd.size & 0xfffe;
            if (len == 0)
                len = 0x10000;
            bm->cur_prd_len = len;
            bm->cur_prd_addr = prd.addr;
            bm->cur_prd_last = (prd.size & 0x80000000);
        }
        if (l > bm->cur_prd_len)
            l = bm->cur_prd_len;
        if (l > 0) {
            if (is_write) {
                pci_dma_write(pci_dev, bm->cur_prd_addr,
                              s->io_buffer + s->io_buffer_index, l);
            } else {
                pci_dma_read(pci_dev, bm->cur_prd_addr,
                             s->io_buffer + s->io_buffer_index, l);
            }
            bm->cur_prd_addr += l;
            bm->cur_prd_len -= l;
            s->io_buffer_index += l;
        }
    }
    return 1;
}

static int bmdma_set_unit(IDEDMA *dma, int unit)
{
    BMDMAState *bm = DO_UPCAST(BMDMAState, dma, dma);
    bm->unit = unit;

    return 0;
}

static int bmdma_add_status(IDEDMA *dma, int status)
{
    BMDMAState *bm = DO_UPCAST(BMDMAState, dma, dma);
    bm->status |= status;

    return 0;
}

static void bmdma_set_inactive(IDEDMA *dma)
{
    BMDMAState *bm = DO_UPCAST(BMDMAState, dma, dma);

    bm->status &= ~BM_STATUS_DMAING;
    bm->dma_cb = NULL;
    bm->unit = -1;
}

static void bmdma_restart_dma(BMDMAState *bm, enum ide_dma_cmd dma_cmd)
{
    IDEState *s = bmdma_active_if(bm);

    ide_set_sector(s, bm->sector_num);
    s->io_buffer_index = 0;
    s->io_buffer_size = 0;
    s->nsector = bm->nsector;
    s->dma_cmd = dma_cmd;
    bm->cur_addr = bm->addr;
    bm->dma_cb = ide_dma_cb;
    bmdma_start_dma(&bm->dma, s, bm->dma_cb);
}

/* TODO This should be common IDE code */
static void bmdma_restart_bh(void *opaque)
{
    BMDMAState *bm = opaque;
    IDEBus *bus = bm->bus;
    bool is_read;
    int error_status;

    qemu_bh_delete(bm->bh);
    bm->bh = NULL;

    if (bm->unit == (uint8_t) -1) {
        return;
    }

    is_read = (bus->error_status & BM_STATUS_RETRY_READ) != 0;

    /* The error status must be cleared before resubmitting the request: The
     * request may fail again, and this case can only be distinguished if the
     * called function can set a new error status. */
    error_status = bus->error_status;
    bus->error_status = 0;

    if (error_status & BM_STATUS_DMA_RETRY) {
        if (error_status & BM_STATUS_RETRY_TRIM) {
            bmdma_restart_dma(bm, IDE_DMA_TRIM);
        } else {
            bmdma_restart_dma(bm, is_read ? IDE_DMA_READ : IDE_DMA_WRITE);
        }
    } else if (error_status & BM_STATUS_PIO_RETRY) {
        if (is_read) {
            ide_sector_read(bmdma_active_if(bm));
        } else {
            ide_sector_write(bmdma_active_if(bm));
        }
    } else if (error_status & BM_STATUS_RETRY_FLUSH) {
        ide_flush_cache(bmdma_active_if(bm));
    }
}

static void bmdma_restart_cb(void *opaque, int running, RunState state)
{
    IDEDMA *dma = opaque;
    BMDMAState *bm = DO_UPCAST(BMDMAState, dma, dma);

    if (!running)
        return;

    if (!bm->bh) {
        bm->bh = qemu_bh_new(bmdma_restart_bh, &bm->dma);
        qemu_bh_schedule(bm->bh);
    }
}

static void bmdma_cancel(BMDMAState *bm)
{
    if (bm->status & BM_STATUS_DMAING) {
        /* cancel DMA request */
        bmdma_set_inactive(&bm->dma);
    }
}

static void bmdma_reset(IDEDMA *dma)
{
    BMDMAState *bm = DO_UPCAST(BMDMAState, dma, dma);

#ifdef DEBUG_IDE
    printf("ide: dma_reset\n");
#endif
    bmdma_cancel(bm);
    bm->cmd = 0;
    bm->status = 0;
    bm->addr = 0;
    bm->cur_addr = 0;
    bm->cur_prd_last = 0;
    bm->cur_prd_addr = 0;
    bm->cur_prd_len = 0;
    bm->sector_num = 0;
    bm->nsector = 0;
}

static int bmdma_start_transfer(IDEDMA *dma)
{
    return 0;
}

static void bmdma_irq(void *opaque, int n, int level)
{
    BMDMAState *bm = opaque;

    if (!level) {
        /* pass through lower */
        qemu_set_irq(bm->irq, level);
        return;
    }

    bm->status |= BM_STATUS_INT;

    /* trigger the real irq */
    qemu_set_irq(bm->irq, level);
}

void bmdma_cmd_writeb(BMDMAState *bm, uint32_t val)
{
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif

    /* Ignore writes to SSBM if it keeps the old value */
    if ((val & BM_CMD_START) != (bm->cmd & BM_CMD_START)) {
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
            if (bm->bus->dma->aiocb) {
                bdrv_drain_all();
                assert(bm->bus->dma->aiocb == NULL);
            }
            bm->status &= ~BM_STATUS_DMAING;
        } else {
            bm->cur_addr = bm->addr;
            if (!(bm->status & BM_STATUS_DMAING)) {
                bm->status |= BM_STATUS_DMAING;
                /* start dma transfer if possible */
                if (bm->dma_cb)
                    bm->dma_cb(bmdma_active_if(bm), 0);
            }
        }
    }

    bm->cmd = val & 0x09;
}

static uint64_t bmdma_addr_read(void *opaque, hwaddr addr,
                                unsigned width)
{
    BMDMAState *bm = opaque;
    uint32_t mask = (1ULL << (width * 8)) - 1;
    uint64_t data;

    data = (bm->addr >> (addr * 8)) & mask;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, (unsigned)data);
#endif
    return data;
}

static void bmdma_addr_write(void *opaque, hwaddr addr,
                             uint64_t data, unsigned width)
{
    BMDMAState *bm = opaque;
    int shift = addr * 8;
    uint32_t mask = (1ULL << (width * 8)) - 1;

#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, (unsigned)data);
#endif
    bm->addr &= ~(mask << shift);
    bm->addr |= ((data & mask) << shift) & ~3;
}

MemoryRegionOps bmdma_addr_ioport_ops = {
    .read = bmdma_addr_read,
    .write = bmdma_addr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static bool ide_bmdma_current_needed(void *opaque)
{
    BMDMAState *bm = opaque;

    return (bm->cur_prd_len != 0);
}

static bool ide_bmdma_status_needed(void *opaque)
{
    BMDMAState *bm = opaque;

    /* Older versions abused some bits in the status register for internal
     * error state. If any of these bits are set, we must add a subsection to
     * transfer the real status register */
    uint8_t abused_bits = BM_MIGRATION_COMPAT_STATUS_BITS;

    return ((bm->status & abused_bits) != 0);
}

static void ide_bmdma_pre_save(void *opaque)
{
    BMDMAState *bm = opaque;
    uint8_t abused_bits = BM_MIGRATION_COMPAT_STATUS_BITS;

    bm->migration_compat_status =
        (bm->status & ~abused_bits) | (bm->bus->error_status & abused_bits);
}

/* This function accesses bm->bus->error_status which is loaded only after
 * BMDMA itself. This is why the function is called from ide_pci_post_load
 * instead of being registered with VMState where it would run too early. */
static int ide_bmdma_post_load(void *opaque, int version_id)
{
    BMDMAState *bm = opaque;
    uint8_t abused_bits = BM_MIGRATION_COMPAT_STATUS_BITS;

    if (bm->status == 0) {
        bm->status = bm->migration_compat_status & ~abused_bits;
        bm->bus->error_status |= bm->migration_compat_status & abused_bits;
    }

    return 0;
}

static const VMStateDescription vmstate_bmdma_current = {
    .name = "ide bmdma_current",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cur_addr, BMDMAState),
        VMSTATE_UINT32(cur_prd_last, BMDMAState),
        VMSTATE_UINT32(cur_prd_addr, BMDMAState),
        VMSTATE_UINT32(cur_prd_len, BMDMAState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_bmdma_status = {
    .name ="ide bmdma/status",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(status, BMDMAState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_bmdma = {
    .name = "ide bmdma",
    .version_id = 3,
    .minimum_version_id = 0,
    .pre_save  = ide_bmdma_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(cmd, BMDMAState),
        VMSTATE_UINT8(migration_compat_status, BMDMAState),
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
            .vmsd = &vmstate_bmdma_status,
            .needed = ide_bmdma_status_needed,
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
        ide_bmdma_post_load(&d->bmdma[i], -1);
    }

    return 0;
}

const VMStateDescription vmstate_ide_pci = {
    .name = "ide",
    .version_id = 3,
    .minimum_version_id = 0,
    .post_load = ide_pci_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIIDEState),
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
    PCIIDEState *d = PCI_IDE(dev);
    static const int bus[4]  = { 0, 0, 1, 1 };
    static const int unit[4] = { 0, 1, 0, 1 };
    int i;

    for (i = 0; i < 4; i++) {
        if (hd_table[i] == NULL)
            continue;
        ide_create_drive(d->bus+bus[i], unit[i], hd_table[i]);
    }
}

static const struct IDEDMAOps bmdma_ops = {
    .start_dma = bmdma_start_dma,
    .start_transfer = bmdma_start_transfer,
    .prepare_buf = bmdma_prepare_buf,
    .rw_buf = bmdma_rw_buf,
    .set_unit = bmdma_set_unit,
    .add_status = bmdma_add_status,
    .set_inactive = bmdma_set_inactive,
    .restart_cb = bmdma_restart_cb,
    .reset = bmdma_reset,
};

void bmdma_init(IDEBus *bus, BMDMAState *bm, PCIIDEState *d)
{
    qemu_irq *irq;

    if (bus->dma == &bm->dma) {
        return;
    }

    bm->dma.ops = &bmdma_ops;
    bus->dma = &bm->dma;
    bm->irq = bus->irq;
    irq = qemu_allocate_irqs(bmdma_irq, bm, 1);
    bus->irq = *irq;
    bm->pci_dev = d;
}

static const TypeInfo pci_ide_type_info = {
    .name = TYPE_PCI_IDE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIIDEState),
    .abstract = true,
};

static void pci_ide_register_types(void)
{
    type_register_static(&pci_ide_type_info);
}

type_init(pci_ide_register_types)
