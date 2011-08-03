/*
 * Arm PrimeCell PL061 General Purpose IO with additional
 * Luminary Micro Stellaris bits.
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "sysbus.h"

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

typedef struct {
    SysBusDevice busdev;
    uint32_t locked;
    uint32_t data;
    uint32_t old_data;
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
    uint32_t float_high;
    qemu_irq irq;
    qemu_irq out[8];
    const unsigned char *id;
} pl061_state;

static const VMStateDescription vmstate_pl061 = {
    .name = "pl061",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(locked, pl061_state),
        VMSTATE_UINT32(data, pl061_state),
        VMSTATE_UINT32(old_data, pl061_state),
        VMSTATE_UINT32(dir, pl061_state),
        VMSTATE_UINT32(isense, pl061_state),
        VMSTATE_UINT32(ibe, pl061_state),
        VMSTATE_UINT32(iev, pl061_state),
        VMSTATE_UINT32(im, pl061_state),
        VMSTATE_UINT32(istate, pl061_state),
        VMSTATE_UINT32(afsel, pl061_state),
        VMSTATE_UINT32(dr2r, pl061_state),
        VMSTATE_UINT32(dr4r, pl061_state),
        VMSTATE_UINT32(dr8r, pl061_state),
        VMSTATE_UINT32(odr, pl061_state),
        VMSTATE_UINT32(pur, pl061_state),
        VMSTATE_UINT32(pdr, pl061_state),
        VMSTATE_UINT32(slr, pl061_state),
        VMSTATE_UINT32(den, pl061_state),
        VMSTATE_UINT32(cr, pl061_state),
        VMSTATE_UINT32(float_high, pl061_state),
        VMSTATE_END_OF_LIST()
    }
};

static void pl061_update(pl061_state *s)
{
    uint8_t changed;
    uint8_t mask;
    uint8_t out;
    int i;

    /* Outputs float high.  */
    /* FIXME: This is board dependent.  */
    out = (s->data & s->dir) | ~s->dir;
    changed = s->old_data ^ out;
    if (!changed)
        return;

    s->old_data = out;
    for (i = 0; i < 8; i++) {
        mask = 1 << i;
        if ((changed & mask) && s->out) {
            DPRINTF("Set output %d = %d\n", i, (out & mask) != 0);
            qemu_set_irq(s->out[i], (out & mask) != 0);
        }
    }

    /* FIXME: Implement input interrupts.  */
}

static uint32_t pl061_read(void *opaque, target_phys_addr_t offset)
{
    pl061_state *s = (pl061_state *)opaque;

    if (offset >= 0xfd0 && offset < 0x1000) {
        return s->id[(offset - 0xfd0) >> 2];
    }
    if (offset < 0x400) {
        return s->data & (offset >> 2);
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
        return s->istate | s->im;
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
    default:
        hw_error("pl061_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void pl061_write(void *opaque, target_phys_addr_t offset,
                        uint32_t value)
{
    pl061_state *s = (pl061_state *)opaque;
    uint8_t mask;

    if (offset < 0x400) {
        mask = (offset >> 2) & s->dir;
        s->data = (s->data & ~mask) | (value & mask);
        pl061_update(s);
        return;
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
    default:
        hw_error("pl061_write: Bad offset %x\n", (int)offset);
    }
    pl061_update(s);
}

static void pl061_reset(pl061_state *s)
{
  s->locked = 1;
  s->cr = 0xff;
}

static void pl061_set_irq(void * opaque, int irq, int level)
{
    pl061_state *s = (pl061_state *)opaque;
    uint8_t mask;

    mask = 1 << irq;
    if ((s->dir & mask) == 0) {
        s->data &= ~mask;
        if (level)
            s->data |= mask;
        pl061_update(s);
    }
}

static CPUReadMemoryFunc * const pl061_readfn[] = {
   pl061_read,
   pl061_read,
   pl061_read
};

static CPUWriteMemoryFunc * const pl061_writefn[] = {
   pl061_write,
   pl061_write,
   pl061_write
};

static int pl061_init(SysBusDevice *dev, const unsigned char *id)
{
    int iomemtype;
    pl061_state *s = FROM_SYSBUS(pl061_state, dev);
    s->id = id;
    iomemtype = cpu_register_io_memory(pl061_readfn,
                                       pl061_writefn, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, iomemtype);
    sysbus_init_irq(dev, &s->irq);
    qdev_init_gpio_in(&dev->qdev, pl061_set_irq, 8);
    qdev_init_gpio_out(&dev->qdev, s->out, 8);
    pl061_reset(s);
    return 0;
}

static int pl061_init_luminary(SysBusDevice *dev)
{
    return pl061_init(dev, pl061_id_luminary);
}

static int pl061_init_arm(SysBusDevice *dev)
{
    return pl061_init(dev, pl061_id);
}

static SysBusDeviceInfo pl061_info = {
    .init = pl061_init_arm,
    .qdev.name = "pl061",
    .qdev.size = sizeof(pl061_state),
    .qdev.vmsd = &vmstate_pl061,
};

static SysBusDeviceInfo pl061_luminary_info = {
    .init = pl061_init_luminary,
    .qdev.name = "pl061_luminary",
    .qdev.size = sizeof(pl061_state),
    .qdev.vmsd = &vmstate_pl061,
};

static void pl061_register_devices(void)
{
    sysbus_register_withprop(&pl061_info);
    sysbus_register_withprop(&pl061_luminary_info);
}

device_init(pl061_register_devices)
