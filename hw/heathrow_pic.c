/*
 * Heathrow PIC support (OldWorld PowerMac)
 *
 * Copyright (c) 2005-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
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
#include "hw.h"
#include "ppc_mac.h"

/* debug PIC */
//#define DEBUG_PIC

#ifdef DEBUG_PIC
#define PIC_DPRINTF(fmt, ...)                                   \
    do { printf("PIC: " fmt , ## __VA_ARGS__); } while (0)
#else
#define PIC_DPRINTF(fmt, ...)
#endif

typedef struct HeathrowPIC {
    uint32_t events;
    uint32_t mask;
    uint32_t levels;
    uint32_t level_triggered;
} HeathrowPIC;

typedef struct HeathrowPICS {
    MemoryRegion mem;
    HeathrowPIC pics[2];
    qemu_irq *irqs;
} HeathrowPICS;

static inline int check_irq(HeathrowPIC *pic)
{
    return (pic->events | (pic->levels & pic->level_triggered)) & pic->mask;
}

/* update the CPU irq state */
static void heathrow_pic_update(HeathrowPICS *s)
{
    if (check_irq(&s->pics[0]) || check_irq(&s->pics[1])) {
        qemu_irq_raise(s->irqs[0]);
    } else {
        qemu_irq_lower(s->irqs[0]);
    }
}

static void pic_write(void *opaque, target_phys_addr_t addr,
                      uint64_t value, unsigned size)
{
    HeathrowPICS *s = opaque;
    HeathrowPIC *pic;
    unsigned int n;

    n = ((addr & 0xfff) - 0x10) >> 4;
    PIC_DPRINTF("writel: " TARGET_FMT_plx " %u: %08x\n", addr, n, value);
    if (n >= 2)
        return;
    pic = &s->pics[n];
    switch(addr & 0xf) {
    case 0x04:
        pic->mask = value;
        heathrow_pic_update(s);
        break;
    case 0x08:
        /* do not reset level triggered IRQs */
        value &= ~pic->level_triggered;
        pic->events &= ~value;
        heathrow_pic_update(s);
        break;
    default:
        break;
    }
}

static uint64_t pic_read(void *opaque, target_phys_addr_t addr,
                         unsigned size)
{
    HeathrowPICS *s = opaque;
    HeathrowPIC *pic;
    unsigned int n;
    uint32_t value;

    n = ((addr & 0xfff) - 0x10) >> 4;
    if (n >= 2) {
        value = 0;
    } else {
        pic = &s->pics[n];
        switch(addr & 0xf) {
        case 0x0:
            value = pic->events;
            break;
        case 0x4:
            value = pic->mask;
            break;
        case 0xc:
            value = pic->levels;
            break;
        default:
            value = 0;
            break;
        }
    }
    PIC_DPRINTF("readl: " TARGET_FMT_plx " %u: %08x\n", addr, n, value);
    return value;
}

static const MemoryRegionOps heathrow_pic_ops = {
    .read = pic_read,
    .write = pic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void heathrow_pic_set_irq(void *opaque, int num, int level)
{
    HeathrowPICS *s = opaque;
    HeathrowPIC *pic;
    unsigned int irq_bit;

#if defined(DEBUG)
    {
        static int last_level[64];
        if (last_level[num] != level) {
            PIC_DPRINTF("set_irq: num=0x%02x level=%d\n", num, level);
            last_level[num] = level;
        }
    }
#endif
    pic = &s->pics[1 - (num >> 5)];
    irq_bit = 1 << (num & 0x1f);
    if (level) {
        pic->events |= irq_bit & ~pic->level_triggered;
        pic->levels |= irq_bit;
    } else {
        pic->levels &= ~irq_bit;
    }
    heathrow_pic_update(s);
}

static const VMStateDescription vmstate_heathrow_pic_one = {
    .name = "heathrow_pic_one",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(events, HeathrowPIC),
        VMSTATE_UINT32(mask, HeathrowPIC),
        VMSTATE_UINT32(levels, HeathrowPIC),
        VMSTATE_UINT32(level_triggered, HeathrowPIC),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_heathrow_pic = {
    .name = "heathrow_pic",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(pics, HeathrowPICS, 2, 1,
                             vmstate_heathrow_pic_one, HeathrowPIC),
        VMSTATE_END_OF_LIST()
    }
};

static void heathrow_pic_reset_one(HeathrowPIC *s)
{
    memset(s, '\0', sizeof(HeathrowPIC));
}

static void heathrow_pic_reset(void *opaque)
{
    HeathrowPICS *s = opaque;

    heathrow_pic_reset_one(&s->pics[0]);
    heathrow_pic_reset_one(&s->pics[1]);

    s->pics[0].level_triggered = 0;
    s->pics[1].level_triggered = 0x1ff00000;
}

qemu_irq *heathrow_pic_init(MemoryRegion **pmem,
                            int nb_cpus, qemu_irq **irqs)
{
    HeathrowPICS *s;

    s = g_malloc0(sizeof(HeathrowPICS));
    /* only 1 CPU */
    s->irqs = irqs[0];
    memory_region_init_io(&s->mem, &heathrow_pic_ops, s,
                          "heathrow-pic", 0x1000);
    *pmem = &s->mem;

    vmstate_register(NULL, -1, &vmstate_heathrow_pic, s);
    qemu_register_reset(heathrow_pic_reset, s);
    return qemu_allocate_irqs(heathrow_pic_set_irq, s, 64);
}
