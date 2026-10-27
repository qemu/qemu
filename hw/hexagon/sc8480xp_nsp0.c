/*
 * Models the Hexagon v81 NSP of the SC8480XP.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/boards.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/qcom_dsp.h"
#include "target/hexagon/cpu.h"

#include "machine_cfg_sc8480xp_nsp0.h.inc"

#define TYPE_SC8480XP_NSP0_MACHINE MACHINE_TYPE_NAME("sc8480xp-nsp0")

static const QcomDspRegion sc8480xp_nsp0_regions[] = {
    { "tcsr", 0x01f40000, 0xc0000 },
    { "hwkm-prng", 0x010ca000 },
    { "pll", 0x32340000 },
    { "clkctl", 0x32344000, 0x4000 },
    { "turing-cc", 0x32008000, 0x14000 },
    { "rsc", 0x320a4000 },
    { "dsp-rsc", 0x323c0000 },
    { "ipcc", 0x03e00000 },
    { "ipcc-legacy", 0x32380000 },
    { "pinctrl", 0x0f100000, 0x0f00000 },
    { "gcc", 0x00100000, 0x200000 },
    { "turing-cc-pll", 0x32000000, 0x8000 },
    { "q6-acd", 0x32350000, 0x800 },
    { "hmx-pll", 0x32348000 },
    { "hmx-cc", 0x3234c000, 0x4000 },
    { "hmx-acd", 0x32350800, 0x800 },
    { "stmtrace", 0x16000000, 0x1000000 },
    { "stmcfg", 0x10002000 },
    { "etb", 0x11c05000 },
    { "tpdm", 0x11181000, 0x6000 },
    { "tpda", 0x11188000 },
    { "funnel", 0x10041000 },
    { "funnel", 0x11c04000 },
    { "llcc", 0x20400000, 0x100000 },
    { "llcc", 0x20600000, 0x100000 },
    { "llcc", 0x21800000, 0x100000 },
    { "llcc", 0x21a00000, 0x100000 },
    { "llcc", 0x21c00000, 0x100000 },
    { "llcc", 0x21e00000, 0x100000 },
    { "llcc", 0x22800000, 0x100000 },
    { "llcc", 0x22a00000, 0x100000 },
    { "llcc", 0x22c00000, 0x100000 },
    { "llcc", 0x22e00000, 0x100000 },
    { "llcc", 0x23800000, 0x100000 },
    { "llcc", 0x23a00000, 0x100000 },
    { "llcc", 0x23c00000, 0x100000 },
    { "llcc", 0x23e00000, 0x100000 },
    { "ddrss", 0x20280000, 0x400 },
};

static void sc8480xp_nsp0_init_devices(MachineState *ms)
{
    qcom_dsp_add_regions(ms, sc8480xp_nsp0_regions,
                         ARRAY_SIZE(sc8480xp_nsp0_regions));

    qcom_soc_add_qup_geni_ports("sc8480xp", 0x07980000, 15, 0x4000);
    qcom_soc_add_qup_geni_ports("sc8480xp", 0x00b80000, 8, 0x4000);
    qcom_soc_add_qup_geni_ports("sc8480xp", 0x00a80000, 8, 0x4000);
    qcom_soc_add_qup_geni_ports("sc8480xp", 0x00980000, 2, 0x4000);
    qcom_soc_add_qup_geni_ports("sc8480xp", 0x00880000, 8, 0x4000);
}

static const QcomDspMachineInfo sc8480xp_nsp0_info = {
    .desc = "SC8480XP NSP",
    .cpu_type = HEXAGON_CPU_TYPE_NAME("v81"),
    .cpus = 12,
    .rev = v81_sc8480xp_rev,
    .cfg = &sc8480xp_nsp0,
    .kernel_addr = 0xa0000000,
    .init_devices = sc8480xp_nsp0_init_devices,
};

static const TypeInfo sc8480xp_nsp0_types[] = {
    {
        .name = TYPE_SC8480XP_NSP0_MACHINE,
        .parent = TYPE_QCOM_DSP_MACHINE,
        .class_init = qcom_dsp_class_init,
        .class_data = &sc8480xp_nsp0_info,
    },
};

DEFINE_TYPES(sc8480xp_nsp0_types)
