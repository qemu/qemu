/*
 * QEMU Sparc SBI interrupt controller emulation
 *
 * Based on slavio_intctl, copyright (c) 2003-2005 Fabrice Bellard
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
#include "sun4m.h"
#include "console.h"

//#define DEBUG_IRQ

#ifdef DEBUG_IRQ
#define DPRINTF(fmt, ...)                                       \
    do { printf("IRQ: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
#endif

#define MAX_CPUS 16

#define SBI_NREGS 16

typedef struct SBIState {
    uint32_t regs[SBI_NREGS];
    uint32_t intreg_pending[MAX_CPUS];
    qemu_irq *cpu_irqs[MAX_CPUS];
    uint32_t pil_out[MAX_CPUS];
} SBIState;

#define SBI_SIZE (SBI_NREGS * 4)

static void sbi_check_interrupts(void *opaque)
{
}

static void sbi_set_irq(void *opaque, int irq, int level)
{
}

static void sbi_set_timer_irq_cpu(void *opaque, int cpu, int level)
{
}

static uint32_t sbi_mem_readl(void *opaque, target_phys_addr_t addr)
{
    SBIState *s = opaque;
    uint32_t saddr, ret;

    saddr = addr >> 2;
    switch (saddr) {
    default:
        ret = s->regs[saddr];
        break;
    }
    DPRINTF("read system reg 0x" TARGET_FMT_plx " = %x\n", addr, ret);

    return ret;
}

static void sbi_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    SBIState *s = opaque;
    uint32_t saddr;

    saddr = addr >> 2;
    DPRINTF("write system reg 0x" TARGET_FMT_plx " = %x\n", addr, val);
    switch (saddr) {
    default:
        s->regs[saddr] = val;
        break;
    }
}

static CPUReadMemoryFunc *sbi_mem_read[3] = {
    NULL,
    NULL,
    sbi_mem_readl,
};

static CPUWriteMemoryFunc *sbi_mem_write[3] = {
    NULL,
    NULL,
    sbi_mem_writel,
};

static void sbi_save(QEMUFile *f, void *opaque)
{
    SBIState *s = opaque;
    unsigned int i;

    for (i = 0; i < MAX_CPUS; i++) {
        qemu_put_be32s(f, &s->intreg_pending[i]);
    }
}

static int sbi_load(QEMUFile *f, void *opaque, int version_id)
{
    SBIState *s = opaque;
    unsigned int i;

    if (version_id != 1)
        return -EINVAL;

    for (i = 0; i < MAX_CPUS; i++) {
        qemu_get_be32s(f, &s->intreg_pending[i]);
    }
    sbi_check_interrupts(s);

    return 0;
}

static void sbi_reset(void *opaque)
{
    SBIState *s = opaque;
    unsigned int i;

    for (i = 0; i < MAX_CPUS; i++) {
        s->intreg_pending[i] = 0;
    }
    sbi_check_interrupts(s);
}

void *sbi_init(target_phys_addr_t addr, qemu_irq **irq, qemu_irq **cpu_irq,
               qemu_irq **parent_irq)
{
    unsigned int i;
    int sbi_io_memory;
    SBIState *s;

    s = qemu_mallocz(sizeof(SBIState));

    for (i = 0; i < MAX_CPUS; i++) {
        s->cpu_irqs[i] = parent_irq[i];
    }

    sbi_io_memory = cpu_register_io_memory(0, sbi_mem_read, sbi_mem_write, s);
    cpu_register_physical_memory(addr, SBI_SIZE, sbi_io_memory);

    register_savevm("sbi", addr, 1, sbi_save, sbi_load, s);
    qemu_register_reset(sbi_reset, 0, s);
    *irq = qemu_allocate_irqs(sbi_set_irq, s, 32);
    *cpu_irq = qemu_allocate_irqs(sbi_set_timer_irq_cpu, s, MAX_CPUS);
    sbi_reset(s);

    return s;
}
