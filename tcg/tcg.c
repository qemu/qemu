/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* define it to use liveness analysis (better code) */
#define USE_TCG_OPTIMIZATIONS

#include "qemu/osdep.h"

/* Define to jump the ELF file used to communicate with GDB.  */
#undef DEBUG_JIT

#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "qemu/host-utils.h"
#include "qemu/qemu-print.h"
#include "qemu/cacheflush.h"
#include "qemu/cacheinfo.h"
#include "qemu/timer.h"

/* Note: the long term plan is to reduce the dependencies on the QEMU
   CPU definitions. Currently they are used for qemu_ld/st
   instructions */
#define NO_CPU_IO_DEFS

#include "exec/exec-all.h"
#include "tcg/tcg-op.h"

#if UINTPTR_MAX == UINT32_MAX
# define ELF_CLASS  ELFCLASS32
#else
# define ELF_CLASS  ELFCLASS64
#endif
#if HOST_BIG_ENDIAN
# define ELF_DATA   ELFDATA2MSB
#else
# define ELF_DATA   ELFDATA2LSB
#endif

#include "elf.h"
#include "exec/log.h"
#include "tcg/tcg-ldst.h"
#include "tcg/tcg-temp-internal.h"
#include "tcg-internal.h"
#include "accel/tcg/perf.h"

/* Forward declarations for functions declared in tcg-target.c.inc and
   used here. */
static void tcg_target_init(TCGContext *s);
static void tcg_target_qemu_prologue(TCGContext *s);
static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend);

/* The CIE and FDE header definitions will be common to all hosts.  */
typedef struct {
    uint32_t len __attribute__((aligned((sizeof(void *)))));
    uint32_t id;
    uint8_t version;
    char augmentation[1];
    uint8_t code_align;
    uint8_t data_align;
    uint8_t return_column;
} DebugFrameCIE;

typedef struct QEMU_PACKED {
    uint32_t len __attribute__((aligned((sizeof(void *)))));
    uint32_t cie_offset;
    uintptr_t func_start;
    uintptr_t func_len;
} DebugFrameFDEHeader;

typedef struct QEMU_PACKED {
    DebugFrameCIE cie;
    DebugFrameFDEHeader fde;
} DebugFrameHeader;

typedef struct TCGLabelQemuLdst {
    bool is_ld;             /* qemu_ld: true, qemu_st: false */
    MemOpIdx oi;
    TCGType type;           /* result type of a load */
    TCGReg addrlo_reg;      /* reg index for low word of guest virtual addr */
    TCGReg addrhi_reg;      /* reg index for high word of guest virtual addr */
    TCGReg datalo_reg;      /* reg index for low word to be loaded or stored */
    TCGReg datahi_reg;      /* reg index for high word to be loaded or stored */
    const tcg_insn_unit *raddr;   /* addr of the next IR of qemu_ld/st IR */
    tcg_insn_unit *label_ptr[2]; /* label pointers to be updated */
    QSIMPLEQ_ENTRY(TCGLabelQemuLdst) next;
} TCGLabelQemuLdst;

static void tcg_register_jit_int(const void *buf, size_t size,
                                 const void *debug_frame,
                                 size_t debug_frame_size)
    __attribute__((unused));

/* Forward declarations for functions declared and used in tcg-target.c.inc. */
static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1,
                       intptr_t arg2);
static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg);
static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg);
static void tcg_out_ext8s(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg);
static void tcg_out_ext16s(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg);
static void tcg_out_ext8u(TCGContext *s, TCGReg ret, TCGReg arg);
static void tcg_out_ext16u(TCGContext *s, TCGReg ret, TCGReg arg);
static void tcg_out_ext32s(TCGContext *s, TCGReg ret, TCGReg arg);
static void tcg_out_ext32u(TCGContext *s, TCGReg ret, TCGReg arg);
static void tcg_out_exts_i32_i64(TCGContext *s, TCGReg ret, TCGReg arg);
static void tcg_out_extu_i32_i64(TCGContext *s, TCGReg ret, TCGReg arg);
static void tcg_out_extrl_i64_i32(TCGContext *s, TCGReg ret, TCGReg arg);
static void tcg_out_addi_ptr(TCGContext *s, TCGReg, TCGReg, tcg_target_long);
static bool tcg_out_xchg(TCGContext *s, TCGType type, TCGReg r1, TCGReg r2);
static void tcg_out_exit_tb(TCGContext *s, uintptr_t arg);
static void tcg_out_goto_tb(TCGContext *s, int which);
static void tcg_out_op(TCGContext *s, TCGOpcode opc,
                       const TCGArg args[TCG_MAX_OP_ARGS],
                       const int const_args[TCG_MAX_OP_ARGS]);
#if TCG_TARGET_MAYBE_vec
static bool tcg_out_dup_vec(TCGContext *s, TCGType type, unsigned vece,
                            TCGReg dst, TCGReg src);
static bool tcg_out_dupm_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg dst, TCGReg base, intptr_t offset);
static void tcg_out_dupi_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg dst, int64_t arg);
static void tcg_out_vec_op(TCGContext *s, TCGOpcode opc,
                           unsigned vecl, unsigned vece,
                           const TCGArg args[TCG_MAX_OP_ARGS],
                           const int const_args[TCG_MAX_OP_ARGS]);
#else
static inline bool tcg_out_dup_vec(TCGContext *s, TCGType type, unsigned vece,
                                   TCGReg dst, TCGReg src)
{
    g_assert_not_reached();
}
static inline bool tcg_out_dupm_vec(TCGContext *s, TCGType type, unsigned vece,
                                    TCGReg dst, TCGReg base, intptr_t offset)
{
    g_assert_not_reached();
}
static inline void tcg_out_dupi_vec(TCGContext *s, TCGType type, unsigned vece,
                                    TCGReg dst, int64_t arg)
{
    g_assert_not_reached();
}
static inline void tcg_out_vec_op(TCGContext *s, TCGOpcode opc,
                                  unsigned vecl, unsigned vece,
                                  const TCGArg args[TCG_MAX_OP_ARGS],
                                  const int const_args[TCG_MAX_OP_ARGS])
{
    g_assert_not_reached();
}
#endif
static void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg, TCGReg arg1,
                       intptr_t arg2);
static bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                        TCGReg base, intptr_t ofs);
static void tcg_out_call(TCGContext *s, const tcg_insn_unit *target,
                         const TCGHelperInfo *info);
static TCGReg tcg_target_call_oarg_reg(TCGCallReturnKind kind, int slot);
static bool tcg_target_const_match(int64_t val, TCGType type, int ct);
#ifdef TCG_TARGET_NEED_LDST_LABELS
static int tcg_out_ldst_finalize(TCGContext *s);
#endif

typedef struct TCGLdstHelperParam {
    TCGReg (*ra_gen)(TCGContext *s, const TCGLabelQemuLdst *l, int arg_reg);
    unsigned ntmp;
    int tmp[3];
} TCGLdstHelperParam;

static void tcg_out_ld_helper_args(TCGContext *s, const TCGLabelQemuLdst *l,
                                   const TCGLdstHelperParam *p)
    __attribute__((unused));
static void tcg_out_ld_helper_ret(TCGContext *s, const TCGLabelQemuLdst *l,
                                  bool load_sign, const TCGLdstHelperParam *p)
    __attribute__((unused));
static void tcg_out_st_helper_args(TCGContext *s, const TCGLabelQemuLdst *l,
                                   const TCGLdstHelperParam *p)
    __attribute__((unused));

static void * const qemu_ld_helpers[MO_SSIZE + 1] __attribute__((unused)) = {
    [MO_UB] = helper_ldub_mmu,
    [MO_SB] = helper_ldsb_mmu,
    [MO_UW] = helper_lduw_mmu,
    [MO_SW] = helper_ldsw_mmu,
    [MO_UL] = helper_ldul_mmu,
    [MO_UQ] = helper_ldq_mmu,
#if TCG_TARGET_REG_BITS == 64
    [MO_SL] = helper_ldsl_mmu,
#endif
};

static void * const qemu_st_helpers[MO_SIZE + 1] __attribute__((unused)) = {
    [MO_8]  = helper_stb_mmu,
    [MO_16] = helper_stw_mmu,
    [MO_32] = helper_stl_mmu,
    [MO_64] = helper_stq_mmu,
};

TCGContext tcg_init_ctx;
__thread TCGContext *tcg_ctx;

TCGContext **tcg_ctxs;
unsigned int tcg_cur_ctxs;
unsigned int tcg_max_ctxs;
TCGv_env cpu_env = 0;
const void *tcg_code_gen_epilogue;
uintptr_t tcg_splitwx_diff;

#ifndef CONFIG_TCG_INTERPRETER
tcg_prologue_fn *tcg_qemu_tb_exec;
#endif

static TCGRegSet tcg_target_available_regs[TCG_TYPE_COUNT];
static TCGRegSet tcg_target_call_clobber_regs;

#if TCG_TARGET_INSN_UNIT_SIZE == 1
static __attribute__((unused)) inline void tcg_out8(TCGContext *s, uint8_t v)
{
    *s->code_ptr++ = v;
}

static __attribute__((unused)) inline void tcg_patch8(tcg_insn_unit *p,
                                                      uint8_t v)
{
    *p = v;
}
#endif

#if TCG_TARGET_INSN_UNIT_SIZE <= 2
static __attribute__((unused)) inline void tcg_out16(TCGContext *s, uint16_t v)
{
    if (TCG_TARGET_INSN_UNIT_SIZE == 2) {
        *s->code_ptr++ = v;
    } else {
        tcg_insn_unit *p = s->code_ptr;
        memcpy(p, &v, sizeof(v));
        s->code_ptr = p + (2 / TCG_TARGET_INSN_UNIT_SIZE);
    }
}

static __attribute__((unused)) inline void tcg_patch16(tcg_insn_unit *p,
                                                       uint16_t v)
{
    if (TCG_TARGET_INSN_UNIT_SIZE == 2) {
        *p = v;
    } else {
        memcpy(p, &v, sizeof(v));
    }
}
#endif

#if TCG_TARGET_INSN_UNIT_SIZE <= 4
static __attribute__((unused)) inline void tcg_out32(TCGContext *s, uint32_t v)
{
    if (TCG_TARGET_INSN_UNIT_SIZE == 4) {
        *s->code_ptr++ = v;
    } else {
        tcg_insn_unit *p = s->code_ptr;
        memcpy(p, &v, sizeof(v));
        s->code_ptr = p + (4 / TCG_TARGET_INSN_UNIT_SIZE);
    }
}

static __attribute__((unused)) inline void tcg_patch32(tcg_insn_unit *p,
                                                       uint32_t v)
{
    if (TCG_TARGET_INSN_UNIT_SIZE == 4) {
        *p = v;
    } else {
        memcpy(p, &v, sizeof(v));
    }
}
#endif

#if TCG_TARGET_INSN_UNIT_SIZE <= 8
static __attribute__((unused)) inline void tcg_out64(TCGContext *s, uint64_t v)
{
    if (TCG_TARGET_INSN_UNIT_SIZE == 8) {
        *s->code_ptr++ = v;
    } else {
        tcg_insn_unit *p = s->code_ptr;
        memcpy(p, &v, sizeof(v));
        s->code_ptr = p + (8 / TCG_TARGET_INSN_UNIT_SIZE);
    }
}

static __attribute__((unused)) inline void tcg_patch64(tcg_insn_unit *p,
                                                       uint64_t v)
{
    if (TCG_TARGET_INSN_UNIT_SIZE == 8) {
        *p = v;
    } else {
        memcpy(p, &v, sizeof(v));
    }
}
#endif

/* label relocation processing */

static void tcg_out_reloc(TCGContext *s, tcg_insn_unit *code_ptr, int type,
                          TCGLabel *l, intptr_t addend)
{
    TCGRelocation *r = tcg_malloc(sizeof(TCGRelocation));

    r->type = type;
    r->ptr = code_ptr;
    r->addend = addend;
    QSIMPLEQ_INSERT_TAIL(&l->relocs, r, next);
}

static void tcg_out_label(TCGContext *s, TCGLabel *l)
{
    tcg_debug_assert(!l->has_value);
    l->has_value = 1;
    l->u.value_ptr = tcg_splitwx_to_rx(s->code_ptr);
}

TCGLabel *gen_new_label(void)
{
    TCGContext *s = tcg_ctx;
    TCGLabel *l = tcg_malloc(sizeof(TCGLabel));

    memset(l, 0, sizeof(TCGLabel));
    l->id = s->nb_labels++;
    QSIMPLEQ_INIT(&l->branches);
    QSIMPLEQ_INIT(&l->relocs);

    QSIMPLEQ_INSERT_TAIL(&s->labels, l, next);

    return l;
}

static bool tcg_resolve_relocs(TCGContext *s)
{
    TCGLabel *l;

    QSIMPLEQ_FOREACH(l, &s->labels, next) {
        TCGRelocation *r;
        uintptr_t value = l->u.value;

        QSIMPLEQ_FOREACH(r, &l->relocs, next) {
            if (!patch_reloc(r->ptr, r->type, value, r->addend)) {
                return false;
            }
        }
    }
    return true;
}

static void set_jmp_reset_offset(TCGContext *s, int which)
{
    /*
     * We will check for overflow at the end of the opcode loop in
     * tcg_gen_code, where we bound tcg_current_code_size to UINT16_MAX.
     */
    s->gen_tb->jmp_reset_offset[which] = tcg_current_code_size(s);
}

static void G_GNUC_UNUSED set_jmp_insn_offset(TCGContext *s, int which)
{
    /*
     * We will check for overflow at the end of the opcode loop in
     * tcg_gen_code, where we bound tcg_current_code_size to UINT16_MAX.
     */
    s->gen_tb->jmp_insn_offset[which] = tcg_current_code_size(s);
}

static uintptr_t G_GNUC_UNUSED get_jmp_target_addr(TCGContext *s, int which)
{
    /*
     * Return the read-execute version of the pointer, for the benefit
     * of any pc-relative addressing mode.
     */
    return (uintptr_t)tcg_splitwx_to_rx(&s->gen_tb->jmp_target_addr[which]);
}

/* Signal overflow, starting over with fewer guest insns. */
static G_NORETURN
void tcg_raise_tb_overflow(TCGContext *s)
{
    siglongjmp(s->jmp_trans, -2);
}

/*
 * Used by tcg_out_movext{1,2} to hold the arguments for tcg_out_movext.
 * By the time we arrive at tcg_out_movext1, @dst is always a TCGReg.
 *
 * However, tcg_out_helper_load_slots reuses this field to hold an
 * argument slot number (which may designate a argument register or an
 * argument stack slot), converting to TCGReg once all arguments that
 * are destined for the stack are processed.
 */
typedef struct TCGMovExtend {
    unsigned dst;
    TCGReg src;
    TCGType dst_type;
    TCGType src_type;
    MemOp src_ext;
} TCGMovExtend;

/**
 * tcg_out_movext -- move and extend
 * @s: tcg context
 * @dst_type: integral type for destination
 * @dst: destination register
 * @src_type: integral type for source
 * @src_ext: extension to apply to source
 * @src: source register
 *
 * Move or extend @src into @dst, depending on @src_ext and the types.
 */
static void tcg_out_movext(TCGContext *s, TCGType dst_type, TCGReg dst,
                           TCGType src_type, MemOp src_ext, TCGReg src)
{
    switch (src_ext) {
    case MO_UB:
        tcg_out_ext8u(s, dst, src);
        break;
    case MO_SB:
        tcg_out_ext8s(s, dst_type, dst, src);
        break;
    case MO_UW:
        tcg_out_ext16u(s, dst, src);
        break;
    case MO_SW:
        tcg_out_ext16s(s, dst_type, dst, src);
        break;
    case MO_UL:
    case MO_SL:
        if (dst_type == TCG_TYPE_I32) {
            if (src_type == TCG_TYPE_I32) {
                tcg_out_mov(s, TCG_TYPE_I32, dst, src);
            } else {
                tcg_out_extrl_i64_i32(s, dst, src);
            }
        } else if (src_type == TCG_TYPE_I32) {
            if (src_ext & MO_SIGN) {
                tcg_out_exts_i32_i64(s, dst, src);
            } else {
                tcg_out_extu_i32_i64(s, dst, src);
            }
        } else {
            if (src_ext & MO_SIGN) {
                tcg_out_ext32s(s, dst, src);
            } else {
                tcg_out_ext32u(s, dst, src);
            }
        }
        break;
    case MO_UQ:
        tcg_debug_assert(TCG_TARGET_REG_BITS == 64);
        if (dst_type == TCG_TYPE_I32) {
            tcg_out_extrl_i64_i32(s, dst, src);
        } else {
            tcg_out_mov(s, TCG_TYPE_I64, dst, src);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

/* Minor variations on a theme, using a structure. */
static void tcg_out_movext1_new_src(TCGContext *s, const TCGMovExtend *i,
                                    TCGReg src)
{
    tcg_out_movext(s, i->dst_type, i->dst, i->src_type, i->src_ext, src);
}

static void tcg_out_movext1(TCGContext *s, const TCGMovExtend *i)
{
    tcg_out_movext1_new_src(s, i, i->src);
}

/**
 * tcg_out_movext2 -- move and extend two pair
 * @s: tcg context
 * @i1: first move description
 * @i2: second move description
 * @scratch: temporary register, or -1 for none
 *
 * As tcg_out_movext, for both @i1 and @i2, caring for overlap
 * between the sources and destinations.
 */

static void tcg_out_movext2(TCGContext *s, const TCGMovExtend *i1,
                            const TCGMovExtend *i2, int scratch)
{
    TCGReg src1 = i1->src;
    TCGReg src2 = i2->src;

    if (i1->dst != src2) {
        tcg_out_movext1(s, i1);
        tcg_out_movext1(s, i2);
        return;
    }
    if (i2->dst == src1) {
        TCGType src1_type = i1->src_type;
        TCGType src2_type = i2->src_type;

        if (tcg_out_xchg(s, MAX(src1_type, src2_type), src1, src2)) {
            /* The data is now in the correct registers, now extend. */
            src1 = i2->src;
            src2 = i1->src;
        } else {
            tcg_debug_assert(scratch >= 0);
            tcg_out_mov(s, src1_type, scratch, src1);
            src1 = scratch;
        }
    }
    tcg_out_movext1_new_src(s, i2, src2);
    tcg_out_movext1_new_src(s, i1, src1);
}

/**
 * tcg_out_movext3 -- move and extend three pair
 * @s: tcg context
 * @i1: first move description
 * @i2: second move description
 * @i3: third move description
 * @scratch: temporary register, or -1 for none
 *
 * As tcg_out_movext, for all of @i1, @i2 and @i3, caring for overlap
 * between the sources and destinations.
 */

static void tcg_out_movext3(TCGContext *s, const TCGMovExtend *i1,
                            const TCGMovExtend *i2, const TCGMovExtend *i3,
                            int scratch)
{
    TCGReg src1 = i1->src;
    TCGReg src2 = i2->src;
    TCGReg src3 = i3->src;

    if (i1->dst != src2 && i1->dst != src3) {
        tcg_out_movext1(s, i1);
        tcg_out_movext2(s, i2, i3, scratch);
        return;
    }
    if (i2->dst != src1 && i2->dst != src3) {
        tcg_out_movext1(s, i2);
        tcg_out_movext2(s, i1, i3, scratch);
        return;
    }
    if (i3->dst != src1 && i3->dst != src2) {
        tcg_out_movext1(s, i3);
        tcg_out_movext2(s, i1, i2, scratch);
        return;
    }

    /*
     * There is a cycle.  Since there are only 3 nodes, the cycle is
     * either "clockwise" or "anti-clockwise", and can be solved with
     * a single scratch or two xchg.
     */
    if (i1->dst == src2 && i2->dst == src3 && i3->dst == src1) {
        /* "Clockwise" */
        if (tcg_out_xchg(s, MAX(i1->src_type, i2->src_type), src1, src2)) {
            tcg_out_xchg(s, MAX(i2->src_type, i3->src_type), src2, src3);
            /* The data is now in the correct registers, now extend. */
            tcg_out_movext1_new_src(s, i1, i1->dst);
            tcg_out_movext1_new_src(s, i2, i2->dst);
            tcg_out_movext1_new_src(s, i3, i3->dst);
        } else {
            tcg_debug_assert(scratch >= 0);
            tcg_out_mov(s, i1->src_type, scratch, src1);
            tcg_out_movext1(s, i3);
            tcg_out_movext1(s, i2);
            tcg_out_movext1_new_src(s, i1, scratch);
        }
    } else if (i1->dst == src3 && i2->dst == src1 && i3->dst == src2) {
        /* "Anti-clockwise" */
        if (tcg_out_xchg(s, MAX(i2->src_type, i3->src_type), src2, src3)) {
            tcg_out_xchg(s, MAX(i1->src_type, i2->src_type), src1, src2);
            /* The data is now in the correct registers, now extend. */
            tcg_out_movext1_new_src(s, i1, i1->dst);
            tcg_out_movext1_new_src(s, i2, i2->dst);
            tcg_out_movext1_new_src(s, i3, i3->dst);
        } else {
            tcg_debug_assert(scratch >= 0);
            tcg_out_mov(s, i1->src_type, scratch, src1);
            tcg_out_movext1(s, i2);
            tcg_out_movext1(s, i3);
            tcg_out_movext1_new_src(s, i1, scratch);
        }
    } else {
        g_assert_not_reached();
    }
}

#define C_PFX1(P, A)                    P##A
#define C_PFX2(P, A, B)                 P##A##_##B
#define C_PFX3(P, A, B, C)              P##A##_##B##_##C
#define C_PFX4(P, A, B, C, D)           P##A##_##B##_##C##_##D
#define C_PFX5(P, A, B, C, D, E)        P##A##_##B##_##C##_##D##_##E
#define C_PFX6(P, A, B, C, D, E, F)     P##A##_##B##_##C##_##D##_##E##_##F

/* Define an enumeration for the various combinations. */

#define C_O0_I1(I1)                     C_PFX1(c_o0_i1_, I1),
#define C_O0_I2(I1, I2)                 C_PFX2(c_o0_i2_, I1, I2),
#define C_O0_I3(I1, I2, I3)             C_PFX3(c_o0_i3_, I1, I2, I3),
#define C_O0_I4(I1, I2, I3, I4)         C_PFX4(c_o0_i4_, I1, I2, I3, I4),

#define C_O1_I1(O1, I1)                 C_PFX2(c_o1_i1_, O1, I1),
#define C_O1_I2(O1, I1, I2)             C_PFX3(c_o1_i2_, O1, I1, I2),
#define C_O1_I3(O1, I1, I2, I3)         C_PFX4(c_o1_i3_, O1, I1, I2, I3),
#define C_O1_I4(O1, I1, I2, I3, I4)     C_PFX5(c_o1_i4_, O1, I1, I2, I3, I4),

#define C_N1_I2(O1, I1, I2)             C_PFX3(c_n1_i2_, O1, I1, I2),

#define C_O2_I1(O1, O2, I1)             C_PFX3(c_o2_i1_, O1, O2, I1),
#define C_O2_I2(O1, O2, I1, I2)         C_PFX4(c_o2_i2_, O1, O2, I1, I2),
#define C_O2_I3(O1, O2, I1, I2, I3)     C_PFX5(c_o2_i3_, O1, O2, I1, I2, I3),
#define C_O2_I4(O1, O2, I1, I2, I3, I4) C_PFX6(c_o2_i4_, O1, O2, I1, I2, I3, I4),

typedef enum {
#include "tcg-target-con-set.h"
} TCGConstraintSetIndex;

static TCGConstraintSetIndex tcg_target_op_def(TCGOpcode);

#undef C_O0_I1
#undef C_O0_I2
#undef C_O0_I3
#undef C_O0_I4
#undef C_O1_I1
#undef C_O1_I2
#undef C_O1_I3
#undef C_O1_I4
#undef C_N1_I2
#undef C_O2_I1
#undef C_O2_I2
#undef C_O2_I3
#undef C_O2_I4

/* Put all of the constraint sets into an array, indexed by the enum. */

#define C_O0_I1(I1)                     { .args_ct_str = { #I1 } },
#define C_O0_I2(I1, I2)                 { .args_ct_str = { #I1, #I2 } },
#define C_O0_I3(I1, I2, I3)             { .args_ct_str = { #I1, #I2, #I3 } },
#define C_O0_I4(I1, I2, I3, I4)         { .args_ct_str = { #I1, #I2, #I3, #I4 } },

#define C_O1_I1(O1, I1)                 { .args_ct_str = { #O1, #I1 } },
#define C_O1_I2(O1, I1, I2)             { .args_ct_str = { #O1, #I1, #I2 } },
#define C_O1_I3(O1, I1, I2, I3)         { .args_ct_str = { #O1, #I1, #I2, #I3 } },
#define C_O1_I4(O1, I1, I2, I3, I4)     { .args_ct_str = { #O1, #I1, #I2, #I3, #I4 } },

#define C_N1_I2(O1, I1, I2)             { .args_ct_str = { "&" #O1, #I1, #I2 } },

#define C_O2_I1(O1, O2, I1)             { .args_ct_str = { #O1, #O2, #I1 } },
#define C_O2_I2(O1, O2, I1, I2)         { .args_ct_str = { #O1, #O2, #I1, #I2 } },
#define C_O2_I3(O1, O2, I1, I2, I3)     { .args_ct_str = { #O1, #O2, #I1, #I2, #I3 } },
#define C_O2_I4(O1, O2, I1, I2, I3, I4) { .args_ct_str = { #O1, #O2, #I1, #I2, #I3, #I4 } },

static const TCGTargetOpDef constraint_sets[] = {
#include "tcg-target-con-set.h"
};


#undef C_O0_I1
#undef C_O0_I2
#undef C_O0_I3
#undef C_O0_I4
#undef C_O1_I1
#undef C_O1_I2
#undef C_O1_I3
#undef C_O1_I4
#undef C_N1_I2
#undef C_O2_I1
#undef C_O2_I2
#undef C_O2_I3
#undef C_O2_I4

/* Expand the enumerator to be returned from tcg_target_op_def(). */

#define C_O0_I1(I1)                     C_PFX1(c_o0_i1_, I1)
#define C_O0_I2(I1, I2)                 C_PFX2(c_o0_i2_, I1, I2)
#define C_O0_I3(I1, I2, I3)             C_PFX3(c_o0_i3_, I1, I2, I3)
#define C_O0_I4(I1, I2, I3, I4)         C_PFX4(c_o0_i4_, I1, I2, I3, I4)

#define C_O1_I1(O1, I1)                 C_PFX2(c_o1_i1_, O1, I1)
#define C_O1_I2(O1, I1, I2)             C_PFX3(c_o1_i2_, O1, I1, I2)
#define C_O1_I3(O1, I1, I2, I3)         C_PFX4(c_o1_i3_, O1, I1, I2, I3)
#define C_O1_I4(O1, I1, I2, I3, I4)     C_PFX5(c_o1_i4_, O1, I1, I2, I3, I4)

#define C_N1_I2(O1, I1, I2)             C_PFX3(c_n1_i2_, O1, I1, I2)

#define C_O2_I1(O1, O2, I1)             C_PFX3(c_o2_i1_, O1, O2, I1)
#define C_O2_I2(O1, O2, I1, I2)         C_PFX4(c_o2_i2_, O1, O2, I1, I2)
#define C_O2_I3(O1, O2, I1, I2, I3)     C_PFX5(c_o2_i3_, O1, O2, I1, I2, I3)
#define C_O2_I4(O1, O2, I1, I2, I3, I4) C_PFX6(c_o2_i4_, O1, O2, I1, I2, I3, I4)

#include "tcg-target.c.inc"

static void alloc_tcg_plugin_context(TCGContext *s)
{
#ifdef CONFIG_PLUGIN
    s->plugin_tb = g_new0(struct qemu_plugin_tb, 1);
    s->plugin_tb->insns =
        g_ptr_array_new_with_free_func(qemu_plugin_insn_cleanup_fn);
#endif
}

/*
 * All TCG threads except the parent (i.e. the one that called tcg_context_init
 * and registered the target's TCG globals) must register with this function
 * before initiating translation.
 *
 * In user-mode we just point tcg_ctx to tcg_init_ctx. See the documentation
 * of tcg_region_init() for the reasoning behind this.
 *
 * In softmmu each caller registers its context in tcg_ctxs[]. Note that in
 * softmmu tcg_ctxs[] does not track tcg_ctx_init, since the initial context
 * is not used anymore for translation once this function is called.
 *
 * Not tracking tcg_init_ctx in tcg_ctxs[] in softmmu keeps code that iterates
 * over the array (e.g. tcg_code_size() the same for both softmmu and user-mode.
 */
#ifdef CONFIG_USER_ONLY
void tcg_register_thread(void)
{
    tcg_ctx = &tcg_init_ctx;
}
#else
void tcg_register_thread(void)
{
    TCGContext *s = g_malloc(sizeof(*s));
    unsigned int i, n;

    *s = tcg_init_ctx;

    /* Relink mem_base.  */
    for (i = 0, n = tcg_init_ctx.nb_globals; i < n; ++i) {
        if (tcg_init_ctx.temps[i].mem_base) {
            ptrdiff_t b = tcg_init_ctx.temps[i].mem_base - tcg_init_ctx.temps;
            tcg_debug_assert(b >= 0 && b < n);
            s->temps[i].mem_base = &s->temps[b];
        }
    }

    /* Claim an entry in tcg_ctxs */
    n = qatomic_fetch_inc(&tcg_cur_ctxs);
    g_assert(n < tcg_max_ctxs);
    qatomic_set(&tcg_ctxs[n], s);

    if (n > 0) {
        alloc_tcg_plugin_context(s);
        tcg_region_initial_alloc(s);
    }

    tcg_ctx = s;
}
#endif /* !CONFIG_USER_ONLY */

/* pool based memory allocation */
void *tcg_malloc_internal(TCGContext *s, int size)
{
    TCGPool *p;
    int pool_size;

    if (size > TCG_POOL_CHUNK_SIZE) {
        /* big malloc: insert a new pool (XXX: could optimize) */
        p = g_malloc(sizeof(TCGPool) + size);
        p->size = size;
        p->next = s->pool_first_large;
        s->pool_first_large = p;
        return p->data;
    } else {
        p = s->pool_current;
        if (!p) {
            p = s->pool_first;
            if (!p)
                goto new_pool;
        } else {
            if (!p->next) {
            new_pool:
                pool_size = TCG_POOL_CHUNK_SIZE;
                p = g_malloc(sizeof(TCGPool) + pool_size);
                p->size = pool_size;
                p->next = NULL;
                if (s->pool_current) {
                    s->pool_current->next = p;
                } else {
                    s->pool_first = p;
                }
            } else {
                p = p->next;
            }
        }
    }
    s->pool_current = p;
    s->pool_cur = p->data + size;
    s->pool_end = p->data + p->size;
    return p->data;
}

void tcg_pool_reset(TCGContext *s)
{
    TCGPool *p, *t;
    for (p = s->pool_first_large; p; p = t) {
        t = p->next;
        g_free(p);
    }
    s->pool_first_large = NULL;
    s->pool_cur = s->pool_end = NULL;
    s->pool_current = NULL;
}

#include "exec/helper-proto.h"

static TCGHelperInfo all_helpers[] = {
#include "exec/helper-tcg.h"
};
static GHashTable *helper_table;

/*
 * Create TCGHelperInfo structures for "tcg/tcg-ldst.h" functions,
 * akin to what "exec/helper-tcg.h" does with DEF_HELPER_FLAGS_N.
 * We only use these for layout in tcg_out_ld_helper_ret and
 * tcg_out_st_helper_args, and share them between several of
 * the helpers, with the end result that it's easier to build manually.
 */

#if TCG_TARGET_REG_BITS == 32
# define dh_typecode_ttl  dh_typecode_i32
#else
# define dh_typecode_ttl  dh_typecode_i64
#endif

static TCGHelperInfo info_helper_ld32_mmu = {
    .flags = TCG_CALL_NO_WG,
    .typemask = dh_typemask(ttl, 0)  /* return tcg_target_ulong */
              | dh_typemask(env, 1)
              | dh_typemask(tl, 2)   /* target_ulong addr */
              | dh_typemask(i32, 3)  /* unsigned oi */
              | dh_typemask(ptr, 4)  /* uintptr_t ra */
};

static TCGHelperInfo info_helper_ld64_mmu = {
    .flags = TCG_CALL_NO_WG,
    .typemask = dh_typemask(i64, 0)  /* return uint64_t */
              | dh_typemask(env, 1)
              | dh_typemask(tl, 2)   /* target_ulong addr */
              | dh_typemask(i32, 3)  /* unsigned oi */
              | dh_typemask(ptr, 4)  /* uintptr_t ra */
};

static TCGHelperInfo info_helper_st32_mmu = {
    .flags = TCG_CALL_NO_WG,
    .typemask = dh_typemask(void, 0)
              | dh_typemask(env, 1)
              | dh_typemask(tl, 2)   /* target_ulong addr */
              | dh_typemask(i32, 3)  /* uint32_t data */
              | dh_typemask(i32, 4)  /* unsigned oi */
              | dh_typemask(ptr, 5)  /* uintptr_t ra */
};

static TCGHelperInfo info_helper_st64_mmu = {
    .flags = TCG_CALL_NO_WG,
    .typemask = dh_typemask(void, 0)
              | dh_typemask(env, 1)
              | dh_typemask(tl, 2)   /* target_ulong addr */
              | dh_typemask(i64, 3)  /* uint64_t data */
              | dh_typemask(i32, 4)  /* unsigned oi */
              | dh_typemask(ptr, 5)  /* uintptr_t ra */
};

#ifdef CONFIG_TCG_INTERPRETER
static ffi_type *typecode_to_ffi(int argmask)
{
    /*
     * libffi does not support __int128_t, so we have forced Int128
     * to use the structure definition instead of the builtin type.
     */
    static ffi_type *ffi_type_i128_elements[3] = {
        &ffi_type_uint64,
        &ffi_type_uint64,
        NULL
    };
    static ffi_type ffi_type_i128 = {
        .size = 16,
        .alignment = __alignof__(Int128),
        .type = FFI_TYPE_STRUCT,
        .elements = ffi_type_i128_elements,
    };

    switch (argmask) {
    case dh_typecode_void:
        return &ffi_type_void;
    case dh_typecode_i32:
        return &ffi_type_uint32;
    case dh_typecode_s32:
        return &ffi_type_sint32;
    case dh_typecode_i64:
        return &ffi_type_uint64;
    case dh_typecode_s64:
        return &ffi_type_sint64;
    case dh_typecode_ptr:
        return &ffi_type_pointer;
    case dh_typecode_i128:
        return &ffi_type_i128;
    }
    g_assert_not_reached();
}

static void init_ffi_layouts(void)
{
    /* g_direct_hash/equal for direct comparisons on uint32_t.  */
    GHashTable *ffi_table = g_hash_table_new(NULL, NULL);

    for (int i = 0; i < ARRAY_SIZE(all_helpers); ++i) {
        TCGHelperInfo *info = &all_helpers[i];
        unsigned typemask = info->typemask;
        gpointer hash = (gpointer)(uintptr_t)typemask;
        struct {
            ffi_cif cif;
            ffi_type *args[];
        } *ca;
        ffi_status status;
        int nargs;
        ffi_cif *cif;

        cif = g_hash_table_lookup(ffi_table, hash);
        if (cif) {
            info->cif = cif;
            continue;
        }

        /* Ignoring the return type, find the last non-zero field. */
        nargs = 32 - clz32(typemask >> 3);
        nargs = DIV_ROUND_UP(nargs, 3);
        assert(nargs <= MAX_CALL_IARGS);

        ca = g_malloc0(sizeof(*ca) + nargs * sizeof(ffi_type *));
        ca->cif.rtype = typecode_to_ffi(typemask & 7);
        ca->cif.nargs = nargs;

        if (nargs != 0) {
            ca->cif.arg_types = ca->args;
            for (int j = 0; j < nargs; ++j) {
                int typecode = extract32(typemask, (j + 1) * 3, 3);
                ca->args[j] = typecode_to_ffi(typecode);
            }
        }

        status = ffi_prep_cif(&ca->cif, FFI_DEFAULT_ABI, nargs,
                              ca->cif.rtype, ca->cif.arg_types);
        assert(status == FFI_OK);

        cif = &ca->cif;
        info->cif = cif;
        g_hash_table_insert(ffi_table, hash, (gpointer)cif);
    }

    g_hash_table_destroy(ffi_table);
}
#endif /* CONFIG_TCG_INTERPRETER */

static inline bool arg_slot_reg_p(unsigned arg_slot)
{
    /*
     * Split the sizeof away from the comparison to avoid Werror from
     * "unsigned < 0 is always false", when iarg_regs is empty.
     */
    unsigned nreg = ARRAY_SIZE(tcg_target_call_iarg_regs);
    return arg_slot < nreg;
}

static inline int arg_slot_stk_ofs(unsigned arg_slot)
{
    unsigned max = TCG_STATIC_CALL_ARGS_SIZE / sizeof(tcg_target_long);
    unsigned stk_slot = arg_slot - ARRAY_SIZE(tcg_target_call_iarg_regs);

    tcg_debug_assert(stk_slot < max);
    return TCG_TARGET_CALL_STACK_OFFSET + stk_slot * sizeof(tcg_target_long);
}

typedef struct TCGCumulativeArgs {
    int arg_idx;                /* tcg_gen_callN args[] */
    int info_in_idx;            /* TCGHelperInfo in[] */
    int arg_slot;               /* regs+stack slot */
    int ref_slot;               /* stack slots for references */
} TCGCumulativeArgs;

static void layout_arg_even(TCGCumulativeArgs *cum)
{
    cum->arg_slot += cum->arg_slot & 1;
}

static void layout_arg_1(TCGCumulativeArgs *cum, TCGHelperInfo *info,
                         TCGCallArgumentKind kind)
{
    TCGCallArgumentLoc *loc = &info->in[cum->info_in_idx];

    *loc = (TCGCallArgumentLoc){
        .kind = kind,
        .arg_idx = cum->arg_idx,
        .arg_slot = cum->arg_slot,
    };
    cum->info_in_idx++;
    cum->arg_slot++;
}

static void layout_arg_normal_n(TCGCumulativeArgs *cum,
                                TCGHelperInfo *info, int n)
{
    TCGCallArgumentLoc *loc = &info->in[cum->info_in_idx];

    for (int i = 0; i < n; ++i) {
        /* Layout all using the same arg_idx, adjusting the subindex. */
        loc[i] = (TCGCallArgumentLoc){
            .kind = TCG_CALL_ARG_NORMAL,
            .arg_idx = cum->arg_idx,
            .tmp_subindex = i,
            .arg_slot = cum->arg_slot + i,
        };
    }
    cum->info_in_idx += n;
    cum->arg_slot += n;
}

static void layout_arg_by_ref(TCGCumulativeArgs *cum, TCGHelperInfo *info)
{
    TCGCallArgumentLoc *loc = &info->in[cum->info_in_idx];
    int n = 128 / TCG_TARGET_REG_BITS;

    /* The first subindex carries the pointer. */
    layout_arg_1(cum, info, TCG_CALL_ARG_BY_REF);

    /*
     * The callee is allowed to clobber memory associated with
     * structure pass by-reference.  Therefore we must make copies.
     * Allocate space from "ref_slot", which will be adjusted to
     * follow the parameters on the stack.
     */
    loc[0].ref_slot = cum->ref_slot;

    /*
     * Subsequent words also go into the reference slot, but
     * do not accumulate into the regular arguments.
     */
    for (int i = 1; i < n; ++i) {
        loc[i] = (TCGCallArgumentLoc){
            .kind = TCG_CALL_ARG_BY_REF_N,
            .arg_idx = cum->arg_idx,
            .tmp_subindex = i,
            .ref_slot = cum->ref_slot + i,
        };
    }
    cum->info_in_idx += n;
    cum->ref_slot += n;
}

static void init_call_layout(TCGHelperInfo *info)
{
    int max_reg_slots = ARRAY_SIZE(tcg_target_call_iarg_regs);
    int max_stk_slots = TCG_STATIC_CALL_ARGS_SIZE / sizeof(tcg_target_long);
    unsigned typemask = info->typemask;
    unsigned typecode;
    TCGCumulativeArgs cum = { };

    /*
     * Parse and place any function return value.
     */
    typecode = typemask & 7;
    switch (typecode) {
    case dh_typecode_void:
        info->nr_out = 0;
        break;
    case dh_typecode_i32:
    case dh_typecode_s32:
    case dh_typecode_ptr:
        info->nr_out = 1;
        info->out_kind = TCG_CALL_RET_NORMAL;
        break;
    case dh_typecode_i64:
    case dh_typecode_s64:
        info->nr_out = 64 / TCG_TARGET_REG_BITS;
        info->out_kind = TCG_CALL_RET_NORMAL;
        /* Query the last register now to trigger any assert early. */
        tcg_target_call_oarg_reg(info->out_kind, info->nr_out - 1);
        break;
    case dh_typecode_i128:
        info->nr_out = 128 / TCG_TARGET_REG_BITS;
        info->out_kind = TCG_TARGET_CALL_RET_I128;
        switch (TCG_TARGET_CALL_RET_I128) {
        case TCG_CALL_RET_NORMAL:
            /* Query the last register now to trigger any assert early. */
            tcg_target_call_oarg_reg(info->out_kind, info->nr_out - 1);
            break;
        case TCG_CALL_RET_BY_VEC:
            /* Query the single register now to trigger any assert early. */
            tcg_target_call_oarg_reg(TCG_CALL_RET_BY_VEC, 0);
            break;
        case TCG_CALL_RET_BY_REF:
            /*
             * Allocate the first argument to the output.
             * We don't need to store this anywhere, just make it
             * unavailable for use in the input loop below.
             */
            cum.arg_slot = 1;
            break;
        default:
            qemu_build_not_reached();
        }
        break;
    default:
        g_assert_not_reached();
    }

    /*
     * Parse and place function arguments.
     */
    for (typemask >>= 3; typemask; typemask >>= 3, cum.arg_idx++) {
        TCGCallArgumentKind kind;
        TCGType type;

        typecode = typemask & 7;
        switch (typecode) {
        case dh_typecode_i32:
        case dh_typecode_s32:
            type = TCG_TYPE_I32;
            break;
        case dh_typecode_i64:
        case dh_typecode_s64:
            type = TCG_TYPE_I64;
            break;
        case dh_typecode_ptr:
            type = TCG_TYPE_PTR;
            break;
        case dh_typecode_i128:
            type = TCG_TYPE_I128;
            break;
        default:
            g_assert_not_reached();
        }

        switch (type) {
        case TCG_TYPE_I32:
            switch (TCG_TARGET_CALL_ARG_I32) {
            case TCG_CALL_ARG_EVEN:
                layout_arg_even(&cum);
                /* fall through */
            case TCG_CALL_ARG_NORMAL:
                layout_arg_1(&cum, info, TCG_CALL_ARG_NORMAL);
                break;
            case TCG_CALL_ARG_EXTEND:
                kind = TCG_CALL_ARG_EXTEND_U + (typecode & 1);
                layout_arg_1(&cum, info, kind);
                break;
            default:
                qemu_build_not_reached();
            }
            break;

        case TCG_TYPE_I64:
            switch (TCG_TARGET_CALL_ARG_I64) {
            case TCG_CALL_ARG_EVEN:
                layout_arg_even(&cum);
                /* fall through */
            case TCG_CALL_ARG_NORMAL:
                if (TCG_TARGET_REG_BITS == 32) {
                    layout_arg_normal_n(&cum, info, 2);
                } else {
                    layout_arg_1(&cum, info, TCG_CALL_ARG_NORMAL);
                }
                break;
            default:
                qemu_build_not_reached();
            }
            break;

        case TCG_TYPE_I128:
            switch (TCG_TARGET_CALL_ARG_I128) {
            case TCG_CALL_ARG_EVEN:
                layout_arg_even(&cum);
                /* fall through */
            case TCG_CALL_ARG_NORMAL:
                layout_arg_normal_n(&cum, info, 128 / TCG_TARGET_REG_BITS);
                break;
            case TCG_CALL_ARG_BY_REF:
                layout_arg_by_ref(&cum, info);
                break;
            default:
                qemu_build_not_reached();
            }
            break;

        default:
            g_assert_not_reached();
        }
    }
    info->nr_in = cum.info_in_idx;

    /* Validate that we didn't overrun the input array. */
    assert(cum.info_in_idx <= ARRAY_SIZE(info->in));
    /* Validate the backend has enough argument space. */
    assert(cum.arg_slot <= max_reg_slots + max_stk_slots);

    /*
     * Relocate the "ref_slot" area to the end of the parameters.
     * Minimizing this stack offset helps code size for x86,
     * which has a signed 8-bit offset encoding.
     */
    if (cum.ref_slot != 0) {
        int ref_base = 0;

        if (cum.arg_slot > max_reg_slots) {
            int align = __alignof(Int128) / sizeof(tcg_target_long);

            ref_base = cum.arg_slot - max_reg_slots;
            if (align > 1) {
                ref_base = ROUND_UP(ref_base, align);
            }
        }
        assert(ref_base + cum.ref_slot <= max_stk_slots);
        ref_base += max_reg_slots;

        if (ref_base != 0) {
            for (int i = cum.info_in_idx - 1; i >= 0; --i) {
                TCGCallArgumentLoc *loc = &info->in[i];
                switch (loc->kind) {
                case TCG_CALL_ARG_BY_REF:
                case TCG_CALL_ARG_BY_REF_N:
                    loc->ref_slot += ref_base;
                    break;
                default:
                    break;
                }
            }
        }
    }
}

static int indirect_reg_alloc_order[ARRAY_SIZE(tcg_target_reg_alloc_order)];
static void process_op_defs(TCGContext *s);
static TCGTemp *tcg_global_reg_new_internal(TCGContext *s, TCGType type,
                                            TCGReg reg, const char *name);

static void tcg_context_init(unsigned max_cpus)
{
    TCGContext *s = &tcg_init_ctx;
    int op, total_args, n, i;
    TCGOpDef *def;
    TCGArgConstraint *args_ct;
    TCGTemp *ts;

    memset(s, 0, sizeof(*s));
    s->nb_globals = 0;

    /* Count total number of arguments and allocate the corresponding
       space */
    total_args = 0;
    for(op = 0; op < NB_OPS; op++) {
        def = &tcg_op_defs[op];
        n = def->nb_iargs + def->nb_oargs;
        total_args += n;
    }

    args_ct = g_new0(TCGArgConstraint, total_args);

    for(op = 0; op < NB_OPS; op++) {
        def = &tcg_op_defs[op];
        def->args_ct = args_ct;
        n = def->nb_iargs + def->nb_oargs;
        args_ct += n;
    }

    /* Register helpers.  */
    /* Use g_direct_hash/equal for direct pointer comparisons on func.  */
    helper_table = g_hash_table_new(NULL, NULL);

    for (i = 0; i < ARRAY_SIZE(all_helpers); ++i) {
        init_call_layout(&all_helpers[i]);
        g_hash_table_insert(helper_table, (gpointer)all_helpers[i].func,
                            (gpointer)&all_helpers[i]);
    }

    init_call_layout(&info_helper_ld32_mmu);
    init_call_layout(&info_helper_ld64_mmu);
    init_call_layout(&info_helper_st32_mmu);
    init_call_layout(&info_helper_st64_mmu);

#ifdef CONFIG_TCG_INTERPRETER
    init_ffi_layouts();
#endif

    tcg_target_init(s);
    process_op_defs(s);

    /* Reverse the order of the saved registers, assuming they're all at
       the start of tcg_target_reg_alloc_order.  */
    for (n = 0; n < ARRAY_SIZE(tcg_target_reg_alloc_order); ++n) {
        int r = tcg_target_reg_alloc_order[n];
        if (tcg_regset_test_reg(tcg_target_call_clobber_regs, r)) {
            break;
        }
    }
    for (i = 0; i < n; ++i) {
        indirect_reg_alloc_order[i] = tcg_target_reg_alloc_order[n - 1 - i];
    }
    for (; i < ARRAY_SIZE(tcg_target_reg_alloc_order); ++i) {
        indirect_reg_alloc_order[i] = tcg_target_reg_alloc_order[i];
    }

    alloc_tcg_plugin_context(s);

    tcg_ctx = s;
    /*
     * In user-mode we simply share the init context among threads, since we
     * use a single region. See the documentation tcg_region_init() for the
     * reasoning behind this.
     * In softmmu we will have at most max_cpus TCG threads.
     */
#ifdef CONFIG_USER_ONLY
    tcg_ctxs = &tcg_ctx;
    tcg_cur_ctxs = 1;
    tcg_max_ctxs = 1;
#else
    tcg_max_ctxs = max_cpus;
    tcg_ctxs = g_new0(TCGContext *, max_cpus);
#endif

    tcg_debug_assert(!tcg_regset_test_reg(s->reserved_regs, TCG_AREG0));
    ts = tcg_global_reg_new_internal(s, TCG_TYPE_PTR, TCG_AREG0, "env");
    cpu_env = temp_tcgv_ptr(ts);
}

void tcg_init(size_t tb_size, int splitwx, unsigned max_cpus)
{
    tcg_context_init(max_cpus);
    tcg_region_init(tb_size, splitwx, max_cpus);
}

/*
 * Allocate TBs right before their corresponding translated code, making
 * sure that TBs and code are on different cache lines.
 */
TranslationBlock *tcg_tb_alloc(TCGContext *s)
{
    uintptr_t align = qemu_icache_linesize;
    TranslationBlock *tb;
    void *next;

 retry:
    tb = (void *)ROUND_UP((uintptr_t)s->code_gen_ptr, align);
    next = (void *)ROUND_UP((uintptr_t)(tb + 1), align);

    if (unlikely(next > s->code_gen_highwater)) {
        if (tcg_region_alloc(s)) {
            return NULL;
        }
        goto retry;
    }
    qatomic_set(&s->code_gen_ptr, next);
    s->data_gen_ptr = NULL;
    return tb;
}

void tcg_prologue_init(TCGContext *s)
{
    size_t prologue_size;

    s->code_ptr = s->code_gen_ptr;
    s->code_buf = s->code_gen_ptr;
    s->data_gen_ptr = NULL;

#ifndef CONFIG_TCG_INTERPRETER
    tcg_qemu_tb_exec = (tcg_prologue_fn *)tcg_splitwx_to_rx(s->code_ptr);
#endif

#ifdef TCG_TARGET_NEED_POOL_LABELS
    s->pool_labels = NULL;
#endif

    qemu_thread_jit_write();
    /* Generate the prologue.  */
    tcg_target_qemu_prologue(s);

#ifdef TCG_TARGET_NEED_POOL_LABELS
    /* Allow the prologue to put e.g. guest_base into a pool entry.  */
    {
        int result = tcg_out_pool_finalize(s);
        tcg_debug_assert(result == 0);
    }
#endif

    prologue_size = tcg_current_code_size(s);
    perf_report_prologue(s->code_gen_ptr, prologue_size);

#ifndef CONFIG_TCG_INTERPRETER
    flush_idcache_range((uintptr_t)tcg_splitwx_to_rx(s->code_buf),
                        (uintptr_t)s->code_buf, prologue_size);
#endif

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_OUT_ASM)) {
        FILE *logfile = qemu_log_trylock();
        if (logfile) {
            fprintf(logfile, "PROLOGUE: [size=%zu]\n", prologue_size);
            if (s->data_gen_ptr) {
                size_t code_size = s->data_gen_ptr - s->code_gen_ptr;
                size_t data_size = prologue_size - code_size;
                size_t i;

                disas(logfile, s->code_gen_ptr, code_size);

                for (i = 0; i < data_size; i += sizeof(tcg_target_ulong)) {
                    if (sizeof(tcg_target_ulong) == 8) {
                        fprintf(logfile,
                                "0x%08" PRIxPTR ":  .quad  0x%016" PRIx64 "\n",
                                (uintptr_t)s->data_gen_ptr + i,
                                *(uint64_t *)(s->data_gen_ptr + i));
                    } else {
                        fprintf(logfile,
                                "0x%08" PRIxPTR ":  .long  0x%08x\n",
                                (uintptr_t)s->data_gen_ptr + i,
                                *(uint32_t *)(s->data_gen_ptr + i));
                    }
                }
            } else {
                disas(logfile, s->code_gen_ptr, prologue_size);
            }
            fprintf(logfile, "\n");
            qemu_log_unlock(logfile);
        }
    }
#endif

#ifndef CONFIG_TCG_INTERPRETER
    /*
     * Assert that goto_ptr is implemented completely, setting an epilogue.
     * For tci, we use NULL as the signal to return from the interpreter,
     * so skip this check.
     */
    tcg_debug_assert(tcg_code_gen_epilogue != NULL);
#endif

    tcg_region_prologue_set(s);
}

void tcg_func_start(TCGContext *s)
{
    tcg_pool_reset(s);
    s->nb_temps = s->nb_globals;

    /* No temps have been previously allocated for size or locality.  */
    memset(s->free_temps, 0, sizeof(s->free_temps));

    /* No constant temps have been previously allocated. */
    for (int i = 0; i < TCG_TYPE_COUNT; ++i) {
        if (s->const_table[i]) {
            g_hash_table_remove_all(s->const_table[i]);
        }
    }

    s->nb_ops = 0;
    s->nb_labels = 0;
    s->current_frame_offset = s->frame_start;

#ifdef CONFIG_DEBUG_TCG
    s->goto_tb_issue_mask = 0;
#endif

    QTAILQ_INIT(&s->ops);
    QTAILQ_INIT(&s->free_ops);
    QSIMPLEQ_INIT(&s->labels);
}

static TCGTemp *tcg_temp_alloc(TCGContext *s)
{
    int n = s->nb_temps++;

    if (n >= TCG_MAX_TEMPS) {
        tcg_raise_tb_overflow(s);
    }
    return memset(&s->temps[n], 0, sizeof(TCGTemp));
}

static TCGTemp *tcg_global_alloc(TCGContext *s)
{
    TCGTemp *ts;

    tcg_debug_assert(s->nb_globals == s->nb_temps);
    tcg_debug_assert(s->nb_globals < TCG_MAX_TEMPS);
    s->nb_globals++;
    ts = tcg_temp_alloc(s);
    ts->kind = TEMP_GLOBAL;

    return ts;
}

static TCGTemp *tcg_global_reg_new_internal(TCGContext *s, TCGType type,
                                            TCGReg reg, const char *name)
{
    TCGTemp *ts;

    tcg_debug_assert(TCG_TARGET_REG_BITS == 64 || type == TCG_TYPE_I32);

    ts = tcg_global_alloc(s);
    ts->base_type = type;
    ts->type = type;
    ts->kind = TEMP_FIXED;
    ts->reg = reg;
    ts->name = name;
    tcg_regset_set_reg(s->reserved_regs, reg);

    return ts;
}

void tcg_set_frame(TCGContext *s, TCGReg reg, intptr_t start, intptr_t size)
{
    s->frame_start = start;
    s->frame_end = start + size;
    s->frame_temp
        = tcg_global_reg_new_internal(s, TCG_TYPE_PTR, reg, "_frame");
}

TCGTemp *tcg_global_mem_new_internal(TCGType type, TCGv_ptr base,
                                     intptr_t offset, const char *name)
{
    TCGContext *s = tcg_ctx;
    TCGTemp *base_ts = tcgv_ptr_temp(base);
    TCGTemp *ts = tcg_global_alloc(s);
    int indirect_reg = 0;

    switch (base_ts->kind) {
    case TEMP_FIXED:
        break;
    case TEMP_GLOBAL:
        /* We do not support double-indirect registers.  */
        tcg_debug_assert(!base_ts->indirect_reg);
        base_ts->indirect_base = 1;
        s->nb_indirects += (TCG_TARGET_REG_BITS == 32 && type == TCG_TYPE_I64
                            ? 2 : 1);
        indirect_reg = 1;
        break;
    default:
        g_assert_not_reached();
    }

    if (TCG_TARGET_REG_BITS == 32 && type == TCG_TYPE_I64) {
        TCGTemp *ts2 = tcg_global_alloc(s);
        char buf[64];

        ts->base_type = TCG_TYPE_I64;
        ts->type = TCG_TYPE_I32;
        ts->indirect_reg = indirect_reg;
        ts->mem_allocated = 1;
        ts->mem_base = base_ts;
        ts->mem_offset = offset;
        pstrcpy(buf, sizeof(buf), name);
        pstrcat(buf, sizeof(buf), "_0");
        ts->name = strdup(buf);

        tcg_debug_assert(ts2 == ts + 1);
        ts2->base_type = TCG_TYPE_I64;
        ts2->type = TCG_TYPE_I32;
        ts2->indirect_reg = indirect_reg;
        ts2->mem_allocated = 1;
        ts2->mem_base = base_ts;
        ts2->mem_offset = offset + 4;
        ts2->temp_subindex = 1;
        pstrcpy(buf, sizeof(buf), name);
        pstrcat(buf, sizeof(buf), "_1");
        ts2->name = strdup(buf);
    } else {
        ts->base_type = type;
        ts->type = type;
        ts->indirect_reg = indirect_reg;
        ts->mem_allocated = 1;
        ts->mem_base = base_ts;
        ts->mem_offset = offset;
        ts->name = name;
    }
    return ts;
}

TCGTemp *tcg_temp_new_internal(TCGType type, TCGTempKind kind)
{
    TCGContext *s = tcg_ctx;
    TCGTemp *ts;
    int n;

    if (kind == TEMP_EBB) {
        int idx = find_first_bit(s->free_temps[type].l, TCG_MAX_TEMPS);

        if (idx < TCG_MAX_TEMPS) {
            /* There is already an available temp with the right type.  */
            clear_bit(idx, s->free_temps[type].l);

            ts = &s->temps[idx];
            ts->temp_allocated = 1;
            tcg_debug_assert(ts->base_type == type);
            tcg_debug_assert(ts->kind == kind);
            return ts;
        }
    } else {
        tcg_debug_assert(kind == TEMP_TB);
    }

    switch (type) {
    case TCG_TYPE_I32:
    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
    case TCG_TYPE_V256:
        n = 1;
        break;
    case TCG_TYPE_I64:
        n = 64 / TCG_TARGET_REG_BITS;
        break;
    case TCG_TYPE_I128:
        n = 128 / TCG_TARGET_REG_BITS;
        break;
    default:
        g_assert_not_reached();
    }

    ts = tcg_temp_alloc(s);
    ts->base_type = type;
    ts->temp_allocated = 1;
    ts->kind = kind;

    if (n == 1) {
        ts->type = type;
    } else {
        ts->type = TCG_TYPE_REG;

        for (int i = 1; i < n; ++i) {
            TCGTemp *ts2 = tcg_temp_alloc(s);

            tcg_debug_assert(ts2 == ts + i);
            ts2->base_type = type;
            ts2->type = TCG_TYPE_REG;
            ts2->temp_allocated = 1;
            ts2->temp_subindex = i;
            ts2->kind = kind;
        }
    }
    return ts;
}

TCGv_vec tcg_temp_new_vec(TCGType type)
{
    TCGTemp *t;

#ifdef CONFIG_DEBUG_TCG
    switch (type) {
    case TCG_TYPE_V64:
        assert(TCG_TARGET_HAS_v64);
        break;
    case TCG_TYPE_V128:
        assert(TCG_TARGET_HAS_v128);
        break;
    case TCG_TYPE_V256:
        assert(TCG_TARGET_HAS_v256);
        break;
    default:
        g_assert_not_reached();
    }
#endif

    t = tcg_temp_new_internal(type, TEMP_EBB);
    return temp_tcgv_vec(t);
}

/* Create a new temp of the same type as an existing temp.  */
TCGv_vec tcg_temp_new_vec_matching(TCGv_vec match)
{
    TCGTemp *t = tcgv_vec_temp(match);

    tcg_debug_assert(t->temp_allocated != 0);

    t = tcg_temp_new_internal(t->base_type, TEMP_EBB);
    return temp_tcgv_vec(t);
}

void tcg_temp_free_internal(TCGTemp *ts)
{
    TCGContext *s = tcg_ctx;

    switch (ts->kind) {
    case TEMP_CONST:
    case TEMP_TB:
        /* Silently ignore free. */
        break;
    case TEMP_EBB:
        tcg_debug_assert(ts->temp_allocated != 0);
        ts->temp_allocated = 0;
        set_bit(temp_idx(ts), s->free_temps[ts->base_type].l);
        break;
    default:
        /* It never made sense to free TEMP_FIXED or TEMP_GLOBAL. */
        g_assert_not_reached();
    }
}

TCGTemp *tcg_constant_internal(TCGType type, int64_t val)
{
    TCGContext *s = tcg_ctx;
    GHashTable *h = s->const_table[type];
    TCGTemp *ts;

    if (h == NULL) {
        h = g_hash_table_new(g_int64_hash, g_int64_equal);
        s->const_table[type] = h;
    }

    ts = g_hash_table_lookup(h, &val);
    if (ts == NULL) {
        int64_t *val_ptr;

        ts = tcg_temp_alloc(s);

        if (TCG_TARGET_REG_BITS == 32 && type == TCG_TYPE_I64) {
            TCGTemp *ts2 = tcg_temp_alloc(s);

            tcg_debug_assert(ts2 == ts + 1);

            ts->base_type = TCG_TYPE_I64;
            ts->type = TCG_TYPE_I32;
            ts->kind = TEMP_CONST;
            ts->temp_allocated = 1;

            ts2->base_type = TCG_TYPE_I64;
            ts2->type = TCG_TYPE_I32;
            ts2->kind = TEMP_CONST;
            ts2->temp_allocated = 1;
            ts2->temp_subindex = 1;

            /*
             * Retain the full value of the 64-bit constant in the low
             * part, so that the hash table works.  Actual uses will
             * truncate the value to the low part.
             */
            ts[HOST_BIG_ENDIAN].val = val;
            ts[!HOST_BIG_ENDIAN].val = val >> 32;
            val_ptr = &ts[HOST_BIG_ENDIAN].val;
        } else {
            ts->base_type = type;
            ts->type = type;
            ts->kind = TEMP_CONST;
            ts->temp_allocated = 1;
            ts->val = val;
            val_ptr = &ts->val;
        }
        g_hash_table_insert(h, val_ptr, ts);
    }

    return ts;
}

TCGv_vec tcg_constant_vec(TCGType type, unsigned vece, int64_t val)
{
    val = dup_const(vece, val);
    return temp_tcgv_vec(tcg_constant_internal(type, val));
}

TCGv_vec tcg_constant_vec_matching(TCGv_vec match, unsigned vece, int64_t val)
{
    TCGTemp *t = tcgv_vec_temp(match);

    tcg_debug_assert(t->temp_allocated != 0);
    return tcg_constant_vec(t->base_type, vece, val);
}

/* Return true if OP may appear in the opcode stream.
   Test the runtime variable that controls each opcode.  */
bool tcg_op_supported(TCGOpcode op)
{
    const bool have_vec
        = TCG_TARGET_HAS_v64 | TCG_TARGET_HAS_v128 | TCG_TARGET_HAS_v256;

    switch (op) {
    case INDEX_op_discard:
    case INDEX_op_set_label:
    case INDEX_op_call:
    case INDEX_op_br:
    case INDEX_op_mb:
    case INDEX_op_insn_start:
    case INDEX_op_exit_tb:
    case INDEX_op_goto_tb:
    case INDEX_op_goto_ptr:
    case INDEX_op_qemu_ld_i32:
    case INDEX_op_qemu_st_i32:
    case INDEX_op_qemu_ld_i64:
    case INDEX_op_qemu_st_i64:
        return true;

    case INDEX_op_qemu_st8_i32:
        return TCG_TARGET_HAS_qemu_st8_i32;

    case INDEX_op_qemu_ld_i128:
    case INDEX_op_qemu_st_i128:
        return TCG_TARGET_HAS_qemu_ldst_i128;

    case INDEX_op_mov_i32:
    case INDEX_op_setcond_i32:
    case INDEX_op_brcond_i32:
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld_i32:
    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
    case INDEX_op_add_i32:
    case INDEX_op_sub_i32:
    case INDEX_op_mul_i32:
    case INDEX_op_and_i32:
    case INDEX_op_or_i32:
    case INDEX_op_xor_i32:
    case INDEX_op_shl_i32:
    case INDEX_op_shr_i32:
    case INDEX_op_sar_i32:
        return true;

    case INDEX_op_movcond_i32:
        return TCG_TARGET_HAS_movcond_i32;
    case INDEX_op_div_i32:
    case INDEX_op_divu_i32:
        return TCG_TARGET_HAS_div_i32;
    case INDEX_op_rem_i32:
    case INDEX_op_remu_i32:
        return TCG_TARGET_HAS_rem_i32;
    case INDEX_op_div2_i32:
    case INDEX_op_divu2_i32:
        return TCG_TARGET_HAS_div2_i32;
    case INDEX_op_rotl_i32:
    case INDEX_op_rotr_i32:
        return TCG_TARGET_HAS_rot_i32;
    case INDEX_op_deposit_i32:
        return TCG_TARGET_HAS_deposit_i32;
    case INDEX_op_extract_i32:
        return TCG_TARGET_HAS_extract_i32;
    case INDEX_op_sextract_i32:
        return TCG_TARGET_HAS_sextract_i32;
    case INDEX_op_extract2_i32:
        return TCG_TARGET_HAS_extract2_i32;
    case INDEX_op_add2_i32:
        return TCG_TARGET_HAS_add2_i32;
    case INDEX_op_sub2_i32:
        return TCG_TARGET_HAS_sub2_i32;
    case INDEX_op_mulu2_i32:
        return TCG_TARGET_HAS_mulu2_i32;
    case INDEX_op_muls2_i32:
        return TCG_TARGET_HAS_muls2_i32;
    case INDEX_op_muluh_i32:
        return TCG_TARGET_HAS_muluh_i32;
    case INDEX_op_mulsh_i32:
        return TCG_TARGET_HAS_mulsh_i32;
    case INDEX_op_ext8s_i32:
        return TCG_TARGET_HAS_ext8s_i32;
    case INDEX_op_ext16s_i32:
        return TCG_TARGET_HAS_ext16s_i32;
    case INDEX_op_ext8u_i32:
        return TCG_TARGET_HAS_ext8u_i32;
    case INDEX_op_ext16u_i32:
        return TCG_TARGET_HAS_ext16u_i32;
    case INDEX_op_bswap16_i32:
        return TCG_TARGET_HAS_bswap16_i32;
    case INDEX_op_bswap32_i32:
        return TCG_TARGET_HAS_bswap32_i32;
    case INDEX_op_not_i32:
        return TCG_TARGET_HAS_not_i32;
    case INDEX_op_neg_i32:
        return TCG_TARGET_HAS_neg_i32;
    case INDEX_op_andc_i32:
        return TCG_TARGET_HAS_andc_i32;
    case INDEX_op_orc_i32:
        return TCG_TARGET_HAS_orc_i32;
    case INDEX_op_eqv_i32:
        return TCG_TARGET_HAS_eqv_i32;
    case INDEX_op_nand_i32:
        return TCG_TARGET_HAS_nand_i32;
    case INDEX_op_nor_i32:
        return TCG_TARGET_HAS_nor_i32;
    case INDEX_op_clz_i32:
        return TCG_TARGET_HAS_clz_i32;
    case INDEX_op_ctz_i32:
        return TCG_TARGET_HAS_ctz_i32;
    case INDEX_op_ctpop_i32:
        return TCG_TARGET_HAS_ctpop_i32;

    case INDEX_op_brcond2_i32:
    case INDEX_op_setcond2_i32:
        return TCG_TARGET_REG_BITS == 32;

    case INDEX_op_mov_i64:
    case INDEX_op_setcond_i64:
    case INDEX_op_brcond_i64:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld_i64:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
    case INDEX_op_add_i64:
    case INDEX_op_sub_i64:
    case INDEX_op_mul_i64:
    case INDEX_op_and_i64:
    case INDEX_op_or_i64:
    case INDEX_op_xor_i64:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extu_i32_i64:
        return TCG_TARGET_REG_BITS == 64;

    case INDEX_op_movcond_i64:
        return TCG_TARGET_HAS_movcond_i64;
    case INDEX_op_div_i64:
    case INDEX_op_divu_i64:
        return TCG_TARGET_HAS_div_i64;
    case INDEX_op_rem_i64:
    case INDEX_op_remu_i64:
        return TCG_TARGET_HAS_rem_i64;
    case INDEX_op_div2_i64:
    case INDEX_op_divu2_i64:
        return TCG_TARGET_HAS_div2_i64;
    case INDEX_op_rotl_i64:
    case INDEX_op_rotr_i64:
        return TCG_TARGET_HAS_rot_i64;
    case INDEX_op_deposit_i64:
        return TCG_TARGET_HAS_deposit_i64;
    case INDEX_op_extract_i64:
        return TCG_TARGET_HAS_extract_i64;
    case INDEX_op_sextract_i64:
        return TCG_TARGET_HAS_sextract_i64;
    case INDEX_op_extract2_i64:
        return TCG_TARGET_HAS_extract2_i64;
    case INDEX_op_extrl_i64_i32:
        return TCG_TARGET_HAS_extrl_i64_i32;
    case INDEX_op_extrh_i64_i32:
        return TCG_TARGET_HAS_extrh_i64_i32;
    case INDEX_op_ext8s_i64:
        return TCG_TARGET_HAS_ext8s_i64;
    case INDEX_op_ext16s_i64:
        return TCG_TARGET_HAS_ext16s_i64;
    case INDEX_op_ext32s_i64:
        return TCG_TARGET_HAS_ext32s_i64;
    case INDEX_op_ext8u_i64:
        return TCG_TARGET_HAS_ext8u_i64;
    case INDEX_op_ext16u_i64:
        return TCG_TARGET_HAS_ext16u_i64;
    case INDEX_op_ext32u_i64:
        return TCG_TARGET_HAS_ext32u_i64;
    case INDEX_op_bswap16_i64:
        return TCG_TARGET_HAS_bswap16_i64;
    case INDEX_op_bswap32_i64:
        return TCG_TARGET_HAS_bswap32_i64;
    case INDEX_op_bswap64_i64:
        return TCG_TARGET_HAS_bswap64_i64;
    case INDEX_op_not_i64:
        return TCG_TARGET_HAS_not_i64;
    case INDEX_op_neg_i64:
        return TCG_TARGET_HAS_neg_i64;
    case INDEX_op_andc_i64:
        return TCG_TARGET_HAS_andc_i64;
    case INDEX_op_orc_i64:
        return TCG_TARGET_HAS_orc_i64;
    case INDEX_op_eqv_i64:
        return TCG_TARGET_HAS_eqv_i64;
    case INDEX_op_nand_i64:
        return TCG_TARGET_HAS_nand_i64;
    case INDEX_op_nor_i64:
        return TCG_TARGET_HAS_nor_i64;
    case INDEX_op_clz_i64:
        return TCG_TARGET_HAS_clz_i64;
    case INDEX_op_ctz_i64:
        return TCG_TARGET_HAS_ctz_i64;
    case INDEX_op_ctpop_i64:
        return TCG_TARGET_HAS_ctpop_i64;
    case INDEX_op_add2_i64:
        return TCG_TARGET_HAS_add2_i64;
    case INDEX_op_sub2_i64:
        return TCG_TARGET_HAS_sub2_i64;
    case INDEX_op_mulu2_i64:
        return TCG_TARGET_HAS_mulu2_i64;
    case INDEX_op_muls2_i64:
        return TCG_TARGET_HAS_muls2_i64;
    case INDEX_op_muluh_i64:
        return TCG_TARGET_HAS_muluh_i64;
    case INDEX_op_mulsh_i64:
        return TCG_TARGET_HAS_mulsh_i64;

    case INDEX_op_mov_vec:
    case INDEX_op_dup_vec:
    case INDEX_op_dupm_vec:
    case INDEX_op_ld_vec:
    case INDEX_op_st_vec:
    case INDEX_op_add_vec:
    case INDEX_op_sub_vec:
    case INDEX_op_and_vec:
    case INDEX_op_or_vec:
    case INDEX_op_xor_vec:
    case INDEX_op_cmp_vec:
        return have_vec;
    case INDEX_op_dup2_vec:
        return have_vec && TCG_TARGET_REG_BITS == 32;
    case INDEX_op_not_vec:
        return have_vec && TCG_TARGET_HAS_not_vec;
    case INDEX_op_neg_vec:
        return have_vec && TCG_TARGET_HAS_neg_vec;
    case INDEX_op_abs_vec:
        return have_vec && TCG_TARGET_HAS_abs_vec;
    case INDEX_op_andc_vec:
        return have_vec && TCG_TARGET_HAS_andc_vec;
    case INDEX_op_orc_vec:
        return have_vec && TCG_TARGET_HAS_orc_vec;
    case INDEX_op_nand_vec:
        return have_vec && TCG_TARGET_HAS_nand_vec;
    case INDEX_op_nor_vec:
        return have_vec && TCG_TARGET_HAS_nor_vec;
    case INDEX_op_eqv_vec:
        return have_vec && TCG_TARGET_HAS_eqv_vec;
    case INDEX_op_mul_vec:
        return have_vec && TCG_TARGET_HAS_mul_vec;
    case INDEX_op_shli_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_sari_vec:
        return have_vec && TCG_TARGET_HAS_shi_vec;
    case INDEX_op_shls_vec:
    case INDEX_op_shrs_vec:
    case INDEX_op_sars_vec:
        return have_vec && TCG_TARGET_HAS_shs_vec;
    case INDEX_op_shlv_vec:
    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
        return have_vec && TCG_TARGET_HAS_shv_vec;
    case INDEX_op_rotli_vec:
        return have_vec && TCG_TARGET_HAS_roti_vec;
    case INDEX_op_rotls_vec:
        return have_vec && TCG_TARGET_HAS_rots_vec;
    case INDEX_op_rotlv_vec:
    case INDEX_op_rotrv_vec:
        return have_vec && TCG_TARGET_HAS_rotv_vec;
    case INDEX_op_ssadd_vec:
    case INDEX_op_usadd_vec:
    case INDEX_op_sssub_vec:
    case INDEX_op_ussub_vec:
        return have_vec && TCG_TARGET_HAS_sat_vec;
    case INDEX_op_smin_vec:
    case INDEX_op_umin_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_umax_vec:
        return have_vec && TCG_TARGET_HAS_minmax_vec;
    case INDEX_op_bitsel_vec:
        return have_vec && TCG_TARGET_HAS_bitsel_vec;
    case INDEX_op_cmpsel_vec:
        return have_vec && TCG_TARGET_HAS_cmpsel_vec;

    default:
        tcg_debug_assert(op > INDEX_op_last_generic && op < NB_OPS);
        return true;
    }
}

static TCGOp *tcg_op_alloc(TCGOpcode opc, unsigned nargs);

void tcg_gen_callN(void *func, TCGTemp *ret, int nargs, TCGTemp **args)
{
    const TCGHelperInfo *info;
    TCGv_i64 extend_free[MAX_CALL_IARGS];
    int n_extend = 0;
    TCGOp *op;
    int i, n, pi = 0, total_args;

    info = g_hash_table_lookup(helper_table, (gpointer)func);
    total_args = info->nr_out + info->nr_in + 2;
    op = tcg_op_alloc(INDEX_op_call, total_args);

#ifdef CONFIG_PLUGIN
    /* Flag helpers that may affect guest state */
    if (tcg_ctx->plugin_insn &&
        !(info->flags & TCG_CALL_PLUGIN) &&
        !(info->flags & TCG_CALL_NO_SIDE_EFFECTS)) {
        tcg_ctx->plugin_insn->calls_helpers = true;
    }
#endif

    TCGOP_CALLO(op) = n = info->nr_out;
    switch (n) {
    case 0:
        tcg_debug_assert(ret == NULL);
        break;
    case 1:
        tcg_debug_assert(ret != NULL);
        op->args[pi++] = temp_arg(ret);
        break;
    case 2:
    case 4:
        tcg_debug_assert(ret != NULL);
        tcg_debug_assert(ret->base_type == ret->type + ctz32(n));
        tcg_debug_assert(ret->temp_subindex == 0);
        for (i = 0; i < n; ++i) {
            op->args[pi++] = temp_arg(ret + i);
        }
        break;
    default:
        g_assert_not_reached();
    }

    TCGOP_CALLI(op) = n = info->nr_in;
    for (i = 0; i < n; i++) {
        const TCGCallArgumentLoc *loc = &info->in[i];
        TCGTemp *ts = args[loc->arg_idx] + loc->tmp_subindex;

        switch (loc->kind) {
        case TCG_CALL_ARG_NORMAL:
        case TCG_CALL_ARG_BY_REF:
        case TCG_CALL_ARG_BY_REF_N:
            op->args[pi++] = temp_arg(ts);
            break;

        case TCG_CALL_ARG_EXTEND_U:
        case TCG_CALL_ARG_EXTEND_S:
            {
                TCGv_i64 temp = tcg_temp_ebb_new_i64();
                TCGv_i32 orig = temp_tcgv_i32(ts);

                if (loc->kind == TCG_CALL_ARG_EXTEND_S) {
                    tcg_gen_ext_i32_i64(temp, orig);
                } else {
                    tcg_gen_extu_i32_i64(temp, orig);
                }
                op->args[pi++] = tcgv_i64_arg(temp);
                extend_free[n_extend++] = temp;
            }
            break;

        default:
            g_assert_not_reached();
        }
    }
    op->args[pi++] = (uintptr_t)func;
    op->args[pi++] = (uintptr_t)info;
    tcg_debug_assert(pi == total_args);

    QTAILQ_INSERT_TAIL(&tcg_ctx->ops, op, link);

    tcg_debug_assert(n_extend < ARRAY_SIZE(extend_free));
    for (i = 0; i < n_extend; ++i) {
        tcg_temp_free_i64(extend_free[i]);
    }
}

static void tcg_reg_alloc_start(TCGContext *s)
{
    int i, n;

    for (i = 0, n = s->nb_temps; i < n; i++) {
        TCGTemp *ts = &s->temps[i];
        TCGTempVal val = TEMP_VAL_MEM;

        switch (ts->kind) {
        case TEMP_CONST:
            val = TEMP_VAL_CONST;
            break;
        case TEMP_FIXED:
            val = TEMP_VAL_REG;
            break;
        case TEMP_GLOBAL:
            break;
        case TEMP_EBB:
            val = TEMP_VAL_DEAD;
            /* fall through */
        case TEMP_TB:
            ts->mem_allocated = 0;
            break;
        default:
            g_assert_not_reached();
        }
        ts->val_type = val;
    }

    memset(s->reg_to_temp, 0, sizeof(s->reg_to_temp));
}

static char *tcg_get_arg_str_ptr(TCGContext *s, char *buf, int buf_size,
                                 TCGTemp *ts)
{
    int idx = temp_idx(ts);

    switch (ts->kind) {
    case TEMP_FIXED:
    case TEMP_GLOBAL:
        pstrcpy(buf, buf_size, ts->name);
        break;
    case TEMP_TB:
        snprintf(buf, buf_size, "loc%d", idx - s->nb_globals);
        break;
    case TEMP_EBB:
        snprintf(buf, buf_size, "tmp%d", idx - s->nb_globals);
        break;
    case TEMP_CONST:
        switch (ts->type) {
        case TCG_TYPE_I32:
            snprintf(buf, buf_size, "$0x%x", (int32_t)ts->val);
            break;
#if TCG_TARGET_REG_BITS > 32
        case TCG_TYPE_I64:
            snprintf(buf, buf_size, "$0x%" PRIx64, ts->val);
            break;
#endif
        case TCG_TYPE_V64:
        case TCG_TYPE_V128:
        case TCG_TYPE_V256:
            snprintf(buf, buf_size, "v%d$0x%" PRIx64,
                     64 << (ts->type - TCG_TYPE_V64), ts->val);
            break;
        default:
            g_assert_not_reached();
        }
        break;
    }
    return buf;
}

static char *tcg_get_arg_str(TCGContext *s, char *buf,
                             int buf_size, TCGArg arg)
{
    return tcg_get_arg_str_ptr(s, buf, buf_size, arg_temp(arg));
}

static const char * const cond_name[] =
{
    [TCG_COND_NEVER] = "never",
    [TCG_COND_ALWAYS] = "always",
    [TCG_COND_EQ] = "eq",
    [TCG_COND_NE] = "ne",
    [TCG_COND_LT] = "lt",
    [TCG_COND_GE] = "ge",
    [TCG_COND_LE] = "le",
    [TCG_COND_GT] = "gt",
    [TCG_COND_LTU] = "ltu",
    [TCG_COND_GEU] = "geu",
    [TCG_COND_LEU] = "leu",
    [TCG_COND_GTU] = "gtu"
};

static const char * const ldst_name[(MO_BSWAP | MO_SSIZE) + 1] =
{
    [MO_UB]   = "ub",
    [MO_SB]   = "sb",
    [MO_LEUW] = "leuw",
    [MO_LESW] = "lesw",
    [MO_LEUL] = "leul",
    [MO_LESL] = "lesl",
    [MO_LEUQ] = "leq",
    [MO_BEUW] = "beuw",
    [MO_BESW] = "besw",
    [MO_BEUL] = "beul",
    [MO_BESL] = "besl",
    [MO_BEUQ] = "beq",
    [MO_128 + MO_BE] = "beo",
    [MO_128 + MO_LE] = "leo",
};

static const char * const alignment_name[(MO_AMASK >> MO_ASHIFT) + 1] = {
    [MO_UNALN >> MO_ASHIFT]    = "un+",
    [MO_ALIGN >> MO_ASHIFT]    = "al+",
    [MO_ALIGN_2 >> MO_ASHIFT]  = "al2+",
    [MO_ALIGN_4 >> MO_ASHIFT]  = "al4+",
    [MO_ALIGN_8 >> MO_ASHIFT]  = "al8+",
    [MO_ALIGN_16 >> MO_ASHIFT] = "al16+",
    [MO_ALIGN_32 >> MO_ASHIFT] = "al32+",
    [MO_ALIGN_64 >> MO_ASHIFT] = "al64+",
};

static const char * const atom_name[(MO_ATOM_MASK >> MO_ATOM_SHIFT) + 1] = {
    [MO_ATOM_IFALIGN >> MO_ATOM_SHIFT] = "",
    [MO_ATOM_IFALIGN_PAIR >> MO_ATOM_SHIFT] = "pair+",
    [MO_ATOM_WITHIN16 >> MO_ATOM_SHIFT] = "w16+",
    [MO_ATOM_WITHIN16_PAIR >> MO_ATOM_SHIFT] = "w16p+",
    [MO_ATOM_SUBALIGN >> MO_ATOM_SHIFT] = "sub+",
    [MO_ATOM_NONE >> MO_ATOM_SHIFT] = "noat+",
};

static const char bswap_flag_name[][6] = {
    [TCG_BSWAP_IZ] = "iz",
    [TCG_BSWAP_OZ] = "oz",
    [TCG_BSWAP_OS] = "os",
    [TCG_BSWAP_IZ | TCG_BSWAP_OZ] = "iz,oz",
    [TCG_BSWAP_IZ | TCG_BSWAP_OS] = "iz,os",
};

static inline bool tcg_regset_single(TCGRegSet d)
{
    return (d & (d - 1)) == 0;
}

static inline TCGReg tcg_regset_first(TCGRegSet d)
{
    if (TCG_TARGET_NB_REGS <= 32) {
        return ctz32(d);
    } else {
        return ctz64(d);
    }
}

/* Return only the number of characters output -- no error return. */
#define ne_fprintf(...) \
    ({ int ret_ = fprintf(__VA_ARGS__); ret_ >= 0 ? ret_ : 0; })

static void tcg_dump_ops(TCGContext *s, FILE *f, bool have_prefs)
{
    char buf[128];
    TCGOp *op;

    QTAILQ_FOREACH(op, &s->ops, link) {
        int i, k, nb_oargs, nb_iargs, nb_cargs;
        const TCGOpDef *def;
        TCGOpcode c;
        int col = 0;

        c = op->opc;
        def = &tcg_op_defs[c];

        if (c == INDEX_op_insn_start) {
            nb_oargs = 0;
            col += ne_fprintf(f, "\n ----");

            for (i = 0; i < TARGET_INSN_START_WORDS; ++i) {
                target_ulong a;
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
                a = deposit64(op->args[i * 2], 32, 32, op->args[i * 2 + 1]);
#else
                a = op->args[i];
#endif
                col += ne_fprintf(f, " " TARGET_FMT_lx, a);
            }
        } else if (c == INDEX_op_call) {
            const TCGHelperInfo *info = tcg_call_info(op);
            void *func = tcg_call_func(op);

            /* variable number of arguments */
            nb_oargs = TCGOP_CALLO(op);
            nb_iargs = TCGOP_CALLI(op);
            nb_cargs = def->nb_cargs;

            col += ne_fprintf(f, " %s ", def->name);

            /*
             * Print the function name from TCGHelperInfo, if available.
             * Note that plugins have a template function for the info,
             * but the actual function pointer comes from the plugin.
             */
            if (func == info->func) {
                col += ne_fprintf(f, "%s", info->name);
            } else {
                col += ne_fprintf(f, "plugin(%p)", func);
            }

            col += ne_fprintf(f, ",$0x%x,$%d", info->flags, nb_oargs);
            for (i = 0; i < nb_oargs; i++) {
                col += ne_fprintf(f, ",%s", tcg_get_arg_str(s, buf, sizeof(buf),
                                                            op->args[i]));
            }
            for (i = 0; i < nb_iargs; i++) {
                TCGArg arg = op->args[nb_oargs + i];
                const char *t = tcg_get_arg_str(s, buf, sizeof(buf), arg);
                col += ne_fprintf(f, ",%s", t);
            }
        } else {
            col += ne_fprintf(f, " %s ", def->name);

            nb_oargs = def->nb_oargs;
            nb_iargs = def->nb_iargs;
            nb_cargs = def->nb_cargs;

            if (def->flags & TCG_OPF_VECTOR) {
                col += ne_fprintf(f, "v%d,e%d,", 64 << TCGOP_VECL(op),
                                  8 << TCGOP_VECE(op));
            }

            k = 0;
            for (i = 0; i < nb_oargs; i++) {
                const char *sep =  k ? "," : "";
                col += ne_fprintf(f, "%s%s", sep,
                                  tcg_get_arg_str(s, buf, sizeof(buf),
                                                  op->args[k++]));
            }
            for (i = 0; i < nb_iargs; i++) {
                const char *sep =  k ? "," : "";
                col += ne_fprintf(f, "%s%s", sep,
                                  tcg_get_arg_str(s, buf, sizeof(buf),
                                                  op->args[k++]));
            }
            switch (c) {
            case INDEX_op_brcond_i32:
            case INDEX_op_setcond_i32:
            case INDEX_op_movcond_i32:
            case INDEX_op_brcond2_i32:
            case INDEX_op_setcond2_i32:
            case INDEX_op_brcond_i64:
            case INDEX_op_setcond_i64:
            case INDEX_op_movcond_i64:
            case INDEX_op_cmp_vec:
            case INDEX_op_cmpsel_vec:
                if (op->args[k] < ARRAY_SIZE(cond_name)
                    && cond_name[op->args[k]]) {
                    col += ne_fprintf(f, ",%s", cond_name[op->args[k++]]);
                } else {
                    col += ne_fprintf(f, ",$0x%" TCG_PRIlx, op->args[k++]);
                }
                i = 1;
                break;
            case INDEX_op_qemu_ld_i32:
            case INDEX_op_qemu_st_i32:
            case INDEX_op_qemu_st8_i32:
            case INDEX_op_qemu_ld_i64:
            case INDEX_op_qemu_st_i64:
            case INDEX_op_qemu_ld_i128:
            case INDEX_op_qemu_st_i128:
                {
                    const char *s_al, *s_op, *s_at;
                    MemOpIdx oi = op->args[k++];
                    MemOp op = get_memop(oi);
                    unsigned ix = get_mmuidx(oi);

                    s_al = alignment_name[(op & MO_AMASK) >> MO_ASHIFT];
                    s_op = ldst_name[op & (MO_BSWAP | MO_SSIZE)];
                    s_at = atom_name[(op & MO_ATOM_MASK) >> MO_ATOM_SHIFT];
                    op &= ~(MO_AMASK | MO_BSWAP | MO_SSIZE | MO_ATOM_MASK);

                    /* If all fields are accounted for, print symbolically. */
                    if (!op && s_al && s_op && s_at) {
                        col += ne_fprintf(f, ",%s%s%s,%u",
                                          s_at, s_al, s_op, ix);
                    } else {
                        op = get_memop(oi);
                        col += ne_fprintf(f, ",$0x%x,%u", op, ix);
                    }
                    i = 1;
                }
                break;
            case INDEX_op_bswap16_i32:
            case INDEX_op_bswap16_i64:
            case INDEX_op_bswap32_i32:
            case INDEX_op_bswap32_i64:
            case INDEX_op_bswap64_i64:
                {
                    TCGArg flags = op->args[k];
                    const char *name = NULL;

                    if (flags < ARRAY_SIZE(bswap_flag_name)) {
                        name = bswap_flag_name[flags];
                    }
                    if (name) {
                        col += ne_fprintf(f, ",%s", name);
                    } else {
                        col += ne_fprintf(f, ",$0x%" TCG_PRIlx, flags);
                    }
                    i = k = 1;
                }
                break;
            default:
                i = 0;
                break;
            }
            switch (c) {
            case INDEX_op_set_label:
            case INDEX_op_br:
            case INDEX_op_brcond_i32:
            case INDEX_op_brcond_i64:
            case INDEX_op_brcond2_i32:
                col += ne_fprintf(f, "%s$L%d", k ? "," : "",
                                  arg_label(op->args[k])->id);
                i++, k++;
                break;
            case INDEX_op_mb:
                {
                    TCGBar membar = op->args[k];
                    const char *b_op, *m_op;

                    switch (membar & TCG_BAR_SC) {
                    case 0:
                        b_op = "none";
                        break;
                    case TCG_BAR_LDAQ:
                        b_op = "acq";
                        break;
                    case TCG_BAR_STRL:
                        b_op = "rel";
                        break;
                    case TCG_BAR_SC:
                        b_op = "seq";
                        break;
                    default:
                        g_assert_not_reached();
                    }

                    switch (membar & TCG_MO_ALL) {
                    case 0:
                        m_op = "none";
                        break;
                    case TCG_MO_LD_LD:
                        m_op = "rr";
                        break;
                    case TCG_MO_LD_ST:
                        m_op = "rw";
                        break;
                    case TCG_MO_ST_LD:
                        m_op = "wr";
                        break;
                    case TCG_MO_ST_ST:
                        m_op = "ww";
                        break;
                    case TCG_MO_LD_LD | TCG_MO_LD_ST:
                        m_op = "rr+rw";
                        break;
                    case TCG_MO_LD_LD | TCG_MO_ST_LD:
                        m_op = "rr+wr";
                        break;
                    case TCG_MO_LD_LD | TCG_MO_ST_ST:
                        m_op = "rr+ww";
                        break;
                    case TCG_MO_LD_ST | TCG_MO_ST_LD:
                        m_op = "rw+wr";
                        break;
                    case TCG_MO_LD_ST | TCG_MO_ST_ST:
                        m_op = "rw+ww";
                        break;
                    case TCG_MO_ST_LD | TCG_MO_ST_ST:
                        m_op = "wr+ww";
                        break;
                    case TCG_MO_LD_LD | TCG_MO_LD_ST | TCG_MO_ST_LD:
                        m_op = "rr+rw+wr";
                        break;
                    case TCG_MO_LD_LD | TCG_MO_LD_ST | TCG_MO_ST_ST:
                        m_op = "rr+rw+ww";
                        break;
                    case TCG_MO_LD_LD | TCG_MO_ST_LD | TCG_MO_ST_ST:
                        m_op = "rr+wr+ww";
                        break;
                    case TCG_MO_LD_ST | TCG_MO_ST_LD | TCG_MO_ST_ST:
                        m_op = "rw+wr+ww";
                        break;
                    case TCG_MO_ALL:
                        m_op = "all";
                        break;
                    default:
                        g_assert_not_reached();
                    }

                    col += ne_fprintf(f, "%s%s:%s", (k ? "," : ""), b_op, m_op);
                    i++, k++;
                }
                break;
            default:
                break;
            }
            for (; i < nb_cargs; i++, k++) {
                col += ne_fprintf(f, "%s$0x%" TCG_PRIlx, k ? "," : "",
                                  op->args[k]);
            }
        }

        if (have_prefs || op->life) {
            for (; col < 40; ++col) {
                putc(' ', f);
            }
        }

        if (op->life) {
            unsigned life = op->life;

            if (life & (SYNC_ARG * 3)) {
                ne_fprintf(f, "  sync:");
                for (i = 0; i < 2; ++i) {
                    if (life & (SYNC_ARG << i)) {
                        ne_fprintf(f, " %d", i);
                    }
                }
            }
            life /= DEAD_ARG;
            if (life) {
                ne_fprintf(f, "  dead:");
                for (i = 0; life; ++i, life >>= 1) {
                    if (life & 1) {
                        ne_fprintf(f, " %d", i);
                    }
                }
            }
        }

        if (have_prefs) {
            for (i = 0; i < nb_oargs; ++i) {
                TCGRegSet set = output_pref(op, i);

                if (i == 0) {
                    ne_fprintf(f, "  pref=");
                } else {
                    ne_fprintf(f, ",");
                }
                if (set == 0) {
                    ne_fprintf(f, "none");
                } else if (set == MAKE_64BIT_MASK(0, TCG_TARGET_NB_REGS)) {
                    ne_fprintf(f, "all");
#ifdef CONFIG_DEBUG_TCG
                } else if (tcg_regset_single(set)) {
                    TCGReg reg = tcg_regset_first(set);
                    ne_fprintf(f, "%s", tcg_target_reg_names[reg]);
#endif
                } else if (TCG_TARGET_NB_REGS <= 32) {
                    ne_fprintf(f, "0x%x", (uint32_t)set);
                } else {
                    ne_fprintf(f, "0x%" PRIx64, (uint64_t)set);
                }
            }
        }

        putc('\n', f);
    }
}

/* we give more priority to constraints with less registers */
static int get_constraint_priority(const TCGOpDef *def, int k)
{
    const TCGArgConstraint *arg_ct = &def->args_ct[k];
    int n = ctpop64(arg_ct->regs);

    /*
     * Sort constraints of a single register first, which includes output
     * aliases (which must exactly match the input already allocated).
     */
    if (n == 1 || arg_ct->oalias) {
        return INT_MAX;
    }

    /*
     * Sort register pairs next, first then second immediately after.
     * Arbitrarily sort multiple pairs by the index of the first reg;
     * there shouldn't be many pairs.
     */
    switch (arg_ct->pair) {
    case 1:
    case 3:
        return (k + 1) * 2;
    case 2:
        return (arg_ct->pair_index + 1) * 2 - 1;
    }

    /* Finally, sort by decreasing register count. */
    assert(n > 1);
    return -n;
}

/* sort from highest priority to lowest */
static void sort_constraints(TCGOpDef *def, int start, int n)
{
    int i, j;
    TCGArgConstraint *a = def->args_ct;

    for (i = 0; i < n; i++) {
        a[start + i].sort_index = start + i;
    }
    if (n <= 1) {
        return;
    }
    for (i = 0; i < n - 1; i++) {
        for (j = i + 1; j < n; j++) {
            int p1 = get_constraint_priority(def, a[start + i].sort_index);
            int p2 = get_constraint_priority(def, a[start + j].sort_index);
            if (p1 < p2) {
                int tmp = a[start + i].sort_index;
                a[start + i].sort_index = a[start + j].sort_index;
                a[start + j].sort_index = tmp;
            }
        }
    }
}

static void process_op_defs(TCGContext *s)
{
    TCGOpcode op;

    for (op = 0; op < NB_OPS; op++) {
        TCGOpDef *def = &tcg_op_defs[op];
        const TCGTargetOpDef *tdefs;
        bool saw_alias_pair = false;
        int i, o, i2, o2, nb_args;

        if (def->flags & TCG_OPF_NOT_PRESENT) {
            continue;
        }

        nb_args = def->nb_iargs + def->nb_oargs;
        if (nb_args == 0) {
            continue;
        }

        /*
         * Macro magic should make it impossible, but double-check that
         * the array index is in range.  Since the signness of an enum
         * is implementation defined, force the result to unsigned.
         */
        unsigned con_set = tcg_target_op_def(op);
        tcg_debug_assert(con_set < ARRAY_SIZE(constraint_sets));
        tdefs = &constraint_sets[con_set];

        for (i = 0; i < nb_args; i++) {
            const char *ct_str = tdefs->args_ct_str[i];
            bool input_p = i >= def->nb_oargs;

            /* Incomplete TCGTargetOpDef entry. */
            tcg_debug_assert(ct_str != NULL);

            switch (*ct_str) {
            case '0' ... '9':
                o = *ct_str - '0';
                tcg_debug_assert(input_p);
                tcg_debug_assert(o < def->nb_oargs);
                tcg_debug_assert(def->args_ct[o].regs != 0);
                tcg_debug_assert(!def->args_ct[o].oalias);
                def->args_ct[i] = def->args_ct[o];
                /* The output sets oalias.  */
                def->args_ct[o].oalias = 1;
                def->args_ct[o].alias_index = i;
                /* The input sets ialias. */
                def->args_ct[i].ialias = 1;
                def->args_ct[i].alias_index = o;
                if (def->args_ct[i].pair) {
                    saw_alias_pair = true;
                }
                tcg_debug_assert(ct_str[1] == '\0');
                continue;

            case '&':
                tcg_debug_assert(!input_p);
                def->args_ct[i].newreg = true;
                ct_str++;
                break;

            case 'p': /* plus */
                /* Allocate to the register after the previous. */
                tcg_debug_assert(i > (input_p ? def->nb_oargs : 0));
                o = i - 1;
                tcg_debug_assert(!def->args_ct[o].pair);
                tcg_debug_assert(!def->args_ct[o].ct);
                def->args_ct[i] = (TCGArgConstraint){
                    .pair = 2,
                    .pair_index = o,
                    .regs = def->args_ct[o].regs << 1,
                };
                def->args_ct[o].pair = 1;
                def->args_ct[o].pair_index = i;
                tcg_debug_assert(ct_str[1] == '\0');
                continue;

            case 'm': /* minus */
                /* Allocate to the register before the previous. */
                tcg_debug_assert(i > (input_p ? def->nb_oargs : 0));
                o = i - 1;
                tcg_debug_assert(!def->args_ct[o].pair);
                tcg_debug_assert(!def->args_ct[o].ct);
                def->args_ct[i] = (TCGArgConstraint){
                    .pair = 1,
                    .pair_index = o,
                    .regs = def->args_ct[o].regs >> 1,
                };
                def->args_ct[o].pair = 2;
                def->args_ct[o].pair_index = i;
                tcg_debug_assert(ct_str[1] == '\0');
                continue;
            }

            do {
                switch (*ct_str) {
                case 'i':
                    def->args_ct[i].ct |= TCG_CT_CONST;
                    break;

                /* Include all of the target-specific constraints. */

#undef CONST
#define CONST(CASE, MASK) \
    case CASE: def->args_ct[i].ct |= MASK; break;
#define REGS(CASE, MASK) \
    case CASE: def->args_ct[i].regs |= MASK; break;

#include "tcg-target-con-str.h"

#undef REGS
#undef CONST
                default:
                case '0' ... '9':
                case '&':
                case 'p':
                case 'm':
                    /* Typo in TCGTargetOpDef constraint. */
                    g_assert_not_reached();
                }
            } while (*++ct_str != '\0');
        }

        /* TCGTargetOpDef entry with too much information? */
        tcg_debug_assert(i == TCG_MAX_OP_ARGS || tdefs->args_ct_str[i] == NULL);

        /*
         * Fix up output pairs that are aliased with inputs.
         * When we created the alias, we copied pair from the output.
         * There are three cases:
         *    (1a) Pairs of inputs alias pairs of outputs.
         *    (1b) One input aliases the first of a pair of outputs.
         *    (2)  One input aliases the second of a pair of outputs.
         *
         * Case 1a is handled by making sure that the pair_index'es are
         * properly updated so that they appear the same as a pair of inputs.
         *
         * Case 1b is handled by setting the pair_index of the input to
         * itself, simply so it doesn't point to an unrelated argument.
         * Since we don't encounter the "second" during the input allocation
         * phase, nothing happens with the second half of the input pair.
         *
         * Case 2 is handled by setting the second input to pair=3, the
         * first output to pair=3, and the pair_index'es to match.
         */
        if (saw_alias_pair) {
            for (i = def->nb_oargs; i < nb_args; i++) {
                /*
                 * Since [0-9pm] must be alone in the constraint string,
                 * the only way they can both be set is if the pair comes
                 * from the output alias.
                 */
                if (!def->args_ct[i].ialias) {
                    continue;
                }
                switch (def->args_ct[i].pair) {
                case 0:
                    break;
                case 1:
                    o = def->args_ct[i].alias_index;
                    o2 = def->args_ct[o].pair_index;
                    tcg_debug_assert(def->args_ct[o].pair == 1);
                    tcg_debug_assert(def->args_ct[o2].pair == 2);
                    if (def->args_ct[o2].oalias) {
                        /* Case 1a */
                        i2 = def->args_ct[o2].alias_index;
                        tcg_debug_assert(def->args_ct[i2].pair == 2);
                        def->args_ct[i2].pair_index = i;
                        def->args_ct[i].pair_index = i2;
                    } else {
                        /* Case 1b */
                        def->args_ct[i].pair_index = i;
                    }
                    break;
                case 2:
                    o = def->args_ct[i].alias_index;
                    o2 = def->args_ct[o].pair_index;
                    tcg_debug_assert(def->args_ct[o].pair == 2);
                    tcg_debug_assert(def->args_ct[o2].pair == 1);
                    if (def->args_ct[o2].oalias) {
                        /* Case 1a */
                        i2 = def->args_ct[o2].alias_index;
                        tcg_debug_assert(def->args_ct[i2].pair == 1);
                        def->args_ct[i2].pair_index = i;
                        def->args_ct[i].pair_index = i2;
                    } else {
                        /* Case 2 */
                        def->args_ct[i].pair = 3;
                        def->args_ct[o2].pair = 3;
                        def->args_ct[i].pair_index = o2;
                        def->args_ct[o2].pair_index = i;
                    }
                    break;
                default:
                    g_assert_not_reached();
                }
            }
        }

        /* sort the constraints (XXX: this is just an heuristic) */
        sort_constraints(def, 0, def->nb_oargs);
        sort_constraints(def, def->nb_oargs, def->nb_iargs);
    }
}

static void remove_label_use(TCGOp *op, int idx)
{
    TCGLabel *label = arg_label(op->args[idx]);
    TCGLabelUse *use;

    QSIMPLEQ_FOREACH(use, &label->branches, next) {
        if (use->op == op) {
            QSIMPLEQ_REMOVE(&label->branches, use, TCGLabelUse, next);
            return;
        }
    }
    g_assert_not_reached();
}

void tcg_op_remove(TCGContext *s, TCGOp *op)
{
    switch (op->opc) {
    case INDEX_op_br:
        remove_label_use(op, 0);
        break;
    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        remove_label_use(op, 3);
        break;
    case INDEX_op_brcond2_i32:
        remove_label_use(op, 5);
        break;
    default:
        break;
    }

    QTAILQ_REMOVE(&s->ops, op, link);
    QTAILQ_INSERT_TAIL(&s->free_ops, op, link);
    s->nb_ops--;

#ifdef CONFIG_PROFILER
    qatomic_set(&s->prof.del_op_count, s->prof.del_op_count + 1);
#endif
}

void tcg_remove_ops_after(TCGOp *op)
{
    TCGContext *s = tcg_ctx;

    while (true) {
        TCGOp *last = tcg_last_op();
        if (last == op) {
            return;
        }
        tcg_op_remove(s, last);
    }
}

static TCGOp *tcg_op_alloc(TCGOpcode opc, unsigned nargs)
{
    TCGContext *s = tcg_ctx;
    TCGOp *op = NULL;

    if (unlikely(!QTAILQ_EMPTY(&s->free_ops))) {
        QTAILQ_FOREACH(op, &s->free_ops, link) {
            if (nargs <= op->nargs) {
                QTAILQ_REMOVE(&s->free_ops, op, link);
                nargs = op->nargs;
                goto found;
            }
        }
    }

    /* Most opcodes have 3 or 4 operands: reduce fragmentation. */
    nargs = MAX(4, nargs);
    op = tcg_malloc(sizeof(TCGOp) + sizeof(TCGArg) * nargs);

 found:
    memset(op, 0, offsetof(TCGOp, link));
    op->opc = opc;
    op->nargs = nargs;

    /* Check for bitfield overflow. */
    tcg_debug_assert(op->nargs == nargs);

    s->nb_ops++;
    return op;
}

TCGOp *tcg_emit_op(TCGOpcode opc, unsigned nargs)
{
    TCGOp *op = tcg_op_alloc(opc, nargs);
    QTAILQ_INSERT_TAIL(&tcg_ctx->ops, op, link);
    return op;
}

TCGOp *tcg_op_insert_before(TCGContext *s, TCGOp *old_op,
                            TCGOpcode opc, unsigned nargs)
{
    TCGOp *new_op = tcg_op_alloc(opc, nargs);
    QTAILQ_INSERT_BEFORE(old_op, new_op, link);
    return new_op;
}

TCGOp *tcg_op_insert_after(TCGContext *s, TCGOp *old_op,
                           TCGOpcode opc, unsigned nargs)
{
    TCGOp *new_op = tcg_op_alloc(opc, nargs);
    QTAILQ_INSERT_AFTER(&s->ops, old_op, new_op, link);
    return new_op;
}

static void move_label_uses(TCGLabel *to, TCGLabel *from)
{
    TCGLabelUse *u;

    QSIMPLEQ_FOREACH(u, &from->branches, next) {
        TCGOp *op = u->op;
        switch (op->opc) {
        case INDEX_op_br:
            op->args[0] = label_arg(to);
            break;
        case INDEX_op_brcond_i32:
        case INDEX_op_brcond_i64:
            op->args[3] = label_arg(to);
            break;
        case INDEX_op_brcond2_i32:
            op->args[5] = label_arg(to);
            break;
        default:
            g_assert_not_reached();
        }
    }

    QSIMPLEQ_CONCAT(&to->branches, &from->branches);
}

/* Reachable analysis : remove unreachable code.  */
static void __attribute__((noinline))
reachable_code_pass(TCGContext *s)
{
    TCGOp *op, *op_next, *op_prev;
    bool dead = false;

    QTAILQ_FOREACH_SAFE(op, &s->ops, link, op_next) {
        bool remove = dead;
        TCGLabel *label;

        switch (op->opc) {
        case INDEX_op_set_label:
            label = arg_label(op->args[0]);

            /*
             * Note that the first op in the TB is always a load,
             * so there is always something before a label.
             */
            op_prev = QTAILQ_PREV(op, link);

            /*
             * If we find two sequential labels, move all branches to
             * reference the second label and remove the first label.
             * Do this before branch to next optimization, so that the
             * middle label is out of the way.
             */
            if (op_prev->opc == INDEX_op_set_label) {
                move_label_uses(label, arg_label(op_prev->args[0]));
                tcg_op_remove(s, op_prev);
                op_prev = QTAILQ_PREV(op, link);
            }

            /*
             * Optimization can fold conditional branches to unconditional.
             * If we find a label which is preceded by an unconditional
             * branch to next, remove the branch.  We couldn't do this when
             * processing the branch because any dead code between the branch
             * and label had not yet been removed.
             */
            if (op_prev->opc == INDEX_op_br &&
                label == arg_label(op_prev->args[0])) {
                tcg_op_remove(s, op_prev);
                /* Fall through means insns become live again.  */
                dead = false;
            }

            if (QSIMPLEQ_EMPTY(&label->branches)) {
                /*
                 * While there is an occasional backward branch, virtually
                 * all branches generated by the translators are forward.
                 * Which means that generally we will have already removed
                 * all references to the label that will be, and there is
                 * little to be gained by iterating.
                 */
                remove = true;
            } else {
                /* Once we see a label, insns become live again.  */
                dead = false;
                remove = false;
            }
            break;

        case INDEX_op_br:
        case INDEX_op_exit_tb:
        case INDEX_op_goto_ptr:
            /* Unconditional branches; everything following is dead.  */
            dead = true;
            break;

        case INDEX_op_call:
            /* Notice noreturn helper calls, raising exceptions.  */
            if (tcg_call_flags(op) & TCG_CALL_NO_RETURN) {
                dead = true;
            }
            break;

        case INDEX_op_insn_start:
            /* Never remove -- we need to keep these for unwind.  */
            remove = false;
            break;

        default:
            break;
        }

        if (remove) {
            tcg_op_remove(s, op);
        }
    }
}

#define TS_DEAD  1
#define TS_MEM   2

#define IS_DEAD_ARG(n)   (arg_life & (DEAD_ARG << (n)))
#define NEED_SYNC_ARG(n) (arg_life & (SYNC_ARG << (n)))

/* For liveness_pass_1, the register preferences for a given temp.  */
static inline TCGRegSet *la_temp_pref(TCGTemp *ts)
{
    return ts->state_ptr;
}

/* For liveness_pass_1, reset the preferences for a given temp to the
 * maximal regset for its type.
 */
static inline void la_reset_pref(TCGTemp *ts)
{
    *la_temp_pref(ts)
        = (ts->state == TS_DEAD ? 0 : tcg_target_available_regs[ts->type]);
}

/* liveness analysis: end of function: all temps are dead, and globals
   should be in memory. */
static void la_func_end(TCGContext *s, int ng, int nt)
{
    int i;

    for (i = 0; i < ng; ++i) {
        s->temps[i].state = TS_DEAD | TS_MEM;
        la_reset_pref(&s->temps[i]);
    }
    for (i = ng; i < nt; ++i) {
        s->temps[i].state = TS_DEAD;
        la_reset_pref(&s->temps[i]);
    }
}

/* liveness analysis: end of basic block: all temps are dead, globals
   and local temps should be in memory. */
static void la_bb_end(TCGContext *s, int ng, int nt)
{
    int i;

    for (i = 0; i < nt; ++i) {
        TCGTemp *ts = &s->temps[i];
        int state;

        switch (ts->kind) {
        case TEMP_FIXED:
        case TEMP_GLOBAL:
        case TEMP_TB:
            state = TS_DEAD | TS_MEM;
            break;
        case TEMP_EBB:
        case TEMP_CONST:
            state = TS_DEAD;
            break;
        default:
            g_assert_not_reached();
        }
        ts->state = state;
        la_reset_pref(ts);
    }
}

/* liveness analysis: sync globals back to memory.  */
static void la_global_sync(TCGContext *s, int ng)
{
    int i;

    for (i = 0; i < ng; ++i) {
        int state = s->temps[i].state;
        s->temps[i].state = state | TS_MEM;
        if (state == TS_DEAD) {
            /* If the global was previously dead, reset prefs.  */
            la_reset_pref(&s->temps[i]);
        }
    }
}

/*
 * liveness analysis: conditional branch: all temps are dead unless
 * explicitly live-across-conditional-branch, globals and local temps
 * should be synced.
 */
static void la_bb_sync(TCGContext *s, int ng, int nt)
{
    la_global_sync(s, ng);

    for (int i = ng; i < nt; ++i) {
        TCGTemp *ts = &s->temps[i];
        int state;

        switch (ts->kind) {
        case TEMP_TB:
            state = ts->state;
            ts->state = state | TS_MEM;
            if (state != TS_DEAD) {
                continue;
            }
            break;
        case TEMP_EBB:
        case TEMP_CONST:
            continue;
        default:
            g_assert_not_reached();
        }
        la_reset_pref(&s->temps[i]);
    }
}

/* liveness analysis: sync globals back to memory and kill.  */
static void la_global_kill(TCGContext *s, int ng)
{
    int i;

    for (i = 0; i < ng; i++) {
        s->temps[i].state = TS_DEAD | TS_MEM;
        la_reset_pref(&s->temps[i]);
    }
}

/* liveness analysis: note live globals crossing calls.  */
static void la_cross_call(TCGContext *s, int nt)
{
    TCGRegSet mask = ~tcg_target_call_clobber_regs;
    int i;

    for (i = 0; i < nt; i++) {
        TCGTemp *ts = &s->temps[i];
        if (!(ts->state & TS_DEAD)) {
            TCGRegSet *pset = la_temp_pref(ts);
            TCGRegSet set = *pset;

            set &= mask;
            /* If the combination is not possible, restart.  */
            if (set == 0) {
                set = tcg_target_available_regs[ts->type] & mask;
            }
            *pset = set;
        }
    }
}

/*
 * Liveness analysis: Verify the lifetime of TEMP_TB, and reduce
 * to TEMP_EBB, if possible.
 */
static void __attribute__((noinline))
liveness_pass_0(TCGContext *s)
{
    void * const multiple_ebb = (void *)(uintptr_t)-1;
    int nb_temps = s->nb_temps;
    TCGOp *op, *ebb;

    for (int i = s->nb_globals; i < nb_temps; ++i) {
        s->temps[i].state_ptr = NULL;
    }

    /*
     * Represent each EBB by the op at which it begins.  In the case of
     * the first EBB, this is the first op, otherwise it is a label.
     * Collect the uses of each TEMP_TB: NULL for unused, EBB for use
     * within a single EBB, else MULTIPLE_EBB.
     */
    ebb = QTAILQ_FIRST(&s->ops);
    QTAILQ_FOREACH(op, &s->ops, link) {
        const TCGOpDef *def;
        int nb_oargs, nb_iargs;

        switch (op->opc) {
        case INDEX_op_set_label:
            ebb = op;
            continue;
        case INDEX_op_discard:
            continue;
        case INDEX_op_call:
            nb_oargs = TCGOP_CALLO(op);
            nb_iargs = TCGOP_CALLI(op);
            break;
        default:
            def = &tcg_op_defs[op->opc];
            nb_oargs = def->nb_oargs;
            nb_iargs = def->nb_iargs;
            break;
        }

        for (int i = 0; i < nb_oargs + nb_iargs; ++i) {
            TCGTemp *ts = arg_temp(op->args[i]);

            if (ts->kind != TEMP_TB) {
                continue;
            }
            if (ts->state_ptr == NULL) {
                ts->state_ptr = ebb;
            } else if (ts->state_ptr != ebb) {
                ts->state_ptr = multiple_ebb;
            }
        }
    }

    /*
     * For TEMP_TB that turned out not to be used beyond one EBB,
     * reduce the liveness to TEMP_EBB.
     */
    for (int i = s->nb_globals; i < nb_temps; ++i) {
        TCGTemp *ts = &s->temps[i];
        if (ts->kind == TEMP_TB && ts->state_ptr != multiple_ebb) {
            ts->kind = TEMP_EBB;
        }
    }
}

/* Liveness analysis : update the opc_arg_life array to tell if a
   given input arguments is dead. Instructions updating dead
   temporaries are removed. */
static void __attribute__((noinline))
liveness_pass_1(TCGContext *s)
{
    int nb_globals = s->nb_globals;
    int nb_temps = s->nb_temps;
    TCGOp *op, *op_prev;
    TCGRegSet *prefs;
    int i;

    prefs = tcg_malloc(sizeof(TCGRegSet) * nb_temps);
    for (i = 0; i < nb_temps; ++i) {
        s->temps[i].state_ptr = prefs + i;
    }

    /* ??? Should be redundant with the exit_tb that ends the TB.  */
    la_func_end(s, nb_globals, nb_temps);

    QTAILQ_FOREACH_REVERSE_SAFE(op, &s->ops, link, op_prev) {
        int nb_iargs, nb_oargs;
        TCGOpcode opc_new, opc_new2;
        bool have_opc_new2;
        TCGLifeData arg_life = 0;
        TCGTemp *ts;
        TCGOpcode opc = op->opc;
        const TCGOpDef *def = &tcg_op_defs[opc];

        switch (opc) {
        case INDEX_op_call:
            {
                const TCGHelperInfo *info = tcg_call_info(op);
                int call_flags = tcg_call_flags(op);

                nb_oargs = TCGOP_CALLO(op);
                nb_iargs = TCGOP_CALLI(op);

                /* pure functions can be removed if their result is unused */
                if (call_flags & TCG_CALL_NO_SIDE_EFFECTS) {
                    for (i = 0; i < nb_oargs; i++) {
                        ts = arg_temp(op->args[i]);
                        if (ts->state != TS_DEAD) {
                            goto do_not_remove_call;
                        }
                    }
                    goto do_remove;
                }
            do_not_remove_call:

                /* Output args are dead.  */
                for (i = 0; i < nb_oargs; i++) {
                    ts = arg_temp(op->args[i]);
                    if (ts->state & TS_DEAD) {
                        arg_life |= DEAD_ARG << i;
                    }
                    if (ts->state & TS_MEM) {
                        arg_life |= SYNC_ARG << i;
                    }
                    ts->state = TS_DEAD;
                    la_reset_pref(ts);
                }

                /* Not used -- it will be tcg_target_call_oarg_reg().  */
                memset(op->output_pref, 0, sizeof(op->output_pref));

                if (!(call_flags & (TCG_CALL_NO_WRITE_GLOBALS |
                                    TCG_CALL_NO_READ_GLOBALS))) {
                    la_global_kill(s, nb_globals);
                } else if (!(call_flags & TCG_CALL_NO_READ_GLOBALS)) {
                    la_global_sync(s, nb_globals);
                }

                /* Record arguments that die in this helper.  */
                for (i = nb_oargs; i < nb_iargs + nb_oargs; i++) {
                    ts = arg_temp(op->args[i]);
                    if (ts->state & TS_DEAD) {
                        arg_life |= DEAD_ARG << i;
                    }
                }

                /* For all live registers, remove call-clobbered prefs.  */
                la_cross_call(s, nb_temps);

                /*
                 * Input arguments are live for preceding opcodes.
                 *
                 * For those arguments that die, and will be allocated in
                 * registers, clear the register set for that arg, to be
                 * filled in below.  For args that will be on the stack,
                 * reset to any available reg.  Process arguments in reverse
                 * order so that if a temp is used more than once, the stack
                 * reset to max happens before the register reset to 0.
                 */
                for (i = nb_iargs - 1; i >= 0; i--) {
                    const TCGCallArgumentLoc *loc = &info->in[i];
                    ts = arg_temp(op->args[nb_oargs + i]);

                    if (ts->state & TS_DEAD) {
                        switch (loc->kind) {
                        case TCG_CALL_ARG_NORMAL:
                        case TCG_CALL_ARG_EXTEND_U:
                        case TCG_CALL_ARG_EXTEND_S:
                            if (arg_slot_reg_p(loc->arg_slot)) {
                                *la_temp_pref(ts) = 0;
                                break;
                            }
                            /* fall through */
                        default:
                            *la_temp_pref(ts) =
                                tcg_target_available_regs[ts->type];
                            break;
                        }
                        ts->state &= ~TS_DEAD;
                    }
                }

                /*
                 * For each input argument, add its input register to prefs.
                 * If a temp is used once, this produces a single set bit;
                 * if a temp is used multiple times, this produces a set.
                 */
                for (i = 0; i < nb_iargs; i++) {
                    const TCGCallArgumentLoc *loc = &info->in[i];
                    ts = arg_temp(op->args[nb_oargs + i]);

                    switch (loc->kind) {
                    case TCG_CALL_ARG_NORMAL:
                    case TCG_CALL_ARG_EXTEND_U:
                    case TCG_CALL_ARG_EXTEND_S:
                        if (arg_slot_reg_p(loc->arg_slot)) {
                            tcg_regset_set_reg(*la_temp_pref(ts),
                                tcg_target_call_iarg_regs[loc->arg_slot]);
                        }
                        break;
                    default:
                        break;
                    }
                }
            }
            break;
        case INDEX_op_insn_start:
            break;
        case INDEX_op_discard:
            /* mark the temporary as dead */
            ts = arg_temp(op->args[0]);
            ts->state = TS_DEAD;
            la_reset_pref(ts);
            break;

        case INDEX_op_add2_i32:
            opc_new = INDEX_op_add_i32;
            goto do_addsub2;
        case INDEX_op_sub2_i32:
            opc_new = INDEX_op_sub_i32;
            goto do_addsub2;
        case INDEX_op_add2_i64:
            opc_new = INDEX_op_add_i64;
            goto do_addsub2;
        case INDEX_op_sub2_i64:
            opc_new = INDEX_op_sub_i64;
        do_addsub2:
            nb_iargs = 4;
            nb_oargs = 2;
            /* Test if the high part of the operation is dead, but not
               the low part.  The result can be optimized to a simple
               add or sub.  This happens often for x86_64 guest when the
               cpu mode is set to 32 bit.  */
            if (arg_temp(op->args[1])->state == TS_DEAD) {
                if (arg_temp(op->args[0])->state == TS_DEAD) {
                    goto do_remove;
                }
                /* Replace the opcode and adjust the args in place,
                   leaving 3 unused args at the end.  */
                op->opc = opc = opc_new;
                op->args[1] = op->args[2];
                op->args[2] = op->args[4];
                /* Fall through and mark the single-word operation live.  */
                nb_iargs = 2;
                nb_oargs = 1;
            }
            goto do_not_remove;

        case INDEX_op_mulu2_i32:
            opc_new = INDEX_op_mul_i32;
            opc_new2 = INDEX_op_muluh_i32;
            have_opc_new2 = TCG_TARGET_HAS_muluh_i32;
            goto do_mul2;
        case INDEX_op_muls2_i32:
            opc_new = INDEX_op_mul_i32;
            opc_new2 = INDEX_op_mulsh_i32;
            have_opc_new2 = TCG_TARGET_HAS_mulsh_i32;
            goto do_mul2;
        case INDEX_op_mulu2_i64:
            opc_new = INDEX_op_mul_i64;
            opc_new2 = INDEX_op_muluh_i64;
            have_opc_new2 = TCG_TARGET_HAS_muluh_i64;
            goto do_mul2;
        case INDEX_op_muls2_i64:
            opc_new = INDEX_op_mul_i64;
            opc_new2 = INDEX_op_mulsh_i64;
            have_opc_new2 = TCG_TARGET_HAS_mulsh_i64;
            goto do_mul2;
        do_mul2:
            nb_iargs = 2;
            nb_oargs = 2;
            if (arg_temp(op->args[1])->state == TS_DEAD) {
                if (arg_temp(op->args[0])->state == TS_DEAD) {
                    /* Both parts of the operation are dead.  */
                    goto do_remove;
                }
                /* The high part of the operation is dead; generate the low. */
                op->opc = opc = opc_new;
                op->args[1] = op->args[2];
                op->args[2] = op->args[3];
            } else if (arg_temp(op->args[0])->state == TS_DEAD && have_opc_new2) {
                /* The low part of the operation is dead; generate the high. */
                op->opc = opc = opc_new2;
                op->args[0] = op->args[1];
                op->args[1] = op->args[2];
                op->args[2] = op->args[3];
            } else {
                goto do_not_remove;
            }
            /* Mark the single-word operation live.  */
            nb_oargs = 1;
            goto do_not_remove;

        default:
            /* XXX: optimize by hardcoding common cases (e.g. triadic ops) */
            nb_iargs = def->nb_iargs;
            nb_oargs = def->nb_oargs;

            /* Test if the operation can be removed because all
               its outputs are dead. We assume that nb_oargs == 0
               implies side effects */
            if (!(def->flags & TCG_OPF_SIDE_EFFECTS) && nb_oargs != 0) {
                for (i = 0; i < nb_oargs; i++) {
                    if (arg_temp(op->args[i])->state != TS_DEAD) {
                        goto do_not_remove;
                    }
                }
                goto do_remove;
            }
            goto do_not_remove;

        do_remove:
            tcg_op_remove(s, op);
            break;

        do_not_remove:
            for (i = 0; i < nb_oargs; i++) {
                ts = arg_temp(op->args[i]);

                /* Remember the preference of the uses that followed.  */
                if (i < ARRAY_SIZE(op->output_pref)) {
                    op->output_pref[i] = *la_temp_pref(ts);
                }

                /* Output args are dead.  */
                if (ts->state & TS_DEAD) {
                    arg_life |= DEAD_ARG << i;
                }
                if (ts->state & TS_MEM) {
                    arg_life |= SYNC_ARG << i;
                }
                ts->state = TS_DEAD;
                la_reset_pref(ts);
            }

            /* If end of basic block, update.  */
            if (def->flags & TCG_OPF_BB_EXIT) {
                la_func_end(s, nb_globals, nb_temps);
            } else if (def->flags & TCG_OPF_COND_BRANCH) {
                la_bb_sync(s, nb_globals, nb_temps);
            } else if (def->flags & TCG_OPF_BB_END) {
                la_bb_end(s, nb_globals, nb_temps);
            } else if (def->flags & TCG_OPF_SIDE_EFFECTS) {
                la_global_sync(s, nb_globals);
                if (def->flags & TCG_OPF_CALL_CLOBBER) {
                    la_cross_call(s, nb_temps);
                }
            }

            /* Record arguments that die in this opcode.  */
            for (i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
                ts = arg_temp(op->args[i]);
                if (ts->state & TS_DEAD) {
                    arg_life |= DEAD_ARG << i;
                }
            }

            /* Input arguments are live for preceding opcodes.  */
            for (i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
                ts = arg_temp(op->args[i]);
                if (ts->state & TS_DEAD) {
                    /* For operands that were dead, initially allow
                       all regs for the type.  */
                    *la_temp_pref(ts) = tcg_target_available_regs[ts->type];
                    ts->state &= ~TS_DEAD;
                }
            }

            /* Incorporate constraints for this operand.  */
            switch (opc) {
            case INDEX_op_mov_i32:
            case INDEX_op_mov_i64:
                /* Note that these are TCG_OPF_NOT_PRESENT and do not
                   have proper constraints.  That said, special case
                   moves to propagate preferences backward.  */
                if (IS_DEAD_ARG(1)) {
                    *la_temp_pref(arg_temp(op->args[0]))
                        = *la_temp_pref(arg_temp(op->args[1]));
                }
                break;

            default:
                for (i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
                    const TCGArgConstraint *ct = &def->args_ct[i];
                    TCGRegSet set, *pset;

                    ts = arg_temp(op->args[i]);
                    pset = la_temp_pref(ts);
                    set = *pset;

                    set &= ct->regs;
                    if (ct->ialias) {
                        set &= output_pref(op, ct->alias_index);
                    }
                    /* If the combination is not possible, restart.  */
                    if (set == 0) {
                        set = ct->regs;
                    }
                    *pset = set;
                }
                break;
            }
            break;
        }
        op->life = arg_life;
    }
}

/* Liveness analysis: Convert indirect regs to direct temporaries.  */
static bool __attribute__((noinline))
liveness_pass_2(TCGContext *s)
{
    int nb_globals = s->nb_globals;
    int nb_temps, i;
    bool changes = false;
    TCGOp *op, *op_next;

    /* Create a temporary for each indirect global.  */
    for (i = 0; i < nb_globals; ++i) {
        TCGTemp *its = &s->temps[i];
        if (its->indirect_reg) {
            TCGTemp *dts = tcg_temp_alloc(s);
            dts->type = its->type;
            dts->base_type = its->base_type;
            dts->temp_subindex = its->temp_subindex;
            dts->kind = TEMP_EBB;
            its->state_ptr = dts;
        } else {
            its->state_ptr = NULL;
        }
        /* All globals begin dead.  */
        its->state = TS_DEAD;
    }
    for (nb_temps = s->nb_temps; i < nb_temps; ++i) {
        TCGTemp *its = &s->temps[i];
        its->state_ptr = NULL;
        its->state = TS_DEAD;
    }

    QTAILQ_FOREACH_SAFE(op, &s->ops, link, op_next) {
        TCGOpcode opc = op->opc;
        const TCGOpDef *def = &tcg_op_defs[opc];
        TCGLifeData arg_life = op->life;
        int nb_iargs, nb_oargs, call_flags;
        TCGTemp *arg_ts, *dir_ts;

        if (opc == INDEX_op_call) {
            nb_oargs = TCGOP_CALLO(op);
            nb_iargs = TCGOP_CALLI(op);
            call_flags = tcg_call_flags(op);
        } else {
            nb_iargs = def->nb_iargs;
            nb_oargs = def->nb_oargs;

            /* Set flags similar to how calls require.  */
            if (def->flags & TCG_OPF_COND_BRANCH) {
                /* Like reading globals: sync_globals */
                call_flags = TCG_CALL_NO_WRITE_GLOBALS;
            } else if (def->flags & TCG_OPF_BB_END) {
                /* Like writing globals: save_globals */
                call_flags = 0;
            } else if (def->flags & TCG_OPF_SIDE_EFFECTS) {
                /* Like reading globals: sync_globals */
                call_flags = TCG_CALL_NO_WRITE_GLOBALS;
            } else {
                /* No effect on globals.  */
                call_flags = (TCG_CALL_NO_READ_GLOBALS |
                              TCG_CALL_NO_WRITE_GLOBALS);
            }
        }

        /* Make sure that input arguments are available.  */
        for (i = nb_oargs; i < nb_iargs + nb_oargs; i++) {
            arg_ts = arg_temp(op->args[i]);
            dir_ts = arg_ts->state_ptr;
            if (dir_ts && arg_ts->state == TS_DEAD) {
                TCGOpcode lopc = (arg_ts->type == TCG_TYPE_I32
                                  ? INDEX_op_ld_i32
                                  : INDEX_op_ld_i64);
                TCGOp *lop = tcg_op_insert_before(s, op, lopc, 3);

                lop->args[0] = temp_arg(dir_ts);
                lop->args[1] = temp_arg(arg_ts->mem_base);
                lop->args[2] = arg_ts->mem_offset;

                /* Loaded, but synced with memory.  */
                arg_ts->state = TS_MEM;
            }
        }

        /* Perform input replacement, and mark inputs that became dead.
           No action is required except keeping temp_state up to date
           so that we reload when needed.  */
        for (i = nb_oargs; i < nb_iargs + nb_oargs; i++) {
            arg_ts = arg_temp(op->args[i]);
            dir_ts = arg_ts->state_ptr;
            if (dir_ts) {
                op->args[i] = temp_arg(dir_ts);
                changes = true;
                if (IS_DEAD_ARG(i)) {
                    arg_ts->state = TS_DEAD;
                }
            }
        }

        /* Liveness analysis should ensure that the following are
           all correct, for call sites and basic block end points.  */
        if (call_flags & TCG_CALL_NO_READ_GLOBALS) {
            /* Nothing to do */
        } else if (call_flags & TCG_CALL_NO_WRITE_GLOBALS) {
            for (i = 0; i < nb_globals; ++i) {
                /* Liveness should see that globals are synced back,
                   that is, either TS_DEAD or TS_MEM.  */
                arg_ts = &s->temps[i];
                tcg_debug_assert(arg_ts->state_ptr == 0
                                 || arg_ts->state != 0);
            }
        } else {
            for (i = 0; i < nb_globals; ++i) {
                /* Liveness should see that globals are saved back,
                   that is, TS_DEAD, waiting to be reloaded.  */
                arg_ts = &s->temps[i];
                tcg_debug_assert(arg_ts->state_ptr == 0
                                 || arg_ts->state == TS_DEAD);
            }
        }

        /* Outputs become available.  */
        if (opc == INDEX_op_mov_i32 || opc == INDEX_op_mov_i64) {
            arg_ts = arg_temp(op->args[0]);
            dir_ts = arg_ts->state_ptr;
            if (dir_ts) {
                op->args[0] = temp_arg(dir_ts);
                changes = true;

                /* The output is now live and modified.  */
                arg_ts->state = 0;

                if (NEED_SYNC_ARG(0)) {
                    TCGOpcode sopc = (arg_ts->type == TCG_TYPE_I32
                                      ? INDEX_op_st_i32
                                      : INDEX_op_st_i64);
                    TCGOp *sop = tcg_op_insert_after(s, op, sopc, 3);
                    TCGTemp *out_ts = dir_ts;

                    if (IS_DEAD_ARG(0)) {
                        out_ts = arg_temp(op->args[1]);
                        arg_ts->state = TS_DEAD;
                        tcg_op_remove(s, op);
                    } else {
                        arg_ts->state = TS_MEM;
                    }

                    sop->args[0] = temp_arg(out_ts);
                    sop->args[1] = temp_arg(arg_ts->mem_base);
                    sop->args[2] = arg_ts->mem_offset;
                } else {
                    tcg_debug_assert(!IS_DEAD_ARG(0));
                }
            }
        } else {
            for (i = 0; i < nb_oargs; i++) {
                arg_ts = arg_temp(op->args[i]);
                dir_ts = arg_ts->state_ptr;
                if (!dir_ts) {
                    continue;
                }
                op->args[i] = temp_arg(dir_ts);
                changes = true;

                /* The output is now live and modified.  */
                arg_ts->state = 0;

                /* Sync outputs upon their last write.  */
                if (NEED_SYNC_ARG(i)) {
                    TCGOpcode sopc = (arg_ts->type == TCG_TYPE_I32
                                      ? INDEX_op_st_i32
                                      : INDEX_op_st_i64);
                    TCGOp *sop = tcg_op_insert_after(s, op, sopc, 3);

                    sop->args[0] = temp_arg(dir_ts);
                    sop->args[1] = temp_arg(arg_ts->mem_base);
                    sop->args[2] = arg_ts->mem_offset;

                    arg_ts->state = TS_MEM;
                }
                /* Drop outputs that are dead.  */
                if (IS_DEAD_ARG(i)) {
                    arg_ts->state = TS_DEAD;
                }
            }
        }
    }

    return changes;
}

static void temp_allocate_frame(TCGContext *s, TCGTemp *ts)
{
    intptr_t off;
    int size, align;

    /* When allocating an object, look at the full type. */
    size = tcg_type_size(ts->base_type);
    switch (ts->base_type) {
    case TCG_TYPE_I32:
        align = 4;
        break;
    case TCG_TYPE_I64:
    case TCG_TYPE_V64:
        align = 8;
        break;
    case TCG_TYPE_I128:
    case TCG_TYPE_V128:
    case TCG_TYPE_V256:
        /*
         * Note that we do not require aligned storage for V256,
         * and that we provide alignment for I128 to match V128,
         * even if that's above what the host ABI requires.
         */
        align = 16;
        break;
    default:
        g_assert_not_reached();
    }

    /*
     * Assume the stack is sufficiently aligned.
     * This affects e.g. ARM NEON, where we have 8 byte stack alignment
     * and do not require 16 byte vector alignment.  This seems slightly
     * easier than fully parameterizing the above switch statement.
     */
    align = MIN(TCG_TARGET_STACK_ALIGN, align);
    off = ROUND_UP(s->current_frame_offset, align);

    /* If we've exhausted the stack frame, restart with a smaller TB. */
    if (off + size > s->frame_end) {
        tcg_raise_tb_overflow(s);
    }
    s->current_frame_offset = off + size;
#if defined(__sparc__)
    off += TCG_TARGET_STACK_BIAS;
#endif

    /* If the object was subdivided, assign memory to all the parts. */
    if (ts->base_type != ts->type) {
        int part_size = tcg_type_size(ts->type);
        int part_count = size / part_size;

        /*
         * Each part is allocated sequentially in tcg_temp_new_internal.
         * Jump back to the first part by subtracting the current index.
         */
        ts -= ts->temp_subindex;
        for (int i = 0; i < part_count; ++i) {
            ts[i].mem_offset = off + i * part_size;
            ts[i].mem_base = s->frame_temp;
            ts[i].mem_allocated = 1;
        }
    } else {
        ts->mem_offset = off;
        ts->mem_base = s->frame_temp;
        ts->mem_allocated = 1;
    }
}

/* Assign @reg to @ts, and update reg_to_temp[]. */
static void set_temp_val_reg(TCGContext *s, TCGTemp *ts, TCGReg reg)
{
    if (ts->val_type == TEMP_VAL_REG) {
        TCGReg old = ts->reg;
        tcg_debug_assert(s->reg_to_temp[old] == ts);
        if (old == reg) {
            return;
        }
        s->reg_to_temp[old] = NULL;
    }
    tcg_debug_assert(s->reg_to_temp[reg] == NULL);
    s->reg_to_temp[reg] = ts;
    ts->val_type = TEMP_VAL_REG;
    ts->reg = reg;
}

/* Assign a non-register value type to @ts, and update reg_to_temp[]. */
static void set_temp_val_nonreg(TCGContext *s, TCGTemp *ts, TCGTempVal type)
{
    tcg_debug_assert(type != TEMP_VAL_REG);
    if (ts->val_type == TEMP_VAL_REG) {
        TCGReg reg = ts->reg;
        tcg_debug_assert(s->reg_to_temp[reg] == ts);
        s->reg_to_temp[reg] = NULL;
    }
    ts->val_type = type;
}

static void temp_load(TCGContext *, TCGTemp *, TCGRegSet, TCGRegSet, TCGRegSet);

/* Mark a temporary as free or dead.  If 'free_or_dead' is negative,
   mark it free; otherwise mark it dead.  */
static void temp_free_or_dead(TCGContext *s, TCGTemp *ts, int free_or_dead)
{
    TCGTempVal new_type;

    switch (ts->kind) {
    case TEMP_FIXED:
        return;
    case TEMP_GLOBAL:
    case TEMP_TB:
        new_type = TEMP_VAL_MEM;
        break;
    case TEMP_EBB:
        new_type = free_or_dead < 0 ? TEMP_VAL_MEM : TEMP_VAL_DEAD;
        break;
    case TEMP_CONST:
        new_type = TEMP_VAL_CONST;
        break;
    default:
        g_assert_not_reached();
    }
    set_temp_val_nonreg(s, ts, new_type);
}

/* Mark a temporary as dead.  */
static inline void temp_dead(TCGContext *s, TCGTemp *ts)
{
    temp_free_or_dead(s, ts, 1);
}

/* Sync a temporary to memory. 'allocated_regs' is used in case a temporary
   registers needs to be allocated to store a constant.  If 'free_or_dead'
   is non-zero, subsequently release the temporary; if it is positive, the
   temp is dead; if it is negative, the temp is free.  */
static void temp_sync(TCGContext *s, TCGTemp *ts, TCGRegSet allocated_regs,
                      TCGRegSet preferred_regs, int free_or_dead)
{
    if (!temp_readonly(ts) && !ts->mem_coherent) {
        if (!ts->mem_allocated) {
            temp_allocate_frame(s, ts);
        }
        switch (ts->val_type) {
        case TEMP_VAL_CONST:
            /* If we're going to free the temp immediately, then we won't
               require it later in a register, so attempt to store the
               constant to memory directly.  */
            if (free_or_dead
                && tcg_out_sti(s, ts->type, ts->val,
                               ts->mem_base->reg, ts->mem_offset)) {
                break;
            }
            temp_load(s, ts, tcg_target_available_regs[ts->type],
                      allocated_regs, preferred_regs);
            /* fallthrough */

        case TEMP_VAL_REG:
            tcg_out_st(s, ts->type, ts->reg,
                       ts->mem_base->reg, ts->mem_offset);
            break;

        case TEMP_VAL_MEM:
            break;

        case TEMP_VAL_DEAD:
        default:
            g_assert_not_reached();
        }
        ts->mem_coherent = 1;
    }
    if (free_or_dead) {
        temp_free_or_dead(s, ts, free_or_dead);
    }
}

/* free register 'reg' by spilling the corresponding temporary if necessary */
static void tcg_reg_free(TCGContext *s, TCGReg reg, TCGRegSet allocated_regs)
{
    TCGTemp *ts = s->reg_to_temp[reg];
    if (ts != NULL) {
        temp_sync(s, ts, allocated_regs, 0, -1);
    }
}

/**
 * tcg_reg_alloc:
 * @required_regs: Set of registers in which we must allocate.
 * @allocated_regs: Set of registers which must be avoided.
 * @preferred_regs: Set of registers we should prefer.
 * @rev: True if we search the registers in "indirect" order.
 *
 * The allocated register must be in @required_regs & ~@allocated_regs,
 * but if we can put it in @preferred_regs we may save a move later.
 */
static TCGReg tcg_reg_alloc(TCGContext *s, TCGRegSet required_regs,
                            TCGRegSet allocated_regs,
                            TCGRegSet preferred_regs, bool rev)
{
    int i, j, f, n = ARRAY_SIZE(tcg_target_reg_alloc_order);
    TCGRegSet reg_ct[2];
    const int *order;

    reg_ct[1] = required_regs & ~allocated_regs;
    tcg_debug_assert(reg_ct[1] != 0);
    reg_ct[0] = reg_ct[1] & preferred_regs;

    /* Skip the preferred_regs option if it cannot be satisfied,
       or if the preference made no difference.  */
    f = reg_ct[0] == 0 || reg_ct[0] == reg_ct[1];

    order = rev ? indirect_reg_alloc_order : tcg_target_reg_alloc_order;

    /* Try free registers, preferences first.  */
    for (j = f; j < 2; j++) {
        TCGRegSet set = reg_ct[j];

        if (tcg_regset_single(set)) {
            /* One register in the set.  */
            TCGReg reg = tcg_regset_first(set);
            if (s->reg_to_temp[reg] == NULL) {
                return reg;
            }
        } else {
            for (i = 0; i < n; i++) {
                TCGReg reg = order[i];
                if (s->reg_to_temp[reg] == NULL &&
                    tcg_regset_test_reg(set, reg)) {
                    return reg;
                }
            }
        }
    }

    /* We must spill something.  */
    for (j = f; j < 2; j++) {
        TCGRegSet set = reg_ct[j];

        if (tcg_regset_single(set)) {
            /* One register in the set.  */
            TCGReg reg = tcg_regset_first(set);
            tcg_reg_free(s, reg, allocated_regs);
            return reg;
        } else {
            for (i = 0; i < n; i++) {
                TCGReg reg = order[i];
                if (tcg_regset_test_reg(set, reg)) {
                    tcg_reg_free(s, reg, allocated_regs);
                    return reg;
                }
            }
        }
    }

    g_assert_not_reached();
}

static TCGReg tcg_reg_alloc_pair(TCGContext *s, TCGRegSet required_regs,
                                 TCGRegSet allocated_regs,
                                 TCGRegSet preferred_regs, bool rev)
{
    int i, j, k, fmin, n = ARRAY_SIZE(tcg_target_reg_alloc_order);
    TCGRegSet reg_ct[2];
    const int *order;

    /* Ensure that if I is not in allocated_regs, I+1 is not either. */
    reg_ct[1] = required_regs & ~(allocated_regs | (allocated_regs >> 1));
    tcg_debug_assert(reg_ct[1] != 0);
    reg_ct[0] = reg_ct[1] & preferred_regs;

    order = rev ? indirect_reg_alloc_order : tcg_target_reg_alloc_order;

    /*
     * Skip the preferred_regs option if it cannot be satisfied,
     * or if the preference made no difference.
     */
    k = reg_ct[0] == 0 || reg_ct[0] == reg_ct[1];

    /*
     * Minimize the number of flushes by looking for 2 free registers first,
     * then a single flush, then two flushes.
     */
    for (fmin = 2; fmin >= 0; fmin--) {
        for (j = k; j < 2; j++) {
            TCGRegSet set = reg_ct[j];

            for (i = 0; i < n; i++) {
                TCGReg reg = order[i];

                if (tcg_regset_test_reg(set, reg)) {
                    int f = !s->reg_to_temp[reg] + !s->reg_to_temp[reg + 1];
                    if (f >= fmin) {
                        tcg_reg_free(s, reg, allocated_regs);
                        tcg_reg_free(s, reg + 1, allocated_regs);
                        return reg;
                    }
                }
            }
        }
    }
    g_assert_not_reached();
}

/* Make sure the temporary is in a register.  If needed, allocate the register
   from DESIRED while avoiding ALLOCATED.  */
static void temp_load(TCGContext *s, TCGTemp *ts, TCGRegSet desired_regs,
                      TCGRegSet allocated_regs, TCGRegSet preferred_regs)
{
    TCGReg reg;

    switch (ts->val_type) {
    case TEMP_VAL_REG:
        return;
    case TEMP_VAL_CONST:
        reg = tcg_reg_alloc(s, desired_regs, allocated_regs,
                            preferred_regs, ts->indirect_base);
        if (ts->type <= TCG_TYPE_I64) {
            tcg_out_movi(s, ts->type, reg, ts->val);
        } else {
            uint64_t val = ts->val;
            MemOp vece = MO_64;

            /*
             * Find the minimal vector element that matches the constant.
             * The targets will, in general, have to do this search anyway,
             * do this generically.
             */
            if (val == dup_const(MO_8, val)) {
                vece = MO_8;
            } else if (val == dup_const(MO_16, val)) {
                vece = MO_16;
            } else if (val == dup_const(MO_32, val)) {
                vece = MO_32;
            }

            tcg_out_dupi_vec(s, ts->type, vece, reg, ts->val);
        }
        ts->mem_coherent = 0;
        break;
    case TEMP_VAL_MEM:
        reg = tcg_reg_alloc(s, desired_regs, allocated_regs,
                            preferred_regs, ts->indirect_base);
        tcg_out_ld(s, ts->type, reg, ts->mem_base->reg, ts->mem_offset);
        ts->mem_coherent = 1;
        break;
    case TEMP_VAL_DEAD:
    default:
        g_assert_not_reached();
    }
    set_temp_val_reg(s, ts, reg);
}

/* Save a temporary to memory. 'allocated_regs' is used in case a
   temporary registers needs to be allocated to store a constant.  */
static void temp_save(TCGContext *s, TCGTemp *ts, TCGRegSet allocated_regs)
{
    /* The liveness analysis already ensures that globals are back
       in memory. Keep an tcg_debug_assert for safety. */
    tcg_debug_assert(ts->val_type == TEMP_VAL_MEM || temp_readonly(ts));
}

/* save globals to their canonical location and assume they can be
   modified be the following code. 'allocated_regs' is used in case a
   temporary registers needs to be allocated to store a constant. */
static void save_globals(TCGContext *s, TCGRegSet allocated_regs)
{
    int i, n;

    for (i = 0, n = s->nb_globals; i < n; i++) {
        temp_save(s, &s->temps[i], allocated_regs);
    }
}

/* sync globals to their canonical location and assume they can be
   read by the following code. 'allocated_regs' is used in case a
   temporary registers needs to be allocated to store a constant. */
static void sync_globals(TCGContext *s, TCGRegSet allocated_regs)
{
    int i, n;

    for (i = 0, n = s->nb_globals; i < n; i++) {
        TCGTemp *ts = &s->temps[i];
        tcg_debug_assert(ts->val_type != TEMP_VAL_REG
                         || ts->kind == TEMP_FIXED
                         || ts->mem_coherent);
    }
}

/* at the end of a basic block, we assume all temporaries are dead and
   all globals are stored at their canonical location. */
static void tcg_reg_alloc_bb_end(TCGContext *s, TCGRegSet allocated_regs)
{
    int i;

    for (i = s->nb_globals; i < s->nb_temps; i++) {
        TCGTemp *ts = &s->temps[i];

        switch (ts->kind) {
        case TEMP_TB:
            temp_save(s, ts, allocated_regs);
            break;
        case TEMP_EBB:
            /* The liveness analysis already ensures that temps are dead.
               Keep an tcg_debug_assert for safety. */
            tcg_debug_assert(ts->val_type == TEMP_VAL_DEAD);
            break;
        case TEMP_CONST:
            /* Similarly, we should have freed any allocated register. */
            tcg_debug_assert(ts->val_type == TEMP_VAL_CONST);
            break;
        default:
            g_assert_not_reached();
        }
    }

    save_globals(s, allocated_regs);
}

/*
 * At a conditional branch, we assume all temporaries are dead unless
 * explicitly live-across-conditional-branch; all globals and local
 * temps are synced to their location.
 */
static void tcg_reg_alloc_cbranch(TCGContext *s, TCGRegSet allocated_regs)
{
    sync_globals(s, allocated_regs);

    for (int i = s->nb_globals; i < s->nb_temps; i++) {
        TCGTemp *ts = &s->temps[i];
        /*
         * The liveness analysis already ensures that temps are dead.
         * Keep tcg_debug_asserts for safety.
         */
        switch (ts->kind) {
        case TEMP_TB:
            tcg_debug_assert(ts->val_type != TEMP_VAL_REG || ts->mem_coherent);
            break;
        case TEMP_EBB:
        case TEMP_CONST:
            break;
        default:
            g_assert_not_reached();
        }
    }
}

/*
 * Specialized code generation for INDEX_op_mov_* with a constant.
 */
static void tcg_reg_alloc_do_movi(TCGContext *s, TCGTemp *ots,
                                  tcg_target_ulong val, TCGLifeData arg_life,
                                  TCGRegSet preferred_regs)
{
    /* ENV should not be modified.  */
    tcg_debug_assert(!temp_readonly(ots));

    /* The movi is not explicitly generated here.  */
    set_temp_val_nonreg(s, ots, TEMP_VAL_CONST);
    ots->val = val;
    ots->mem_coherent = 0;
    if (NEED_SYNC_ARG(0)) {
        temp_sync(s, ots, s->reserved_regs, preferred_regs, IS_DEAD_ARG(0));
    } else if (IS_DEAD_ARG(0)) {
        temp_dead(s, ots);
    }
}

/*
 * Specialized code generation for INDEX_op_mov_*.
 */
static void tcg_reg_alloc_mov(TCGContext *s, const TCGOp *op)
{
    const TCGLifeData arg_life = op->life;
    TCGRegSet allocated_regs, preferred_regs;
    TCGTemp *ts, *ots;
    TCGType otype, itype;
    TCGReg oreg, ireg;

    allocated_regs = s->reserved_regs;
    preferred_regs = output_pref(op, 0);
    ots = arg_temp(op->args[0]);
    ts = arg_temp(op->args[1]);

    /* ENV should not be modified.  */
    tcg_debug_assert(!temp_readonly(ots));

    /* Note that otype != itype for no-op truncation.  */
    otype = ots->type;
    itype = ts->type;

    if (ts->val_type == TEMP_VAL_CONST) {
        /* propagate constant or generate sti */
        tcg_target_ulong val = ts->val;
        if (IS_DEAD_ARG(1)) {
            temp_dead(s, ts);
        }
        tcg_reg_alloc_do_movi(s, ots, val, arg_life, preferred_regs);
        return;
    }

    /* If the source value is in memory we're going to be forced
       to have it in a register in order to perform the copy.  Copy
       the SOURCE value into its own register first, that way we
       don't have to reload SOURCE the next time it is used. */
    if (ts->val_type == TEMP_VAL_MEM) {
        temp_load(s, ts, tcg_target_available_regs[itype],
                  allocated_regs, preferred_regs);
    }
    tcg_debug_assert(ts->val_type == TEMP_VAL_REG);
    ireg = ts->reg;

    if (IS_DEAD_ARG(0)) {
        /* mov to a non-saved dead register makes no sense (even with
           liveness analysis disabled). */
        tcg_debug_assert(NEED_SYNC_ARG(0));
        if (!ots->mem_allocated) {
            temp_allocate_frame(s, ots);
        }
        tcg_out_st(s, otype, ireg, ots->mem_base->reg, ots->mem_offset);
        if (IS_DEAD_ARG(1)) {
            temp_dead(s, ts);
        }
        temp_dead(s, ots);
        return;
    }

    if (IS_DEAD_ARG(1) && ts->kind != TEMP_FIXED) {
        /*
         * The mov can be suppressed.  Kill input first, so that it
         * is unlinked from reg_to_temp, then set the output to the
         * reg that we saved from the input.
         */
        temp_dead(s, ts);
        oreg = ireg;
    } else {
        if (ots->val_type == TEMP_VAL_REG) {
            oreg = ots->reg;
        } else {
            /* Make sure to not spill the input register during allocation. */
            oreg = tcg_reg_alloc(s, tcg_target_available_regs[otype],
                                 allocated_regs | ((TCGRegSet)1 << ireg),
                                 preferred_regs, ots->indirect_base);
        }
        if (!tcg_out_mov(s, otype, oreg, ireg)) {
            /*
             * Cross register class move not supported.
             * Store the source register into the destination slot
             * and leave the destination temp as TEMP_VAL_MEM.
             */
            assert(!temp_readonly(ots));
            if (!ts->mem_allocated) {
                temp_allocate_frame(s, ots);
            }
            tcg_out_st(s, ts->type, ireg, ots->mem_base->reg, ots->mem_offset);
            set_temp_val_nonreg(s, ts, TEMP_VAL_MEM);
            ots->mem_coherent = 1;
            return;
        }
    }
    set_temp_val_reg(s, ots, oreg);
    ots->mem_coherent = 0;

    if (NEED_SYNC_ARG(0)) {
        temp_sync(s, ots, allocated_regs, 0, 0);
    }
}

/*
 * Specialized code generation for INDEX_op_dup_vec.
 */
static void tcg_reg_alloc_dup(TCGContext *s, const TCGOp *op)
{
    const TCGLifeData arg_life = op->life;
    TCGRegSet dup_out_regs, dup_in_regs;
    TCGTemp *its, *ots;
    TCGType itype, vtype;
    unsigned vece;
    int lowpart_ofs;
    bool ok;

    ots = arg_temp(op->args[0]);
    its = arg_temp(op->args[1]);

    /* ENV should not be modified.  */
    tcg_debug_assert(!temp_readonly(ots));

    itype = its->type;
    vece = TCGOP_VECE(op);
    vtype = TCGOP_VECL(op) + TCG_TYPE_V64;

    if (its->val_type == TEMP_VAL_CONST) {
        /* Propagate constant via movi -> dupi.  */
        tcg_target_ulong val = its->val;
        if (IS_DEAD_ARG(1)) {
            temp_dead(s, its);
        }
        tcg_reg_alloc_do_movi(s, ots, val, arg_life, output_pref(op, 0));
        return;
    }

    dup_out_regs = tcg_op_defs[INDEX_op_dup_vec].args_ct[0].regs;
    dup_in_regs = tcg_op_defs[INDEX_op_dup_vec].args_ct[1].regs;

    /* Allocate the output register now.  */
    if (ots->val_type != TEMP_VAL_REG) {
        TCGRegSet allocated_regs = s->reserved_regs;
        TCGReg oreg;

        if (!IS_DEAD_ARG(1) && its->val_type == TEMP_VAL_REG) {
            /* Make sure to not spill the input register. */
            tcg_regset_set_reg(allocated_regs, its->reg);
        }
        oreg = tcg_reg_alloc(s, dup_out_regs, allocated_regs,
                             output_pref(op, 0), ots->indirect_base);
        set_temp_val_reg(s, ots, oreg);
    }

    switch (its->val_type) {
    case TEMP_VAL_REG:
        /*
         * The dup constriaints must be broad, covering all possible VECE.
         * However, tcg_op_dup_vec() gets to see the VECE and we allow it
         * to fail, indicating that extra moves are required for that case.
         */
        if (tcg_regset_test_reg(dup_in_regs, its->reg)) {
            if (tcg_out_dup_vec(s, vtype, vece, ots->reg, its->reg)) {
                goto done;
            }
            /* Try again from memory or a vector input register.  */
        }
        if (!its->mem_coherent) {
            /*
             * The input register is not synced, and so an extra store
             * would be required to use memory.  Attempt an integer-vector
             * register move first.  We do not have a TCGRegSet for this.
             */
            if (tcg_out_mov(s, itype, ots->reg, its->reg)) {
                break;
            }
            /* Sync the temp back to its slot and load from there.  */
            temp_sync(s, its, s->reserved_regs, 0, 0);
        }
        /* fall through */

    case TEMP_VAL_MEM:
        lowpart_ofs = 0;
        if (HOST_BIG_ENDIAN) {
            lowpart_ofs = tcg_type_size(itype) - (1 << vece);
        }
        if (tcg_out_dupm_vec(s, vtype, vece, ots->reg, its->mem_base->reg,
                             its->mem_offset + lowpart_ofs)) {
            goto done;
        }
        /* Load the input into the destination vector register. */
        tcg_out_ld(s, itype, ots->reg, its->mem_base->reg, its->mem_offset);
        break;

    default:
        g_assert_not_reached();
    }

    /* We now have a vector input register, so dup must succeed. */
    ok = tcg_out_dup_vec(s, vtype, vece, ots->reg, ots->reg);
    tcg_debug_assert(ok);

 done:
    ots->mem_coherent = 0;
    if (IS_DEAD_ARG(1)) {
        temp_dead(s, its);
    }
    if (NEED_SYNC_ARG(0)) {
        temp_sync(s, ots, s->reserved_regs, 0, 0);
    }
    if (IS_DEAD_ARG(0)) {
        temp_dead(s, ots);
    }
}

static void tcg_reg_alloc_op(TCGContext *s, const TCGOp *op)
{
    const TCGLifeData arg_life = op->life;
    const TCGOpDef * const def = &tcg_op_defs[op->opc];
    TCGRegSet i_allocated_regs;
    TCGRegSet o_allocated_regs;
    int i, k, nb_iargs, nb_oargs;
    TCGReg reg;
    TCGArg arg;
    const TCGArgConstraint *arg_ct;
    TCGTemp *ts;
    TCGArg new_args[TCG_MAX_OP_ARGS];
    int const_args[TCG_MAX_OP_ARGS];

    nb_oargs = def->nb_oargs;
    nb_iargs = def->nb_iargs;

    /* copy constants */
    memcpy(new_args + nb_oargs + nb_iargs,
           op->args + nb_oargs + nb_iargs,
           sizeof(TCGArg) * def->nb_cargs);

    i_allocated_regs = s->reserved_regs;
    o_allocated_regs = s->reserved_regs;

    /* satisfy input constraints */
    for (k = 0; k < nb_iargs; k++) {
        TCGRegSet i_preferred_regs, i_required_regs;
        bool allocate_new_reg, copyto_new_reg;
        TCGTemp *ts2;
        int i1, i2;

        i = def->args_ct[nb_oargs + k].sort_index;
        arg = op->args[i];
        arg_ct = &def->args_ct[i];
        ts = arg_temp(arg);

        if (ts->val_type == TEMP_VAL_CONST
            && tcg_target_const_match(ts->val, ts->type, arg_ct->ct)) {
            /* constant is OK for instruction */
            const_args[i] = 1;
            new_args[i] = ts->val;
            continue;
        }

        reg = ts->reg;
        i_preferred_regs = 0;
        i_required_regs = arg_ct->regs;
        allocate_new_reg = false;
        copyto_new_reg = false;

        switch (arg_ct->pair) {
        case 0: /* not paired */
            if (arg_ct->ialias) {
                i_preferred_regs = output_pref(op, arg_ct->alias_index);

                /*
                 * If the input is readonly, then it cannot also be an
                 * output and aliased to itself.  If the input is not
                 * dead after the instruction, we must allocate a new
                 * register and move it.
                 */
                if (temp_readonly(ts) || !IS_DEAD_ARG(i)) {
                    allocate_new_reg = true;
                } else if (ts->val_type == TEMP_VAL_REG) {
                    /*
                     * Check if the current register has already been
                     * allocated for another input.
                     */
                    allocate_new_reg =
                        tcg_regset_test_reg(i_allocated_regs, reg);
                }
            }
            if (!allocate_new_reg) {
                temp_load(s, ts, i_required_regs, i_allocated_regs,
                          i_preferred_regs);
                reg = ts->reg;
                allocate_new_reg = !tcg_regset_test_reg(i_required_regs, reg);
            }
            if (allocate_new_reg) {
                /*
                 * Allocate a new register matching the constraint
                 * and move the temporary register into it.
                 */
                temp_load(s, ts, tcg_target_available_regs[ts->type],
                          i_allocated_regs, 0);
                reg = tcg_reg_alloc(s, i_required_regs, i_allocated_regs,
                                    i_preferred_regs, ts->indirect_base);
                copyto_new_reg = true;
            }
            break;

        case 1:
            /* First of an input pair; if i1 == i2, the second is an output. */
            i1 = i;
            i2 = arg_ct->pair_index;
            ts2 = i1 != i2 ? arg_temp(op->args[i2]) : NULL;

            /*
             * It is easier to default to allocating a new pair
             * and to identify a few cases where it's not required.
             */
            if (arg_ct->ialias) {
                i_preferred_regs = output_pref(op, arg_ct->alias_index);
                if (IS_DEAD_ARG(i1) &&
                    IS_DEAD_ARG(i2) &&
                    !temp_readonly(ts) &&
                    ts->val_type == TEMP_VAL_REG &&
                    ts->reg < TCG_TARGET_NB_REGS - 1 &&
                    tcg_regset_test_reg(i_required_regs, reg) &&
                    !tcg_regset_test_reg(i_allocated_regs, reg) &&
                    !tcg_regset_test_reg(i_allocated_regs, reg + 1) &&
                    (ts2
                     ? ts2->val_type == TEMP_VAL_REG &&
                       ts2->reg == reg + 1 &&
                       !temp_readonly(ts2)
                     : s->reg_to_temp[reg + 1] == NULL)) {
                    break;
                }
            } else {
                /* Without aliasing, the pair must also be an input. */
                tcg_debug_assert(ts2);
                if (ts->val_type == TEMP_VAL_REG &&
                    ts2->val_type == TEMP_VAL_REG &&
                    ts2->reg == reg + 1 &&
                    tcg_regset_test_reg(i_required_regs, reg)) {
                    break;
                }
            }
            reg = tcg_reg_alloc_pair(s, i_required_regs, i_allocated_regs,
                                     0, ts->indirect_base);
            goto do_pair;

        case 2: /* pair second */
            reg = new_args[arg_ct->pair_index] + 1;
            goto do_pair;

        case 3: /* ialias with second output, no first input */
            tcg_debug_assert(arg_ct->ialias);
            i_preferred_regs = output_pref(op, arg_ct->alias_index);

            if (IS_DEAD_ARG(i) &&
                !temp_readonly(ts) &&
                ts->val_type == TEMP_VAL_REG &&
                reg > 0 &&
                s->reg_to_temp[reg - 1] == NULL &&
                tcg_regset_test_reg(i_required_regs, reg) &&
                !tcg_regset_test_reg(i_allocated_regs, reg) &&
                !tcg_regset_test_reg(i_allocated_regs, reg - 1)) {
                tcg_regset_set_reg(i_allocated_regs, reg - 1);
                break;
            }
            reg = tcg_reg_alloc_pair(s, i_required_regs >> 1,
                                     i_allocated_regs, 0,
                                     ts->indirect_base);
            tcg_regset_set_reg(i_allocated_regs, reg);
            reg += 1;
            goto do_pair;

        do_pair:
            /*
             * If an aliased input is not dead after the instruction,
             * we must allocate a new register and move it.
             */
            if (arg_ct->ialias && (!IS_DEAD_ARG(i) || temp_readonly(ts))) {
                TCGRegSet t_allocated_regs = i_allocated_regs;

                /*
                 * Because of the alias, and the continued life, make sure
                 * that the temp is somewhere *other* than the reg pair,
                 * and we get a copy in reg.
                 */
                tcg_regset_set_reg(t_allocated_regs, reg);
                tcg_regset_set_reg(t_allocated_regs, reg + 1);
                if (ts->val_type == TEMP_VAL_REG && ts->reg == reg) {
                    /* If ts was already in reg, copy it somewhere else. */
                    TCGReg nr;
                    bool ok;

                    tcg_debug_assert(ts->kind != TEMP_FIXED);
                    nr = tcg_reg_alloc(s, tcg_target_available_regs[ts->type],
                                       t_allocated_regs, 0, ts->indirect_base);
                    ok = tcg_out_mov(s, ts->type, nr, reg);
                    tcg_debug_assert(ok);

                    set_temp_val_reg(s, ts, nr);
                } else {
                    temp_load(s, ts, tcg_target_available_regs[ts->type],
                              t_allocated_regs, 0);
                    copyto_new_reg = true;
                }
            } else {
                /* Preferably allocate to reg, otherwise copy. */
                i_required_regs = (TCGRegSet)1 << reg;
                temp_load(s, ts, i_required_regs, i_allocated_regs,
                          i_preferred_regs);
                copyto_new_reg = ts->reg != reg;
            }
            break;

        default:
            g_assert_not_reached();
        }

        if (copyto_new_reg) {
            if (!tcg_out_mov(s, ts->type, reg, ts->reg)) {
                /*
                 * Cross register class move not supported.  Sync the
                 * temp back to its slot and load from there.
                 */
                temp_sync(s, ts, i_allocated_regs, 0, 0);
                tcg_out_ld(s, ts->type, reg,
                           ts->mem_base->reg, ts->mem_offset);
            }
        }
        new_args[i] = reg;
        const_args[i] = 0;
        tcg_regset_set_reg(i_allocated_regs, reg);
    }

    /* mark dead temporaries and free the associated registers */
    for (i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
        if (IS_DEAD_ARG(i)) {
            temp_dead(s, arg_temp(op->args[i]));
        }
    }

    if (def->flags & TCG_OPF_COND_BRANCH) {
        tcg_reg_alloc_cbranch(s, i_allocated_regs);
    } else if (def->flags & TCG_OPF_BB_END) {
        tcg_reg_alloc_bb_end(s, i_allocated_regs);
    } else {
        if (def->flags & TCG_OPF_CALL_CLOBBER) {
            /* XXX: permit generic clobber register list ? */
            for (i = 0; i < TCG_TARGET_NB_REGS; i++) {
                if (tcg_regset_test_reg(tcg_target_call_clobber_regs, i)) {
                    tcg_reg_free(s, i, i_allocated_regs);
                }
            }
        }
        if (def->flags & TCG_OPF_SIDE_EFFECTS) {
            /* sync globals if the op has side effects and might trigger
               an exception. */
            sync_globals(s, i_allocated_regs);
        }

        /* satisfy the output constraints */
        for(k = 0; k < nb_oargs; k++) {
            i = def->args_ct[k].sort_index;
            arg = op->args[i];
            arg_ct = &def->args_ct[i];
            ts = arg_temp(arg);

            /* ENV should not be modified.  */
            tcg_debug_assert(!temp_readonly(ts));

            switch (arg_ct->pair) {
            case 0: /* not paired */
                if (arg_ct->oalias && !const_args[arg_ct->alias_index]) {
                    reg = new_args[arg_ct->alias_index];
                } else if (arg_ct->newreg) {
                    reg = tcg_reg_alloc(s, arg_ct->regs,
                                        i_allocated_regs | o_allocated_regs,
                                        output_pref(op, k), ts->indirect_base);
                } else {
                    reg = tcg_reg_alloc(s, arg_ct->regs, o_allocated_regs,
                                        output_pref(op, k), ts->indirect_base);
                }
                break;

            case 1: /* first of pair */
                tcg_debug_assert(!arg_ct->newreg);
                if (arg_ct->oalias) {
                    reg = new_args[arg_ct->alias_index];
                    break;
                }
                reg = tcg_reg_alloc_pair(s, arg_ct->regs, o_allocated_regs,
                                         output_pref(op, k), ts->indirect_base);
                break;

            case 2: /* second of pair */
                tcg_debug_assert(!arg_ct->newreg);
                if (arg_ct->oalias) {
                    reg = new_args[arg_ct->alias_index];
                } else {
                    reg = new_args[arg_ct->pair_index] + 1;
                }
                break;

            case 3: /* first of pair, aliasing with a second input */
                tcg_debug_assert(!arg_ct->newreg);
                reg = new_args[arg_ct->pair_index] - 1;
                break;

            default:
                g_assert_not_reached();
            }
            tcg_regset_set_reg(o_allocated_regs, reg);
            set_temp_val_reg(s, ts, reg);
            ts->mem_coherent = 0;
            new_args[i] = reg;
        }
    }

    /* emit instruction */
    switch (op->opc) {
    case INDEX_op_ext8s_i32:
        tcg_out_ext8s(s, TCG_TYPE_I32, new_args[0], new_args[1]);
        break;
    case INDEX_op_ext8s_i64:
        tcg_out_ext8s(s, TCG_TYPE_I64, new_args[0], new_args[1]);
        break;
    case INDEX_op_ext8u_i32:
    case INDEX_op_ext8u_i64:
        tcg_out_ext8u(s, new_args[0], new_args[1]);
        break;
    case INDEX_op_ext16s_i32:
        tcg_out_ext16s(s, TCG_TYPE_I32, new_args[0], new_args[1]);
        break;
    case INDEX_op_ext16s_i64:
        tcg_out_ext16s(s, TCG_TYPE_I64, new_args[0], new_args[1]);
        break;
    case INDEX_op_ext16u_i32:
    case INDEX_op_ext16u_i64:
        tcg_out_ext16u(s, new_args[0], new_args[1]);
        break;
    case INDEX_op_ext32s_i64:
        tcg_out_ext32s(s, new_args[0], new_args[1]);
        break;
    case INDEX_op_ext32u_i64:
        tcg_out_ext32u(s, new_args[0], new_args[1]);
        break;
    case INDEX_op_ext_i32_i64:
        tcg_out_exts_i32_i64(s, new_args[0], new_args[1]);
        break;
    case INDEX_op_extu_i32_i64:
        tcg_out_extu_i32_i64(s, new_args[0], new_args[1]);
        break;
    case INDEX_op_extrl_i64_i32:
        tcg_out_extrl_i64_i32(s, new_args[0], new_args[1]);
        break;
    default:
        if (def->flags & TCG_OPF_VECTOR) {
            tcg_out_vec_op(s, op->opc, TCGOP_VECL(op), TCGOP_VECE(op),
                           new_args, const_args);
        } else {
            tcg_out_op(s, op->opc, new_args, const_args);
        }
        break;
    }

    /* move the outputs in the correct register if needed */
    for(i = 0; i < nb_oargs; i++) {
        ts = arg_temp(op->args[i]);

        /* ENV should not be modified.  */
        tcg_debug_assert(!temp_readonly(ts));

        if (NEED_SYNC_ARG(i)) {
            temp_sync(s, ts, o_allocated_regs, 0, IS_DEAD_ARG(i));
        } else if (IS_DEAD_ARG(i)) {
            temp_dead(s, ts);
        }
    }
}

static bool tcg_reg_alloc_dup2(TCGContext *s, const TCGOp *op)
{
    const TCGLifeData arg_life = op->life;
    TCGTemp *ots, *itsl, *itsh;
    TCGType vtype = TCGOP_VECL(op) + TCG_TYPE_V64;

    /* This opcode is only valid for 32-bit hosts, for 64-bit elements. */
    tcg_debug_assert(TCG_TARGET_REG_BITS == 32);
    tcg_debug_assert(TCGOP_VECE(op) == MO_64);

    ots = arg_temp(op->args[0]);
    itsl = arg_temp(op->args[1]);
    itsh = arg_temp(op->args[2]);

    /* ENV should not be modified.  */
    tcg_debug_assert(!temp_readonly(ots));

    /* Allocate the output register now.  */
    if (ots->val_type != TEMP_VAL_REG) {
        TCGRegSet allocated_regs = s->reserved_regs;
        TCGRegSet dup_out_regs =
            tcg_op_defs[INDEX_op_dup_vec].args_ct[0].regs;
        TCGReg oreg;

        /* Make sure to not spill the input registers. */
        if (!IS_DEAD_ARG(1) && itsl->val_type == TEMP_VAL_REG) {
            tcg_regset_set_reg(allocated_regs, itsl->reg);
        }
        if (!IS_DEAD_ARG(2) && itsh->val_type == TEMP_VAL_REG) {
            tcg_regset_set_reg(allocated_regs, itsh->reg);
        }

        oreg = tcg_reg_alloc(s, dup_out_regs, allocated_regs,
                             output_pref(op, 0), ots->indirect_base);
        set_temp_val_reg(s, ots, oreg);
    }

    /* Promote dup2 of immediates to dupi_vec. */
    if (itsl->val_type == TEMP_VAL_CONST && itsh->val_type == TEMP_VAL_CONST) {
        uint64_t val = deposit64(itsl->val, 32, 32, itsh->val);
        MemOp vece = MO_64;

        if (val == dup_const(MO_8, val)) {
            vece = MO_8;
        } else if (val == dup_const(MO_16, val)) {
            vece = MO_16;
        } else if (val == dup_const(MO_32, val)) {
            vece = MO_32;
        }

        tcg_out_dupi_vec(s, vtype, vece, ots->reg, val);
        goto done;
    }

    /* If the two inputs form one 64-bit value, try dupm_vec. */
    if (itsl->temp_subindex == HOST_BIG_ENDIAN &&
        itsh->temp_subindex == !HOST_BIG_ENDIAN &&
        itsl == itsh + (HOST_BIG_ENDIAN ? 1 : -1)) {
        TCGTemp *its = itsl - HOST_BIG_ENDIAN;

        temp_sync(s, its + 0, s->reserved_regs, 0, 0);
        temp_sync(s, its + 1, s->reserved_regs, 0, 0);

        if (tcg_out_dupm_vec(s, vtype, MO_64, ots->reg,
                             its->mem_base->reg, its->mem_offset)) {
            goto done;
        }
    }

    /* Fall back to generic expansion. */
    return false;

 done:
    ots->mem_coherent = 0;
    if (IS_DEAD_ARG(1)) {
        temp_dead(s, itsl);
    }
    if (IS_DEAD_ARG(2)) {
        temp_dead(s, itsh);
    }
    if (NEED_SYNC_ARG(0)) {
        temp_sync(s, ots, s->reserved_regs, 0, IS_DEAD_ARG(0));
    } else if (IS_DEAD_ARG(0)) {
        temp_dead(s, ots);
    }
    return true;
}

static void load_arg_reg(TCGContext *s, TCGReg reg, TCGTemp *ts,
                         TCGRegSet allocated_regs)
{
    if (ts->val_type == TEMP_VAL_REG) {
        if (ts->reg != reg) {
            tcg_reg_free(s, reg, allocated_regs);
            if (!tcg_out_mov(s, ts->type, reg, ts->reg)) {
                /*
                 * Cross register class move not supported.  Sync the
                 * temp back to its slot and load from there.
                 */
                temp_sync(s, ts, allocated_regs, 0, 0);
                tcg_out_ld(s, ts->type, reg,
                           ts->mem_base->reg, ts->mem_offset);
            }
        }
    } else {
        TCGRegSet arg_set = 0;

        tcg_reg_free(s, reg, allocated_regs);
        tcg_regset_set_reg(arg_set, reg);
        temp_load(s, ts, arg_set, allocated_regs, 0);
    }
}

static void load_arg_stk(TCGContext *s, unsigned arg_slot, TCGTemp *ts,
                         TCGRegSet allocated_regs)
{
    /*
     * When the destination is on the stack, load up the temp and store.
     * If there are many call-saved registers, the temp might live to
     * see another use; otherwise it'll be discarded.
     */
    temp_load(s, ts, tcg_target_available_regs[ts->type], allocated_regs, 0);
    tcg_out_st(s, ts->type, ts->reg, TCG_REG_CALL_STACK,
               arg_slot_stk_ofs(arg_slot));
}

static void load_arg_normal(TCGContext *s, const TCGCallArgumentLoc *l,
                            TCGTemp *ts, TCGRegSet *allocated_regs)
{
    if (arg_slot_reg_p(l->arg_slot)) {
        TCGReg reg = tcg_target_call_iarg_regs[l->arg_slot];
        load_arg_reg(s, reg, ts, *allocated_regs);
        tcg_regset_set_reg(*allocated_regs, reg);
    } else {
        load_arg_stk(s, l->arg_slot, ts, *allocated_regs);
    }
}

static void load_arg_ref(TCGContext *s, unsigned arg_slot, TCGReg ref_base,
                         intptr_t ref_off, TCGRegSet *allocated_regs)
{
    TCGReg reg;

    if (arg_slot_reg_p(arg_slot)) {
        reg = tcg_target_call_iarg_regs[arg_slot];
        tcg_reg_free(s, reg, *allocated_regs);
        tcg_out_addi_ptr(s, reg, ref_base, ref_off);
        tcg_regset_set_reg(*allocated_regs, reg);
    } else {
        reg = tcg_reg_alloc(s, tcg_target_available_regs[TCG_TYPE_PTR],
                            *allocated_regs, 0, false);
        tcg_out_addi_ptr(s, reg, ref_base, ref_off);
        tcg_out_st(s, TCG_TYPE_PTR, reg, TCG_REG_CALL_STACK,
                   arg_slot_stk_ofs(arg_slot));
    }
}

static void tcg_reg_alloc_call(TCGContext *s, TCGOp *op)
{
    const int nb_oargs = TCGOP_CALLO(op);
    const int nb_iargs = TCGOP_CALLI(op);
    const TCGLifeData arg_life = op->life;
    const TCGHelperInfo *info = tcg_call_info(op);
    TCGRegSet allocated_regs = s->reserved_regs;
    int i;

    /*
     * Move inputs into place in reverse order,
     * so that we place stacked arguments first.
     */
    for (i = nb_iargs - 1; i >= 0; --i) {
        const TCGCallArgumentLoc *loc = &info->in[i];
        TCGTemp *ts = arg_temp(op->args[nb_oargs + i]);

        switch (loc->kind) {
        case TCG_CALL_ARG_NORMAL:
        case TCG_CALL_ARG_EXTEND_U:
        case TCG_CALL_ARG_EXTEND_S:
            load_arg_normal(s, loc, ts, &allocated_regs);
            break;
        case TCG_CALL_ARG_BY_REF:
            load_arg_stk(s, loc->ref_slot, ts, allocated_regs);
            load_arg_ref(s, loc->arg_slot, TCG_REG_CALL_STACK,
                         arg_slot_stk_ofs(loc->ref_slot),
                         &allocated_regs);
            break;
        case TCG_CALL_ARG_BY_REF_N:
            load_arg_stk(s, loc->ref_slot, ts, allocated_regs);
            break;
        default:
            g_assert_not_reached();
        }
    }

    /* Mark dead temporaries and free the associated registers.  */
    for (i = nb_oargs; i < nb_iargs + nb_oargs; i++) {
        if (IS_DEAD_ARG(i)) {
            temp_dead(s, arg_temp(op->args[i]));
        }
    }

    /* Clobber call registers.  */
    for (i = 0; i < TCG_TARGET_NB_REGS; i++) {
        if (tcg_regset_test_reg(tcg_target_call_clobber_regs, i)) {
            tcg_reg_free(s, i, allocated_regs);
        }
    }

    /*
     * Save globals if they might be written by the helper,
     * sync them if they might be read.
     */
    if (info->flags & TCG_CALL_NO_READ_GLOBALS) {
        /* Nothing to do */
    } else if (info->flags & TCG_CALL_NO_WRITE_GLOBALS) {
        sync_globals(s, allocated_regs);
    } else {
        save_globals(s, allocated_regs);
    }

    /*
     * If the ABI passes a pointer to the returned struct as the first
     * argument, load that now.  Pass a pointer to the output home slot.
     */
    if (info->out_kind == TCG_CALL_RET_BY_REF) {
        TCGTemp *ts = arg_temp(op->args[0]);

        if (!ts->mem_allocated) {
            temp_allocate_frame(s, ts);
        }
        load_arg_ref(s, 0, ts->mem_base->reg, ts->mem_offset, &allocated_regs);
    }

    tcg_out_call(s, tcg_call_func(op), info);

    /* Assign output registers and emit moves if needed.  */
    switch (info->out_kind) {
    case TCG_CALL_RET_NORMAL:
        for (i = 0; i < nb_oargs; i++) {
            TCGTemp *ts = arg_temp(op->args[i]);
            TCGReg reg = tcg_target_call_oarg_reg(TCG_CALL_RET_NORMAL, i);

            /* ENV should not be modified.  */
            tcg_debug_assert(!temp_readonly(ts));

            set_temp_val_reg(s, ts, reg);
            ts->mem_coherent = 0;
        }
        break;

    case TCG_CALL_RET_BY_VEC:
        {
            TCGTemp *ts = arg_temp(op->args[0]);

            tcg_debug_assert(ts->base_type == TCG_TYPE_I128);
            tcg_debug_assert(ts->temp_subindex == 0);
            if (!ts->mem_allocated) {
                temp_allocate_frame(s, ts);
            }
            tcg_out_st(s, TCG_TYPE_V128,
                       tcg_target_call_oarg_reg(TCG_CALL_RET_BY_VEC, 0),
                       ts->mem_base->reg, ts->mem_offset);
        }
        /* fall through to mark all parts in memory */

    case TCG_CALL_RET_BY_REF:
        /* The callee has performed a write through the reference. */
        for (i = 0; i < nb_oargs; i++) {
            TCGTemp *ts = arg_temp(op->args[i]);
            ts->val_type = TEMP_VAL_MEM;
        }
        break;

    default:
        g_assert_not_reached();
    }

    /* Flush or discard output registers as needed. */
    for (i = 0; i < nb_oargs; i++) {
        TCGTemp *ts = arg_temp(op->args[i]);
        if (NEED_SYNC_ARG(i)) {
            temp_sync(s, ts, s->reserved_regs, 0, IS_DEAD_ARG(i));
        } else if (IS_DEAD_ARG(i)) {
            temp_dead(s, ts);
        }
    }
}

/*
 * Similarly for qemu_ld/st slow path helpers.
 * We must re-implement tcg_gen_callN and tcg_reg_alloc_call simultaneously,
 * using only the provided backend tcg_out_* functions.
 */

static int tcg_out_helper_stk_ofs(TCGType type, unsigned slot)
{
    int ofs = arg_slot_stk_ofs(slot);

    /*
     * Each stack slot is TCG_TARGET_LONG_BITS.  If the host does not
     * require extension to uint64_t, adjust the address for uint32_t.
     */
    if (HOST_BIG_ENDIAN &&
        TCG_TARGET_REG_BITS == 64 &&
        type == TCG_TYPE_I32) {
        ofs += 4;
    }
    return ofs;
}

static void tcg_out_helper_load_slots(TCGContext *s,
                                      unsigned nmov, TCGMovExtend *mov,
                                      const TCGLdstHelperParam *parm)
{
    unsigned i;
    TCGReg dst3;

    /*
     * Start from the end, storing to the stack first.
     * This frees those registers, so we need not consider overlap.
     */
    for (i = nmov; i-- > 0; ) {
        unsigned slot = mov[i].dst;

        if (arg_slot_reg_p(slot)) {
            goto found_reg;
        }

        TCGReg src = mov[i].src;
        TCGType dst_type = mov[i].dst_type;
        MemOp dst_mo = dst_type == TCG_TYPE_I32 ? MO_32 : MO_64;

        /* The argument is going onto the stack; extend into scratch. */
        if ((mov[i].src_ext & MO_SIZE) != dst_mo) {
            tcg_debug_assert(parm->ntmp != 0);
            mov[i].dst = src = parm->tmp[0];
            tcg_out_movext1(s, &mov[i]);
        }

        tcg_out_st(s, dst_type, src, TCG_REG_CALL_STACK,
                   tcg_out_helper_stk_ofs(dst_type, slot));
    }
    return;

 found_reg:
    /*
     * The remaining arguments are in registers.
     * Convert slot numbers to argument registers.
     */
    nmov = i + 1;
    for (i = 0; i < nmov; ++i) {
        mov[i].dst = tcg_target_call_iarg_regs[mov[i].dst];
    }

    switch (nmov) {
    case 4:
        /* The backend must have provided enough temps for the worst case. */
        tcg_debug_assert(parm->ntmp >= 2);

        dst3 = mov[3].dst;
        for (unsigned j = 0; j < 3; ++j) {
            if (dst3 == mov[j].src) {
                /*
                 * Conflict. Copy the source to a temporary, perform the
                 * remaining moves, then the extension from our scratch
                 * on the way out.
                 */
                TCGReg scratch = parm->tmp[1];

                tcg_out_mov(s, mov[3].src_type, scratch, mov[3].src);
                tcg_out_movext3(s, mov, mov + 1, mov + 2, parm->tmp[0]);
                tcg_out_movext1_new_src(s, &mov[3], scratch);
                break;
            }
        }

        /* No conflicts: perform this move and continue. */
        tcg_out_movext1(s, &mov[3]);
        /* fall through */

    case 3:
        tcg_out_movext3(s, mov, mov + 1, mov + 2,
                        parm->ntmp ? parm->tmp[0] : -1);
        break;
    case 2:
        tcg_out_movext2(s, mov, mov + 1,
                        parm->ntmp ? parm->tmp[0] : -1);
        break;
    case 1:
        tcg_out_movext1(s, mov);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_helper_load_imm(TCGContext *s, unsigned slot,
                                    TCGType type, tcg_target_long imm,
                                    const TCGLdstHelperParam *parm)
{
    if (arg_slot_reg_p(slot)) {
        tcg_out_movi(s, type, tcg_target_call_iarg_regs[slot], imm);
    } else {
        int ofs = tcg_out_helper_stk_ofs(type, slot);
        if (!tcg_out_sti(s, type, imm, TCG_REG_CALL_STACK, ofs)) {
            tcg_debug_assert(parm->ntmp != 0);
            tcg_out_movi(s, type, parm->tmp[0], imm);
            tcg_out_st(s, type, parm->tmp[0], TCG_REG_CALL_STACK, ofs);
        }
    }
}

static void tcg_out_helper_load_common_args(TCGContext *s,
                                            const TCGLabelQemuLdst *ldst,
                                            const TCGLdstHelperParam *parm,
                                            const TCGHelperInfo *info,
                                            unsigned next_arg)
{
    TCGMovExtend ptr_mov = {
        .dst_type = TCG_TYPE_PTR,
        .src_type = TCG_TYPE_PTR,
        .src_ext = sizeof(void *) == 4 ? MO_32 : MO_64
    };
    const TCGCallArgumentLoc *loc = &info->in[0];
    TCGType type;
    unsigned slot;
    tcg_target_ulong imm;

    /*
     * Handle env, which is always first.
     */
    ptr_mov.dst = loc->arg_slot;
    ptr_mov.src = TCG_AREG0;
    tcg_out_helper_load_slots(s, 1, &ptr_mov, parm);

    /*
     * Handle oi.
     */
    imm = ldst->oi;
    loc = &info->in[next_arg];
    type = TCG_TYPE_I32;
    switch (loc->kind) {
    case TCG_CALL_ARG_NORMAL:
        break;
    case TCG_CALL_ARG_EXTEND_U:
    case TCG_CALL_ARG_EXTEND_S:
        /* No extension required for MemOpIdx. */
        tcg_debug_assert(imm <= INT32_MAX);
        type = TCG_TYPE_REG;
        break;
    default:
        g_assert_not_reached();
    }
    tcg_out_helper_load_imm(s, loc->arg_slot, type, imm, parm);
    next_arg++;

    /*
     * Handle ra.
     */
    loc = &info->in[next_arg];
    slot = loc->arg_slot;
    if (parm->ra_gen) {
        int arg_reg = -1;
        TCGReg ra_reg;

        if (arg_slot_reg_p(slot)) {
            arg_reg = tcg_target_call_iarg_regs[slot];
        }
        ra_reg = parm->ra_gen(s, ldst, arg_reg);

        ptr_mov.dst = slot;
        ptr_mov.src = ra_reg;
        tcg_out_helper_load_slots(s, 1, &ptr_mov, parm);
    } else {
        imm = (uintptr_t)ldst->raddr;
        tcg_out_helper_load_imm(s, slot, TCG_TYPE_PTR, imm, parm);
    }
}

static unsigned tcg_out_helper_add_mov(TCGMovExtend *mov,
                                       const TCGCallArgumentLoc *loc,
                                       TCGType dst_type, TCGType src_type,
                                       TCGReg lo, TCGReg hi)
{
    if (dst_type <= TCG_TYPE_REG) {
        MemOp src_ext;

        switch (loc->kind) {
        case TCG_CALL_ARG_NORMAL:
            src_ext = src_type == TCG_TYPE_I32 ? MO_32 : MO_64;
            break;
        case TCG_CALL_ARG_EXTEND_U:
            dst_type = TCG_TYPE_REG;
            src_ext = MO_UL;
            break;
        case TCG_CALL_ARG_EXTEND_S:
            dst_type = TCG_TYPE_REG;
            src_ext = MO_SL;
            break;
        default:
            g_assert_not_reached();
        }

        mov[0].dst = loc->arg_slot;
        mov[0].dst_type = dst_type;
        mov[0].src = lo;
        mov[0].src_type = src_type;
        mov[0].src_ext = src_ext;
        return 1;
    }

    assert(TCG_TARGET_REG_BITS == 32);

    mov[0].dst = loc[HOST_BIG_ENDIAN].arg_slot;
    mov[0].src = lo;
    mov[0].dst_type = TCG_TYPE_I32;
    mov[0].src_type = TCG_TYPE_I32;
    mov[0].src_ext = MO_32;

    mov[1].dst = loc[!HOST_BIG_ENDIAN].arg_slot;
    mov[1].src = hi;
    mov[1].dst_type = TCG_TYPE_I32;
    mov[1].src_type = TCG_TYPE_I32;
    mov[1].src_ext = MO_32;

    return 2;
}

static void tcg_out_ld_helper_args(TCGContext *s, const TCGLabelQemuLdst *ldst,
                                   const TCGLdstHelperParam *parm)
{
    const TCGHelperInfo *info;
    const TCGCallArgumentLoc *loc;
    TCGMovExtend mov[2];
    unsigned next_arg, nmov;
    MemOp mop = get_memop(ldst->oi);

    switch (mop & MO_SIZE) {
    case MO_8:
    case MO_16:
    case MO_32:
        info = &info_helper_ld32_mmu;
        break;
    case MO_64:
        info = &info_helper_ld64_mmu;
        break;
    default:
        g_assert_not_reached();
    }

    /* Defer env argument. */
    next_arg = 1;

    loc = &info->in[next_arg];
    nmov = tcg_out_helper_add_mov(mov, loc, TCG_TYPE_TL, TCG_TYPE_TL,
                                  ldst->addrlo_reg, ldst->addrhi_reg);
    next_arg += nmov;

    tcg_out_helper_load_slots(s, nmov, mov, parm);

    /* No special attention for 32 and 64-bit return values. */
    tcg_debug_assert(info->out_kind == TCG_CALL_RET_NORMAL);

    tcg_out_helper_load_common_args(s, ldst, parm, info, next_arg);
}

static void tcg_out_ld_helper_ret(TCGContext *s, const TCGLabelQemuLdst *ldst,
                                  bool load_sign,
                                  const TCGLdstHelperParam *parm)
{
    TCGMovExtend mov[2];

    if (ldst->type <= TCG_TYPE_REG) {
        MemOp mop = get_memop(ldst->oi);

        mov[0].dst = ldst->datalo_reg;
        mov[0].src = tcg_target_call_oarg_reg(TCG_CALL_RET_NORMAL, 0);
        mov[0].dst_type = ldst->type;
        mov[0].src_type = TCG_TYPE_REG;

        /*
         * If load_sign, then we allowed the helper to perform the
         * appropriate sign extension to tcg_target_ulong, and all
         * we need now is a plain move.
         *
         * If they do not, then we expect the relevant extension
         * instruction to be no more expensive than a move, and
         * we thus save the icache etc by only using one of two
         * helper functions.
         */
        if (load_sign || !(mop & MO_SIGN)) {
            if (TCG_TARGET_REG_BITS == 32 || ldst->type == TCG_TYPE_I32) {
                mov[0].src_ext = MO_32;
            } else {
                mov[0].src_ext = MO_64;
            }
        } else {
            mov[0].src_ext = mop & MO_SSIZE;
        }
        tcg_out_movext1(s, mov);
    } else {
        assert(TCG_TARGET_REG_BITS == 32);

        mov[0].dst = ldst->datalo_reg;
        mov[0].src =
            tcg_target_call_oarg_reg(TCG_CALL_RET_NORMAL, HOST_BIG_ENDIAN);
        mov[0].dst_type = TCG_TYPE_I32;
        mov[0].src_type = TCG_TYPE_I32;
        mov[0].src_ext = MO_32;

        mov[1].dst = ldst->datahi_reg;
        mov[1].src =
            tcg_target_call_oarg_reg(TCG_CALL_RET_NORMAL, !HOST_BIG_ENDIAN);
        mov[1].dst_type = TCG_TYPE_REG;
        mov[1].src_type = TCG_TYPE_REG;
        mov[1].src_ext = MO_32;

        tcg_out_movext2(s, mov, mov + 1, parm->ntmp ? parm->tmp[0] : -1);
    }
}

static void tcg_out_st_helper_args(TCGContext *s, const TCGLabelQemuLdst *ldst,
                                   const TCGLdstHelperParam *parm)
{
    const TCGHelperInfo *info;
    const TCGCallArgumentLoc *loc;
    TCGMovExtend mov[4];
    TCGType data_type;
    unsigned next_arg, nmov, n;
    MemOp mop = get_memop(ldst->oi);

    switch (mop & MO_SIZE) {
    case MO_8:
    case MO_16:
    case MO_32:
        info = &info_helper_st32_mmu;
        data_type = TCG_TYPE_I32;
        break;
    case MO_64:
        info = &info_helper_st64_mmu;
        data_type = TCG_TYPE_I64;
        break;
    default:
        g_assert_not_reached();
    }

    /* Defer env argument. */
    next_arg = 1;
    nmov = 0;

    /* Handle addr argument. */
    loc = &info->in[next_arg];
    n = tcg_out_helper_add_mov(mov, loc, TCG_TYPE_TL, TCG_TYPE_TL,
                               ldst->addrlo_reg, ldst->addrhi_reg);
    next_arg += n;
    nmov += n;

    /* Handle data argument. */
    loc = &info->in[next_arg];
    n = tcg_out_helper_add_mov(mov + nmov, loc, data_type, ldst->type,
                               ldst->datalo_reg, ldst->datahi_reg);
    next_arg += n;
    nmov += n;
    tcg_debug_assert(nmov <= ARRAY_SIZE(mov));

    tcg_out_helper_load_slots(s, nmov, mov, parm);
    tcg_out_helper_load_common_args(s, ldst, parm, info, next_arg);
}

#ifdef CONFIG_PROFILER

/* avoid copy/paste errors */
#define PROF_ADD(to, from, field)                       \
    do {                                                \
        (to)->field += qatomic_read(&((from)->field));  \
    } while (0)

#define PROF_MAX(to, from, field)                                       \
    do {                                                                \
        typeof((from)->field) val__ = qatomic_read(&((from)->field));   \
        if (val__ > (to)->field) {                                      \
            (to)->field = val__;                                        \
        }                                                               \
    } while (0)

/* Pass in a zero'ed @prof */
static inline
void tcg_profile_snapshot(TCGProfile *prof, bool counters, bool table)
{
    unsigned int n_ctxs = qatomic_read(&tcg_cur_ctxs);
    unsigned int i;

    for (i = 0; i < n_ctxs; i++) {
        TCGContext *s = qatomic_read(&tcg_ctxs[i]);
        const TCGProfile *orig = &s->prof;

        if (counters) {
            PROF_ADD(prof, orig, cpu_exec_time);
            PROF_ADD(prof, orig, tb_count1);
            PROF_ADD(prof, orig, tb_count);
            PROF_ADD(prof, orig, op_count);
            PROF_MAX(prof, orig, op_count_max);
            PROF_ADD(prof, orig, temp_count);
            PROF_MAX(prof, orig, temp_count_max);
            PROF_ADD(prof, orig, del_op_count);
            PROF_ADD(prof, orig, code_in_len);
            PROF_ADD(prof, orig, code_out_len);
            PROF_ADD(prof, orig, search_out_len);
            PROF_ADD(prof, orig, interm_time);
            PROF_ADD(prof, orig, code_time);
            PROF_ADD(prof, orig, la_time);
            PROF_ADD(prof, orig, opt_time);
            PROF_ADD(prof, orig, restore_count);
            PROF_ADD(prof, orig, restore_time);
        }
        if (table) {
            int i;

            for (i = 0; i < NB_OPS; i++) {
                PROF_ADD(prof, orig, table_op_count[i]);
            }
        }
    }
}

#undef PROF_ADD
#undef PROF_MAX

static void tcg_profile_snapshot_counters(TCGProfile *prof)
{
    tcg_profile_snapshot(prof, true, false);
}

static void tcg_profile_snapshot_table(TCGProfile *prof)
{
    tcg_profile_snapshot(prof, false, true);
}

void tcg_dump_op_count(GString *buf)
{
    TCGProfile prof = {};
    int i;

    tcg_profile_snapshot_table(&prof);
    for (i = 0; i < NB_OPS; i++) {
        g_string_append_printf(buf, "%s %" PRId64 "\n", tcg_op_defs[i].name,
                               prof.table_op_count[i]);
    }
}

int64_t tcg_cpu_exec_time(void)
{
    unsigned int n_ctxs = qatomic_read(&tcg_cur_ctxs);
    unsigned int i;
    int64_t ret = 0;

    for (i = 0; i < n_ctxs; i++) {
        const TCGContext *s = qatomic_read(&tcg_ctxs[i]);
        const TCGProfile *prof = &s->prof;

        ret += qatomic_read(&prof->cpu_exec_time);
    }
    return ret;
}
#else
void tcg_dump_op_count(GString *buf)
{
    g_string_append_printf(buf, "[TCG profiler not compiled]\n");
}

int64_t tcg_cpu_exec_time(void)
{
    error_report("%s: TCG profiler not compiled", __func__);
    exit(EXIT_FAILURE);
}
#endif


int tcg_gen_code(TCGContext *s, TranslationBlock *tb, target_ulong pc_start)
{
#ifdef CONFIG_PROFILER
    TCGProfile *prof = &s->prof;
#endif
    int i, num_insns;
    TCGOp *op;

#ifdef CONFIG_PROFILER
    {
        int n = 0;

        QTAILQ_FOREACH(op, &s->ops, link) {
            n++;
        }
        qatomic_set(&prof->op_count, prof->op_count + n);
        if (n > prof->op_count_max) {
            qatomic_set(&prof->op_count_max, n);
        }

        n = s->nb_temps;
        qatomic_set(&prof->temp_count, prof->temp_count + n);
        if (n > prof->temp_count_max) {
            qatomic_set(&prof->temp_count_max, n);
        }
    }
#endif

#ifdef DEBUG_DISAS
    if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP)
                 && qemu_log_in_addr_range(pc_start))) {
        FILE *logfile = qemu_log_trylock();
        if (logfile) {
            fprintf(logfile, "OP:\n");
            tcg_dump_ops(s, logfile, false);
            fprintf(logfile, "\n");
            qemu_log_unlock(logfile);
        }
    }
#endif

#ifdef CONFIG_DEBUG_TCG
    /* Ensure all labels referenced have been emitted.  */
    {
        TCGLabel *l;
        bool error = false;

        QSIMPLEQ_FOREACH(l, &s->labels, next) {
            if (unlikely(!l->present) && !QSIMPLEQ_EMPTY(&l->branches)) {
                qemu_log_mask(CPU_LOG_TB_OP,
                              "$L%d referenced but not present.\n", l->id);
                error = true;
            }
        }
        assert(!error);
    }
#endif

#ifdef CONFIG_PROFILER
    qatomic_set(&prof->opt_time, prof->opt_time - profile_getclock());
#endif

#ifdef USE_TCG_OPTIMIZATIONS
    tcg_optimize(s);
#endif

#ifdef CONFIG_PROFILER
    qatomic_set(&prof->opt_time, prof->opt_time + profile_getclock());
    qatomic_set(&prof->la_time, prof->la_time - profile_getclock());
#endif

    reachable_code_pass(s);
    liveness_pass_0(s);
    liveness_pass_1(s);

    if (s->nb_indirects > 0) {
#ifdef DEBUG_DISAS
        if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP_IND)
                     && qemu_log_in_addr_range(pc_start))) {
            FILE *logfile = qemu_log_trylock();
            if (logfile) {
                fprintf(logfile, "OP before indirect lowering:\n");
                tcg_dump_ops(s, logfile, false);
                fprintf(logfile, "\n");
                qemu_log_unlock(logfile);
            }
        }
#endif
        /* Replace indirect temps with direct temps.  */
        if (liveness_pass_2(s)) {
            /* If changes were made, re-run liveness.  */
            liveness_pass_1(s);
        }
    }

#ifdef CONFIG_PROFILER
    qatomic_set(&prof->la_time, prof->la_time + profile_getclock());
#endif

#ifdef DEBUG_DISAS
    if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP_OPT)
                 && qemu_log_in_addr_range(pc_start))) {
        FILE *logfile = qemu_log_trylock();
        if (logfile) {
            fprintf(logfile, "OP after optimization and liveness analysis:\n");
            tcg_dump_ops(s, logfile, true);
            fprintf(logfile, "\n");
            qemu_log_unlock(logfile);
        }
    }
#endif

    /* Initialize goto_tb jump offsets. */
    tb->jmp_reset_offset[0] = TB_JMP_OFFSET_INVALID;
    tb->jmp_reset_offset[1] = TB_JMP_OFFSET_INVALID;
    tb->jmp_insn_offset[0] = TB_JMP_OFFSET_INVALID;
    tb->jmp_insn_offset[1] = TB_JMP_OFFSET_INVALID;

    tcg_reg_alloc_start(s);

    /*
     * Reset the buffer pointers when restarting after overflow.
     * TODO: Move this into translate-all.c with the rest of the
     * buffer management.  Having only this done here is confusing.
     */
    s->code_buf = tcg_splitwx_to_rw(tb->tc.ptr);
    s->code_ptr = s->code_buf;

#ifdef TCG_TARGET_NEED_LDST_LABELS
    QSIMPLEQ_INIT(&s->ldst_labels);
#endif
#ifdef TCG_TARGET_NEED_POOL_LABELS
    s->pool_labels = NULL;
#endif

    num_insns = -1;
    QTAILQ_FOREACH(op, &s->ops, link) {
        TCGOpcode opc = op->opc;

#ifdef CONFIG_PROFILER
        qatomic_set(&prof->table_op_count[opc], prof->table_op_count[opc] + 1);
#endif

        switch (opc) {
        case INDEX_op_mov_i32:
        case INDEX_op_mov_i64:
        case INDEX_op_mov_vec:
            tcg_reg_alloc_mov(s, op);
            break;
        case INDEX_op_dup_vec:
            tcg_reg_alloc_dup(s, op);
            break;
        case INDEX_op_insn_start:
            if (num_insns >= 0) {
                size_t off = tcg_current_code_size(s);
                s->gen_insn_end_off[num_insns] = off;
                /* Assert that we do not overflow our stored offset.  */
                assert(s->gen_insn_end_off[num_insns] == off);
            }
            num_insns++;
            for (i = 0; i < TARGET_INSN_START_WORDS; ++i) {
                target_ulong a;
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
                a = deposit64(op->args[i * 2], 32, 32, op->args[i * 2 + 1]);
#else
                a = op->args[i];
#endif
                s->gen_insn_data[num_insns][i] = a;
            }
            break;
        case INDEX_op_discard:
            temp_dead(s, arg_temp(op->args[0]));
            break;
        case INDEX_op_set_label:
            tcg_reg_alloc_bb_end(s, s->reserved_regs);
            tcg_out_label(s, arg_label(op->args[0]));
            break;
        case INDEX_op_call:
            tcg_reg_alloc_call(s, op);
            break;
        case INDEX_op_exit_tb:
            tcg_out_exit_tb(s, op->args[0]);
            break;
        case INDEX_op_goto_tb:
            tcg_out_goto_tb(s, op->args[0]);
            break;
        case INDEX_op_dup2_vec:
            if (tcg_reg_alloc_dup2(s, op)) {
                break;
            }
            /* fall through */
        default:
            /* Sanity check that we've not introduced any unhandled opcodes. */
            tcg_debug_assert(tcg_op_supported(opc));
            /* Note: in order to speed up the code, it would be much
               faster to have specialized register allocator functions for
               some common argument patterns */
            tcg_reg_alloc_op(s, op);
            break;
        }
        /* Test for (pending) buffer overflow.  The assumption is that any
           one operation beginning below the high water mark cannot overrun
           the buffer completely.  Thus we can test for overflow after
           generating code without having to check during generation.  */
        if (unlikely((void *)s->code_ptr > s->code_gen_highwater)) {
            return -1;
        }
        /* Test for TB overflow, as seen by gen_insn_end_off.  */
        if (unlikely(tcg_current_code_size(s) > UINT16_MAX)) {
            return -2;
        }
    }
    tcg_debug_assert(num_insns >= 0);
    s->gen_insn_end_off[num_insns] = tcg_current_code_size(s);

    /* Generate TB finalization at the end of block */
#ifdef TCG_TARGET_NEED_LDST_LABELS
    i = tcg_out_ldst_finalize(s);
    if (i < 0) {
        return i;
    }
#endif
#ifdef TCG_TARGET_NEED_POOL_LABELS
    i = tcg_out_pool_finalize(s);
    if (i < 0) {
        return i;
    }
#endif
    if (!tcg_resolve_relocs(s)) {
        return -2;
    }

#ifndef CONFIG_TCG_INTERPRETER
    /* flush instruction cache */
    flush_idcache_range((uintptr_t)tcg_splitwx_to_rx(s->code_buf),
                        (uintptr_t)s->code_buf,
                        tcg_ptr_byte_diff(s->code_ptr, s->code_buf));
#endif

    return tcg_current_code_size(s);
}

#ifdef CONFIG_PROFILER
void tcg_dump_info(GString *buf)
{
    TCGProfile prof = {};
    const TCGProfile *s;
    int64_t tb_count;
    int64_t tb_div_count;
    int64_t tot;

    tcg_profile_snapshot_counters(&prof);
    s = &prof;
    tb_count = s->tb_count;
    tb_div_count = tb_count ? tb_count : 1;
    tot = s->interm_time + s->code_time;

    g_string_append_printf(buf, "JIT cycles          %" PRId64
                           " (%0.3f s at 2.4 GHz)\n",
                           tot, tot / 2.4e9);
    g_string_append_printf(buf, "translated TBs      %" PRId64
                           " (aborted=%" PRId64 " %0.1f%%)\n",
                           tb_count, s->tb_count1 - tb_count,
                           (double)(s->tb_count1 - s->tb_count)
                           / (s->tb_count1 ? s->tb_count1 : 1) * 100.0);
    g_string_append_printf(buf, "avg ops/TB          %0.1f max=%d\n",
                           (double)s->op_count / tb_div_count, s->op_count_max);
    g_string_append_printf(buf, "deleted ops/TB      %0.2f\n",
                           (double)s->del_op_count / tb_div_count);
    g_string_append_printf(buf, "avg temps/TB        %0.2f max=%d\n",
                           (double)s->temp_count / tb_div_count,
                           s->temp_count_max);
    g_string_append_printf(buf, "avg host code/TB    %0.1f\n",
                           (double)s->code_out_len / tb_div_count);
    g_string_append_printf(buf, "avg search data/TB  %0.1f\n",
                           (double)s->search_out_len / tb_div_count);

    g_string_append_printf(buf, "cycles/op           %0.1f\n",
                           s->op_count ? (double)tot / s->op_count : 0);
    g_string_append_printf(buf, "cycles/in byte      %0.1f\n",
                           s->code_in_len ? (double)tot / s->code_in_len : 0);
    g_string_append_printf(buf, "cycles/out byte     %0.1f\n",
                           s->code_out_len ? (double)tot / s->code_out_len : 0);
    g_string_append_printf(buf, "cycles/search byte     %0.1f\n",
                           s->search_out_len ?
                           (double)tot / s->search_out_len : 0);
    if (tot == 0) {
        tot = 1;
    }
    g_string_append_printf(buf, "  gen_interm time   %0.1f%%\n",
                           (double)s->interm_time / tot * 100.0);
    g_string_append_printf(buf, "  gen_code time     %0.1f%%\n",
                           (double)s->code_time / tot * 100.0);
    g_string_append_printf(buf, "optim./code time    %0.1f%%\n",
                           (double)s->opt_time / (s->code_time ?
                                                  s->code_time : 1)
                           * 100.0);
    g_string_append_printf(buf, "liveness/code time  %0.1f%%\n",
                           (double)s->la_time / (s->code_time ?
                                                 s->code_time : 1) * 100.0);
    g_string_append_printf(buf, "cpu_restore count   %" PRId64 "\n",
                           s->restore_count);
    g_string_append_printf(buf, "  avg cycles        %0.1f\n",
                           s->restore_count ?
                           (double)s->restore_time / s->restore_count : 0);
}
#else
void tcg_dump_info(GString *buf)
{
    g_string_append_printf(buf, "[TCG profiler not compiled]\n");
}
#endif

#ifdef ELF_HOST_MACHINE
/* In order to use this feature, the backend needs to do three things:

   (1) Define ELF_HOST_MACHINE to indicate both what value to
       put into the ELF image and to indicate support for the feature.

   (2) Define tcg_register_jit.  This should create a buffer containing
       the contents of a .debug_frame section that describes the post-
       prologue unwind info for the tcg machine.

   (3) Call tcg_register_jit_int, with the constructed .debug_frame.
*/

/* Begin GDB interface.  THE FOLLOWING MUST MATCH GDB DOCS.  */
typedef enum {
    JIT_NOACTION = 0,
    JIT_REGISTER_FN,
    JIT_UNREGISTER_FN
} jit_actions_t;

struct jit_code_entry {
    struct jit_code_entry *next_entry;
    struct jit_code_entry *prev_entry;
    const void *symfile_addr;
    uint64_t symfile_size;
};

struct jit_descriptor {
    uint32_t version;
    uint32_t action_flag;
    struct jit_code_entry *relevant_entry;
    struct jit_code_entry *first_entry;
};

void __jit_debug_register_code(void) __attribute__((noinline));
void __jit_debug_register_code(void)
{
    asm("");
}

/* Must statically initialize the version, because GDB may check
   the version before we can set it.  */
struct jit_descriptor __jit_debug_descriptor = { 1, 0, 0, 0 };

/* End GDB interface.  */

static int find_string(const char *strtab, const char *str)
{
    const char *p = strtab + 1;

    while (1) {
        if (strcmp(p, str) == 0) {
            return p - strtab;
        }
        p += strlen(p) + 1;
    }
}

static void tcg_register_jit_int(const void *buf_ptr, size_t buf_size,
                                 const void *debug_frame,
                                 size_t debug_frame_size)
{
    struct __attribute__((packed)) DebugInfo {
        uint32_t  len;
        uint16_t  version;
        uint32_t  abbrev;
        uint8_t   ptr_size;
        uint8_t   cu_die;
        uint16_t  cu_lang;
        uintptr_t cu_low_pc;
        uintptr_t cu_high_pc;
        uint8_t   fn_die;
        char      fn_name[16];
        uintptr_t fn_low_pc;
        uintptr_t fn_high_pc;
        uint8_t   cu_eoc;
    };

    struct ElfImage {
        ElfW(Ehdr) ehdr;
        ElfW(Phdr) phdr;
        ElfW(Shdr) shdr[7];
        ElfW(Sym)  sym[2];
        struct DebugInfo di;
        uint8_t    da[24];
        char       str[80];
    };

    struct ElfImage *img;

    static const struct ElfImage img_template = {
        .ehdr = {
            .e_ident[EI_MAG0] = ELFMAG0,
            .e_ident[EI_MAG1] = ELFMAG1,
            .e_ident[EI_MAG2] = ELFMAG2,
            .e_ident[EI_MAG3] = ELFMAG3,
            .e_ident[EI_CLASS] = ELF_CLASS,
            .e_ident[EI_DATA] = ELF_DATA,
            .e_ident[EI_VERSION] = EV_CURRENT,
            .e_type = ET_EXEC,
            .e_machine = ELF_HOST_MACHINE,
            .e_version = EV_CURRENT,
            .e_phoff = offsetof(struct ElfImage, phdr),
            .e_shoff = offsetof(struct ElfImage, shdr),
            .e_ehsize = sizeof(ElfW(Shdr)),
            .e_phentsize = sizeof(ElfW(Phdr)),
            .e_phnum = 1,
            .e_shentsize = sizeof(ElfW(Shdr)),
            .e_shnum = ARRAY_SIZE(img->shdr),
            .e_shstrndx = ARRAY_SIZE(img->shdr) - 1,
#ifdef ELF_HOST_FLAGS
            .e_flags = ELF_HOST_FLAGS,
#endif
#ifdef ELF_OSABI
            .e_ident[EI_OSABI] = ELF_OSABI,
#endif
        },
        .phdr = {
            .p_type = PT_LOAD,
            .p_flags = PF_X,
        },
        .shdr = {
            [0] = { .sh_type = SHT_NULL },
            /* Trick: The contents of code_gen_buffer are not present in
               this fake ELF file; that got allocated elsewhere.  Therefore
               we mark .text as SHT_NOBITS (similar to .bss) so that readers
               will not look for contents.  We can record any address.  */
            [1] = { /* .text */
                .sh_type = SHT_NOBITS,
                .sh_flags = SHF_EXECINSTR | SHF_ALLOC,
            },
            [2] = { /* .debug_info */
                .sh_type = SHT_PROGBITS,
                .sh_offset = offsetof(struct ElfImage, di),
                .sh_size = sizeof(struct DebugInfo),
            },
            [3] = { /* .debug_abbrev */
                .sh_type = SHT_PROGBITS,
                .sh_offset = offsetof(struct ElfImage, da),
                .sh_size = sizeof(img->da),
            },
            [4] = { /* .debug_frame */
                .sh_type = SHT_PROGBITS,
                .sh_offset = sizeof(struct ElfImage),
            },
            [5] = { /* .symtab */
                .sh_type = SHT_SYMTAB,
                .sh_offset = offsetof(struct ElfImage, sym),
                .sh_size = sizeof(img->sym),
                .sh_info = 1,
                .sh_link = ARRAY_SIZE(img->shdr) - 1,
                .sh_entsize = sizeof(ElfW(Sym)),
            },
            [6] = { /* .strtab */
                .sh_type = SHT_STRTAB,
                .sh_offset = offsetof(struct ElfImage, str),
                .sh_size = sizeof(img->str),
            }
        },
        .sym = {
            [1] = { /* code_gen_buffer */
                .st_info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC),
                .st_shndx = 1,
            }
        },
        .di = {
            .len = sizeof(struct DebugInfo) - 4,
            .version = 2,
            .ptr_size = sizeof(void *),
            .cu_die = 1,
            .cu_lang = 0x8001,  /* DW_LANG_Mips_Assembler */
            .fn_die = 2,
            .fn_name = "code_gen_buffer"
        },
        .da = {
            1,          /* abbrev number (the cu) */
            0x11, 1,    /* DW_TAG_compile_unit, has children */
            0x13, 0x5,  /* DW_AT_language, DW_FORM_data2 */
            0x11, 0x1,  /* DW_AT_low_pc, DW_FORM_addr */
            0x12, 0x1,  /* DW_AT_high_pc, DW_FORM_addr */
            0, 0,       /* end of abbrev */
            2,          /* abbrev number (the fn) */
            0x2e, 0,    /* DW_TAG_subprogram, no children */
            0x3, 0x8,   /* DW_AT_name, DW_FORM_string */
            0x11, 0x1,  /* DW_AT_low_pc, DW_FORM_addr */
            0x12, 0x1,  /* DW_AT_high_pc, DW_FORM_addr */
            0, 0,       /* end of abbrev */
            0           /* no more abbrev */
        },
        .str = "\0" ".text\0" ".debug_info\0" ".debug_abbrev\0"
               ".debug_frame\0" ".symtab\0" ".strtab\0" "code_gen_buffer",
    };

    /* We only need a single jit entry; statically allocate it.  */
    static struct jit_code_entry one_entry;

    uintptr_t buf = (uintptr_t)buf_ptr;
    size_t img_size = sizeof(struct ElfImage) + debug_frame_size;
    DebugFrameHeader *dfh;

    img = g_malloc(img_size);
    *img = img_template;

    img->phdr.p_vaddr = buf;
    img->phdr.p_paddr = buf;
    img->phdr.p_memsz = buf_size;

    img->shdr[1].sh_name = find_string(img->str, ".text");
    img->shdr[1].sh_addr = buf;
    img->shdr[1].sh_size = buf_size;

    img->shdr[2].sh_name = find_string(img->str, ".debug_info");
    img->shdr[3].sh_name = find_string(img->str, ".debug_abbrev");

    img->shdr[4].sh_name = find_string(img->str, ".debug_frame");
    img->shdr[4].sh_size = debug_frame_size;

    img->shdr[5].sh_name = find_string(img->str, ".symtab");
    img->shdr[6].sh_name = find_string(img->str, ".strtab");

    img->sym[1].st_name = find_string(img->str, "code_gen_buffer");
    img->sym[1].st_value = buf;
    img->sym[1].st_size = buf_size;

    img->di.cu_low_pc = buf;
    img->di.cu_high_pc = buf + buf_size;
    img->di.fn_low_pc = buf;
    img->di.fn_high_pc = buf + buf_size;

    dfh = (DebugFrameHeader *)(img + 1);
    memcpy(dfh, debug_frame, debug_frame_size);
    dfh->fde.func_start = buf;
    dfh->fde.func_len = buf_size;

#ifdef DEBUG_JIT
    /* Enable this block to be able to debug the ELF image file creation.
       One can use readelf, objdump, or other inspection utilities.  */
    {
        g_autofree char *jit = g_strdup_printf("%s/qemu.jit", g_get_tmp_dir());
        FILE *f = fopen(jit, "w+b");
        if (f) {
            if (fwrite(img, img_size, 1, f) != img_size) {
                /* Avoid stupid unused return value warning for fwrite.  */
            }
            fclose(f);
        }
    }
#endif

    one_entry.symfile_addr = img;
    one_entry.symfile_size = img_size;

    __jit_debug_descriptor.action_flag = JIT_REGISTER_FN;
    __jit_debug_descriptor.relevant_entry = &one_entry;
    __jit_debug_descriptor.first_entry = &one_entry;
    __jit_debug_register_code();
}
#else
/* No support for the feature.  Provide the entry point expected by exec.c,
   and implement the internal function we declared earlier.  */

static void tcg_register_jit_int(const void *buf, size_t size,
                                 const void *debug_frame,
                                 size_t debug_frame_size)
{
}

void tcg_register_jit(const void *buf, size_t buf_size)
{
}
#endif /* ELF_HOST_MACHINE */

#if !TCG_TARGET_MAYBE_vec
void tcg_expand_vec_op(TCGOpcode o, TCGType t, unsigned e, TCGArg a0, ...)
{
    g_assert_not_reached();
}
#endif
