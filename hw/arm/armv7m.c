/*
 * ARMV7M System emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/arm/armv7m.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/qtest.h"
#include "qemu/error-report.h"

/* Bitbanded IO.  Each word corresponds to a single bit.  */

/* Get the byte address of the real memory for a bitband access.  */
static inline uint32_t bitband_addr(void * opaque, uint32_t addr)
{
    uint32_t res;

    res = *(uint32_t *)opaque;
    res |= (addr & 0x1ffffff) >> 5;
    return res;

}

static uint32_t bitband_readb(void *opaque, hwaddr offset)
{
    uint8_t v;
    cpu_physical_memory_read(bitband_addr(opaque, offset), &v, 1);
    return (v & (1 << ((offset >> 2) & 7))) != 0;
}

static void bitband_writeb(void *opaque, hwaddr offset,
                           uint32_t value)
{
    uint32_t addr;
    uint8_t mask;
    uint8_t v;
    addr = bitband_addr(opaque, offset);
    mask = (1 << ((offset >> 2) & 7));
    cpu_physical_memory_read(addr, &v, 1);
    if (value & 1)
        v |= mask;
    else
        v &= ~mask;
    cpu_physical_memory_write(addr, &v, 1);
}

static uint32_t bitband_readw(void *opaque, hwaddr offset)
{
    uint32_t addr;
    uint16_t mask;
    uint16_t v;
    addr = bitband_addr(opaque, offset) & ~1;
    mask = (1 << ((offset >> 2) & 15));
    mask = tswap16(mask);
    cpu_physical_memory_read(addr, &v, 2);
    return (v & mask) != 0;
}

static void bitband_writew(void *opaque, hwaddr offset,
                           uint32_t value)
{
    uint32_t addr;
    uint16_t mask;
    uint16_t v;
    addr = bitband_addr(opaque, offset) & ~1;
    mask = (1 << ((offset >> 2) & 15));
    mask = tswap16(mask);
    cpu_physical_memory_read(addr, &v, 2);
    if (value & 1)
        v |= mask;
    else
        v &= ~mask;
    cpu_physical_memory_write(addr, &v, 2);
}

static uint32_t bitband_readl(void *opaque, hwaddr offset)
{
    uint32_t addr;
    uint32_t mask;
    uint32_t v;
    addr = bitband_addr(opaque, offset) & ~3;
    mask = (1 << ((offset >> 2) & 31));
    mask = tswap32(mask);
    cpu_physical_memory_read(addr, &v, 4);
    return (v & mask) != 0;
}

static void bitband_writel(void *opaque, hwaddr offset,
                           uint32_t value)
{
    uint32_t addr;
    uint32_t mask;
    uint32_t v;
    addr = bitband_addr(opaque, offset) & ~3;
    mask = (1 << ((offset >> 2) & 31));
    mask = tswap32(mask);
    cpu_physical_memory_read(addr, &v, 4);
    if (value & 1)
        v |= mask;
    else
        v &= ~mask;
    cpu_physical_memory_write(addr, &v, 4);
}

static const MemoryRegionOps bitband_ops = {
    .old_mmio = {
        .read = { bitband_readb, bitband_readw, bitband_readl, },
        .write = { bitband_writeb, bitband_writew, bitband_writel, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void bitband_init(Object *obj)
{
    BitBandState *s = BITBAND(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &bitband_ops, &s->base,
                          "bitband", 0x02000000);
    sysbus_init_mmio(dev, &s->iomem);
}

static void armv7m_bitband_init(void)
{
    DeviceState *dev;

    dev = qdev_create(NULL, TYPE_BITBAND);
    qdev_prop_set_uint32(dev, "base", 0x20000000);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0x22000000);

    dev = qdev_create(NULL, TYPE_BITBAND);
    qdev_prop_set_uint32(dev, "base", 0x40000000);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0x42000000);
}

/* Board init.  */

static const hwaddr bitband_input_addr[ARMV7M_NUM_BITBANDS] = {
    0x20000000, 0x40000000
};

static const hwaddr bitband_output_addr[ARMV7M_NUM_BITBANDS] = {
    0x22000000, 0x42000000
};

static void armv7m_instance_init(Object *obj)
{
    ARMv7MState *s = ARMV7M(obj);
    int i;

    /* Can't init the cpu here, we don't yet know which model to use */

    object_initialize(&s->nvic, sizeof(s->nvic), "armv7m_nvic");
    qdev_set_parent_bus(DEVICE(&s->nvic), sysbus_get_default());
    object_property_add_alias(obj, "num-irq",
                              OBJECT(&s->nvic), "num-irq", &error_abort);

    for (i = 0; i < ARRAY_SIZE(s->bitband); i++) {
        object_initialize(&s->bitband[i], sizeof(s->bitband[i]), TYPE_BITBAND);
        qdev_set_parent_bus(DEVICE(&s->bitband[i]), sysbus_get_default());
    }
}

static void armv7m_realize(DeviceState *dev, Error **errp)
{
    ARMv7MState *s = ARMV7M(dev);
    Error *err = NULL;
    int i;
    char **cpustr;
    ObjectClass *oc;
    const char *typename;
    CPUClass *cc;

    cpustr = g_strsplit(s->cpu_model, ",", 2);

    oc = cpu_class_by_name(TYPE_ARM_CPU, cpustr[0]);
    if (!oc) {
        error_setg(errp, "Unknown CPU model %s", cpustr[0]);
        g_strfreev(cpustr);
        return;
    }

    cc = CPU_CLASS(oc);
    typename = object_class_get_name(oc);
    cc->parse_features(typename, cpustr[1], &err);
    g_strfreev(cpustr);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    s->cpu = ARM_CPU(object_new(typename));
    if (!s->cpu) {
        error_setg(errp, "Unknown CPU model %s", s->cpu_model);
        return;
    }

    object_property_set_bool(OBJECT(s->cpu), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    /* Note that we must realize the NVIC after the CPU */
    object_property_set_bool(OBJECT(&s->nvic), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    /* Alias the NVIC's input and output GPIOs as our own so the board
     * code can wire them up. (We do this in realize because the
     * NVIC doesn't create the input GPIO array until realize.)
     */
    qdev_pass_gpios(DEVICE(&s->nvic), dev, NULL);
    qdev_pass_gpios(DEVICE(&s->nvic), dev, "SYSRESETREQ");

    /* Wire the NVIC up to the CPU */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->nvic), 0,
                       qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));
    s->cpu->env.nvic = &s->nvic;

    for (i = 0; i < ARRAY_SIZE(s->bitband); i++) {
        Object *obj = OBJECT(&s->bitband[i]);
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->bitband[i]);

        object_property_set_int(obj, bitband_input_addr[i], "base", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
        object_property_set_bool(obj, true, "realized", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }

        sysbus_mmio_map(sbd, 0, bitband_output_addr[i]);
    }
}

static Property armv7m_properties[] = {
    DEFINE_PROP_STRING("cpu-model", ARMv7MState, cpu_model),
    DEFINE_PROP_END_OF_LIST(),
};

static void armv7m_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = armv7m_realize;
    dc->props = armv7m_properties;
}

static const TypeInfo armv7m_info = {
    .name = TYPE_ARMV7M,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMv7MState),
    .instance_init = armv7m_instance_init,
    .class_init = armv7m_class_init,
};

static void armv7m_reset(void *opaque)
{
    ARMCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

/* Init CPU and memory for a v7-M based board.
   mem_size is in bytes.
   Returns the NVIC array.  */

DeviceState *armv7m_init(MemoryRegion *system_memory, int mem_size, int num_irq,
                      const char *kernel_filename, const char *cpu_model)
{
    ARMCPU *cpu;
    CPUARMState *env;
    DeviceState *nvic;

    if (cpu_model == NULL) {
	cpu_model = "cortex-m3";
    }
    cpu = cpu_arm_init(cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    env = &cpu->env;

    armv7m_bitband_init();

    nvic = qdev_create(NULL, "armv7m_nvic");
    qdev_prop_set_uint32(nvic, "num-irq", num_irq);
    env->nvic = nvic;
    qdev_init_nofail(nvic);
    sysbus_connect_irq(SYS_BUS_DEVICE(nvic), 0,
                       qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
    armv7m_load_kernel(cpu, kernel_filename, mem_size);
    return nvic;
}

void armv7m_load_kernel(ARMCPU *cpu, const char *kernel_filename, int mem_size)
{
    int image_size;
    uint64_t entry;
    uint64_t lowaddr;
    int big_endian;

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif

    if (!kernel_filename && !qtest_enabled()) {
        fprintf(stderr, "Guest image must be specified (using -kernel)\n");
        exit(1);
    }

    if (kernel_filename) {
        image_size = load_elf(kernel_filename, NULL, NULL, &entry, &lowaddr,
                              NULL, big_endian, EM_ARM, 1, 0);
        if (image_size < 0) {
            image_size = load_image_targphys(kernel_filename, 0, mem_size);
            lowaddr = 0;
        }
        if (image_size < 0) {
            error_report("Could not load kernel '%s'", kernel_filename);
            exit(1);
        }
    }

    /* CPU objects (unlike devices) are not automatically reset on system
     * reset, so we must always register a handler to do so. Unlike
     * A-profile CPUs, we don't need to do anything special in the
     * handler to arrange that it starts correctly.
     * This is arguably the wrong place to do this, but it matches the
     * way A-profile does it. Note that this means that every M profile
     * board must call this function!
     */
    qemu_register_reset(armv7m_reset, cpu);
}

static Property bitband_properties[] = {
    DEFINE_PROP_UINT32("base", BitBandState, base, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void bitband_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = bitband_properties;
}

static const TypeInfo bitband_info = {
    .name          = TYPE_BITBAND,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BitBandState),
    .instance_init = bitband_init,
    .class_init    = bitband_class_init,
};

static void armv7m_register_types(void)
{
    type_register_static(&bitband_info);
    type_register_static(&armv7m_info);
}

type_init(armv7m_register_types)
