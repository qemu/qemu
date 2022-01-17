/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 *
 * This file models the system mailboxes, which are used for
 * communication with low-bandwidth GPU peripherals. Refs:
 *   https://github.com/raspberrypi/firmware/wiki/Mailboxes
 *   https://github.com/raspberrypi/firmware/wiki/Accessing-mailboxes
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/misc/bcm2835_mbox.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

#define MAIL0_PEEK   0x90
#define MAIL0_SENDER 0x94
#define MAIL1_STATUS 0xb8

/* Mailbox status register */
#define MAIL0_STATUS 0x98
#define ARM_MS_FULL       0x80000000
#define ARM_MS_EMPTY      0x40000000
#define ARM_MS_LEVEL      0x400000FF /* Max. value depends on mailbox depth */

/* MAILBOX config/status register */
#define MAIL0_CONFIG 0x9c
/* ANY write to this register clears the error bits! */
#define ARM_MC_IHAVEDATAIRQEN    0x00000001 /* mbox irq enable:  has data */
#define ARM_MC_IHAVESPACEIRQEN   0x00000002 /* mbox irq enable:  has space */
#define ARM_MC_OPPISEMPTYIRQEN   0x00000004 /* mbox irq enable: Opp is empty */
#define ARM_MC_MAIL_CLEAR        0x00000008 /* mbox clear write 1, then  0 */
#define ARM_MC_IHAVEDATAIRQPEND  0x00000010 /* mbox irq pending:  has space */
#define ARM_MC_IHAVESPACEIRQPEND 0x00000020 /* mbox irq pending: Opp is empty */
#define ARM_MC_OPPISEMPTYIRQPEND 0x00000040 /* mbox irq pending */
/* Bit 7 is unused */
#define ARM_MC_ERRNOOWN   0x00000100 /* error : none owner read from mailbox */
#define ARM_MC_ERROVERFLW 0x00000200 /* error : write to fill mailbox */
#define ARM_MC_ERRUNDRFLW 0x00000400 /* error : read from empty mailbox */

static void mbox_update_status(BCM2835Mbox *mb)
{
    mb->status &= ~(ARM_MS_EMPTY | ARM_MS_FULL);
    if (mb->count == 0) {
        mb->status |= ARM_MS_EMPTY;
    } else if (mb->count == MBOX_SIZE) {
        mb->status |= ARM_MS_FULL;
    }
}

static void mbox_reset(BCM2835Mbox *mb)
{
    int n;

    mb->count = 0;
    mb->config = 0;
    for (n = 0; n < MBOX_SIZE; n++) {
        mb->reg[n] = MBOX_INVALID_DATA;
    }
    mbox_update_status(mb);
}

static uint32_t mbox_pull(BCM2835Mbox *mb, int index)
{
    int n;
    uint32_t val;

    assert(mb->count > 0);
    assert(index < mb->count);

    val = mb->reg[index];
    for (n = index + 1; n < mb->count; n++) {
        mb->reg[n - 1] = mb->reg[n];
    }
    mb->count--;
    mb->reg[mb->count] = MBOX_INVALID_DATA;

    mbox_update_status(mb);

    return val;
}

static void mbox_push(BCM2835Mbox *mb, uint32_t val)
{
    assert(mb->count < MBOX_SIZE);
    mb->reg[mb->count++] = val;
    mbox_update_status(mb);
}

static void bcm2835_mbox_update(BCM2835MboxState *s)
{
    uint32_t value;
    bool set;
    int n;

    s->mbox_irq_disabled = true;

    /* Get pending responses and put them in the vc->arm mbox,
     * as long as it's not full
     */
    for (n = 0; n < MBOX_CHAN_COUNT; n++) {
        while (s->available[n] && !(s->mbox[0].status & ARM_MS_FULL)) {
            value = ldl_le_phys(&s->mbox_as, n << MBOX_AS_CHAN_SHIFT);
            assert(value != MBOX_INVALID_DATA); /* Pending interrupt but no data */
            mbox_push(&s->mbox[0], value);
        }
    }

    /* TODO (?): Try to push pending requests from the arm->vc mbox */

    /* Re-enable calls from the IRQ routine */
    s->mbox_irq_disabled = false;

    /* Update ARM IRQ status */
    set = false;
    s->mbox[0].config &= ~ARM_MC_IHAVEDATAIRQPEND;
    if (!(s->mbox[0].status & ARM_MS_EMPTY)) {
        s->mbox[0].config |= ARM_MC_IHAVEDATAIRQPEND;
        if (s->mbox[0].config & ARM_MC_IHAVEDATAIRQEN) {
            set = true;
        }
    }
    trace_bcm2835_mbox_irq(set);
    qemu_set_irq(s->arm_irq, set);
}

static void bcm2835_mbox_set_irq(void *opaque, int irq, int level)
{
    BCM2835MboxState *s = opaque;

    s->available[irq] = level;

    /* avoid recursively calling bcm2835_mbox_update when the interrupt
     * status changes due to the ldl_phys call within that function
     */
    if (!s->mbox_irq_disabled) {
        bcm2835_mbox_update(s);
    }
}

static uint64_t bcm2835_mbox_read(void *opaque, hwaddr offset, unsigned size)
{
    BCM2835MboxState *s = opaque;
    uint32_t res = 0;

    offset &= 0xff;

    switch (offset) {
    case 0x80 ... 0x8c: /* MAIL0_READ */
        if (s->mbox[0].status & ARM_MS_EMPTY) {
            res = MBOX_INVALID_DATA;
        } else {
            res = mbox_pull(&s->mbox[0], 0);
        }
        break;

    case MAIL0_PEEK:
        res = s->mbox[0].reg[0];
        break;

    case MAIL0_SENDER:
        break;

    case MAIL0_STATUS:
        res = s->mbox[0].status;
        break;

    case MAIL0_CONFIG:
        res = s->mbox[0].config;
        break;

    case MAIL1_STATUS:
        res = s->mbox[1].status;
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unsupported offset 0x%"HWADDR_PRIx"\n",
                      __func__, offset);
        trace_bcm2835_mbox_read(size, offset, res);
        return 0;
    }
    trace_bcm2835_mbox_read(size, offset, res);

    bcm2835_mbox_update(s);

    return res;
}

static void bcm2835_mbox_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    BCM2835MboxState *s = opaque;
    hwaddr childaddr;
    uint8_t ch;

    offset &= 0xff;

    trace_bcm2835_mbox_write(size, offset, value);
    switch (offset) {
    case MAIL0_SENDER:
        break;

    case MAIL0_CONFIG:
        s->mbox[0].config &= ~ARM_MC_IHAVEDATAIRQEN;
        s->mbox[0].config |= value & ARM_MC_IHAVEDATAIRQEN;
        break;

    case 0xa0 ... 0xac: /* MAIL1_WRITE */
        if (s->mbox[1].status & ARM_MS_FULL) {
            /* Mailbox full */
            qemu_log_mask(LOG_GUEST_ERROR, "%s: mailbox full\n", __func__);
        } else {
            ch = value & 0xf;
            if (ch < MBOX_CHAN_COUNT) {
                childaddr = ch << MBOX_AS_CHAN_SHIFT;
                if (ldl_le_phys(&s->mbox_as, childaddr + MBOX_AS_PENDING)) {
                    /* Child busy, push delayed. Push it in the arm->vc mbox */
                    mbox_push(&s->mbox[1], value);
                } else {
                    /* Push it directly to the child device */
                    stl_le_phys(&s->mbox_as, childaddr, value);
                }
            } else {
                /* Invalid channel number */
                qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid channel %u\n",
                              __func__, ch);
            }
        }
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unsupported offset 0x%"HWADDR_PRIx
                                 " value 0x%"PRIx64"\n",
                      __func__, offset, value);
        return;
    }

    bcm2835_mbox_update(s);
}

static const MemoryRegionOps bcm2835_mbox_ops = {
    .read = bcm2835_mbox_read,
    .write = bcm2835_mbox_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

/* vmstate of a single mailbox */
static const VMStateDescription vmstate_bcm2835_mbox_box = {
    .name = TYPE_BCM2835_MBOX "_box",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, BCM2835Mbox, MBOX_SIZE),
        VMSTATE_UINT32(count, BCM2835Mbox),
        VMSTATE_UINT32(status, BCM2835Mbox),
        VMSTATE_UINT32(config, BCM2835Mbox),
        VMSTATE_END_OF_LIST()
    }
};

/* vmstate of the entire device */
static const VMStateDescription vmstate_bcm2835_mbox = {
    .name = TYPE_BCM2835_MBOX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_BOOL_ARRAY(available, BCM2835MboxState, MBOX_CHAN_COUNT),
        VMSTATE_STRUCT_ARRAY(mbox, BCM2835MboxState, 2, 1,
                             vmstate_bcm2835_mbox_box, BCM2835Mbox),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_mbox_init(Object *obj)
{
    BCM2835MboxState *s = BCM2835_MBOX(obj);

    memory_region_init_io(&s->iomem, obj, &bcm2835_mbox_ops, s,
                          TYPE_BCM2835_MBOX, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->arm_irq);
    qdev_init_gpio_in(DEVICE(s), bcm2835_mbox_set_irq, MBOX_CHAN_COUNT);
}

static void bcm2835_mbox_reset(DeviceState *dev)
{
    BCM2835MboxState *s = BCM2835_MBOX(dev);
    int n;

    mbox_reset(&s->mbox[0]);
    mbox_reset(&s->mbox[1]);
    s->mbox_irq_disabled = false;
    for (n = 0; n < MBOX_CHAN_COUNT; n++) {
        s->available[n] = false;
    }
}

static void bcm2835_mbox_realize(DeviceState *dev, Error **errp)
{
    BCM2835MboxState *s = BCM2835_MBOX(dev);
    Object *obj;

    obj = object_property_get_link(OBJECT(dev), "mbox-mr", &error_abort);
    s->mbox_mr = MEMORY_REGION(obj);
    address_space_init(&s->mbox_as, s->mbox_mr, TYPE_BCM2835_MBOX "-memory");
    bcm2835_mbox_reset(dev);
}

static void bcm2835_mbox_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bcm2835_mbox_realize;
    dc->reset = bcm2835_mbox_reset;
    dc->vmsd = &vmstate_bcm2835_mbox;
}

static const TypeInfo bcm2835_mbox_info = {
    .name          = TYPE_BCM2835_MBOX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835MboxState),
    .class_init    = bcm2835_mbox_class_init,
    .instance_init = bcm2835_mbox_init,
};

static void bcm2835_mbox_register_types(void)
{
    type_register_static(&bcm2835_mbox_info);
}

type_init(bcm2835_mbox_register_types)
