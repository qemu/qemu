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
    MemoryRegion iomem;
    uint32_t regs[SBI_NREGS];
    uint32_t intreg_pending[MAX_CPUS];
    qemu_irq cpu_irqs[MAX_CPUS];
    uint32_t pil_out[MAX_CPUS];
} SBIState;

#define SBI_SIZE (SBI_NREGS * 4)

static void sbi_set_irq(void *opaque, int irq, int level)
{
}

static uint64_t sbi_mem_read(void *opaque, target_phys_addr_t addr,
                             unsigned size)
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

static void sbi_mem_write(void *opaque, target_phys_addr_t addr,
                          uint64_t val, unsigned dize)
{
    SBIState *s = opaque;
    uint32_t saddr;

    saddr = addr >> 2;
    DPRINTF("write system reg 0x" TARGET_FMT_plx " = %x\n", addr, (int)val);
    switch (saddr) {
    default:
        s->regs[saddr] = val;
        break;
    }
}

static const MemoryRegionOps sbi_mem_ops = {
    .read = sbi_mem_read,
    .write = sbi_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
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
    unsigned int i;

    qdev_init_gpio_in(&dev->qdev, sbi_set_irq, 32 + MAX_CPUS);
    for (i = 0; i < MAX_CPUS; i++) {
        sysbus_init_irq(dev, &s->cpu_irqs[i]);
    }

    memory_region_init_io(&s->iomem, &sbi_mem_ops, s, "sbi", SBI_SIZE);
    sysbus_init_mmio(dev, &s->iomem);

    return 0;
}

static void sbi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = sbi_init1;
    dc->reset = sbi_reset;
    dc->vmsd = &vmstate_sbi;
}

static TypeInfo sbi_info = {
    .name          = "sbi",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SBIState),
    .class_init    = sbi_class_init,
};

static void sbi_register_types(void)
{
    type_register_static(&sbi_info);
}

type_init(sbi_register_types)
