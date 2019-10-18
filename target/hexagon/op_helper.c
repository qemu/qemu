/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

/*
 * Hexagon helpers
 */

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <math.h>
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "tcg-op.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "global_types.h"
#include "macros.h"
#include "mmvec/mmvec.h"
#include "mmvec/macros.h"
#include "utils.h"
#include "fma_emu.h"
#include "myfenv.h"
#include "conv_emu.h"
#include "translate.h"
#include "qemu.h"

#ifdef COUNT_HEX_HELPERS
#include "opcodes.h"

typedef struct {
    int count;
    const char *tag;
} helper_count_t;

helper_count_t helper_counts[] = {
#define OPCODE(TAG)    { 0, #TAG },
#include "opcodes.odef"
#undef OPCODE
    { 0, NULL }
};

#define COUNT_HELPER(TAG)      do { helper_counts[(TAG)].count++; } while (0)

void print_helper_counts(void)
{
    helper_count_t *p;

    printf("HELPER COUNTS\n");
    for (p = helper_counts; p->tag; p++) {
        if (p->count) {
            printf("\t%d\t\t%s\n", p->count, p->tag);
        }
    }
}
#else
#define COUNT_HELPER(TAG)              /* Nothing */
#endif

/* Exceptions processing helpers */
void QEMU_NORETURN do_raise_exception_err(CPUHexagonState *env,
                                          uint32_t exception, uintptr_t pc)
{
    CPUState *cs = CPU(hexagon_env_get_cpu(env));
    qemu_log_mask(CPU_LOG_INT, "%s: %d\n", __func__, exception);
    cs->exception_index = exception;
    cpu_loop_exit_restore(cs, pc);
}

void helper_raise_exception(CPUHexagonState *env, uint32_t exception)
{
    do_raise_exception_err(env, exception, 0);
}

static inline void log_reg_write(CPUHexagonState *env, int rnum, int32_t val,
                                 uint32_t slot)
{
#ifdef DEBUG_HEX
    printf("log_reg_write[%d] = %d (0x%x)", rnum, val, val);
    if (env->slot_cancelled & (1 << slot)) {
        printf(" CANCELLED");
    }
    if (val == env->gpr[rnum]) {
        printf(" NO CHANGE");
    }
    printf("\n");
#endif
    if (!(env->slot_cancelled & (1 << slot))) {
        env->new_value[rnum] = val;
    }
}

static inline void log_reg_write_pair(CPUHexagonState *env, int rnum,
                                      int64_t val, uint32_t slot)
{
#ifdef DEBUG_HEX
    printf("log_reg_write_pair[%d:%d] = %ld\n", rnum + 1, rnum, val);
#endif
    log_reg_write(env, rnum, val & 0xFFFFFFFF, slot);
    log_reg_write(env, rnum + 1, (val >> 32) & 0xFFFFFFFF, slot);
}

static inline void log_pred_write(CPUHexagonState *env, int pnum, int32_t val)
{
#ifdef DEBUG_HEX
    printf("log_pred_write[%d] = %d (0x%x)\n", pnum, val, val);
#endif

    /* Multiple writes to the same preg are and'ed together */
    if (env->pred_written[pnum]) {
        env->new_pred_value[pnum] &= val & 0xff;
    } else {
        env->new_pred_value[pnum] = val & 0xff;
        env->pred_written[pnum] = 1;
    }
}

static inline void log_store32(CPUHexagonState *env, target_ulong addr,
                               int32_t val, int width, int slot)
{
#ifdef DEBUG_HEX
    printf("log_store%d(0x%x, %d [0x%x])\n", width, addr, val, val);
#endif
    env->mem_log_stores[slot].va = addr;
    env->mem_log_stores[slot].width = width;
    env->mem_log_stores[slot].data32 = val;
}

static inline void log_store64(CPUHexagonState *env, target_ulong addr,
                               int64_t val, int width, int slot)
{
#ifdef DEBUG_HEX
    printf("log_store%d(0x%x, %ld [0x%lx])\n", width, addr, val, val);
#endif
    env->mem_log_stores[slot].va = addr;
    env->mem_log_stores[slot].width = width;
    env->mem_log_stores[slot].data64 = val;
}

static inline void write_new_pc(CPUHexagonState *env, target_ulong addr)
{
#ifdef DEBUG_HEX
    printf("write_new_pc(0x%x)\n", addr);
#endif
    if (env->branch_taken) {
#ifdef DEBUG_HEX
        printf("INFO: multiple branches taken in same packet, "
               "ignoring the second one\n");
#endif
    } else {
        fCHECK_PCALIGN(addr);
        env->branch_taken = 1;
        env->next_PC = addr;
    }
}

#ifdef DEBUG_HEX
void HELPER(debug_start_packet)(CPUHexagonState *env)
{
    print_thread_prefix(env);
    printf("Start packet: pc = 0x%x\n", env->gpr[HEX_REG_PC]);
    fflush(stdout);

    int i;
    for (i = 0; i < TOTAL_PER_THREAD_REGS; i++) {
        env->reg_written[i] = 0;
    }
}
#endif

int32_t HELPER(new_value)(CPUHexagonState *env, int rnum)
{
    return env->new_value[rnum];
}

static inline int32_t new_pred_value(CPUHexagonState *env, int pnum)
{
    return env->new_pred_value[pnum];
}

#ifdef DEBUG_HEX
void HELPER(debug_check_store_width)(CPUHexagonState *env, int slot, int check)
{
    if (env->mem_log_stores[slot].width != check) {
        printf("ERROR: %d != %d\n", env->mem_log_stores[slot].width, check);
        g_assert_not_reached();
    }
}
#endif

void HELPER(commit_hvx_stores)(CPUHexagonState *env)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (env->vstore_pending[i]) {
            env->vstore_pending[i] = 0;
            target_ulong va = env->vstore[i].va;
            int size = env->vstore[i].size;
            for (int j = 0; j < size; j++) {
                if (env->vstore[i].mask.ub[j]) {
                    put_user_u8(env->vstore[i].data.ub[j], va + j);
                }
            }
        }
    }

    if (env->vtcm_pending) {
        env->vtcm_pending = 0;
        if (env->vtcm_log.op) {
            /* Need to perform the scatter read/modify/write at commit time */
            if (env->vtcm_log.op_size == 2) {
                SCATTER_OP_WRITE_TO_MEM(size2u_t);
            } else if (env->vtcm_log.op_size == 4) {
                /* Word Scatter += */
                SCATTER_OP_WRITE_TO_MEM(size4u_t);
            } else {
                g_assert_not_reached();
            }
        } else {
            for (int i = 0; i < env->vtcm_log.size; i++) {
                if (env->vtcm_log.mask.ub[i] != 0) {
                    put_user_u8(env->vtcm_log.data.ub[i], env->vtcm_log.va[i]);
                    env->vtcm_log.mask.ub[i] = 0;
                    env->vtcm_log.data.ub[i] = 0;
                    env->vtcm_log.offsets.ub[i] = 0;
                }

            }
        }
    }
}

#ifdef FIXME
/*  This is a poor man's watch point  */
static int32_t help_peek(target_ulong vaddr)
{
    int32_t peek;
    get_user_u32(peek, vaddr);
    return peek;
}

#define WATCH(vaddr) \
{ \
    static int last_peek; \
    int32_t peek = help_peek(vaddr); \
    if (peek != last_peek) { \
        print_thread_prefix(env); \
        printf("Packet committed: pc = 0x%x, watchpoint(0x%x) = %d (0x%x)\n", \
               env->this_PC, vaddr, peek, peek); \
        last_peek = peek; \
    } \
}
#endif

#ifdef DEBUG_HEX
static void print_store(CPUHexagonState *env, int slot)
{
    if (!(env->slot_cancelled & (1 << slot))) {
        size1u_t width = env->mem_log_stores[slot].width;
        if (width == 1) {
            size4u_t data = env->mem_log_stores[slot].data32 & 0xff;
            printf("\tmemb[0x%x] = %d (0x%02x)\n",
                   env->mem_log_stores[slot].va, data, data);
        } else if (width == 2) {
            size4u_t data = env->mem_log_stores[slot].data32 & 0xffff;
            printf("\tmemh[0x%x] = %d (0x%04x)\n",
                   env->mem_log_stores[slot].va, data, data);
        } else if (width == 4) {
            size4u_t data = env->mem_log_stores[slot].data32;
            printf("\tmemw[0x%x] = %d (0x%08x)\n",
                   env->mem_log_stores[slot].va, data, data);
        } else if (width == 8) {
            printf("\tmemd[0x%x] = %lld (0x%016llx)\n",
                   env->mem_log_stores[slot].va,
                   env->mem_log_stores[slot].data64,
                   env->mem_log_stores[slot].data64);
        } else {
            printf("\tBad store width %d\n", width);
            g_assert_not_reached();
        }
    }
}

/* This function is a handy place to set a breakpoint */
void HELPER(debug_commit_end)(CPUHexagonState *env, int has_st0, int has_st1)
{
    bool reg_printed = false;
    bool pred_printed = false;
    int i;

    print_thread_prefix(env);
    printf("Packet committed: pc = 0x%x\n", env->this_PC);

    for (i = 0; i < TOTAL_PER_THREAD_REGS; i++) {
        if (env->reg_written[i]) {
            if (!reg_printed) {
                print_thread_prefix(env);
                printf("Regs written\n");
                reg_printed = true;
            }
            printf("\tr%d = %d (0x%x)\n", i, env->new_value[i],
                   env->new_value[i]);
        }
    }

    for (i = 0; i < NUM_PREGS; i++) {
        if (env->pred_written[i]) {
            if (!pred_printed) {
                print_thread_prefix(env);
                printf("Predicates written\n");
                pred_printed = true;
            }
            printf("\tp%d = 0x%x\n", i, env->new_pred_value[i]);
        }
    }

    if (has_st0 || has_st1) {
        print_thread_prefix(env);
        printf("Stores\n");
        if (has_st0) {
            print_store(env, 0);
        }
        if (has_st1) {
            print_store(env, 1);
        }
    }

    print_thread_prefix(env);
    printf("Next PC = 0x%x\n", env->next_PC);
    print_thread_prefix(env);
    printf("Exec counters: pkt = %d, insn = %d, hvx = %d\n",
           env->gpr[HEX_REG_QEMU_PKT_CNT],
           env->gpr[HEX_REG_QEMU_INSN_CNT],
           env->gpr[HEX_REG_QEMU_HVX_CNT]);
    fflush(stdout);

#ifdef FIXME /* Convert this to a command-line option */
    WATCH(0x0783aa00)
#endif
}
#endif

int32_t HELPER(sfrecipa_val)(CPUHexagonState *env, int32_t RsV, int32_t RtV)
{
    /* int32_t PeV; Not needed to compute value */
    int32_t RdV;
    fHIDE(int idx;)
    fHIDE(int adjust;)
    fHIDE(int mant;)
    fHIDE(int exp;)
    if (fSF_RECIP_COMMON(RsV, RtV, RdV, adjust)) {
        /* PeV = adjust; Not needed to compute value */
        idx = (RtV >> 16) & 0x7f;
        mant = (fSF_RECIP_LOOKUP(idx) << 15) | 1;
        exp = fSF_BIAS() - (fSF_GETEXP(RtV) - fSF_BIAS()) - 1;
        RdV = fMAKESF(fGETBIT(31, RtV), exp, mant);
    }
    return RdV;
}

int32_t HELPER(sfrecipa_pred)(CPUHexagonState *env, int32_t RsV, int32_t RtV)
{
    int32_t PeV = 0;
    int32_t RdV;
    fHIDE(int idx;)
    fHIDE(int adjust;)
    fHIDE(int mant;)
    fHIDE(int exp;)
    if (fSF_RECIP_COMMON(RsV, RtV, RdV, adjust)) {
        PeV = adjust;
        idx = (RtV >> 16) & 0x7f;
        mant = (fSF_RECIP_LOOKUP(idx) << 15) | 1;
        exp = fSF_BIAS() - (fSF_GETEXP(RtV) - fSF_BIAS()) - 1;
        RdV = fMAKESF(fGETBIT(31, RtV), exp, mant);
    }
    return PeV;
}

int32_t HELPER(sfinvsqrta_val)(CPUHexagonState *env, int32_t RsV)
{
    /* int32_t PeV; Not needed for val version */
    int32_t RdV;
    fHIDE(int idx;)
    fHIDE(int adjust;)
    fHIDE(int mant;)
    fHIDE(int exp;)
    if (fSF_INVSQRT_COMMON(RsV, RdV, adjust)) {
        /* PeV = adjust; Not needed for val version */
        idx = (RsV >> 17) & 0x7f;
        mant = (fSF_INVSQRT_LOOKUP(idx) << 15);
        exp = fSF_BIAS() - ((fSF_GETEXP(RsV) - fSF_BIAS()) >> 1) - 1;
        RdV = fMAKESF(fGETBIT(31, RsV), exp, mant);
    }
    return RdV;
}

int32_t HELPER(sfinvsqrta_pred)(CPUHexagonState *env, int32_t RsV)
{
    int32_t PeV = 0;
    int32_t RdV;
    fHIDE(int idx;)
    fHIDE(int adjust;)
    fHIDE(int mant;)
    fHIDE(int exp;)
    if (fSF_INVSQRT_COMMON(RsV, RdV, adjust)) {
        PeV = adjust;
        idx = (RsV >> 17) & 0x7f;
        mant = (fSF_INVSQRT_LOOKUP(idx) << 15);
        exp = fSF_BIAS() - ((fSF_GETEXP(RsV) - fSF_BIAS()) >> 1) - 1;
        RdV = fMAKESF(fGETBIT(31, RsV), exp, mant);
    }
    return PeV;
}

/* Helpful for printing intermediate values within instructions */
void HELPER(debug_value)(CPUHexagonState *env, int32_t value)
{
    printf("value = 0x%x\n", value);
}

void HELPER(debug_value_i64)(CPUHexagonState *env, int64_t value)
{
    printf("value = 0x%lx\n", value);
}

static inline void log_ext_vreg_write(CPUHexagonState *env, int num, void *var,
                                      int vnew, uint32_t slot)
{
#ifdef DEBUG_HEX
    printf("log_ext_vreg_write[%d]", num);
    if (env->slot_cancelled & (1 << slot)) {
        printf(" CANCELLED");
    }
    printf("\n");
#endif

    if (!(env->slot_cancelled & (1 << slot))) {
        VRegMask regnum_mask = ((VRegMask)1) << num;
        env->VRegs_updated |=      (vnew != EXT_TMP) ? regnum_mask : 0;
        env->VRegs_select |=       (vnew == EXT_NEW) ? regnum_mask : 0;
        env->VRegs_updated_tmp  |= (vnew == EXT_TMP) ? regnum_mask : 0;
        env->future_VRegs[num] = *(mmvector_t *)var;
        if (vnew == EXT_TMP) {
            env->tmp_VRegs[num] = env->future_VRegs[num];
        }
    }
}

static void cancel_slot(CPUHexagonState *env, uint32_t slot)
{
#ifdef DEBUG_HEX
    printf("Slot %d cancelled\n", slot);
#endif
    env->slot_cancelled |= (1 << slot);
}

static inline void log_mmvector_write(CPUHexagonState *env, int num,
                                      mmvector_t var, int vnew, uint32_t slot)
{
    log_ext_vreg_write(env, num, &var, vnew, slot);
}

#include "q6v_defines.h"
#include "regs.h"

#define BOGUS_HELPER(tag) \
    printf("ERROR: bogus helper: " #tag "\n")

#define DEF_QEMU(TAG, SHORTCODE, HELPER, GENFN, HELPFN) HELPFN
#include "qemu.odef"
#undef DEF_QEMU
