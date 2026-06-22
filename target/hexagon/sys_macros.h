/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_SYS_MACROS_H
#define HEXAGON_SYS_MACROS_H

/*
 * Macro definitions for Hexagon system mode
 */

#ifndef CONFIG_USER_ONLY

#include "system/physmem.h"

#ifdef QEMU_GENERATE
#define GET_SSR_FIELD(RES, FIELD) \
    GET_FIELD(RES, FIELD, hex_t_sreg[HEX_SREG_SSR])
#else

#define GET_SSR_FIELD(FIELD, REGIN) \
    (uint32_t)GET_FIELD(FIELD, REGIN)
#define GET_SYSCFG_FIELD(FIELD, REGIN) \
    (uint32_t)GET_FIELD(FIELD, REGIN)
#define SET_SYSTEM_FIELD(ENV, REG, FIELD, VAL) \
    do { \
        HexagonCPU *_sf_cpu = env_archcpu(ENV); \
        uint32_t regval; \
        if ((REG) < HEX_SREG_GLB_START) { \
            regval = (ENV)->t_sreg[(REG)]; \
        } else { \
            regval = _sf_cpu->globalregs ? \
                hexagon_globalreg_read(_sf_cpu->globalregs, (REG), \
                                       (ENV)->threadId) : 0; \
        } \
        fINSERT_BITS(regval, reg_field_info[FIELD].width, \
                     reg_field_info[FIELD].offset, (VAL)); \
        if ((REG) < HEX_SREG_GLB_START) { \
            (ENV)->t_sreg[(REG)] = regval; \
        } else if (_sf_cpu->globalregs) { \
            hexagon_globalreg_write(_sf_cpu->globalregs, (REG), regval, \
                                    (ENV)->threadId); \
        } \
    } while (0)
#define SET_SSR_FIELD(ENV, FIELD, VAL) \
    SET_SYSTEM_FIELD(ENV, HEX_SREG_SSR, FIELD, VAL)
#define SET_SYSCFG_FIELD(ENV, FIELD, VAL) \
    SET_SYSTEM_FIELD(ENV, HEX_SREG_SYSCFG, FIELD, VAL)

#define CCR_FIELD_SET(ENV, FIELD) \
    (!!GET_FIELD(FIELD, (ENV)->t_sreg[HEX_SREG_CCR]))

/*
 * Direct-to-guest is not implemented yet, continuing would cause unexpected
 * behavior, so we abort.
 */
#define ASSERT_DIRECT_TO_GUEST_UNSET(ENV, EXCP) \
    do { \
        switch (EXCP) { \
        case HEX_EVENT_TRAP0: \
            g_assert(!CCR_FIELD_SET(ENV, CCR_GTE)); \
            break; \
        case HEX_EVENT_IMPRECISE: \
        case HEX_EVENT_PRECISE: \
        case HEX_EVENT_FPTRAP: \
            g_assert(!CCR_FIELD_SET(ENV, CCR_GEE)); \
            break; \
        default: \
            if ((EXCP) >= HEX_EVENT_INT0) { \
                g_assert(!CCR_FIELD_SET(ENV, CCR_GIE)); \
            } \
            break; \
        } \
    } while (0)
#endif

#define fREAD_ELR() (env->t_sreg[HEX_SREG_ELR])

#define fLOAD_PHYS(NUM, SIZE, SIGN, SRC1, SRC2, DST) { \
  const uintptr_t rs = ((unsigned long)(unsigned)(SRC1)) & 0x7ff; \
  const uintptr_t rt = ((unsigned long)(unsigned)(SRC2)) << 11; \
  const uintptr_t addr = rs + rt;         \
  physical_memory_read(addr, &DST, sizeof(uint32_t)); \
}

#define fPOW2_HELP_ROUNDUP(VAL) \
    ((VAL) | \
     ((VAL) >> 1) | \
     ((VAL) >> 2) | \
     ((VAL) >> 4) | \
     ((VAL) >> 8) | \
     ((VAL) >> 16))
#define fPOW2_ROUNDUP(VAL) (fPOW2_HELP_ROUNDUP((VAL) - 1) + 1)

#define fTRAP(TRAPTYPE, IMM) \
    register_trap_exception(env, TRAPTYPE, IMM, PC)

#ifdef QEMU_GENERATE
#define fFRAMECHECK(ADDR, EA) gen_framecheck(ctx, ADDR, EA)
#endif

#define fVIRTINSN_SPSWAP(IMM, REG)
#define fVIRTINSN_GETIE(IMM, REG) { REG = 0xdeafbeef; }
#define fVIRTINSN_SETIE(IMM, REG)
#define fVIRTINSN_RTE(IMM, REG)
#define fGRE_ENABLED() \
    GET_FIELD(CCR_GRE, env->t_sreg[HEX_SREG_CCR])
#define fTRAP1_VIRTINSN(IMM) \
    (fGRE_ENABLED() && \
        (((IMM) == 1) || ((IMM) == 3) || ((IMM) == 4) || ((IMM) == 6)))

/* Not modeled in qemu */

#define MARK_LATE_PRED_WRITE(RNUM)
#define fICINVIDX(REG)
#define fICKILL()
#define fDCKILL()
#define fL2KILL()
#define fL2UNLOCK()
#define fL2CLEAN()
#define fL2CLEANINV()
#define fL2CLEANPA(REG)
#define fL2CLEANINVPA(REG)
#define fL2CLEANINVIDX(REG)
#define fL2CLEANIDX(REG)
#define fL2INVIDX(REG)
#define fL2TAGR(INDEX, DST, DSTREG)
#define fL2UNLOCKA(VA) ((void) VA)
#define fL2TAGW(INDEX, PART2)
#define fDCCLEANIDX(REG)
#define fDCCLEANINVIDX(REG)

/* Always succeed: */
#define fL2LOCKA(EA, PDV, PDN) ((void) EA, PDV = 0xFF)
#define fCLEAR_RTE_EX() \
    do { \
        uint32_t tmp = env->t_sreg[HEX_SREG_SSR]; \
        fINSERT_BITS(tmp, reg_field_info[SSR_EX].width, \
                     reg_field_info[SSR_EX].offset, 0); \
        log_sreg_write(env, HEX_SREG_SSR, tmp, slot); \
    } while (0)

#define fDCINVIDX(REG)
#define fDCINVA(REG) do { REG = REG; } while (0) /* Nothing to do in qemu */

#define fSET_TLB_LOCK()       hex_tlb_lock(env);
#define fCLEAR_TLB_LOCK()     hex_tlb_unlock(env);

#define fSET_K0_LOCK()        hex_k0_lock(env);
#define fCLEAR_K0_LOCK()      hex_k0_unlock(env);

#define fTLB_IDXMASK(INDEX) \
    ((INDEX) & (fPOW2_ROUNDUP( \
        fCAST4u(hexagon_tlb_get_num_entries(env_archcpu(env)->tlb))) - 1))

#define fTLB_NONPOW2WRAP(INDEX) \
    (((INDEX) >= hexagon_tlb_get_num_entries(env_archcpu(env)->tlb)) ? \
         ((INDEX) - hexagon_tlb_get_num_entries(env_archcpu(env)->tlb)) : \
         (INDEX))


#define fTLBW(INDEX, VALUE) \
    hex_tlbw(env, (INDEX), (VALUE))
#define fTLBW_EXTENDED(INDEX, VALUE) \
    hex_tlbw(env, (INDEX), (VALUE))
#define fTLB_ENTRY_OVERLAP(VALUE) \
    (hex_tlb_check_overlap(env, VALUE, -1) != -2)
#define fTLB_ENTRY_OVERLAP_IDX(VALUE) \
    hex_tlb_check_overlap(env, VALUE, -1)
#define fTLBR(INDEX) \
    hexagon_tlb_read(env_archcpu(env)->tlb, \
                     fTLB_NONPOW2WRAP(fTLB_IDXMASK(INDEX)))
#define fTLBR_EXTENDED(INDEX) \
    hexagon_tlb_read(env_archcpu(env)->tlb, \
                     fTLB_NONPOW2WRAP(fTLB_IDXMASK(INDEX)))
#define fTLBP(TLBHI) \
    hex_tlb_lookup(env, ((TLBHI) >> 12), ((TLBHI) << 12))
#define iic_flush_cache(p)

#define fIN_DEBUG_MODE(TNUM) ({ \
    HexagonCPU *_cpu = env_archcpu(env); \
    uint32_t _isdbst = _cpu->globalregs ? \
        hexagon_globalreg_read(_cpu->globalregs, \
                               HEX_SREG_ISDBST, env->threadId) : 0; \
    (GET_FIELD(ISDBST_DEBUGMODE, _isdbst) \
        & (0x1 << (TNUM))) != 0; })

#define fIN_DEBUG_MODE_NO_ISDB(TNUM) false
#define fIN_DEBUG_MODE_WARN(TNUM) false

#ifdef QEMU_GENERATE

/*
 * Read tags back as zero for now:
 *
 * tag value in RD[31:10] for 32k, RD[31:9] for 16k
 */
#define fICTAGR(RS, RD, RD2) \
    do { \
        RD = ctx->zero; \
    } while (0)
#define fICTAGW(RS, RD)
#define fICDATAR(RS, RD) \
    do { \
        RD = ctx->zero; \
    } while (0)
#define fICDATAW(RS, RD)

#define fDCTAGW(RS, RT)
/* tag: RD[23:0], state: RD[30:29] */
#define fDCTAGR(INDEX, DST, DST_REG_NUM) \
    do { \
        DST = ctx->zero; \
    } while (0)
#else

/*
 * Read tags back as zero for now:
 *
 * tag value in RD[31:10] for 32k, RD[31:9] for 16k
 */
#define fICTAGR(RS, RD, RD2) \
    do { \
        RD = 0x00; \
    } while (0)
#define fICTAGW(RS, RD)
#define fICDATAR(RS, RD) \
    do { \
        RD = 0x00; \
    } while (0)
#define fICDATAW(RS, RD)

#define fDCTAGW(RS, RT)
/* tag: RD[23:0], state: RD[30:29] */
#define fDCTAGR(INDEX, DST, DST_REG_NUM) \
    do { \
        DST = 0; \
    } while (0)
#endif

#else
#define ASSERT_DIRECT_TO_GUEST_UNSET(ENV, EXCP) do { } while (0)
#endif

#define NUM_TLB_REGS(x) (hexagon_tlb_get_num_entries(env_archcpu(env)->tlb))

/* NMI routing not yet implemented; Y4_nmi is a no-op for now */
#define fDO_NMI(THREAD_MASK) do { } while (0)

#endif
