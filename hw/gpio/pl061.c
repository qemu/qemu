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

#define TYPE_PL061 "pl061"
OBJECT_DECLARE_SIMPLE_TYPE(PL061State, PL061)

#define N_GPIOS 8
enum Pin {
    PIN0 = 0,
    PIN1 = 1,
    PIN2 = 2,
    PIN3 = 3,
    PIN4 = 4,
    PIN5 = 5,
    PIN6 = 6,
    PIN7 = 7,
};

struct PL061State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint8_t state;
    uint8_t dirs;
    uint8_t sense;
    uint8_t bothEdge;
    uint8_t eventTriggers;
    uint8_t interrupts;
    uint8_t interruptMask;
    uint8_t controlMode;
    struct {
        qemu_irq irq;
        qemu_irq out[N_GPIOS];
    };
    struct {
        uint8_t pullups;
        uint8_t pulldowns;
    };
};

static const VMStateDescription vmstate_pl061 = {
    .name = "pl061",
    .version_id = 4,
    .minimum_version_id = 4,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(state, PL061State),
        VMSTATE_UINT8(dirs, PL061State),
        VMSTATE_UINT8(sense, PL061State),
        VMSTATE_UINT8(bothEdge, PL061State),
        VMSTATE_UINT8(eventTriggers, PL061State),
        VMSTATE_UINT8(interrupts, PL061State),
        VMSTATE_UINT8(interruptMask, PL061State),
        VMSTATE_UINT8(controlMode, PL061State),
        VMSTATE_END_OF_LIST()
    }
};

static void change_input_pin(PL061State *s, uint8_t pin, uint8_t value) {
    uint16_t pinMask = 0x1;
    pinMask <<= pin;
    uint16_t oldValue = 0x0;
    if ((s->state & pinMask) == pinMask) {
        oldValue = 0x1;
    }

    if ((s->dirs & pinMask) == pinMask) {
        return;
    }

    if (value == 0x0) {
        s->state &= ~(1 << pin);
    } else {
        s->state |= (1 << pin);
    }

    if ((s->sense & pinMask) == pinMask) {
        uint16_t level = s->eventTriggers;
        level &= pinMask;
        level >>= pin;
        if (level == value) {
            s->interrupts |= (1 << pin);
        } else {
            s->interrupts &= ~(1 << pin);
        }

        return;
    }

    if ((s->interrupts & pinMask) == pinMask) {
        return;
    }

    uint16_t diff = value;
    diff ^= oldValue;
    if (diff == 0x0) {
        return;
    }

    if ((s->bothEdge & pinMask) == pinMask) {
        s->interrupts |= (1 << pin);
    }

    if ((s->eventTriggers & pinMask) == pinMask) {
        diff &= value;
    } else {
        diff &= oldValue;
    }

    if (diff == 0x1) {
        s->interrupts |= (1 << pin);
    }

}

static inline uint8_t get_mask(enum Pin pin) {
    return 1 << pin;
}

static int is_input(PL061State *s, enum Pin pin) {
    uint8_t mask = get_mask(pin);
    return (s->dirs & mask) == 0;
}

static int is_output(PL061State *s, enum Pin pin) {
    return !is_input(s, pin);
}

static void set_output_pin(PL061State *s,  enum Pin pin, int level) {
    trace_pl061_set_output(DEVICE(s)->canonical_path, pin, level);
    qemu_set_irq(s->out[pin], level);
}

static int get_pin_level(PL061State *s, enum Pin pin) {
    uint8_t mask = get_mask(pin);
    return (s->state & mask) != 0;
}

static void pl061_update(PL061State *s) {
    for (int pin = PIN0; pin <= PIN7; pin++)
    {
        int level = get_pin_level(s, pin);
        if (is_output(s, pin)) {
            set_output_pin(s, pin, level);
        } else {
            trace_pl061_input_change(DEVICE(s)->canonical_path, pin, level);
            change_input_pin(s, pin, level);
        }
    }
    qemu_set_irq(s->irq, (s->interrupts & s->interruptMask) != 0);
}


static uint64_t readGPIOAFSEL(PL061State *s) {
    return s->controlMode;
}


static uint64_t readGPIOCellID0(PL061State *s) {
    return 0xd;
}


static uint64_t readGPIOCellID1(PL061State *s) {
    return 0xf0;
}


static uint64_t readGPIOCellID2(PL061State *s) {
    return 0x5;
}


static uint64_t readGPIOCellID3(PL061State *s) {
    return 0xb1;
}


static uint64_t readGPIODATA(PL061State *s, uint8_t mask) {
    mask &= s->state;
    return mask;
}


static uint64_t readGPIODIR(PL061State *s) {
    return s->dirs;
}


static uint64_t readGPIOIBE(PL061State *s) {
    return s->bothEdge;
}


static uint64_t readGPIOIE(PL061State *s) {
    return s->interruptMask;
}


static uint64_t readGPIOIEV(PL061State *s) {
    return s->eventTriggers;
}


static uint64_t readGPIOIS(PL061State *s) {
    return s->sense;
}


static uint64_t readGPIOMIS(PL061State *s) {
    uint16_t tmp = s->interrupts;
    tmp &= s->interruptMask;
    return tmp;
}


static uint64_t readGPIOPeriphID0(PL061State *s) {
    return 0x61;
}


static uint64_t readGPIOPeriphID1(PL061State *s) {
    return 0x10;
}


static uint64_t readGPIOPeriphID2(PL061State *s) {
    return 0x4;
}


static uint64_t readGPIOPeriphID3(PL061State *s) {
    return 0x0;
}


static uint64_t readGPIORIS(PL061State *s) {
    return s->interrupts;
}


static void writeGPIOAFSEL(PL061State *s, uint64_t value) {
    s->controlMode = value;
}


static void writeGPIODATA(PL061State *s, uint8_t mask, uint8_t value) {
    mask &= s->dirs;
    value &= mask;
    mask = ~mask;
    s->state &= mask;
    s->state |= value;
}


static void writeGPIODIR(PL061State *s, uint64_t value) {
    s->dirs = value;
}


static void writeGPIOIBE(PL061State *s, uint64_t value) {
    s->bothEdge = value;
}


static void writeGPIOIC(PL061State *s, uint64_t value) {
    value = ~value;
    s->interrupts &= value;
    value = ~value;
}


static void writeGPIOIE(PL061State *s, uint64_t value) {
    s->interruptMask = value;
}


static void writeGPIOIEV(PL061State *s, uint64_t value) {
    s->eventTriggers = value;
}


static void writeGPIOIS(PL061State *s, uint64_t value) {
    s->sense = value;
}

static uint64_t pl061_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    PL061State *s = (PL061State *)opaque;
    uint64_t r = 0;
    uint8_t mask = 0;

    switch (offset) {
    case 0x0 ... 0x3fC: /* Data */
        mask = (uint8_t)((offset & 0x3FC) >> 2);
        r = readGPIODATA(s, mask);
        break;
    case 0x400: /* Direction */
        r = readGPIODIR(s);
        break;
    case 0x404: /* Interrupt sense */
        r = readGPIOIS(s);
        break;
    case 0x408: /* Interrupt both edges */
        r = readGPIOIBE(s);
        break;
    case 0x40c: /* Interrupt event */
        r = readGPIOIEV(s);
        break;
    case 0x410: /* Interrupt mask */
        r = readGPIOIE(s);
        break;
    case 0x414: /* Raw interrupt status */
        r = readGPIORIS(s);
        break;
    case 0x418: /* Masked interrupt status */
        r = readGPIOMIS(s);
        break;
    case 0x420: /* Alternate function select */
        r = readGPIOAFSEL(s);
        break;
    case 0xFE0:
        r = readGPIOPeriphID0(s);
        break;
    case 0xFE4:
        r = readGPIOPeriphID1(s);
        break;
    case 0xFE8:
        r = readGPIOPeriphID2(s);
        break;
    case 0xFEC:
        r = readGPIOPeriphID3(s);
        break;
    case 0xFF0:
        r = readGPIOCellID0(s);
        break;
    case 0xFF4:
        r = readGPIOCellID1(s);
        break;
    case 0xFF8:
        r = readGPIOCellID2(s);
        break;
    case 0xFFC:
        r = readGPIOCellID3(s);
        break;
    default:
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
        mask = (uint8_t)((offset & 0x3FC) >> 2);
        writeGPIODATA(s, mask, value);
        return;
    case 0x400: /* Direction */
        writeGPIODIR(s, value);
        break;
    case 0x404: /* Interrupt sense */
        writeGPIOIS(s, value);
        break;
    case 0x408: /* Interrupt both edges */
        writeGPIOIBE(s, value);
        break;
    case 0x40c: /* Interrupt event */
        writeGPIOIEV(s, value);
        break;
    case 0x410: /* Interrupt mask */
        writeGPIOIE(s, value);
        break;
    case 0x41c: /* Interrupt clear */
        writeGPIOIC(s, value);
        break;
    case 0x420: /* Alternate function select */
        writeGPIOAFSEL(s, value);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl061_write: Bad offset %x\n", (int)offset);
        return;
    }
    pl061_update(s);
    return;
}

static void pl061_enter_reset(Object *obj, ResetType type)
{
    PL061State *s = PL061(obj);

    trace_pl061_reset(DEVICE(s)->canonical_path);
    s->state = 0;
    s->dirs = 0;
    s->sense = 0;
    s->bothEdge = 0;
    s->eventTriggers = 0;
    s->interrupts = 0;
    s->interruptMask = 0;
    s->controlMode = 0;
}

static void pl061_hold_reset(Object *obj, ResetType type)
{
    PL061State *s = PL061(obj);
    uint8_t level = 0;

    // on RESET, all pins become input
    for (int pin = 0; pin <= PIN7 ; pin++) {
        trace_pl061_set_output(DEVICE(s)->canonical_path, pin, level);
        
    }
}

static void pl061_set_irq(void * opaque, int pin, int level)
{
    PL061State *s = (PL061State *)opaque;
    change_input_pin(s, pin, level);
    pl061_update(s);
}

static const MemoryRegionOps pl061_ops = {
    .read = pl061_read,
    .write = pl061_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pl061_init(Object *obj)
{
    PL061State *s = PL061(obj);
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &pl061_ops, s, "pl061", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in(dev, pl061_set_irq, N_GPIOS);
    qdev_init_gpio_out(dev, s->out, N_GPIOS);
}

static void pl061_realize(DeviceState *dev, Error **errp)
{
    // reset to known state
    pl061_enter_reset(dev);
}

static Property pl061_props[] = {
    DEFINE_PROP_UINT8("pullups", PL061State, pullups, 0xff),
    DEFINE_PROP_UINT8("pulldowns", PL061State, pulldowns, 0x0),
    DEFINE_PROP_END_OF_LIST()
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

static void pl061_register_types(void)
{
    type_register_static(&pl061_info);
}

type_init(pl061_register_types)
