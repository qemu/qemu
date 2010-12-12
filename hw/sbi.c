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

#include "sysbus.h"

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
    SysBusDevice busdev;
    uint32_t regs[SBI_NREGS];
    uint32_t intreg_pending[MAX_CPUS];
    qemu_irq cpu_irqs[MAX_CPUS];
    uint32_t pil_out[MAX_CPUS];
} SBIState;

#define SBI_SIZE (SBI_NREGS * 4)

static void sbi_set_irq(void *opaque, int irq, int level)
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

static CPUReadMemoryFunc * const sbi_mem_read[3] = {
    NULL,
    NULL,
    sbi_mem_readl,
};

static CPUWriteMemoryFunc * const sbi_mem_write[3] = {
    NULL,
    NULL,
    sbi_mem_writel,
};

static const VMStateDescription vmstate_sbi = {
    .name ="sbi",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32_ARRAY(intreg_pending, SBIState, MAX_CPUS),
        VMSTATE_END_OF_LIST()
    }
};

static void sbi_reset(DeviceState *d)
{
    SBIState *s = container_of(d, SBIState, busdev.qdev);
    unsigned int i;

    for (i = 0; i < MAX_CPUS; i++) {
        s->intreg_pending[i] = 0;
    }
}

static int sbi_init1(SysBusDevice *dev)
{
    SBIState *s = FROM_SYSBUS(SBIState, dev);
    int sbi_io_memory;
    unsigned int i;

    qdev_init_gpio_in(&dev->qdev, sbi_set_irq, 32 + MAX_CPUS);
    for (i = 0; i < MAX_CPUS; i++) {
        sysbus_init_irq(dev, &s->cpu_irqs[i]);
    }

    sbi_io_memory = cpu_register_io_memory(sbi_mem_read, sbi_mem_write, s,
                                           DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, SBI_SIZE, sbi_io_memory);

    return 0;
}

static SysBusDeviceInfo sbi_info = {
    .init = sbi_init1,
    .qdev.name  = "sbi",
    .qdev.size  = sizeof(SBIState),
    .qdev.vmsd  = &vmstate_sbi,
    .qdev.reset = sbi_reset,
};

static void sbi_register_devices(void)
{
    sysbus_register_withprop(&sbi_info);
}

device_init(sbi_register_devices)
