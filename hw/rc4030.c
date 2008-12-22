/*
 * QEMU JAZZ RC4030 chipset
 *
 * Copyright (c) 2007-2008 Hervé Poussineau
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
#include "mips.h"
#include "qemu-timer.h"

//#define DEBUG_RC4030

#ifdef DEBUG_RC4030
static const char* irq_names[] = { "parallel", "floppy", "sound", "video",
            "network", "scsi", "keyboard", "mouse", "serial0", "serial1" };
#endif

typedef struct rc4030State
{
    uint32_t config; /* 0x0000: RC4030 config register */
    uint32_t invalid_address_register; /* 0x0010: Invalid Address register */

    /* DMA */
    uint32_t dma_regs[8][4];
    uint32_t dma_tl_base; /* 0x0018: DMA transl. table base */
    uint32_t dma_tl_limit; /* 0x0020: DMA transl. table limit */

    /* cache */
    uint32_t remote_failed_address; /* 0x0038: Remote Failed Address */
    uint32_t memory_failed_address; /* 0x0040: Memory Failed Address */
    uint32_t cache_ptag; /* 0x0048: I/O Cache Physical Tag */
    uint32_t cache_ltag; /* 0x0050: I/O Cache Logical Tag */
    uint32_t cache_bmask; /* 0x0058: I/O Cache Byte Mask */
    uint32_t cache_bwin; /* 0x0060: I/O Cache Buffer Window */

    uint32_t offset208;
    uint32_t offset210;
    uint32_t nvram_protect; /* 0x0220: NV ram protect register */
    uint32_t offset238;
    uint32_t rem_speed[15];
    uint32_t imr_jazz; /* Local bus int enable mask */
    uint32_t isr_jazz; /* Local bus int source */

    /* timer */
    QEMUTimer *periodic_timer;
    uint32_t itr; /* Interval timer reload */

    uint32_t dummy32;
    qemu_irq timer_irq;
    qemu_irq jazz_bus_irq;
} rc4030State;

static void set_next_tick(rc4030State *s)
{
    qemu_irq_lower(s->timer_irq);
    uint32_t tm_hz;

    tm_hz = 1000 / (s->itr + 1);

    qemu_mod_timer(s->periodic_timer, qemu_get_clock(vm_clock) + ticks_per_sec / tm_hz);
}

/* called for accesses to rc4030 */
static uint32_t rc4030_readl(void *opaque, target_phys_addr_t addr)
{
    rc4030State *s = opaque;
    uint32_t val;

    addr &= 0x3fff;
    switch (addr & ~0x3) {
    /* Global config register */
    case 0x0000:
        val = s->config;
        break;
    /* Invalid Address register */
    case 0x0010:
        val = s->invalid_address_register;
        break;
    /* DMA transl. table base */
    case 0x0018:
        val = s->dma_tl_base;
        break;
    /* DMA transl. table limit */
    case 0x0020:
        val = s->dma_tl_limit;
        break;
    /* Remote Failed Address */
    case 0x0038:
        val = s->remote_failed_address;
        break;
    /* Memory Failed Address */
    case 0x0040:
        val = s->memory_failed_address;
        break;
    /* I/O Cache Byte Mask */
    case 0x0058:
        val = s->cache_bmask;
        /* HACK */
        if (s->cache_bmask == (uint32_t)-1)
            s->cache_bmask = 0;
        break;
    /* Remote Speed Registers */
    case 0x0070:
    case 0x0078:
    case 0x0080:
    case 0x0088:
    case 0x0090:
    case 0x0098:
    case 0x00a0:
    case 0x00a8:
    case 0x00b0:
    case 0x00b8:
    case 0x00c0:
    case 0x00c8:
    case 0x00d0:
    case 0x00d8:
    case 0x00e0:
        val = s->rem_speed[(addr - 0x0070) >> 3];
        break;
    /* DMA channel base address */
    case 0x0100:
    case 0x0108:
    case 0x0110:
    case 0x0118:
    case 0x0120:
    case 0x0128:
    case 0x0130:
    case 0x0138:
    case 0x0140:
    case 0x0148:
    case 0x0150:
    case 0x0158:
    case 0x0160:
    case 0x0168:
    case 0x0170:
    case 0x0178:
    case 0x0180:
    case 0x0188:
    case 0x0190:
    case 0x0198:
    case 0x01a0:
    case 0x01a8:
    case 0x01b0:
    case 0x01b8:
    case 0x01c0:
    case 0x01c8:
    case 0x01d0:
    case 0x01d8:
    case 0x01e0:
    case 0x1e8:
    case 0x01f0:
    case 0x01f8:
        {
            int entry = (addr - 0x0100) >> 5;
            int idx = (addr & 0x1f) >> 3;
            val = s->dma_regs[entry][idx];
        }
        break;
    /* Offset 0x0208 */
    case 0x0208:
        val = s->offset208;
        break;
    /* Offset 0x0210 */
    case 0x0210:
        val = s->offset210;
        break;
    /* NV ram protect register */
    case 0x0220:
        val = s->nvram_protect;
        break;
    /* Interval timer count */
    case 0x0230:
        val = s->dummy32;
        qemu_irq_lower(s->timer_irq);
        break;
    /* Offset 0x0238 */
    case 0x0238:
        val = s->offset238;
        break;
    default:
#ifdef DEBUG_RC4030
        printf("rc4030: invalid read [" TARGET_FMT_lx "]\n", addr);
#endif
        val = 0;
        break;
    }

#ifdef DEBUG_RC4030
    if ((addr & ~3) != 0x230)
        printf("rc4030: read 0x%02x at " TARGET_FMT_lx "\n", val, addr);
#endif

    return val;
}

static uint32_t rc4030_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v = rc4030_readl(opaque, addr & ~0x3);
    if (addr & 0x2)
        return v >> 16;
    else
        return v & 0xffff;
}

static uint32_t rc4030_readb(void *opaque, target_phys_addr_t addr)
{
    uint32_t v = rc4030_readl(opaque, addr & ~0x3);
    return (v >> (8 * (addr & 0x3))) & 0xff;
}

static void rc4030_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    rc4030State *s = opaque;
    addr &= 0x3fff;

#ifdef DEBUG_RC4030
    printf("rc4030: write 0x%02x at " TARGET_FMT_lx "\n", val, addr);
#endif

    switch (addr & ~0x3) {
    /* Global config register */
    case 0x0000:
        s->config = val;
        break;
    /* DMA transl. table base */
    case 0x0018:
        s->dma_tl_base = val;
        break;
    /* DMA transl. table limit */
    case 0x0020:
        s->dma_tl_limit = val;
        break;
    /* I/O Cache Physical Tag */
    case 0x0048:
        s->cache_ptag = val;
        break;
    /* I/O Cache Logical Tag */
    case 0x0050:
        s->cache_ltag = val;
        break;
    /* I/O Cache Byte Mask */
    case 0x0058:
        s->cache_bmask |= val; /* HACK */
        break;
    /* I/O Cache Buffer Window */
    case 0x0060:
        s->cache_bwin = val;
        /* HACK */
        if (s->cache_ltag == 0x80000001 && s->cache_bmask == 0xf0f0f0f) {
            target_phys_addr_t dests[] = { 4, 0, 8, 0x10 };
            static int current = 0;
            target_phys_addr_t dest = 0 + dests[current];
            uint8_t buf;
            current = (current + 1) % (ARRAY_SIZE(dests));
            buf = s->cache_bwin - 1;
            cpu_physical_memory_rw(dest, &buf, 1, 1);
        }
        break;
    /* Remote Speed Registers */
    case 0x0070:
    case 0x0078:
    case 0x0080:
    case 0x0088:
    case 0x0090:
    case 0x0098:
    case 0x00a0:
    case 0x00a8:
    case 0x00b0:
    case 0x00b8:
    case 0x00c0:
    case 0x00c8:
    case 0x00d0:
    case 0x00d8:
    case 0x00e0:
        s->rem_speed[(addr - 0x0070) >> 3] = val;
        break;
    /* DMA channel base address */
    case 0x0100:
    case 0x0108:
    case 0x0110:
    case 0x0118:
    case 0x0120:
    case 0x0128:
    case 0x0130:
    case 0x0138:
    case 0x0140:
    case 0x0148:
    case 0x0150:
    case 0x0158:
    case 0x0160:
    case 0x0168:
    case 0x0170:
    case 0x0178:
    case 0x0180:
    case 0x0188:
    case 0x0190:
    case 0x0198:
    case 0x01a0:
    case 0x01a8:
    case 0x01b0:
    case 0x01b8:
    case 0x01c0:
    case 0x01c8:
    case 0x01d0:
    case 0x01d8:
    case 0x01e0:
    case 0x1e8:
    case 0x01f0:
    case 0x01f8:
        {
            int entry = (addr - 0x0100) >> 5;
            int idx = (addr & 0x1f) >> 3;
            s->dma_regs[entry][idx] = val;
        }
        break;
    /* Offset 0x0210 */
    case 0x0210:
        s->offset210 = val;
        break;
    /* Interval timer reload */
    case 0x0228:
        s->itr = val;
        qemu_irq_lower(s->timer_irq);
        set_next_tick(s);
        break;
    default:
#ifdef DEBUG_RC4030
        printf("rc4030: invalid write of 0x%02x at [" TARGET_FMT_lx "]\n", val, addr);
#endif
        break;
    }
}

static void rc4030_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    uint32_t old_val = rc4030_readl(opaque, addr & ~0x3);

    if (addr & 0x2)
        val = (val << 16) | (old_val & 0x0000ffff);
    else
        val = val | (old_val & 0xffff0000);
    rc4030_writel(opaque, addr & ~0x3, val);
}

static void rc4030_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    uint32_t old_val = rc4030_readl(opaque, addr & ~0x3);

    switch (addr & 3) {
    case 0:
        val = val | (old_val & 0xffffff00);
        break;
    case 1:
        val = (val << 8) | (old_val & 0xffff00ff);
        break;
    case 2:
        val = (val << 16) | (old_val & 0xff00ffff);
        break;
    case 3:
        val = (val << 24) | (old_val & 0x00ffffff);
        break;
    }
    rc4030_writel(opaque, addr & ~0x3, val);
}

static CPUReadMemoryFunc *rc4030_read[3] = {
    rc4030_readb,
    rc4030_readw,
    rc4030_readl,
};

static CPUWriteMemoryFunc *rc4030_write[3] = {
    rc4030_writeb,
    rc4030_writew,
    rc4030_writel,
};

static void update_jazz_irq(rc4030State *s)
{
    uint16_t pending;

    pending = s->isr_jazz & s->imr_jazz;

#ifdef DEBUG_RC4030
    if (s->isr_jazz != 0) {
        uint32_t irq = 0;
        printf("jazz pending:");
        for (irq = 0; irq < ARRAY_SIZE(irq_names); irq++) {
            if (s->isr_jazz & (1 << irq)) {
                printf(" %s", irq_names[irq]);
                if (!(s->imr_jazz & (1 << irq))) {
                    printf("(ignored)");
                }
            }
        }
        printf("\n");
    }
#endif

    if (pending != 0)
        qemu_irq_raise(s->jazz_bus_irq);
    else
        qemu_irq_lower(s->jazz_bus_irq);
}

static void rc4030_irq_jazz_request(void *opaque, int irq, int level)
{
    rc4030State *s = opaque;

    if (level) {
        s->isr_jazz |= 1 << irq;
    } else {
        s->isr_jazz &= ~(1 << irq);
    }

    update_jazz_irq(s);
}

static void rc4030_periodic_timer(void *opaque)
{
    rc4030State *s = opaque;

    set_next_tick(s);
    qemu_irq_raise(s->timer_irq);
}

static uint32_t int_readb(void *opaque, target_phys_addr_t addr)
{
    rc4030State *s = opaque;
    uint32_t val;
    uint32_t irq;
    addr &= 0xfff;

    switch (addr) {
    case 0x00: {
        /* Local bus int source */
        uint32_t pending = s->isr_jazz & s->imr_jazz;
        val = 0;
        irq = 0;
        while (pending) {
            if (pending & 1) {
                //printf("returning irq %s\n", irq_names[irq]);
                val = (irq + 1) << 2;
                break;
            }
            irq++;
            pending >>= 1;
        }
        break;
    }
    default:
#ifdef DEBUG_RC4030
            printf("rc4030: (interrupt controller) invalid read [" TARGET_FMT_lx "]\n", addr);
#endif
            val = 0;
    }

#ifdef DEBUG_RC4030
    printf("rc4030: (interrupt controller) read 0x%02x at " TARGET_FMT_lx "\n", val, addr);
#endif

    return val;
}

static uint32_t int_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
    v = int_readb(opaque, addr);
    v |= int_readb(opaque, addr + 1) << 8;
    return v;
}

static uint32_t int_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
    v = int_readb(opaque, addr);
    v |= int_readb(opaque, addr + 1) << 8;
    v |= int_readb(opaque, addr + 2) << 16;
    v |= int_readb(opaque, addr + 3) << 24;
    return v;
}

static void int_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    rc4030State *s = opaque;
    addr &= 0xfff;

#ifdef DEBUG_RC4030
    printf("rc4030: (interrupt controller) write 0x%02x at " TARGET_FMT_lx "\n", val, addr);
#endif

    switch (addr) {
    /* Local bus int enable mask */
    case 0x02:
        s->imr_jazz = (s->imr_jazz & 0xff00) | (val << 0); update_jazz_irq(s);
        break;
    case 0x03:
        s->imr_jazz = (s->imr_jazz & 0x00ff) | (val << 8); update_jazz_irq(s);
        break;
    default:
#ifdef DEBUG_RC4030
        printf("rc4030: (interrupt controller) invalid write of 0x%02x at [" TARGET_FMT_lx "]\n", val, addr);
#endif
        break;
    }
}

static void int_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    int_writeb(opaque, addr, val & 0xff);
    int_writeb(opaque, addr + 1, (val >> 8) & 0xff);
}

static void int_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    int_writeb(opaque, addr, val & 0xff);
    int_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    int_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    int_writeb(opaque, addr + 3, (val >> 24) & 0xff);
}

static CPUReadMemoryFunc *int_read[3] = {
    int_readb,
    int_readw,
    int_readl,
};

static CPUWriteMemoryFunc *int_write[3] = {
    int_writeb,
    int_writew,
    int_writel,
};

#define G364_512KB_RAM (0x0)
#define G364_2MB_RAM   (0x1)
#define G364_8MB_RAM   (0x2)
#define G364_32MB_RAM  (0x3)

static void rc4030_reset(void *opaque)
{
    rc4030State *s = opaque;
    int i;

    s->config = (G364_2MB_RAM << 8) | 0x04;
    s->invalid_address_register = 0;

    memset(s->dma_regs, 0, sizeof(s->dma_regs));
    s->dma_tl_base = s->dma_tl_limit = 0;

    s->remote_failed_address = s->memory_failed_address = 0;
    s->cache_ptag = s->cache_ltag = 0;
    s->cache_bmask = s->cache_bwin = 0;

    s->offset208 = 0;
    s->offset210 = 0x18186;
    s->nvram_protect = 7;
    s->offset238 = 7;
    for (i = 0; i < 15; i++)
        s->rem_speed[i] = 7;
    s->imr_jazz = s->isr_jazz = 0;

    s->itr = 0;
    s->dummy32 = 0;

    qemu_irq_lower(s->timer_irq);
    qemu_irq_lower(s->jazz_bus_irq);
}

qemu_irq *rc4030_init(qemu_irq timer, qemu_irq jazz_bus)
{
    rc4030State *s;
    int s_chipset, s_int;

    s = qemu_mallocz(sizeof(rc4030State));
    if (!s)
        return NULL;

    s->periodic_timer = qemu_new_timer(vm_clock, rc4030_periodic_timer, s);
    s->timer_irq = timer;
    s->jazz_bus_irq = jazz_bus;

    qemu_register_reset(rc4030_reset, s);
    rc4030_reset(s);

    s_chipset = cpu_register_io_memory(0, rc4030_read, rc4030_write, s);
    cpu_register_physical_memory(0x80000000, 0x300, s_chipset);
    s_int = cpu_register_io_memory(0, int_read, int_write, s);
    cpu_register_physical_memory(0xf0000000, 0x00001000, s_int);

    return qemu_allocate_irqs(rc4030_irq_jazz_request, s, 16);
}
