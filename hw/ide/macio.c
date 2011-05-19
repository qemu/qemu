/*
 * QEMU IDE Emulation: MacIO support.
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
#include <hw/ppc_mac.h>
#include <hw/mac_dbdma.h>
#include "block.h"
#include "block_int.h"
#include "dma.h"

#include <hw/ide/internal.h>

/***********************************************************/
/* MacIO based PowerPC IDE */

typedef struct MACIOIDEState {
    IDEBus bus;
    BlockDriverAIOCB *aiocb;
} MACIOIDEState;

#define MACIO_PAGE_SIZE 4096

static void pmac_ide_atapi_transfer_cb(void *opaque, int ret)
{
    DBDMA_io *io = opaque;
    MACIOIDEState *m = io->opaque;
    IDEState *s = idebus_active_if(&m->bus);

    if (ret < 0) {
        m->aiocb = NULL;
        qemu_sglist_destroy(&s->sg);
        ide_atapi_io_error(s, ret);
        io->dma_end(opaque);
        return;
    }

    if (s->io_buffer_size > 0) {
        m->aiocb = NULL;
        qemu_sglist_destroy(&s->sg);

        s->packet_transfer_size -= s->io_buffer_size;

        s->io_buffer_index += s->io_buffer_size;
	s->lba += s->io_buffer_index >> 11;
        s->io_buffer_index &= 0x7ff;
    }

    if (s->packet_transfer_size <= 0)
        ide_atapi_cmd_ok(s);

    if (io->len == 0) {
        io->dma_end(opaque);
        return;
    }

    /* launch next transfer */

    s->io_buffer_size = io->len;

    qemu_sglist_init(&s->sg, io->len / MACIO_PAGE_SIZE + 1);
    qemu_sglist_add(&s->sg, io->addr, io->len);
    io->addr += io->len;
    io->len = 0;

    m->aiocb = dma_bdrv_read(s->bs, &s->sg,
                             (int64_t)(s->lba << 2) + (s->io_buffer_index >> 9),
                             pmac_ide_atapi_transfer_cb, io);
    if (!m->aiocb) {
        qemu_sglist_destroy(&s->sg);
        /* Note: media not present is the most likely case */
        ide_atapi_cmd_error(s, SENSE_NOT_READY,
                            ASC_MEDIUM_NOT_PRESENT);
        io->dma_end(opaque);
        return;
    }
}

static void pmac_ide_transfer_cb(void *opaque, int ret)
{
    DBDMA_io *io = opaque;
    MACIOIDEState *m = io->opaque;
    IDEState *s = idebus_active_if(&m->bus);
    int n;
    int64_t sector_num;

    if (ret < 0) {
        m->aiocb = NULL;
        qemu_sglist_destroy(&s->sg);
	ide_dma_error(s);
        io->dma_end(io);
        return;
    }

    sector_num = ide_get_sector(s);
    if (s->io_buffer_size > 0) {
        m->aiocb = NULL;
        qemu_sglist_destroy(&s->sg);
        n = (s->io_buffer_size + 0x1ff) >> 9;
        sector_num += n;
        ide_set_sector(s, sector_num);
        s->nsector -= n;
    }

    /* end of transfer ? */
    if (s->nsector == 0) {
        s->status = READY_STAT | SEEK_STAT;
        ide_set_irq(s->bus);
    }

    /* end of DMA ? */

    if (io->len == 0) {
        io->dma_end(io);
	return;
    }

    /* launch next transfer */

    s->io_buffer_index = 0;
    s->io_buffer_size = io->len;

    qemu_sglist_init(&s->sg, io->len / MACIO_PAGE_SIZE + 1);
    qemu_sglist_add(&s->sg, io->addr, io->len);
    io->addr += io->len;
    io->len = 0;

    switch (s->dma_cmd) {
    case IDE_DMA_READ:
        m->aiocb = dma_bdrv_read(s->bs, &s->sg, sector_num,
		                 pmac_ide_transfer_cb, io);
        break;
    case IDE_DMA_WRITE:
        m->aiocb = dma_bdrv_write(s->bs, &s->sg, sector_num,
		                  pmac_ide_transfer_cb, io);
        break;
    }

    if (!m->aiocb)
        pmac_ide_transfer_cb(io, -1);
}

static void pmac_ide_transfer(DBDMA_io *io)
{
    MACIOIDEState *m = io->opaque;
    IDEState *s = idebus_active_if(&m->bus);

    s->io_buffer_size = 0;
    if (s->drive_kind == IDE_CD) {
        pmac_ide_atapi_transfer_cb(io, 0);
        return;
    }

    pmac_ide_transfer_cb(io, 0);
}

static void pmac_ide_flush(DBDMA_io *io)
{
    MACIOIDEState *m = io->opaque;

    if (m->aiocb)
        qemu_aio_flush();
}

/* PowerMac IDE memory IO */
static void pmac_ide_writeb (void *opaque,
                             target_phys_addr_t addr, uint32_t val)
{
    MACIOIDEState *d = opaque;

    addr = (addr & 0xFFF) >> 4;
    switch (addr) {
    case 1 ... 7:
        ide_ioport_write(&d->bus, addr, val);
        break;
    case 8:
    case 22:
        ide_cmd_write(&d->bus, 0, val);
        break;
    default:
        break;
    }
}

static uint32_t pmac_ide_readb (void *opaque,target_phys_addr_t addr)
{
    uint8_t retval;
    MACIOIDEState *d = opaque;

    addr = (addr & 0xFFF) >> 4;
    switch (addr) {
    case 1 ... 7:
        retval = ide_ioport_read(&d->bus, addr);
        break;
    case 8:
    case 22:
        retval = ide_status_read(&d->bus, 0);
        break;
    default:
        retval = 0xFF;
        break;
    }
    return retval;
}

static void pmac_ide_writew (void *opaque,
                             target_phys_addr_t addr, uint32_t val)
{
    MACIOIDEState *d = opaque;

    addr = (addr & 0xFFF) >> 4;
    val = bswap16(val);
    if (addr == 0) {
        ide_data_writew(&d->bus, 0, val);
    }
}

static uint32_t pmac_ide_readw (void *opaque,target_phys_addr_t addr)
{
    uint16_t retval;
    MACIOIDEState *d = opaque;

    addr = (addr & 0xFFF) >> 4;
    if (addr == 0) {
        retval = ide_data_readw(&d->bus, 0);
    } else {
        retval = 0xFFFF;
    }
    retval = bswap16(retval);
    return retval;
}

static void pmac_ide_writel (void *opaque,
                             target_phys_addr_t addr, uint32_t val)
{
    MACIOIDEState *d = opaque;

    addr = (addr & 0xFFF) >> 4;
    val = bswap32(val);
    if (addr == 0) {
        ide_data_writel(&d->bus, 0, val);
    }
}

static uint32_t pmac_ide_readl (void *opaque,target_phys_addr_t addr)
{
    uint32_t retval;
    MACIOIDEState *d = opaque;

    addr = (addr & 0xFFF) >> 4;
    if (addr == 0) {
        retval = ide_data_readl(&d->bus, 0);
    } else {
        retval = 0xFFFFFFFF;
    }
    retval = bswap32(retval);
    return retval;
}

static CPUWriteMemoryFunc * const pmac_ide_write[] = {
    pmac_ide_writeb,
    pmac_ide_writew,
    pmac_ide_writel,
};

static CPUReadMemoryFunc * const pmac_ide_read[] = {
    pmac_ide_readb,
    pmac_ide_readw,
    pmac_ide_readl,
};

static const VMStateDescription vmstate_pmac = {
    .name = "ide",
    .version_id = 3,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField []) {
        VMSTATE_IDE_BUS(bus, MACIOIDEState),
        VMSTATE_IDE_DRIVES(bus.ifs, MACIOIDEState),
        VMSTATE_END_OF_LIST()
    }
};

static void pmac_ide_reset(void *opaque)
{
    MACIOIDEState *d = opaque;

    ide_bus_reset(&d->bus);
}

/* hd_table must contain 4 block drivers */
/* PowerMac uses memory mapped registers, not I/O. Return the memory
   I/O index to access the ide. */
int pmac_ide_init (DriveInfo **hd_table, qemu_irq irq,
		   void *dbdma, int channel, qemu_irq dma_irq)
{
    MACIOIDEState *d;
    int pmac_ide_memory;

    d = qemu_mallocz(sizeof(MACIOIDEState));
    ide_init2_with_non_qdev_drives(&d->bus, hd_table[0], hd_table[1], irq);

    if (dbdma)
        DBDMA_register_channel(dbdma, channel, dma_irq, pmac_ide_transfer, pmac_ide_flush, d);

    pmac_ide_memory = cpu_register_io_memory(pmac_ide_read,
                                             pmac_ide_write, d,
                                             DEVICE_NATIVE_ENDIAN);
    vmstate_register(NULL, 0, &vmstate_pmac, d);
    qemu_register_reset(pmac_ide_reset, d);

    return pmac_ide_memory;
}
