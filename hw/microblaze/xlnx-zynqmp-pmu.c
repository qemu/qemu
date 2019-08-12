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
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "cpu.h"
#include "boot.h"

#include "hw/intc/xlnx-zynqmp-ipi.h"
#include "hw/intc/xlnx-pmu-iomod-intc.h"

/* Define the PMU device */

#define TYPE_XLNX_ZYNQMP_PMU_SOC "xlnx,zynqmp-pmu-soc"
#define XLNX_ZYNQMP_PMU_SOC(obj) OBJECT_CHECK(XlnxZynqMPPMUSoCState, (obj), \
                                              TYPE_XLNX_ZYNQMP_PMU_SOC)

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

typedef struct XlnxZynqMPPMUSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    MicroBlazeCPU cpu;
    XlnxPMUIOIntc intc;
    XlnxZynqMPIPI ipi[XLNX_ZYNQMP_PMU_NUM_IPIS];
}  XlnxZynqMPPMUSoCState;


static void xlnx_zynqmp_pmu_soc_init(Object *obj)
{
    XlnxZynqMPPMUSoCState *s = XLNX_ZYNQMP_PMU_SOC(obj);

    object_initialize_child(obj, "pmu-cpu", &s->cpu, sizeof(s->cpu),
                            TYPE_MICROBLAZE_CPU, &error_abort, NULL);

    sysbus_init_child_obj(obj, "intc", &s->intc, sizeof(s->intc),
                          TYPE_XLNX_PMU_IO_INTC);

    /* Create the IPI device */
    for (int i = 0; i < XLNX_ZYNQMP_PMU_NUM_IPIS; i++) {
        char *name = g_strdup_printf("ipi%d", i);
        sysbus_init_child_obj(obj, name, &s->ipi[i],
                              sizeof(XlnxZynqMPIPI), TYPE_XLNX_ZYNQMP_IPI);
        g_free(name);
    }
}

static void xlnx_zynqmp_pmu_soc_realize(DeviceState *dev, Error **errp)
{
    XlnxZynqMPPMUSoCState *s = XLNX_ZYNQMP_PMU_SOC(dev);
    Error *err = NULL;

    object_property_set_uint(OBJECT(&s->cpu), XLNX_ZYNQMP_PMU_ROM_ADDR,
                             "base-vectors", &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), true, "use-stack-protection",
                             &error_abort);
    object_property_set_uint(OBJECT(&s->cpu), 0, "use-fpu", &error_abort);
    object_property_set_uint(OBJECT(&s->cpu), 0, "use-hw-mul", &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), true, "use-barrel",
                             &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), true, "use-msr-instr",
                             &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), true, "use-pcmp-instr",
                             &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), false, "use-mmu", &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), true, "endianness",
                             &error_abort);
    object_property_set_str(OBJECT(&s->cpu), "8.40.b", "version",
                            &error_abort);
    object_property_set_uint(OBJECT(&s->cpu), 0, "pvr", &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_uint(OBJECT(&s->intc), 0x10, "intc-intr-size",
                             &error_abort);
    object_property_set_uint(OBJECT(&s->intc), 0x0, "intc-level-edge",
                             &error_abort);
    object_property_set_uint(OBJECT(&s->intc), 0xffff, "intc-positive",
                             &error_abort);
    object_property_set_bool(OBJECT(&s->intc), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->intc), 0, XLNX_ZYNQMP_PMU_INTC_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->intc), 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu), MB_CPU_IRQ));

    /* Connect the IPI device */
    for (int i = 0; i < XLNX_ZYNQMP_PMU_NUM_IPIS; i++) {
        object_property_set_bool(OBJECT(&s->ipi[i]), true, "realized",
                                 &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->ipi[i]), 0, ipi_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->ipi[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->intc), ipi_irq[i]));
    }
}

static void xlnx_zynqmp_pmu_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

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
                            sizeof(XlnxZynqMPPMUSoCState),
                            TYPE_XLNX_ZYNQMP_PMU_SOC, &error_abort, NULL);
    object_property_set_bool(OBJECT(pmu), true, "realized", &error_fatal);

    /* Load the kernel */
    microblaze_load_kernel(&pmu->cpu, XLNX_ZYNQMP_PMU_RAM_ADDR,
                           machine->ram_size,
                           machine->initrd_filename,
                           machine->dtb,
                           NULL);
}

static void xlnx_zynqmp_pmu_machine_init(MachineClass *mc)
{
    mc->desc = "Xilinx ZynqMP PMU machine";
    mc->init = xlnx_zynqmp_pmu_init;
}

DEFINE_MACHINE("xlnx-zynqmp-pmu", xlnx_zynqmp_pmu_machine_init)

