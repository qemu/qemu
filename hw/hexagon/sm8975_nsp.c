/*
 * sm8975-nsp machine.
 *
 * Models the Hexagon v85 NSP of the SM8975.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/boards.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/qcom_dsp.h"
#include "target/hexagon/cpu.h"

#include "machine_cfg_sm8975_nsp.h.inc"

#define TYPE_SM8975_NSP_MACHINE MACHINE_TYPE_NAME("sm8975-nsp")

static const QcomDspRegion sm8975_nsp_regions[] = {
    { "dspss-pub", 0x26300000, 0x10000 },
    { "wdog", 0x26384000 },
    { "turing-cc", 0x26008000, 0x14000 },
    { "pll", 0x26340000 },
    { "clkctl", 0x26344000, 0x4000 },
    { "compute-rsc", 0x260a4000 },
    { "qdsp6-rsc", 0x263c0000 },
    { "ipcc", 0x01100000 },
    { "tcsr", 0x26080000, 0x1f000 },
    { "turing-cc-pll", 0x26000000 },
    { "turing-cc-pll", 0x26001000 },
    { "turing-cc-pll", 0x26002000 },
    { "turing-cc-pll", 0x26003000 },
    { "turing-cc-pll", 0x26004000 },
    { "ahb2phy", 0x26006000, 0x2000 },
    { "ahbe-time", 0x260b7000 },
    { "nsp-noc", 0x260c0000, 0x21280 },
    { "ceng", 0x26260000, 0xc000 },
    { "ceng-irq", 0x2626c000 },
    { "ubwcd", 0x26280000, 0x10000 },
    { "vapss", 0x262c0000, 0x40000 },
    { "llm-isense", 0x26310000, 0x10000 },
    { "lpm-stats", 0x26320000, 0x10000 },
    { "mxu-pll", 0x26348000 },
    { "acd-mnd-dcc", 0x26350000, 0x800 },
    { "hmx-acd-mnd-dcc", 0x26350800, 0x800 },
    { "gcc", 0x00100000, 0x200000 },
    { "fuse", 0x221c8000 },
    { "hmx-swman", 0x2634a000, 0x400 },
    { "hmx-broadcast", 0x2634b000, 0x400 },
    { "hmx-cc", 0x2634c000, 0x4000 },
    { "ipcc-legacy", 0x26380000 },
    { "pinctrl", 0x0f100000, 0x0f00000 },
    { "llcc", 0x31800000, 0x200000 },
    { "llcc", 0x31c00000, 0x200000 },
    { "llcc", 0x32800000, 0x200000 },
    { "llcc", 0x32c00000, 0x200000 },
    { "llcc", 0x34800000, 0x200000 },
    { "llcc", 0x34c00000, 0x200000 },
    { "stmtrace", 0x16000000, 0x1000000 },
    { "stmcfg", 0x10002000 },
    { "etb", 0x11305000 },
    { "tpdm", 0x11181000, 0x2000 },
    { "tpdm", 0x11185000, 0x2000 },
    { "funnel", 0x10041000 },
    { "funnel", 0x11304000 },
    { "tpda", 0x11188000 },
};

static void sm8975_nsp_init_devices(MachineState *ms)
{
    qcom_dsp_add_regions(ms, sm8975_nsp_regions,
                         ARRAY_SIZE(sm8975_nsp_regions));

    qcom_soc_add_qup_geni_ports("sm8975", 0x08180000, 8, 0x4000);
    qcom_soc_add_qup_geni_ports("sm8975", 0x08280000, 8, 0x4000);
    qcom_soc_add_qup_geni_ports("sm8975", 0x01980000, 6, 0x4000);
    qcom_soc_add_qup_geni_ports("sm8975", 0x01a80000, 5, 0x4000);
    qcom_soc_add_qup_geni_ports("sm8975", 0x00a80000, 8, 0x4000);
    qcom_soc_add_qup_geni_ports("sm8975", 0x00880000, 5, 0x4000);
}

static const QcomDspMachineInfo sm8975_nsp_info = {
    .desc = "SM8975 NSP",
    .cpu_type = HEXAGON_CPU_TYPE_NAME("v85"),
    .cpus = 12,
    .rev = v85_sm8975_rev,
    .cfg = &sm8975_nsp,
    .kernel_addr = 0xa0000000,
    .init_devices = sm8975_nsp_init_devices,
};

static const TypeInfo sm8975_nsp_types[] = {
    {
        .name = TYPE_SM8975_NSP_MACHINE,
        .parent = TYPE_QCOM_DSP_MACHINE,
        .class_init = qcom_dsp_class_init,
        .class_data = &sm8975_nsp_info,
    },
};

DEFINE_TYPES(sm8975_nsp_types)
