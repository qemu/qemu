/*
 * SiFive Platform DMA emulation
 *
 * Copyright (c) 2020 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "sysemu/dma.h"
#include "hw/dma/sifive_pdma.h"

#define DMA_CONTROL         0x000
#define   CONTROL_CLAIM     BIT(0)
#define   CONTROL_RUN       BIT(1)
#define   CONTROL_DONE_IE   BIT(14)
#define   CONTROL_ERR_IE    BIT(15)
#define   CONTROL_DONE      BIT(30)
#define   CONTROL_ERR       BIT(31)

#define DMA_NEXT_CONFIG     0x004
#define   CONFIG_REPEAT     BIT(2)
#define   CONFIG_ORDER      BIT(3)
#define   CONFIG_WRSZ_SHIFT 24
#define   CONFIG_RDSZ_SHIFT 28
#define   CONFIG_SZ_MASK    0xf

#define DMA_NEXT_BYTES      0x008
#define DMA_NEXT_DST        0x010
#define DMA_NEXT_SRC        0x018
#define DMA_EXEC_CONFIG     0x104
#define DMA_EXEC_BYTES      0x108
#define DMA_EXEC_DST        0x110
#define DMA_EXEC_SRC        0x118

enum dma_chan_state {
    DMA_CHAN_STATE_IDLE,
    DMA_CHAN_STATE_STARTED,
    DMA_CHAN_STATE_ERROR,
    DMA_CHAN_STATE_DONE
};

static void sifive_pdma_run(SiFivePDMAState *s, int ch)
{
    uint64_t bytes = s->chan[ch].next_bytes;
    uint64_t dst = s->chan[ch].next_dst;
    uint64_t src = s->chan[ch].next_src;
    uint32_t config = s->chan[ch].next_config;
    int wsize, rsize, size;
    uint8_t buf[64];
    int n;

    /* do nothing if bytes to transfer is zero */
    if (!bytes) {
        goto error;
    }

    /*
     * The manual does not describe how the hardware behaviors when
     * config.wsize and config.rsize are given different values.
     * A common case is memory to memory DMA, and in this case they
     * are normally the same. Abort if this expectation fails.
     */
    wsize = (config >> CONFIG_WRSZ_SHIFT) & CONFIG_SZ_MASK;
    rsize = (config >> CONFIG_RDSZ_SHIFT) & CONFIG_SZ_MASK;
    if (wsize != rsize) {
        goto error;
    }

    /*
     * Calculate the transaction size
     *
     * size field is base 2 logarithm of DMA transaction size,
     * but there is an upper limit of 64 bytes per transaction.
     */
    size = wsize;
    if (size > 6) {
        size = 6;
    }
    size = 1 << size;

    /* the bytes to transfer should be multiple of transaction size */
    if (bytes % size) {
        goto error;
    }

    /* indicate a DMA transfer is started */
    s->chan[ch].state = DMA_CHAN_STATE_STARTED;
    s->chan[ch].control &= ~CONTROL_DONE;
    s->chan[ch].control &= ~CONTROL_ERR;

    /* load the next_ registers into their exec_ counterparts */
    s->chan[ch].exec_config = config;
    s->chan[ch].exec_bytes = bytes;
    s->chan[ch].exec_dst = dst;
    s->chan[ch].exec_src = src;

    for (n = 0; n < bytes / size; n++) {
        cpu_physical_memory_read(s->chan[ch].exec_src, buf, size);
        cpu_physical_memory_write(s->chan[ch].exec_dst, buf, size);
        s->chan[ch].exec_src += size;
        s->chan[ch].exec_dst += size;
        s->chan[ch].exec_bytes -= size;
    }

    /* indicate a DMA transfer is done */
    s->chan[ch].state = DMA_CHAN_STATE_DONE;
    s->chan[ch].control &= ~CONTROL_RUN;
    s->chan[ch].control |= CONTROL_DONE;

    /* reload exec_ registers if repeat is required */
    if (s->chan[ch].next_config & CONFIG_REPEAT) {
        s->chan[ch].exec_bytes = bytes;
        s->chan[ch].exec_dst = dst;
        s->chan[ch].exec_src = src;
    }

    return;

error:
    s->chan[ch].state = DMA_CHAN_STATE_ERROR;
    s->chan[ch].control |= CONTROL_ERR;
    return;
}

static inline void sifive_pdma_update_irq(SiFivePDMAState *s, int ch)
{
    bool done_ie, err_ie;

    done_ie = !!(s->chan[ch].control & CONTROL_DONE_IE);
    err_ie = !!(s->chan[ch].control & CONTROL_ERR_IE);

    if (done_ie && (s->chan[ch].control & CONTROL_DONE)) {
        qemu_irq_raise(s->irq[ch * 2]);
    } else {
        qemu_irq_lower(s->irq[ch * 2]);
    }

    if (err_ie && (s->chan[ch].control & CONTROL_ERR)) {
        qemu_irq_raise(s->irq[ch * 2 + 1]);
    } else {
        qemu_irq_lower(s->irq[ch * 2 + 1]);
    }

    s->chan[ch].state = DMA_CHAN_STATE_IDLE;
}

static uint64_t sifive_pdma_read(void *opaque, hwaddr offset, unsigned size)
{
    SiFivePDMAState *s = opaque;
    int ch = SIFIVE_PDMA_CHAN_NO(offset);
    uint64_t val = 0;

    if (ch >= SIFIVE_PDMA_CHANS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid channel no %d\n",
                      __func__, ch);
        return 0;
    }

    offset &= 0xfff;
    switch (offset) {
    case DMA_CONTROL:
        val = s->chan[ch].control;
        break;
    case DMA_NEXT_CONFIG:
        val = s->chan[ch].next_config;
        break;
    case DMA_NEXT_BYTES:
        val = s->chan[ch].next_bytes;
        break;
    case DMA_NEXT_DST:
        val = s->chan[ch].next_dst;
        break;
    case DMA_NEXT_SRC:
        val = s->chan[ch].next_src;
        break;
    case DMA_EXEC_CONFIG:
        val = s->chan[ch].exec_config;
        break;
    case DMA_EXEC_BYTES:
        val = s->chan[ch].exec_bytes;
        break;
    case DMA_EXEC_DST:
        val = s->chan[ch].exec_dst;
        break;
    case DMA_EXEC_SRC:
        val = s->chan[ch].exec_src;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIX "\n",
                      __func__, offset);
        break;
    }

    return val;
}

static void sifive_pdma_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    SiFivePDMAState *s = opaque;
    int ch = SIFIVE_PDMA_CHAN_NO(offset);

    if (ch >= SIFIVE_PDMA_CHANS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Invalid channel no %d\n",
                      __func__, ch);
        return;
    }

    offset &= 0xfff;
    switch (offset) {
    case DMA_CONTROL:
        s->chan[ch].control = value;

        if (value & CONTROL_RUN) {
            sifive_pdma_run(s, ch);
        }

        sifive_pdma_update_irq(s, ch);
        break;
    case DMA_NEXT_CONFIG:
        s->chan[ch].next_config = value;
        break;
    case DMA_NEXT_BYTES:
        s->chan[ch].next_bytes = value;
        break;
    case DMA_NEXT_DST:
        s->chan[ch].next_dst = value;
        break;
    case DMA_NEXT_SRC:
        s->chan[ch].next_src = value;
        break;
    case DMA_EXEC_CONFIG:
    case DMA_EXEC_BYTES:
    case DMA_EXEC_DST:
    case DMA_EXEC_SRC:
        /* these are read-only registers */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIX "\n",
                      __func__, offset);
        break;
    }
}

static const MemoryRegionOps sifive_pdma_ops = {
    .read = sifive_pdma_read,
    .write = sifive_pdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* there are 32-bit and 64-bit wide registers */
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    }
};

static void sifive_pdma_realize(DeviceState *dev, Error **errp)
{
    SiFivePDMAState *s = SIFIVE_PDMA(dev);
    int i;

    memory_region_init_io(&s->iomem, OBJECT(dev), &sifive_pdma_ops, s,
                          TYPE_SIFIVE_PDMA, SIFIVE_PDMA_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    for (i = 0; i < SIFIVE_PDMA_IRQS; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq[i]);
    }
}

static void sifive_pdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "SiFive Platform DMA controller";
    dc->realize = sifive_pdma_realize;
}

static const TypeInfo sifive_pdma_info = {
    .name          = TYPE_SIFIVE_PDMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SiFivePDMAState),
    .class_init    = sifive_pdma_class_init,
};

static void sifive_pdma_register_types(void)
{
    type_register_static(&sifive_pdma_info);
}

type_init(sifive_pdma_register_types)
