/*
 * Arm PrimeCell PL061 General Purpose IO with additional
 * Luminary Micro Stellaris bits.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

//#define DEBUG_PL061 1

#ifdef DEBUG_PL061
#define DPRINTF(fmt, ...) \
do { printf("pl061: " fmt , ## __VA_ARGS__); } while (0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "pl061: error: " fmt , ## __VA_ARGS__); exit(1);} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "pl061: error: " fmt , ## __VA_ARGS__);} while (0)
#endif

static const uint8_t pl061_id[12] =
  { 0x00, 0x00, 0x00, 0x00, 0x61, 0x10, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };
static const uint8_t pl061_id_luminary[12] =
  { 0x00, 0x00, 0x00, 0x00, 0x61, 0x00, 0x18, 0x01, 0x0d, 0xf0, 0x05, 0xb1 };

#define TYPE_PL061 "pl061"
#define PL061(obj) OBJECT_CHECK(PL061State, (obj), TYPE_PL061)

typedef struct PL061State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t locked;
    uint32_t data;
    uint32_t old_out_data;
    uint32_t old_in_data;
    uint32_t dir;
    uint32_t isense;
    uint32_t ibe;
    uint32_t iev;
    uint32_t im;
    uint32_t istate;
    uint32_t afsel;
    uint32_t dr2r;
    uint32_t dr4r;
    uint32_t dr8r;
    uint32_t odr;
    uint32_t pur;
    uint32_t pdr;
    uint32_t slr;
    uint32_t den;
    uint32_t cr;
    uint32_t amsel;
    qemu_irq irq;
    qemu_irq out[8];
    const unsigned char *id;
    uint32_t rsvd_start; /* reserved area: [rsvd_start, 0xfcc] */
} PL061State;

static const VMStateDescription vmstate_pl061 = {
    .name = "pl061",
    .version_id = 4,
    .minimum_version_id = 4,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(locked, PL061State),
        VMSTATE_UINT32(data, PL061State),
        VMSTATE_UINT32(old_out_data, PL061State),
        VMSTATE_UINT32(old_in_data, PL061State),
        VMSTATE_UINT32(dir, PL061State),
        VMSTATE_UINT32(isense, PL061State),
        VMSTATE_UINT32(ibe, PL061State),
        VMSTATE_UINT32(iev, PL061State),
        VMSTATE_UINT32(im, PL061State),
        VMSTATE_UINT32(istate, PL061State),
        VMSTATE_UINT32(afsel, PL061State),
        VMSTATE_UINT32(dr2r, PL061State),
        VMSTATE_UINT32(dr4r, PL061State),
        VMSTATE_UINT32(dr8r, PL061State),
        VMSTATE_UINT32(odr, PL061State),
        VMSTATE_UINT32(pur, PL061State),
        VMSTATE_UINT32(pdr, PL061State),
        VMSTATE_UINT32(slr, PL061State),
        VMSTATE_UINT32(den, PL061State),
        VMSTATE_UINT32(cr, PL061State),
        VMSTATE_UINT32_V(amsel, PL061State, 2),
        VMSTATE_END_OF_LIST()
    }
};

static void pl061_update(PL061State *s)
{
    uint8_t changed;
    uint8_t mask;
    uint8_t out;
    int i;

    DPRINTF("dir = %d, data = %d\n", s->dir, s->data);

    /* Outputs float high.  */
    /* FIXME: This is board dependent.  */
    out = (s->data & s->dir) | ~s->dir;
    changed = s->old_out_data ^ out;
    if (changed) {
        s->old_out_data = out;
        for (i = 0; i < 8; i++) {
            mask = 1 << i;
            if (changed & mask) {
                DPRINTF("Set output %d = %d\n", i, (out & mask) != 0);
                qemu_set_irq(s->out[i], (out & mask) != 0);
            }
        }
    }

    /* Inputs */
    changed = (s->old_in_data ^ s->data) & ~s->dir;
    if (changed) {
        s->old_in_data = s->data;
        for (i = 0; i < 8; i++) {
            mask = 1 << i;
            if (changed & mask) {
                DPRINTF("Changed input %d = %d\n", i, (s->data & mask) != 0);

                if (!(s->isense & mask)) {
                    /* Edge interrupt */
                    if (s->ibe & mask) {
                        /* Any edge triggers the interrupt */
                        s->istate |= mask;
                    } else {
                        /* Edge is selected by IEV */
                        s->istate |= ~(s->data ^ s->iev) & mask;
                    }
                }
            }
        }
    }

    /* Level interrupt */
    s->istate |= ~(s->data ^ s->iev) & s->isense;

    DPRINTF("istate = %02X\n", s->istate);

    qemu_set_irq(s->irq, (s->istate & s->im) != 0);
}

static uint64_t pl061_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    PL061State *s = (PL061State *)opaque;

    if (offset < 0x400) {
        return s->data & (offset >> 2);
    }
    if (offset >= s->rsvd_start && offset <= 0xfcc) {
        goto err_out;
    }
    if (offset >= 0xfd0 && offset < 0x1000) {
        return s->id[(offset - 0xfd0) >> 2];
    }
    switch (offset) {
    case 0x400: /* Direction */
        return s->dir;
    case 0x404: /* Interrupt sense */
        return s->isense;
    case 0x408: /* Interrupt both edges */
        return s->ibe;
    case 0x40c: /* Interrupt event */
        return s->iev;
    case 0x410: /* Interrupt mask */
        return s->im;
    case 0x414: /* Raw interrupt status */
        return s->istate;
    case 0x418: /* Masked interrupt status */
        return s->istate & s->im;
    case 0x420: /* Alternate function select */
        return s->afsel;
    case 0x500: /* 2mA drive */
        return s->dr2r;
    case 0x504: /* 4mA drive */
        return s->dr4r;
    case 0x508: /* 8mA drive */
        return s->dr8r;
    case 0x50c: /* Open drain */
        return s->odr;
    case 0x510: /* Pull-up */
        return s->pur;
    case 0x514: /* Pull-down */
        return s->pdr;
    case 0x518: /* Slew rate control */
        return s->slr;
    case 0x51c: /* Digital enable */
        return s->den;
    case 0x520: /* Lock */
        return s->locked;
    case 0x524: /* Commit */
        return s->cr;
    case 0x528: /* Analog mode select */
        return s->amsel;
    default:
        break;
    }
err_out:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "pl061_read: Bad offset %x\n", (int)offset);
    return 0;
}

static void pl061_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    PL061State *s = (PL061State *)opaque;
    uint8_t mask;

    if (offset < 0x400) {
        mask = (offset >> 2) & s->dir;
        s->data = (s->data & ~mask) | (value & mask);
        pl061_update(s);
        return;
    }
    if (offset >= s->rsvd_start) {
        goto err_out;
    }
    switch (offset) {
    case 0x400: /* Direction */
        s->dir = value & 0xff;
        break;
    case 0x404: /* Interrupt sense */
        s->isense = value & 0xff;
        break;
    case 0x408: /* Interrupt both edges */
        s->ibe = value & 0xff;
        break;
    case 0x40c: /* Interrupt event */
        s->iev = value & 0xff;
        break;
    case 0x410: /* Interrupt mask */
        s->im = value & 0xff;
        break;
    case 0x41c: /* Interrupt clear */
        s->istate &= ~value;
        break;
    case 0x420: /* Alternate function select */
        mask = s->cr;
        s->afsel = (s->afsel & ~mask) | (value & mask);
        break;
    case 0x500: /* 2mA drive */
        s->dr2r = value & 0xff;
        break;
    case 0x504: /* 4mA drive */
        s->dr4r = value & 0xff;
        break;
    case 0x508: /* 8mA drive */
        s->dr8r = value & 0xff;
        break;
    case 0x50c: /* Open drain */
        s->odr = value & 0xff;
        break;
    case 0x510: /* Pull-up */
        s->pur = value & 0xff;
        break;
    case 0x514: /* Pull-down */
        s->pdr = value & 0xff;
        break;
    case 0x518: /* Slew rate control */
        s->slr = value & 0xff;
        break;
    case 0x51c: /* Digital enable */
        s->den = value & 0xff;
        break;
    case 0x520: /* Lock */
        s->locked = (value != 0xacce551);
        break;
    case 0x524: /* Commit */
        if (!s->locked)
            s->cr = value & 0xff;
        break;
    case 0x528:
        s->amsel = value & 0xff;
        break;
    default:
        goto err_out;
    }
    pl061_update(s);
    return;
err_out:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "pl061_write: Bad offset %x\n", (int)offset);
}

static void pl061_reset(DeviceState *dev)
{
    PL061State *s = PL061(dev);

    /* reset values from PL061 TRM, Stellaris LM3S5P31 & LM3S8962 Data Sheet */
    s->data = 0;
    s->old_out_data = 0;
    s->old_in_data = 0;
    s->dir = 0;
    s->isense = 0;
    s->ibe = 0;
    s->iev = 0;
    s->im = 0;
    s->istate = 0;
    s->afsel = 0;
    s->dr2r = 0xff;
    s->dr4r = 0;
    s->dr8r = 0;
    s->odr = 0;
    s->pur = 0;
    s->pdr = 0;
    s->slr = 0;
    s->den = 0;
    s->locked = 1;
    s->cr = 0xff;
    s->amsel = 0;
}

static void pl061_set_irq(void * opaque, int irq, int level)
{
    PL061State *s = (PL061State *)opaque;
    uint8_t mask;

    mask = 1 << irq;
    if ((s->dir & mask) == 0) {
        s->data &= ~mask;
        if (level)
            s->data |= mask;
        pl061_update(s);
    }
}

static const MemoryRegionOps pl061_ops = {
    .read = pl061_read,
    .write = pl061_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pl061_luminary_init(Object *obj)
{
    PL061State *s = PL061(obj);

    s->id = pl061_id_luminary;
    s->rsvd_start = 0x52c;
}

static void pl061_init(Object *obj)
{
    PL061State *s = PL061(obj);
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->id = pl061_id;
    s->rsvd_start = 0x424;

    memory_region_init_io(&s->iomem, obj, &pl061_ops, s, "pl061", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in(dev, pl061_set_irq, 8);
    qdev_init_gpio_out(dev, s->out, 8);
}

static void pl061_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_pl061;
    dc->reset = &pl061_reset;
}

static const TypeInfo pl061_info = {
    .name          = TYPE_PL061,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PL061State),
    .instance_init = pl061_init,
    .class_init    = pl061_class_init,
};

static const TypeInfo pl061_luminary_info = {
    .name          = "pl061_luminary",
    .parent        = TYPE_PL061,
    .instance_init = pl061_luminary_init,
};

static void pl061_register_types(void)
{
    type_register_static(&pl061_info);
    type_register_static(&pl061_luminary_info);
}

type_init(pl061_register_types)
