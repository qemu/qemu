/*
 * Models the v73 CDSP0 of the SA8775P.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/boards.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/qcom_dsp.h"
#include "target/hexagon/cpu.h"

#include "machine_cfg_sa8775_cdsp0.h.inc"

#define TYPE_SA8775P_CDSP_MACHINE MACHINE_TYPE_NAME("sa8775p-cdsp")

static const QcomDspRegion sa8775p_cdsp_regions[] = {
    { "tcsr", 0x01fc0000 },
    { "hwkm-prng", 0x010da000 },
    { "gdscr", 0x00151000 },
    { "pll0", 0x26340000 },
    { "pll1", 0x26000000 },
    { "clkctl", 0x26348000 },
    { "ccswi", 0x26008000 },
    { "rsc", 0x260a4000 },
};

static void sa8775p_cdsp_init_devices(MachineState *ms)
{
    qcom_dsp_add_regions(ms, sa8775p_cdsp_regions,
                         ARRAY_SIZE(sa8775p_cdsp_regions));

    qcom_soc_add_qup_geni_ports("sa8775p", 0x00b80000, 1, 0x4000);
    qcom_soc_add_qup_geni_ports("sa8775p", 0x00880000, 7, 0x4000);
    qcom_soc_add_qup_geni_ports("sa8775p", 0x00980000, 7, 0x4000);
    qcom_soc_add_qup_geni_ports("sa8775p", 0x00a80000, 7, 0x4000);
}

static const QcomDspMachineInfo sa8775p_cdsp_info = {
    .desc = "SA8775P CDSP0",
    .cpu_type = HEXAGON_CPU_TYPE_NAME("v73"),
    .cpus = 6,
    .rev = v73_rev,
    .cfg = &SA8775P_cdsp0,
    .kernel_addr = 0xa0000000,
    .init_devices = sa8775p_cdsp_init_devices,
};

static const TypeInfo sa8775p_cdsp_types[] = {
    {
        .name = TYPE_SA8775P_CDSP_MACHINE,
        .parent = TYPE_QCOM_DSP_MACHINE,
        .class_init = qcom_dsp_class_init,
        .class_data = &sa8775p_cdsp_info,
    },
};

DEFINE_TYPES(sa8775p_cdsp_types)
