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
#include "qemu-common.h"
#include "hw/boards.h"
#include "cpu.h"

/* Define the PMU device */

#define TYPE_XLNX_ZYNQMP_PMU_SOC "xlnx,zynqmp-pmu-soc"
#define XLNX_ZYNQMP_PMU_SOC(obj) OBJECT_CHECK(XlnxZynqMPPMUSoCState, (obj), \
                                              TYPE_XLNX_ZYNQMP_PMU_SOC)

typedef struct XlnxZynqMPPMUSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
}  XlnxZynqMPPMUSoCState;

static void xlnx_zynqmp_pmu_soc_init(Object *obj)
{

}

static void xlnx_zynqmp_pmu_soc_realize(DeviceState *dev, Error **errp)
{

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

}

static void xlnx_zynqmp_pmu_machine_init(MachineClass *mc)
{
    mc->desc = "Xilinx ZynqMP PMU machine";
    mc->init = xlnx_zynqmp_pmu_init;
}

DEFINE_MACHINE("xlnx-zynqmp-pmu", xlnx_zynqmp_pmu_machine_init)

