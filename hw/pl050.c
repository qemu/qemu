/*
 * Arm PrimeCell PL050 Keyboard / Mouse Interface
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "sysbus.h"
#include "ps2.h"

typedef struct {
    SysBusDevice busdev;
    void *dev;
    uint32_t cr;
    uint32_t clk;
    uint32_t last;
    int pending;
    qemu_irq irq;
    int is_mouse;
} pl050_state;

#define PL050_TXEMPTY         (1 << 6)
#define PL050_TXBUSY          (1 << 5)
#define PL050_RXFULL          (1 << 4)
#define PL050_RXBUSY          (1 << 3)
#define PL050_RXPARITY        (1 << 2)
#define PL050_KMIC            (1 << 1)
#define PL050_KMID            (1 << 0)

static const unsigned char pl050_id[] =
{ 0x50, 0x10, 0x04, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };

static void pl050_update(void *opaque, int level)
{
    pl050_state *s = (pl050_state *)opaque;
    int raise;

    s->pending = level;
    raise = (s->pending && (s->cr & 0x10) != 0)
            || (s->cr & 0x08) != 0;
    qemu_set_irq(s->irq, raise);
}

static uint32_t pl050_read(void *opaque, target_phys_addr_t offset)
{
    pl050_state *s = (pl050_state *)opaque;
    if (offset >= 0xfe0 && offset < 0x1000)
        return pl050_id[(offset - 0xfe0) >> 2];

    switch (offset >> 2) {
    case 0: /* KMICR */
        return s->cr;
    case 1: /* KMISTAT */
        {
            uint8_t val;
            uint32_t stat;

            val = s->last;
            val = val ^ (val >> 4);
            val = val ^ (val >> 2);
            val = (val ^ (val >> 1)) & 1;

            stat = PL050_TXEMPTY;
            if (val)
                stat |= PL050_RXPARITY;
            if (s->pending)
                stat |= PL050_RXFULL;

            return stat;
        }
    case 2: /* KMIDATA */
        if (s->pending)
            s->last = ps2_read_data(s->dev);
        return s->last;
    case 3: /* KMICLKDIV */
        return s->clk;
    case 4: /* KMIIR */
        return s->pending | 2;
    default:
        hw_error("pl050_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void pl050_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    pl050_state *s = (pl050_state *)opaque;
    switch (offset >> 2) {
    case 0: /* KMICR */
        s->cr = value;
        pl050_update(s, s->pending);
        /* ??? Need to implement the enable/disable bit.  */
        break;
    case 2: /* KMIDATA */
        /* ??? This should toggle the TX interrupt line.  */
        /* ??? This means kbd/mouse can block each other.  */
        if (s->is_mouse) {
            ps2_write_mouse(s->dev, value);
        } else {
            ps2_write_keyboard(s->dev, value);
        }
        break;
    case 3: /* KMICLKDIV */
        s->clk = value;
        return;
    default:
        hw_error("pl050_write: Bad offset %x\n", (int)offset);
    }
}
static CPUReadMemoryFunc *pl050_readfn[] = {
   pl050_read,
   pl050_read,
   pl050_read
};

static CPUWriteMemoryFunc *pl050_writefn[] = {
   pl050_write,
   pl050_write,
   pl050_write
};

static void pl050_init(SysBusDevice *dev, int is_mouse)
{
    pl050_state *s = FROM_SYSBUS(pl050_state, dev);
    int iomemtype;

    iomemtype = cpu_register_io_memory(0, pl050_readfn,
                                       pl050_writefn, s);
    sysbus_init_mmio(dev, 0x1000, iomemtype);
    sysbus_init_irq(dev, &s->irq);
    s->is_mouse = is_mouse;
    if (s->is_mouse)
        s->dev = ps2_mouse_init(pl050_update, s);
    else
        s->dev = ps2_kbd_init(pl050_update, s);
    /* ??? Save/restore.  */
}

static void pl050_init_keyboard(SysBusDevice *dev)
{
    pl050_init(dev, 0);
}

static void pl050_init_mouse(SysBusDevice *dev)
{
    pl050_init(dev, 1);
}

static void pl050_register_devices(void)
{
    sysbus_register_dev("pl050_keyboard", sizeof(pl050_state),
                        pl050_init_keyboard);
    sysbus_register_dev("pl050_mouse", sizeof(pl050_state),
                        pl050_init_mouse);
}

device_init(pl050_register_devices)
