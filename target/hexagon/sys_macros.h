/*
 * Copyright(c) 2019-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_SYS_MACROS_H
#define HEXAGON_SYS_MACROS_H

/*
 * Macro definitions for Hexagon system mode
 */

#ifndef CONFIG_USER_ONLY

#define READ_SREG(NUM) arch_get_system_reg(env, NUM)
#define READ_SGP0()    arch_get_system_reg(env, HEX_SREG_SGP0)
#define READ_SGP1()    arch_get_system_reg(env, HEX_SREG_SGP1)
#define READ_SGP10()   ((uint64_t)arch_get_system_reg(env, HEX_SREG_SGP0) | \
    ((uint64_t)arch_get_system_reg(env, HEX_SREG_SGP1) << 32))

#define WRITE_SREG(NUM, VAL)      log_sreg_write(env, NUM, VAL, slot)
#define WRITE_SGP0(VAL)           log_sreg_write(env, HEX_SREG_SGP0, VAL, slot)
#define WRITE_SGP1(VAL)           log_sreg_write(env, HEX_SREG_SGP1, VAL, slot)
#define WRITE_SGP10(VAL) \
    do { \
        log_sreg_write(env, HEX_SREG_SGP0, (VAL) & 0xFFFFFFFF, slot); \
        log_sreg_write(env, HEX_SREG_SGP1, (VAL) >> 32, slot); \
    } while (0)

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
        uint32_t regval = arch_get_system_reg(ENV, REG); \
        fINSERT_BITS(regval, reg_field_info[FIELD].width, \
                     reg_field_info[FIELD].offset, (VAL)); \
        arch_set_system_reg(ENV, REG, regval); \
    } while (0)
#define SET_SSR_FIELD(ENV, FIELD, VAL) \
    SET_SYSTEM_FIELD(ENV, HEX_SREG_SSR, FIELD, VAL)
#define SET_SYSCFG_FIELD(ENV, FIELD, VAL) \
    SET_SYSTEM_FIELD(ENV, HEX_SREG_SYSCFG, FIELD, VAL)

#define CCR_FIELD_SET(ENV, FIELD) \
    (!!GET_FIELD(FIELD, arch_get_system_reg(ENV, HEX_SREG_CCR)))

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

#define fREAD_ELR() (READ_SREG(HEX_SREG_ELR))

#define fLOAD_PHYS(NUM, SIZE, SIGN, SRC1, SRC2, DST) { \
  const uintptr_t rs = ((unsigned long)(unsigned)(SRC1)) & 0x7ff; \
  const uintptr_t rt = ((unsigned long)(unsigned)(SRC2)) << 11; \
  const uintptr_t addr = rs + rt;         \
  cpu_physical_memory_read(addr, &DST, sizeof(uint32_t)); \
}

#define fPOW2_HELP_ROUNDUP(VAL) \
    ((VAL) | \
     ((VAL) >> 1) | \
     ((VAL) >> 2) | \
     ((VAL) >> 4) | \
     ((VAL) >> 8) | \
     ((VAL) >> 16))
#define fPOW2_ROUNDUP(VAL) (fPOW2_HELP_ROUNDUP((VAL) - 1) + 1)

#define fFRAMECHECK(ADDR, EA)  g_assert_not_reached();

#define fTRAP(TRAPTYPE, IMM) \
    register_trap_exception(env, TRAPTYPE, IMM, PC)

#define fVIRTINSN_SPSWAP(IMM, REG)
#define fVIRTINSN_GETIE(IMM, REG) { REG = 0xdeafbeef; }
#define fVIRTINSN_SETIE(IMM, REG)
#define fVIRTINSN_RTE(IMM, REG)
#define fGRE_ENABLED() GET_FIELD(CCR_GRE, READ_SREG(HEX_SREG_CCR))
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
        uint32_t tmp = 0; \
        tmp = arch_get_system_reg(env, HEX_SREG_SSR); \
        fINSERT_BITS(tmp, reg_field_info[SSR_EX].width, \
                     reg_field_info[SSR_EX].offset, 0); \
        log_sreg_write(env, HEX_SREG_SSR, tmp, slot); \
    } while (0)

#define fDCINVIDX(REG)
#define fDCINVA(REG) do { REG = REG; } while (0) /* Nothing to do in qemu */

#define fSET_TLB_LOCK()       g_assert_not_reached()
#define fCLEAR_TLB_LOCK()     g_assert_not_reached()

#define fSET_K0_LOCK()        g_assert_not_reached()
#define fCLEAR_K0_LOCK()      g_assert_not_reached()

#define fTLB_IDXMASK(INDEX) \
    ((INDEX) & (fPOW2_ROUNDUP(fCAST4u(env_archcpu(env)->num_tlbs)) - 1))

#define fTLB_NONPOW2WRAP(INDEX)                 \
    (((INDEX) >= env_archcpu(env)->num_tlbs) ?  \
         ((INDEX) - env_archcpu(env)->num_tlbs) : \
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
    (env->hex_tlb->entries[fTLB_NONPOW2WRAP(fTLB_IDXMASK(INDEX))])
#define fTLBR_EXTENDED(INDEX) \
    (env->hex_tlb->entries[fTLB_NONPOW2WRAP(fTLB_IDXMASK(INDEX))])
#define fTLBP(TLBHI) \
    hex_tlb_lookup(env, ((TLBHI) >> 12), ((TLBHI) << 12))
#define iic_flush_cache(p)

#define fIN_DEBUG_MODE(TNUM) \
    ((GET_FIELD(ISDBST_DEBUGMODE, arch_get_system_reg(env, HEX_SREG_ISDBST)) \
        & (0x1 << (TNUM))) != 0)

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
        DST = HEX_DC_STATE_INVALID | 0x00; \
    } while (0)
#endif

#endif

#define NUM_TLB_REGS(x) (env_archcpu(env)->num_tlbs)

#endif
