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
    int locked;
    uint8_t data;
    uint8_t old_data;
    uint8_t dir;
    uint8_t isense;
    uint8_t ibe;
    uint8_t iev;
    uint8_t im;
    uint8_t istate;
    uint8_t afsel;
    uint8_t dr2r;
    uint8_t dr4r;
    uint8_t dr8r;
    uint8_t odr;
    uint8_t pur;
    uint8_t pdr;
    uint8_t slr;
    uint8_t den;
    uint8_t cr;
    uint8_t float_high;
    qemu_irq irq;
    qemu_irq out[8];
    const unsigned char *id;
} pl061_state;

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
        s->dir = value;
        break;
    case 0x404: /* Interrupt sense */
        s->isense = value;
        break;
    case 0x408: /* Interrupt both edges */
        s->ibe = value;
        break;
    case 0x40c: /* Interrupt event */
        s->iev = value;
        break;
    case 0x410: /* Interrupt mask */
        s->im = value;
        break;
    case 0x41c: /* Interrupt clear */
        s->istate &= ~value;
        break;
    case 0x420: /* Alternate function select */
        mask = s->cr;
        s->afsel = (s->afsel & ~mask) | (value & mask);
        break;
    case 0x500: /* 2mA drive */
        s->dr2r = value;
        break;
    case 0x504: /* 4mA drive */
        s->dr4r = value;
        break;
    case 0x508: /* 8mA drive */
        s->dr8r = value;
        break;
    case 0x50c: /* Open drain */
        s->odr = value;
        break;
    case 0x510: /* Pull-up */
        s->pur = value;
        break;
    case 0x514: /* Pull-down */
        s->pdr = value;
        break;
    case 0x518: /* Slew rate control */
        s->slr = value;
        break;
    case 0x51c: /* Digital enable */
        s->den = value;
        break;
    case 0x520: /* Lock */
        s->locked = (value != 0xacce551);
        break;
    case 0x524: /* Commit */
        if (!s->locked)
            s->cr = value;
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

static void pl061_save(QEMUFile *f, void *opaque)
{
    pl061_state *s = (pl061_state *)opaque;

    qemu_put_be32(f, s->locked);
    qemu_put_be32(f, s->data);
    qemu_put_be32(f, s->old_data);
    qemu_put_be32(f, s->dir);
    qemu_put_be32(f, s->isense);
    qemu_put_be32(f, s->ibe);
    qemu_put_be32(f, s->iev);
    qemu_put_be32(f, s->im);
    qemu_put_be32(f, s->istate);
    qemu_put_be32(f, s->afsel);
    qemu_put_be32(f, s->dr2r);
    qemu_put_be32(f, s->dr4r);
    qemu_put_be32(f, s->dr8r);
    qemu_put_be32(f, s->odr);
    qemu_put_be32(f, s->pur);
    qemu_put_be32(f, s->pdr);
    qemu_put_be32(f, s->slr);
    qemu_put_be32(f, s->den);
    qemu_put_be32(f, s->cr);
    qemu_put_be32(f, s->float_high);
}

static int pl061_load(QEMUFile *f, void *opaque, int version_id)
{
    pl061_state *s = (pl061_state *)opaque;
    if (version_id != 1)
        return -EINVAL;

    s->locked = qemu_get_be32(f);
    s->data = qemu_get_be32(f);
    s->old_data = qemu_get_be32(f);
    s->dir = qemu_get_be32(f);
    s->isense = qemu_get_be32(f);
    s->ibe = qemu_get_be32(f);
    s->iev = qemu_get_be32(f);
    s->im = qemu_get_be32(f);
    s->istate = qemu_get_be32(f);
    s->afsel = qemu_get_be32(f);
    s->dr2r = qemu_get_be32(f);
    s->dr4r = qemu_get_be32(f);
    s->dr8r = qemu_get_be32(f);
    s->odr = qemu_get_be32(f);
    s->pur = qemu_get_be32(f);
    s->pdr = qemu_get_be32(f);
    s->slr = qemu_get_be32(f);
    s->den = qemu_get_be32(f);
    s->cr = qemu_get_be32(f);
    s->float_high = qemu_get_be32(f);

    return 0;
}

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
    register_savevm(&dev->qdev, "pl061_gpio", -1, 1, pl061_save, pl061_load, s);
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

static void pl061_register_devices(void)
{
    sysbus_register_dev("pl061", sizeof(pl061_state),
                        pl061_init_arm);
    sysbus_register_dev("pl061_luminary", sizeof(pl061_state),
                        pl061_init_luminary);
}

device_init(pl061_register_devices)
