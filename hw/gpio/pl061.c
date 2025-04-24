/*
 * Arm PrimeCell PL061 General Purpose IO with additional
 * Luminary Micro Stellaris bits.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 *
 * QEMU interface:
 *  + sysbus MMIO region 0: the device registers
 *  + sysbus IRQ: the GPIOINTR interrupt line
 *  + unnamed GPIO inputs 0..7: inputs to connect to the emulated GPIO lines
 *  + unnamed GPIO outputs 0..7: the emulated GPIO lines, considered as
 *    outputs
 *  + QOM property "pullups": an integer defining whether non-floating lines
 *    configured as inputs should be pulled up to logical 1 (ie whether in
 *    real hardware they have a pullup resistor on the line out of the PL061).
 *    This should be an 8-bit value, where bit 0 is 1 if GPIO line 0 should
 *    be pulled high, bit 1 configures line 1, and so on. The default is 0xff,
 *    indicating that all GPIO lines are pulled up to logical 1.
 *  + QOM property "pulldowns": an integer defining whether non-floating lines
 *    configured as inputs should be pulled down to logical 0 (ie whether in
 *    real hardware they have a pulldown resistor on the line out of the PL061).
 *    This should be an 8-bit value, where bit 0 is 1 if GPIO line 0 should
 *    be pulled low, bit 1 configures line 1, and so on. The default is 0x0.
 *    It is an error to set a bit in both "pullups" and "pulldowns". If a bit
 *    is 0 in both, then the line is considered to be floating, and it will
 *    not have qemu_set_irq() called on it when it is configured as an input.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "trace.h"

static const uint8_t pl061_id[12] =
  { 0x00, 0x00, 0x00, 0x00, 0x61, 0x10, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };
static const uint8_t pl061_id_luminary[12] =
  { 0x00, 0x00, 0x00, 0x00, 0x61, 0x00, 0x18, 0x01, 0x0d, 0xf0, 0x05, 0xb1 };

#define TYPE_PL061 "pl061"
OBJECT_DECLARE_SIMPLE_TYPE(PL061State, PL061)

#define N_GPIOS 8

struct PL061State {
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
    qemu_irq out[N_GPIOS];
    const unsigned char *id;
    /* Properties, for non-Luminary PL061 */
    uint32_t pullups;
    uint32_t pulldowns;
};

static const VMStateDescription vmstate_pl061 = {
    .name = "pl061",
    .version_id = 4,
    .minimum_version_id = 4,
    .fields = (const VMStateField[]) {
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

static uint8_t pl061_floating(PL061State *s)
{
    /*
     * Return mask of bits which correspond to pins configured as inputs
     * and which are floating (neither pulled up to 1 nor down to 0).
     */
    uint8_t floating;

    if (s->id == pl061_id_luminary) {
        /*
         * If both PUR and PDR bits are clear, there is neither a pullup
         * nor a pulldown in place, and the output truly floats.
         */
        floating = ~(s->pur | s->pdr);
    } else {
        floating = ~(s->pullups | s->pulldowns);
    }
    return floating & ~s->dir;
}

static uint8_t pl061_pullups(PL061State *s)
{
    /*
     * Return mask of bits which correspond to pins configured as inputs
     * and which are pulled up to 1.
     */
    uint8_t pullups;

    if (s->id == pl061_id_luminary) {
        /*
         * The Luminary variant of the PL061 has an extra registers which
         * the guest can use to configure whether lines should be pullup
         * or pulldown.
         */
        pullups = s->pur;
    } else {
        pullups = s->pullups;
    }
    return pullups & ~s->dir;
}

static void pl061_update(PL061State *s)
{
    uint8_t changed;
    uint8_t mask;
    uint8_t out;
    int i;
    uint8_t pullups = pl061_pullups(s);
    uint8_t floating = pl061_floating(s);

    trace_pl061_update(DEVICE(s)->canonical_path, s->dir, s->data,
                       pullups, floating);

    /*
     * Pins configured as output are driven from the data register;
     * otherwise if they're pulled up they're 1, and if they're floating
     * then we give them the same value they had previously, so we don't
     * report any change to the other end.
     */
    out = (s->data & s->dir) | pullups | (s->old_out_data & floating);
    changed = s->old_out_data ^ out;
    if (changed) {
        s->old_out_data = out;
        for (i = 0; i < N_GPIOS; i++) {
            mask = 1 << i;
            if (changed & mask) {
                int level = (out & mask) != 0;
                trace_pl061_set_output(DEVICE(s)->canonical_path, i, level);
                qemu_set_irq(s->out[i], level);
            }
        }
    }

    /* Inputs */
    changed = (s->old_in_data ^ s->data) & ~s->dir;
    if (changed) {
        s->old_in_data = s->data;
        for (i = 0; i < N_GPIOS; i++) {
            mask = 1 << i;
            if (changed & mask) {
                trace_pl061_input_change(DEVICE(s)->canonical_path, i,
                                         (s->data & mask) != 0);

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

    trace_pl061_update_istate(DEVICE(s)->canonical_path,
                              s->istate, s->im, (s->istate & s->im) != 0);

    qemu_set_irq(s->irq, (s->istate & s->im) != 0);
}

static uint64_t pl061_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    PL061State *s = (PL061State *)opaque;
    uint64_t r = 0;

    switch (offset) {
    case 0x0 ... 0x3ff: /* Data */
        r = s->data & (offset >> 2);
        break;
    case 0x400: /* Direction */
        r = s->dir;
        break;
    case 0x404: /* Interrupt sense */
        r = s->isense;
        break;
    case 0x408: /* Interrupt both edges */
        r = s->ibe;
        break;
    case 0x40c: /* Interrupt event */
        r = s->iev;
        break;
    case 0x410: /* Interrupt mask */
        r = s->im;
        break;
    case 0x414: /* Raw interrupt status */
        r = s->istate;
        break;
    case 0x418: /* Masked interrupt status */
        r = s->istate & s->im;
        break;
    case 0x420: /* Alternate function select */
        r = s->afsel;
        break;
    case 0x500: /* 2mA drive */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        r = s->dr2r;
        break;
    case 0x504: /* 4mA drive */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        r = s->dr4r;
        break;
    case 0x508: /* 8mA drive */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        r = s->dr8r;
        break;
    case 0x50c: /* Open drain */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        r = s->odr;
        break;
    case 0x510: /* Pull-up */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        r = s->pur;
        break;
    case 0x514: /* Pull-down */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        r = s->pdr;
        break;
    case 0x518: /* Slew rate control */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        r = s->slr;
        break;
    case 0x51c: /* Digital enable */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        r = s->den;
        break;
    case 0x520: /* Lock */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        r = s->locked;
        break;
    case 0x524: /* Commit */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        r = s->cr;
        break;
    case 0x528: /* Analog mode select */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        r = s->amsel;
        break;
    case 0xfd0 ... 0xfff: /* ID registers */
        r = s->id[(offset - 0xfd0) >> 2];
        break;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl061_read: Bad offset %x\n", (int)offset);
        break;
    }

    trace_pl061_read(DEVICE(s)->canonical_path, offset, r);
    return r;
}

static void pl061_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    PL061State *s = (PL061State *)opaque;
    uint8_t mask;

    trace_pl061_write(DEVICE(s)->canonical_path, offset, value);

    switch (offset) {
    case 0 ... 0x3ff:
        mask = (offset >> 2) & s->dir;
        s->data = (s->data & ~mask) | (value & mask);
        pl061_update(s);
        return;
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
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        s->dr2r = value & 0xff;
        break;
    case 0x504: /* 4mA drive */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        s->dr4r = value & 0xff;
        break;
    case 0x508: /* 8mA drive */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        s->dr8r = value & 0xff;
        break;
    case 0x50c: /* Open drain */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        s->odr = value & 0xff;
        break;
    case 0x510: /* Pull-up */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        s->pur = value & 0xff;
        break;
    case 0x514: /* Pull-down */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        s->pdr = value & 0xff;
        break;
    case 0x518: /* Slew rate control */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        s->slr = value & 0xff;
        break;
    case 0x51c: /* Digital enable */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        s->den = value & 0xff;
        break;
    case 0x520: /* Lock */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        s->locked = (value != 0xacce551);
        break;
    case 0x524: /* Commit */
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        if (!s->locked)
            s->cr = value & 0xff;
        break;
    case 0x528:
        if (s->id != pl061_id_luminary) {
            goto bad_offset;
        }
        s->amsel = value & 0xff;
        break;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl061_write: Bad offset %x\n", (int)offset);
        return;
    }
    pl061_update(s);
}

static void pl061_enter_reset(Object *obj, ResetType type)
{
    PL061State *s = PL061(obj);

    trace_pl061_reset(DEVICE(s)->canonical_path);

    /* reset values from PL061 TRM, Stellaris LM3S5P31 & LM3S8962 Data Sheet */

    /*
     * FIXME: For the LM3S6965, not all of the PL061 instances have the
     * same reset values for GPIOPUR, GPIOAFSEL and GPIODEN, so in theory
     * we should allow the board to configure these via properties.
     * In practice, we don't wire anything up to the affected GPIO lines
     * (PB7, PC0, PC1, PC2, PC3 -- they're used for JTAG), so we can
     * get away with this inaccuracy.
     */
    s->data = 0;
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

static void pl061_hold_reset(Object *obj, ResetType type)
{
    PL061State *s = PL061(obj);
    int i, level;
    uint8_t floating = pl061_floating(s);
    uint8_t pullups = pl061_pullups(s);

    for (i = 0; i < N_GPIOS; i++) {
        if (extract32(floating, i, 1)) {
            continue;
        }
        level = extract32(pullups, i, 1);
        trace_pl061_set_output(DEVICE(s)->canonical_path, i, level);
        qemu_set_irq(s->out[i], level);
    }
    s->old_out_data = pullups;
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
}

static void pl061_init(Object *obj)
{
    PL061State *s = PL061(obj);
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->id = pl061_id;

    memory_region_init_io(&s->iomem, obj, &pl061_ops, s, "pl061", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in(dev, pl061_set_irq, N_GPIOS);
    qdev_init_gpio_out(dev, s->out, N_GPIOS);
}

static void pl061_realize(DeviceState *dev, Error **errp)
{
    PL061State *s = PL061(dev);

    if (s->pullups > 0xff) {
        error_setg(errp, "pullups property must be between 0 and 0xff");
        return;
    }
    if (s->pulldowns > 0xff) {
        error_setg(errp, "pulldowns property must be between 0 and 0xff");
        return;
    }
    if (s->pullups & s->pulldowns) {
        error_setg(errp, "no bit may be set both in pullups and pulldowns");
        return;
    }
}

static const Property pl061_props[] = {
    DEFINE_PROP_UINT32("pullups", PL061State, pullups, 0xff),
    DEFINE_PROP_UINT32("pulldowns", PL061State, pulldowns, 0x0),
};

static void pl061_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_pl061;
    dc->realize = pl061_realize;
    device_class_set_props(dc, pl061_props);
    rc->phases.enter = pl061_enter_reset;
    rc->phases.hold = pl061_hold_reset;
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
