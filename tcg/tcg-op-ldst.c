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
#include "tcg/tcg.h"
#include "tcg/tcg-temp-internal.h"
#include "tcg/tcg-op-common.h"
#include "tcg/tcg-mo.h"
#include "exec/target_page.h"
#include "exec/translation-block.h"
#include "exec/plugin-gen.h"
#include "tcg-internal.h"
#include "tcg-has.h"
#include "tcg-target-mo.h"

static void check_max_alignment(unsigned a_bits)
{
    /*
     * The requested alignment cannot overlap the TLB flags.
     * FIXME: Must keep the count up-to-date with "exec/tlb-flags.h".
     */
    if (tcg_use_softmmu) {
        tcg_debug_assert(a_bits + 5 <= TARGET_PAGE_BITS);
    }
}

static MemOp tcg_canonicalize_memop(MemOp op, bool is64, bool st)
{
    unsigned a_bits = memop_alignment_bits(op);

    check_max_alignment(a_bits);

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

    /* In serial mode, reduce atomicity. */
    if (!(tcg_ctx->gen_tb->cflags & CF_PARALLEL)) {
        op &= ~MO_ATOM_MASK;
        op |= MO_ATOM_NONE;
    }

    return op;
}

static void gen_ldst1(TCGOpcode opc, TCGType type, TCGTemp *v,
                      TCGTemp *addr, MemOpIdx oi)
{
    TCGOp *op = tcg_gen_op3(opc, type, temp_arg(v), temp_arg(addr), oi);
    TCGOP_FLAGS(op) = get_memop(oi) & MO_SIZE;
}

static void gen_ldst2(TCGOpcode opc, TCGType type, TCGTemp *vl, TCGTemp *vh,
                      TCGTemp *addr, MemOpIdx oi)
{
    TCGOp *op = tcg_gen_op4(opc, type, temp_arg(vl), temp_arg(vh),
                            temp_arg(addr), oi);
    TCGOP_FLAGS(op) = get_memop(oi) & MO_SIZE;
}

static void gen_ld_i64(TCGv_i64 v, TCGTemp *addr, MemOpIdx oi)
{
    gen_ldst1(INDEX_op_qemu_ld, TCG_TYPE_I64, tcgv_i64_temp(v), addr, oi);
}

static void gen_st_i64(TCGv_i64 v, TCGTemp *addr, MemOpIdx oi)
{
    gen_ldst1(INDEX_op_qemu_st, TCG_TYPE_I64, tcgv_i64_temp(v), addr, oi);
}

static void tcg_gen_req_mo(TCGBar type)
{
    type &= tcg_ctx->guest_mo;
    type &= ~TCG_TARGET_DEFAULT_MO;
    if (type) {
        tcg_gen_mb(type | TCG_BAR_SC);
    }
}

static TCGTemp *tci_extend_addr(TCGTemp *addr)
{
#ifdef CONFIG_TCG_INTERPRETER
    /*
     * 64-bit interpreter requires 64-bit addresses.
     * Compare to the extension performed by tcg_out_{ld,st}_helper_args
     * for native code generation.
     */
    if (tcg_ctx->addr_type == TCG_TYPE_I32) {
        TCGv_i64 temp = tcg_temp_ebb_new_i64();
        tcg_gen_extu_i32_i64(temp, temp_tcgv_i32(addr));
        return tcgv_i64_temp(temp);
    }
#endif
    return addr;
}

static void maybe_free_addr(TCGTemp *addr, TCGTemp *copy)
{
    if (addr != copy) {
        tcg_temp_free_internal(copy);
    }
}

/* Only required for loads, where value might overlap addr. */
static TCGv_i64 plugin_maybe_preserve_addr(TCGTemp *addr)
{
#ifdef CONFIG_PLUGIN
    if (tcg_ctx->plugin_insn != NULL) {
        /* Save a copy of the vaddr for use after a load.  */
        TCGv_i64 temp = tcg_temp_ebb_new_i64();
        if (tcg_ctx->addr_type == TCG_TYPE_I32) {
            tcg_gen_extu_i32_i64(temp, temp_tcgv_i32(addr));
        } else {
            tcg_gen_mov_i64(temp, temp_tcgv_i64(addr));
        }
        return temp;
    }
#endif
    return NULL;
}

#ifdef CONFIG_PLUGIN
static void
plugin_gen_mem_callbacks(TCGv_i64 copy_addr, TCGTemp *orig_addr, MemOpIdx oi,
                         enum qemu_plugin_mem_rw rw)
{
    qemu_plugin_meminfo_t info = make_plugin_meminfo(oi, rw);

    if (tcg_ctx->addr_type == TCG_TYPE_I32) {
        if (!copy_addr) {
            copy_addr = tcg_temp_ebb_new_i64();
            tcg_gen_extu_i32_i64(copy_addr, temp_tcgv_i32(orig_addr));
        }
        tcg_gen_plugin_mem_cb(copy_addr, info);
        tcg_temp_free_i64(copy_addr);
    } else {
        if (copy_addr) {
            tcg_gen_plugin_mem_cb(copy_addr, info);
            tcg_temp_free_i64(copy_addr);
        } else {
            tcg_gen_plugin_mem_cb(temp_tcgv_i64(orig_addr), info);
        }
    }
}
#endif

static void
plugin_gen_mem_callbacks_tmp(TCGType type, TCGTemp *val,
                             TCGv_i64 copy_addr, TCGTemp *orig_addr,
                             MemOpIdx oi, enum qemu_plugin_mem_rw rw)
{
#ifdef CONFIG_PLUGIN
    if (tcg_ctx->plugin_insn != NULL) {
        tcg_gen_st(type, val, tcgv_ptr_temp(tcg_env),
                   offsetof(CPUState, neg.plugin_mem_value_low)
                   - sizeof(CPUState)
                   + (HOST_BIG_ENDIAN && type == TCG_TYPE_I32 ? 4 : 0));
        plugin_gen_mem_callbacks(copy_addr, orig_addr, oi, rw);
    }
#endif
}

static void
plugin_gen_mem_callbacks_i128(TCGv_i128 val,
                             TCGv_i64 copy_addr, TCGTemp *orig_addr,
                             MemOpIdx oi, enum qemu_plugin_mem_rw rw)
{
#ifdef CONFIG_PLUGIN
    if (tcg_ctx->plugin_insn != NULL) {
        tcg_gen_st_i64(TCGV128_LOW(val), tcg_env,
                       offsetof(CPUState, neg.plugin_mem_value_low) -
                       sizeof(CPUState));
        tcg_gen_st_i64(TCGV128_HIGH(val), tcg_env,
                       offsetof(CPUState, neg.plugin_mem_value_high) -
                       sizeof(CPUState));
        plugin_gen_mem_callbacks(copy_addr, orig_addr, oi, rw);
    }
#endif
}

static void tcg_gen_bswap(TCGType type, TCGTemp *dst,
                          TCGTemp *src, unsigned flags, MemOp memop)
{
    switch (memop & MO_SIZE) {
    case MO_16:
        tcg_gen_bswap16(type, dst, src, flags);
        break;
    case MO_32:
        if (type == TCG_TYPE_I32) {
            tcg_gen_bswap32_i32(temp_tcgv_i32(dst), temp_tcgv_i32(src));
        } else {
            tcg_gen_bswap32_i64(temp_tcgv_i64(dst), temp_tcgv_i64(src), flags);
        }
        break;
    case MO_64:
        tcg_debug_assert(type == TCG_TYPE_I64);
        tcg_gen_bswap64_i64(temp_tcgv_i64(dst), temp_tcgv_i64(src));
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_gen_qemu_ld_int(TCGType type, TCGTemp *val,
                                TCGTemp *addr, TCGArg idx, MemOp memop)
{
    MemOp orig_memop;
    MemOpIdx orig_oi, oi;
    TCGv_i64 copy_addr;
    TCGTemp *addr_new;

    tcg_gen_req_mo(TCG_MO_LD_LD | TCG_MO_ST_LD);
    memop = tcg_canonicalize_memop(memop, type != TCG_TYPE_I32, 0);
    orig_memop = memop;
    orig_oi = oi = make_memop_idx(memop, idx);

    if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
        /* The bswap primitive benefits from zero-extended input.  */
        memop &= ~(MO_BSWAP | MO_SIGN);
        oi = make_memop_idx(memop, idx);
    }

    addr_new = tci_extend_addr(addr);
    copy_addr = plugin_maybe_preserve_addr(addr);
    tcg_gen_op_tti(INDEX_op_qemu_ld, type, val, addr_new, oi);

    if ((orig_memop ^ memop) & MO_BSWAP) {
        unsigned flags = (orig_memop & MO_SIGN
                          ? TCG_BSWAP_IZ | TCG_BSWAP_OS
                          : TCG_BSWAP_IZ | TCG_BSWAP_OZ);
        tcg_gen_bswap(type, val, val, flags, orig_memop);
    }

    plugin_gen_mem_callbacks_tmp(type, val, copy_addr, addr,
                                 orig_oi, QEMU_PLUGIN_MEM_R);
    maybe_free_addr(addr, addr_new);
}

void tcg_gen_qemu_ld_chk(TCGType val_type, TCGTemp *val, TCGTemp *addr,
                         unsigned idx, MemOp memop, TCGType addr_type)
{
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert(memop_size(memop) <= tcg_type_size(val_type));
    tcg_debug_assert(idx < NB_MMU_MODES);

    tcg_gen_qemu_ld_int(val_type, val, addr, idx, memop);
}

static void tcg_gen_qemu_st_int(TCGType type, TCGTemp *orig_val,
                                TCGTemp *addr, unsigned idx, MemOp memop)
{
    TCGTemp *val = orig_val;
    MemOpIdx orig_oi, oi;
    TCGTemp *addr_new;

    tcg_gen_req_mo(TCG_MO_LD_ST | TCG_MO_ST_ST);
    memop = tcg_canonicalize_memop(memop, type != TCG_TYPE_I32, 1);
    orig_oi = oi = make_memop_idx(memop, idx);

    if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
        val = tcg_temp_new_ebb(type);
        tcg_gen_bswap(type, val, orig_val, 0, memop);
        memop &= ~MO_BSWAP;
        oi = make_memop_idx(memop, idx);
    }

    addr_new = tci_extend_addr(addr);
    tcg_gen_op_tti(INDEX_op_qemu_st, type, val, addr_new, oi);
    plugin_gen_mem_callbacks_tmp(type, orig_val, NULL, addr,
                                 orig_oi, QEMU_PLUGIN_MEM_W);
    maybe_free_addr(addr, addr_new);

    if (val != orig_val) {
        tcg_temp_free_internal(val);
    }
}

void tcg_gen_qemu_st_chk(TCGType val_type, TCGTemp *val, TCGTemp *addr,
                         unsigned idx, MemOp memop, TCGType addr_type)
{
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert(memop_size(memop) <= tcg_type_size(val_type));
    tcg_debug_assert(idx < NB_MMU_MODES);

    tcg_gen_qemu_st_int(val_type, val, addr, idx, memop);
}

/*
 * Return true if @mop, without knowledge of the pointer alignment,
 * does not require 16-byte atomicity, and it would be adventagous
 * to avoid a call to a helper function.
 */
static bool use_two_i64_for_i128(MemOp mop)
{
    /* Two softmmu tlb lookups is larger than one function call. */
    if (tcg_use_softmmu) {
        return false;
    }

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
        return false;
    default:
        g_assert_not_reached();
    }
}

static void canonicalize_memop_i128_as_i64(MemOp ret[2], MemOp orig)
{
    MemOp mop_1 = orig, mop_2;

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

static TCGv_i64 maybe_extend_addr64(TCGTemp *addr)
{
    if (tcg_ctx->addr_type == TCG_TYPE_I32) {
        TCGv_i64 a64 = tcg_temp_ebb_new_i64();
        tcg_gen_extu_i32_i64(a64, temp_tcgv_i32(addr));
        return a64;
    }
    return temp_tcgv_i64(addr);
}

static void maybe_free_addr64(TCGv_i64 a64)
{
    if (tcg_ctx->addr_type == TCG_TYPE_I32) {
        tcg_temp_free_i64(a64);
    }
}

static void tcg_gen_qemu_ld_i128_int(TCGv_i128 val, TCGTemp *addr,
                                     TCGArg idx, MemOp memop)
{
    MemOpIdx orig_oi;
    TCGv_i64 ext_addr = NULL;
    TCGTemp *addr_new;

    check_max_alignment(memop_alignment_bits(memop));
    tcg_gen_req_mo(TCG_MO_LD_LD | TCG_MO_ST_LD);

    /* In serial mode, reduce atomicity. */
    if (!(tcg_ctx->gen_tb->cflags & CF_PARALLEL)) {
        memop &= ~MO_ATOM_MASK;
        memop |= MO_ATOM_NONE;
    }
    orig_oi = make_memop_idx(memop, idx);

    /* TODO: For now, force 32-bit hosts to use the helper. */
    if (TCG_TARGET_HAS_qemu_ldst_i128) {
        TCGv_i64 lo, hi;
        bool need_bswap = false;
        MemOpIdx oi = orig_oi;

        if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
            lo = TCGV128_HIGH(val);
            hi = TCGV128_LOW(val);
            oi = make_memop_idx(memop & ~MO_BSWAP, idx);
            need_bswap = true;
        } else {
            lo = TCGV128_LOW(val);
            hi = TCGV128_HIGH(val);
        }

        addr_new = tci_extend_addr(addr);
        gen_ldst2(INDEX_op_qemu_ld2, TCG_TYPE_I128, tcgv_i64_temp(lo),
                  tcgv_i64_temp(hi), addr_new, oi);
        maybe_free_addr(addr, addr_new);

        if (need_bswap) {
            tcg_gen_bswap64_i64(lo, lo);
            tcg_gen_bswap64_i64(hi, hi);
        }
    } else if (use_two_i64_for_i128(memop)) {
        MemOp mop[2];
        TCGTemp *addr_p8;
        TCGv_i64 x, y;
        bool need_bswap;

        canonicalize_memop_i128_as_i64(mop, memop);
        need_bswap = (mop[0] ^ memop) & MO_BSWAP;

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

        addr_new = tci_extend_addr(addr);
        gen_ld_i64(x, addr_new, make_memop_idx(mop[0], idx));
        maybe_free_addr(addr, addr_new);

        if (need_bswap) {
            tcg_gen_bswap64_i64(x, x);
        }

        if (tcg_ctx->addr_type == TCG_TYPE_I32) {
            TCGv_i32 t = tcg_temp_ebb_new_i32();
            tcg_gen_addi_i32(t, temp_tcgv_i32(addr), 8);
            addr_p8 = tcgv_i32_temp(t);
        } else {
            TCGv_i64 t = tcg_temp_ebb_new_i64();
            tcg_gen_addi_i64(t, temp_tcgv_i64(addr), 8);
            addr_p8 = tcgv_i64_temp(t);
        }

        addr_new = tci_extend_addr(addr_p8);
        gen_ld_i64(y, addr_new, make_memop_idx(mop[1], idx));
        maybe_free_addr(addr_p8, addr_new);
        tcg_temp_free_internal(addr_p8);

        if (need_bswap) {
            tcg_gen_bswap64_i64(y, y);
        }
    } else {
        if (tcg_ctx->addr_type == TCG_TYPE_I32) {
            ext_addr = tcg_temp_ebb_new_i64();
            tcg_gen_extu_i32_i64(ext_addr, temp_tcgv_i32(addr));
            addr = tcgv_i64_temp(ext_addr);
        }
        gen_helper_ld_i128(val, tcg_env, temp_tcgv_i64(addr),
                           tcg_constant_i32(orig_oi));
    }

    plugin_gen_mem_callbacks_i128(val, ext_addr, addr, orig_oi,
                                  QEMU_PLUGIN_MEM_R);
}

void tcg_gen_qemu_ld_chk_i128(TCGv_i128 val, TCGTemp *addr, TCGArg idx,
                              MemOp memop, TCGType addr_type)
{
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert((memop & MO_SIZE) == MO_128);
    tcg_debug_assert((memop & MO_SIGN) == 0);
    tcg_gen_qemu_ld_i128_int(val, addr, idx, memop);
}

static void tcg_gen_qemu_st_i128_int(TCGv_i128 val, TCGTemp *addr,
                                     TCGArg idx, MemOp memop)
{
    MemOpIdx orig_oi;
    TCGv_i64 ext_addr = NULL;
    TCGTemp *addr_new;

    check_max_alignment(memop_alignment_bits(memop));
    tcg_gen_req_mo(TCG_MO_ST_LD | TCG_MO_ST_ST);

    /* In serial mode, reduce atomicity. */
    if (!(tcg_ctx->gen_tb->cflags & CF_PARALLEL)) {
        memop &= ~MO_ATOM_MASK;
        memop |= MO_ATOM_NONE;
    }
    orig_oi = make_memop_idx(memop, idx);

    /* TODO: For now, force 32-bit hosts to use the helper. */

    if (TCG_TARGET_HAS_qemu_ldst_i128) {
        TCGv_i64 lo, hi;
        MemOpIdx oi = orig_oi;
        bool need_bswap = false;

        if ((memop & MO_BSWAP) && !tcg_target_has_memory_bswap(memop)) {
            lo = tcg_temp_ebb_new_i64();
            hi = tcg_temp_ebb_new_i64();
            tcg_gen_bswap64_i64(lo, TCGV128_HIGH(val));
            tcg_gen_bswap64_i64(hi, TCGV128_LOW(val));
            oi = make_memop_idx(memop & ~MO_BSWAP, idx);
            need_bswap = true;
        } else {
            lo = TCGV128_LOW(val);
            hi = TCGV128_HIGH(val);
        }

        addr_new = tci_extend_addr(addr);
        gen_ldst2(INDEX_op_qemu_st2, TCG_TYPE_I128,
                  tcgv_i64_temp(lo), tcgv_i64_temp(hi), addr_new, oi);
        maybe_free_addr(addr, addr_new);

        if (need_bswap) {
            tcg_temp_free_i64(lo);
            tcg_temp_free_i64(hi);
        }
    } else if (use_two_i64_for_i128(memop)) {
        MemOp mop[2];
        TCGTemp *addr_p8;
        TCGv_i64 x, y, b = NULL;

        canonicalize_memop_i128_as_i64(mop, memop);

        if ((memop & MO_BSWAP) == MO_LE) {
            x = TCGV128_LOW(val);
            y = TCGV128_HIGH(val);
        } else {
            x = TCGV128_HIGH(val);
            y = TCGV128_LOW(val);
        }

        if ((mop[0] ^ memop) & MO_BSWAP) {
            b = tcg_temp_ebb_new_i64();
            tcg_gen_bswap64_i64(b, x);
            x = b;
        }

        addr_new = tci_extend_addr(addr);
        gen_st_i64(x, addr_new, make_memop_idx(mop[0], idx));
        maybe_free_addr(addr, addr_new);

        if (tcg_ctx->addr_type == TCG_TYPE_I32) {
            TCGv_i32 t = tcg_temp_ebb_new_i32();
            tcg_gen_addi_i32(t, temp_tcgv_i32(addr), 8);
            addr_p8 = tcgv_i32_temp(t);
        } else {
            TCGv_i64 t = tcg_temp_ebb_new_i64();
            tcg_gen_addi_i64(t, temp_tcgv_i64(addr), 8);
            addr_p8 = tcgv_i64_temp(t);
        }

        addr_new = tci_extend_addr(addr_p8);
        if (b) {
            tcg_gen_bswap64_i64(b, y);
            gen_st_i64(b, addr_new, make_memop_idx(mop[1], idx));
            tcg_temp_free_i64(b);
        } else {
            gen_st_i64(y, addr_new, make_memop_idx(mop[1], idx));
        }
        maybe_free_addr(addr_p8, addr_new);
        tcg_temp_free_internal(addr_p8);
    } else {
        if (tcg_ctx->addr_type == TCG_TYPE_I32) {
            ext_addr = tcg_temp_ebb_new_i64();
            tcg_gen_extu_i32_i64(ext_addr, temp_tcgv_i32(addr));
            addr = tcgv_i64_temp(ext_addr);
        }
        gen_helper_st_i128(tcg_env, temp_tcgv_i64(addr), val,
                           tcg_constant_i32(orig_oi));
    }

    plugin_gen_mem_callbacks_i128(val, ext_addr, addr, orig_oi,
                                  QEMU_PLUGIN_MEM_W);
}

void tcg_gen_qemu_st_chk_i128(TCGv_i128 val, TCGTemp *addr, TCGArg idx,
                              MemOp memop, TCGType addr_type)
{
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert((memop & MO_SIZE) == MO_128);
    tcg_debug_assert((memop & MO_SIGN) == 0);
    tcg_gen_qemu_st_i128_int(val, addr, idx, memop);
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
typedef void (*gen_atomic_op_i128)(TCGv_i128, TCGv_env, TCGv_i64,
                                   TCGv_i128, TCGv_i32);

#if HAVE_CMPXCHG128
# define WITH_ATOMIC128(X) X,
#else
# define WITH_ATOMIC128(X)
#endif

static void tcg_gen_nonatomic_cmpxchg_int(TCGType type, TCGTemp *retv,
                                          TCGTemp *addr, TCGTemp *cmpv,
                                          TCGTemp *newv, unsigned idx,
                                          MemOp memop)
{
    g_autoptr(TCGTemp) t1 = tcg_temp_new_ebb(type);
    g_autoptr(TCGTemp) t2 = tcg_temp_new_ebb(type);

    tcg_gen_ext(type, t2, cmpv, memop & MO_SIZE);

    tcg_gen_qemu_ld_int(type, t1, addr, idx, memop & ~MO_SIGN);
    tcg_gen_movcond(type, TCG_COND_EQ, t2, t1, t2, newv, t1);
    tcg_gen_qemu_st_int(type, t2, addr, idx, memop);

    if (memop & MO_SIGN) {
        tcg_gen_ext(type, retv, t1, memop);
    } else {
        tcg_gen_mov(type, retv, t1);
    }
}

void tcg_gen_nonatomic_cmpxchg_chk(TCGType val_type, TCGTemp *retv,
                                   TCGTemp *addr, TCGTemp *cmpv, TCGTemp *newv,
                                   unsigned idx, MemOp memop, TCGType addr_type)
{
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert(memop_size(memop) <= tcg_type_size(val_type));
    tcg_debug_assert(idx < NB_MMU_MODES);

    tcg_gen_nonatomic_cmpxchg_int(val_type, retv, addr, cmpv, newv, idx, memop);
}

static void tcg_gen_atomic_cmpxchg_int(TCGType type, TCGTemp *retv,
                                       TCGTemp *addr, TCGTemp *cmpv,
                                       TCGTemp *newv, unsigned idx,
                                       MemOp orig_memop)
{
    MemOp memop = orig_memop;
    TCGv_i64 a64;
    MemOpIdx oi;

    if (!(tcg_ctx->gen_tb->cflags & CF_PARALLEL)) {
        tcg_gen_nonatomic_cmpxchg_int(type, retv, addr, cmpv,
                                      newv, idx, memop);
        return;
    }

    a64 = maybe_extend_addr64(addr);

    if ((memop & MO_SIZE) == MO_64) {
        gen_atomic_cx_i64 gen;

        tcg_debug_assert(type == TCG_TYPE_I64);

        memop = tcg_canonicalize_memop(memop, 1, 0);
        oi = make_memop_idx(memop & ~MO_SIGN, idx);

        gen = ((memop & MO_BSWAP) == MO_LE
               ? gen_helper_atomic_cmpxchgq_le
               : gen_helper_atomic_cmpxchgq_be);

        gen(temp_tcgv_i64(retv), tcg_env, a64, temp_tcgv_i64(cmpv),
            temp_tcgv_i64(newv), tcg_constant_i32(oi));
    } else {
        static gen_atomic_cx_i32 const table_cmpxchg[(MO_SIZE | MO_BSWAP) + 1] = {
            [MO_8] = gen_helper_atomic_cmpxchgb,
            [MO_16 | MO_LE] = gen_helper_atomic_cmpxchgw_le,
            [MO_16 | MO_BE] = gen_helper_atomic_cmpxchgw_be,
            [MO_32 | MO_LE] = gen_helper_atomic_cmpxchgl_le,
            [MO_32 | MO_BE] = gen_helper_atomic_cmpxchgl_be,
        };

        g_autoptr(TCGTemp) c32 = NULL;
        g_autoptr(TCGTemp) n32 = NULL;
        g_autoptr(TCGTemp) r32 = NULL;
        TCGTemp *retv_orig = retv;
        gen_atomic_cx_i32 gen;

        if (type == TCG_TYPE_I64) {
            c32 = tcg_temp_new_ebb(TCG_TYPE_I32);
            n32 = tcg_temp_new_ebb(TCG_TYPE_I32);
            r32 = tcg_temp_new_ebb(TCG_TYPE_I32);

            tcg_gen_extrl(c32, cmpv);
            tcg_gen_extrl(n32, newv);

            cmpv = c32;
            newv = n32;
            retv = r32;
        }

        memop = tcg_canonicalize_memop(memop, 0, 0);
        oi = make_memop_idx(memop & ~MO_SIGN, idx);

        gen = table_cmpxchg[memop & (MO_SIZE | MO_BSWAP)];
        tcg_debug_assert(gen != NULL);

        gen(temp_tcgv_i32(retv), tcg_env, a64, temp_tcgv_i32(cmpv),
            temp_tcgv_i32(newv), tcg_constant_i32(oi));

        if (memop & MO_SIGN) {
            tcg_gen_ext(TCG_TYPE_I32, retv, retv, memop);
        }
        if (retv != retv_orig) {
            if (orig_memop & MO_SIGN) {
                tcg_gen_exts(retv_orig, retv);
            } else {
                tcg_gen_extu(retv_orig, retv);
            }
        }
    }
    maybe_free_addr64(a64);
}

void tcg_gen_atomic_cmpxchg_chk(TCGType val_type, TCGTemp *retv, TCGTemp *addr,
                                TCGTemp *cmpv, TCGTemp *newv,
                                unsigned idx, MemOp memop, TCGType addr_type)
{
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert(memop_size(memop) <= tcg_type_size(val_type));
    tcg_debug_assert(idx < NB_MMU_MODES);

    tcg_gen_atomic_cmpxchg_int(val_type, retv, addr, cmpv, newv, idx, memop);
}

static void tcg_gen_nonatomic_cmpxchg_i128_int(TCGv_i128 retv, TCGTemp *addr,
                                               TCGv_i128 cmpv, TCGv_i128 newv,
                                               TCGArg idx, MemOp memop)
{
    TCGv_i128 oldv = tcg_temp_ebb_new_i128();
    TCGv_i128 tmpv = tcg_temp_ebb_new_i128();
    TCGv_i64 t0 = tcg_temp_ebb_new_i64();
    TCGv_i64 t1 = tcg_temp_ebb_new_i64();
    TCGv_i64 z = tcg_constant_i64(0);

    tcg_gen_qemu_ld_i128_int(oldv, addr, idx, memop);

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
    tcg_gen_qemu_st_i128_int(tmpv, addr, idx, memop);
    tcg_gen_mov_i128(retv, oldv);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i128(tmpv);
    tcg_temp_free_i128(oldv);
}

void tcg_gen_nonatomic_cmpxchg_chk_i128(TCGv_i128 retv, TCGTemp *addr,
                                        TCGv_i128 cmpv, TCGv_i128 newv,
                                        TCGArg idx, MemOp memop,
                                        TCGType addr_type)
{
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert((memop & (MO_SIZE | MO_SIGN)) == MO_128);
    tcg_gen_nonatomic_cmpxchg_i128_int(retv, addr, cmpv, newv, idx, memop);
}

static void tcg_gen_atomic_cmpxchg_int_i128(TCGv_i128 retv, TCGTemp *addr,
                                            TCGv_i128 cmpv, TCGv_i128 newv,
                                            TCGArg idx, MemOp memop)
{
    gen_atomic_cx_i128 gen = NULL;

    if (!(tcg_ctx->gen_tb->cflags & CF_PARALLEL)) {
        tcg_gen_nonatomic_cmpxchg_i128_int(retv, addr, cmpv, newv, idx, memop);
        return;
    }

#if HAVE_CMPXCHG128
    gen = ((memop & MO_BSWAP) == MO_LE
           ? gen_helper_atomic_cmpxchgo_le
           : gen_helper_atomic_cmpxchgo_be);
#endif

    if (gen) {
        MemOpIdx oi = make_memop_idx(memop, idx);
        TCGv_i64 a64 = maybe_extend_addr64(addr);
        gen(retv, tcg_env, a64, cmpv, newv, tcg_constant_i32(oi));
        maybe_free_addr64(a64);
        return;
    }

    gen_helper_exit_atomic(tcg_env);

    /*
     * Produce a result for a well-formed opcode stream.  This satisfies
     * liveness for set before used, which happens before this dead code
     * is removed.
     */
    tcg_gen_movi_i64(TCGV128_LOW(retv), 0);
    tcg_gen_movi_i64(TCGV128_HIGH(retv), 0);
}

void tcg_gen_atomic_cmpxchg_chk_i128(TCGv_i128 retv, TCGTemp *addr,
                                     TCGv_i128 cmpv, TCGv_i128 newv,
                                     TCGArg idx, MemOp memop,
                                     TCGType addr_type)
{
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);
    tcg_debug_assert((memop & (MO_SIZE | MO_SIGN)) == MO_128);
    tcg_gen_atomic_cmpxchg_int_i128(retv, addr, cmpv, newv, idx, memop);
}

static void do_nonatomic_op(TCGType type, TCGTemp *ret, TCGTemp *addr,
                            TCGTemp *val, unsigned idx, MemOp memop,
                            bool new_val,
                            void (*gen)(TCGType, TCGTemp *,
                                        TCGTemp *, TCGTemp *))
{
    g_autoptr(TCGTemp) t1 = tcg_temp_new_ebb(type);
    g_autoptr(TCGTemp) t2 = tcg_temp_new_ebb(type);

    memop = tcg_canonicalize_memop(memop, type != TCG_TYPE_I32, 0);

    tcg_gen_qemu_ld_int(type, t1, addr, idx, memop);
    tcg_gen_ext(type, t2, val, memop);
    gen(type, t2, t1, t2);
    tcg_gen_qemu_st_int(type, t2, addr, idx, memop);

    tcg_gen_ext(type, ret, (new_val ? t2 : t1), memop);
}

static void do_atomic_op(TCGType type, TCGTemp *ret, TCGTemp *addr,
                         TCGTemp *val, unsigned idx, MemOp orig_memop,
                         void * const table[])
{
    TCGv_i64 a64 = maybe_extend_addr64(addr);
    MemOp memop = orig_memop;
    MemOpIdx oi;

    if ((memop & MO_SIZE) == MO_64) {
        gen_atomic_op_i64 gen;

        tcg_debug_assert(type == TCG_TYPE_I64);

        memop = tcg_canonicalize_memop(memop, 1, 0);
        oi = make_memop_idx(memop & ~MO_SIGN, idx);

        gen = table[memop & (MO_SIZE | MO_BSWAP)];
        tcg_debug_assert(gen != NULL);

        gen(temp_tcgv_i64(ret), tcg_env, a64,
            temp_tcgv_i64(val), tcg_constant_i32(oi));
    } else {
        g_autoptr(TCGTemp) v32 = NULL;
        g_autoptr(TCGTemp) r32 = NULL;
        TCGTemp *ret_orig = ret;
        gen_atomic_op_i32 gen;

        if (type == TCG_TYPE_I64) {
            v32 = tcg_temp_new_ebb(TCG_TYPE_I32);
            r32 = tcg_temp_new_ebb(TCG_TYPE_I32);

            tcg_gen_extrl(v32, val);
            val = v32;
            ret = r32;
        }

        memop = tcg_canonicalize_memop(memop, 0, 0);
        oi = make_memop_idx(memop & ~MO_SIGN, idx);

        gen = table[memop & (MO_SIZE | MO_BSWAP)];
        tcg_debug_assert(gen != NULL);

        gen(temp_tcgv_i32(ret), tcg_env, a64,
            temp_tcgv_i32(val), tcg_constant_i32(oi));

        if (memop & MO_SIGN) {
            tcg_gen_ext(TCG_TYPE_I32, ret, ret, memop);
        }
        if (ret != ret_orig) {
            if (orig_memop & MO_SIGN) {
                tcg_gen_exts(ret_orig, ret);
            } else {
                tcg_gen_extu(ret_orig, ret);
            }
        }
    }
    maybe_free_addr64(a64);
}

static void do_nonatomic_op_i128(TCGv_i128 ret, TCGTemp *addr, TCGv_i128 val,
                                 TCGArg idx, MemOp memop, bool new_val,
                                 void (*gen)(TCGType, TCGTemp *,
                                             TCGTemp *, TCGTemp *))
{
    TCGv_i128 t = tcg_temp_ebb_new_i128();
    TCGv_i128 r = tcg_temp_ebb_new_i128();

    tcg_gen_qemu_ld_i128_int(r, addr, idx, memop);
    gen(TCG_TYPE_I64, tcgv_i64_temp(TCGV128_LOW(t)),
        tcgv_i64_temp(TCGV128_LOW(r)), tcgv_i64_temp(TCGV128_LOW(val)));
    gen(TCG_TYPE_I64, tcgv_i64_temp(TCGV128_HIGH(t)),
        tcgv_i64_temp(TCGV128_HIGH(r)), tcgv_i64_temp(TCGV128_HIGH(val)));
    tcg_gen_qemu_st_i128_int(t, addr, idx, memop);

    tcg_gen_mov_i128(ret, r);
    tcg_temp_free_i128(t);
    tcg_temp_free_i128(r);
}

static void do_atomic_op_i128(TCGv_i128 ret, TCGTemp *addr, TCGv_i128 val,
                              TCGArg idx, MemOp memop, void * const table[])
{
    gen_atomic_op_i128 gen = table[memop & (MO_SIZE | MO_BSWAP)];

    if (gen) {
        MemOpIdx oi = make_memop_idx(memop & ~MO_SIGN, idx);
        TCGv_i64 a64 = maybe_extend_addr64(addr);
        gen(ret, tcg_env, a64, val, tcg_constant_i32(oi));
        maybe_free_addr64(a64);
        return;
    }

    gen_helper_exit_atomic(tcg_env);
    /* Produce a result */
    tcg_gen_movi_i64(TCGV128_LOW(ret), 0);
    tcg_gen_movi_i64(TCGV128_HIGH(ret), 0);
}

#define GEN_ATOMIC_HELPER128(NAME, OP, NEW)                             \
static void * const table_##NAME[(MO_SIZE | MO_BSWAP) + 1] = {          \
    [MO_8] = gen_helper_atomic_##NAME##b,                               \
    [MO_16 | MO_LE] = gen_helper_atomic_##NAME##w_le,                   \
    [MO_16 | MO_BE] = gen_helper_atomic_##NAME##w_be,                   \
    [MO_32 | MO_LE] = gen_helper_atomic_##NAME##l_le,                   \
    [MO_32 | MO_BE] = gen_helper_atomic_##NAME##l_be,                   \
    [MO_64 | MO_LE] = gen_helper_atomic_##NAME##q_le,                   \
    [MO_64 | MO_BE] = gen_helper_atomic_##NAME##q_be,                   \
    WITH_ATOMIC128([MO_128 | MO_LE] = gen_helper_atomic_##NAME##o_le)   \
    WITH_ATOMIC128([MO_128 | MO_BE] = gen_helper_atomic_##NAME##o_be)   \
};                                                                      \
void tcg_gen_atomic_##NAME##_chk(TCGType val_type, TCGTemp *ret,        \
                                 TCGTemp *addr, TCGTemp *val, unsigned idx, \
                                 MemOp memop, TCGType addr_type)        \
{                                                                       \
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);                  \
    tcg_debug_assert(memop_size(memop) <= tcg_type_size(val_type));     \
    tcg_debug_assert(idx < NB_MMU_MODES);                               \
    if (tcg_ctx->gen_tb->cflags & CF_PARALLEL) {                        \
        do_atomic_op(val_type, ret, addr, val, idx, memop, table_##NAME); \
    } else {                                                            \
        do_nonatomic_op(val_type, ret, addr, val, idx, memop, NEW,      \
                        tcg_gen_##OP);                                  \
    }                                                                   \
}                                                                       \
void tcg_gen_atomic_##NAME##_chk_i128(TCGv_i128 ret, TCGTemp *addr,     \
                                      TCGv_i128 val, TCGArg idx,        \
                                      MemOp memop, TCGType addr_type)   \
{                                                                       \
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);                  \
    tcg_debug_assert((memop & MO_SIZE) == MO_128);                      \
    if (tcg_ctx->gen_tb->cflags & CF_PARALLEL) {                        \
        do_atomic_op_i128(ret, addr, val, idx, memop, table_##NAME);    \
    } else {                                                            \
        do_nonatomic_op_i128(ret, addr, val, idx, memop, NEW,           \
                             tcg_gen_##OP);                             \
    }                                                                   \
}

#define GEN_ATOMIC_HELPER(NAME, OP, NEW)                                \
static void * const table_##NAME[(MO_SIZE | MO_BSWAP) + 1] = {          \
    [MO_8] = gen_helper_atomic_##NAME##b,                               \
    [MO_16 | MO_LE] = gen_helper_atomic_##NAME##w_le,                   \
    [MO_16 | MO_BE] = gen_helper_atomic_##NAME##w_be,                   \
    [MO_32 | MO_LE] = gen_helper_atomic_##NAME##l_le,                   \
    [MO_32 | MO_BE] = gen_helper_atomic_##NAME##l_be,                   \
    [MO_64 | MO_LE] = gen_helper_atomic_##NAME##q_le,                   \
    [MO_64 | MO_BE] = gen_helper_atomic_##NAME##q_be,                   \
};                                                                      \
void tcg_gen_atomic_##NAME##_chk(TCGType val_type, TCGTemp *ret,        \
                                 TCGTemp *addr, TCGTemp *val, unsigned idx, \
                                 MemOp memop, TCGType addr_type)        \
{                                                                       \
    tcg_debug_assert(addr_type == tcg_ctx->addr_type);                  \
    tcg_debug_assert(memop_size(memop) <= tcg_type_size(val_type));     \
    tcg_debug_assert(idx < NB_MMU_MODES);                               \
    if (tcg_ctx->gen_tb->cflags & CF_PARALLEL) {                        \
        do_atomic_op(val_type, ret, addr, val, idx, memop, table_##NAME); \
    } else {                                                            \
        do_nonatomic_op(val_type, ret, addr, val, idx, memop, NEW,      \
                        tcg_gen_##OP);                                  \
    }                                                                   \
}

GEN_ATOMIC_HELPER(fetch_add, add, 0)
GEN_ATOMIC_HELPER128(fetch_and, and, 0)
GEN_ATOMIC_HELPER128(fetch_or, or, 0)
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

static void tcg_gen_mov2(TCGType type, TCGTemp *r, TCGTemp *a, TCGTemp *b)
{
    tcg_gen_mov(type, r, b);
}

GEN_ATOMIC_HELPER128(xchg, mov2, 0)

#undef GEN_ATOMIC_HELPER
#undef GEN_ATOMIC_HELPER128
