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

//#define DEBUG

typedef struct HeathrowPIC {
    uint32_t events;
    uint32_t mask;
    uint32_t levels;
    uint32_t level_triggered;
} HeathrowPIC;

typedef struct HeathrowPICS {
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

static void pic_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    HeathrowPICS *s = opaque;
    HeathrowPIC *pic;
    unsigned int n;

#ifdef TARGET_WORDS_BIGENDIAN
    value = bswap32(value);
#endif
    n = ((addr & 0xfff) - 0x10) >> 4;
#ifdef DEBUG
    printf("pic_writel: " PADDRX " %u: %08x\n", addr, n, value);
#endif
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

static uint32_t pic_readl (void *opaque, target_phys_addr_t addr)
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
#ifdef DEBUG
    printf("pic_readl: " PADDRX " %u: %08x\n", addr, n, value);
#endif
#ifdef TARGET_WORDS_BIGENDIAN
    value = bswap32(value);
#endif
    return value;
}

static CPUWriteMemoryFunc *pic_write[] = {
    &pic_writel,
    &pic_writel,
    &pic_writel,
};

static CPUReadMemoryFunc *pic_read[] = {
    &pic_readl,
    &pic_readl,
    &pic_readl,
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
            printf("set_irq: num=0x%02x level=%d\n", num, level);
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

qemu_irq *heathrow_pic_init(int *pmem_index,
                            int nb_cpus, qemu_irq **irqs)
{
    HeathrowPICS *s;

    s = qemu_mallocz(sizeof(HeathrowPICS));
    s->pics[0].level_triggered = 0;
    s->pics[1].level_triggered = 0x1ff00000;
    /* only 1 CPU */
    s->irqs = irqs[0];
    *pmem_index = cpu_register_io_memory(0, pic_read, pic_write, s);

    return qemu_allocate_irqs(heathrow_pic_set_irq, s, 64);
}
