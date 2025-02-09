/*
 * BCM2835 (Raspberry Pi / Pi 2) Aux block (mini UART and SPI).
 * Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 * Based on pl011.c, copyright terms below:
 *
 * Arm PrimeCell PL011 UART
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 *
 * At present only the core UART functions (data path for tx/rx) are
 * implemented. The following features/registers are unimplemented:
 *  - Line/modem control
 *  - Scratch register
 *  - Extra control
 *  - Baudrate
 *  - SPI interfaces
 */

#include "qemu/osdep.h"
#include "hw/char/bcm2835_aux.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define AUX_IRQ         0x0
#define AUX_ENABLES     0x4
#define AUX_MU_IO_REG   0x40
#define AUX_MU_IER_REG  0x44
#define AUX_MU_IIR_REG  0x48
#define AUX_MU_LCR_REG  0x4c
#define AUX_MU_MCR_REG  0x50
#define AUX_MU_LSR_REG  0x54
#define AUX_MU_MSR_REG  0x58
#define AUX_MU_SCRATCH  0x5c
#define AUX_MU_CNTL_REG 0x60
#define AUX_MU_STAT_REG 0x64
#define AUX_MU_BAUD_REG 0x68

/* bits in IER/IIR registers */
#define RX_INT  0x1
#define TX_INT  0x2

static void bcm2835_aux_update(BCM2835AuxState *s)
{
    /* signal an interrupt if either:
     * 1. rx interrupt is enabled and we have a non-empty rx fifo, or
     * 2. the tx interrupt is enabled (since we instantly drain the tx fifo)
     */
    s->iir = 0;
    if ((s->ier & RX_INT) && s->read_count != 0) {
        s->iir |= RX_INT;
    }
    if (s->ier & TX_INT) {
        s->iir |= TX_INT;
    }
    qemu_set_irq(s->irq, s->iir != 0);
}

static uint64_t bcm2835_aux_read(void *opaque, hwaddr offset, unsigned size)
{
    BCM2835AuxState *s = opaque;
    uint32_t c, res;

    switch (offset) {
    case AUX_IRQ:
        return s->iir != 0;

    case AUX_ENABLES:
        return 1; /* mini UART permanently enabled */

    case AUX_MU_IO_REG:
        /* "DLAB bit set means access baudrate register" is NYI */
        c = s->read_fifo[s->read_pos];
        if (s->read_count > 0) {
            s->read_count--;
            if (++s->read_pos == BCM2835_AUX_RX_FIFO_LEN) {
                s->read_pos = 0;
            }
        }
        qemu_chr_fe_accept_input(&s->chr);
        bcm2835_aux_update(s);
        return c;

    case AUX_MU_IER_REG:
        /* "DLAB bit set means access baudrate register" is NYI */
        return 0xc0 | s->ier; /* FIFO enables always read 1 */

    case AUX_MU_IIR_REG:
        res = 0xc0; /* FIFO enables */
        /* The spec is unclear on what happens when both tx and rx
         * interrupts are active, besides that this cannot occur. At
         * present, we choose to prioritise the rx interrupt, since
         * the tx fifo is always empty. */
        if ((s->iir & RX_INT) && s->read_count != 0) {
            res |= 0x4;
        } else {
            res |= 0x2;
        }
        if (s->iir == 0) {
            res |= 0x1;
        }
        return res;

    case AUX_MU_LCR_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_LCR_REG unsupported\n", __func__);
        return 0;

    case AUX_MU_MCR_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_MCR_REG unsupported\n", __func__);
        return 0;

    case AUX_MU_LSR_REG:
        res = 0x60; /* tx idle, empty */
        if (s->read_count != 0) {
            res |= 0x1;
        }
        return res;

    case AUX_MU_MSR_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_MSR_REG unsupported\n", __func__);
        return 0;

    case AUX_MU_SCRATCH:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_SCRATCH unsupported\n", __func__);
        return 0;

    case AUX_MU_CNTL_REG:
        return 0x3; /* tx, rx enabled */

    case AUX_MU_STAT_REG:
        res = 0x30e; /* space in the output buffer, empty tx fifo, idle tx/rx */
        if (s->read_count > 0) {
            res |= 0x1; /* data in input buffer */
            assert(s->read_count <= BCM2835_AUX_RX_FIFO_LEN);
            res |= ((uint32_t)s->read_count) << 16; /* rx fifo fill level */
        }
        return res;

    case AUX_MU_BAUD_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_BAUD_REG unsupported\n", __func__);
        return 0;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
        return 0;
    }
}

static void bcm2835_aux_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    BCM2835AuxState *s = opaque;
    unsigned char ch;

    switch (offset) {
    case AUX_ENABLES:
        if (value != 1) {
            qemu_log_mask(LOG_UNIMP, "%s: unsupported attempt to enable SPI"
                                     " or disable UART: 0x%"PRIx64"\n",
                          __func__, value);
        }
        break;

    case AUX_MU_IO_REG:
        /* "DLAB bit set means access baudrate register" is NYI */
        ch = value;
        /* XXX this blocks entire thread. Rewrite to use
         * qemu_chr_fe_write and background I/O callbacks */
        qemu_chr_fe_write_all(&s->chr, &ch, 1);
        break;

    case AUX_MU_IER_REG:
        /* "DLAB bit set means access baudrate register" is NYI */
        s->ier = value & (TX_INT | RX_INT);
        bcm2835_aux_update(s);
        break;

    case AUX_MU_IIR_REG:
        if (value & 0x2) {
            s->read_count = 0;
        }
        break;

    case AUX_MU_LCR_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_LCR_REG unsupported\n", __func__);
        break;

    case AUX_MU_MCR_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_MCR_REG unsupported\n", __func__);
        break;

    case AUX_MU_SCRATCH:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_SCRATCH unsupported\n", __func__);
        break;

    case AUX_MU_CNTL_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_CNTL_REG unsupported\n", __func__);
        break;

    case AUX_MU_BAUD_REG:
        qemu_log_mask(LOG_UNIMP, "%s: AUX_MU_BAUD_REG unsupported\n", __func__);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset %"HWADDR_PRIx"\n",
                      __func__, offset);
    }

    bcm2835_aux_update(s);
}

static int bcm2835_aux_can_receive(void *opaque)
{
    BCM2835AuxState *s = opaque;

    return BCM2835_AUX_RX_FIFO_LEN - s->read_count;
}

static void bcm2835_aux_put_fifo(void *opaque, uint8_t value)
{
    BCM2835AuxState *s = opaque;
    int slot;

    slot = s->read_pos + s->read_count;
    if (slot >= BCM2835_AUX_RX_FIFO_LEN) {
        slot -= BCM2835_AUX_RX_FIFO_LEN;
    }
    s->read_fifo[slot] = value;
    s->read_count++;
    if (s->read_count == BCM2835_AUX_RX_FIFO_LEN) {
        /* buffer full */
    }
    bcm2835_aux_update(s);
}

static void bcm2835_aux_receive(void *opaque, const uint8_t *buf, int size)
{
    for (int i = 0; i < size; i++) {
        bcm2835_aux_put_fifo(opaque, buf[i]);
    }
}

static const MemoryRegionOps bcm2835_aux_ops = {
    .read = bcm2835_aux_read,
    .write = bcm2835_aux_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_aux = {
    .name = TYPE_BCM2835_AUX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(read_fifo, BCM2835AuxState,
                            BCM2835_AUX_RX_FIFO_LEN),
        VMSTATE_UINT8(read_pos, BCM2835AuxState),
        VMSTATE_UINT8(read_count, BCM2835AuxState),
        VMSTATE_UINT8(ier, BCM2835AuxState),
        VMSTATE_UINT8(iir, BCM2835AuxState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_aux_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    BCM2835AuxState *s = BCM2835_AUX(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_aux_ops, s,
                          TYPE_BCM2835_AUX, 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void bcm2835_aux_realize(DeviceState *dev, Error **errp)
{
    BCM2835AuxState *s = BCM2835_AUX(dev);

    qemu_chr_fe_set_handlers(&s->chr, bcm2835_aux_can_receive,
                             bcm2835_aux_receive, NULL, NULL, s, NULL, true);
}

static const Property bcm2835_aux_props[] = {
    DEFINE_PROP_CHR("chardev", BCM2835AuxState, chr),
};

static void bcm2835_aux_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = bcm2835_aux_realize;
    dc->vmsd = &vmstate_bcm2835_aux;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    device_class_set_props(dc, bcm2835_aux_props);
}

static const TypeInfo bcm2835_aux_info = {
    .name          = TYPE_BCM2835_AUX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835AuxState),
    .instance_init = bcm2835_aux_init,
    .class_init    = bcm2835_aux_class_init,
};

static void bcm2835_aux_register_types(void)
{
    type_register_static(&bcm2835_aux_info);
}

type_init(bcm2835_aux_register_types)
