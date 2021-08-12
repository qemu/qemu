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
#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "elf.h"
#include "sysemu/reset.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "target/arm/idau.h"
#include "migration/vmstate.h"

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
    res = address_space_read(&s->source_as, addr, attrs, buf, size);
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
    res = address_space_read(&s->source_as, addr, attrs, buf, size);
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
    return address_space_write(&s->source_as, addr, attrs, buf, size);
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

    address_space_init(&s->source_as, s->source_memory, "bitband-source");
}

/* Board init.  */

static const hwaddr bitband_input_addr[ARMV7M_NUM_BITBANDS] = {
    0x20000000, 0x40000000
};

static const hwaddr bitband_output_addr[ARMV7M_NUM_BITBANDS] = {
    0x22000000, 0x42000000
};

static MemTxResult v7m_sysreg_ns_write(void *opaque, hwaddr addr,
                                       uint64_t value, unsigned size,
                                       MemTxAttrs attrs)
{
    MemoryRegion *mr = opaque;

    if (attrs.secure) {
        /* S accesses to the alias act like NS accesses to the real region */
        attrs.secure = 0;
        return memory_region_dispatch_write(mr, addr, value,
                                            size_memop(size) | MO_TE, attrs);
    } else {
        /* NS attrs are RAZ/WI for privileged, and BusFault for user */
        if (attrs.user) {
            return MEMTX_ERROR;
        }
        return MEMTX_OK;
    }
}

static MemTxResult v7m_sysreg_ns_read(void *opaque, hwaddr addr,
                                      uint64_t *data, unsigned size,
                                      MemTxAttrs attrs)
{
    MemoryRegion *mr = opaque;

    if (attrs.secure) {
        /* S accesses to the alias act like NS accesses to the real region */
        attrs.secure = 0;
        return memory_region_dispatch_read(mr, addr, data,
                                           size_memop(size) | MO_TE, attrs);
    } else {
        /* NS attrs are RAZ/WI for privileged, and BusFault for user */
        if (attrs.user) {
            return MEMTX_ERROR;
        }
        *data = 0;
        return MEMTX_OK;
    }
}

static const MemoryRegionOps v7m_sysreg_ns_ops = {
    .read_with_attrs = v7m_sysreg_ns_read,
    .write_with_attrs = v7m_sysreg_ns_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static MemTxResult v7m_systick_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    ARMv7MState *s = opaque;
    MemoryRegion *mr;

    /* Direct the access to the correct systick */
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->systick[attrs.secure]), 0);
    return memory_region_dispatch_write(mr, addr, value,
                                        size_memop(size) | MO_TE, attrs);
}

static MemTxResult v7m_systick_read(void *opaque, hwaddr addr,
                                    uint64_t *data, unsigned size,
                                    MemTxAttrs attrs)
{
    ARMv7MState *s = opaque;
    MemoryRegion *mr;

    /* Direct the access to the correct systick */
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->systick[attrs.secure]), 0);
    return memory_region_dispatch_read(mr, addr, data, size_memop(size) | MO_TE,
                                       attrs);
}

static const MemoryRegionOps v7m_systick_ops = {
    .read_with_attrs = v7m_systick_read,
    .write_with_attrs = v7m_systick_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*
 * Unassigned portions of the PPB space are RAZ/WI for privileged
 * accesses, and fault for non-privileged accesses.
 */
static MemTxResult ppb_default_read(void *opaque, hwaddr addr,
                                    uint64_t *data, unsigned size,
                                    MemTxAttrs attrs)
{
    qemu_log_mask(LOG_UNIMP, "Read of unassigned area of PPB: offset 0x%x\n",
                  (uint32_t)addr);
    if (attrs.user) {
        return MEMTX_ERROR;
    }
    *data = 0;
    return MEMTX_OK;
}

static MemTxResult ppb_default_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    qemu_log_mask(LOG_UNIMP, "Write of unassigned area of PPB: offset 0x%x\n",
                  (uint32_t)addr);
    if (attrs.user) {
        return MEMTX_ERROR;
    }
    return MEMTX_OK;
}

static const MemoryRegionOps ppb_default_ops = {
    .read_with_attrs = ppb_default_read,
    .write_with_attrs = ppb_default_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
};

static void armv7m_instance_init(Object *obj)
{
    ARMv7MState *s = ARMV7M(obj);
    int i;

    /* Can't init the cpu here, we don't yet know which model to use */

    memory_region_init(&s->container, obj, "armv7m-container", UINT64_MAX);

    object_initialize_child(obj, "nvic", &s->nvic, TYPE_NVIC);
    object_property_add_alias(obj, "num-irq",
                              OBJECT(&s->nvic), "num-irq");

    object_initialize_child(obj, "systick-reg-ns", &s->systick[M_REG_NS],
                            TYPE_SYSTICK);
    /*
     * We can't initialize the secure systick here, as we don't know
     * yet if we need it.
     */

    for (i = 0; i < ARRAY_SIZE(s->bitband); i++) {
        object_initialize_child(obj, "bitband[*]", &s->bitband[i],
                                TYPE_BITBAND);
    }

    s->refclk = qdev_init_clock_in(DEVICE(obj), "refclk", NULL, NULL, 0);
    s->cpuclk = qdev_init_clock_in(DEVICE(obj), "cpuclk", NULL, NULL, 0);
}

static void armv7m_realize(DeviceState *dev, Error **errp)
{
    ARMv7MState *s = ARMV7M(dev);
    SysBusDevice *sbd;
    Error *err = NULL;
    int i;

    if (!s->board_memory) {
        error_setg(errp, "memory property was not set");
        return;
    }

    memory_region_add_subregion_overlap(&s->container, 0, s->board_memory, -1);

    s->cpu = ARM_CPU(object_new_with_props(s->cpu_type, OBJECT(s), "cpu",
                                           &err, NULL));
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_link(OBJECT(s->cpu), "memory", OBJECT(&s->container),
                             &error_abort);
    if (object_property_find(OBJECT(s->cpu), "idau")) {
        object_property_set_link(OBJECT(s->cpu), "idau", s->idau,
                                 &error_abort);
    }
    if (object_property_find(OBJECT(s->cpu), "init-svtor")) {
        if (!object_property_set_uint(OBJECT(s->cpu), "init-svtor",
                                      s->init_svtor, errp)) {
            return;
        }
    }
    if (object_property_find(OBJECT(s->cpu), "init-nsvtor")) {
        if (!object_property_set_uint(OBJECT(s->cpu), "init-nsvtor",
                                      s->init_nsvtor, errp)) {
            return;
        }
    }
    if (object_property_find(OBJECT(s->cpu), "start-powered-off")) {
        if (!object_property_set_bool(OBJECT(s->cpu), "start-powered-off",
                                      s->start_powered_off, errp)) {
            return;
        }
    }
    if (object_property_find(OBJECT(s->cpu), "vfp")) {
        if (!object_property_set_bool(OBJECT(s->cpu), "vfp", s->vfp, errp)) {
            return;
        }
    }
    if (object_property_find(OBJECT(s->cpu), "dsp")) {
        if (!object_property_set_bool(OBJECT(s->cpu), "dsp", s->dsp, errp)) {
            return;
        }
    }

    /*
     * Tell the CPU where the NVIC is; it will fail realize if it doesn't
     * have one. Similarly, tell the NVIC where its CPU is.
     */
    s->cpu->env.nvic = &s->nvic;
    s->nvic.cpu = s->cpu;

    if (!qdev_realize(DEVICE(s->cpu), NULL, errp)) {
        return;
    }

    /* Note that we must realize the NVIC after the CPU */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->nvic), errp)) {
        return;
    }

    /* Alias the NVIC's input and output GPIOs as our own so the board
     * code can wire them up. (We do this in realize because the
     * NVIC doesn't create the input GPIO array until realize.)
     */
    qdev_pass_gpios(DEVICE(&s->nvic), dev, NULL);
    qdev_pass_gpios(DEVICE(&s->nvic), dev, "SYSRESETREQ");
    qdev_pass_gpios(DEVICE(&s->nvic), dev, "NMI");

    /*
     * We map various devices into the container MR at their architected
     * addresses. In particular, we map everything corresponding to the
     * "System PPB" space. This is the range from 0xe0000000 to 0xe00fffff
     * and includes the NVIC, the System Control Space (system registers),
     * the systick timer, and for CPUs with the Security extension an NS
     * banked version of all of these.
     *
     * The default behaviour for unimplemented registers/ranges
     * (for instance the Data Watchpoint and Trace unit at 0xe0001000)
     * is to RAZ/WI for privileged access and BusFault for non-privileged
     * access.
     *
     * The NVIC and System Control Space (SCS) starts at 0xe000e000
     * and looks like this:
     *  0x004 - ICTR
     *  0x010 - 0xff - systick
     *  0x100..0x7ec - NVIC
     *  0x7f0..0xcff - Reserved
     *  0xd00..0xd3c - SCS registers
     *  0xd40..0xeff - Reserved or Not implemented
     *  0xf00 - STIR
     *
     * Some registers within this space are banked between security states.
     * In v8M there is a second range 0xe002e000..0xe002efff which is the
     * NonSecure alias SCS; secure accesses to this behave like NS accesses
     * to the main SCS range, and non-secure accesses (including when
     * the security extension is not implemented) are RAZ/WI.
     * Note that both the main SCS range and the alias range are defined
     * to be exempt from memory attribution (R_BLJT) and so the memory
     * transaction attribute always matches the current CPU security
     * state (attrs.secure == env->v7m.secure). In the v7m_sysreg_ns_ops
     * wrappers we change attrs.secure to indicate the NS access; so
     * generally code determining which banked register to use should
     * use attrs.secure; code determining actual behaviour of the system
     * should use env->v7m.secure.
     *
     * Within the PPB space, some MRs overlap, and the priority
     * of overlapping regions is:
     *  - default region (for RAZ/WI and BusFault) : -1
     *  - system register regions (provided by the NVIC) : 0
     *  - systick : 1
     * This is because the systick device is a small block of registers
     * in the middle of the other system control registers.
     */

    memory_region_init_io(&s->defaultmem, OBJECT(s), &ppb_default_ops, s,
                          "nvic-default", 0x100000);
    memory_region_add_subregion_overlap(&s->container, 0xe0000000,
                                        &s->defaultmem, -1);

    /* Wire the NVIC up to the CPU */
    sbd = SYS_BUS_DEVICE(&s->nvic);
    sysbus_connect_irq(sbd, 0,
                       qdev_get_gpio_in(DEVICE(s->cpu), ARM_CPU_IRQ));

    memory_region_add_subregion(&s->container, 0xe000e000,
                                sysbus_mmio_get_region(sbd, 0));
    if (arm_feature(&s->cpu->env, ARM_FEATURE_V8)) {
        /* Create the NS alias region for the NVIC sysregs */
        memory_region_init_io(&s->sysreg_ns_mem, OBJECT(s),
                              &v7m_sysreg_ns_ops,
                              sysbus_mmio_get_region(sbd, 0),
                              "nvic_sysregs_ns", 0x1000);
        memory_region_add_subregion(&s->container, 0xe002e000,
                                    &s->sysreg_ns_mem);
    }

    /* Create and map the systick devices */
    qdev_connect_clock_in(DEVICE(&s->systick[M_REG_NS]), "refclk", s->refclk);
    qdev_connect_clock_in(DEVICE(&s->systick[M_REG_NS]), "cpuclk", s->cpuclk);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->systick[M_REG_NS]), errp)) {
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->systick[M_REG_NS]), 0,
                       qdev_get_gpio_in_named(DEVICE(&s->nvic),
                                              "systick-trigger", M_REG_NS));

    if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY)) {
        /*
         * We couldn't init the secure systick device in instance_init
         * as we didn't know then if the CPU had the security extensions;
         * so we have to do it here.
         */
        object_initialize_child(OBJECT(dev), "systick-reg-s",
                                &s->systick[M_REG_S], TYPE_SYSTICK);
        qdev_connect_clock_in(DEVICE(&s->systick[M_REG_S]), "refclk",
                              s->refclk);
        qdev_connect_clock_in(DEVICE(&s->systick[M_REG_S]), "cpuclk",
                              s->cpuclk);

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->systick[M_REG_S]), errp)) {
            return;
        }
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->systick[M_REG_S]), 0,
                           qdev_get_gpio_in_named(DEVICE(&s->nvic),
                                                  "systick-trigger", M_REG_S));
    }

    memory_region_init_io(&s->systickmem, OBJECT(s),
                          &v7m_systick_ops, s,
                          "v7m_systick", 0xe0);

    memory_region_add_subregion_overlap(&s->container, 0xe000e010,
                                        &s->systickmem, 1);
    if (arm_feature(&s->cpu->env, ARM_FEATURE_V8)) {
        memory_region_init_io(&s->systick_ns_mem, OBJECT(s),
                              &v7m_sysreg_ns_ops, &s->systickmem,
                              "v7m_systick_ns", 0xe0);
        memory_region_add_subregion_overlap(&s->container, 0xe002e010,
                                            &s->systick_ns_mem, 1);
    }

    /* If the CPU has RAS support, create the RAS register block */
    if (cpu_isar_feature(aa32_ras, s->cpu)) {
        object_initialize_child(OBJECT(dev), "armv7m-ras",
                                &s->ras, TYPE_ARMV7M_RAS);
        sbd = SYS_BUS_DEVICE(&s->ras);
        if (!sysbus_realize(sbd, errp)) {
            return;
        }
        memory_region_add_subregion_overlap(&s->container, 0xe0005000,
                                            sysbus_mmio_get_region(sbd, 0), 1);
    }

    for (i = 0; i < ARRAY_SIZE(s->bitband); i++) {
        if (s->enable_bitband) {
            Object *obj = OBJECT(&s->bitband[i]);
            SysBusDevice *sbd = SYS_BUS_DEVICE(&s->bitband[i]);

            if (!object_property_set_int(obj, "base",
                                         bitband_input_addr[i], errp)) {
                return;
            }
            object_property_set_link(obj, "source-memory",
                                     OBJECT(s->board_memory), &error_abort);
            if (!sysbus_realize(SYS_BUS_DEVICE(obj), errp)) {
                return;
            }

            memory_region_add_subregion(&s->container, bitband_output_addr[i],
                                        sysbus_mmio_get_region(sbd, 0));
        } else {
            object_unparent(OBJECT(&s->bitband[i]));
        }
    }
}

static Property armv7m_properties[] = {
    DEFINE_PROP_STRING("cpu-type", ARMv7MState, cpu_type),
    DEFINE_PROP_LINK("memory", ARMv7MState, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_LINK("idau", ARMv7MState, idau, TYPE_IDAU_INTERFACE, Object *),
    DEFINE_PROP_UINT32("init-svtor", ARMv7MState, init_svtor, 0),
    DEFINE_PROP_UINT32("init-nsvtor", ARMv7MState, init_nsvtor, 0),
    DEFINE_PROP_BOOL("enable-bitband", ARMv7MState, enable_bitband, false),
    DEFINE_PROP_BOOL("start-powered-off", ARMv7MState, start_powered_off,
                     false),
    DEFINE_PROP_BOOL("vfp", ARMv7MState, vfp, true),
    DEFINE_PROP_BOOL("dsp", ARMv7MState, dsp, true),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_armv7m = {
    .name = "armv7m",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_CLOCK(refclk, SysTickState),
        VMSTATE_CLOCK(cpuclk, SysTickState),
        VMSTATE_END_OF_LIST()
    }
};

static void armv7m_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = armv7m_realize;
    dc->vmsd = &vmstate_armv7m;
    device_class_set_props(dc, armv7m_properties);
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

void armv7m_load_kernel(ARMCPU *cpu, const char *kernel_filename, int mem_size)
{
    int image_size;
    uint64_t entry;
    int big_endian;
    AddressSpace *as;
    int asidx;
    CPUState *cs = CPU(cpu);

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif

    if (arm_feature(&cpu->env, ARM_FEATURE_EL3)) {
        asidx = ARMASIdx_S;
    } else {
        asidx = ARMASIdx_NS;
    }
    as = cpu_get_address_space(cs, asidx);

    if (kernel_filename) {
        image_size = load_elf_as(kernel_filename, NULL, NULL, NULL,
                                 &entry, NULL, NULL,
                                 NULL, big_endian, EM_ARM, 1, 0, as);
        if (image_size < 0) {
            image_size = load_image_targphys_as(kernel_filename, 0,
                                                mem_size, as);
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
    DEFINE_PROP_LINK("source-memory", BitBandState, source_memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void bitband_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bitband_realize;
    device_class_set_props(dc, bitband_properties);
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
