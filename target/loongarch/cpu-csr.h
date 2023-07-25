/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch CSRs
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_CPU_CSR_H
#define LOONGARCH_CPU_CSR_H

#include "hw/registerfields.h"

/* Based on kernel definitions: arch/loongarch/include/asm/loongarch.h */

/* Basic CSRs */
#define LOONGARCH_CSR_CRMD           0x0 /* Current mode info */

#define LOONGARCH_CSR_PRMD           0x1 /* Prev-exception mode info */
FIELD(CSR_PRMD, PPLV, 0, 2)
FIELD(CSR_PRMD, PIE, 2, 1)
FIELD(CSR_PRMD, PWE, 3, 1)

#define LOONGARCH_CSR_EUEN           0x2 /* Extended unit enable */
FIELD(CSR_EUEN, FPE, 0, 1)
FIELD(CSR_EUEN, SXE, 1, 1)
FIELD(CSR_EUEN, ASXE, 2, 1)
FIELD(CSR_EUEN, BTE, 3, 1)

#define LOONGARCH_CSR_MISC           0x3 /* Misc config */
FIELD(CSR_MISC, VA32, 0, 4)
FIELD(CSR_MISC, DRDTL, 4, 4)
FIELD(CSR_MISC, RPCNTL, 8, 4)
FIELD(CSR_MISC, ALCL, 12, 4)
FIELD(CSR_MISC, DWPL, 16, 3)

#define LOONGARCH_CSR_ECFG           0x4 /* Exception config */
FIELD(CSR_ECFG, LIE, 0, 13)
FIELD(CSR_ECFG, VS, 16, 3)

#define LOONGARCH_CSR_ESTAT          0x5 /* Exception status */
FIELD(CSR_ESTAT, IS, 0, 13)
FIELD(CSR_ESTAT, ECODE, 16, 6)
FIELD(CSR_ESTAT, ESUBCODE, 22, 9)

#define LOONGARCH_CSR_ERA            0x6 /* Exception return address */

#define LOONGARCH_CSR_BADV           0x7 /* Bad virtual address */

#define LOONGARCH_CSR_BADI           0x8 /* Bad instruction */

#define LOONGARCH_CSR_EENTRY         0xc /* Exception entry address */

/* TLB related CSRs */
#define LOONGARCH_CSR_TLBIDX         0x10 /* TLB Index, EHINV, PageSize, NP */
FIELD(CSR_TLBIDX, INDEX, 0, 12)
FIELD(CSR_TLBIDX, PS, 24, 6)
FIELD(CSR_TLBIDX, NE, 31, 1)

#define LOONGARCH_CSR_TLBEHI         0x11 /* TLB EntryHi */
FIELD(CSR_TLBEHI, VPPN, 13, 35)

#define LOONGARCH_CSR_TLBELO0        0x12 /* TLB EntryLo0 */
#define LOONGARCH_CSR_TLBELO1        0x13 /* TLB EntryLo1 */
FIELD(TLBENTRY, V, 0, 1)
FIELD(TLBENTRY, D, 1, 1)
FIELD(TLBENTRY, PLV, 2, 2)
FIELD(TLBENTRY, MAT, 4, 2)
FIELD(TLBENTRY, G, 6, 1)
FIELD(TLBENTRY, PPN, 12, 36)
FIELD(TLBENTRY, NR, 61, 1)
FIELD(TLBENTRY, NX, 62, 1)
FIELD(TLBENTRY, RPLV, 63, 1)

#define LOONGARCH_CSR_ASID           0x18 /* Address space identifier */
FIELD(CSR_ASID, ASID, 0, 10)
FIELD(CSR_ASID, ASIDBITS, 16, 8)

/* Page table base address when badv[47] = 0 */
#define LOONGARCH_CSR_PGDL           0x19
/* Page table base address when badv[47] = 1 */
#define LOONGARCH_CSR_PGDH           0x1a

#define LOONGARCH_CSR_PGD            0x1b /* Page table base address */

/* Page walk controller's low addr */
#define LOONGARCH_CSR_PWCL           0x1c
FIELD(CSR_PWCL, PTBASE, 0, 5)
FIELD(CSR_PWCL, PTWIDTH, 5, 5)
FIELD(CSR_PWCL, DIR1_BASE, 10, 5)
FIELD(CSR_PWCL, DIR1_WIDTH, 15, 5)
FIELD(CSR_PWCL, DIR2_BASE, 20, 5)
FIELD(CSR_PWCL, DIR2_WIDTH, 25, 5)
FIELD(CSR_PWCL, PTEWIDTH, 30, 2)

/* Page walk controller's high addr */
#define LOONGARCH_CSR_PWCH           0x1d
FIELD(CSR_PWCH, DIR3_BASE, 0, 6)
FIELD(CSR_PWCH, DIR3_WIDTH, 6, 6)
FIELD(CSR_PWCH, DIR4_BASE, 12, 6)
FIELD(CSR_PWCH, DIR4_WIDTH, 18, 6)

#define LOONGARCH_CSR_STLBPS         0x1e /* Stlb page size */
FIELD(CSR_STLBPS, PS, 0, 5)

#define LOONGARCH_CSR_RVACFG         0x1f /* Reduced virtual address config */
FIELD(CSR_RVACFG, RBITS, 0, 4)

/* Config CSRs */
#define LOONGARCH_CSR_CPUID          0x20 /* CPU core id */

#define LOONGARCH_CSR_PRCFG1         0x21 /* Config1 */
FIELD(CSR_PRCFG1, SAVE_NUM, 0, 4)
FIELD(CSR_PRCFG1, TIMER_BITS, 4, 8)
FIELD(CSR_PRCFG1, VSMAX, 12, 3)

#define LOONGARCH_CSR_PRCFG2         0x22 /* Config2 */

#define LOONGARCH_CSR_PRCFG3         0x23 /* Config3 */
FIELD(CSR_PRCFG3, TLB_TYPE, 0, 4)
FIELD(CSR_PRCFG3, MTLB_ENTRY, 4, 8)
FIELD(CSR_PRCFG3, STLB_WAYS, 12, 8)
FIELD(CSR_PRCFG3, STLB_SETS, 20, 8)

/*
 * Save registers count can read from PRCFG1.SAVE_NUM
 * The Min count is 1. Max count is 15.
 */
#define LOONGARCH_CSR_SAVE(N)        (0x30 + N)

/* Timer CSRs */
#define LOONGARCH_CSR_TID            0x40 /* Timer ID */

#define LOONGARCH_CSR_TCFG           0x41 /* Timer config */
FIELD(CSR_TCFG, EN, 0, 1)
FIELD(CSR_TCFG, PERIODIC, 1, 1)
FIELD(CSR_TCFG, INIT_VAL, 2, 46)

#define LOONGARCH_CSR_TVAL           0x42 /* Timer ticks remain */

#define LOONGARCH_CSR_CNTC           0x43 /* Timer offset */

#define LOONGARCH_CSR_TICLR          0x44 /* Timer interrupt clear */

/* LLBCTL CSRs */
#define LOONGARCH_CSR_LLBCTL         0x60 /* LLBit control */
FIELD(CSR_LLBCTL, ROLLB, 0, 1)
FIELD(CSR_LLBCTL, WCLLB, 1, 1)
FIELD(CSR_LLBCTL, KLO, 2, 1)

/* Implement dependent */
#define LOONGARCH_CSR_IMPCTL1        0x80 /* LoongArch config1 */

#define LOONGARCH_CSR_IMPCTL2        0x81 /* LoongArch config2*/

/* TLB Refill CSRs */
#define LOONGARCH_CSR_TLBRENTRY      0x88 /* TLB refill exception address */
#define LOONGARCH_CSR_TLBRBADV       0x89 /* TLB refill badvaddr */
#define LOONGARCH_CSR_TLBRERA        0x8a /* TLB refill ERA */
#define LOONGARCH_CSR_TLBRSAVE       0x8b /* KScratch for TLB refill */
FIELD(CSR_TLBRERA, ISTLBR, 0, 1)
FIELD(CSR_TLBRERA, PC, 2, 62)
#define LOONGARCH_CSR_TLBRELO0       0x8c /* TLB refill entrylo0 */
#define LOONGARCH_CSR_TLBRELO1       0x8d /* TLB refill entrylo1 */
#define LOONGARCH_CSR_TLBREHI        0x8e /* TLB refill entryhi */
FIELD(CSR_TLBREHI, PS, 0, 6)
FIELD(CSR_TLBREHI, VPPN, 13, 35)
#define LOONGARCH_CSR_TLBRPRMD       0x8f /* TLB refill mode info */
FIELD(CSR_TLBRPRMD, PPLV, 0, 2)
FIELD(CSR_TLBRPRMD, PIE, 2, 1)
FIELD(CSR_TLBRPRMD, PWE, 4, 1)

/* Machine Error CSRs */
#define LOONGARCH_CSR_MERRCTL        0x90 /* ERRCTL */
FIELD(CSR_MERRCTL, ISMERR, 0, 1)
#define LOONGARCH_CSR_MERRINFO1      0x91
#define LOONGARCH_CSR_MERRINFO2      0x92
#define LOONGARCH_CSR_MERRENTRY      0x93 /* MError exception base */
#define LOONGARCH_CSR_MERRERA        0x94 /* MError exception PC */
#define LOONGARCH_CSR_MERRSAVE       0x95 /* KScratch for error exception */

#define LOONGARCH_CSR_CTAG           0x98 /* TagLo + TagHi */

/* Direct map windows CSRs*/
#define LOONGARCH_CSR_DMW(N)         (0x180 + N)
FIELD(CSR_DMW, PLV0, 0, 1)
FIELD(CSR_DMW, PLV1, 1, 1)
FIELD(CSR_DMW, PLV2, 2, 1)
FIELD(CSR_DMW, PLV3, 3, 1)
FIELD(CSR_DMW, MAT, 4, 2)
FIELD(CSR_DMW, VSEG, 60, 4)

#define dmw_va2pa(va) \
    (va & MAKE_64BIT_MASK(0, TARGET_VIRT_ADDR_SPACE_BITS))

/* Debug CSRs */
#define LOONGARCH_CSR_DBG            0x500 /* debug config */
FIELD(CSR_DBG, DST, 0, 1)
FIELD(CSR_DBG, DREV, 1, 7)
FIELD(CSR_DBG, DEI, 8, 1)
FIELD(CSR_DBG, DCL, 9, 1)
FIELD(CSR_DBG, DFW, 10, 1)
FIELD(CSR_DBG, DMW, 11, 1)
FIELD(CSR_DBG, ECODE, 16, 6)

#define LOONGARCH_CSR_DERA           0x501 /* Debug era */
#define LOONGARCH_CSR_DSAVE          0x502 /* Debug save */

#endif /* LOONGARCH_CPU_CSR_H */
