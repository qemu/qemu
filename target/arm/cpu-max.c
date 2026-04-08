/*
 * QEMU ARM 'max' CPU
 *
 * Copyright (c) 2018 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "target/arm/internals.h"

/* Share AArch32 -cpu max features with AArch64. */
void aa32_max_features(ARMCPU *cpu)
{
    uint32_t t;
    ARMISARegisters *isar = &cpu->isar;

    /* Add additional features supported by QEMU */
    t = GET_IDREG(isar, ID_ISAR5);
    t = FIELD_DP32(t, ID_ISAR5, AES, 2);          /* FEAT_PMULL */
    t = FIELD_DP32(t, ID_ISAR5, SHA1, 1);         /* FEAT_SHA1 */
    t = FIELD_DP32(t, ID_ISAR5, SHA2, 1);         /* FEAT_SHA256 */
    t = FIELD_DP32(t, ID_ISAR5, CRC32, 1);
    t = FIELD_DP32(t, ID_ISAR5, RDM, 1);          /* FEAT_RDM */
    t = FIELD_DP32(t, ID_ISAR5, VCMA, 1);         /* FEAT_FCMA */
    SET_IDREG(isar, ID_ISAR5, t);

    t = GET_IDREG(isar, ID_ISAR6);
    t = FIELD_DP32(t, ID_ISAR6, JSCVT, 1);        /* FEAT_JSCVT */
    t = FIELD_DP32(t, ID_ISAR6, DP, 1);           /* Feat_DotProd */
    t = FIELD_DP32(t, ID_ISAR6, FHM, 1);          /* FEAT_FHM */
    t = FIELD_DP32(t, ID_ISAR6, SB, 1);           /* FEAT_SB */
    t = FIELD_DP32(t, ID_ISAR6, SPECRES, 1);      /* FEAT_SPECRES */
    t = FIELD_DP32(t, ID_ISAR6, BF16, 1);         /* FEAT_AA32BF16 */
    t = FIELD_DP32(t, ID_ISAR6, I8MM, 1);         /* FEAT_AA32I8MM */
    SET_IDREG(isar, ID_ISAR6, t);

    t = cpu->isar.mvfr1;
    t = FIELD_DP32(t, MVFR1, FPHP, 3);            /* FEAT_FP16 */
    t = FIELD_DP32(t, MVFR1, SIMDHP, 2);          /* FEAT_FP16 */
    cpu->isar.mvfr1 = t;

    t = cpu->isar.mvfr2;
    t = FIELD_DP32(t, MVFR2, SIMDMISC, 3);        /* SIMD MaxNum */
    t = FIELD_DP32(t, MVFR2, FPMISC, 4);          /* FP MaxNum */
    cpu->isar.mvfr2 = t;

    FIELD_DP32_IDREG(isar, ID_MMFR3, PAN, 2);          /* FEAT_PAN2 */

    t = GET_IDREG(isar, ID_MMFR4);
    t = FIELD_DP32(t, ID_MMFR4, HPDS, 2);         /* FEAT_HPDS2 */
    t = FIELD_DP32(t, ID_MMFR4, AC2, 1);          /* ACTLR2, HACTLR2 */
    t = FIELD_DP32(t, ID_MMFR4, CNP, 1);          /* FEAT_TTCNP */
    t = FIELD_DP32(t, ID_MMFR4, XNX, 1);          /* FEAT_XNX */
    t = FIELD_DP32(t, ID_MMFR4, EVT, 2);          /* FEAT_EVT */
    SET_IDREG(isar, ID_MMFR4, t);

    FIELD_DP32_IDREG(isar, ID_MMFR5, ETS, 2);          /* FEAT_ETS2 */

    t = GET_IDREG(isar, ID_PFR0);
    t = FIELD_DP32(t, ID_PFR0, CSV2, 2);          /* FEAT_CSV2 */
    t = FIELD_DP32(t, ID_PFR0, DIT, 1);           /* FEAT_DIT */
    t = FIELD_DP32(t, ID_PFR0, RAS, 1);           /* FEAT_RAS */
    SET_IDREG(isar, ID_PFR0, t);

    t = GET_IDREG(isar, ID_PFR2);
    t = FIELD_DP32(t, ID_PFR2, CSV3, 1);          /* FEAT_CSV3 */
    t = FIELD_DP32(t, ID_PFR2, SSBS, 1);          /* FEAT_SSBS */
    SET_IDREG(isar, ID_PFR2, t);

    t = GET_IDREG(isar, ID_DFR0);
    t = FIELD_DP32(t, ID_DFR0, COPDBG, 10);       /* FEAT_Debugv8p8 */
    t = FIELD_DP32(t, ID_DFR0, COPSDBG, 10);      /* FEAT_Debugv8p8 */
    t = FIELD_DP32(t, ID_DFR0, PERFMON, 6);       /* FEAT_PMUv3p5 */
    SET_IDREG(isar, ID_DFR0, t);

    /* Debug ID registers. */

    /* Bit[15] is RES1, Bit[13] and Bits[11:0] are RES0. */
    t = 0x00008000;
    t = FIELD_DP32(t, DBGDIDR, SE_IMP, 1);
    t = FIELD_DP32(t, DBGDIDR, NSUHD_IMP, 1);
    t = FIELD_DP32(t, DBGDIDR, VERSION, 10);      /* FEAT_Debugv8p8 */
    t = FIELD_DP32(t, DBGDIDR, CTX_CMPS, 1);
    t = FIELD_DP32(t, DBGDIDR, BRPS, 5);
    t = FIELD_DP32(t, DBGDIDR, WRPS, 3);
    cpu->isar.dbgdidr = t;

    t = 0;
    t = FIELD_DP32(t, DBGDEVID, PCSAMPLE, 3);
    t = FIELD_DP32(t, DBGDEVID, WPADDRMASK, 1);
    t = FIELD_DP32(t, DBGDEVID, BPADDRMASK, 15);
    t = FIELD_DP32(t, DBGDEVID, VECTORCATCH, 0);
    t = FIELD_DP32(t, DBGDEVID, VIRTEXTNS, 1);
    t = FIELD_DP32(t, DBGDEVID, DOUBLELOCK, 1);
    t = FIELD_DP32(t, DBGDEVID, AUXREGS, 0);
    t = FIELD_DP32(t, DBGDEVID, CIDMASK, 0);
    cpu->isar.dbgdevid = t;

    /* Bits[31:4] are RES0. */
    t = 0;
    t = FIELD_DP32(t, DBGDEVID1, PCSROFFSET, 2);
    cpu->isar.dbgdevid1 = t;

    FIELD_DP32_IDREG(isar, ID_DFR1, HPMN0, 1);         /* FEAT_HPMN0 */
}
