/*
 * Tiny Code Interpreter for QEMU
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

/* Enable TCI assertions only when debugging TCG (and without NDEBUG defined).
 * Without assertions, the interpreter runs much faster. */
#if defined(CONFIG_DEBUG_TCG)
# define tci_assert(cond) assert(cond)
#else
# define tci_assert(cond) ((void)0)
#endif

#include "qemu-common.h"
#include "tcg/tcg.h"           /* MAX_OPC_PARAM_IARGS */
#include "exec/cpu_ldst.h"
#include "tcg/tcg-op.h"
#include "qemu/compiler.h"

#if MAX_OPC_PARAM_IARGS != 6
# error Fix needed, number of supported input arguments changed!
#endif
#if TCG_TARGET_REG_BITS == 32
typedef uint64_t (*helper_function)(tcg_target_ulong, tcg_target_ulong,
                                    tcg_target_ulong, tcg_target_ulong,
                                    tcg_target_ulong, tcg_target_ulong,
                                    tcg_target_ulong, tcg_target_ulong,
                                    tcg_target_ulong, tcg_target_ulong,
                                    tcg_target_ulong, tcg_target_ulong);
#else
typedef uint64_t (*helper_function)(tcg_target_ulong, tcg_target_ulong,
                                    tcg_target_ulong, tcg_target_ulong,
                                    tcg_target_ulong, tcg_target_ulong);
#endif

__thread uintptr_t tci_tb_ptr;

static tcg_target_ulong tci_read_reg(const tcg_target_ulong *regs, TCGReg index)
{
    tci_assert(index < TCG_TARGET_NB_REGS);
    return regs[index];
}

static void
tci_write_reg(tcg_target_ulong *regs, TCGReg index, tcg_target_ulong value)
{
    tci_assert(index < TCG_TARGET_NB_REGS);
    tci_assert(index != TCG_AREG0);
    tci_assert(index != TCG_REG_CALL_STACK);
    regs[index] = value;
}

#if TCG_TARGET_REG_BITS == 32
static void tci_write_reg64(tcg_target_ulong *regs, uint32_t high_index,
                            uint32_t low_index, uint64_t value)
{
    tci_write_reg(regs, low_index, value);
    tci_write_reg(regs, high_index, value >> 32);
}
#endif

#if TCG_TARGET_REG_BITS == 32
/* Create a 64 bit value from two 32 bit values. */
static uint64_t tci_uint64(uint32_t high, uint32_t low)
{
    return ((uint64_t)high << 32) + low;
}
#endif

/* Read constant byte from bytecode. */
static uint8_t tci_read_b(const uint8_t **tb_ptr)
{
    return *(tb_ptr[0]++);
}

/* Read register number from bytecode. */
static TCGReg tci_read_r(const uint8_t **tb_ptr)
{
    uint8_t regno = tci_read_b(tb_ptr);
    tci_assert(regno < TCG_TARGET_NB_REGS);
    return regno;
}

/* Read constant (native size) from bytecode. */
static tcg_target_ulong tci_read_i(const uint8_t **tb_ptr)
{
    tcg_target_ulong value = *(const tcg_target_ulong *)(*tb_ptr);
    *tb_ptr += sizeof(value);
    return value;
}

/* Read unsigned constant (32 bit) from bytecode. */
static uint32_t tci_read_i32(const uint8_t **tb_ptr)
{
    uint32_t value = *(const uint32_t *)(*tb_ptr);
    *tb_ptr += sizeof(value);
    return value;
}

/* Read signed constant (32 bit) from bytecode. */
static int32_t tci_read_s32(const uint8_t **tb_ptr)
{
    int32_t value = *(const int32_t *)(*tb_ptr);
    *tb_ptr += sizeof(value);
    return value;
}

#if TCG_TARGET_REG_BITS == 64
/* Read constant (64 bit) from bytecode. */
static uint64_t tci_read_i64(const uint8_t **tb_ptr)
{
    uint64_t value = *(const uint64_t *)(*tb_ptr);
    *tb_ptr += sizeof(value);
    return value;
}
#endif

/* Read indexed register (native size) from bytecode. */
static tcg_target_ulong
tci_read_rval(const tcg_target_ulong *regs, const uint8_t **tb_ptr)
{
    tcg_target_ulong value = tci_read_reg(regs, **tb_ptr);
    *tb_ptr += 1;
    return value;
}

#if TCG_TARGET_REG_BITS == 32
/* Read two indexed registers (2 * 32 bit) from bytecode. */
static uint64_t tci_read_r64(const tcg_target_ulong *regs,
                             const uint8_t **tb_ptr)
{
    uint32_t low = tci_read_rval(regs, tb_ptr);
    return tci_uint64(tci_read_rval(regs, tb_ptr), low);
}
#elif TCG_TARGET_REG_BITS == 64
/* Read indexed register (64 bit) from bytecode. */
static uint64_t tci_read_r64(const tcg_target_ulong *regs,
                             const uint8_t **tb_ptr)
{
    return tci_read_rval(regs, tb_ptr);
}
#endif

/* Read indexed register(s) with target address from bytecode. */
static target_ulong
tci_read_ulong(const tcg_target_ulong *regs, const uint8_t **tb_ptr)
{
    target_ulong taddr = tci_read_rval(regs, tb_ptr);
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
    taddr += (uint64_t)tci_read_rval(regs, tb_ptr) << 32;
#endif
    return taddr;
}

static tcg_target_ulong tci_read_label(const uint8_t **tb_ptr)
{
    tcg_target_ulong label = tci_read_i(tb_ptr);
    tci_assert(label != 0);
    return label;
}

/*
 * Load sets of arguments all at once.  The naming convention is:
 *   tci_args_<arguments>
 * where arguments is a sequence of
 *
 *   r = register
 *   s = signed ldst offset
 */

static void tci_args_rr(const uint8_t **tb_ptr,
                        TCGReg *r0, TCGReg *r1)
{
    *r0 = tci_read_r(tb_ptr);
    *r1 = tci_read_r(tb_ptr);
}

static void tci_args_rrr(const uint8_t **tb_ptr,
                         TCGReg *r0, TCGReg *r1, TCGReg *r2)
{
    *r0 = tci_read_r(tb_ptr);
    *r1 = tci_read_r(tb_ptr);
    *r2 = tci_read_r(tb_ptr);
}

static void tci_args_rrs(const uint8_t **tb_ptr,
                         TCGReg *r0, TCGReg *r1, int32_t *i2)
{
    *r0 = tci_read_r(tb_ptr);
    *r1 = tci_read_r(tb_ptr);
    *i2 = tci_read_s32(tb_ptr);
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
    default:
        g_assert_not_reached();
    }
    return result;
}

#define qemu_ld_ub \
    cpu_ldub_mmuidx_ra(env, taddr, get_mmuidx(oi), (uintptr_t)tb_ptr)
#define qemu_ld_leuw \
    cpu_lduw_le_mmuidx_ra(env, taddr, get_mmuidx(oi), (uintptr_t)tb_ptr)
#define qemu_ld_leul \
    cpu_ldl_le_mmuidx_ra(env, taddr, get_mmuidx(oi), (uintptr_t)tb_ptr)
#define qemu_ld_leq \
    cpu_ldq_le_mmuidx_ra(env, taddr, get_mmuidx(oi), (uintptr_t)tb_ptr)
#define qemu_ld_beuw \
    cpu_lduw_be_mmuidx_ra(env, taddr, get_mmuidx(oi), (uintptr_t)tb_ptr)
#define qemu_ld_beul \
    cpu_ldl_be_mmuidx_ra(env, taddr, get_mmuidx(oi), (uintptr_t)tb_ptr)
#define qemu_ld_beq \
    cpu_ldq_be_mmuidx_ra(env, taddr, get_mmuidx(oi), (uintptr_t)tb_ptr)
#define qemu_st_b(X) \
    cpu_stb_mmuidx_ra(env, taddr, X, get_mmuidx(oi), (uintptr_t)tb_ptr)
#define qemu_st_lew(X) \
    cpu_stw_le_mmuidx_ra(env, taddr, X, get_mmuidx(oi), (uintptr_t)tb_ptr)
#define qemu_st_lel(X) \
    cpu_stl_le_mmuidx_ra(env, taddr, X, get_mmuidx(oi), (uintptr_t)tb_ptr)
#define qemu_st_leq(X) \
    cpu_stq_le_mmuidx_ra(env, taddr, X, get_mmuidx(oi), (uintptr_t)tb_ptr)
#define qemu_st_bew(X) \
    cpu_stw_be_mmuidx_ra(env, taddr, X, get_mmuidx(oi), (uintptr_t)tb_ptr)
#define qemu_st_bel(X) \
    cpu_stl_be_mmuidx_ra(env, taddr, X, get_mmuidx(oi), (uintptr_t)tb_ptr)
#define qemu_st_beq(X) \
    cpu_stq_be_mmuidx_ra(env, taddr, X, get_mmuidx(oi), (uintptr_t)tb_ptr)

#if TCG_TARGET_REG_BITS == 64
# define CASE_32_64(x) \
        case glue(glue(INDEX_op_, x), _i64): \
        case glue(glue(INDEX_op_, x), _i32):
# define CASE_64(x) \
        case glue(glue(INDEX_op_, x), _i64):
#else
# define CASE_32_64(x) \
        case glue(glue(INDEX_op_, x), _i32):
# define CASE_64(x)
#endif

/* Interpret pseudo code in tb. */
/*
 * Disable CFI checks.
 * One possible operation in the pseudo code is a call to binary code.
 * Therefore, disable CFI checks in the interpreter function
 */
uintptr_t QEMU_DISABLE_CFI tcg_qemu_tb_exec(CPUArchState *env,
                                            const void *v_tb_ptr)
{
    const uint8_t *tb_ptr = v_tb_ptr;
    tcg_target_ulong regs[TCG_TARGET_NB_REGS];
    long tcg_temps[CPU_TEMP_BUF_NLONGS];
    uintptr_t sp_value = (uintptr_t)(tcg_temps + CPU_TEMP_BUF_NLONGS);
    uintptr_t ret = 0;

    regs[TCG_AREG0] = (tcg_target_ulong)env;
    regs[TCG_REG_CALL_STACK] = sp_value;
    tci_assert(tb_ptr);

    for (;;) {
        TCGOpcode opc = tb_ptr[0];
#if defined(CONFIG_DEBUG_TCG) && !defined(NDEBUG)
        uint8_t op_size = tb_ptr[1];
        const uint8_t *old_code_ptr = tb_ptr;
#endif
        TCGReg r0, r1, r2;
        tcg_target_ulong t0;
        tcg_target_ulong t1;
        tcg_target_ulong t2;
        tcg_target_ulong label;
        TCGCond condition;
        target_ulong taddr;
        uint8_t tmp8;
        uint16_t tmp16;
        uint32_t tmp32;
        uint64_t tmp64;
#if TCG_TARGET_REG_BITS == 32
        uint64_t v64;
#endif
        TCGMemOpIdx oi;
        int32_t ofs;
        void *ptr;

        /* Skip opcode and size entry. */
        tb_ptr += 2;

        switch (opc) {
        case INDEX_op_call:
            t0 = tci_read_i(&tb_ptr);
            tci_tb_ptr = (uintptr_t)tb_ptr;
#if TCG_TARGET_REG_BITS == 32
            tmp64 = ((helper_function)t0)(tci_read_reg(regs, TCG_REG_R0),
                                          tci_read_reg(regs, TCG_REG_R1),
                                          tci_read_reg(regs, TCG_REG_R2),
                                          tci_read_reg(regs, TCG_REG_R3),
                                          tci_read_reg(regs, TCG_REG_R4),
                                          tci_read_reg(regs, TCG_REG_R5),
                                          tci_read_reg(regs, TCG_REG_R6),
                                          tci_read_reg(regs, TCG_REG_R7),
                                          tci_read_reg(regs, TCG_REG_R8),
                                          tci_read_reg(regs, TCG_REG_R9),
                                          tci_read_reg(regs, TCG_REG_R10),
                                          tci_read_reg(regs, TCG_REG_R11));
            tci_write_reg(regs, TCG_REG_R0, tmp64);
            tci_write_reg(regs, TCG_REG_R1, tmp64 >> 32);
#else
            tmp64 = ((helper_function)t0)(tci_read_reg(regs, TCG_REG_R0),
                                          tci_read_reg(regs, TCG_REG_R1),
                                          tci_read_reg(regs, TCG_REG_R2),
                                          tci_read_reg(regs, TCG_REG_R3),
                                          tci_read_reg(regs, TCG_REG_R4),
                                          tci_read_reg(regs, TCG_REG_R5));
            tci_write_reg(regs, TCG_REG_R0, tmp64);
#endif
            break;
        case INDEX_op_br:
            label = tci_read_label(&tb_ptr);
            tci_assert(tb_ptr == old_code_ptr + op_size);
            tb_ptr = (uint8_t *)label;
            continue;
        case INDEX_op_setcond_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_rval(regs, &tb_ptr);
            t2 = tci_read_rval(regs, &tb_ptr);
            condition = *tb_ptr++;
            tci_write_reg(regs, t0, tci_compare32(t1, t2, condition));
            break;
#if TCG_TARGET_REG_BITS == 32
        case INDEX_op_setcond2_i32:
            t0 = *tb_ptr++;
            tmp64 = tci_read_r64(regs, &tb_ptr);
            v64 = tci_read_r64(regs, &tb_ptr);
            condition = *tb_ptr++;
            tci_write_reg(regs, t0, tci_compare64(tmp64, v64, condition));
            break;
#elif TCG_TARGET_REG_BITS == 64
        case INDEX_op_setcond_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_rval(regs, &tb_ptr);
            t2 = tci_read_rval(regs, &tb_ptr);
            condition = *tb_ptr++;
            tci_write_reg(regs, t0, tci_compare64(t1, t2, condition));
            break;
#endif
        CASE_32_64(mov)
            tci_args_rr(&tb_ptr, &r0, &r1);
            regs[r0] = regs[r1];
            break;
        case INDEX_op_tci_movi_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_i32(&tb_ptr);
            tci_write_reg(regs, t0, t1);
            break;

            /* Load/store operations (32 bit). */

        CASE_32_64(ld8u)
            tci_args_rrs(&tb_ptr, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(uint8_t *)ptr;
            break;
        CASE_32_64(ld8s)
            tci_args_rrs(&tb_ptr, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(int8_t *)ptr;
            break;
        CASE_32_64(ld16u)
            tci_args_rrs(&tb_ptr, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(uint16_t *)ptr;
            break;
        CASE_32_64(ld16s)
            tci_args_rrs(&tb_ptr, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(int16_t *)ptr;
            break;
        case INDEX_op_ld_i32:
        CASE_64(ld32u)
            tci_args_rrs(&tb_ptr, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(uint32_t *)ptr;
            break;
        CASE_32_64(st8)
            tci_args_rrs(&tb_ptr, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            *(uint8_t *)ptr = regs[r0];
            break;
        CASE_32_64(st16)
            tci_args_rrs(&tb_ptr, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            *(uint16_t *)ptr = regs[r0];
            break;
        case INDEX_op_st_i32:
        CASE_64(st32)
            tci_args_rrs(&tb_ptr, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            *(uint32_t *)ptr = regs[r0];
            break;

            /* Arithmetic operations (mixed 32/64 bit). */

        CASE_32_64(add)
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = regs[r1] + regs[r2];
            break;
        CASE_32_64(sub)
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = regs[r1] - regs[r2];
            break;
        CASE_32_64(mul)
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = regs[r1] * regs[r2];
            break;
        CASE_32_64(and)
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = regs[r1] & regs[r2];
            break;
        CASE_32_64(or)
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = regs[r1] | regs[r2];
            break;
        CASE_32_64(xor)
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = regs[r1] ^ regs[r2];
            break;

            /* Arithmetic operations (32 bit). */

        case INDEX_op_div_i32:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = (int32_t)regs[r1] / (int32_t)regs[r2];
            break;
        case INDEX_op_divu_i32:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = (uint32_t)regs[r1] / (uint32_t)regs[r2];
            break;
        case INDEX_op_rem_i32:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = (int32_t)regs[r1] % (int32_t)regs[r2];
            break;
        case INDEX_op_remu_i32:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = (uint32_t)regs[r1] % (uint32_t)regs[r2];
            break;

            /* Shift/rotate operations (32 bit). */

        case INDEX_op_shl_i32:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = (uint32_t)regs[r1] << (regs[r2] & 31);
            break;
        case INDEX_op_shr_i32:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = (uint32_t)regs[r1] >> (regs[r2] & 31);
            break;
        case INDEX_op_sar_i32:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = (int32_t)regs[r1] >> (regs[r2] & 31);
            break;
#if TCG_TARGET_HAS_rot_i32
        case INDEX_op_rotl_i32:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = rol32(regs[r1], regs[r2] & 31);
            break;
        case INDEX_op_rotr_i32:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = ror32(regs[r1], regs[r2] & 31);
            break;
#endif
#if TCG_TARGET_HAS_deposit_i32
        case INDEX_op_deposit_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_rval(regs, &tb_ptr);
            t2 = tci_read_rval(regs, &tb_ptr);
            tmp16 = *tb_ptr++;
            tmp8 = *tb_ptr++;
            tmp32 = (((1 << tmp8) - 1) << tmp16);
            tci_write_reg(regs, t0, (t1 & ~tmp32) | ((t2 << tmp16) & tmp32));
            break;
#endif
        case INDEX_op_brcond_i32:
            t0 = tci_read_rval(regs, &tb_ptr);
            t1 = tci_read_rval(regs, &tb_ptr);
            condition = *tb_ptr++;
            label = tci_read_label(&tb_ptr);
            if (tci_compare32(t0, t1, condition)) {
                tci_assert(tb_ptr == old_code_ptr + op_size);
                tb_ptr = (uint8_t *)label;
                continue;
            }
            break;
#if TCG_TARGET_REG_BITS == 32
        case INDEX_op_add2_i32:
            t0 = *tb_ptr++;
            t1 = *tb_ptr++;
            tmp64 = tci_read_r64(regs, &tb_ptr);
            tmp64 += tci_read_r64(regs, &tb_ptr);
            tci_write_reg64(regs, t1, t0, tmp64);
            break;
        case INDEX_op_sub2_i32:
            t0 = *tb_ptr++;
            t1 = *tb_ptr++;
            tmp64 = tci_read_r64(regs, &tb_ptr);
            tmp64 -= tci_read_r64(regs, &tb_ptr);
            tci_write_reg64(regs, t1, t0, tmp64);
            break;
        case INDEX_op_brcond2_i32:
            tmp64 = tci_read_r64(regs, &tb_ptr);
            v64 = tci_read_r64(regs, &tb_ptr);
            condition = *tb_ptr++;
            label = tci_read_label(&tb_ptr);
            if (tci_compare64(tmp64, v64, condition)) {
                tci_assert(tb_ptr == old_code_ptr + op_size);
                tb_ptr = (uint8_t *)label;
                continue;
            }
            break;
        case INDEX_op_mulu2_i32:
            t0 = *tb_ptr++;
            t1 = *tb_ptr++;
            t2 = tci_read_rval(regs, &tb_ptr);
            tmp64 = (uint32_t)tci_read_rval(regs, &tb_ptr);
            tci_write_reg64(regs, t1, t0, (uint32_t)t2 * tmp64);
            break;
#endif /* TCG_TARGET_REG_BITS == 32 */
#if TCG_TARGET_HAS_ext8s_i32 || TCG_TARGET_HAS_ext8s_i64
        CASE_32_64(ext8s)
            tci_args_rr(&tb_ptr, &r0, &r1);
            regs[r0] = (int8_t)regs[r1];
            break;
#endif
#if TCG_TARGET_HAS_ext16s_i32 || TCG_TARGET_HAS_ext16s_i64
        CASE_32_64(ext16s)
            tci_args_rr(&tb_ptr, &r0, &r1);
            regs[r0] = (int16_t)regs[r1];
            break;
#endif
#if TCG_TARGET_HAS_ext8u_i32 || TCG_TARGET_HAS_ext8u_i64
        CASE_32_64(ext8u)
            tci_args_rr(&tb_ptr, &r0, &r1);
            regs[r0] = (uint8_t)regs[r1];
            break;
#endif
#if TCG_TARGET_HAS_ext16u_i32 || TCG_TARGET_HAS_ext16u_i64
        CASE_32_64(ext16u)
            tci_args_rr(&tb_ptr, &r0, &r1);
            regs[r0] = (uint16_t)regs[r1];
            break;
#endif
#if TCG_TARGET_HAS_bswap16_i32 || TCG_TARGET_HAS_bswap16_i64
        CASE_32_64(bswap16)
            tci_args_rr(&tb_ptr, &r0, &r1);
            regs[r0] = bswap16(regs[r1]);
            break;
#endif
#if TCG_TARGET_HAS_bswap32_i32 || TCG_TARGET_HAS_bswap32_i64
        CASE_32_64(bswap32)
            tci_args_rr(&tb_ptr, &r0, &r1);
            regs[r0] = bswap32(regs[r1]);
            break;
#endif
#if TCG_TARGET_HAS_not_i32 || TCG_TARGET_HAS_not_i64
        CASE_32_64(not)
            tci_args_rr(&tb_ptr, &r0, &r1);
            regs[r0] = ~regs[r1];
            break;
#endif
#if TCG_TARGET_HAS_neg_i32 || TCG_TARGET_HAS_neg_i64
        CASE_32_64(neg)
            tci_args_rr(&tb_ptr, &r0, &r1);
            regs[r0] = -regs[r1];
            break;
#endif
#if TCG_TARGET_REG_BITS == 64
        case INDEX_op_tci_movi_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_i64(&tb_ptr);
            tci_write_reg(regs, t0, t1);
            break;

            /* Load/store operations (64 bit). */

        case INDEX_op_ld32s_i64:
            tci_args_rrs(&tb_ptr, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(int32_t *)ptr;
            break;
        case INDEX_op_ld_i64:
            tci_args_rrs(&tb_ptr, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            regs[r0] = *(uint64_t *)ptr;
            break;
        case INDEX_op_st_i64:
            tci_args_rrs(&tb_ptr, &r0, &r1, &ofs);
            ptr = (void *)(regs[r1] + ofs);
            *(uint64_t *)ptr = regs[r0];
            break;

            /* Arithmetic operations (64 bit). */

        case INDEX_op_div_i64:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = (int64_t)regs[r1] / (int64_t)regs[r2];
            break;
        case INDEX_op_divu_i64:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = (uint64_t)regs[r1] / (uint64_t)regs[r2];
            break;
        case INDEX_op_rem_i64:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = (int64_t)regs[r1] % (int64_t)regs[r2];
            break;
        case INDEX_op_remu_i64:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = (uint64_t)regs[r1] % (uint64_t)regs[r2];
            break;

            /* Shift/rotate operations (64 bit). */

        case INDEX_op_shl_i64:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = regs[r1] << (regs[r2] & 63);
            break;
        case INDEX_op_shr_i64:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = regs[r1] >> (regs[r2] & 63);
            break;
        case INDEX_op_sar_i64:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = (int64_t)regs[r1] >> (regs[r2] & 63);
            break;
#if TCG_TARGET_HAS_rot_i64
        case INDEX_op_rotl_i64:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = rol64(regs[r1], regs[r2] & 63);
            break;
        case INDEX_op_rotr_i64:
            tci_args_rrr(&tb_ptr, &r0, &r1, &r2);
            regs[r0] = ror64(regs[r1], regs[r2] & 63);
            break;
#endif
#if TCG_TARGET_HAS_deposit_i64
        case INDEX_op_deposit_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_rval(regs, &tb_ptr);
            t2 = tci_read_rval(regs, &tb_ptr);
            tmp16 = *tb_ptr++;
            tmp8 = *tb_ptr++;
            tmp64 = (((1ULL << tmp8) - 1) << tmp16);
            tci_write_reg(regs, t0, (t1 & ~tmp64) | ((t2 << tmp16) & tmp64));
            break;
#endif
        case INDEX_op_brcond_i64:
            t0 = tci_read_rval(regs, &tb_ptr);
            t1 = tci_read_rval(regs, &tb_ptr);
            condition = *tb_ptr++;
            label = tci_read_label(&tb_ptr);
            if (tci_compare64(t0, t1, condition)) {
                tci_assert(tb_ptr == old_code_ptr + op_size);
                tb_ptr = (uint8_t *)label;
                continue;
            }
            break;
        case INDEX_op_ext32s_i64:
        case INDEX_op_ext_i32_i64:
            tci_args_rr(&tb_ptr, &r0, &r1);
            regs[r0] = (int32_t)regs[r1];
            break;
        case INDEX_op_ext32u_i64:
        case INDEX_op_extu_i32_i64:
            tci_args_rr(&tb_ptr, &r0, &r1);
            regs[r0] = (uint32_t)regs[r1];
            break;
#if TCG_TARGET_HAS_bswap64_i64
        case INDEX_op_bswap64_i64:
            tci_args_rr(&tb_ptr, &r0, &r1);
            regs[r0] = bswap64(regs[r1]);
            break;
#endif
#endif /* TCG_TARGET_REG_BITS == 64 */

            /* QEMU specific operations. */

        case INDEX_op_exit_tb:
            ret = *(uint64_t *)tb_ptr;
            goto exit;
            break;
        case INDEX_op_goto_tb:
            /* Jump address is aligned */
            tb_ptr = QEMU_ALIGN_PTR_UP(tb_ptr, 4);
            t0 = qatomic_read((int32_t *)tb_ptr);
            tb_ptr += sizeof(int32_t);
            tci_assert(tb_ptr == old_code_ptr + op_size);
            tb_ptr += (int32_t)t0;
            continue;
        case INDEX_op_qemu_ld_i32:
            t0 = *tb_ptr++;
            taddr = tci_read_ulong(regs, &tb_ptr);
            oi = tci_read_i(&tb_ptr);
            switch (get_memop(oi) & (MO_BSWAP | MO_SSIZE)) {
            case MO_UB:
                tmp32 = qemu_ld_ub;
                break;
            case MO_SB:
                tmp32 = (int8_t)qemu_ld_ub;
                break;
            case MO_LEUW:
                tmp32 = qemu_ld_leuw;
                break;
            case MO_LESW:
                tmp32 = (int16_t)qemu_ld_leuw;
                break;
            case MO_LEUL:
                tmp32 = qemu_ld_leul;
                break;
            case MO_BEUW:
                tmp32 = qemu_ld_beuw;
                break;
            case MO_BESW:
                tmp32 = (int16_t)qemu_ld_beuw;
                break;
            case MO_BEUL:
                tmp32 = qemu_ld_beul;
                break;
            default:
                g_assert_not_reached();
            }
            tci_write_reg(regs, t0, tmp32);
            break;
        case INDEX_op_qemu_ld_i64:
            t0 = *tb_ptr++;
            if (TCG_TARGET_REG_BITS == 32) {
                t1 = *tb_ptr++;
            }
            taddr = tci_read_ulong(regs, &tb_ptr);
            oi = tci_read_i(&tb_ptr);
            switch (get_memop(oi) & (MO_BSWAP | MO_SSIZE)) {
            case MO_UB:
                tmp64 = qemu_ld_ub;
                break;
            case MO_SB:
                tmp64 = (int8_t)qemu_ld_ub;
                break;
            case MO_LEUW:
                tmp64 = qemu_ld_leuw;
                break;
            case MO_LESW:
                tmp64 = (int16_t)qemu_ld_leuw;
                break;
            case MO_LEUL:
                tmp64 = qemu_ld_leul;
                break;
            case MO_LESL:
                tmp64 = (int32_t)qemu_ld_leul;
                break;
            case MO_LEQ:
                tmp64 = qemu_ld_leq;
                break;
            case MO_BEUW:
                tmp64 = qemu_ld_beuw;
                break;
            case MO_BESW:
                tmp64 = (int16_t)qemu_ld_beuw;
                break;
            case MO_BEUL:
                tmp64 = qemu_ld_beul;
                break;
            case MO_BESL:
                tmp64 = (int32_t)qemu_ld_beul;
                break;
            case MO_BEQ:
                tmp64 = qemu_ld_beq;
                break;
            default:
                g_assert_not_reached();
            }
            tci_write_reg(regs, t0, tmp64);
            if (TCG_TARGET_REG_BITS == 32) {
                tci_write_reg(regs, t1, tmp64 >> 32);
            }
            break;
        case INDEX_op_qemu_st_i32:
            t0 = tci_read_rval(regs, &tb_ptr);
            taddr = tci_read_ulong(regs, &tb_ptr);
            oi = tci_read_i(&tb_ptr);
            switch (get_memop(oi) & (MO_BSWAP | MO_SIZE)) {
            case MO_UB:
                qemu_st_b(t0);
                break;
            case MO_LEUW:
                qemu_st_lew(t0);
                break;
            case MO_LEUL:
                qemu_st_lel(t0);
                break;
            case MO_BEUW:
                qemu_st_bew(t0);
                break;
            case MO_BEUL:
                qemu_st_bel(t0);
                break;
            default:
                g_assert_not_reached();
            }
            break;
        case INDEX_op_qemu_st_i64:
            tmp64 = tci_read_r64(regs, &tb_ptr);
            taddr = tci_read_ulong(regs, &tb_ptr);
            oi = tci_read_i(&tb_ptr);
            switch (get_memop(oi) & (MO_BSWAP | MO_SIZE)) {
            case MO_UB:
                qemu_st_b(tmp64);
                break;
            case MO_LEUW:
                qemu_st_lew(tmp64);
                break;
            case MO_LEUL:
                qemu_st_lel(tmp64);
                break;
            case MO_LEQ:
                qemu_st_leq(tmp64);
                break;
            case MO_BEUW:
                qemu_st_bew(tmp64);
                break;
            case MO_BEUL:
                qemu_st_bel(tmp64);
                break;
            case MO_BEQ:
                qemu_st_beq(tmp64);
                break;
            default:
                g_assert_not_reached();
            }
            break;
        case INDEX_op_mb:
            /* Ensure ordering for all kinds */
            smp_mb();
            break;
        default:
            g_assert_not_reached();
        }
        tci_assert(tb_ptr == old_code_ptr + op_size);
    }
exit:
    return ret;
}
