/*
 * Xilinx Zynq MPSoC PMU (Power Management Unit) emulation
 *
 * Copyright (C) 2017 Xilinx Inc
 * Written by Alistair Francis <alistair.francis@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "hw/boards.h"
#include "cpu.h"
#include "boot.h"

#include "hw/intc/xlnx-zynqmp-ipi.h"
#include "hw/intc/xlnx-pmu-iomod-intc.h"
#include "qom/object.h"

/* Define the PMU device */

#define TYPE_XLNX_ZYNQMP_PMU_SOC "xlnx-zynqmp-pmu-soc"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxZynqMPPMUSoCState, XLNX_ZYNQMP_PMU_SOC)

#define XLNX_ZYNQMP_PMU_ROM_SIZE    0x8000
#define XLNX_ZYNQMP_PMU_ROM_ADDR    0xFFD00000
#define XLNX_ZYNQMP_PMU_RAM_ADDR    0xFFDC0000

#define XLNX_ZYNQMP_PMU_INTC_ADDR   0xFFD40000

#define XLNX_ZYNQMP_PMU_NUM_IPIS    4

static const uint64_t ipi_addr[XLNX_ZYNQMP_PMU_NUM_IPIS] = {
    0xFF340000, 0xFF350000, 0xFF360000, 0xFF370000,
};
static const uint64_t ipi_irq[XLNX_ZYNQMP_PMU_NUM_IPIS] = {
    19, 20, 21, 22,
};

struct XlnxZynqMPPMUSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    MicroBlazeCPU cpu;
    XlnxPMUIOIntc intc;
    XlnxZynqMPIPI ipi[XLNX_ZYNQMP_PMU_NUM_IPIS];
};


static void xlnx_zynqmp_pmu_soc_init(Object *obj)
{
    XlnxZynqMPPMUSoCState *s = XLNX_ZYNQMP_PMU_SOC(obj);

    object_initialize_child(obj, "pmu-cpu", &s->cpu, TYPE_MICROBLAZE_CPU);

    object_initialize_child(obj, "intc", &s->intc, TYPE_XLNX_PMU_IO_INTC);

    /* Create the IPI device */
    for (int i = 0; i < XLNX_ZYNQMP_PMU_NUM_IPIS; i++) {
        char *name = g_strdup_printf("ipi%d", i);
        object_initialize_child(obj, name, &s->ipi[i], TYPE_XLNX_ZYNQMP_IPI);
        g_free(name);
    }
}

static void xlnx_zynqmp_pmu_soc_realize(DeviceState *dev, Error **errp)
{
    XlnxZynqMPPMUSoCState *s = XLNX_ZYNQMP_PMU_SOC(dev);

    object_property_set_uint(OBJECT(&s->cpu), "base-vectors",
                             XLNX_ZYNQMP_PMU_ROM_ADDR, &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), "use-stack-protection", true,
                             &error_abort);
    object_property_set_uint(OBJECT(&s->cpu), "use-fpu", 0, &error_abort);
    object_property_set_uint(OBJECT(&s->cpu), "use-hw-mul", 0, &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), "use-barrel", true,
                             &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), "use-msr-instr", true,
                             &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), "use-pcmp-instr", true,
                             &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), "use-mmu", false, &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), "little-endian", true,
                             &error_abort);
    object_property_set_str(OBJECT(&s->cpu), "version", "8.40.b",
                            &error_abort);
    object_property_set_uint(OBJECT(&s->cpu), "pvr", 0, &error_abort);
    if (!qdev_realize(DEVICE(&s->cpu), NULL, errp)) {
        return;
    }

    object_property_set_uint(OBJECT(&s->intc), "intc-intr-size", 0x10,
                             &error_abort);
    object_property_set_uint(OBJECT(&s->intc), "intc-level-edge", 0x0,
                             &error_abort);
    object_property_set_uint(OBJECT(&s->intc), "intc-positive", 0xffff,
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->intc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->intc), 0, XLNX_ZYNQMP_PMU_INTC_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->intc), 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu), MB_CPU_IRQ));

    /* Connect the IPI device */
    for (int i = 0; i < XLNX_ZYNQMP_PMU_NUM_IPIS; i++) {
        sysbus_realize(SYS_BUS_DEVICE(&s->ipi[i]), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->ipi[i]), 0, ipi_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->ipi[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->intc), ipi_irq[i]));
    }
}

static void xlnx_zynqmp_pmu_soc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    /* xlnx-zynqmp-pmu-soc causes crashes when cold-plugged twice */
    dc->user_creatable = false;
    dc->realize = xlnx_zynqmp_pmu_soc_realize;
}

static const TypeInfo xlnx_zynqmp_pmu_soc_type_info = {
    .name = TYPE_XLNX_ZYNQMP_PMU_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(XlnxZynqMPPMUSoCState),
    .instance_init = xlnx_zynqmp_pmu_soc_init,
    .class_init = xlnx_zynqmp_pmu_soc_class_init,
};

static void xlnx_zynqmp_pmu_soc_register_types(void)
{
    type_register_static(&xlnx_zynqmp_pmu_soc_type_info);
}

type_init(xlnx_zynqmp_pmu_soc_register_types)

/* Define the PMU Machine */

static void xlnx_zynqmp_pmu_init(MachineState *machine)
{
    XlnxZynqMPPMUSoCState *pmu = g_new0(XlnxZynqMPPMUSoCState, 1);
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *pmu_rom = g_new(MemoryRegion, 1);
    MemoryRegion *pmu_ram = g_new(MemoryRegion, 1);

    /* Create the ROM */
    memory_region_init_rom(pmu_rom, NULL, "xlnx-zynqmp-pmu.rom",
                           XLNX_ZYNQMP_PMU_ROM_SIZE, &error_fatal);
    memory_region_add_subregion(address_space_mem, XLNX_ZYNQMP_PMU_ROM_ADDR,
                                pmu_rom);

    /* Create the RAM */
    memory_region_init_ram(pmu_ram, NULL, "xlnx-zynqmp-pmu.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(address_space_mem, XLNX_ZYNQMP_PMU_RAM_ADDR,
                                pmu_ram);

    /* Create the PMU device */
    object_initialize_child(OBJECT(machine), "pmu", pmu,
                            TYPE_XLNX_ZYNQMP_PMU_SOC);
    qdev_realize(DEVICE(pmu), NULL, &error_fatal);

    /* Load the kernel */
    microblaze_load_kernel(&pmu->cpu, true, XLNX_ZYNQMP_PMU_RAM_ADDR,
                           machine->ram_size,
                           machine->initrd_filename,
                           machine->dtb,
                           NULL);
}

static void xlnx_zynqmp_pmu_machine_init(MachineClass *mc)
{
    mc->desc = "Xilinx ZynqMP PMU machine (little endian)";
    mc->init = xlnx_zynqmp_pmu_init;
}

DEFINE_MACHINE("xlnx-zynqmp-pmu", xlnx_zynqmp_pmu_machine_init)
