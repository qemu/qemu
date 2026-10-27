/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

/* Config table of the SC8480XP NSP0. */
static const struct hexagon_machine_config sc8480xp_nsp0 = {
    .ddr_base = 0x80000000,
    .cfgbase = 0x30180000,
    .l2vic_base = 0x32390000,
    .l2vic_size = 0x1000,
    .csr_base = 0x323a0000,
    .qtmr_region = 0x323a1000,
    .cfgtable = {
        .l2tcm_base = 0x3000,
        .subsystem_base = 0x3238,
        .etm_base = 0x3019,
        .l2cfg_base = 0x301a,
        .l1s0_base = 0x3100,
        .fastl2vic_base = 0x301e,
        .jtlb_size_entries = 0x80,
        .coproc_present = 0xd,
        .ext_contexts = 8,
        .vtcm_base = 0x3100,
        .vtcm_size_kb = 0x2000,
        .l2tag_size = 0x400,
        .l2ecomem_size = 0x400,
        .thread_enable_mask = 0xfff,
        .l2line_size = 0x80,
        .l2itcm_base = 0xd800,
        .dma_version = 8,
        .hvx_vec_log_length = 7,
        .coproc2_reg0 = 0x10,
        .coproc2_reg1 = 8,
        .acd_preset = 1,
        .mnd_preset = 1,
        .l1d_size_kb = 0x40,
        .l1i_size_kb = 0x40,
        .l1d_write_policy = 2,
        .vtcm_bank_width = 0x80,
        .reserved4 = 1,
        .axi3_lowaddr = 0x4000,
        .corecfg_present = 0xd,
        .coproc2_fp16_acc_exp = 1,
        .AXIM_data_width = 0x100,
        .l1d_ways = 8,
        .l1i_ways = 4,
        .l2way_num = 8,
        .dma_jtlb_entries = 0x40,
    },
};
