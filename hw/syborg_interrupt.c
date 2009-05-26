/*
 * Syborg interrupt controller.
 *
 * Copyright (c) 2008 CodeSourcery
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "sysbus.h"
#include "syborg.h"

//#define DEBUG_SYBORG_INT

#ifdef DEBUG_SYBORG_INT
#define DPRINTF(fmt, ...) \
do { printf("syborg_int: " fmt , ## __VA_ARGS__); } while (0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "syborg_int: error: " fmt , ## __VA_ARGS__); \
    exit(1);} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "syborg_int: error: " fmt , ## __VA_ARGS__);} while (0)
#endif
enum {
    INT_ID            = 0,
    INT_STATUS        = 1, /* number of pending interrupts */
    INT_CURRENT       = 2, /* next interrupt to be serviced */
    INT_DISABLE_ALL   = 3,
    INT_DISABLE       = 4,
    INT_ENABLE        = 5,
    INT_TOTAL         = 6
};

typedef struct {
    unsigned level:1;
    unsigned enabled:1;
} syborg_int_flags;

typedef struct {
    SysBusDevice busdev;
    int pending_count;
    int num_irqs;
    syborg_int_flags *flags;
    qemu_irq parent_irq;
} SyborgIntState;

static void syborg_int_update(SyborgIntState *s)
{
    DPRINTF("pending %d\n", s->pending_count);
    qemu_set_irq(s->parent_irq, s->pending_count > 0);
}

static void syborg_int_set_irq(void *opaque, int irq, int level)
{
    SyborgIntState *s = (SyborgIntState *)opaque;

    if (s->flags[irq].level == level)
        return;

    s->flags[irq].level = level;
    if (s->flags[irq].enabled) {
        if (level)
            s->pending_count++;
        else
            s->pending_count--;
        syborg_int_update(s);
    }
}

static uint32_t syborg_int_read(void *opaque, target_phys_addr_t offset)
{
    SyborgIntState *s = (SyborgIntState *)opaque;
    int i;

    offset &= 0xfff;
    switch (offset >> 2) {
    case INT_ID:
        return SYBORG_ID_INT;
    case INT_STATUS:
        DPRINTF("read status=%d\n", s->pending_count);
        return s->pending_count;

    case INT_CURRENT:
        for (i = 0; i < s->num_irqs; i++) {
            if (s->flags[i].level & s->flags[i].enabled) {
                DPRINTF("read current=%d\n", i);
                return i;
            }
        }
        DPRINTF("read current=none\n");
        return 0xffffffffu;

    default:
        cpu_abort(cpu_single_env, "syborg_int_read: Bad offset %x\n",
                  (int)offset);
        return 0;
    }
}

static void syborg_int_write(void *opaque, target_phys_addr_t offset, uint32_t value)
{
    SyborgIntState *s = (SyborgIntState *)opaque;
    int i;
    offset &= 0xfff;

    DPRINTF("syborg_int_write offset=%d val=%d\n", (int)offset, (int)value);
    switch (offset >> 2) {
    case INT_DISABLE_ALL:
        s->pending_count = 0;
        for (i = 0; i < s->num_irqs; i++)
            s->flags[i].enabled = 0;
        break;

    case INT_DISABLE:
        if (value >= s->num_irqs)
            break;
        if (s->flags[value].enabled) {
            if (s->flags[value].enabled)
                s->pending_count--;
            s->flags[value].enabled = 0;
        }
        break;

    case INT_ENABLE:
      if (value >= s->num_irqs)
          break;
      if (!(s->flags[value].enabled)) {
          if(s->flags[value].level)
              s->pending_count++;
          s->flags[value].enabled = 1;
      }
      break;

    default:
        cpu_abort(cpu_single_env, "syborg_int_write: Bad offset %x\n",
                  (int)offset);
        return;
    }
    syborg_int_update(s);
}

static CPUReadMemoryFunc *syborg_int_readfn[] = {
    syborg_int_read,
    syborg_int_read,
    syborg_int_read
};

static CPUWriteMemoryFunc *syborg_int_writefn[] = {
    syborg_int_write,
    syborg_int_write,
    syborg_int_write
};

static void syborg_int_save(QEMUFile *f, void *opaque)
{
    SyborgIntState *s = (SyborgIntState *)opaque;
    int i;

    qemu_put_be32(f, s->num_irqs);
    qemu_put_be32(f, s->pending_count);
    for (i = 0; i < s->num_irqs; i++) {
        qemu_put_be32(f, s->flags[i].enabled
                         | ((unsigned)s->flags[i].level << 1));
    }
}

static int syborg_int_load(QEMUFile *f, void *opaque, int version_id)
{
    SyborgIntState *s = (SyborgIntState *)opaque;
    uint32_t val;
    int i;

    if (version_id != 1)
        return -EINVAL;

    val = qemu_get_be32(f);
    if (val != s->num_irqs)
        return -EINVAL;
    s->pending_count = qemu_get_be32(f);
    for (i = 0; i < s->num_irqs; i++) {
        val = qemu_get_be32(f);
        s->flags[i].enabled = val & 1;
        s->flags[i].level = (val >> 1) & 1;
    }
    return 0;
}

static void syborg_int_init(SysBusDevice *dev)
{
    SyborgIntState *s = FROM_SYSBUS(SyborgIntState, dev);
    int iomemtype;

    sysbus_init_irq(dev, &s->parent_irq);
    s->num_irqs = qdev_get_prop_int(&dev->qdev, "num-interrupts", 64);
    qdev_init_gpio_in(&dev->qdev, syborg_int_set_irq, s->num_irqs);
    iomemtype = cpu_register_io_memory(0, syborg_int_readfn,
                                       syborg_int_writefn, s);
    sysbus_init_mmio(dev, 0x1000, iomemtype);
    s->flags = qemu_mallocz(s->num_irqs * sizeof(syborg_int_flags));

    register_savevm("syborg_int", -1, 1, syborg_int_save, syborg_int_load, s);
}

static void syborg_interrupt_register_devices(void)
{
    sysbus_register_dev("syborg,interrupt", sizeof(SyborgIntState),
                        syborg_int_init);
}

device_init(syborg_interrupt_register_devices)
