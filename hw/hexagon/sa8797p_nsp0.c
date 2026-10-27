/*
 * Models the v81 NSP0 of the SA8797P.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/boards.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/qcom_dsp.h"
#include "target/hexagon/cpu.h"

#include "machine_cfg_sa8797p_nsp0.h.inc"

#define TYPE_SA8797P_NSP0_MACHINE MACHINE_TYPE_NAME("sa8797p-nsp0")

static const QcomDspRegion sa8797p_nsp0_regions[] = {
    { "gcc", 0x00100000, 0x200000 },
    { "ne-gcc", 0x08900000, 0x100000 },
    { "se-gcc", 0x08a00000, 0x100000 },
    { "nw-gcc", 0x08b00000, 0x100000 },
    { "nsp-cc", 0x320c0000, 0x10000 },
    { "qfprom", 0x360c8000, 0x8000 },
    { "q6-cc", 0x32340000, 0x11000 },
    { "pinctrl", 0x0f100000, 0x0f00000 },
    { "ipcc", 0x09000000 },
    { "ipcc-legacy", 0x32380000 },
    { "stmtrace", 0x16000000, 0x1000000 },
    { "tpdm", 0x117c0000, 0x2000 },
    { "tpdm", 0x117c5000, 0x2000 },
    { "tpda", 0x117c8000 },
};

static void sa8797p_nsp0_init_devices(MachineState *ms)
{
    qcom_dsp_add_regions(ms, sa8797p_nsp0_regions,
                         ARRAY_SIZE(sa8797p_nsp0_regions));

    qcom_soc_add_qup_geni_ports("sa8797p", 0x09b86000, 5, 0x2000);
    qcom_soc_add_qup_geni_ports("sa8797p", 0x09c06000, 5, 0x2000);
    qcom_soc_add_qup_geni_ports("sa8797p", 0x00c80000, 1, 0x4000);
    qcom_soc_add_qup_geni_ports("sa8797p", 0x00a80000, 7, 0x4000);
    qcom_soc_add_qup_geni_ports("sa8797p", 0x00980000, 7, 0x4000);
    qcom_soc_add_qup_geni_ports("sa8797p", 0x00880000, 7, 0x4000);
    qcom_soc_add_qup_geni_ports("sa8797p", 0x198880000, 6, 0x4000);
}

static const QcomDspMachineInfo sa8797p_nsp0_info = {
    .desc = "SA8797P NSP",
    .cpu_type = HEXAGON_CPU_TYPE_NAME("v81"),
    .cpus = 12,
    .rev = v81_rev,
    .cfg = &sa8797p_nsp0,
    .kernel_addr = 0xa0000000,
    .init_devices = sa8797p_nsp0_init_devices,
};

static const TypeInfo sa8797p_nsp0_types[] = {
    {
        .name = TYPE_SA8797P_NSP0_MACHINE,
        .parent = TYPE_QCOM_DSP_MACHINE,
        .class_init = qcom_dsp_class_init,
        .class_data = &sa8797p_nsp0_info,
    },
};

DEFINE_TYPES(sa8797p_nsp0_types)
