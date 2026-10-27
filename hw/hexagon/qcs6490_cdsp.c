/*
 * QCS6490 CDSP machine.
 *
 * Models the Hexagon v68 CDSP subsystem of the QCS6490/SM7325.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/boards.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/qcom_dsp.h"
#include "hw/misc/qcom-qdsp6ss-clk.h"
#include "hw/misc/unimp.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "target/hexagon/cpu.h"

#include "machine_cfg_qcs6490_cdsp.h.inc"

#define TYPE_QCS6490_CDSP_MACHINE MACHINE_TYPE_NAME("qcs6490-cdsp")

#define QCS6490_CFGSPACE_BASE 0x09990000
#define QCS6490_CFGSPACE_SIZE 0x00070000

static const QcomDspRegion qcs6490_regions[] = {
    { "ss-csr", 0x0a380000, 0x10000 },
};

static void qcs6490_cdsp_init_devices(MachineState *ms)
{
    MemoryRegion *cfgspace = g_new(MemoryRegion, 1);

    memory_region_init_ram(cfgspace, NULL, "cfgspace.ram",
                           QCS6490_CFGSPACE_SIZE, &error_fatal);
    memory_region_add_subregion_overlap(get_system_memory(),
                                        QCS6490_CFGSPACE_BASE, cfgspace, -1);
    qcom_dsp_add_regions(ms, qcs6490_regions, ARRAY_SIZE(qcs6490_regions));
    qcom_dsp_add_device(TYPE_QCOM_LUCID_PLL, 0x0a001000);
    qcom_dsp_add_device(TYPE_QCOM_LUCID_PLL, 0x0a340000);
    qcom_dsp_add_device(TYPE_QCOM_RCG, 0x0a341000);
}

static const QcomDspMachineInfo qcs6490_cdsp_info = {
    .desc = "QCS6490 CDSP",
    .cpu_type = HEXAGON_CPU_TYPE_NAME("v68"),
    .cpus = 6,
    .rev = v68_qcs6490_rev,
    .cfg = &qcs6490_cdsp,
    .kernel_addr = 0xa1000000,
    .init_devices = qcs6490_cdsp_init_devices,
};

static const TypeInfo qcs6490_cdsp_types[] = {
    {
        .name = TYPE_QCS6490_CDSP_MACHINE,
        .parent = TYPE_QCOM_DSP_MACHINE,
        .class_init = qcom_dsp_class_init,
        .class_data = &qcs6490_cdsp_info,
    },
};

DEFINE_TYPES(qcs6490_cdsp_types)
