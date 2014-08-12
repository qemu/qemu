/*
 * Copyright (C) 2010-2011 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UC32_SYSCALL_H__
#define __UC32_SYSCALL_H__
struct target_pt_regs {
    abi_ulong uregs[34];
};

#define UC32_REG_pc             uregs[31]
#define UC32_REG_lr             uregs[30]
#define UC32_REG_sp             uregs[29]
#define UC32_REG_ip             uregs[28]
#define UC32_REG_fp             uregs[27]
#define UC32_REG_26             uregs[26]
#define UC32_REG_25             uregs[25]
#define UC32_REG_24             uregs[24]
#define UC32_REG_23             uregs[23]
#define UC32_REG_22             uregs[22]
#define UC32_REG_21             uregs[21]
#define UC32_REG_20             uregs[20]
#define UC32_REG_19             uregs[19]
#define UC32_REG_18             uregs[18]
#define UC32_REG_17             uregs[17]
#define UC32_REG_16             uregs[16]
#define UC32_REG_15             uregs[15]
#define UC32_REG_14             uregs[14]
#define UC32_REG_13             uregs[13]
#define UC32_REG_12             uregs[12]
#define UC32_REG_11             uregs[11]
#define UC32_REG_10             uregs[10]
#define UC32_REG_09             uregs[9]
#define UC32_REG_08             uregs[8]
#define UC32_REG_07             uregs[7]
#define UC32_REG_06             uregs[6]
#define UC32_REG_05             uregs[5]
#define UC32_REG_04             uregs[4]
#define UC32_REG_03             uregs[3]
#define UC32_REG_02             uregs[2]
#define UC32_REG_01             uregs[1]
#define UC32_REG_00             uregs[0]
#define UC32_REG_asr            uregs[32]
#define UC32_REG_ORIG_00        uregs[33]

#define UC32_SYSCALL_BASE               0x900000
#define UC32_SYSCALL_ARCH_BASE          0xf0000
#define UC32_SYSCALL_NR_set_tls         (UC32_SYSCALL_ARCH_BASE + 5)

#define UNAME_MACHINE "UniCore-II"
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_MINSIGSTKSZ 2048

#endif /* __UC32_SYSCALL_H__ */
