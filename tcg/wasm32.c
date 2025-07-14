/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * WebAssembly backend with forked TCI, based on tci.c
 *
 * Copyright (c) 2009, 2011, 2016 Stefan Weil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "tcg/tcg.h"
#include "tcg/helper-info.h"
#include "tcg/tcg-ldst.h"
#include "disas/dis-asm.h"
#include "tcg-has.h"
#include <ffi.h>
#include <emscripten.h>
#include "wasm32.h"


#define ctpop_tr    glue(ctpop, TCG_TARGET_REG_BITS)
#define deposit_tr  glue(deposit, TCG_TARGET_REG_BITS)
#define extract_tr  glue(extract, TCG_TARGET_REG_BITS)
#define sextract_tr glue(sextract, TCG_TARGET_REG_BITS)

/*
 * Enable TCI assertions only when debugging TCG (and without NDEBUG defined).
 * Without assertions, the interpreter runs much faster.
 */
#if defined(CONFIG_DEBUG_TCG)
# define tci_assert(cond) assert(cond)
#else
# define tci_assert(cond) ((void)(cond))
#endif

__thread uintptr_t tci_tb_ptr;

/* TBs executed more than this value will be compiled to wasm */
#define INSTANTIATE_NUM 1500

#define EM_JS_PRE(ret, name, args, body...) EM_JS(ret, name, args, body)

EM_JS_PRE(int, instantiate_wasm, (int wasm_begin,
                              int wasm_size,
                              int import_vec_begin,
                              int import_vec_size),
{
    const memory_v = new DataView(HEAP8.buffer);
    const wasm = HEAP8.subarray(wasm_begin, wasm_begin + wasm_size);
    var helper = {};
    helper.u = () => {
        return (Asyncify.state != Asyncify.State.Unwinding) ? 1 : 0;
    };
    const entsize = TCG_TARGET_REG_BITS / 8;
    for (var i = 0; i < import_vec_size / entsize; i++) {
        const p = import_vec_begin + i * entsize;
#if TCG_TARGET_REG_BITS == 32
        const idx = memory_v.getInt32(p, true);
#elif WASM64_MEMORY64 == 2
        const idx = Number(memory_v.getBigInt64(p, true));
#else
        const idx = memory_v.getBigInt64(p, true);
#endif
        helper[i] = wasmTable.get(idx);
    }
    const mod = new WebAssembly.Module(new Uint8Array(wasm));
    const inst = new WebAssembly.Instance(mod, {
            "env" : {
                "buffer" : wasmMemory,
            },
            "helper" : helper,
    });

    Module.__wasm32_tb.inst_gc_registry.register(inst, "tbinstance");

    return addFunction(inst.exports.start, 'ii');
});

static void tci_write_reg64(tcg_target_ulong *regs, uint32_t high_index,
                            uint32_t low_index, uint64_t value)
{
    regs[low_index] = (uint32_t)value;
    regs[high_index] = value >> 32;
}

/* Create a 64 bit value from two 32 bit values. */
static uint64_t tci_uint64(uint32_t high, uint32_t low)
{
    return ((uint64_t)high << 32) + low;
}

/*
 * Load sets of arguments all at once.  The naming convention is:
 *   tci_args_<arguments>
 * where arguments is a sequence of
 *
 *   b = immediate (bit position)
 *   c = condition (TCGCond)
 *   i = immediate (uint32_t)
 *   I = immediate (tcg_target_ulong)
 *   l = label or pointer
 *   m = immediate (MemOpIdx)
 *   n = immediate (call return length)
 *   r = register
 *   s = signed ldst offset
 */

static void tci_args_l(uint32_t insn, const void *tb_ptr, void **l0)
{
    int diff = sextract32(insn, 12, 20);
    *l0 = diff ? (void *)tb_ptr + diff : NULL;
}

static void tci_args_r(uint32_t insn, TCGReg *r0)
{
    *r0 = extract32(insn, 8, 4);
}

static void tci_args_nl(uint32_t insn, const void *tb_ptr,
                        uint8_t *n0, void **l1)
{
    *n0 = extract32(insn, 8, 4);
    *l1 = sextract32(insn, 12, 20) + (void *)tb_ptr;
}

static void tci_args_rl(uint32_t insn, const void *tb_ptr,
                        TCGReg *r0, void **l1)
{
    *r0 = extract32(insn, 8, 4);
    *l1 = sextract32(insn, 12, 20) + (void *)tb_ptr;
}

static void tci_args_rr(uint32_t insn, TCGReg *r0, TCGReg *r1)
{
    *r0 = extract32(insn, 8, 4);
    *r1 = extract32(insn, 12, 4);
}

static void tci_args_ri(uint32_t insn, TCGReg *r0, tcg_target_ulong *i1)
{
    *r0 = extract32(insn, 8, 4);
    *i1 = sextract32(insn, 12, 20);
}

static void tci_args_rrm(uint32_t insn, TCGReg *r0,
                         TCGReg *r1, MemOpIdx *m2)
{
    *r0 = extract32(insn, 8, 4);
    *r1 = extract32(insn, 12, 4);
    *m2 = extract32(insn, 16, 16);
}

static void tci_args_rrr(uint32_t insn, TCGReg *r0, TCGReg *r1, TCGReg *r2)
{
    *r0 = extract32(insn, 8, 4);
    *r1 = extract32(insn, 12, 4);
    *r2 = extract32(insn, 16, 4);
}

static void tci_args_rrs(uint32_t insn, TCGReg *r0, TCGReg *r1, int32_t *i2)
{
    *r0 = extract32(insn, 8, 4);
    *r1 = extract32(insn, 12, 4);
    *i2 = sextract32(insn, 16, 16);
}

static void tci_args_rrbb(uint32_t insn, TCGReg *r0, TCGReg *r1,
                          uint8_t *i2, uint8_t *i3)
{
    *r0 = extract32(insn, 8, 4);
    *r1 = extract32(insn, 12, 4);
    *i2 = extract32(insn, 16, 6);
    *i3 = extract32(insn, 22, 6);
}

static void tci_args_rrrc(uint32_t insn,
                          TCGReg *r0, TCGReg *r1, TCGReg *r2, TCGCond *c3)
{
    *r0 = extract32(insn, 8, 4);
    *r1 = extract32(insn, 12, 4);
    *r2 = extract32(insn, 16, 4);
    *c3 = extract32(insn, 20, 4);
}

static void tci_args_rrrbb(uint32_t insn, TCGReg *r0, TCGReg *r1,
                           TCGReg *r2, uint8_t *i3, uint8_t *i4)
{
    *r0 = extract32(insn, 8, 4);
    *r1 = extract32(insn, 12, 4);
    *r2 = extract32(insn, 16, 4);
    *i3 = extract32(insn, 20, 6);
    *i4 = extract32(insn, 26, 6);
}

static void tci_args_rrrr(uint32_t insn,
                          TCGReg *r0, TCGReg *r1, TCGReg *r2, TCGReg *r3)
{
    *r0 = extract32(insn, 8, 4);
    *r1 = extract32(insn, 12, 4);
    *r2 = extract32(insn, 16, 4);
    *r3 = extract32(insn, 20, 4);
}

static void tci_args_rrrrrc(uint32_t insn, TCGReg *r0, TCGReg *r1,
                            TCGReg *r2, TCGReg *r3, TCGReg *r4, TCGCond *c5)
{
    *r0 = extract32(insn, 8, 4);
    *r1 = extract32(insn, 12, 4);
    *r2 = extract32(insn, 16, 4);
    *r3 = extract32(insn, 20, 4);
    *r4 = extract32(insn, 24, 4);
    *c5 = extract32(insn, 28, 4);
}

static bool tci_compare32(uint32_t u0, uint32_t u1, TCGCond condition)
{
    bool result = false;
    int32_t i0 = u0;
    int32_t i1 = u1;
    switch (condition) {
    case TCG_COND_EQ:
        result = (u0 == u1);
        break;
    case TCG_COND_NE:
        result = (u0 != u1);
        break;
    case TCG_COND_LT:
        result = (i0 < i1);
        break;
    case TCG_COND_GE:
        result = (i0 >= i1);
        break;
    case TCG_COND_LE:
        result = (i0 <= i1);
        break;
    case TCG_COND_GT:
        result = (i0 > i1);
        break;
    case TCG_COND_LTU:
        result = (u0 < u1);
        break;
    case TCG_COND_GEU:
        result = (u0 >= u1);
        break;
    case TCG_COND_LEU:
        result = (u0 <= u1);
        break;
    case TCG_COND_GTU:
        result = (u0 > u1);
        break;
    case TCG_COND_TSTEQ:
        result = (u0 & u1) == 0;
        break;
    case TCG_COND_TSTNE:
        result = (u0 & u1) != 0;
        break;
    default:
        g_assert_not_reached();
    }
    return result;
}

static bool tci_compare64(uint64_t u0, uint64_t u1, TCGCond condition)
{
    bool result = false;
    int64_t i0 = u0;
    int64_t i1 = u1;
    switch (condition) {
    case TCG_COND_EQ:
        result = (u0 == u1);
        break;
    case TCG_COND_NE:
        result = (u0 != u1);
        break;
    case TCG_COND_LT:
        result = (i0 < i1);
        break;
    case TCG_COND_GE:
        result = (i0 >= i1);
        break;
    case TCG_COND_LE:
        result = (i0 <= i1);
        break;
    case TCG_COND_GT:
        result = (i0 > i1);
        break;
    case TCG_COND_LTU:
        result = (u0 < u1);
        break;
    case TCG_COND_GEU:
        result = (u0 >= u1);
        break;
    case TCG_COND_LEU:
        result = (u0 <= u1);
        break;
    case TCG_COND_GTU:
        result = (u0 > u1);
        break;
    case TCG_COND_TSTEQ:
        result = (u0 & u1) == 0;
        break;
    case TCG_COND_TSTNE:
        result = (u0 & u1) != 0;
        break;
    default:
        g_assert_not_reached();
    }
    return result;
}

static uint64_t tci_qemu_ld(CPUArchState *env, uint64_t taddr,
                            MemOpIdx oi, const void *tb_ptr)
{
    MemOp mop = get_memop(oi);
    uintptr_t ra = (uintptr_t)tb_ptr;

    switch (mop & MO_SSIZE) {
    case MO_UB:
        return helper_ldub_mmu(env, taddr, oi, ra);
    case MO_SB:
        return helper_ldsb_mmu(env, taddr, oi, ra);
    case MO_UW:
        return helper_lduw_mmu(env, taddr, oi, ra);
    case MO_SW:
        return helper_ldsw_mmu(env, taddr, oi, ra);
    case MO_UL:
        return helper_ldul_mmu(env, taddr, oi, ra);
    case MO_SL:
        return helper_ldsl_mmu(env, taddr, oi, ra);
    case MO_UQ:
        return helper_ldq_mmu(env, taddr, oi, ra);
    default:
        g_assert_not_reached();
    }
}

static void tci_qemu_st(CPUArchState *env, uint64_t taddr, uint64_t val,
                        MemOpIdx oi, const void *tb_ptr)
{
    MemOp mop = get_memop(oi);
    uintptr_t ra = (uintptr_t)tb_ptr;

    switch (mop & MO_SIZE) {
    case MO_UB:
        helper_stb_mmu(env, taddr, val, oi, ra);
        break;
    case MO_UW:
        helper_stw_mmu(env, taddr, val, oi, ra);
        break;
    case MO_UL:
        helper_stl_mmu(env, taddr, val, oi, ra);
        break;
    case MO_UQ:
        helper_stq_mmu(env, taddr, val, oi, ra);
        break;
    default:
        g_assert_not_reached();
    }
}

__thread int thread_idx;

static inline int32_t get_counter_local(void *tb_ptr)
{
    return get_counter(tb_ptr, thread_idx);
}

static inline void set_counter_local(void *tb_ptr, int v)
{
    set_counter(tb_ptr, thread_idx, v);
}

static inline struct wasmInstanceInfo *get_info_local(void *tb_ptr)
{
    return get_info(tb_ptr, thread_idx);
}

static inline void set_info_local(void *tb_ptr, struct wasmInstanceInfo *info)
{
    set_info(tb_ptr, thread_idx, info);
}

__thread struct wasmContext ctx = {
    .tb_ptr = 0,
    .stack = NULL,
    .do_init = 1,
    .buf128 = NULL,
};

/* Interpret pseudo code in tb. */
/*
 * Disable CFI checks.
 * One possible operation in the pseudo code is a call to binary code.
 * Therefore, disable CFI checks in the interpreter function
 */
static uintptr_t QEMU_DISABLE_CFI tcg_qemu_tb_exec_tci(CPUArchState *env)
{
    uint32_t *tb_ptr = get_tci_ptr(ctx.tb_ptr);
    tcg_target_ulong regs[TCG_TARGET_NB_REGS];
    uint64_t stack[(TCG_STATIC_CALL_ARGS_SIZE + TCG_STATIC_FRAME_SIZE)
                   / sizeof(uint64_t)];
    bool carry = false;

    regs[TCG_AREG0] = (tcg_target_ulong)env;
    regs[TCG_REG_CALL_STACK] = (uintptr_t)stack;
    tci_assert(tb_ptr);

    for (;;) {
        uint32_t insn;
        TCGOpcode opc;
        TCGReg r0, r1, r2, r3, r4;
        tcg_target_ulong t1;
        TCGCond condition;
        uint8_t pos, len;
        uint32_t tmp32;
        uint64_t tmp64, taddr;
        MemOpIdx oi;
        int32_t ofs;
        void *ptr;
        int32_t counter;

        insn = *tb_ptr++;
        opc = extract32(insn, 0, 8);

        switch (opc) {
        case INDEX_op_call:
            {
                void *call_slots[MAX_CALL_IARGS];
                ffi_cif *cif;
                void *func;
                unsigned i, s, n;

                tci_args_nl(insn, tb_ptr, &len, &ptr);
                func = ((void **)ptr)[0];
                cif = ((void **)ptr)[1];

                n = cif->nargs;
                for (i = s = 0; i < n; ++i) {
                    ffi_type *t = cif->arg_types[i];
                    call_slots[i] = &stack[s];
                    s += DIV_ROUND_UP(t->size, 8);
                }

                /* Helper functions may need to access the "return address" */
                tci_tb_ptr = (uintptr_t)tb_ptr;
                ffi_call(cif, func, stack, call_slots);
            }

            switch (len) {
            case 0: /* void */
                break;
            case 1: /* uint32_t */
                /*
                 * The result winds up "left-aligned" in the stack[0] slot.
                 * Note that libffi has an odd special case in that it will
                 * always widen an integral result to ffi_arg.
                 */
                if (sizeof(ffi_arg) == 8) {
                    regs[TCG_REG_R0] = (uint32_t)stack[0];
                } else {
                    regs[TCG_REG_R0] = *(uint32_t *)stack;
                }
                break;
            case 2: /* uint64_t */
                /*
                 * For TCG_TARGET_REG_BITS == 32, the register pair
                 * must stay in host memory order.
                 */
                memcpy(&regs[TCG_REG_R0], stack, 8);
                break;
            case 3: /* Int128 */
                memcpy(&regs[TCG_REG_R0], stack, 16);
                break;
            default:
                g_assert_not_reached();
            }
            break;

        case INDEX_op_br:
            tci_args_l(insn, tb_ptr, &ptr);
            tb_ptr = ptr;
            continue;
#if TCG_TARGET_REG_BITS == 32
        case INDEX_op_setcond2_i32:
            tci_args_rrrrrc(insn, &r0, &r1, &r2, &r3, &r4, &condition);
            regs[r0] = tci_compare64(tci_uint64(regs[r2], regs[r1]),
                                     tci_uint64(regs[r4], regs[r3]),
                                     condition);
            break;
#elif TCG_TARGET_REG_BITS == 64
        case INDEX_op_setcond:
            tci_args_rrrc(insn, &r0, &r1, &r2, &condition);
            regs[r0] = tci_compare64(regs[r1], regs[r2], condition);
            break;
        case INDEX_op_movcond:
            tci_args_rrrrrc(insn, &r0, &r1, &r2, &r3, &r4, &condition);
            tmp32 = tci_compare64(regs[r1], regs[r2], condition);
            regs[r0] = regs[tmp32 ? r3 : r4];
            break;
#endif
        case INDEX_op_mov:
            tci_args_rr(insn, &r0, &r1);
            regs[r0] = regs[r1];
            break;
        case INDEX_op_tci_movi:
            tci_args_ri(insn, &r0, &t1);
            regs[r0] = t1;
            break;
        case INDEX_op_tci_movl:
            tci_args_rl(insn, tb_ptr, &r0, &ptr);
            regs[r0] = *(tcg_target_ulong *)ptr;
            break;
        case INDEX_op_tci_setcarry:
            carry = true;
            break;

            /* Load/store operations (32 bit). */

        case INDEX_op_ld8u:
            tci_args_rrs(insn, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(uint8_t *)ptr;
            break;
        case INDEX_op_ld8s:
            tci_args_rrs(insn, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(int8_t *)ptr;
            break;
        case INDEX_op_ld16u:
            tci_args_rrs(insn, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(uint16_t *)ptr;
            break;
        case INDEX_op_ld16s:
            tci_args_rrs(insn, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(int16_t *)ptr;
            break;
        case INDEX_op_ld:
            tci_args_rrs(insn, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(tcg_target_ulong *)ptr;
            break;
        case INDEX_op_st8:
            tci_args_rrs(insn, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            *(uint8_t *)ptr = regs[r0];
            break;
        case INDEX_op_st16:
            tci_args_rrs(insn, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            *(uint16_t *)ptr = regs[r0];
            break;
        case INDEX_op_st:
            tci_args_rrs(insn, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            *(tcg_target_ulong *)ptr = regs[r0];
            break;

            /* Arithmetic operations (mixed 32/64 bit). */

        case INDEX_op_add:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] + regs[r2];
            break;
        case INDEX_op_sub:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] - regs[r2];
            break;
        case INDEX_op_mul:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] * regs[r2];
            break;
        case INDEX_op_and:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] & regs[r2];
            break;
        case INDEX_op_or:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] | regs[r2];
            break;
        case INDEX_op_xor:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] ^ regs[r2];
            break;
        case INDEX_op_andc:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] & ~regs[r2];
            break;
        case INDEX_op_orc:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] | ~regs[r2];
            break;
        case INDEX_op_eqv:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = ~(regs[r1] ^ regs[r2]);
            break;
        case INDEX_op_nand:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = ~(regs[r1] & regs[r2]);
            break;
        case INDEX_op_nor:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = ~(regs[r1] | regs[r2]);
            break;
        case INDEX_op_neg:
            tci_args_rr(insn, &r0, &r1);
            regs[r0] = -regs[r1];
            break;
        case INDEX_op_not:
            tci_args_rr(insn, &r0, &r1);
            regs[r0] = ~regs[r1];
            break;
        case INDEX_op_ctpop:
            tci_args_rr(insn, &r0, &r1);
            regs[r0] = ctpop_tr(regs[r1]);
            break;
        case INDEX_op_addco:
            tci_args_rrr(insn, &r0, &r1, &r2);
            t1 = regs[r1] + regs[r2];
            carry = t1 < regs[r1];
            regs[r0] = t1;
            break;
        case INDEX_op_addci:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] + regs[r2] + carry;
            break;
        case INDEX_op_addcio:
            tci_args_rrr(insn, &r0, &r1, &r2);
            if (carry) {
                t1 = regs[r1] + regs[r2] + 1;
                carry = t1 <= regs[r1];
            } else {
                t1 = regs[r1] + regs[r2];
                carry = t1 < regs[r1];
            }
            regs[r0] = t1;
            break;
        case INDEX_op_subbo:
            tci_args_rrr(insn, &r0, &r1, &r2);
            carry = regs[r1] < regs[r2];
            regs[r0] = regs[r1] - regs[r2];
            break;
        case INDEX_op_subbi:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] - regs[r2] - carry;
            break;
        case INDEX_op_subbio:
            tci_args_rrr(insn, &r0, &r1, &r2);
            if (carry) {
                carry = regs[r1] <= regs[r2];
                regs[r0] = regs[r1] - regs[r2] - 1;
            } else {
                carry = regs[r1] < regs[r2];
                regs[r0] = regs[r1] - regs[r2];
            }
            break;
        case INDEX_op_muls2:
            tci_args_rrrr(insn, &r0, &r1, &r2, &r3);
#if TCG_TARGET_REG_BITS == 32
            tmp64 = (int64_t)(int32_t)regs[r2] * (int32_t)regs[r3];
            tci_write_reg64(regs, r1, r0, tmp64);
#else
            muls64(&regs[r0], &regs[r1], regs[r2], regs[r3]);
#endif
            break;
        case INDEX_op_mulu2:
            tci_args_rrrr(insn, &r0, &r1, &r2, &r3);
#if TCG_TARGET_REG_BITS == 32
            tmp64 = (uint64_t)(uint32_t)regs[r2] * (uint32_t)regs[r3];
            tci_write_reg64(regs, r1, r0, tmp64);
#else
            mulu64(&regs[r0], &regs[r1], regs[r2], regs[r3]);
#endif
            break;

            /* Arithmetic operations (32 bit). */

        case INDEX_op_tci_divs32:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = (int32_t)regs[r1] / (int32_t)regs[r2];
            break;
        case INDEX_op_tci_divu32:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = (uint32_t)regs[r1] / (uint32_t)regs[r2];
            break;
        case INDEX_op_tci_rems32:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = (int32_t)regs[r1] % (int32_t)regs[r2];
            break;
        case INDEX_op_tci_remu32:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = (uint32_t)regs[r1] % (uint32_t)regs[r2];
            break;
        case INDEX_op_tci_clz32:
            tci_args_rrr(insn, &r0, &r1, &r2);
            tmp32 = regs[r1];
            regs[r0] = tmp32 ? clz32(tmp32) : regs[r2];
            break;
        case INDEX_op_tci_ctz32:
            tci_args_rrr(insn, &r0, &r1, &r2);
            tmp32 = regs[r1];
            regs[r0] = tmp32 ? ctz32(tmp32) : regs[r2];
            break;
        case INDEX_op_tci_setcond32:
            tci_args_rrrc(insn, &r0, &r1, &r2, &condition);
            regs[r0] = tci_compare32(regs[r1], regs[r2], condition);
            break;
        case INDEX_op_tci_movcond32:
            tci_args_rrrrrc(insn, &r0, &r1, &r2, &r3, &r4, &condition);
            tmp32 = tci_compare32(regs[r1], regs[r2], condition);
            regs[r0] = regs[tmp32 ? r3 : r4];
            break;

            /* Shift/rotate operations. */

        case INDEX_op_shl:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] << (regs[r2] % TCG_TARGET_REG_BITS);
            break;
        case INDEX_op_shr:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] >> (regs[r2] % TCG_TARGET_REG_BITS);
            break;
        case INDEX_op_sar:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = ((tcg_target_long)regs[r1]
                        >> (regs[r2] % TCG_TARGET_REG_BITS));
            break;
        case INDEX_op_tci_rotl32:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = rol32(regs[r1], regs[r2] & 31);
            break;
        case INDEX_op_tci_rotr32:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = ror32(regs[r1], regs[r2] & 31);
            break;
        case INDEX_op_deposit:
            tci_args_rrrbb(insn, &r0, &r1, &r2, &pos, &len);
            regs[r0] = deposit_tr(regs[r1], pos, len, regs[r2]);
            break;
        case INDEX_op_extract:
            tci_args_rrbb(insn, &r0, &r1, &pos, &len);
            regs[r0] = extract_tr(regs[r1], pos, len);
            break;
        case INDEX_op_sextract:
            tci_args_rrbb(insn, &r0, &r1, &pos, &len);
            regs[r0] = sextract_tr(regs[r1], pos, len);
            break;
        case INDEX_op_brcond:
            tci_args_rl(insn, tb_ptr, &r0, &ptr);
            if (regs[r0]) {
                tb_ptr = ptr;
            }
            break;
        case INDEX_op_bswap16:
            tci_args_rr(insn, &r0, &r1);
            regs[r0] = bswap16(regs[r1]);
            break;
        case INDEX_op_bswap32:
            tci_args_rr(insn, &r0, &r1);
            regs[r0] = bswap32(regs[r1]);
            break;
#if TCG_TARGET_REG_BITS == 64
            /* Load/store operations (64 bit). */

        case INDEX_op_ld32u:
            tci_args_rrs(insn, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(uint32_t *)ptr;
            break;
        case INDEX_op_ld32s:
            tci_args_rrs(insn, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(int32_t *)ptr;
            break;
        case INDEX_op_st32:
            tci_args_rrs(insn, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            *(uint32_t *)ptr = regs[r0];
            break;

            /* Arithmetic operations (64 bit). */

        case INDEX_op_divs:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = (int64_t)regs[r1] / (int64_t)regs[r2];
            break;
        case INDEX_op_divu:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = (uint64_t)regs[r1] / (uint64_t)regs[r2];
            break;
        case INDEX_op_rems:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = (int64_t)regs[r1] % (int64_t)regs[r2];
            break;
        case INDEX_op_remu:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = (uint64_t)regs[r1] % (uint64_t)regs[r2];
            break;
        case INDEX_op_clz:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] ? clz64(regs[r1]) : regs[r2];
            break;
        case INDEX_op_ctz:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = regs[r1] ? ctz64(regs[r1]) : regs[r2];
            break;

            /* Shift/rotate operations (64 bit). */

        case INDEX_op_rotl:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = rol64(regs[r1], regs[r2] & 63);
            break;
        case INDEX_op_rotr:
            tci_args_rrr(insn, &r0, &r1, &r2);
            regs[r0] = ror64(regs[r1], regs[r2] & 63);
            break;
        case INDEX_op_ext_i32_i64:
            tci_args_rr(insn, &r0, &r1);
            regs[r0] = (int32_t)regs[r1];
            break;
        case INDEX_op_extu_i32_i64:
            tci_args_rr(insn, &r0, &r1);
            regs[r0] = (uint32_t)regs[r1];
            break;
        case INDEX_op_bswap64:
            tci_args_rr(insn, &r0, &r1);
            regs[r0] = bswap64(regs[r1]);
            break;
#endif /* TCG_TARGET_REG_BITS == 64 */

            /* QEMU specific operations. */

        case INDEX_op_exit_tb:
            tci_args_l(insn, tb_ptr, &ptr);
            ctx.tb_ptr = 0;
            return (uintptr_t)ptr;

        case INDEX_op_goto_tb:
            tci_args_l(insn, tb_ptr, &ptr);
            if (*(uint32_t **)ptr != tb_ptr) {
                tb_ptr = *(uint32_t **)ptr;
                ctx.tb_ptr = tb_ptr;
                counter = get_counter_local(tb_ptr);
                if ((counter >= 0) && (counter < INSTANTIATE_NUM)) {
                    set_counter_local(tb_ptr, counter + 1);
                } else {
                    return 0; /* enter to wasm TB */
                }
                tb_ptr = get_tci_ptr(tb_ptr);
            }
            break;

        case INDEX_op_goto_ptr:
            tci_args_r(insn, &r0);
            ptr = (void *)regs[r0];
            if (!ptr) {
                ctx.tb_ptr = 0;
                return 0;
            }
            tb_ptr = ptr;
            ctx.tb_ptr = tb_ptr;
            counter = get_counter_local(tb_ptr);
            if ((counter >= 0) && (counter < INSTANTIATE_NUM)) {
                set_counter_local(tb_ptr, counter + 1);
            } else {
                return 0; /* enter to wasm TB */
            }
            tb_ptr = get_tci_ptr(tb_ptr);
            break;

        case INDEX_op_qemu_ld:
            tci_args_rrm(insn, &r0, &r1, &oi);
            taddr = regs[r1];
            regs[r0] = tci_qemu_ld(env, taddr, oi, tb_ptr);
            break;

        case INDEX_op_qemu_st:
            tci_args_rrm(insn, &r0, &r1, &oi);
            taddr = regs[r1];
            tci_qemu_st(env, taddr, regs[r0], oi, tb_ptr);
            break;

        case INDEX_op_qemu_ld2:
            tcg_debug_assert(TCG_TARGET_REG_BITS == 32);
            tci_args_rrrr(insn, &r0, &r1, &r2, &r3);
            taddr = regs[r2];
            oi = regs[r3];
            tmp64 = tci_qemu_ld(env, taddr, oi, tb_ptr);
            tci_write_reg64(regs, r1, r0, tmp64);
            break;

        case INDEX_op_qemu_st2:
            tcg_debug_assert(TCG_TARGET_REG_BITS == 32);
            tci_args_rrrr(insn, &r0, &r1, &r2, &r3);
            tmp64 = tci_uint64(regs[r1], regs[r0]);
            taddr = regs[r2];
            oi = regs[r3];
            tci_qemu_st(env, taddr, tmp64, oi, tb_ptr);
            break;

        case INDEX_op_mb:
            /* Ensure ordering for all kinds */
            smp_mb();
            break;
        default:
            g_assert_not_reached();
        }
    }
}

/*
 * TODO: Disassembler is not implemented
 */

/*
 * Max number of instances can exist simultaneously.
 *
 * If the number of instances reaches this and a new instance needs to be
 * created, old instances are removed so that new instances can be created
 * without hitting the browser's limit.
 */
#define MAX_INSTANCES 15000

int instances_global;

/* Avoid overwrapping of begin/end pointers */
#define INSTANCES_BUF_MAX (MAX_INSTANCES + 1)

__thread struct wasmInstanceInfo instances[INSTANCES_BUF_MAX];
__thread int instances_begin;
__thread int instances_end;

static void add_instance(wasm_tb_func tb_func, void *tb_ptr)
{
    instances[instances_end].tb_func = tb_func;
    instances[instances_end].tb_ptr = tb_ptr;
    set_info_local(tb_ptr, &(instances[instances_end]));
    instances_end  = (instances_end + 1) % INSTANCES_BUF_MAX;

    qatomic_inc(&instances_global);
}

__thread int instance_pending_gc;
__thread int instance_done_gc;

static void remove_old_instances(void)
{
    int num;
    if (instance_pending_gc > 0) {
        return;
    }
    if (instances_begin <= instances_end) {
        num = instances_end - instances_begin;
    } else {
        num = instances_end + (INSTANCES_BUF_MAX - instances_begin);
    }
    /* removes the half of the oldest instances in the buffer */
    num /= 2;
    for (int i = 0; i < num; i++) {
        EM_ASM({ removeFunction($0); }, instances[instances_begin].tb_func);
        instances[instances_begin].tb_ptr = NULL;
        instances_begin = (instances_begin + 1) % INSTANCES_BUF_MAX;
    }
    instance_pending_gc += num;
}

static bool can_add_instance(void)
{
    return qatomic_read(&instances_global) < MAX_INSTANCES;
}

static wasm_tb_func get_instance_from_tb(void *tb_ptr)
{
    struct wasmInstanceInfo *elm = get_info_local(tb_ptr);
    if (elm == NULL) {
        return NULL;
    }
    if (elm->tb_ptr != tb_ptr) {
        /*
         * This TB was instantiated but has been removed. Set counter to the
         * max value so that this will be instantiated again at the next
         * invocation.
         */
        set_counter_local(tb_ptr, INSTANTIATE_NUM);
        set_info_local(tb_ptr, NULL);
        return NULL;
    }
    return elm->tb_func;
}

static void check_instance_garbage_collected(void)
{
    if (instance_done_gc > 0) {
        qatomic_sub(&instances_global, instance_done_gc);
        instance_pending_gc -= instance_done_gc;
        instance_done_gc = 0;
    }
}

EM_JS_PRE(void, init_wasm32_js, (void *instance_done_gc_ptr),
{
    Module.__wasm32_tb = {
        inst_gc_registry: new FinalizationRegistry((i) => {
            if (i == "tbinstance") {
                const memory_v = new DataView(HEAP8.buffer);
                let v = memory_v.getInt32(instance_done_gc_ptr, true);
                memory_v.setInt32(instance_done_gc_ptr, v + 1, true);
            }
        })
    };
});

#define MAX_EXEC_NUM 50000
__thread int exec_cnt = MAX_EXEC_NUM;
static inline void trysleep(void)
{
    /*
     * Even during running TBs continuously, try to return the control
     * to the browser periodically and allow browsers doing tasks.
     */
    if (--exec_cnt == 0) {
        if (!can_add_instance()) {
            emscripten_sleep(0);
            check_instance_garbage_collected();
        }
        exec_cnt = MAX_EXEC_NUM;
    }
}

int thread_idx_max;

static void init_wasm32(void)
{
    thread_idx = qatomic_fetch_inc(&thread_idx_max);
    ctx.stack = g_malloc(TCG_STATIC_CALL_ARGS_SIZE + TCG_STATIC_FRAME_SIZE);
    ctx.buf128 = g_malloc(16);
    ctx.tci_tb_ptr = (uint32_t *)&tci_tb_ptr;
    init_wasm32_js(&instance_done_gc);
}

__thread bool initdone;

uintptr_t tcg_qemu_tb_exec(CPUArchState *env, const void *v_tb_ptr)
{
    if (!initdone) {
        init_wasm32();
        initdone = true;
    }
    ctx.env = env;
    ctx.tb_ptr = (void *)v_tb_ptr;
    while (true) {
        trysleep();
        struct wasmTBHeader *header = (struct wasmTBHeader *)ctx.tb_ptr;
        int32_t counter = get_counter_local(header);
        uintptr_t res;
        /* res = tcg_qemu_tb_exec_tci(env); */
        wasm_tb_func tb_func = get_instance_from_tb(ctx.tb_ptr);
        if (tb_func) {
            /*
             * call the instance if available
             */
            res = call_wasm_tb(tb_func, &ctx);
        } else if (counter < INSTANTIATE_NUM) {
            /*
             * run it on TCI if the counter value is small
             */
            set_counter_local(ctx.tb_ptr, counter + 1);
            res = tcg_qemu_tb_exec_tci(env);
        } else if (!can_add_instance()) {
            /*
             * too many instances has been created, try removing older
             * instances and keep running this TB on TCI
             */
            remove_old_instances();
            check_instance_garbage_collected();
            res = tcg_qemu_tb_exec_tci(env);
        } else {
            /*
             * instantiate and run TB as Wasm
             */
            tb_func = (wasm_tb_func)instantiate_wasm((int)header->wasm_ptr,
                                                     header->wasm_size,
                                                     (int)header->import_ptr,
                                                     header->import_size);
            add_instance(tb_func, ctx.tb_ptr);
            res = call_wasm_tb(tb_func, &ctx);
        }
        if (!ctx.tb_ptr) {
            return res;
        }
    }
}
