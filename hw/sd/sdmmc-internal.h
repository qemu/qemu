/*
 * SD/MMC cards common
 *
 * Copyright (c) 2018  Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SDMMC_INTERNAL_H
#define SDMMC_INTERNAL_H

#define TYPE_SDMMC_COMMON "sdmmc-common"
DECLARE_OBJ_CHECKERS(SDState, SDCardClass, SDMMC_COMMON, TYPE_SDMMC_COMMON)

/*
 * EXT_CSD Modes segment
 *
 * Define the configuration the Device is working in.
 * These modes can be changed by the host by means of the SWITCH command.
 */
#define EXT_CSD_CMDQ_MODE_EN            15      /* R/W */
#define EXT_CSD_FLUSH_CACHE             32      /* W */
#define EXT_CSD_CACHE_CTRL              33      /* R/W */
#define EXT_CSD_POWER_OFF_NOTIFICATION  34      /* R/W */
#define EXT_CSD_PACKED_FAILURE_INDEX    35      /* RO */
#define EXT_CSD_PACKED_CMD_STATUS       36      /* RO */
#define EXT_CSD_EXP_EVENTS_STATUS       54      /* RO, 2 bytes */
#define EXT_CSD_EXP_EVENTS_CTRL         56      /* R/W, 2 bytes */
#define EXT_CSD_CLASS_6_CTRL            59
#define EXT_CSD_INI_TIMEOUT_EMU         60
#define EXT_CSD_DATA_SECTOR_SIZE        61      /* R */
#define EXT_CSD_USE_NATIVE_SECTOR       62
#define EXT_CSD_NATIVE_SECTOR_SIZE      63
#define EXT_CSD_VENDOR_SPECIFIC_FIELD   64      /* 64 bytes */
#define EXT_CSD_PROGRAM_CID_CSD_DDR_SUPPORT 130
#define EXT_CSD_PERIODIC_WAKEUP         131
#define EXT_CSD_TCASE_SUPPORT           132
#define EXT_CSD_SEC_BAD_BLK_MGMNT       134
#define EXT_CSD_GP_SIZE_MULT            143     /* R/W */
#define EXT_CSD_PARTITION_SETTING_COMPLETED 155 /* R/W */
#define EXT_CSD_PARTITION_ATTRIBUTE     156     /* R/W */
#define EXT_CSD_MAX_ENH_SIZE_MULT       157     /* RO, 3 bytes */
#define EXT_CSD_PARTITION_SUPPORT       160     /* RO */
#define EXT_CSD_HPI_MGMT                161     /* R/W */
#define EXT_CSD_RST_N_FUNCTION          162     /* R/W */
#define EXT_CSD_BKOPS_EN                163     /* R/W */
#define EXT_CSD_BKOPS_START             164     /* W */
#define EXT_CSD_SANITIZE_START          165     /* W */
#define EXT_CSD_WR_REL_PARAM            166     /* RO */
#define EXT_CSD_WR_REL_SET              167
#define EXT_CSD_RPMB_MULT               168     /* RO */
#define EXT_CSD_FW_CONFIG               169     /* R/W */
#define EXT_CSD_USER_WP                 171
#define EXT_CSD_BOOT_WP                 173     /* R/W */
#define EXT_CSD_BOOT_WP_STATUS          174
#define EXT_CSD_ERASE_GROUP_DEF         175     /* R/W */
#define EXT_CSD_BOOT_BUS_CONDITIONS     177
#define EXT_CSD_BOOT_CONFIG_PROT        178
#define EXT_CSD_PART_CONFIG             179     /* R/W */
#define EXT_CSD_ERASED_MEM_CONT         181     /* RO */
#define EXT_CSD_BUS_WIDTH               183     /* R/W */
#define EXT_CSD_STROBE_SUPPORT          184     /* RO */
#define EXT_CSD_HS_TIMING               185     /* R/W */
#define EXT_CSD_POWER_CLASS             187     /* R/W */
#define EXT_CSD_CMD_SET_REV             189
#define EXT_CSD_CMD_SET                 191
/*
 * EXT_CSD Properties segment
 *
 * Define the Device capabilities, cannot be modified by the host.
 */
#define EXT_CSD_REV                     192
#define EXT_CSD_STRUCTURE               194
#define EXT_CSD_CARD_TYPE               196
#define EXT_CSD_DRIVER_STRENGTH         197
#define EXT_CSD_OUT_OF_INTERRUPT_TIME   198
#define EXT_CSD_PART_SWITCH_TIME        199
#define EXT_CSD_PWR_CL_52_195           200
#define EXT_CSD_PWR_CL_26_195           201
#define EXT_CSD_PWR_CL_52_360           202
#define EXT_CSD_PWR_CL_26_360           203
#define EXT_CSD_SEC_CNT                 212     /* 4 bytes */
#define EXT_CSD_S_A_TIMEOUT             217
#define EXT_CSD_S_C_VCCQ                219
#define EXT_CSD_S_C_VCC                 220
#define EXT_CSD_REL_WR_SEC_C            222
#define EXT_CSD_HC_WP_GRP_SIZE          221
#define EXT_CSD_ERASE_TIMEOUT_MULT      223
#define EXT_CSD_HC_ERASE_GRP_SIZE       224
#define EXT_CSD_ACC_SIZE                225
#define EXT_CSD_BOOT_MULT               226
#define EXT_CSD_BOOT_INFO               228
#define EXT_CSD_SEC_FEATURE_SUPPORT     231
#define EXT_CSD_TRIM_MULT               232
#define EXT_CSD_INI_TIMEOUT_PA          241
#define EXT_CSD_BKOPS_STATUS            246
#define EXT_CSD_POWER_OFF_LONG_TIME     247
#define EXT_CSD_GENERIC_CMD6_TIME       248
#define EXT_CSD_CACHE_SIZE              249     /* 4 bytes */
#define EXT_CSD_EXT_SUPPORT             494
#define EXT_CSD_LARGE_UNIT_SIZE_M1      495
#define EXT_CSD_CONTEXT_CAPABILITIES    496
#define EXT_CSD_TAG_RES_SIZE            497
#define EXT_CSD_TAG_UNIT_SIZE           498
#define EXT_CSD_DATA_TAG_SUPPORT        499
#define EXT_CSD_MAX_PACKED_WRITES       500
#define EXT_CSD_MAX_PACKED_READS        501
#define EXT_CSD_BKOPS_SUPPORT           502
#define EXT_CSD_HPI_FEATURES            503
#define EXT_CSD_S_CMD_SET               504

#define EXT_CSD_WR_REL_PARAM_EN                 (1 << 2)
#define EXT_CSD_WR_REL_PARAM_EN_RPMB_REL_WR     (1 << 4)

#define EXT_CSD_PART_CONFIG_ACC_MASK            (0x7)
#define EXT_CSD_PART_CONFIG_ACC_DEFAULT         (0x0)
#define EXT_CSD_PART_CONFIG_ACC_BOOT0           (0x1)

#define EXT_CSD_PART_CONFIG_EN_MASK             (0x7 << 3)
#define EXT_CSD_PART_CONFIG_EN_BOOT0            (0x1 << 3)
#define EXT_CSD_PART_CONFIG_EN_USER             (0x7 << 3)

#endif
