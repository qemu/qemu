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
#include "exec/address-spaces.h"

/* Bitbanded IO.  Each word corresponds to a single bit.  */

/* Get the byte address of the real memory for a bitband access.  */
static inline hwaddr bitband_addr(BitBandState *s, hwaddr offset)
{
    return s->base | (offset & 0x1ffffff) >> 5;
}

static MemTxResult bitband_read(void *opaque, hwaddr offset,
                                uint64_t *data, unsigned size, MemTxAttrs attrs)
{
    BitBandState *s = opaque;
    uint8_t buf[4];
    MemTxResult res;
    int bitpos, bit;
    hwaddr addr;

    assert(size <= 4);

    /* Find address in underlying memory and round down to multiple of size */
    addr = bitband_addr(s, offset) & (-size);
    res = address_space_read(s->source_as, addr, attrs, buf, size);
    if (res) {
        return res;
    }
    /* Bit position in the N bytes read... */
    bitpos = (offset >> 2) & ((size * 8) - 1);
    /* ...converted to byte in buffer and bit in byte */
    bit = (buf[bitpos >> 3] >> (bitpos & 7)) & 1;
    *data = bit;
    return MEMTX_OK;
}

static MemTxResult bitband_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size, MemTxAttrs attrs)
{
    BitBandState *s = opaque;
    uint8_t buf[4];
    MemTxResult res;
    int bitpos, bit;
    hwaddr addr;

    assert(size <= 4);

    /* Find address in underlying memory and round down to multiple of size */
    addr = bitband_addr(s, offset) & (-size);
    res = address_space_read(s->source_as, addr, attrs, buf, size);
    if (res) {
        return res;
    }
    /* Bit position in the N bytes read... */
    bitpos = (offset >> 2) & ((size * 8) - 1);
    /* ...converted to byte in buffer and bit in byte */
    bit = 1 << (bitpos & 7);
    if (value & 1) {
        buf[bitpos >> 3] |= bit;
    } else {
        buf[bitpos >> 3] &= ~bit;
    }
    return address_space_write(s->source_as, addr, attrs, buf, size);
}

static const MemoryRegionOps bitband_ops = {
    .read_with_attrs = bitband_read,
    .write_with_attrs = bitband_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void bitband_init(Object *obj)
{
    BitBandState *s = BITBAND(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    object_property_add_link(obj, "source-memory",
                             TYPE_MEMORY_REGION,
                             (Object **)&s->source_memory,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
    memory_region_init_io(&s->iomem, obj, &bitband_ops, s,
                          "bitband", 0x02000000);
    sysbus_init_mmio(dev, &s->iomem);
}

static void bitband_realize(DeviceState *dev, Error **errp)
{
    BitBandState *s = BITBAND(dev);

    if (!s->source_memory) {
        error_setg(errp, "source-memory property not set");
        return;
    }

    s->source_as = address_space_init_shareable(s->source_memory,
                                                "bitband-source");
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

    object_property_add_link(obj, "memory",
                             TYPE_MEMORY_REGION,
                             (Object **)&s->board_memory,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
    memory_region_init(&s->container, obj, "armv7m-container", UINT64_MAX);

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
    SysBusDevice *sbd;
    Error *err = NULL;
    int i;
    char **cpustr;
    ObjectClass *oc;
    const char *typename;
    CPUClass *cc;

    if (!s->board_memory) {
        error_setg(errp, "memory property was not set");
        return;
    }

    memory_region_add_subregion_overlap(&s->container, 0, s->board_memory, -1);

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

    object_property_set_link(OBJECT(s->cpu), OBJECT(&s->container), "memory",
                             &error_abort);
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
    sbd = SYS_BUS_DEVICE(&s->nvic);
    sysbus_connect_irq(sbd, 0,
                       qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));
    s->cpu->env.nvic = &s->nvic;

    memory_region_add_subregion(&s->container, 0xe000e000,
                                sysbus_mmio_get_region(sbd, 0));

    for (i = 0; i < ARRAY_SIZE(s->bitband); i++) {
        Object *obj = OBJECT(&s->bitband[i]);
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->bitband[i]);

        object_property_set_int(obj, bitband_input_addr[i], "base", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
        object_property_set_link(obj, OBJECT(s->board_memory),
                                 "source-memory", &error_abort);
        object_property_set_bool(obj, true, "realized", &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }

        memory_region_add_subregion(&s->container, bitband_output_addr[i],
                                    sysbus_mmio_get_region(sbd, 0));
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
   Returns the ARMv7M device.  */

DeviceState *armv7m_init(MemoryRegion *system_memory, int mem_size, int num_irq,
                      const char *kernel_filename, const char *cpu_model)
{
    DeviceState *armv7m;

    if (cpu_model == NULL) {
        cpu_model = "cortex-m3";
    }

    armv7m = qdev_create(NULL, "armv7m");
    qdev_prop_set_uint32(armv7m, "num-irq", num_irq);
    qdev_prop_set_string(armv7m, "cpu-model", cpu_model);
    object_property_set_link(OBJECT(armv7m), OBJECT(get_system_memory()),
                                     "memory", &error_abort);
    /* This will exit with an error if the user passed us a bad cpu_model */
    qdev_init_nofail(armv7m);

    armv7m_load_kernel(ARM_CPU(first_cpu), kernel_filename, mem_size);
    return armv7m;
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

    dc->realize = bitband_realize;
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
