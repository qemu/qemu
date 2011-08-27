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
#include "trace.h"

//#define DEBUG_IRQ_COUNT

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
    struct SLAVIO_INTCTLState *master;
    uint32_t intreg_pending;
    uint32_t cpu;
    uint32_t irl_out;
} SLAVIO_CPUINTCTLState;

typedef struct SLAVIO_INTCTLState {
    SysBusDevice busdev;
#ifdef DEBUG_IRQ_COUNT
    uint64_t irq_count[32];
#endif
    qemu_irq cpu_irqs[MAX_CPUS][MAX_PILS];
    SLAVIO_CPUINTCTLState slaves[MAX_CPUS];
    uint32_t intregm_pending;
    uint32_t intregm_disabled;
    uint32_t target_cpu;
} SLAVIO_INTCTLState;

#define INTCTL_MAXADDR 0xf
#define INTCTL_SIZE (INTCTL_MAXADDR + 1)
#define INTCTLM_SIZE 0x14
#define MASTER_IRQ_MASK ~0x0fa2007f
#define MASTER_DISABLE 0x80000000
#define CPU_SOFTIRQ_MASK 0xfffe0000
#define CPU_IRQ_INT15_IN (1 << 15)
#define CPU_IRQ_TIMER_IN (1 << 14)

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
    trace_slavio_intctl_mem_readl(s->cpu, addr, ret);

    return ret;
}

static void slavio_intctl_mem_writel(void *opaque, target_phys_addr_t addr,
                                     uint32_t val)
{
    SLAVIO_CPUINTCTLState *s = opaque;
    uint32_t saddr;

    saddr = addr >> 2;
    trace_slavio_intctl_mem_writel(s->cpu, addr, val);
    switch (saddr) {
    case 1: // clear pending softints
        val &= CPU_SOFTIRQ_MASK | CPU_IRQ_INT15_IN;
        s->intreg_pending &= ~val;
        slavio_check_interrupts(s->master, 1);
        trace_slavio_intctl_mem_writel_clear(s->cpu, val, s->intreg_pending);
        break;
    case 2: // set softint
        val &= CPU_SOFTIRQ_MASK;
        s->intreg_pending |= val;
        slavio_check_interrupts(s->master, 1);
        trace_slavio_intctl_mem_writel_set(s->cpu, val, s->intreg_pending);
        break;
    default:
        break;
    }
}

static CPUReadMemoryFunc * const slavio_intctl_mem_read[3] = {
    NULL,
    NULL,
    slavio_intctl_mem_readl,
};

static CPUWriteMemoryFunc * const slavio_intctl_mem_write[3] = {
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
    trace_slavio_intctlm_mem_readl(addr, ret);

    return ret;
}

static void slavio_intctlm_mem_writel(void *opaque, target_phys_addr_t addr,
                                      uint32_t val)
{
    SLAVIO_INTCTLState *s = opaque;
    uint32_t saddr;

    saddr = addr >> 2;
    trace_slavio_intctlm_mem_writel(addr, val);
    switch (saddr) {
    case 2: // clear (enable)
        // Force clear unused bits
        val &= MASTER_IRQ_MASK;
        s->intregm_disabled &= ~val;
        trace_slavio_intctlm_mem_writel_enable(val, s->intregm_disabled);
        slavio_check_interrupts(s, 1);
        break;
    case 3: // set (disable; doesn't affect pending)
        // Force clear unused bits
        val &= MASTER_IRQ_MASK;
        s->intregm_disabled |= val;
        slavio_check_interrupts(s, 1);
        trace_slavio_intctlm_mem_writel_disable(val, s->intregm_disabled);
        break;
    case 4:
        s->target_cpu = val & (MAX_CPUS - 1);
        slavio_check_interrupts(s, 1);
        trace_slavio_intctlm_mem_writel_target(s->target_cpu);
        break;
    default:
        break;
    }
}

static CPUReadMemoryFunc * const slavio_intctlm_mem_read[3] = {
    NULL,
    NULL,
    slavio_intctlm_mem_readl,
};

static CPUWriteMemoryFunc * const slavio_intctlm_mem_write[3] = {
    NULL,
    NULL,
    slavio_intctlm_mem_writel,
};

void slavio_pic_info(Monitor *mon, DeviceState *dev)
{
    SysBusDevice *sd;
    SLAVIO_INTCTLState *s;
    int i;

    sd = sysbus_from_qdev(dev);
    s = FROM_SYSBUS(SLAVIO_INTCTLState, sd);
    for (i = 0; i < MAX_CPUS; i++) {
        monitor_printf(mon, "per-cpu %d: pending 0x%08x\n", i,
                       s->slaves[i].intreg_pending);
    }
    monitor_printf(mon, "master: pending 0x%08x, disabled 0x%08x\n",
                   s->intregm_pending, s->intregm_disabled);
}

void slavio_irq_info(Monitor *mon, DeviceState *dev)
{
#ifndef DEBUG_IRQ_COUNT
    monitor_printf(mon, "irq statistic code not compiled.\n");
#else
    SysBusDevice *sd;
    SLAVIO_INTCTLState *s;
    int i;
    int64_t count;

    sd = sysbus_from_qdev(dev);
    s = FROM_SYSBUS(SLAVIO_INTCTLState, sd);
    monitor_printf(mon, "IRQ statistics:\n");
    for (i = 0; i < 32; i++) {
        count = s->irq_count[i];
        if (count > 0)
            monitor_printf(mon, "%2d: %" PRId64 "\n", i, count);
    }
#endif
}

static const uint32_t intbit_to_level[] = {
    2, 3, 5, 7, 9, 11, 13, 2,   3, 5, 7, 9, 11, 13, 12, 12,
    6, 13, 4, 10, 8, 9, 11, 0,  0, 0, 0, 15, 15, 15, 15, 0,
};

static void slavio_check_interrupts(SLAVIO_INTCTLState *s, int set_irqs)
{
    uint32_t pending = s->intregm_pending, pil_pending;
    unsigned int i, j;

    pending &= ~s->intregm_disabled;

    trace_slavio_check_interrupts(pending, s->intregm_disabled);
    for (i = 0; i < MAX_CPUS; i++) {
        pil_pending = 0;

        /* If we are the current interrupt target, get hard interrupts */
        if (pending && !(s->intregm_disabled & MASTER_DISABLE) &&
            (i == s->target_cpu)) {
            for (j = 0; j < 32; j++) {
                if ((pending & (1 << j)) && intbit_to_level[j]) {
                    pil_pending |= 1 << intbit_to_level[j];
                }
            }
        }

        /* Calculate current pending hard interrupts for display */
        s->slaves[i].intreg_pending &= CPU_SOFTIRQ_MASK | CPU_IRQ_INT15_IN |
            CPU_IRQ_TIMER_IN;
        if (i == s->target_cpu) {
            for (j = 0; j < 32; j++) {
                if ((s->intregm_pending & (1 << j)) && intbit_to_level[j]) {
                    s->slaves[i].intreg_pending |= 1 << intbit_to_level[j];
                }
            }
        }

        /* Level 15 and CPU timer interrupts are only masked when
           the MASTER_DISABLE bit is set */
        if (!(s->intregm_disabled & MASTER_DISABLE)) {
            pil_pending |= s->slaves[i].intreg_pending &
                (CPU_IRQ_INT15_IN | CPU_IRQ_TIMER_IN);
        }

        /* Add soft interrupts */
        pil_pending |= (s->slaves[i].intreg_pending & CPU_SOFTIRQ_MASK) >> 16;

        if (set_irqs) {
            /* Since there is not really an interrupt 0 (and pil_pending
             * and irl_out bit zero are thus always zero) there is no need
             * to do anything with cpu_irqs[i][0] and it is OK not to do
             * the j=0 iteration of this loop.
             */
            for (j = MAX_PILS-1; j > 0; j--) {
                if (pil_pending & (1 << j)) {
                    if (!(s->slaves[i].irl_out & (1 << j))) {
                        qemu_irq_raise(s->cpu_irqs[i][j]);
                    }
                } else {
                    if (s->slaves[i].irl_out & (1 << j)) {
                        qemu_irq_lower(s->cpu_irqs[i][j]);
                    }
                }
            }
        }
        s->slaves[i].irl_out = pil_pending;
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
    uint32_t pil = intbit_to_level[irq];
    unsigned int i;

    trace_slavio_set_irq(s->target_cpu, irq, pil, level);
    if (pil > 0) {
        if (level) {
#ifdef DEBUG_IRQ_COUNT
            s->irq_count[pil]++;
#endif
            s->intregm_pending |= mask;
            if (pil == 15) {
                for (i = 0; i < MAX_CPUS; i++) {
                    s->slaves[i].intreg_pending |= 1 << pil;
                }
            }
        } else {
            s->intregm_pending &= ~mask;
            if (pil == 15) {
                for (i = 0; i < MAX_CPUS; i++) {
                    s->slaves[i].intreg_pending &= ~(1 << pil);
                }
            }
        }
        slavio_check_interrupts(s, 1);
    }
}

static void slavio_set_timer_irq_cpu(void *opaque, int cpu, int level)
{
    SLAVIO_INTCTLState *s = opaque;

    trace_slavio_set_timer_irq_cpu(cpu, level);

    if (level) {
        s->slaves[cpu].intreg_pending |= CPU_IRQ_TIMER_IN;
    } else {
        s->slaves[cpu].intreg_pending &= ~CPU_IRQ_TIMER_IN;
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

static int vmstate_intctl_post_load(void *opaque, int version_id)
{
    SLAVIO_INTCTLState *s = opaque;

    slavio_check_interrupts(s, 0);
    return 0;
}

static const VMStateDescription vmstate_intctl_cpu = {
    .name ="slavio_intctl_cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(intreg_pending, SLAVIO_CPUINTCTLState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_intctl = {
    .name ="slavio_intctl",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = vmstate_intctl_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_STRUCT_ARRAY(slaves, SLAVIO_INTCTLState, MAX_CPUS, 1,
                             vmstate_intctl_cpu, SLAVIO_CPUINTCTLState),
        VMSTATE_UINT32(intregm_pending, SLAVIO_INTCTLState),
        VMSTATE_UINT32(intregm_disabled, SLAVIO_INTCTLState),
        VMSTATE_UINT32(target_cpu, SLAVIO_INTCTLState),
        VMSTATE_END_OF_LIST()
    }
};

static void slavio_intctl_reset(DeviceState *d)
{
    SLAVIO_INTCTLState *s = container_of(d, SLAVIO_INTCTLState, busdev.qdev);
    int i;

    for (i = 0; i < MAX_CPUS; i++) {
        s->slaves[i].intreg_pending = 0;
        s->slaves[i].irl_out = 0;
    }
    s->intregm_disabled = ~MASTER_IRQ_MASK;
    s->intregm_pending = 0;
    s->target_cpu = 0;
    slavio_check_interrupts(s, 0);
}

static int slavio_intctl_init1(SysBusDevice *dev)
{
    SLAVIO_INTCTLState *s = FROM_SYSBUS(SLAVIO_INTCTLState, dev);
    int io_memory;
    unsigned int i, j;

    qdev_init_gpio_in(&dev->qdev, slavio_set_irq_all, 32 + MAX_CPUS);
    io_memory = cpu_register_io_memory(slavio_intctlm_mem_read,
                                       slavio_intctlm_mem_write, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, INTCTLM_SIZE, io_memory);

    for (i = 0; i < MAX_CPUS; i++) {
        for (j = 0; j < MAX_PILS; j++) {
            sysbus_init_irq(dev, &s->cpu_irqs[i][j]);
        }
        io_memory = cpu_register_io_memory(slavio_intctl_mem_read,
                                           slavio_intctl_mem_write,
                                           &s->slaves[i],
                                           DEVICE_NATIVE_ENDIAN);
        sysbus_init_mmio(dev, INTCTL_SIZE, io_memory);
        s->slaves[i].cpu = i;
        s->slaves[i].master = s;
    }

    return 0;
}

static SysBusDeviceInfo slavio_intctl_info = {
    .init = slavio_intctl_init1,
    .qdev.name  = "slavio_intctl",
    .qdev.size  = sizeof(SLAVIO_INTCTLState),
    .qdev.vmsd  = &vmstate_intctl,
    .qdev.reset = slavio_intctl_reset,
};

static void slavio_intctl_register_devices(void)
{
    sysbus_register_withprop(&slavio_intctl_info);
}

device_init(slavio_intctl_register_devices)
