/*
 * QEMU Arm CPU -- feature test functions
 *
 *  Copyright (c) 2023 Linaro Ltd
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_ARM_FEATURES_H
#define TARGET_ARM_FEATURES_H

#include "hw/registerfields.h"
#include "qemu/host-utils.h"
#include "cpu.h"
#include "cpu-sysregs.h"

/*
 * System register ID fields.
 */
FIELD(CLIDR_EL1, CTYPE1, 0, 3)
FIELD(CLIDR_EL1, CTYPE2, 3, 3)
FIELD(CLIDR_EL1, CTYPE3, 6, 3)
FIELD(CLIDR_EL1, CTYPE4, 9, 3)
FIELD(CLIDR_EL1, CTYPE5, 12, 3)
FIELD(CLIDR_EL1, CTYPE6, 15, 3)
FIELD(CLIDR_EL1, CTYPE7, 18, 3)
FIELD(CLIDR_EL1, LOUIS, 21, 3)
FIELD(CLIDR_EL1, LOC, 24, 3)
FIELD(CLIDR_EL1, LOUU, 27, 3)
FIELD(CLIDR_EL1, ICB, 30, 3)

/* When FEAT_CCIDX is implemented */
FIELD(CCSIDR_EL1, CCIDX_LINESIZE, 0, 3)
FIELD(CCSIDR_EL1, CCIDX_ASSOCIATIVITY, 3, 21)
FIELD(CCSIDR_EL1, CCIDX_NUMSETS, 32, 24)

/* When FEAT_CCIDX is not implemented */
FIELD(CCSIDR_EL1, LINESIZE, 0, 3)
FIELD(CCSIDR_EL1, ASSOCIATIVITY, 3, 10)
FIELD(CCSIDR_EL1, NUMSETS, 13, 15)

FIELD(CTR_EL0,  IMINLINE, 0, 4)
FIELD(CTR_EL0,  L1IP, 14, 2)
FIELD(CTR_EL0,  DMINLINE, 16, 4)
FIELD(CTR_EL0,  ERG, 20, 4)
FIELD(CTR_EL0,  CWG, 24, 4)
FIELD(CTR_EL0,  IDC, 28, 1)
FIELD(CTR_EL0,  DIC, 29, 1)
FIELD(CTR_EL0,  TMINLINE, 32, 6)

FIELD(MIDR_EL1, REVISION, 0, 4)
FIELD(MIDR_EL1, PARTNUM, 4, 12)
FIELD(MIDR_EL1, ARCHITECTURE, 16, 4)
FIELD(MIDR_EL1, VARIANT, 20, 4)
FIELD(MIDR_EL1, IMPLEMENTER, 24, 8)

FIELD(ID_ISAR0, SWAP, 0, 4)
FIELD(ID_ISAR0, BITCOUNT, 4, 4)
FIELD(ID_ISAR0, BITFIELD, 8, 4)
FIELD(ID_ISAR0, CMPBRANCH, 12, 4)
FIELD(ID_ISAR0, COPROC, 16, 4)
FIELD(ID_ISAR0, DEBUG, 20, 4)
FIELD(ID_ISAR0, DIVIDE, 24, 4)

FIELD(ID_ISAR1, ENDIAN, 0, 4)
FIELD(ID_ISAR1, EXCEPT, 4, 4)
FIELD(ID_ISAR1, EXCEPT_AR, 8, 4)
FIELD(ID_ISAR1, EXTEND, 12, 4)
FIELD(ID_ISAR1, IFTHEN, 16, 4)
FIELD(ID_ISAR1, IMMEDIATE, 20, 4)
FIELD(ID_ISAR1, INTERWORK, 24, 4)
FIELD(ID_ISAR1, JAZELLE, 28, 4)

FIELD(ID_ISAR2, LOADSTORE, 0, 4)
FIELD(ID_ISAR2, MEMHINT, 4, 4)
FIELD(ID_ISAR2, MULTIACCESSINT, 8, 4)
FIELD(ID_ISAR2, MULT, 12, 4)
FIELD(ID_ISAR2, MULTS, 16, 4)
FIELD(ID_ISAR2, MULTU, 20, 4)
FIELD(ID_ISAR2, PSR_AR, 24, 4)
FIELD(ID_ISAR2, REVERSAL, 28, 4)

FIELD(ID_ISAR3, SATURATE, 0, 4)
FIELD(ID_ISAR3, SIMD, 4, 4)
FIELD(ID_ISAR3, SVC, 8, 4)
FIELD(ID_ISAR3, SYNCHPRIM, 12, 4)
FIELD(ID_ISAR3, TABBRANCH, 16, 4)
FIELD(ID_ISAR3, T32COPY, 20, 4)
FIELD(ID_ISAR3, TRUENOP, 24, 4)
FIELD(ID_ISAR3, T32EE, 28, 4)

FIELD(ID_ISAR4, UNPRIV, 0, 4)
FIELD(ID_ISAR4, WITHSHIFTS, 4, 4)
FIELD(ID_ISAR4, WRITEBACK, 8, 4)
FIELD(ID_ISAR4, SMC, 12, 4)
FIELD(ID_ISAR4, BARRIER, 16, 4)
FIELD(ID_ISAR4, SYNCHPRIM_FRAC, 20, 4)
FIELD(ID_ISAR4, PSR_M, 24, 4)
FIELD(ID_ISAR4, SWP_FRAC, 28, 4)

FIELD(ID_ISAR5, SEVL, 0, 4)
FIELD(ID_ISAR5, AES, 4, 4)
FIELD(ID_ISAR5, SHA1, 8, 4)
FIELD(ID_ISAR5, SHA2, 12, 4)
FIELD(ID_ISAR5, CRC32, 16, 4)
FIELD(ID_ISAR5, RDM, 24, 4)
FIELD(ID_ISAR5, VCMA, 28, 4)

FIELD(ID_ISAR6, JSCVT, 0, 4)
FIELD(ID_ISAR6, DP, 4, 4)
FIELD(ID_ISAR6, FHM, 8, 4)
FIELD(ID_ISAR6, SB, 12, 4)
FIELD(ID_ISAR6, SPECRES, 16, 4)
FIELD(ID_ISAR6, BF16, 20, 4)
FIELD(ID_ISAR6, I8MM, 24, 4)

FIELD(ID_MMFR0, VMSA, 0, 4)
FIELD(ID_MMFR0, PMSA, 4, 4)
FIELD(ID_MMFR0, OUTERSHR, 8, 4)
FIELD(ID_MMFR0, SHARELVL, 12, 4)
FIELD(ID_MMFR0, TCM, 16, 4)
FIELD(ID_MMFR0, AUXREG, 20, 4)
FIELD(ID_MMFR0, FCSE, 24, 4)
FIELD(ID_MMFR0, INNERSHR, 28, 4)

FIELD(ID_MMFR1, L1HVDVA, 0, 4)
FIELD(ID_MMFR1, L1UNIVA, 4, 4)
FIELD(ID_MMFR1, L1HVDSW, 8, 4)
FIELD(ID_MMFR1, L1UNISW, 12, 4)
FIELD(ID_MMFR1, L1HVD, 16, 4)
FIELD(ID_MMFR1, L1UNI, 20, 4)
FIELD(ID_MMFR1, L1TSTCLN, 24, 4)
FIELD(ID_MMFR1, BPRED, 28, 4)

FIELD(ID_MMFR2, L1HVDFG, 0, 4)
FIELD(ID_MMFR2, L1HVDBG, 4, 4)
FIELD(ID_MMFR2, L1HVDRNG, 8, 4)
FIELD(ID_MMFR2, HVDTLB, 12, 4)
FIELD(ID_MMFR2, UNITLB, 16, 4)
FIELD(ID_MMFR2, MEMBARR, 20, 4)
FIELD(ID_MMFR2, WFISTALL, 24, 4)
FIELD(ID_MMFR2, HWACCFLG, 28, 4)

FIELD(ID_MMFR3, CMAINTVA, 0, 4)
FIELD(ID_MMFR3, CMAINTSW, 4, 4)
FIELD(ID_MMFR3, BPMAINT, 8, 4)
FIELD(ID_MMFR3, MAINTBCST, 12, 4)
FIELD(ID_MMFR3, PAN, 16, 4)
FIELD(ID_MMFR3, COHWALK, 20, 4)
FIELD(ID_MMFR3, CMEMSZ, 24, 4)
FIELD(ID_MMFR3, SUPERSEC, 28, 4)

FIELD(ID_MMFR4, SPECSEI, 0, 4)
FIELD(ID_MMFR4, AC2, 4, 4)
FIELD(ID_MMFR4, XNX, 8, 4)
FIELD(ID_MMFR4, CNP, 12, 4)
FIELD(ID_MMFR4, HPDS, 16, 4)
FIELD(ID_MMFR4, LSM, 20, 4)
FIELD(ID_MMFR4, CCIDX, 24, 4)
FIELD(ID_MMFR4, EVT, 28, 4)

FIELD(ID_MMFR5, ETS, 0, 4)
FIELD(ID_MMFR5, NTLBPA, 4, 4)

FIELD(ID_PFR0, STATE0, 0, 4)
FIELD(ID_PFR0, STATE1, 4, 4)
FIELD(ID_PFR0, STATE2, 8, 4)
FIELD(ID_PFR0, STATE3, 12, 4)
FIELD(ID_PFR0, CSV2, 16, 4)
FIELD(ID_PFR0, AMU, 20, 4)
FIELD(ID_PFR0, DIT, 24, 4)
FIELD(ID_PFR0, RAS, 28, 4)

FIELD(ID_PFR1, PROGMOD, 0, 4)
FIELD(ID_PFR1, SECURITY, 4, 4)
FIELD(ID_PFR1, MPROGMOD, 8, 4)
FIELD(ID_PFR1, VIRTUALIZATION, 12, 4)
FIELD(ID_PFR1, GENTIMER, 16, 4)
FIELD(ID_PFR1, SEC_FRAC, 20, 4)
FIELD(ID_PFR1, VIRT_FRAC, 24, 4)
FIELD(ID_PFR1, GIC, 28, 4)

FIELD(ID_PFR2, CSV3, 0, 4)
FIELD(ID_PFR2, SSBS, 4, 4)
FIELD(ID_PFR2, RAS_FRAC, 8, 4)

FIELD(ID_AA64ISAR0, AES, 4, 4)
FIELD(ID_AA64ISAR0, SHA1, 8, 4)
FIELD(ID_AA64ISAR0, SHA2, 12, 4)
FIELD(ID_AA64ISAR0, CRC32, 16, 4)
FIELD(ID_AA64ISAR0, ATOMIC, 20, 4)
FIELD(ID_AA64ISAR0, TME, 24, 4)
FIELD(ID_AA64ISAR0, RDM, 28, 4)
FIELD(ID_AA64ISAR0, SHA3, 32, 4)
FIELD(ID_AA64ISAR0, SM3, 36, 4)
FIELD(ID_AA64ISAR0, SM4, 40, 4)
FIELD(ID_AA64ISAR0, DP, 44, 4)
FIELD(ID_AA64ISAR0, FHM, 48, 4)
FIELD(ID_AA64ISAR0, TS, 52, 4)
FIELD(ID_AA64ISAR0, TLB, 56, 4)
FIELD(ID_AA64ISAR0, RNDR, 60, 4)

FIELD(ID_AA64ISAR1, DPB, 0, 4)
FIELD(ID_AA64ISAR1, APA, 4, 4)
FIELD(ID_AA64ISAR1, API, 8, 4)
FIELD(ID_AA64ISAR1, JSCVT, 12, 4)
FIELD(ID_AA64ISAR1, FCMA, 16, 4)
FIELD(ID_AA64ISAR1, LRCPC, 20, 4)
FIELD(ID_AA64ISAR1, GPA, 24, 4)
FIELD(ID_AA64ISAR1, GPI, 28, 4)
FIELD(ID_AA64ISAR1, FRINTTS, 32, 4)
FIELD(ID_AA64ISAR1, SB, 36, 4)
FIELD(ID_AA64ISAR1, SPECRES, 40, 4)
FIELD(ID_AA64ISAR1, BF16, 44, 4)
FIELD(ID_AA64ISAR1, DGH, 48, 4)
FIELD(ID_AA64ISAR1, I8MM, 52, 4)
FIELD(ID_AA64ISAR1, XS, 56, 4)
FIELD(ID_AA64ISAR1, LS64, 60, 4)

FIELD(ID_AA64ISAR2, WFXT, 0, 4)
FIELD(ID_AA64ISAR2, RPRES, 4, 4)
FIELD(ID_AA64ISAR2, GPA3, 8, 4)
FIELD(ID_AA64ISAR2, APA3, 12, 4)
FIELD(ID_AA64ISAR2, MOPS, 16, 4)
FIELD(ID_AA64ISAR2, BC, 20, 4)
FIELD(ID_AA64ISAR2, PAC_FRAC, 24, 4)
FIELD(ID_AA64ISAR2, CLRBHB, 28, 4)
FIELD(ID_AA64ISAR2, SYSREG_128, 32, 4)
FIELD(ID_AA64ISAR2, SYSINSTR_128, 36, 4)
FIELD(ID_AA64ISAR2, PRFMSLC, 40, 4)
FIELD(ID_AA64ISAR2, RPRFM, 48, 4)
FIELD(ID_AA64ISAR2, CSSC, 52, 4)
FIELD(ID_AA64ISAR2, LUT, 56, 4)
FIELD(ID_AA64ISAR2, ATS1A, 60, 4)

FIELD(ID_AA64PFR0, EL0, 0, 4)
FIELD(ID_AA64PFR0, EL1, 4, 4)
FIELD(ID_AA64PFR0, EL2, 8, 4)
FIELD(ID_AA64PFR0, EL3, 12, 4)
FIELD(ID_AA64PFR0, FP, 16, 4)
FIELD(ID_AA64PFR0, ADVSIMD, 20, 4)
FIELD(ID_AA64PFR0, GIC, 24, 4)
FIELD(ID_AA64PFR0, RAS, 28, 4)
FIELD(ID_AA64PFR0, SVE, 32, 4)
FIELD(ID_AA64PFR0, SEL2, 36, 4)
FIELD(ID_AA64PFR0, MPAM, 40, 4)
FIELD(ID_AA64PFR0, AMU, 44, 4)
FIELD(ID_AA64PFR0, DIT, 48, 4)
FIELD(ID_AA64PFR0, RME, 52, 4)
FIELD(ID_AA64PFR0, CSV2, 56, 4)
FIELD(ID_AA64PFR0, CSV3, 60, 4)

FIELD(ID_AA64PFR1, BT, 0, 4)
FIELD(ID_AA64PFR1, SSBS, 4, 4)
FIELD(ID_AA64PFR1, MTE, 8, 4)
FIELD(ID_AA64PFR1, RAS_FRAC, 12, 4)
FIELD(ID_AA64PFR1, MPAM_FRAC, 16, 4)
FIELD(ID_AA64PFR1, SME, 24, 4)
FIELD(ID_AA64PFR1, RNDR_TRAP, 28, 4)
FIELD(ID_AA64PFR1, CSV2_FRAC, 32, 4)
FIELD(ID_AA64PFR1, NMI, 36, 4)
FIELD(ID_AA64PFR1, MTE_FRAC, 40, 4)
FIELD(ID_AA64PFR1, GCS, 44, 4)
FIELD(ID_AA64PFR1, THE, 48, 4)
FIELD(ID_AA64PFR1, MTEX, 52, 4)
FIELD(ID_AA64PFR1, DF2, 56, 4)
FIELD(ID_AA64PFR1, PFAR, 60, 4)

FIELD(ID_AA64PFR2, MTEPERM, 0, 4)
FIELD(ID_AA64PFR2, MTESTOREONLY, 4, 4)
FIELD(ID_AA64PFR2, MTEFAR, 8, 4)
FIELD(ID_AA64PFR2, FPMR, 32, 4)

FIELD(ID_AA64MMFR0, PARANGE, 0, 4)
FIELD(ID_AA64MMFR0, ASIDBITS, 4, 4)
FIELD(ID_AA64MMFR0, BIGEND, 8, 4)
FIELD(ID_AA64MMFR0, SNSMEM, 12, 4)
FIELD(ID_AA64MMFR0, BIGENDEL0, 16, 4)
FIELD(ID_AA64MMFR0, TGRAN16, 20, 4)
FIELD(ID_AA64MMFR0, TGRAN64, 24, 4)
FIELD(ID_AA64MMFR0, TGRAN4, 28, 4)
FIELD(ID_AA64MMFR0, TGRAN16_2, 32, 4)
FIELD(ID_AA64MMFR0, TGRAN64_2, 36, 4)
FIELD(ID_AA64MMFR0, TGRAN4_2, 40, 4)
FIELD(ID_AA64MMFR0, EXS, 44, 4)
FIELD(ID_AA64MMFR0, FGT, 56, 4)
FIELD(ID_AA64MMFR0, ECV, 60, 4)

FIELD(ID_AA64MMFR1, HAFDBS, 0, 4)
FIELD(ID_AA64MMFR1, VMIDBITS, 4, 4)
FIELD(ID_AA64MMFR1, VH, 8, 4)
FIELD(ID_AA64MMFR1, HPDS, 12, 4)
FIELD(ID_AA64MMFR1, LO, 16, 4)
FIELD(ID_AA64MMFR1, PAN, 20, 4)
FIELD(ID_AA64MMFR1, SPECSEI, 24, 4)
FIELD(ID_AA64MMFR1, XNX, 28, 4)
FIELD(ID_AA64MMFR1, TWED, 32, 4)
FIELD(ID_AA64MMFR1, ETS, 36, 4)
FIELD(ID_AA64MMFR1, HCX, 40, 4)
FIELD(ID_AA64MMFR1, AFP, 44, 4)
FIELD(ID_AA64MMFR1, NTLBPA, 48, 4)
FIELD(ID_AA64MMFR1, TIDCP1, 52, 4)
FIELD(ID_AA64MMFR1, CMOW, 56, 4)
FIELD(ID_AA64MMFR1, ECBHB, 60, 4)

FIELD(ID_AA64MMFR2, CNP, 0, 4)
FIELD(ID_AA64MMFR2, UAO, 4, 4)
FIELD(ID_AA64MMFR2, LSM, 8, 4)
FIELD(ID_AA64MMFR2, IESB, 12, 4)
FIELD(ID_AA64MMFR2, VARANGE, 16, 4)
FIELD(ID_AA64MMFR2, CCIDX, 20, 4)
FIELD(ID_AA64MMFR2, NV, 24, 4)
FIELD(ID_AA64MMFR2, ST, 28, 4)
FIELD(ID_AA64MMFR2, AT, 32, 4)
FIELD(ID_AA64MMFR2, IDS, 36, 4)
FIELD(ID_AA64MMFR2, FWB, 40, 4)
FIELD(ID_AA64MMFR2, TTL, 48, 4)
FIELD(ID_AA64MMFR2, BBM, 52, 4)
FIELD(ID_AA64MMFR2, EVT, 56, 4)
FIELD(ID_AA64MMFR2, E0PD, 60, 4)

FIELD(ID_AA64MMFR3, TCRX, 0, 4)
FIELD(ID_AA64MMFR3, SCTLRX, 4, 4)
FIELD(ID_AA64MMFR3, S1PIE, 8, 4)
FIELD(ID_AA64MMFR3, S2PIE, 12, 4)
FIELD(ID_AA64MMFR3, S1POE, 16, 4)
FIELD(ID_AA64MMFR3, S2POE, 20, 4)
FIELD(ID_AA64MMFR3, AIE, 24, 4)
FIELD(ID_AA64MMFR3, MEC, 28, 4)
FIELD(ID_AA64MMFR3, D128, 32, 4)
FIELD(ID_AA64MMFR3, D128_2, 36, 4)
FIELD(ID_AA64MMFR3, SNERR, 40, 4)
FIELD(ID_AA64MMFR3, ANERR, 44, 4)
FIELD(ID_AA64MMFR3, SDERR, 52, 4)
FIELD(ID_AA64MMFR3, ADERR, 56, 4)
FIELD(ID_AA64MMFR3, SPEC_FPACC, 60, 4)

FIELD(ID_AA64DFR0, DEBUGVER, 0, 4)
FIELD(ID_AA64DFR0, TRACEVER, 4, 4)
FIELD(ID_AA64DFR0, PMUVER, 8, 4)
FIELD(ID_AA64DFR0, BRPS, 12, 4)
FIELD(ID_AA64DFR0, PMSS, 16, 4)
FIELD(ID_AA64DFR0, WRPS, 20, 4)
FIELD(ID_AA64DFR0, SEBEP, 24, 4)
FIELD(ID_AA64DFR0, CTX_CMPS, 28, 4)
FIELD(ID_AA64DFR0, PMSVER, 32, 4)
FIELD(ID_AA64DFR0, DOUBLELOCK, 36, 4)
FIELD(ID_AA64DFR0, TRACEFILT, 40, 4)
FIELD(ID_AA64DFR0, TRACEBUFFER, 44, 4)
FIELD(ID_AA64DFR0, MTPMU, 48, 4)
FIELD(ID_AA64DFR0, BRBE, 52, 4)
FIELD(ID_AA64DFR0, EXTTRCBUFF, 56, 4)
FIELD(ID_AA64DFR0, HPMN0, 60, 4)

FIELD(ID_AA64ZFR0, SVEVER, 0, 4)
FIELD(ID_AA64ZFR0, AES, 4, 4)
FIELD(ID_AA64ZFR0, BITPERM, 16, 4)
FIELD(ID_AA64ZFR0, BFLOAT16, 20, 4)
FIELD(ID_AA64ZFR0, B16B16, 24, 4)
FIELD(ID_AA64ZFR0, SHA3, 32, 4)
FIELD(ID_AA64ZFR0, SM4, 40, 4)
FIELD(ID_AA64ZFR0, I8MM, 44, 4)
FIELD(ID_AA64ZFR0, F32MM, 52, 4)
FIELD(ID_AA64ZFR0, F64MM, 56, 4)

FIELD(ID_AA64SMFR0, F32F32, 32, 1)
FIELD(ID_AA64SMFR0, BI32I32, 33, 1)
FIELD(ID_AA64SMFR0, B16F32, 34, 1)
FIELD(ID_AA64SMFR0, F16F32, 35, 1)
FIELD(ID_AA64SMFR0, I8I32, 36, 4)
FIELD(ID_AA64SMFR0, F16F16, 42, 1)
FIELD(ID_AA64SMFR0, B16B16, 43, 1)
FIELD(ID_AA64SMFR0, I16I32, 44, 4)
FIELD(ID_AA64SMFR0, F64F64, 48, 1)
FIELD(ID_AA64SMFR0, I16I64, 52, 4)
FIELD(ID_AA64SMFR0, SMEVER, 56, 4)
FIELD(ID_AA64SMFR0, FA64, 63, 1)

FIELD(ID_DFR0, COPDBG, 0, 4)
FIELD(ID_DFR0, COPSDBG, 4, 4)
FIELD(ID_DFR0, MMAPDBG, 8, 4)
FIELD(ID_DFR0, COPTRC, 12, 4)
FIELD(ID_DFR0, MMAPTRC, 16, 4)
FIELD(ID_DFR0, MPROFDBG, 20, 4)
FIELD(ID_DFR0, PERFMON, 24, 4)
FIELD(ID_DFR0, TRACEFILT, 28, 4)

FIELD(ID_DFR1, MTPMU, 0, 4)
FIELD(ID_DFR1, HPMN0, 4, 4)

FIELD(DBGDIDR, SE_IMP, 12, 1)
FIELD(DBGDIDR, NSUHD_IMP, 14, 1)
FIELD(DBGDIDR, VERSION, 16, 4)
FIELD(DBGDIDR, CTX_CMPS, 20, 4)
FIELD(DBGDIDR, BRPS, 24, 4)
FIELD(DBGDIDR, WRPS, 28, 4)

FIELD(DBGDEVID, PCSAMPLE, 0, 4)
FIELD(DBGDEVID, WPADDRMASK, 4, 4)
FIELD(DBGDEVID, BPADDRMASK, 8, 4)
FIELD(DBGDEVID, VECTORCATCH, 12, 4)
FIELD(DBGDEVID, VIRTEXTNS, 16, 4)
FIELD(DBGDEVID, DOUBLELOCK, 20, 4)
FIELD(DBGDEVID, AUXREGS, 24, 4)
FIELD(DBGDEVID, CIDMASK, 28, 4)

FIELD(DBGDEVID1, PCSROFFSET, 0, 4)

FIELD(MVFR0, SIMDREG, 0, 4)
FIELD(MVFR0, FPSP, 4, 4)
FIELD(MVFR0, FPDP, 8, 4)
FIELD(MVFR0, FPTRAP, 12, 4)
FIELD(MVFR0, FPDIVIDE, 16, 4)
FIELD(MVFR0, FPSQRT, 20, 4)
FIELD(MVFR0, FPSHVEC, 24, 4)
FIELD(MVFR0, FPROUND, 28, 4)

FIELD(MVFR1, FPFTZ, 0, 4)
FIELD(MVFR1, FPDNAN, 4, 4)
FIELD(MVFR1, SIMDLS, 8, 4) /* A-profile only */
FIELD(MVFR1, SIMDINT, 12, 4) /* A-profile only */
FIELD(MVFR1, SIMDSP, 16, 4) /* A-profile only */
FIELD(MVFR1, SIMDHP, 20, 4) /* A-profile only */
FIELD(MVFR1, MVE, 8, 4) /* M-profile only */
FIELD(MVFR1, FP16, 20, 4) /* M-profile only */
FIELD(MVFR1, FPHP, 24, 4)
FIELD(MVFR1, SIMDFMAC, 28, 4)

FIELD(MVFR2, SIMDMISC, 0, 4)
FIELD(MVFR2, FPMISC, 4, 4)

/*
 * Naming convention for isar_feature functions:
 * Functions which test 32-bit ID registers should have _aa32_ in
 * their name. Functions which test 64-bit ID registers should have
 * _aa64_ in their name. These must only be used in code where we
 * know for certain that the CPU has AArch32 or AArch64 respectively
 * or where the correct answer for a CPU which doesn't implement that
 * CPU state is "false" (eg when generating A32 or A64 code, if adding
 * system registers that are specific to that CPU state, for "should
 * we let this system register bit be set" tests where the 32-bit
 * flavour of the register doesn't have the bit, and so on).
 * Functions which simply ask "does this feature exist at all" have
 * _any_ in their name, and always return the logical OR of the _aa64_
 * and the _aa32_ function.
 */

/*
 * 32-bit feature tests via id registers.
 */
static inline bool isar_feature_aa32_thumb_div(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR0, DIVIDE) != 0;
}

static inline bool isar_feature_aa32_arm_div(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR0, DIVIDE) > 1;
}

static inline bool isar_feature_aa32_lob(const ARMISARegisters *id)
{
    /* (M-profile) low-overhead loops and branch future */
    return FIELD_EX32_IDREG(id, ID_ISAR0, CMPBRANCH) >= 3;
}

static inline bool isar_feature_aa32_jazelle(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR1, JAZELLE) != 0;
}

static inline bool isar_feature_aa32_aes(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR5, AES) != 0;
}

static inline bool isar_feature_aa32_pmull(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR5, AES) > 1;
}

static inline bool isar_feature_aa32_sha1(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR5, SHA1) != 0;
}

static inline bool isar_feature_aa32_sha2(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR5, SHA2) != 0;
}

static inline bool isar_feature_aa32_crc32(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR5, CRC32) != 0;
}

static inline bool isar_feature_aa32_rdm(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR5, RDM) != 0;
}

static inline bool isar_feature_aa32_vcma(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR5, VCMA) != 0;
}

static inline bool isar_feature_aa32_jscvt(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR6, JSCVT) != 0;
}

static inline bool isar_feature_aa32_dp(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR6, DP) != 0;
}

static inline bool isar_feature_aa32_fhm(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR6, FHM) != 0;
}

static inline bool isar_feature_aa32_sb(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR6, SB) != 0;
}

static inline bool isar_feature_aa32_predinv(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR6, SPECRES) != 0;
}

static inline bool isar_feature_aa32_bf16(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR6, BF16) != 0;
}

static inline bool isar_feature_aa32_i8mm(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_ISAR6, I8MM) != 0;
}

static inline bool isar_feature_aa32_ras(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_PFR0, RAS) != 0;
}

static inline bool isar_feature_aa32_mprofile(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_PFR1, MPROGMOD) != 0;
}

static inline bool isar_feature_aa32_m_sec_state(const ARMISARegisters *id)
{
    /*
     * Return true if M-profile state handling insns
     * (VSCCLRM, CLRM, FPCTX access insns) are implemented
     */
    return FIELD_EX32_IDREG(id, ID_PFR1, SECURITY) >= 3;
}

static inline bool isar_feature_aa32_fp16_arith(const ARMISARegisters *id)
{
    /* Sadly this is encoded differently for A-profile and M-profile */
    if (isar_feature_aa32_mprofile(id)) {
        return FIELD_EX32(id->mvfr1, MVFR1, FP16) > 0;
    } else {
        return FIELD_EX32(id->mvfr1, MVFR1, FPHP) >= 3;
    }
}

static inline bool isar_feature_aa32_mve(const ARMISARegisters *id)
{
    /*
     * Return true if MVE is supported (either integer or floating point).
     * We must check for M-profile as the MVFR1 field means something
     * else for A-profile.
     */
    return isar_feature_aa32_mprofile(id) &&
        FIELD_EX32(id->mvfr1, MVFR1, MVE) > 0;
}

static inline bool isar_feature_aa32_mve_fp(const ARMISARegisters *id)
{
    /*
     * Return true if MVE is supported (either integer or floating point).
     * We must check for M-profile as the MVFR1 field means something
     * else for A-profile.
     */
    return isar_feature_aa32_mprofile(id) &&
        FIELD_EX32(id->mvfr1, MVFR1, MVE) >= 2;
}

static inline bool isar_feature_aa32_vfp_simd(const ARMISARegisters *id)
{
    /*
     * Return true if either VFP or SIMD is implemented.
     * In this case, a minimum of VFP w/ D0-D15.
     */
    return FIELD_EX32(id->mvfr0, MVFR0, SIMDREG) > 0;
}

static inline bool isar_feature_aa32_simd_r32(const ARMISARegisters *id)
{
    /* Return true if D16-D31 are implemented */
    return FIELD_EX32(id->mvfr0, MVFR0, SIMDREG) >= 2;
}

static inline bool isar_feature_aa32_fpshvec(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr0, MVFR0, FPSHVEC) > 0;
}

static inline bool isar_feature_aa32_fpsp_v2(const ARMISARegisters *id)
{
    /* Return true if CPU supports single precision floating point, VFPv2 */
    return FIELD_EX32(id->mvfr0, MVFR0, FPSP) > 0;
}

static inline bool isar_feature_aa32_fpsp_v3(const ARMISARegisters *id)
{
    /* Return true if CPU supports single precision floating point, VFPv3 */
    return FIELD_EX32(id->mvfr0, MVFR0, FPSP) >= 2;
}

static inline bool isar_feature_aa32_fpdp_v2(const ARMISARegisters *id)
{
    /* Return true if CPU supports double precision floating point, VFPv2 */
    return FIELD_EX32(id->mvfr0, MVFR0, FPDP) > 0;
}

static inline bool isar_feature_aa32_fpdp_v3(const ARMISARegisters *id)
{
    /* Return true if CPU supports double precision floating point, VFPv3 */
    return FIELD_EX32(id->mvfr0, MVFR0, FPDP) >= 2;
}

static inline bool isar_feature_aa32_vfp(const ARMISARegisters *id)
{
    return isar_feature_aa32_fpsp_v2(id) || isar_feature_aa32_fpdp_v2(id);
}

/*
 * We always set the FP and SIMD FP16 fields to indicate identical
 * levels of support (assuming SIMD is implemented at all), so
 * we only need one set of accessors.
 */
static inline bool isar_feature_aa32_fp16_spconv(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr1, MVFR1, FPHP) > 0;
}

static inline bool isar_feature_aa32_fp16_dpconv(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr1, MVFR1, FPHP) > 1;
}

/*
 * Note that this ID register field covers both VFP and Neon FMAC,
 * so should usually be tested in combination with some other
 * check that confirms the presence of whichever of VFP or Neon is
 * relevant, to avoid accidentally enabling a Neon feature on
 * a VFP-no-Neon core or vice-versa.
 */
static inline bool isar_feature_aa32_simdfmac(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr1, MVFR1, SIMDFMAC) != 0;
}

static inline bool isar_feature_aa32_vsel(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr2, MVFR2, FPMISC) >= 1;
}

static inline bool isar_feature_aa32_vcvt_dr(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr2, MVFR2, FPMISC) >= 2;
}

static inline bool isar_feature_aa32_vrint(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr2, MVFR2, FPMISC) >= 3;
}

static inline bool isar_feature_aa32_vminmaxnm(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr2, MVFR2, FPMISC) >= 4;
}

static inline bool isar_feature_aa32_pxn(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_MMFR0, VMSA) >= 4;
}

static inline bool isar_feature_aa32_pan(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_MMFR3, PAN) != 0;
}

static inline bool isar_feature_aa32_ats1e1(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_MMFR3, PAN) >= 2;
}

static inline bool isar_feature_aa32_pmuv3p1(const ARMISARegisters *id)
{
    /* 0xf means "non-standard IMPDEF PMU" */
    return FIELD_EX32_IDREG(id, ID_DFR0, PERFMON) >= 4 &&
        FIELD_EX32_IDREG(id, ID_DFR0, PERFMON) != 0xf;
}

static inline bool isar_feature_aa32_pmuv3p4(const ARMISARegisters *id)
{
    /* 0xf means "non-standard IMPDEF PMU" */
    return FIELD_EX32_IDREG(id, ID_DFR0, PERFMON) >= 5 &&
        FIELD_EX32_IDREG(id, ID_DFR0, PERFMON) != 0xf;
}

static inline bool isar_feature_aa32_pmuv3p5(const ARMISARegisters *id)
{
    /* 0xf means "non-standard IMPDEF PMU" */
    return FIELD_EX32_IDREG(id, ID_DFR0, PERFMON) >= 6 &&
        FIELD_EX32_IDREG(id, ID_DFR0, PERFMON) != 0xf;
}

static inline bool isar_feature_aa32_hpd(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_MMFR4, HPDS) != 0;
}

static inline bool isar_feature_aa32_ac2(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_MMFR4, AC2) != 0;
}

static inline bool isar_feature_aa32_ccidx(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_MMFR4, CCIDX) != 0;
}

static inline bool isar_feature_aa32_tts2uxn(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_MMFR4, XNX) != 0;
}

static inline bool isar_feature_aa32_half_evt(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_MMFR4, EVT) >= 1;
}

static inline bool isar_feature_aa32_evt(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_MMFR4, EVT) >= 2;
}

static inline bool isar_feature_aa32_dit(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_PFR0, DIT) != 0;
}

static inline bool isar_feature_aa32_ssbs(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_PFR2, SSBS) != 0;
}

static inline bool isar_feature_aa32_debugv7p1(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_DFR0, COPDBG) >= 5;
}

static inline bool isar_feature_aa32_debugv8p2(const ARMISARegisters *id)
{
    return FIELD_EX32_IDREG(id, ID_DFR0, COPDBG) >= 8;
}

static inline bool isar_feature_aa32_doublelock(const ARMISARegisters *id)
{
    return FIELD_EX32(id->dbgdevid, DBGDEVID, DOUBLELOCK) > 0;
}

/*
 * 64-bit feature tests via id registers.
 */
static inline bool isar_feature_aa64_aes(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, AES) != 0;
}

static inline bool isar_feature_aa64_pmull(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, AES) > 1;
}

static inline bool isar_feature_aa64_sha1(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, SHA1) != 0;
}

static inline bool isar_feature_aa64_sha256(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, SHA2) != 0;
}

static inline bool isar_feature_aa64_sha512(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, SHA2) > 1;
}

static inline bool isar_feature_aa64_crc32(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, CRC32) != 0;
}

static inline bool isar_feature_aa64_lse(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, ATOMIC) >= 2;
}

static inline bool isar_feature_aa64_lse128(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, ATOMIC) >= 3;
}

static inline bool isar_feature_aa64_rdm(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, RDM) != 0;
}

static inline bool isar_feature_aa64_sha3(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, SHA3) != 0;
}

static inline bool isar_feature_aa64_sm3(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, SM3) != 0;
}

static inline bool isar_feature_aa64_sm4(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, SM4) != 0;
}

static inline bool isar_feature_aa64_dp(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, DP) != 0;
}

static inline bool isar_feature_aa64_fhm(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, FHM) != 0;
}

static inline bool isar_feature_aa64_condm_4(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, TS) != 0;
}

static inline bool isar_feature_aa64_condm_5(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, TS) >= 2;
}

static inline bool isar_feature_aa64_rndr(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, RNDR) != 0;
}

static inline bool isar_feature_aa64_tlbirange(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, TLB) == 2;
}

static inline bool isar_feature_aa64_tlbios(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR0, TLB) != 0;
}

static inline bool isar_feature_aa64_jscvt(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, JSCVT) != 0;
}

static inline bool isar_feature_aa64_fcma(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, FCMA) != 0;
}

static inline bool isar_feature_aa64_xs(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, XS) != 0;
}

/*
 * These are the values from APA/API/APA3.
 * In general these must be compared '>=', per the normal Arm ARM
 * treatment of fields in ID registers.
 */
typedef enum {
    PauthFeat_None         = 0,
    PauthFeat_1            = 1,
    PauthFeat_EPAC         = 2,
    PauthFeat_2            = 3,
    PauthFeat_FPAC         = 4,
    PauthFeat_FPACCOMBINED = 5,
} ARMPauthFeature;

static inline ARMPauthFeature
isar_feature_pauth_feature(const ARMISARegisters *id)
{
    /*
     * Architecturally, only one of {APA,API,APA3} may be active (non-zero)
     * and the other two must be zero.  Thus we may avoid conditionals.
     */
    return (FIELD_EX64_IDREG(id, ID_AA64ISAR1, APA) |
            FIELD_EX64_IDREG(id, ID_AA64ISAR1, API) |
            FIELD_EX64_IDREG(id, ID_AA64ISAR2, APA3));
}

static inline bool isar_feature_aa64_pauth(const ARMISARegisters *id)
{
    /*
     * Return true if any form of pauth is enabled, as this
     * predicate controls migration of the 128-bit keys.
     */
    return isar_feature_pauth_feature(id) != PauthFeat_None;
}

static inline bool isar_feature_aa64_pauth_qarma5(const ARMISARegisters *id)
{
    /*
     * Return true if pauth is enabled with the architected QARMA5 algorithm.
     * QEMU will always enable or disable both APA and GPA.
     */
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, APA) != 0;
}

static inline bool isar_feature_aa64_pauth_qarma3(const ARMISARegisters *id)
{
    /*
     * Return true if pauth is enabled with the architected QARMA3 algorithm.
     * QEMU will always enable or disable both APA3 and GPA3.
     */
    return FIELD_EX64_IDREG(id, ID_AA64ISAR2, APA3) != 0;
}

static inline bool isar_feature_aa64_sb(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, SB) != 0;
}

static inline bool isar_feature_aa64_predinv(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, SPECRES) != 0;
}

static inline bool isar_feature_aa64_frint(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, FRINTTS) != 0;
}

static inline bool isar_feature_aa64_dcpop(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, DPB) != 0;
}

static inline bool isar_feature_aa64_dcpodp(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, DPB) >= 2;
}

static inline bool isar_feature_aa64_bf16(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, BF16) != 0;
}

static inline bool isar_feature_aa64_ebf16(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, BF16) > 1;
}

static inline bool isar_feature_aa64_rcpc_8_3(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, LRCPC) != 0;
}

static inline bool isar_feature_aa64_rcpc_8_4(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, LRCPC) >= 2;
}

static inline bool isar_feature_aa64_i8mm(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR1, I8MM) != 0;
}

static inline bool isar_feature_aa64_wfxt(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR2, WFXT) >= 2;
}

static inline bool isar_feature_aa64_hbc(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR2, BC) != 0;
}

static inline bool isar_feature_aa64_mops(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR2, MOPS);
}

static inline bool isar_feature_aa64_rpres(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR2, RPRES);
}

static inline bool isar_feature_aa64_cssc(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR2, CSSC) != 0;
}

static inline bool isar_feature_aa64_lut(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR2, LUT);
}

static inline bool isar_feature_aa64_ats1a(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ISAR2, ATS1A);
}

static inline bool isar_feature_aa64_fp_simd(const ARMISARegisters *id)
{
    /* We always set the AdvSIMD and FP fields identically.  */
    return FIELD_EX64_IDREG(id, ID_AA64PFR0, FP) != 0xf;
}

static inline bool isar_feature_aa64_fp16(const ARMISARegisters *id)
{
    /* We always set the AdvSIMD and FP fields identically wrt FP16.  */
    return FIELD_EX64_IDREG(id, ID_AA64PFR0, FP) == 1;
}

static inline bool isar_feature_aa64_aa32(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR0, EL0) >= 2;
}

static inline bool isar_feature_aa64_aa32_el1(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR0, EL1) >= 2;
}

static inline bool isar_feature_aa64_aa32_el2(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR0, EL2) >= 2;
}

static inline bool isar_feature_aa64_ras(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR0, RAS) != 0;
}

static inline bool isar_feature_aa64_doublefault(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR0, RAS) >= 2;
}

static inline bool isar_feature_aa64_sve(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR0, SVE) != 0;
}

static inline bool isar_feature_aa64_sel2(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR0, SEL2) != 0;
}

static inline bool isar_feature_aa64_rme(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR0, RME) != 0;
}

static inline bool isar_feature_aa64_rme_gpc2(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR0, RME) >= 2;
}

static inline bool isar_feature_aa64_dit(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR0, DIT) != 0;
}

static inline bool isar_feature_aa64_scxtnum(const ARMISARegisters *id)
{
    int key = FIELD_EX64_IDREG(id, ID_AA64PFR0, CSV2);
    if (key >= 2) {
        return true;      /* FEAT_CSV2_2 */
    }
    if (key == 1) {
        key = FIELD_EX64_IDREG(id, ID_AA64PFR1, CSV2_FRAC);
        return key >= 2;  /* FEAT_CSV2_1p2 */
    }
    return false;
}

static inline bool isar_feature_aa64_ssbs(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR1, SSBS) != 0;
}

static inline bool isar_feature_aa64_bti(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR1, BT) != 0;
}

static inline bool isar_feature_aa64_mte_insn_reg(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR1, MTE) != 0;
}

static inline bool isar_feature_aa64_mte(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR1, MTE) >= 2;
}

static inline bool isar_feature_aa64_mte3(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR1, MTE) >= 3;
}

static inline bool isar_feature_aa64_sme(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR1, SME) != 0;
}

static inline bool isar_feature_aa64_nmi(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR1, NMI) != 0;
}

static inline bool isar_feature_aa64_gcs(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64PFR1, GCS) != 0;
}

static inline bool isar_feature_aa64_tgran4_lpa2(const ARMISARegisters *id)
{
    return FIELD_SEX64_IDREG(id, ID_AA64MMFR0, TGRAN4) >= 1;
}

static inline bool isar_feature_aa64_tgran4_2_lpa2(const ARMISARegisters *id)
{
    unsigned t = FIELD_EX64_IDREG(id, ID_AA64MMFR0, TGRAN4_2);
    return t >= 3 || (t == 0 && isar_feature_aa64_tgran4_lpa2(id));
}

static inline bool isar_feature_aa64_tgran16_lpa2(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR0, TGRAN16) >= 2;
}

static inline bool isar_feature_aa64_tgran16_2_lpa2(const ARMISARegisters *id)
{
    unsigned t = FIELD_EX64_IDREG(id, ID_AA64MMFR0, TGRAN16_2);
    return t >= 3 || (t == 0 && isar_feature_aa64_tgran16_lpa2(id));
}

static inline bool isar_feature_aa64_tgran4(const ARMISARegisters *id)
{
    return FIELD_SEX64_IDREG(id, ID_AA64MMFR0, TGRAN4) >= 0;
}

static inline bool isar_feature_aa64_tgran16(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR0, TGRAN16) >= 1;
}

static inline bool isar_feature_aa64_tgran64(const ARMISARegisters *id)
{
    return FIELD_SEX64_IDREG(id, ID_AA64MMFR0, TGRAN64) >= 0;
}

static inline bool isar_feature_aa64_tgran4_2(const ARMISARegisters *id)
{
    unsigned t = FIELD_EX64_IDREG(id, ID_AA64MMFR0, TGRAN4_2);
    return t >= 2 || (t == 0 && isar_feature_aa64_tgran4(id));
}

static inline bool isar_feature_aa64_tgran16_2(const ARMISARegisters *id)
{
    unsigned t = FIELD_EX64_IDREG(id, ID_AA64MMFR0, TGRAN16_2);
    return t >= 2 || (t == 0 && isar_feature_aa64_tgran16(id));
}

static inline bool isar_feature_aa64_tgran64_2(const ARMISARegisters *id)
{
    unsigned t = FIELD_EX64_IDREG(id, ID_AA64MMFR0, TGRAN64_2);
    return t >= 2 || (t == 0 && isar_feature_aa64_tgran64(id));
}

static inline bool isar_feature_aa64_fgt(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR0, FGT) != 0;
}

static inline bool isar_feature_aa64_ecv_traps(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR0, ECV) > 0;
}

static inline bool isar_feature_aa64_ecv(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR0, ECV) > 1;
}

static inline bool isar_feature_aa64_vh(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR1, VH) != 0;
}

static inline bool isar_feature_aa64_lor(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR1, LO) != 0;
}

static inline bool isar_feature_aa64_pan(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR1, PAN) != 0;
}

static inline bool isar_feature_aa64_ats1e1(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR1, PAN) >= 2;
}

static inline bool isar_feature_aa64_pan3(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR1, PAN) >= 3;
}

static inline bool isar_feature_aa64_hcx(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR1, HCX) != 0;
}

static inline bool isar_feature_aa64_afp(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR1, AFP) != 0;
}

static inline bool isar_feature_aa64_tidcp1(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR1, TIDCP1) != 0;
}

static inline bool isar_feature_aa64_cmow(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR1, CMOW) != 0;
}

static inline bool isar_feature_aa64_hafs(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR1, HAFDBS) != 0;
}

static inline bool isar_feature_aa64_hdbs(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR1, HAFDBS) >= 2;
}

static inline bool isar_feature_aa64_tts2uxn(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR1, XNX) != 0;
}

static inline bool isar_feature_aa64_uao(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR2, UAO) != 0;
}

static inline bool isar_feature_aa64_st(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR2, ST) != 0;
}

static inline bool isar_feature_aa64_lse2(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR2, AT) != 0;
}

static inline bool isar_feature_aa64_fwb(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR2, FWB) != 0;
}

static inline bool isar_feature_aa64_ids(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR2, IDS) != 0;
}

static inline bool isar_feature_aa64_half_evt(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR2, EVT) >= 1;
}

static inline bool isar_feature_aa64_evt(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR2, EVT) >= 2;
}

static inline bool isar_feature_aa64_ccidx(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR2, CCIDX) != 0;
}

static inline bool isar_feature_aa64_lva(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR2, VARANGE) != 0;
}

static inline bool isar_feature_aa64_e0pd(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR2, E0PD) != 0;
}

static inline bool isar_feature_aa64_nv(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR2, NV) != 0;
}

static inline bool isar_feature_aa64_nv2(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR2, NV) >= 2;
}

static inline bool isar_feature_aa64_tcr2(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR3, TCRX) != 0;
}

static inline bool isar_feature_aa64_sctlr2(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR3, SCTLRX) != 0;
}

static inline bool isar_feature_aa64_s1pie(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR3, S1PIE) != 0;
}

static inline bool isar_feature_aa64_s2pie(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR3, S2PIE) != 0;
}

static inline bool isar_feature_aa64_aie(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR3, AIE) != 0;
}

static inline bool isar_feature_aa64_mec(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64MMFR3, MEC) != 0;
}

static inline bool isar_feature_aa64_pmuv3p1(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64DFR0, PMUVER) >= 4 &&
        FIELD_EX64_IDREG(id, ID_AA64DFR0, PMUVER) != 0xf;
}

static inline bool isar_feature_aa64_pmuv3p4(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64DFR0, PMUVER) >= 5 &&
        FIELD_EX64_IDREG(id, ID_AA64DFR0, PMUVER) != 0xf;
}

static inline bool isar_feature_aa64_pmuv3p5(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64DFR0, PMUVER) >= 6 &&
        FIELD_EX64_IDREG(id, ID_AA64DFR0, PMUVER) != 0xf;
}

static inline bool isar_feature_aa64_debugv8p2(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64DFR0, DEBUGVER) >= 8;
}

static inline bool isar_feature_aa64_doublelock(const ARMISARegisters *id)
{
    return FIELD_SEX64_IDREG(id, ID_AA64DFR0, DOUBLELOCK) >= 0;
}

static inline bool isar_feature_aa64_sve2(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ZFR0, SVEVER) != 0;
}

static inline bool isar_feature_aa64_sve2p1(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ZFR0, SVEVER) >=2;
}

static inline bool isar_feature_aa64_sve2_aes(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ZFR0, AES) != 0;
}

static inline bool isar_feature_aa64_sve2_pmull128(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ZFR0, AES) >= 2;
}

static inline bool isar_feature_aa64_sve2_bitperm(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ZFR0, BITPERM) != 0;
}

static inline bool isar_feature_aa64_sve_bf16(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ZFR0, BFLOAT16) != 0;
}

static inline bool isar_feature_aa64_sve2_sha3(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ZFR0, SHA3) != 0;
}

static inline bool isar_feature_aa64_sve2_sm4(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ZFR0, SM4) != 0;
}

static inline bool isar_feature_aa64_sve_i8mm(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ZFR0, I8MM) != 0;
}

static inline bool isar_feature_aa64_sve_f32mm(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ZFR0, F32MM) != 0;
}

static inline bool isar_feature_aa64_sve_f64mm(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ZFR0, F64MM) != 0;
}

static inline bool isar_feature_aa64_sve_b16b16(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64ZFR0, B16B16);
}

static inline bool isar_feature_aa64_sme_b16b16(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64SMFR0, B16B16);
}

static inline bool isar_feature_aa64_sme_f16f16(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64SMFR0, F16F16);
}

static inline bool isar_feature_aa64_sme_f64f64(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64SMFR0, F64F64);
}

static inline bool isar_feature_aa64_sme_i16i64(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64SMFR0, I16I64) == 0xf;
}

static inline bool isar_feature_aa64_sme_fa64(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64SMFR0, FA64);
}

static inline bool isar_feature_aa64_sme2(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64SMFR0, SMEVER) != 0;
}

static inline bool isar_feature_aa64_sme2p1(const ARMISARegisters *id)
{
    return FIELD_EX64_IDREG(id, ID_AA64SMFR0, SMEVER) >= 2;
}

/*
 * Combinations of feature tests, for ease of use with TRANS_FEAT.
 */
static inline bool isar_feature_aa64_sme_or_sve2p1(const ARMISARegisters *id)
{
    return isar_feature_aa64_sme(id) || isar_feature_aa64_sve2p1(id);
}

static inline bool isar_feature_aa64_sme2_or_sve2p1(const ARMISARegisters *id)
{
    return isar_feature_aa64_sme2(id) || isar_feature_aa64_sve2p1(id);
}

static inline bool isar_feature_aa64_sme2p1_or_sve2p1(const ARMISARegisters *id)
{
    return isar_feature_aa64_sme2p1(id) || isar_feature_aa64_sve2p1(id);
}

static inline bool isar_feature_aa64_sme2_i16i64(const ARMISARegisters *id)
{
    return isar_feature_aa64_sme2(id) && isar_feature_aa64_sme_i16i64(id);
}

static inline bool isar_feature_aa64_sme2_f64f64(const ARMISARegisters *id)
{
    return isar_feature_aa64_sme2(id) && isar_feature_aa64_sme_f64f64(id);
}

/*
 * Feature tests for "does this exist in either 32-bit or 64-bit?"
 */
static inline bool isar_feature_any_fp16(const ARMISARegisters *id)
{
    return isar_feature_aa64_fp16(id) || isar_feature_aa32_fp16_arith(id);
}

static inline bool isar_feature_any_predinv(const ARMISARegisters *id)
{
    return isar_feature_aa64_predinv(id) || isar_feature_aa32_predinv(id);
}

static inline bool isar_feature_any_pmuv3p1(const ARMISARegisters *id)
{
    return isar_feature_aa64_pmuv3p1(id) || isar_feature_aa32_pmuv3p1(id);
}

static inline bool isar_feature_any_pmuv3p4(const ARMISARegisters *id)
{
    return isar_feature_aa64_pmuv3p4(id) || isar_feature_aa32_pmuv3p4(id);
}

static inline bool isar_feature_any_pmuv3p5(const ARMISARegisters *id)
{
    return isar_feature_aa64_pmuv3p5(id) || isar_feature_aa32_pmuv3p5(id);
}

static inline bool isar_feature_any_ccidx(const ARMISARegisters *id)
{
    return isar_feature_aa64_ccidx(id) || isar_feature_aa32_ccidx(id);
}

static inline bool isar_feature_any_tts2uxn(const ARMISARegisters *id)
{
    return isar_feature_aa64_tts2uxn(id) || isar_feature_aa32_tts2uxn(id);
}

static inline bool isar_feature_any_debugv8p2(const ARMISARegisters *id)
{
    return isar_feature_aa64_debugv8p2(id) || isar_feature_aa32_debugv8p2(id);
}

static inline bool isar_feature_any_ras(const ARMISARegisters *id)
{
    return isar_feature_aa64_ras(id) || isar_feature_aa32_ras(id);
}

static inline bool isar_feature_any_half_evt(const ARMISARegisters *id)
{
    return isar_feature_aa64_half_evt(id) || isar_feature_aa32_half_evt(id);
}

static inline bool isar_feature_any_evt(const ARMISARegisters *id)
{
    return isar_feature_aa64_evt(id) || isar_feature_aa32_evt(id);
}

typedef enum {
    CCSIDR_FORMAT_LEGACY,
    CCSIDR_FORMAT_CCIDX,
} CCSIDRFormat;

static inline uint64_t make_ccsidr(CCSIDRFormat format, unsigned assoc,
                                   unsigned linesize, unsigned cachesize,
                                   uint8_t flags)
{
    unsigned lg_linesize = ctz32(linesize);
    unsigned sets;
    uint64_t ccsidr = 0;

    assert(assoc != 0);
    assert(is_power_of_2(linesize));
    assert(lg_linesize >= 4 && lg_linesize <= 7 + 4);

    /* sets * associativity * linesize == cachesize. */
    sets = cachesize / (assoc * linesize);
    assert(cachesize % (assoc * linesize) == 0);

    if (format == CCSIDR_FORMAT_LEGACY) {
        /*
         * The 32-bit CCSIDR format is:
         *   [27:13] number of sets - 1
         *   [12:3]  associativity - 1
         *   [2:0]   log2(linesize) - 4
         *           so 0 == 16 bytes, 1 == 32 bytes, 2 == 64 bytes, etc
         */
        ccsidr = deposit32(ccsidr, 28,  4, flags);
        ccsidr = deposit32(ccsidr, 13, 15, sets - 1);
        ccsidr = deposit32(ccsidr,  3, 10, assoc - 1);
        ccsidr = deposit32(ccsidr,  0,  3, lg_linesize - 4);
    } else {
        /*
         * The 64-bit CCSIDR_EL1 format is:
         *   [55:32] number of sets - 1
         *   [23:3]  associativity - 1
         *   [2:0]   log2(linesize) - 4
         *           so 0 == 16 bytes, 1 == 32 bytes, 2 == 64 bytes, etc
         */
        ccsidr = deposit64(ccsidr, 32, 24, sets - 1);
        ccsidr = deposit64(ccsidr,  3, 21, assoc - 1);
        ccsidr = deposit64(ccsidr,  0,  3, lg_linesize - 4);
    }

    return ccsidr;
}

/*
 * Forward to the above feature tests given an ARMCPU pointer.
 */
#define cpu_isar_feature(name, cpu) \
    ({ ARMCPU *cpu_ = (cpu); isar_feature_##name(&cpu_->isar); })

#endif
