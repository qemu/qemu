/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/dma/bcm2835_dma.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

/* DMA CS Control and Status bits */
#define BCM2708_DMA_ACTIVE      (1 << 0)
#define BCM2708_DMA_END         (1 << 1) /* GE */
#define BCM2708_DMA_INT         (1 << 2)
#define BCM2708_DMA_ISPAUSED    (1 << 4)  /* Pause requested or not active */
#define BCM2708_DMA_ISHELD      (1 << 5)  /* Is held by DREQ flow control */
#define BCM2708_DMA_ERR         (1 << 8)
#define BCM2708_DMA_ABORT       (1 << 30) /* stop current CB, go to next, WO */
#define BCM2708_DMA_RESET       (1 << 31) /* WO, self clearing */

/* DMA control block "info" field bits */
#define BCM2708_DMA_INT_EN      (1 << 0)
#define BCM2708_DMA_TDMODE      (1 << 1)
#define BCM2708_DMA_WAIT_RESP   (1 << 3)
#define BCM2708_DMA_D_INC       (1 << 4)
#define BCM2708_DMA_D_WIDTH     (1 << 5)
#define BCM2708_DMA_D_DREQ      (1 << 6)
#define BCM2708_DMA_D_IGNORE    (1 << 7)
#define BCM2708_DMA_S_INC       (1 << 8)
#define BCM2708_DMA_S_WIDTH     (1 << 9)
#define BCM2708_DMA_S_DREQ      (1 << 10)
#define BCM2708_DMA_S_IGNORE    (1 << 11)

/* Register offsets */
#define BCM2708_DMA_CS          0x00 /* Control and Status */
#define BCM2708_DMA_ADDR        0x04 /* Control block address */
/* the current control block appears in the following registers - read only */
#define BCM2708_DMA_INFO        0x08
#define BCM2708_DMA_SOURCE_AD   0x0c
#define BCM2708_DMA_DEST_AD     0x10
#define BCM2708_DMA_TXFR_LEN    0x14
#define BCM2708_DMA_STRIDE      0x18
#define BCM2708_DMA_NEXTCB      0x1C
#define BCM2708_DMA_DEBUG       0x20

#define BCM2708_DMA_INT_STATUS  0xfe0 /* Interrupt status of each channel */
#define BCM2708_DMA_ENABLE      0xff0 /* Global enable bits for each channel */

#define BCM2708_DMA_CS_RW_MASK  0x30ff0001 /* All RW bits in DMA_CS */

static void bcm2835_dma_update(BCM2835DMAState *s, unsigned c)
{
    BCM2835DMAChan *ch = &s->chan[c];
    uint32_t data, xlen, xlen_td, ylen;
    int16_t dst_stride, src_stride;

    if (!(s->enable & (1 << c))) {
        return;
    }

    while ((s->enable & (1 << c)) && (ch->conblk_ad != 0)) {
        /* CB fetch */
        ch->ti = ldl_le_phys(&s->dma_as, ch->conblk_ad);
        ch->source_ad = ldl_le_phys(&s->dma_as, ch->conblk_ad + 4);
        ch->dest_ad = ldl_le_phys(&s->dma_as, ch->conblk_ad + 8);
        ch->txfr_len = ldl_le_phys(&s->dma_as, ch->conblk_ad + 12);
        ch->stride = ldl_le_phys(&s->dma_as, ch->conblk_ad + 16);
        ch->nextconbk = ldl_le_phys(&s->dma_as, ch->conblk_ad + 20);

        ylen = 1;
        if (ch->ti & BCM2708_DMA_TDMODE) {
            /* 2D transfer mode */
            ylen += (ch->txfr_len >> 16) & 0x3fff;
            xlen = ch->txfr_len & 0xffff;
            dst_stride = ch->stride >> 16;
            src_stride = ch->stride & 0xffff;
        } else {
            xlen = ch->txfr_len;
            dst_stride = 0;
            src_stride = 0;
        }
        xlen_td = xlen;

        while (ylen != 0) {
            /* Normal transfer mode */
            while (xlen != 0) {
                if (ch->ti & BCM2708_DMA_S_IGNORE) {
                    /* Ignore reads */
                    data = 0;
                } else {
                    data = ldl_le_phys(&s->dma_as, ch->source_ad);
                }
                if (ch->ti & BCM2708_DMA_S_INC) {
                    ch->source_ad += 4;
                }

                if (ch->ti & BCM2708_DMA_D_IGNORE) {
                    /* Ignore writes */
                } else {
                    stl_le_phys(&s->dma_as, ch->dest_ad, data);
                }
                if (ch->ti & BCM2708_DMA_D_INC) {
                    ch->dest_ad += 4;
                }

                /* update remaining transfer length */
                xlen -= 4;
                if (ch->ti & BCM2708_DMA_TDMODE) {
                    ch->txfr_len = (ylen << 16) | xlen;
                } else {
                    ch->txfr_len = xlen;
                }
            }

            if (--ylen != 0) {
                ch->source_ad += src_stride;
                ch->dest_ad += dst_stride;
                xlen = xlen_td;
            }
        }
        ch->cs |= BCM2708_DMA_END;
        if (ch->ti & BCM2708_DMA_INT_EN) {
            ch->cs |= BCM2708_DMA_INT;
            s->int_status |= (1 << c);
            qemu_set_irq(ch->irq, 1);
        }

        /* Process next CB */
        ch->conblk_ad = ch->nextconbk;
    }

    ch->cs &= ~BCM2708_DMA_ACTIVE;
    ch->cs |= BCM2708_DMA_ISPAUSED;
}

static void bcm2835_dma_chan_reset(BCM2835DMAChan *ch)
{
    ch->cs = 0;
    ch->conblk_ad = 0;
}

static uint64_t bcm2835_dma_read(BCM2835DMAState *s, hwaddr offset,
                                 unsigned size, unsigned c)
{
    BCM2835DMAChan *ch;
    uint32_t res = 0;

    assert(size == 4);
    assert(c < BCM2835_DMA_NCHANS);

    ch = &s->chan[c];

    switch (offset) {
    case BCM2708_DMA_CS:
        res = ch->cs;
        break;
    case BCM2708_DMA_ADDR:
        res = ch->conblk_ad;
        break;
    case BCM2708_DMA_INFO:
        res = ch->ti;
        break;
    case BCM2708_DMA_SOURCE_AD:
        res = ch->source_ad;
        break;
    case BCM2708_DMA_DEST_AD:
        res = ch->dest_ad;
        break;
    case BCM2708_DMA_TXFR_LEN:
        res = ch->txfr_len;
        break;
    case BCM2708_DMA_STRIDE:
        res = ch->stride;
        break;
    case BCM2708_DMA_NEXTCB:
        res = ch->nextconbk;
        break;
    case BCM2708_DMA_DEBUG:
        res = ch->debug;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, offset);
        break;
    }
    return res;
}

static void bcm2835_dma_write(BCM2835DMAState *s, hwaddr offset,
                              uint64_t value, unsigned size, unsigned c)
{
    BCM2835DMAChan *ch;
    uint32_t oldcs;

    assert(size == 4);
    assert(c < BCM2835_DMA_NCHANS);

    ch = &s->chan[c];

    switch (offset) {
    case BCM2708_DMA_CS:
        oldcs = ch->cs;
        if (value & BCM2708_DMA_RESET) {
            bcm2835_dma_chan_reset(ch);
        }
        if (value & BCM2708_DMA_ABORT) {
            /* abort is a no-op, since we always run to completion */
        }
        if (value & BCM2708_DMA_END) {
            ch->cs &= ~BCM2708_DMA_END;
        }
        if (value & BCM2708_DMA_INT) {
            ch->cs &= ~BCM2708_DMA_INT;
            s->int_status &= ~(1 << c);
            qemu_set_irq(ch->irq, 0);
        }
        ch->cs &= ~BCM2708_DMA_CS_RW_MASK;
        ch->cs |= (value & BCM2708_DMA_CS_RW_MASK);
        if (!(oldcs & BCM2708_DMA_ACTIVE) && (ch->cs & BCM2708_DMA_ACTIVE)) {
            bcm2835_dma_update(s, c);
        }
        break;
    case BCM2708_DMA_ADDR:
        ch->conblk_ad = value;
        break;
    case BCM2708_DMA_DEBUG:
        ch->debug = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, offset);
        break;
    }
}

static uint64_t bcm2835_dma0_read(void *opaque, hwaddr offset, unsigned size)
{
    BCM2835DMAState *s = opaque;

    if (offset < 0xf00) {
        return bcm2835_dma_read(s, (offset & 0xff), size, (offset >> 8) & 0xf);
    } else {
        switch (offset) {
        case BCM2708_DMA_INT_STATUS:
            return s->int_status;
        case BCM2708_DMA_ENABLE:
            return s->enable;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                          __func__, offset);
            return 0;
        }
    }
}

static uint64_t bcm2835_dma15_read(void *opaque, hwaddr offset, unsigned size)
{
    return bcm2835_dma_read(opaque, (offset & 0xff), size, 15);
}

static void bcm2835_dma0_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    BCM2835DMAState *s = opaque;

    if (offset < 0xf00) {
        bcm2835_dma_write(s, (offset & 0xff), value, size, (offset >> 8) & 0xf);
    } else {
        switch (offset) {
        case BCM2708_DMA_INT_STATUS:
            break;
        case BCM2708_DMA_ENABLE:
            s->enable = (value & 0xffff);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                          __func__, offset);
        }
    }

}

static void bcm2835_dma15_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size)
{
    bcm2835_dma_write(opaque, (offset & 0xff), value, size, 15);
}

static const MemoryRegionOps bcm2835_dma0_ops = {
    .read = bcm2835_dma0_read,
    .write = bcm2835_dma0_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const MemoryRegionOps bcm2835_dma15_ops = {
    .read = bcm2835_dma15_read,
    .write = bcm2835_dma15_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_dma_chan = {
    .name = TYPE_BCM2835_DMA "-chan",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cs, BCM2835DMAChan),
        VMSTATE_UINT32(conblk_ad, BCM2835DMAChan),
        VMSTATE_UINT32(ti, BCM2835DMAChan),
        VMSTATE_UINT32(source_ad, BCM2835DMAChan),
        VMSTATE_UINT32(dest_ad, BCM2835DMAChan),
        VMSTATE_UINT32(txfr_len, BCM2835DMAChan),
        VMSTATE_UINT32(stride, BCM2835DMAChan),
        VMSTATE_UINT32(nextconbk, BCM2835DMAChan),
        VMSTATE_UINT32(debug, BCM2835DMAChan),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_bcm2835_dma = {
    .name = TYPE_BCM2835_DMA,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(chan, BCM2835DMAState, BCM2835_DMA_NCHANS, 1,
                             vmstate_bcm2835_dma_chan, BCM2835DMAChan),
        VMSTATE_UINT32(int_status, BCM2835DMAState),
        VMSTATE_UINT32(enable, BCM2835DMAState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_dma_init(Object *obj)
{
    BCM2835DMAState *s = BCM2835_DMA(obj);
    int n;

    /* DMA channels 0-14 occupy a contiguous block of IO memory, along
     * with the global enable and interrupt status bits. Channel 15
     * has the same register map, but is mapped at a discontiguous
     * address in a separate IO block.
     */
    memory_region_init_io(&s->iomem0, OBJECT(s), &bcm2835_dma0_ops, s,
                          TYPE_BCM2835_DMA, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem0);

    memory_region_init_io(&s->iomem15, OBJECT(s), &bcm2835_dma15_ops, s,
                          TYPE_BCM2835_DMA "-chan15", 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem15);

    for (n = 0; n < 16; n++) {
        sysbus_init_irq(SYS_BUS_DEVICE(s), &s->chan[n].irq);
    }
}

static void bcm2835_dma_reset(DeviceState *dev)
{
    BCM2835DMAState *s = BCM2835_DMA(dev);
    int n;

    s->enable = 0xffff;
    s->int_status = 0;
    for (n = 0; n < BCM2835_DMA_NCHANS; n++) {
        bcm2835_dma_chan_reset(&s->chan[n]);
    }
}

static void bcm2835_dma_realize(DeviceState *dev, Error **errp)
{
    BCM2835DMAState *s = BCM2835_DMA(dev);
    Object *obj;

    obj = object_property_get_link(OBJECT(dev), "dma-mr", &error_abort);
    s->dma_mr = MEMORY_REGION(obj);
    address_space_init(&s->dma_as, s->dma_mr, TYPE_BCM2835_DMA "-memory");

    bcm2835_dma_reset(dev);
}

static void bcm2835_dma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bcm2835_dma_realize;
    device_class_set_legacy_reset(dc, bcm2835_dma_reset);
    dc->vmsd = &vmstate_bcm2835_dma;
}

static const TypeInfo bcm2835_dma_info = {
    .name          = TYPE_BCM2835_DMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835DMAState),
    .class_init    = bcm2835_dma_class_init,
    .instance_init = bcm2835_dma_init,
};

static void bcm2835_dma_register_types(void)
{
    type_register_static(&bcm2835_dma_info);
}

type_init(bcm2835_dma_register_types)
