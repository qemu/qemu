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

#include "qemu/osdep.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#include "tcg/tcg-temp-internal.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-mo.h"
#include "exec/plugin-gen.h"
#include "tcg-internal.h"


static inline MemOp tcg_canonicalize_memop(MemOp op, bool is64, bool st)
{
    /* Trigger the asserts within as early as possible.  */
    unsigned a_bits = get_alignment_bits(op);

    /* Prefer MO_ALIGN+MO_XX over MO_ALIGN_XX+MO_XX */
    if (a_bits == (op & MO_SIZE)) {
        op = (op & ~MO_AMASK) | MO_ALIGN;
    }

    switch (op & MO_SIZE) {
    case MO_8:
        op &= ~MO_BSWAP;
        break;
    case MO_16:
        break;
    case MO_32:
        if (!is64) {
            op &= ~MO_SIGN;
        }
        break;
    case MO_64:
        if (is64) {
            op &= ~MO_SIGN;
            break;
        }
        /* fall through */
    default:
        g_assert_not_reached();
    }
    if (st) {
        op &= ~MO_SIGN;
    }
    return op;
}

static void gen_ldst_i32(TCGOpcode opc, TCGv_i32 val, TCGv addr,
                         MemOp memop, TCGArg idx)
{
    MemOpIdx oi = make_memop_idx(memop, idx);
#if TARGET_LONG_BITS == 32
    tcg_gen_op3i_i32(opc, val, addr, oi);
#else
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_op4i_i32(opc, val, TCGV_LOW(addr), TCGV_HIGH(addr), oi);
    } else {
        tcg_gen_op3(opc, tcgv_i32_arg(val), tcgv_i64_arg(addr), oi);
    }
#endif
}

static void gen_ldst_i64(TCGOpcode opc, TCGv_i64 val, TCGv addr,
                         MemOp memop, TCGArg idx)
{
    MemOpIdx oi = make_memop_idx(memop, idx);
#if TARGET_LONG_BITS == 32
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_op4i_i32(opc, TCGV_LOW(val), TCGV_HIGH(val), addr, oi);
    } else {
        tcg_gen_op3(opc, tcgv_i64_arg(val), tcgv_i32_arg(addr), oi);
    }
#else
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_op5i_i32(opc, TCGV_LOW(val), TCGV_HIGH(val),
                         TCGV_LOW(addr), TCGV_HIGH(addr), oi);
    } else {
        tcg_gen_op3i_i64(opc, val, addr, oi);
    }
#endif
}

static void tcg_gen_req_mo(TCGBar type)
{
#ifdef TCG_GUEST_DEFAULT_MO
    type &= TCG_GUEST_DEFAULT_MO;
#endif
    type &= ~TCG_TARGET_DEFAULT_MO;
    if (type) {
        tcg_gen_mb(type | TCG_BAR_SC);
    }
}

static inline TCGv plugin_prep_mem_callbacks(TCGv vaddr)
{
#ifdef CONFIG_PLUGIN
    if (tcg_ctx->plugin_insn != NULL) {
        /* Save a copy of the vaddr for use after a load.  */
        TCGv temp = tcg_temp_new();
        tcg_gen_mov_tl(temp, vaddr);
        return temp;
    }
#endif
    return vaddr;
}

static void plugin_gen_mem_callbacks(TCGv vaddr, MemOpIdx oi,
                                     enum qemu_plugin_mem_rw rw)
{
#ifdef CONFIG_PLUGIN
    if (tcg_ctx->plugin_insn != NULL) {
        qemu_plugin_meminfo_t info = make_plugin_meminfo(oi, rw);
        plugin_gen_empty_mem_callback(vaddr, info);
        tcg_temp_free(vaddr);
    }
#endif
}

void tcg_gen_qemu_ld_i32(TCGv_i32 val, TCGv addr, TCGArg idx, MemOp memop)
{
    MemOp orig_memop;
    MemOpIdx oi;

    tcg_gen_req_mo(TCG_MO_LD_LD | TCG_MO_ST_LD);
    memop = tcg_canonicalize_memop(memop, 0, 0);
    oi = make_memop_idx(memop, idx);

    orig_memop = memop;
    if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
        memop &= ~MO_BSWAP;
        /* The bswap primitive benefits from zero-extended input.  */
        if ((memop & MO_SSIZE) == MO_SW) {
            memop &= ~MO_SIGN;
        }
    }

    addr = plugin_prep_mem_callbacks(addr);
    gen_ldst_i32(INDEX_op_qemu_ld_i32, val, addr, memop, idx);
    plugin_gen_mem_callbacks(addr, oi, QEMU_PLUGIN_MEM_R);

    if ((orig_memop ^ memop) & MO_BSWAP) {
        switch (orig_memop & MO_SIZE) {
        case MO_16:
            tcg_gen_bswap16_i32(val, val, (orig_memop & MO_SIGN
                                           ? TCG_BSWAP_IZ | TCG_BSWAP_OS
                                           : TCG_BSWAP_IZ | TCG_BSWAP_OZ));
            break;
        case MO_32:
            tcg_gen_bswap32_i32(val, val);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

void tcg_gen_qemu_st_i32(TCGv_i32 val, TCGv addr, TCGArg idx, MemOp memop)
{
    TCGv_i32 swap = NULL;
    MemOpIdx oi;

    tcg_gen_req_mo(TCG_MO_LD_ST | TCG_MO_ST_ST);
    memop = tcg_canonicalize_memop(memop, 0, 1);
    oi = make_memop_idx(memop, idx);

    if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
        swap = tcg_temp_ebb_new_i32();
        switch (memop & MO_SIZE) {
        case MO_16:
            tcg_gen_bswap16_i32(swap, val, 0);
            break;
        case MO_32:
            tcg_gen_bswap32_i32(swap, val);
            break;
        default:
            g_assert_not_reached();
        }
        val = swap;
        memop &= ~MO_BSWAP;
    }

    addr = plugin_prep_mem_callbacks(addr);
    if (TCG_TARGET_HAS_qemu_st8_i32 && (memop & MO_SIZE) == MO_8) {
        gen_ldst_i32(INDEX_op_qemu_st8_i32, val, addr, memop, idx);
    } else {
        gen_ldst_i32(INDEX_op_qemu_st_i32, val, addr, memop, idx);
    }
    plugin_gen_mem_callbacks(addr, oi, QEMU_PLUGIN_MEM_W);

    if (swap) {
        tcg_temp_free_i32(swap);
    }
}

void tcg_gen_qemu_ld_i64(TCGv_i64 val, TCGv addr, TCGArg idx, MemOp memop)
{
    MemOp orig_memop;
    MemOpIdx oi;

    if (TCG_TARGET_REG_BITS == 32 && (memop & MO_SIZE) < MO_64) {
        tcg_gen_qemu_ld_i32(TCGV_LOW(val), addr, idx, memop);
        if (memop & MO_SIGN) {
            tcg_gen_sari_i32(TCGV_HIGH(val), TCGV_LOW(val), 31);
        } else {
            tcg_gen_movi_i32(TCGV_HIGH(val), 0);
        }
        return;
    }

    tcg_gen_req_mo(TCG_MO_LD_LD | TCG_MO_ST_LD);
    memop = tcg_canonicalize_memop(memop, 1, 0);
    oi = make_memop_idx(memop, idx);

    orig_memop = memop;
    if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
        memop &= ~MO_BSWAP;
        /* The bswap primitive benefits from zero-extended input.  */
        if ((memop & MO_SIGN) && (memop & MO_SIZE) < MO_64) {
            memop &= ~MO_SIGN;
        }
    }

    addr = plugin_prep_mem_callbacks(addr);
    gen_ldst_i64(INDEX_op_qemu_ld_i64, val, addr, memop, idx);
    plugin_gen_mem_callbacks(addr, oi, QEMU_PLUGIN_MEM_R);

    if ((orig_memop ^ memop) & MO_BSWAP) {
        int flags = (orig_memop & MO_SIGN
                     ? TCG_BSWAP_IZ | TCG_BSWAP_OS
                     : TCG_BSWAP_IZ | TCG_BSWAP_OZ);
        switch (orig_memop & MO_SIZE) {
        case MO_16:
            tcg_gen_bswap16_i64(val, val, flags);
            break;
        case MO_32:
            tcg_gen_bswap32_i64(val, val, flags);
            break;
        case MO_64:
            tcg_gen_bswap64_i64(val, val);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

void tcg_gen_qemu_st_i64(TCGv_i64 val, TCGv addr, TCGArg idx, MemOp memop)
{
    TCGv_i64 swap = NULL;
    MemOpIdx oi;

    if (TCG_TARGET_REG_BITS == 32 && (memop & MO_SIZE) < MO_64) {
        tcg_gen_qemu_st_i32(TCGV_LOW(val), addr, idx, memop);
        return;
    }

    tcg_gen_req_mo(TCG_MO_LD_ST | TCG_MO_ST_ST);
    memop = tcg_canonicalize_memop(memop, 1, 1);
    oi = make_memop_idx(memop, idx);

    if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
        swap = tcg_temp_ebb_new_i64();
        switch (memop & MO_SIZE) {
        case MO_16:
            tcg_gen_bswap16_i64(swap, val, 0);
            break;
        case MO_32:
            tcg_gen_bswap32_i64(swap, val, 0);
            break;
        case MO_64:
            tcg_gen_bswap64_i64(swap, val);
            break;
        default:
            g_assert_not_reached();
        }
        val = swap;
        memop &= ~MO_BSWAP;
    }

    addr = plugin_prep_mem_callbacks(addr);
    gen_ldst_i64(INDEX_op_qemu_st_i64, val, addr, memop, idx);
    plugin_gen_mem_callbacks(addr, oi, QEMU_PLUGIN_MEM_W);

    if (swap) {
        tcg_temp_free_i64(swap);
    }
}

/*
 * Return true if @mop, without knowledge of the pointer alignment,
 * does not require 16-byte atomicity, and it would be adventagous
 * to avoid a call to a helper function.
 */
static bool use_two_i64_for_i128(MemOp mop)
{
#ifdef CONFIG_SOFTMMU
    /* Two softmmu tlb lookups is larger than one function call. */
    return false;
#else
    /*
     * For user-only, two 64-bit operations may well be smaller than a call.
     * Determine if that would be legal for the requested atomicity.
     */
    switch (mop & MO_ATOM_MASK) {
    case MO_ATOM_NONE:
    case MO_ATOM_IFALIGN_PAIR:
        return true;
    case MO_ATOM_IFALIGN:
    case MO_ATOM_SUBALIGN:
    case MO_ATOM_WITHIN16:
    case MO_ATOM_WITHIN16_PAIR:
        /* In a serialized context, no atomicity is required. */
        return !(tcg_ctx->gen_tb->cflags & CF_PARALLEL);
    default:
        g_assert_not_reached();
    }
#endif
}

static void canonicalize_memop_i128_as_i64(MemOp ret[2], MemOp orig)
{
    MemOp mop_1 = orig, mop_2;

    tcg_debug_assert((orig & MO_SIZE) == MO_128);
    tcg_debug_assert((orig & MO_SIGN) == 0);

    /* Reduce the size to 64-bit. */
    mop_1 = (mop_1 & ~MO_SIZE) | MO_64;

    /* Retain the alignment constraints of the original. */
    switch (orig & MO_AMASK) {
    case MO_UNALN:
    case MO_ALIGN_2:
    case MO_ALIGN_4:
        mop_2 = mop_1;
        break;
    case MO_ALIGN_8:
        /* Prefer MO_ALIGN+MO_64 to MO_ALIGN_8+MO_64. */
        mop_1 = (mop_1 & ~MO_AMASK) | MO_ALIGN;
        mop_2 = mop_1;
        break;
    case MO_ALIGN:
        /* Second has 8-byte alignment; first has 16-byte alignment. */
        mop_2 = mop_1;
        mop_1 = (mop_1 & ~MO_AMASK) | MO_ALIGN_16;
        break;
    case MO_ALIGN_16:
    case MO_ALIGN_32:
    case MO_ALIGN_64:
        /* Second has 8-byte alignment; first retains original. */
        mop_2 = (mop_1 & ~MO_AMASK) | MO_ALIGN;
        break;
    default:
        g_assert_not_reached();
    }

    /* Use a memory ordering implemented by the host. */
    if ((orig & MO_BSWAP) && !tcg_target_has_memory_bswap(mop_1)) {
        mop_1 &= ~MO_BSWAP;
        mop_2 &= ~MO_BSWAP;
    }

    ret[0] = mop_1;
    ret[1] = mop_2;
}

#if TARGET_LONG_BITS == 64
#define tcg_temp_ebb_new  tcg_temp_ebb_new_i64
#else
#define tcg_temp_ebb_new  tcg_temp_ebb_new_i32
#endif

static TCGv_i64 maybe_extend_addr64(TCGv addr)
{
#if TARGET_LONG_BITS == 32
    TCGv_i64 a64 = tcg_temp_ebb_new_i64();
    tcg_gen_extu_i32_i64(a64, addr);
    return a64;
#else
    return addr;
#endif
}

static void maybe_free_addr64(TCGv_i64 a64)
{
#if TARGET_LONG_BITS == 32
    tcg_temp_free_i64(a64);
#endif
}

void tcg_gen_qemu_ld_i128(TCGv_i128 val, TCGv addr, TCGArg idx, MemOp memop)
{
    const MemOpIdx oi = make_memop_idx(memop, idx);

    tcg_debug_assert((memop & MO_SIZE) == MO_128);
    tcg_debug_assert((memop & MO_SIGN) == 0);

    tcg_gen_req_mo(TCG_MO_LD_LD | TCG_MO_ST_LD);
    addr = plugin_prep_mem_callbacks(addr);

    /* TODO: For now, force 32-bit hosts to use the helper. */
    if (TCG_TARGET_HAS_qemu_ldst_i128 && TCG_TARGET_REG_BITS == 64) {
        TCGv_i64 lo, hi;
        TCGArg addr_arg;
        MemOpIdx adj_oi;
        bool need_bswap = false;

        if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
            lo = TCGV128_HIGH(val);
            hi = TCGV128_LOW(val);
            adj_oi = make_memop_idx(memop & ~MO_BSWAP, idx);
            need_bswap = true;
        } else {
            lo = TCGV128_LOW(val);
            hi = TCGV128_HIGH(val);
            adj_oi = oi;
        }

#if TARGET_LONG_BITS == 32
        addr_arg = tcgv_i32_arg(addr);
#else
        addr_arg = tcgv_i64_arg(addr);
#endif
        tcg_gen_op4ii_i64(INDEX_op_qemu_ld_i128, lo, hi, addr_arg, adj_oi);

        if (need_bswap) {
            tcg_gen_bswap64_i64(lo, lo);
            tcg_gen_bswap64_i64(hi, hi);
        }
    } else if (use_two_i64_for_i128(memop)) {
        MemOp mop[2];
        TCGv addr_p8;
        TCGv_i64 x, y;

        canonicalize_memop_i128_as_i64(mop, memop);

        /*
         * Since there are no global TCGv_i128, there is no visible state
         * changed if the second load faults.  Load directly into the two
         * subwords.
         */
        if ((memop & MO_BSWAP) == MO_LE) {
            x = TCGV128_LOW(val);
            y = TCGV128_HIGH(val);
        } else {
            x = TCGV128_HIGH(val);
            y = TCGV128_LOW(val);
        }

        gen_ldst_i64(INDEX_op_qemu_ld_i64, x, addr, mop[0], idx);

        if ((mop[0] ^ memop) & MO_BSWAP) {
            tcg_gen_bswap64_i64(x, x);
        }

        addr_p8 = tcg_temp_ebb_new();
        tcg_gen_addi_tl(addr_p8, addr, 8);
        gen_ldst_i64(INDEX_op_qemu_ld_i64, y, addr_p8, mop[1], idx);
        tcg_temp_free(addr_p8);

        if ((mop[0] ^ memop) & MO_BSWAP) {
            tcg_gen_bswap64_i64(y, y);
        }
    } else {
        TCGv_i64 a64 = maybe_extend_addr64(addr);
        gen_helper_ld_i128(val, cpu_env, a64, tcg_constant_i32(oi));
        maybe_free_addr64(a64);
    }

    plugin_gen_mem_callbacks(addr, oi, QEMU_PLUGIN_MEM_R);
}

void tcg_gen_qemu_st_i128(TCGv_i128 val, TCGv addr, TCGArg idx, MemOp memop)
{
    const MemOpIdx oi = make_memop_idx(memop, idx);

    tcg_debug_assert((memop & MO_SIZE) == MO_128);
    tcg_debug_assert((memop & MO_SIGN) == 0);

    tcg_gen_req_mo(TCG_MO_ST_LD | TCG_MO_ST_ST);
    addr = plugin_prep_mem_callbacks(addr);

    /* TODO: For now, force 32-bit hosts to use the helper. */

    if (TCG_TARGET_HAS_qemu_ldst_i128 && TCG_TARGET_REG_BITS == 64) {
        TCGv_i64 lo, hi;
        TCGArg addr_arg;
        MemOpIdx adj_oi;
        bool need_bswap = false;

        if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
            lo = tcg_temp_new_i64();
            hi = tcg_temp_new_i64();
            tcg_gen_bswap64_i64(lo, TCGV128_HIGH(val));
            tcg_gen_bswap64_i64(hi, TCGV128_LOW(val));
            adj_oi = make_memop_idx(memop & ~MO_BSWAP, idx);
            need_bswap = true;
        } else {
            lo = TCGV128_LOW(val);
            hi = TCGV128_HIGH(val);
            adj_oi = oi;
        }

#if TARGET_LONG_BITS == 32
        addr_arg = tcgv_i32_arg(addr);
#else
        addr_arg = tcgv_i64_arg(addr);
#endif
        tcg_gen_op4ii_i64(INDEX_op_qemu_st_i128, lo, hi, addr_arg, adj_oi);

        if (need_bswap) {
            tcg_temp_free_i64(lo);
            tcg_temp_free_i64(hi);
        }
    } else if (use_two_i64_for_i128(memop)) {
        MemOp mop[2];
        TCGv addr_p8;
        TCGv_i64 x, y;

        canonicalize_memop_i128_as_i64(mop, memop);

        if ((memop & MO_BSWAP) == MO_LE) {
            x = TCGV128_LOW(val);
            y = TCGV128_HIGH(val);
        } else {
            x = TCGV128_HIGH(val);
            y = TCGV128_LOW(val);
        }

        addr_p8 = tcg_temp_ebb_new();
        if ((mop[0] ^ memop) & MO_BSWAP) {
            TCGv_i64 t = tcg_temp_ebb_new_i64();

            tcg_gen_bswap64_i64(t, x);
            gen_ldst_i64(INDEX_op_qemu_st_i64, t, addr, mop[0], idx);
            tcg_gen_bswap64_i64(t, y);
            tcg_gen_addi_tl(addr_p8, addr, 8);
            gen_ldst_i64(INDEX_op_qemu_st_i64, t, addr_p8, mop[1], idx);
            tcg_temp_free_i64(t);
        } else {
            gen_ldst_i64(INDEX_op_qemu_st_i64, x, addr, mop[0], idx);
            tcg_gen_addi_tl(addr_p8, addr, 8);
            gen_ldst_i64(INDEX_op_qemu_st_i64, y, addr_p8, mop[1], idx);
        }
        tcg_temp_free(addr_p8);
    } else {
        TCGv_i64 a64 = maybe_extend_addr64(addr);
        gen_helper_st_i128(cpu_env, a64, val, tcg_constant_i32(oi));
        maybe_free_addr64(a64);
    }

    plugin_gen_mem_callbacks(addr, oi, QEMU_PLUGIN_MEM_W);
}

static void tcg_gen_ext_i32(TCGv_i32 ret, TCGv_i32 val, MemOp opc)
{
    switch (opc & MO_SSIZE) {
    case MO_SB:
        tcg_gen_ext8s_i32(ret, val);
        break;
    case MO_UB:
        tcg_gen_ext8u_i32(ret, val);
        break;
    case MO_SW:
        tcg_gen_ext16s_i32(ret, val);
        break;
    case MO_UW:
        tcg_gen_ext16u_i32(ret, val);
        break;
    default:
        tcg_gen_mov_i32(ret, val);
        break;
    }
}

static void tcg_gen_ext_i64(TCGv_i64 ret, TCGv_i64 val, MemOp opc)
{
    switch (opc & MO_SSIZE) {
    case MO_SB:
        tcg_gen_ext8s_i64(ret, val);
        break;
    case MO_UB:
        tcg_gen_ext8u_i64(ret, val);
        break;
    case MO_SW:
        tcg_gen_ext16s_i64(ret, val);
        break;
    case MO_UW:
        tcg_gen_ext16u_i64(ret, val);
        break;
    case MO_SL:
        tcg_gen_ext32s_i64(ret, val);
        break;
    case MO_UL:
        tcg_gen_ext32u_i64(ret, val);
        break;
    default:
        tcg_gen_mov_i64(ret, val);
        break;
    }
}

typedef void (*gen_atomic_cx_i32)(TCGv_i32, TCGv_env, TCGv_i64,
                                  TCGv_i32, TCGv_i32, TCGv_i32);
typedef void (*gen_atomic_cx_i64)(TCGv_i64, TCGv_env, TCGv_i64,
                                  TCGv_i64, TCGv_i64, TCGv_i32);
typedef void (*gen_atomic_cx_i128)(TCGv_i128, TCGv_env, TCGv_i64,
                                   TCGv_i128, TCGv_i128, TCGv_i32);
typedef void (*gen_atomic_op_i32)(TCGv_i32, TCGv_env, TCGv_i64,
                                  TCGv_i32, TCGv_i32);
typedef void (*gen_atomic_op_i64)(TCGv_i64, TCGv_env, TCGv_i64,
                                  TCGv_i64, TCGv_i32);

#ifdef CONFIG_ATOMIC64
# define WITH_ATOMIC64(X) X,
#else
# define WITH_ATOMIC64(X)
#endif
#ifdef CONFIG_CMPXCHG128
# define WITH_ATOMIC128(X) X,
#else
# define WITH_ATOMIC128(X)
#endif

static void * const table_cmpxchg[(MO_SIZE | MO_BSWAP) + 1] = {
    [MO_8] = gen_helper_atomic_cmpxchgb,
    [MO_16 | MO_LE] = gen_helper_atomic_cmpxchgw_le,
    [MO_16 | MO_BE] = gen_helper_atomic_cmpxchgw_be,
    [MO_32 | MO_LE] = gen_helper_atomic_cmpxchgl_le,
    [MO_32 | MO_BE] = gen_helper_atomic_cmpxchgl_be,
    WITH_ATOMIC64([MO_64 | MO_LE] = gen_helper_atomic_cmpxchgq_le)
    WITH_ATOMIC64([MO_64 | MO_BE] = gen_helper_atomic_cmpxchgq_be)
    WITH_ATOMIC128([MO_128 | MO_LE] = gen_helper_atomic_cmpxchgo_le)
    WITH_ATOMIC128([MO_128 | MO_BE] = gen_helper_atomic_cmpxchgo_be)
};

void tcg_gen_nonatomic_cmpxchg_i32(TCGv_i32 retv, TCGv addr, TCGv_i32 cmpv,
                                   TCGv_i32 newv, TCGArg idx, MemOp memop)
{
    TCGv_i32 t1 = tcg_temp_ebb_new_i32();
    TCGv_i32 t2 = tcg_temp_ebb_new_i32();

    tcg_gen_ext_i32(t2, cmpv, memop & MO_SIZE);

    tcg_gen_qemu_ld_i32(t1, addr, idx, memop & ~MO_SIGN);
    tcg_gen_movcond_i32(TCG_COND_EQ, t2, t1, t2, newv, t1);
    tcg_gen_qemu_st_i32(t2, addr, idx, memop);
    tcg_temp_free_i32(t2);

    if (memop & MO_SIGN) {
        tcg_gen_ext_i32(retv, t1, memop);
    } else {
        tcg_gen_mov_i32(retv, t1);
    }
    tcg_temp_free_i32(t1);
}

void tcg_gen_atomic_cmpxchg_i32(TCGv_i32 retv, TCGv addr, TCGv_i32 cmpv,
                                TCGv_i32 newv, TCGArg idx, MemOp memop)
{
    gen_atomic_cx_i32 gen;
    TCGv_i64 a64;
    MemOpIdx oi;

    if (!(tcg_ctx->gen_tb->cflags & CF_PARALLEL)) {
        tcg_gen_nonatomic_cmpxchg_i32(retv, addr, cmpv, newv, idx, memop);
        return;
    }

    memop = tcg_canonicalize_memop(memop, 0, 0);
    gen = table_cmpxchg[memop & (MO_SIZE | MO_BSWAP)];
    tcg_debug_assert(gen != NULL);

    oi = make_memop_idx(memop & ~MO_SIGN, idx);
    a64 = maybe_extend_addr64(addr);
    gen(retv, cpu_env, a64, cmpv, newv, tcg_constant_i32(oi));
    maybe_free_addr64(a64);

    if (memop & MO_SIGN) {
        tcg_gen_ext_i32(retv, retv, memop);
    }
}

void tcg_gen_nonatomic_cmpxchg_i64(TCGv_i64 retv, TCGv addr, TCGv_i64 cmpv,
                                   TCGv_i64 newv, TCGArg idx, MemOp memop)
{
    TCGv_i64 t1, t2;

    if (TCG_TARGET_REG_BITS == 32 && (memop & MO_SIZE) < MO_64) {
        tcg_gen_nonatomic_cmpxchg_i32(TCGV_LOW(retv), addr, TCGV_LOW(cmpv),
                                      TCGV_LOW(newv), idx, memop);
        if (memop & MO_SIGN) {
            tcg_gen_sari_i32(TCGV_HIGH(retv), TCGV_LOW(retv), 31);
        } else {
            tcg_gen_movi_i32(TCGV_HIGH(retv), 0);
        }
        return;
    }

    t1 = tcg_temp_ebb_new_i64();
    t2 = tcg_temp_ebb_new_i64();

    tcg_gen_ext_i64(t2, cmpv, memop & MO_SIZE);

    tcg_gen_qemu_ld_i64(t1, addr, idx, memop & ~MO_SIGN);
    tcg_gen_movcond_i64(TCG_COND_EQ, t2, t1, t2, newv, t1);
    tcg_gen_qemu_st_i64(t2, addr, idx, memop);
    tcg_temp_free_i64(t2);

    if (memop & MO_SIGN) {
        tcg_gen_ext_i64(retv, t1, memop);
    } else {
        tcg_gen_mov_i64(retv, t1);
    }
    tcg_temp_free_i64(t1);
}

void tcg_gen_atomic_cmpxchg_i64(TCGv_i64 retv, TCGv addr, TCGv_i64 cmpv,
                                TCGv_i64 newv, TCGArg idx, MemOp memop)
{
    if (!(tcg_ctx->gen_tb->cflags & CF_PARALLEL)) {
        tcg_gen_nonatomic_cmpxchg_i64(retv, addr, cmpv, newv, idx, memop);
        return;
    }

    if ((memop & MO_SIZE) == MO_64) {
        gen_atomic_cx_i64 gen;

        memop = tcg_canonicalize_memop(memop, 1, 0);
        gen = table_cmpxchg[memop & (MO_SIZE | MO_BSWAP)];
        if (gen) {
            MemOpIdx oi = make_memop_idx(memop, idx);
            TCGv_i64 a64 = maybe_extend_addr64(addr);
            gen(retv, cpu_env, a64, cmpv, newv, tcg_constant_i32(oi));
            maybe_free_addr64(a64);
            return;
        }

        gen_helper_exit_atomic(cpu_env);

        /*
         * Produce a result for a well-formed opcode stream.  This satisfies
         * liveness for set before used, which happens before this dead code
         * is removed.
         */
        tcg_gen_movi_i64(retv, 0);
        return;
    }

    if (TCG_TARGET_REG_BITS == 32) {
        tcg_gen_atomic_cmpxchg_i32(TCGV_LOW(retv), addr, TCGV_LOW(cmpv),
                                   TCGV_LOW(newv), idx, memop);
        if (memop & MO_SIGN) {
            tcg_gen_sari_i32(TCGV_HIGH(retv), TCGV_LOW(retv), 31);
        } else {
            tcg_gen_movi_i32(TCGV_HIGH(retv), 0);
        }
    } else {
        TCGv_i32 c32 = tcg_temp_ebb_new_i32();
        TCGv_i32 n32 = tcg_temp_ebb_new_i32();
        TCGv_i32 r32 = tcg_temp_ebb_new_i32();

        tcg_gen_extrl_i64_i32(c32, cmpv);
        tcg_gen_extrl_i64_i32(n32, newv);
        tcg_gen_atomic_cmpxchg_i32(r32, addr, c32, n32, idx, memop & ~MO_SIGN);
        tcg_temp_free_i32(c32);
        tcg_temp_free_i32(n32);

        tcg_gen_extu_i32_i64(retv, r32);
        tcg_temp_free_i32(r32);

        if (memop & MO_SIGN) {
            tcg_gen_ext_i64(retv, retv, memop);
        }
    }
}

void tcg_gen_nonatomic_cmpxchg_i128(TCGv_i128 retv, TCGv addr, TCGv_i128 cmpv,
                                    TCGv_i128 newv, TCGArg idx, MemOp memop)
{
    if (TCG_TARGET_REG_BITS == 32) {
        /* Inline expansion below is simply too large for 32-bit hosts. */
        gen_atomic_cx_i128 gen = ((memop & MO_BSWAP) == MO_LE
                                  ? gen_helper_nonatomic_cmpxchgo_le 
                                  : gen_helper_nonatomic_cmpxchgo_be);
        MemOpIdx oi = make_memop_idx(memop, idx);
        TCGv_i64 a64;

        tcg_debug_assert((memop & MO_SIZE) == MO_128);
        tcg_debug_assert((memop & MO_SIGN) == 0);

        a64 = maybe_extend_addr64(addr);
        gen(retv, cpu_env, a64, cmpv, newv, tcg_constant_i32(oi));
        maybe_free_addr64(a64);
    } else {
        TCGv_i128 oldv = tcg_temp_ebb_new_i128();
        TCGv_i128 tmpv = tcg_temp_ebb_new_i128();
        TCGv_i64 t0 = tcg_temp_ebb_new_i64();
        TCGv_i64 t1 = tcg_temp_ebb_new_i64();
        TCGv_i64 z = tcg_constant_i64(0);

        tcg_gen_qemu_ld_i128(oldv, addr, idx, memop);

        /* Compare i128 */
        tcg_gen_xor_i64(t0, TCGV128_LOW(oldv), TCGV128_LOW(cmpv));
        tcg_gen_xor_i64(t1, TCGV128_HIGH(oldv), TCGV128_HIGH(cmpv));
        tcg_gen_or_i64(t0, t0, t1);

        /* tmpv = equal ? newv : oldv */
        tcg_gen_movcond_i64(TCG_COND_EQ, TCGV128_LOW(tmpv), t0, z,
                            TCGV128_LOW(newv), TCGV128_LOW(oldv));
        tcg_gen_movcond_i64(TCG_COND_EQ, TCGV128_HIGH(tmpv), t0, z,
                            TCGV128_HIGH(newv), TCGV128_HIGH(oldv));

        /* Unconditional writeback. */
        tcg_gen_qemu_st_i128(tmpv, addr, idx, memop);
        tcg_gen_mov_i128(retv, oldv);

        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
        tcg_temp_free_i128(tmpv);
        tcg_temp_free_i128(oldv);
    }
}

void tcg_gen_atomic_cmpxchg_i128(TCGv_i128 retv, TCGv addr, TCGv_i128 cmpv,
                                 TCGv_i128 newv, TCGArg idx, MemOp memop)
{
    gen_atomic_cx_i128 gen;

    if (!(tcg_ctx->gen_tb->cflags & CF_PARALLEL)) {
        tcg_gen_nonatomic_cmpxchg_i128(retv, addr, cmpv, newv, idx, memop);
        return;
    }

    tcg_debug_assert((memop & MO_SIZE) == MO_128);
    tcg_debug_assert((memop & MO_SIGN) == 0);
    gen = table_cmpxchg[memop & (MO_SIZE | MO_BSWAP)];

    if (gen) {
        MemOpIdx oi = make_memop_idx(memop, idx);
        TCGv_i64 a64 = maybe_extend_addr64(addr);
        gen(retv, cpu_env, a64, cmpv, newv, tcg_constant_i32(oi));
        maybe_free_addr64(a64);
        return;
    }

    gen_helper_exit_atomic(cpu_env);

    /*
     * Produce a result for a well-formed opcode stream.  This satisfies
     * liveness for set before used, which happens before this dead code
     * is removed.
     */
    tcg_gen_movi_i64(TCGV128_LOW(retv), 0);
    tcg_gen_movi_i64(TCGV128_HIGH(retv), 0);
}

static void do_nonatomic_op_i32(TCGv_i32 ret, TCGv addr, TCGv_i32 val,
                                TCGArg idx, MemOp memop, bool new_val,
                                void (*gen)(TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 t1 = tcg_temp_ebb_new_i32();
    TCGv_i32 t2 = tcg_temp_ebb_new_i32();

    memop = tcg_canonicalize_memop(memop, 0, 0);

    tcg_gen_qemu_ld_i32(t1, addr, idx, memop);
    tcg_gen_ext_i32(t2, val, memop);
    gen(t2, t1, t2);
    tcg_gen_qemu_st_i32(t2, addr, idx, memop);

    tcg_gen_ext_i32(ret, (new_val ? t2 : t1), memop);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static void do_atomic_op_i32(TCGv_i32 ret, TCGv addr, TCGv_i32 val,
                             TCGArg idx, MemOp memop, void * const table[])
{
    gen_atomic_op_i32 gen;
    TCGv_i64 a64;
    MemOpIdx oi;

    memop = tcg_canonicalize_memop(memop, 0, 0);

    gen = table[memop & (MO_SIZE | MO_BSWAP)];
    tcg_debug_assert(gen != NULL);

    oi = make_memop_idx(memop & ~MO_SIGN, idx);
    a64 = maybe_extend_addr64(addr);
    gen(ret, cpu_env, a64, val, tcg_constant_i32(oi));
    maybe_free_addr64(a64);

    if (memop & MO_SIGN) {
        tcg_gen_ext_i32(ret, ret, memop);
    }
}

static void do_nonatomic_op_i64(TCGv_i64 ret, TCGv addr, TCGv_i64 val,
                                TCGArg idx, MemOp memop, bool new_val,
                                void (*gen)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 t1 = tcg_temp_ebb_new_i64();
    TCGv_i64 t2 = tcg_temp_ebb_new_i64();

    memop = tcg_canonicalize_memop(memop, 1, 0);

    tcg_gen_qemu_ld_i64(t1, addr, idx, memop);
    tcg_gen_ext_i64(t2, val, memop);
    gen(t2, t1, t2);
    tcg_gen_qemu_st_i64(t2, addr, idx, memop);

    tcg_gen_ext_i64(ret, (new_val ? t2 : t1), memop);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
}

static void do_atomic_op_i64(TCGv_i64 ret, TCGv addr, TCGv_i64 val,
                             TCGArg idx, MemOp memop, void * const table[])
{
    memop = tcg_canonicalize_memop(memop, 1, 0);

    if ((memop & MO_SIZE) == MO_64) {
#ifdef CONFIG_ATOMIC64
        gen_atomic_op_i64 gen;
        TCGv_i64 a64;
        MemOpIdx oi;

        gen = table[memop & (MO_SIZE | MO_BSWAP)];
        tcg_debug_assert(gen != NULL);

        oi = make_memop_idx(memop & ~MO_SIGN, idx);
        a64 = maybe_extend_addr64(addr);
        gen(ret, cpu_env, a64, val, tcg_constant_i32(oi));
        maybe_free_addr64(a64);
#else
        gen_helper_exit_atomic(cpu_env);
        /* Produce a result, so that we have a well-formed opcode stream
           with respect to uses of the result in the (dead) code following.  */
        tcg_gen_movi_i64(ret, 0);
#endif /* CONFIG_ATOMIC64 */
    } else {
        TCGv_i32 v32 = tcg_temp_ebb_new_i32();
        TCGv_i32 r32 = tcg_temp_ebb_new_i32();

        tcg_gen_extrl_i64_i32(v32, val);
        do_atomic_op_i32(r32, addr, v32, idx, memop & ~MO_SIGN, table);
        tcg_temp_free_i32(v32);

        tcg_gen_extu_i32_i64(ret, r32);
        tcg_temp_free_i32(r32);

        if (memop & MO_SIGN) {
            tcg_gen_ext_i64(ret, ret, memop);
        }
    }
}

#define GEN_ATOMIC_HELPER(NAME, OP, NEW)                                \
static void * const table_##NAME[(MO_SIZE | MO_BSWAP) + 1] = {          \
    [MO_8] = gen_helper_atomic_##NAME##b,                               \
    [MO_16 | MO_LE] = gen_helper_atomic_##NAME##w_le,                   \
    [MO_16 | MO_BE] = gen_helper_atomic_##NAME##w_be,                   \
    [MO_32 | MO_LE] = gen_helper_atomic_##NAME##l_le,                   \
    [MO_32 | MO_BE] = gen_helper_atomic_##NAME##l_be,                   \
    WITH_ATOMIC64([MO_64 | MO_LE] = gen_helper_atomic_##NAME##q_le)     \
    WITH_ATOMIC64([MO_64 | MO_BE] = gen_helper_atomic_##NAME##q_be)     \
};                                                                      \
void tcg_gen_atomic_##NAME##_i32                                        \
    (TCGv_i32 ret, TCGv addr, TCGv_i32 val, TCGArg idx, MemOp memop)    \
{                                                                       \
    if (tcg_ctx->gen_tb->cflags & CF_PARALLEL) {                        \
        do_atomic_op_i32(ret, addr, val, idx, memop, table_##NAME);     \
    } else {                                                            \
        do_nonatomic_op_i32(ret, addr, val, idx, memop, NEW,            \
                            tcg_gen_##OP##_i32);                        \
    }                                                                   \
}                                                                       \
void tcg_gen_atomic_##NAME##_i64                                        \
    (TCGv_i64 ret, TCGv addr, TCGv_i64 val, TCGArg idx, MemOp memop)    \
{                                                                       \
    if (tcg_ctx->gen_tb->cflags & CF_PARALLEL) {                        \
        do_atomic_op_i64(ret, addr, val, idx, memop, table_##NAME);     \
    } else {                                                            \
        do_nonatomic_op_i64(ret, addr, val, idx, memop, NEW,            \
                            tcg_gen_##OP##_i64);                        \
    }                                                                   \
}

GEN_ATOMIC_HELPER(fetch_add, add, 0)
GEN_ATOMIC_HELPER(fetch_and, and, 0)
GEN_ATOMIC_HELPER(fetch_or, or, 0)
GEN_ATOMIC_HELPER(fetch_xor, xor, 0)
GEN_ATOMIC_HELPER(fetch_smin, smin, 0)
GEN_ATOMIC_HELPER(fetch_umin, umin, 0)
GEN_ATOMIC_HELPER(fetch_smax, smax, 0)
GEN_ATOMIC_HELPER(fetch_umax, umax, 0)

GEN_ATOMIC_HELPER(add_fetch, add, 1)
GEN_ATOMIC_HELPER(and_fetch, and, 1)
GEN_ATOMIC_HELPER(or_fetch, or, 1)
GEN_ATOMIC_HELPER(xor_fetch, xor, 1)
GEN_ATOMIC_HELPER(smin_fetch, smin, 1)
GEN_ATOMIC_HELPER(umin_fetch, umin, 1)
GEN_ATOMIC_HELPER(smax_fetch, smax, 1)
GEN_ATOMIC_HELPER(umax_fetch, umax, 1)

static void tcg_gen_mov2_i32(TCGv_i32 r, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_mov_i32(r, b);
}

static void tcg_gen_mov2_i64(TCGv_i64 r, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_mov_i64(r, b);
}

GEN_ATOMIC_HELPER(xchg, mov2, 0)

#undef GEN_ATOMIC_HELPER
