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
#include "tcg-op.h"

/* Marker for missing code. */
#define TODO() \
    do { \
        fprintf(stderr, "TODO %s:%u: %s()\n", \
                __FILE__, __LINE__, __func__); \
        tcg_abort(); \
    } while (0)

#if MAX_OPC_PARAM_IARGS != 5
# error Fix needed, number of supported input arguments changed!
#endif
#if TCG_TARGET_REG_BITS == 32
typedef uint64_t (*helper_function)(tcg_target_ulong, tcg_target_ulong,
                                    tcg_target_ulong, tcg_target_ulong,
                                    tcg_target_ulong, tcg_target_ulong,
                                    tcg_target_ulong, tcg_target_ulong,
                                    tcg_target_ulong, tcg_target_ulong);
#else
typedef uint64_t (*helper_function)(tcg_target_ulong, tcg_target_ulong,
                                    tcg_target_ulong, tcg_target_ulong,
                                    tcg_target_ulong);
#endif

static tcg_target_ulong tci_reg[TCG_TARGET_NB_REGS];

static tcg_target_ulong tci_read_reg(TCGReg index)
{
    tci_assert(index < ARRAY_SIZE(tci_reg));
    return tci_reg[index];
}

#if TCG_TARGET_HAS_ext8s_i32 || TCG_TARGET_HAS_ext8s_i64
static int8_t tci_read_reg8s(TCGReg index)
{
    return (int8_t)tci_read_reg(index);
}
#endif

#if TCG_TARGET_HAS_ext16s_i32 || TCG_TARGET_HAS_ext16s_i64
static int16_t tci_read_reg16s(TCGReg index)
{
    return (int16_t)tci_read_reg(index);
}
#endif

#if TCG_TARGET_REG_BITS == 64
static int32_t tci_read_reg32s(TCGReg index)
{
    return (int32_t)tci_read_reg(index);
}
#endif

static uint8_t tci_read_reg8(TCGReg index)
{
    return (uint8_t)tci_read_reg(index);
}

static uint16_t tci_read_reg16(TCGReg index)
{
    return (uint16_t)tci_read_reg(index);
}

static uint32_t tci_read_reg32(TCGReg index)
{
    return (uint32_t)tci_read_reg(index);
}

#if TCG_TARGET_REG_BITS == 64
static uint64_t tci_read_reg64(TCGReg index)
{
    return tci_read_reg(index);
}
#endif

static void tci_write_reg(TCGReg index, tcg_target_ulong value)
{
    tci_assert(index < ARRAY_SIZE(tci_reg));
    tci_assert(index != TCG_AREG0);
    tci_assert(index != TCG_REG_CALL_STACK);
    tci_reg[index] = value;
}

#if TCG_TARGET_REG_BITS == 64
static void tci_write_reg32s(TCGReg index, int32_t value)
{
    tci_write_reg(index, value);
}
#endif

static void tci_write_reg8(TCGReg index, uint8_t value)
{
    tci_write_reg(index, value);
}

static void tci_write_reg32(TCGReg index, uint32_t value)
{
    tci_write_reg(index, value);
}

#if TCG_TARGET_REG_BITS == 32
static void tci_write_reg64(uint32_t high_index, uint32_t low_index,
                            uint64_t value)
{
    tci_write_reg(low_index, value);
    tci_write_reg(high_index, value >> 32);
}
#elif TCG_TARGET_REG_BITS == 64
static void tci_write_reg64(TCGReg index, uint64_t value)
{
    tci_write_reg(index, value);
}
#endif

#if TCG_TARGET_REG_BITS == 32
/* Create a 64 bit value from two 32 bit values. */
static uint64_t tci_uint64(uint32_t high, uint32_t low)
{
    return ((uint64_t)high << 32) + low;
}
#endif

/* Read constant (native size) from bytecode. */
static tcg_target_ulong tci_read_i(uint8_t **tb_ptr)
{
    tcg_target_ulong value = *(tcg_target_ulong *)(*tb_ptr);
    *tb_ptr += sizeof(value);
    return value;
}

/* Read unsigned constant (32 bit) from bytecode. */
static uint32_t tci_read_i32(uint8_t **tb_ptr)
{
    uint32_t value = *(uint32_t *)(*tb_ptr);
    *tb_ptr += sizeof(value);
    return value;
}

/* Read signed constant (32 bit) from bytecode. */
static int32_t tci_read_s32(uint8_t **tb_ptr)
{
    int32_t value = *(int32_t *)(*tb_ptr);
    *tb_ptr += sizeof(value);
    return value;
}

#if TCG_TARGET_REG_BITS == 64
/* Read constant (64 bit) from bytecode. */
static uint64_t tci_read_i64(uint8_t **tb_ptr)
{
    uint64_t value = *(uint64_t *)(*tb_ptr);
    *tb_ptr += sizeof(value);
    return value;
}
#endif

/* Read indexed register (native size) from bytecode. */
static tcg_target_ulong tci_read_r(uint8_t **tb_ptr)
{
    tcg_target_ulong value = tci_read_reg(**tb_ptr);
    *tb_ptr += 1;
    return value;
}

/* Read indexed register (8 bit) from bytecode. */
static uint8_t tci_read_r8(uint8_t **tb_ptr)
{
    uint8_t value = tci_read_reg8(**tb_ptr);
    *tb_ptr += 1;
    return value;
}

#if TCG_TARGET_HAS_ext8s_i32 || TCG_TARGET_HAS_ext8s_i64
/* Read indexed register (8 bit signed) from bytecode. */
static int8_t tci_read_r8s(uint8_t **tb_ptr)
{
    int8_t value = tci_read_reg8s(**tb_ptr);
    *tb_ptr += 1;
    return value;
}
#endif

/* Read indexed register (16 bit) from bytecode. */
static uint16_t tci_read_r16(uint8_t **tb_ptr)
{
    uint16_t value = tci_read_reg16(**tb_ptr);
    *tb_ptr += 1;
    return value;
}

#if TCG_TARGET_HAS_ext16s_i32 || TCG_TARGET_HAS_ext16s_i64
/* Read indexed register (16 bit signed) from bytecode. */
static int16_t tci_read_r16s(uint8_t **tb_ptr)
{
    int16_t value = tci_read_reg16s(**tb_ptr);
    *tb_ptr += 1;
    return value;
}
#endif

/* Read indexed register (32 bit) from bytecode. */
static uint32_t tci_read_r32(uint8_t **tb_ptr)
{
    uint32_t value = tci_read_reg32(**tb_ptr);
    *tb_ptr += 1;
    return value;
}

#if TCG_TARGET_REG_BITS == 32
/* Read two indexed registers (2 * 32 bit) from bytecode. */
static uint64_t tci_read_r64(uint8_t **tb_ptr)
{
    uint32_t low = tci_read_r32(tb_ptr);
    return tci_uint64(tci_read_r32(tb_ptr), low);
}
#elif TCG_TARGET_REG_BITS == 64
/* Read indexed register (32 bit signed) from bytecode. */
static int32_t tci_read_r32s(uint8_t **tb_ptr)
{
    int32_t value = tci_read_reg32s(**tb_ptr);
    *tb_ptr += 1;
    return value;
}

/* Read indexed register (64 bit) from bytecode. */
static uint64_t tci_read_r64(uint8_t **tb_ptr)
{
    uint64_t value = tci_read_reg64(**tb_ptr);
    *tb_ptr += 1;
    return value;
}
#endif

/* Read indexed register(s) with target address from bytecode. */
static target_ulong tci_read_ulong(uint8_t **tb_ptr)
{
    target_ulong taddr = tci_read_r(tb_ptr);
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
    taddr += (uint64_t)tci_read_r(tb_ptr) << 32;
#endif
    return taddr;
}

/* Read indexed register or constant (native size) from bytecode. */
static tcg_target_ulong tci_read_ri(uint8_t **tb_ptr)
{
    tcg_target_ulong value;
    TCGReg r = **tb_ptr;
    *tb_ptr += 1;
    if (r == TCG_CONST) {
        value = tci_read_i(tb_ptr);
    } else {
        value = tci_read_reg(r);
    }
    return value;
}

/* Read indexed register or constant (32 bit) from bytecode. */
static uint32_t tci_read_ri32(uint8_t **tb_ptr)
{
    uint32_t value;
    TCGReg r = **tb_ptr;
    *tb_ptr += 1;
    if (r == TCG_CONST) {
        value = tci_read_i32(tb_ptr);
    } else {
        value = tci_read_reg32(r);
    }
    return value;
}

#if TCG_TARGET_REG_BITS == 32
/* Read two indexed registers or constants (2 * 32 bit) from bytecode. */
static uint64_t tci_read_ri64(uint8_t **tb_ptr)
{
    uint32_t low = tci_read_ri32(tb_ptr);
    return tci_uint64(tci_read_ri32(tb_ptr), low);
}
#elif TCG_TARGET_REG_BITS == 64
/* Read indexed register or constant (64 bit) from bytecode. */
static uint64_t tci_read_ri64(uint8_t **tb_ptr)
{
    uint64_t value;
    TCGReg r = **tb_ptr;
    *tb_ptr += 1;
    if (r == TCG_CONST) {
        value = tci_read_i64(tb_ptr);
    } else {
        value = tci_read_reg64(r);
    }
    return value;
}
#endif

static tcg_target_ulong tci_read_label(uint8_t **tb_ptr)
{
    tcg_target_ulong label = tci_read_i(tb_ptr);
    tci_assert(label != 0);
    return label;
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
        TODO();
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
        TODO();
    }
    return result;
}

#ifdef CONFIG_SOFTMMU
# define qemu_ld_ub \
    helper_ret_ldub_mmu(env, taddr, oi, (uintptr_t)tb_ptr)
# define qemu_ld_leuw \
    helper_le_lduw_mmu(env, taddr, oi, (uintptr_t)tb_ptr)
# define qemu_ld_leul \
    helper_le_ldul_mmu(env, taddr, oi, (uintptr_t)tb_ptr)
# define qemu_ld_leq \
    helper_le_ldq_mmu(env, taddr, oi, (uintptr_t)tb_ptr)
# define qemu_ld_beuw \
    helper_be_lduw_mmu(env, taddr, oi, (uintptr_t)tb_ptr)
# define qemu_ld_beul \
    helper_be_ldul_mmu(env, taddr, oi, (uintptr_t)tb_ptr)
# define qemu_ld_beq \
    helper_be_ldq_mmu(env, taddr, oi, (uintptr_t)tb_ptr)
# define qemu_st_b(X) \
    helper_ret_stb_mmu(env, taddr, X, oi, (uintptr_t)tb_ptr)
# define qemu_st_lew(X) \
    helper_le_stw_mmu(env, taddr, X, oi, (uintptr_t)tb_ptr)
# define qemu_st_lel(X) \
    helper_le_stl_mmu(env, taddr, X, oi, (uintptr_t)tb_ptr)
# define qemu_st_leq(X) \
    helper_le_stq_mmu(env, taddr, X, oi, (uintptr_t)tb_ptr)
# define qemu_st_bew(X) \
    helper_be_stw_mmu(env, taddr, X, oi, (uintptr_t)tb_ptr)
# define qemu_st_bel(X) \
    helper_be_stl_mmu(env, taddr, X, oi, (uintptr_t)tb_ptr)
# define qemu_st_beq(X) \
    helper_be_stq_mmu(env, taddr, X, oi, (uintptr_t)tb_ptr)
#else
# define qemu_ld_ub      ldub_p(g2h(taddr))
# define qemu_ld_leuw    lduw_le_p(g2h(taddr))
# define qemu_ld_leul    (uint32_t)ldl_le_p(g2h(taddr))
# define qemu_ld_leq     ldq_le_p(g2h(taddr))
# define qemu_ld_beuw    lduw_be_p(g2h(taddr))
# define qemu_ld_beul    (uint32_t)ldl_be_p(g2h(taddr))
# define qemu_ld_beq     ldq_be_p(g2h(taddr))
# define qemu_st_b(X)    stb_p(g2h(taddr), X)
# define qemu_st_lew(X)  stw_le_p(g2h(taddr), X)
# define qemu_st_lel(X)  stl_le_p(g2h(taddr), X)
# define qemu_st_leq(X)  stq_le_p(g2h(taddr), X)
# define qemu_st_bew(X)  stw_be_p(g2h(taddr), X)
# define qemu_st_bel(X)  stl_be_p(g2h(taddr), X)
# define qemu_st_beq(X)  stq_be_p(g2h(taddr), X)
#endif

/* Interpret pseudo code in tb. */
uintptr_t tcg_qemu_tb_exec(CPUArchState *env, uint8_t *tb_ptr)
{
    long tcg_temps[CPU_TEMP_BUF_NLONGS];
    uintptr_t sp_value = (uintptr_t)(tcg_temps + CPU_TEMP_BUF_NLONGS);
    uintptr_t ret = 0;

    tci_reg[TCG_AREG0] = (tcg_target_ulong)env;
    tci_reg[TCG_REG_CALL_STACK] = sp_value;
    tci_assert(tb_ptr);

    for (;;) {
        TCGOpcode opc = tb_ptr[0];
#if defined(CONFIG_DEBUG_TCG) && !defined(NDEBUG)
        uint8_t op_size = tb_ptr[1];
        uint8_t *old_code_ptr = tb_ptr;
#endif
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

#if defined(GETPC)
        tci_tb_ptr = (uintptr_t)tb_ptr;
#endif

        /* Skip opcode and size entry. */
        tb_ptr += 2;

        switch (opc) {
        case INDEX_op_call:
            t0 = tci_read_ri(&tb_ptr);
#if TCG_TARGET_REG_BITS == 32
            tmp64 = ((helper_function)t0)(tci_read_reg(TCG_REG_R0),
                                          tci_read_reg(TCG_REG_R1),
                                          tci_read_reg(TCG_REG_R2),
                                          tci_read_reg(TCG_REG_R3),
                                          tci_read_reg(TCG_REG_R5),
                                          tci_read_reg(TCG_REG_R6),
                                          tci_read_reg(TCG_REG_R7),
                                          tci_read_reg(TCG_REG_R8),
                                          tci_read_reg(TCG_REG_R9),
                                          tci_read_reg(TCG_REG_R10));
            tci_write_reg(TCG_REG_R0, tmp64);
            tci_write_reg(TCG_REG_R1, tmp64 >> 32);
#else
            tmp64 = ((helper_function)t0)(tci_read_reg(TCG_REG_R0),
                                          tci_read_reg(TCG_REG_R1),
                                          tci_read_reg(TCG_REG_R2),
                                          tci_read_reg(TCG_REG_R3),
                                          tci_read_reg(TCG_REG_R5));
            tci_write_reg(TCG_REG_R0, tmp64);
#endif
            break;
        case INDEX_op_br:
            label = tci_read_label(&tb_ptr);
            tci_assert(tb_ptr == old_code_ptr + op_size);
            tb_ptr = (uint8_t *)label;
            continue;
        case INDEX_op_setcond_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            condition = *tb_ptr++;
            tci_write_reg32(t0, tci_compare32(t1, t2, condition));
            break;
#if TCG_TARGET_REG_BITS == 32
        case INDEX_op_setcond2_i32:
            t0 = *tb_ptr++;
            tmp64 = tci_read_r64(&tb_ptr);
            v64 = tci_read_ri64(&tb_ptr);
            condition = *tb_ptr++;
            tci_write_reg32(t0, tci_compare64(tmp64, v64, condition));
            break;
#elif TCG_TARGET_REG_BITS == 64
        case INDEX_op_setcond_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            condition = *tb_ptr++;
            tci_write_reg64(t0, tci_compare64(t1, t2, condition));
            break;
#endif
        case INDEX_op_mov_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r32(&tb_ptr);
            tci_write_reg32(t0, t1);
            break;
        case INDEX_op_movi_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_i32(&tb_ptr);
            tci_write_reg32(t0, t1);
            break;

            /* Load/store operations (32 bit). */

        case INDEX_op_ld8u_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_s32(&tb_ptr);
            tci_write_reg8(t0, *(uint8_t *)(t1 + t2));
            break;
        case INDEX_op_ld8s_i32:
        case INDEX_op_ld16u_i32:
            TODO();
            break;
        case INDEX_op_ld16s_i32:
            TODO();
            break;
        case INDEX_op_ld_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_s32(&tb_ptr);
            tci_write_reg32(t0, *(uint32_t *)(t1 + t2));
            break;
        case INDEX_op_st8_i32:
            t0 = tci_read_r8(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_s32(&tb_ptr);
            *(uint8_t *)(t1 + t2) = t0;
            break;
        case INDEX_op_st16_i32:
            t0 = tci_read_r16(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_s32(&tb_ptr);
            *(uint16_t *)(t1 + t2) = t0;
            break;
        case INDEX_op_st_i32:
            t0 = tci_read_r32(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_s32(&tb_ptr);
            tci_assert(t1 != sp_value || (int32_t)t2 < 0);
            *(uint32_t *)(t1 + t2) = t0;
            break;

            /* Arithmetic operations (32 bit). */

        case INDEX_op_add_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, t1 + t2);
            break;
        case INDEX_op_sub_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, t1 - t2);
            break;
        case INDEX_op_mul_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, t1 * t2);
            break;
#if TCG_TARGET_HAS_div_i32
        case INDEX_op_div_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, (int32_t)t1 / (int32_t)t2);
            break;
        case INDEX_op_divu_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, t1 / t2);
            break;
        case INDEX_op_rem_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, (int32_t)t1 % (int32_t)t2);
            break;
        case INDEX_op_remu_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, t1 % t2);
            break;
#elif TCG_TARGET_HAS_div2_i32
        case INDEX_op_div2_i32:
        case INDEX_op_divu2_i32:
            TODO();
            break;
#endif
        case INDEX_op_and_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, t1 & t2);
            break;
        case INDEX_op_or_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, t1 | t2);
            break;
        case INDEX_op_xor_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, t1 ^ t2);
            break;

            /* Shift/rotate operations (32 bit). */

        case INDEX_op_shl_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, t1 << (t2 & 31));
            break;
        case INDEX_op_shr_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, t1 >> (t2 & 31));
            break;
        case INDEX_op_sar_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, ((int32_t)t1 >> (t2 & 31)));
            break;
#if TCG_TARGET_HAS_rot_i32
        case INDEX_op_rotl_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, rol32(t1, t2 & 31));
            break;
        case INDEX_op_rotr_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, ror32(t1, t2 & 31));
            break;
#endif
#if TCG_TARGET_HAS_deposit_i32
        case INDEX_op_deposit_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r32(&tb_ptr);
            t2 = tci_read_r32(&tb_ptr);
            tmp16 = *tb_ptr++;
            tmp8 = *tb_ptr++;
            tmp32 = (((1 << tmp8) - 1) << tmp16);
            tci_write_reg32(t0, (t1 & ~tmp32) | ((t2 << tmp16) & tmp32));
            break;
#endif
        case INDEX_op_brcond_i32:
            t0 = tci_read_r32(&tb_ptr);
            t1 = tci_read_ri32(&tb_ptr);
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
            tmp64 = tci_read_r64(&tb_ptr);
            tmp64 += tci_read_r64(&tb_ptr);
            tci_write_reg64(t1, t0, tmp64);
            break;
        case INDEX_op_sub2_i32:
            t0 = *tb_ptr++;
            t1 = *tb_ptr++;
            tmp64 = tci_read_r64(&tb_ptr);
            tmp64 -= tci_read_r64(&tb_ptr);
            tci_write_reg64(t1, t0, tmp64);
            break;
        case INDEX_op_brcond2_i32:
            tmp64 = tci_read_r64(&tb_ptr);
            v64 = tci_read_ri64(&tb_ptr);
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
            t2 = tci_read_r32(&tb_ptr);
            tmp64 = tci_read_r32(&tb_ptr);
            tci_write_reg64(t1, t0, t2 * tmp64);
            break;
#endif /* TCG_TARGET_REG_BITS == 32 */
#if TCG_TARGET_HAS_ext8s_i32
        case INDEX_op_ext8s_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r8s(&tb_ptr);
            tci_write_reg32(t0, t1);
            break;
#endif
#if TCG_TARGET_HAS_ext16s_i32
        case INDEX_op_ext16s_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r16s(&tb_ptr);
            tci_write_reg32(t0, t1);
            break;
#endif
#if TCG_TARGET_HAS_ext8u_i32
        case INDEX_op_ext8u_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r8(&tb_ptr);
            tci_write_reg32(t0, t1);
            break;
#endif
#if TCG_TARGET_HAS_ext16u_i32
        case INDEX_op_ext16u_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r16(&tb_ptr);
            tci_write_reg32(t0, t1);
            break;
#endif
#if TCG_TARGET_HAS_bswap16_i32
        case INDEX_op_bswap16_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r16(&tb_ptr);
            tci_write_reg32(t0, bswap16(t1));
            break;
#endif
#if TCG_TARGET_HAS_bswap32_i32
        case INDEX_op_bswap32_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r32(&tb_ptr);
            tci_write_reg32(t0, bswap32(t1));
            break;
#endif
#if TCG_TARGET_HAS_not_i32
        case INDEX_op_not_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r32(&tb_ptr);
            tci_write_reg32(t0, ~t1);
            break;
#endif
#if TCG_TARGET_HAS_neg_i32
        case INDEX_op_neg_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r32(&tb_ptr);
            tci_write_reg32(t0, -t1);
            break;
#endif
#if TCG_TARGET_REG_BITS == 64
        case INDEX_op_mov_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r64(&tb_ptr);
            tci_write_reg64(t0, t1);
            break;
        case INDEX_op_movi_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_i64(&tb_ptr);
            tci_write_reg64(t0, t1);
            break;

            /* Load/store operations (64 bit). */

        case INDEX_op_ld8u_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_s32(&tb_ptr);
            tci_write_reg8(t0, *(uint8_t *)(t1 + t2));
            break;
        case INDEX_op_ld8s_i64:
        case INDEX_op_ld16u_i64:
        case INDEX_op_ld16s_i64:
            TODO();
            break;
        case INDEX_op_ld32u_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_s32(&tb_ptr);
            tci_write_reg32(t0, *(uint32_t *)(t1 + t2));
            break;
        case INDEX_op_ld32s_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_s32(&tb_ptr);
            tci_write_reg32s(t0, *(int32_t *)(t1 + t2));
            break;
        case INDEX_op_ld_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_s32(&tb_ptr);
            tci_write_reg64(t0, *(uint64_t *)(t1 + t2));
            break;
        case INDEX_op_st8_i64:
            t0 = tci_read_r8(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_s32(&tb_ptr);
            *(uint8_t *)(t1 + t2) = t0;
            break;
        case INDEX_op_st16_i64:
            t0 = tci_read_r16(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_s32(&tb_ptr);
            *(uint16_t *)(t1 + t2) = t0;
            break;
        case INDEX_op_st32_i64:
            t0 = tci_read_r32(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_s32(&tb_ptr);
            *(uint32_t *)(t1 + t2) = t0;
            break;
        case INDEX_op_st_i64:
            t0 = tci_read_r64(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_s32(&tb_ptr);
            tci_assert(t1 != sp_value || (int32_t)t2 < 0);
            *(uint64_t *)(t1 + t2) = t0;
            break;

            /* Arithmetic operations (64 bit). */

        case INDEX_op_add_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, t1 + t2);
            break;
        case INDEX_op_sub_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, t1 - t2);
            break;
        case INDEX_op_mul_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, t1 * t2);
            break;
#if TCG_TARGET_HAS_div_i64
        case INDEX_op_div_i64:
        case INDEX_op_divu_i64:
        case INDEX_op_rem_i64:
        case INDEX_op_remu_i64:
            TODO();
            break;
#elif TCG_TARGET_HAS_div2_i64
        case INDEX_op_div2_i64:
        case INDEX_op_divu2_i64:
            TODO();
            break;
#endif
        case INDEX_op_and_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, t1 & t2);
            break;
        case INDEX_op_or_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, t1 | t2);
            break;
        case INDEX_op_xor_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, t1 ^ t2);
            break;

            /* Shift/rotate operations (64 bit). */

        case INDEX_op_shl_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, t1 << (t2 & 63));
            break;
        case INDEX_op_shr_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, t1 >> (t2 & 63));
            break;
        case INDEX_op_sar_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, ((int64_t)t1 >> (t2 & 63)));
            break;
#if TCG_TARGET_HAS_rot_i64
        case INDEX_op_rotl_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, rol64(t1, t2 & 63));
            break;
        case INDEX_op_rotr_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, ror64(t1, t2 & 63));
            break;
#endif
#if TCG_TARGET_HAS_deposit_i64
        case INDEX_op_deposit_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r64(&tb_ptr);
            t2 = tci_read_r64(&tb_ptr);
            tmp16 = *tb_ptr++;
            tmp8 = *tb_ptr++;
            tmp64 = (((1ULL << tmp8) - 1) << tmp16);
            tci_write_reg64(t0, (t1 & ~tmp64) | ((t2 << tmp16) & tmp64));
            break;
#endif
        case INDEX_op_brcond_i64:
            t0 = tci_read_r64(&tb_ptr);
            t1 = tci_read_ri64(&tb_ptr);
            condition = *tb_ptr++;
            label = tci_read_label(&tb_ptr);
            if (tci_compare64(t0, t1, condition)) {
                tci_assert(tb_ptr == old_code_ptr + op_size);
                tb_ptr = (uint8_t *)label;
                continue;
            }
            break;
#if TCG_TARGET_HAS_ext8u_i64
        case INDEX_op_ext8u_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r8(&tb_ptr);
            tci_write_reg64(t0, t1);
            break;
#endif
#if TCG_TARGET_HAS_ext8s_i64
        case INDEX_op_ext8s_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r8s(&tb_ptr);
            tci_write_reg64(t0, t1);
            break;
#endif
#if TCG_TARGET_HAS_ext16s_i64
        case INDEX_op_ext16s_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r16s(&tb_ptr);
            tci_write_reg64(t0, t1);
            break;
#endif
#if TCG_TARGET_HAS_ext16u_i64
        case INDEX_op_ext16u_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r16(&tb_ptr);
            tci_write_reg64(t0, t1);
            break;
#endif
#if TCG_TARGET_HAS_ext32s_i64
        case INDEX_op_ext32s_i64:
#endif
        case INDEX_op_ext_i32_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r32s(&tb_ptr);
            tci_write_reg64(t0, t1);
            break;
#if TCG_TARGET_HAS_ext32u_i64
        case INDEX_op_ext32u_i64:
#endif
        case INDEX_op_extu_i32_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r32(&tb_ptr);
            tci_write_reg64(t0, t1);
            break;
#if TCG_TARGET_HAS_bswap16_i64
        case INDEX_op_bswap16_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r16(&tb_ptr);
            tci_write_reg64(t0, bswap16(t1));
            break;
#endif
#if TCG_TARGET_HAS_bswap32_i64
        case INDEX_op_bswap32_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r32(&tb_ptr);
            tci_write_reg64(t0, bswap32(t1));
            break;
#endif
#if TCG_TARGET_HAS_bswap64_i64
        case INDEX_op_bswap64_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r64(&tb_ptr);
            tci_write_reg64(t0, bswap64(t1));
            break;
#endif
#if TCG_TARGET_HAS_not_i64
        case INDEX_op_not_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r64(&tb_ptr);
            tci_write_reg64(t0, ~t1);
            break;
#endif
#if TCG_TARGET_HAS_neg_i64
        case INDEX_op_neg_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r64(&tb_ptr);
            tci_write_reg64(t0, -t1);
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
            t0 = atomic_read((int32_t *)tb_ptr);
            tb_ptr += sizeof(int32_t);
            tci_assert(tb_ptr == old_code_ptr + op_size);
            tb_ptr += (int32_t)t0;
            continue;
        case INDEX_op_qemu_ld_i32:
            t0 = *tb_ptr++;
            taddr = tci_read_ulong(&tb_ptr);
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
                tcg_abort();
            }
            tci_write_reg(t0, tmp32);
            break;
        case INDEX_op_qemu_ld_i64:
            t0 = *tb_ptr++;
            if (TCG_TARGET_REG_BITS == 32) {
                t1 = *tb_ptr++;
            }
            taddr = tci_read_ulong(&tb_ptr);
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
                tcg_abort();
            }
            tci_write_reg(t0, tmp64);
            if (TCG_TARGET_REG_BITS == 32) {
                tci_write_reg(t1, tmp64 >> 32);
            }
            break;
        case INDEX_op_qemu_st_i32:
            t0 = tci_read_r(&tb_ptr);
            taddr = tci_read_ulong(&tb_ptr);
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
                tcg_abort();
            }
            break;
        case INDEX_op_qemu_st_i64:
            tmp64 = tci_read_r64(&tb_ptr);
            taddr = tci_read_ulong(&tb_ptr);
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
                tcg_abort();
            }
            break;
        case INDEX_op_mb:
            /* Ensure ordering for all kinds */
            smp_mb();
            break;
        default:
            TODO();
            break;
        }
        tci_assert(tb_ptr == old_code_ptr + op_size);
    }
exit:
    return ret;
}
