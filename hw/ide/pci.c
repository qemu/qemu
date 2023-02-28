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

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "sysemu/dma.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/ide/pci.h"
#include "trace.h"

#define BMDMA_PAGE_SIZE 4096

#define BM_MIGRATION_COMPAT_STATUS_BITS \
        (IDE_RETRY_DMA | IDE_RETRY_PIO | \
        IDE_RETRY_READ | IDE_RETRY_FLUSH)

static uint64_t pci_ide_status_read(void *opaque, hwaddr addr, unsigned size)
{
    IDEBus *bus = opaque;

    if (addr != 2 || size != 1) {
        return ((uint64_t)1 << (size * 8)) - 1;
    }
    return ide_status_read(bus, addr + 2);
}

static void pci_ide_ctrl_write(void *opaque, hwaddr addr,
                               uint64_t data, unsigned size)
{
    IDEBus *bus = opaque;

    if (addr != 2 || size != 1) {
        return;
    }
    ide_ctrl_write(bus, addr + 2, data);
}

const MemoryRegionOps pci_ide_cmd_le_ops = {
    .read = pci_ide_status_read,
    .write = pci_ide_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t pci_ide_data_read(void *opaque, hwaddr addr, unsigned size)
{
    IDEBus *bus = opaque;

    if (size == 1) {
        return ide_ioport_read(bus, addr);
    } else if (addr == 0) {
        if (size == 2) {
            return ide_data_readw(bus, addr);
        } else {
            return ide_data_readl(bus, addr);
        }
    }
    return ((uint64_t)1 << (size * 8)) - 1;
}

static void pci_ide_data_write(void *opaque, hwaddr addr,
                               uint64_t data, unsigned size)
{
    IDEBus *bus = opaque;

    if (size == 1) {
        ide_ioport_write(bus, addr, data);
    } else if (addr == 0) {
        if (size == 2) {
            ide_data_writew(bus, addr, data);
        } else {
            ide_data_writel(bus, addr, data);
        }
    }
}

const MemoryRegionOps pci_ide_data_le_ops = {
    .read = pci_ide_data_read,
    .write = pci_ide_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static IDEState *bmdma_active_if(BMDMAState *bmdma)
{
    assert(bmdma->bus->retry_unit != (uint8_t)-1);
    return bmdma->bus->ifs + bmdma->bus->retry_unit;
}

static void bmdma_start_dma(const IDEDMA *dma, IDEState *s,
                            BlockCompletionFunc *dma_cb)
{
    BMDMAState *bm = DO_UPCAST(BMDMAState, dma, dma);

    bm->dma_cb = dma_cb;
    bm->cur_prd_last = 0;
    bm->cur_prd_addr = 0;
    bm->cur_prd_len = 0;

    if (bm->status & BM_STATUS_DMAING) {
        bm->dma_cb(bmdma_active_if(bm), 0);
    }
}

/**
 * Prepare an sglist based on available PRDs.
 * @limit: How many bytes to prepare total.
 *
 * Returns the number of bytes prepared, -1 on error.
 * IDEState.io_buffer_size will contain the number of bytes described
 * by the PRDs, whether or not we added them to the sglist.
 */
static int32_t bmdma_prepare_buf(const IDEDMA *dma, int32_t limit)
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
                        s->nsector / (BMDMA_PAGE_SIZE / BDRV_SECTOR_SIZE) + 1);
    s->io_buffer_size = 0;
    for(;;) {
        if (bm->cur_prd_len == 0) {
            /* end of table (with a fail safe of one page) */
            if (bm->cur_prd_last ||
                (bm->cur_addr - bm->addr) >= BMDMA_PAGE_SIZE) {
                return s->sg.size;
            }
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
            uint64_t sg_len;

            /* Don't add extra bytes to the SGList; consume any remaining
             * PRDs from the guest, but ignore them. */
            sg_len = MIN(limit - s->sg.size, bm->cur_prd_len);
            if (sg_len) {
                qemu_sglist_add(&s->sg, bm->cur_prd_addr, sg_len);
            }

            bm->cur_prd_addr += l;
            bm->cur_prd_len -= l;
            s->io_buffer_size += l;
        }
    }

    qemu_sglist_destroy(&s->sg);
    s->io_buffer_size = 0;
    return -1;
}

/* return 0 if buffer completed */
static int bmdma_rw_buf(const IDEDMA *dma, bool is_write)
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

static void bmdma_set_inactive(const IDEDMA *dma, bool more)
{
    BMDMAState *bm = DO_UPCAST(BMDMAState, dma, dma);

    bm->dma_cb = NULL;
    if (more) {
        bm->status |= BM_STATUS_DMAING;
    } else {
        bm->status &= ~BM_STATUS_DMAING;
    }
}

static void bmdma_restart_dma(const IDEDMA *dma)
{
    BMDMAState *bm = DO_UPCAST(BMDMAState, dma, dma);

    bm->cur_addr = bm->addr;
}

static void bmdma_cancel(BMDMAState *bm)
{
    if (bm->status & BM_STATUS_DMAING) {
        /* cancel DMA request */
        bmdma_set_inactive(&bm->dma, false);
    }
}

static void bmdma_reset(const IDEDMA *dma)
{
    BMDMAState *bm = DO_UPCAST(BMDMAState, dma, dma);

    trace_bmdma_reset();
    bmdma_cancel(bm);
    bm->cmd = 0;
    bm->status = 0;
    bm->addr = 0;
    bm->cur_addr = 0;
    bm->cur_prd_last = 0;
    bm->cur_prd_addr = 0;
    bm->cur_prd_len = 0;
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
    trace_bmdma_cmd_writeb(val);

    /* Ignore writes to SSBM if it keeps the old value */
    if ((val & BM_CMD_START) != (bm->cmd & BM_CMD_START)) {
        if (!(val & BM_CMD_START)) {
            ide_cancel_dma_sync(ide_bus_active_if(bm->bus));
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
    trace_bmdma_addr_read(data);
    return data;
}

static void bmdma_addr_write(void *opaque, hwaddr addr,
                             uint64_t data, unsigned width)
{
    BMDMAState *bm = opaque;
    int shift = addr * 8;
    uint32_t mask = (1ULL << (width * 8)) - 1;

    trace_bmdma_addr_write(data);
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

static int ide_bmdma_pre_save(void *opaque)
{
    BMDMAState *bm = opaque;
    uint8_t abused_bits = BM_MIGRATION_COMPAT_STATUS_BITS;

    if (!(bm->status & BM_STATUS_DMAING) && bm->dma_cb) {
        bm->bus->error_status =
            ide_dma_cmd_to_retry(bmdma_active_if(bm)->dma_cmd);
    }
    bm->migration_retry_unit = bm->bus->retry_unit;
    bm->migration_retry_sector_num = bm->bus->retry_sector_num;
    bm->migration_retry_nsector = bm->bus->retry_nsector;
    bm->migration_compat_status =
        (bm->status & ~abused_bits) | (bm->bus->error_status & abused_bits);

    return 0;
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
    if (bm->bus->error_status) {
        bm->bus->retry_sector_num = bm->migration_retry_sector_num;
        bm->bus->retry_nsector = bm->migration_retry_nsector;
        bm->bus->retry_unit = bm->migration_retry_unit;
    }

    return 0;
}

static const VMStateDescription vmstate_bmdma_current = {
    .name = "ide bmdma_current",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = ide_bmdma_current_needed,
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
    .needed = ide_bmdma_status_needed,
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
        VMSTATE_INT64(migration_retry_sector_num, BMDMAState),
        VMSTATE_UINT32(migration_retry_nsector, BMDMAState),
        VMSTATE_UINT8(migration_retry_unit, BMDMAState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_bmdma_current,
        &vmstate_bmdma_status,
        NULL
    }
};

static int ide_pci_post_load(void *opaque, int version_id)
{
    PCIIDEState *d = opaque;
    int i;

    for(i = 0; i < 2; i++) {
        /* current versions always store 0/1, but older version
           stored bigger values. We only need last bit */
        d->bmdma[i].migration_retry_unit &= 1;
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

/* hd_table must contain 4 block drivers */
void pci_ide_create_devs(PCIDevice *dev)
{
    PCIIDEState *d = PCI_IDE(dev);
    DriveInfo *hd_table[2 * MAX_IDE_DEVS];
    static const int bus[4]  = { 0, 0, 1, 1 };
    static const int unit[4] = { 0, 1, 0, 1 };
    int i;

    ide_drive_get(hd_table, ARRAY_SIZE(hd_table));
    for (i = 0; i < 4; i++) {
        if (hd_table[i]) {
            ide_bus_create_drive(d->bus + bus[i], unit[i], hd_table[i]);
        }
    }
}

static const struct IDEDMAOps bmdma_ops = {
    .start_dma = bmdma_start_dma,
    .prepare_buf = bmdma_prepare_buf,
    .rw_buf = bmdma_rw_buf,
    .restart_dma = bmdma_restart_dma,
    .set_inactive = bmdma_set_inactive,
    .reset = bmdma_reset,
};

void bmdma_init(IDEBus *bus, BMDMAState *bm, PCIIDEState *d)
{
    if (bus->dma == &bm->dma) {
        return;
    }

    bm->dma.ops = &bmdma_ops;
    bus->dma = &bm->dma;
    bm->irq = bus->irq;
    bus->irq = qemu_allocate_irq(bmdma_irq, bm, 0);
    bm->pci_dev = d;
}

static const TypeInfo pci_ide_type_info = {
    .name = TYPE_PCI_IDE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIIDEState),
    .abstract = true,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pci_ide_register_types(void)
{
    type_register_static(&pci_ide_type_info);
}

type_init(pci_ide_register_types)
