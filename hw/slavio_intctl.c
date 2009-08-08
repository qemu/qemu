/*
 * QEMU Sparc SLAVIO interrupt controller emulation
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
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

#include "sun4m.h"
#include "monitor.h"
#include "sysbus.h"

//#define DEBUG_IRQ_COUNT
//#define DEBUG_IRQ

#ifdef DEBUG_IRQ
#define DPRINTF(fmt, ...)                                       \
    do { printf("IRQ: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
#endif

/*
 * Registers of interrupt controller in sun4m.
 *
 * This is the interrupt controller part of chip STP2001 (Slave I/O), also
 * produced as NCR89C105. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C105.txt
 *
 * There is a system master controller and one for each cpu.
 *
 */

#define MAX_CPUS 16
#define MAX_PILS 16

struct SLAVIO_INTCTLState;

typedef struct SLAVIO_CPUINTCTLState {
    uint32_t intreg_pending;
    struct SLAVIO_INTCTLState *master;
    uint32_t cpu;
} SLAVIO_CPUINTCTLState;

typedef struct SLAVIO_INTCTLState {
    SysBusDevice busdev;
    uint32_t intregm_pending;
    uint32_t intregm_disabled;
    uint32_t target_cpu;
#ifdef DEBUG_IRQ_COUNT
    uint64_t irq_count[32];
#endif
    qemu_irq cpu_irqs[MAX_CPUS][MAX_PILS];
    const uint32_t *intbit_to_level;
    uint32_t cputimer_lbit, cputimer_mbit;
    uint32_t cputimer_bit;
    uint32_t pil_out[MAX_CPUS];
    SLAVIO_CPUINTCTLState slaves[MAX_CPUS];
} SLAVIO_INTCTLState;

#define INTCTL_MAXADDR 0xf
#define INTCTL_SIZE (INTCTL_MAXADDR + 1)
#define INTCTLM_SIZE 0x14
#define MASTER_IRQ_MASK ~0x0fa2007f
#define MASTER_DISABLE 0x80000000
#define CPU_SOFTIRQ_MASK 0xfffe0000
#define CPU_IRQ_INT15_IN 0x0004000
#define CPU_IRQ_INT15_MASK 0x80000000

static void slavio_check_interrupts(SLAVIO_INTCTLState *s, int set_irqs);

// per-cpu interrupt controller
static uint32_t slavio_intctl_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SLAVIO_CPUINTCTLState *s = opaque;
    uint32_t saddr, ret;

    saddr = addr >> 2;
    switch (saddr) {
    case 0:
        ret = s->intreg_pending;
        break;
    default:
        ret = 0;
        break;
    }
    DPRINTF("read cpu %d reg 0x" TARGET_FMT_plx " = %x\n", s->cpu, addr, ret);

    return ret;
}

static void slavio_intctl_mem_writel(void *opaque, target_phys_addr_t addr,
                                     uint32_t val)
{
    SLAVIO_CPUINTCTLState *s = opaque;
    uint32_t saddr;

    saddr = addr >> 2;
    DPRINTF("write cpu %d reg 0x" TARGET_FMT_plx " = %x\n", s->cpu, addr, val);
    switch (saddr) {
    case 1: // clear pending softints
        if (val & CPU_IRQ_INT15_IN)
            val |= CPU_IRQ_INT15_MASK;
        val &= CPU_SOFTIRQ_MASK;
        s->intreg_pending &= ~val;
        slavio_check_interrupts(s->master, 1);
        DPRINTF("Cleared cpu %d irq mask %x, curmask %x\n", s->cpu, val,
                s->intreg_pending);
        break;
    case 2: // set softint
        val &= CPU_SOFTIRQ_MASK;
        s->intreg_pending |= val;
        slavio_check_interrupts(s->master, 1);
        DPRINTF("Set cpu %d irq mask %x, curmask %x\n", s->cpu, val,
                s->intreg_pending);
        break;
    default:
        break;
    }
}

static CPUReadMemoryFunc *slavio_intctl_mem_read[3] = {
    NULL,
    NULL,
    slavio_intctl_mem_readl,
};

static CPUWriteMemoryFunc *slavio_intctl_mem_write[3] = {
    NULL,
    NULL,
    slavio_intctl_mem_writel,
};

// master system interrupt controller
static uint32_t slavio_intctlm_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SLAVIO_INTCTLState *s = opaque;
    uint32_t saddr, ret;

    saddr = addr >> 2;
    switch (saddr) {
    case 0:
        ret = s->intregm_pending & ~MASTER_DISABLE;
        break;
    case 1:
        ret = s->intregm_disabled & MASTER_IRQ_MASK;
        break;
    case 4:
        ret = s->target_cpu;
        break;
    default:
        ret = 0;
        break;
    }
    DPRINTF("read system reg 0x" TARGET_FMT_plx " = %x\n", addr, ret);

    return ret;
}

static void slavio_intctlm_mem_writel(void *opaque, target_phys_addr_t addr,
                                      uint32_t val)
{
    SLAVIO_INTCTLState *s = opaque;
    uint32_t saddr;

    saddr = addr >> 2;
    DPRINTF("write system reg 0x" TARGET_FMT_plx " = %x\n", addr, val);
    switch (saddr) {
    case 2: // clear (enable)
        // Force clear unused bits
        val &= MASTER_IRQ_MASK;
        s->intregm_disabled &= ~val;
        DPRINTF("Enabled master irq mask %x, curmask %x\n", val,
                s->intregm_disabled);
        slavio_check_interrupts(s, 1);
        break;
    case 3: // set (disable, clear pending)
        // Force clear unused bits
        val &= MASTER_IRQ_MASK;
        s->intregm_disabled |= val;
        s->intregm_pending &= ~val;
        slavio_check_interrupts(s, 1);
        DPRINTF("Disabled master irq mask %x, curmask %x\n", val,
                s->intregm_disabled);
        break;
    case 4:
        s->target_cpu = val & (MAX_CPUS - 1);
        slavio_check_interrupts(s, 1);
        DPRINTF("Set master irq cpu %d\n", s->target_cpu);
        break;
    default:
        break;
    }
}

static CPUReadMemoryFunc *slavio_intctlm_mem_read[3] = {
    NULL,
    NULL,
    slavio_intctlm_mem_readl,
};

static CPUWriteMemoryFunc *slavio_intctlm_mem_write[3] = {
    NULL,
    NULL,
    slavio_intctlm_mem_writel,
};

void slavio_pic_info(Monitor *mon, void *opaque)
{
    SLAVIO_INTCTLState *s = opaque;
    int i;

    for (i = 0; i < MAX_CPUS; i++) {
        monitor_printf(mon, "per-cpu %d: pending 0x%08x\n", i,
                       s->slaves[i].intreg_pending);
    }
    monitor_printf(mon, "master: pending 0x%08x, disabled 0x%08x\n",
                   s->intregm_pending, s->intregm_disabled);
}

void slavio_irq_info(Monitor *mon, void *opaque)
{
#ifndef DEBUG_IRQ_COUNT
    monitor_printf(mon, "irq statistic code not compiled.\n");
#else
    SLAVIO_INTCTLState *s = opaque;
    int i;
    int64_t count;

    monitor_printf(mon, "IRQ statistics:\n");
    for (i = 0; i < 32; i++) {
        count = s->irq_count[i];
        if (count > 0)
            monitor_printf(mon, "%2d: %" PRId64 "\n", i, count);
    }
#endif
}

static void slavio_check_interrupts(SLAVIO_INTCTLState *s, int set_irqs)
{
    uint32_t pending = s->intregm_pending, pil_pending;
    unsigned int i, j;

    pending &= ~s->intregm_disabled;

    DPRINTF("pending %x disabled %x\n", pending, s->intregm_disabled);
    for (i = 0; i < MAX_CPUS; i++) {
        pil_pending = 0;
        if (pending && !(s->intregm_disabled & MASTER_DISABLE) &&
            (i == s->target_cpu)) {
            for (j = 0; j < 32; j++) {
                if (pending & (1 << j))
                    pil_pending |= 1 << s->intbit_to_level[j];
            }
        }
        pil_pending |= (s->slaves[i].intreg_pending & CPU_SOFTIRQ_MASK) >> 16;

        if (set_irqs) {
            for (j = 0; j < MAX_PILS; j++) {
                if (pil_pending & (1 << j)) {
                    if (!(s->pil_out[i] & (1 << j))) {
                        qemu_irq_raise(s->cpu_irqs[i][j]);
                    }
                } else {
                    if (s->pil_out[i] & (1 << j)) {
                        qemu_irq_lower(s->cpu_irqs[i][j]);
                    }
                }
            }
        }
        s->pil_out[i] = pil_pending;
    }
}

/*
 * "irq" here is the bit number in the system interrupt register to
 * separate serial and keyboard interrupts sharing a level.
 */
static void slavio_set_irq(void *opaque, int irq, int level)
{
    SLAVIO_INTCTLState *s = opaque;
    uint32_t mask = 1 << irq;
    uint32_t pil = s->intbit_to_level[irq];

    DPRINTF("Set cpu %d irq %d -> pil %d level %d\n", s->target_cpu, irq, pil,
            level);
    if (pil > 0) {
        if (level) {
#ifdef DEBUG_IRQ_COUNT
            s->irq_count[pil]++;
#endif
            s->intregm_pending |= mask;
            s->slaves[s->target_cpu].intreg_pending |= 1 << pil;
        } else {
            s->intregm_pending &= ~mask;
            s->slaves[s->target_cpu].intreg_pending &= ~(1 << pil);
        }
        slavio_check_interrupts(s, 1);
    }
}

static void slavio_set_timer_irq_cpu(void *opaque, int cpu, int level)
{
    SLAVIO_INTCTLState *s = opaque;

    DPRINTF("Set cpu %d local timer level %d\n", cpu, level);

    if (level) {
        s->intregm_pending |= s->cputimer_mbit;
        s->slaves[cpu].intreg_pending |= s->cputimer_lbit;
    } else {
        s->intregm_pending &= ~s->cputimer_mbit;
        s->slaves[cpu].intreg_pending &= ~s->cputimer_lbit;
    }

    slavio_check_interrupts(s, 1);
}

static void slavio_set_irq_all(void *opaque, int irq, int level)
{
    if (irq < 32) {
        slavio_set_irq(opaque, irq, level);
    } else {
        slavio_set_timer_irq_cpu(opaque, irq - 32, level);
    }
}

static void slavio_intctl_save(QEMUFile *f, void *opaque)
{
    SLAVIO_INTCTLState *s = opaque;
    int i;

    for (i = 0; i < MAX_CPUS; i++) {
        qemu_put_be32s(f, &s->slaves[i].intreg_pending);
    }
    qemu_put_be32s(f, &s->intregm_pending);
    qemu_put_be32s(f, &s->intregm_disabled);
    qemu_put_be32s(f, &s->target_cpu);
}

static int slavio_intctl_load(QEMUFile *f, void *opaque, int version_id)
{
    SLAVIO_INTCTLState *s = opaque;
    int i;

    if (version_id != 1)
        return -EINVAL;

    for (i = 0; i < MAX_CPUS; i++) {
        qemu_get_be32s(f, &s->slaves[i].intreg_pending);
    }
    qemu_get_be32s(f, &s->intregm_pending);
    qemu_get_be32s(f, &s->intregm_disabled);
    qemu_get_be32s(f, &s->target_cpu);
    slavio_check_interrupts(s, 0);
    return 0;
}

static void slavio_intctl_reset(void *opaque)
{
    SLAVIO_INTCTLState *s = opaque;
    int i;

    for (i = 0; i < MAX_CPUS; i++) {
        s->slaves[i].intreg_pending = 0;
    }
    s->intregm_disabled = ~MASTER_IRQ_MASK;
    s->intregm_pending = 0;
    s->target_cpu = 0;
    slavio_check_interrupts(s, 0);
}

static void slavio_intctl_init1(SysBusDevice *dev)
{
    SLAVIO_INTCTLState *s = FROM_SYSBUS(SLAVIO_INTCTLState, dev);
    int io_memory;
    unsigned int i, j;

    qdev_init_gpio_in(&dev->qdev, slavio_set_irq_all, 32 + MAX_CPUS);
    io_memory = cpu_register_io_memory(slavio_intctlm_mem_read,
                                       slavio_intctlm_mem_write, s);
    sysbus_init_mmio(dev, INTCTLM_SIZE, io_memory);
    s->cputimer_mbit = 1 << s->cputimer_bit;
    s->cputimer_lbit = 1 << s->intbit_to_level[s->cputimer_bit];

    for (i = 0; i < MAX_CPUS; i++) {
        for (j = 0; j < MAX_PILS; j++) {
            sysbus_init_irq(dev, &s->cpu_irqs[i][j]);
        }
        io_memory = cpu_register_io_memory(slavio_intctl_mem_read,
                                           slavio_intctl_mem_write,
                                           &s->slaves[i]);
        sysbus_init_mmio(dev, INTCTL_SIZE, io_memory);
        s->slaves[i].cpu = i;
        s->slaves[i].master = s;
    }
    register_savevm("slavio_intctl", -1, 1, slavio_intctl_save,
                    slavio_intctl_load, s);
    qemu_register_reset(slavio_intctl_reset, s);
    slavio_intctl_reset(s);
}

static SysBusDeviceInfo slavio_intctl_info = {
    .init = slavio_intctl_init1,
    .qdev.name  = "slavio_intctl",
    .qdev.size  = sizeof(SLAVIO_INTCTLState),
    .qdev.props = (Property[]) {
        {
            .name = "intbit_to_level",
            .info = &qdev_prop_ptr,
            .offset = offsetof(SLAVIO_INTCTLState, intbit_to_level),
        },
        {
            .name = "cputimer_bit",
            .info = &qdev_prop_uint32,
            .offset = offsetof(SLAVIO_INTCTLState, cputimer_bit),
        },
        {/* end of property list */}
    }
};

static void slavio_intctl_register_devices(void)
{
    sysbus_register_withprop(&slavio_intctl_info);
}

device_init(slavio_intctl_register_devices)
