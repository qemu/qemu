/*
 * Hexagon Baseboard System emulation.
 *
 * Copyright (c) 2020-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#ifndef HW_HEXAGON_H
#define HW_HEXAGON_H

#include "exec/memory.h"

struct hexagon_board_boot_info {
    uint64_t ram_size;
    const char *kernel_filename;
    uint32_t kernel_elf_flags;
};

typedef enum {
    unknown_rev = 0,
    v66_rev = 0xa666,
    v67_rev = 0x2667,
    v68_rev = 0x8d68,
    v69_rev = 0x8c69,
    v71_rev = 0x8c71,
    v73_rev = 0x8c73,
    v73m_rev = 0xcc73,
} Rev_t;
#define HEXAGON_LATEST_REV v73
#define HEXAGON_LATEST_REV_UPPER V73

/*
 * Config table address bases represent bits [35:16].
 */
#define HEXAGON_CFG_ADDR_BASE(addr) (((addr) >> 16) & 0x0fffff)

#define HEXAGON_CFGSPACE_ENTRIES (128)

typedef  union {
  struct {
    /* Base address of L2TCM space */
    uint32_t l2tcm_base;
    uint32_t reserved0;
    /* Base address of subsystem space */
    uint32_t subsystem_base;
    /* Base address of ETM space */
    uint32_t etm_base;
    /* Base address of L2 configuration space */
    uint32_t l2cfg_base;
    uint32_t reserved1;
    /* Base address of L1S */
    uint32_t l1s0_base;
    /* Base address of AXI2 */
    uint32_t axi2_lowaddr;
    /* Base address of streamer base */
    uint32_t streamer_base;
    uint32_t reserved2;
    /* Base address of fast L2VIC */
    uint32_t fastl2vic_base;
    /* Number of entries in JTLB */
    uint32_t jtlb_size_entries;
    /* Coprocessor type */
    uint32_t coproc_present;
    /* Number of extension execution contexts available */
    uint32_t ext_contexts;
    /* Base address of Hexagon Vector Tightly Coupled Memory (VTCM) */
    uint32_t vtcm_base;
    /* Size of VTCM (in KB) */
    uint32_t vtcm_size_kb;
    /* L2 tag size */
    uint32_t l2tag_size;
    /* Amount of physical L2 memory in released version */
    uint32_t l2ecomem_size;
    /* Hardware threads available on the core */
    uint32_t thread_enable_mask;
    /* Base address of the ECC registers */
    uint32_t eccreg_base;
    /* L2 line size */
    uint32_t l2line_size;
    /* Small Core processor (also implies audio extension) */
    uint32_t tiny_core;
    /* Size of L2TCM */
    uint32_t l2itcm_size;
    /* Base address of L2-ITCM */
    uint32_t l2itcm_base;
    uint32_t reserved3;
    /* DTM is present */
    uint32_t dtm_present;
    /* Version of the DMA */
    uint32_t dma_version;
    /* Native HVX vector length in log of bytes */
    uint32_t hvx_vec_log_length;
    /* Core ID of the multi-core */
    uint32_t core_id;
    /* Number of multi-core cores */
    uint32_t core_count;
    uint32_t coproc2_reg0;
    uint32_t coproc2_reg1;
    /* Supported HVX vector length */
    uint32_t v2x_mode;
    uint32_t coproc2_reg2;
    uint32_t coproc2_reg3;
    uint32_t coproc2_reg4;
    uint32_t coproc2_reg5;
    uint32_t coproc2_reg6;
    uint32_t coproc2_reg7;
    /* Voltage droop mitigation technique parameter */
    uint32_t acd_preset;
    /* Voltage droop mitigation technique parameter */
    uint32_t mnd_preset;
    /* L1 data cache size (in KB) */
    uint32_t l1d_size_kb;
    /* L1 instruction cache size in (KB) */
    uint32_t l1i_size_kb;
    /* L1 data cache write policy: see HexagonL1WritePolicy */
    uint32_t l1d_write_policy;
    /* VTCM bank width  */
    uint32_t vtcm_bank_width;
    uint32_t reserved4;
    uint32_t reserved5;
    uint32_t reserved6;
    uint32_t coproc2_cvt_mpy_size;
    uint32_t consistency_domain;
    uint32_t capacity_domain;
    uint32_t axi3_lowaddr;
    uint32_t coproc2_int8_subcolumns;
    uint32_t corecfg_present;
    uint32_t coproc2_fp16_acc_exp;
    uint32_t AXIM2_secondary_base;
  };
  uint32_t raw[HEXAGON_CFGSPACE_ENTRIES];
} hexagon_config_table;

typedef struct {
    /* Base address of config table */
    uint32_t cfgbase;
    /* Size of L2 TCM */
    uint32_t l2tcm_size;
    /* Base address of L2VIC */
    uint32_t l2vic_base;
    /* Size of L2VIC region */
    uint32_t l2vic_size;
    /* QTimer csr base */
    uint32_t csr_base;
    uint32_t qtmr_rg0;
    uint32_t qtmr_rg1;
    hexagon_config_table cfgtable;
} hexagon_machine_config;

#endif
