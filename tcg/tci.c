/*
 * Tiny Code Interpreter for QEMU
 *
 * Copyright (c) 2009 Stefan Weil
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

#include <stdbool.h>    /* bool */

#include "config.h"
#include "qemu-common.h"
#include "tcg-op.h"

/* Marker for missing code. */
#define TODO() \
    fprintf(stderr, "TODO %s:%u: %s()\n", __FILE__, __LINE__, __FUNCTION__); \
    tcg_abort()

/* Trace message to see program flow. */
#if defined(CONFIG_DEBUG_TCG_INTERPRETER)
#define TRACE() \
    fprintf(stderr, "TCG %s:%u: %s()\n", __FILE__, __LINE__, __FUNCTION__)
#else
#define TRACE() ((void)0)
#endif

typedef tcg_target_ulong (*helper_function)(tcg_target_ulong, tcg_target_ulong,
                                            tcg_target_ulong, tcg_target_ulong);

#if defined(TARGET_I386)
struct CPUX86State *env;
#else
#error Target support missing, please fix!
#endif

static tcg_target_ulong tci_reg[TCG_TARGET_NB_REGS];

static tcg_target_ulong tci_read_reg(uint32_t index)
{
    assert(index < ARRAY_SIZE(tci_reg));
    return tci_reg[index];
}

static uint8_t tci_read_reg8(uint32_t index)
{
    assert(index < ARRAY_SIZE(tci_reg));
    return (uint8_t)(tci_reg[index]);
}

static int8_t tci_read_reg8s(uint32_t index)
{
    assert(index < ARRAY_SIZE(tci_reg));
    return (int8_t)(tci_reg[index]);
}

static uint16_t tci_read_reg16(uint32_t index)
{
    assert(index < ARRAY_SIZE(tci_reg));
    return (uint16_t)(tci_reg[index]);
}

static uint32_t tci_read_reg32(uint32_t index)
{
    assert(index < ARRAY_SIZE(tci_reg));
    return (uint32_t)(tci_reg[index]);
}

#if TCG_TARGET_REG_BITS == 64
static int32_t tci_read_reg32s(uint32_t index)
{
    assert(index < ARRAY_SIZE(tci_reg));
    return (int32_t)(tci_reg[index]);
}
#endif

static uint64_t tci_read_reg64(uint32_t index)
{
    assert(index < ARRAY_SIZE(tci_reg));
    return tci_reg[index];
}

#if 0
static void tcg_write_reg(uint32_t index, tcg_target_ulong value)
{
    assert(index < ARRAY_SIZE(tci_reg));
    assert(index != TCG_AREG0);
    tci_reg[index] = value;
}
#endif

static void tci_write_reg8(uint32_t index, uint8_t value)
{
    assert(index < ARRAY_SIZE(tci_reg));
    assert(index != TCG_AREG0);
    tci_reg[index] = value;
}

static void tci_write_reg8s(uint32_t index, int8_t value)
{
    assert(index < ARRAY_SIZE(tci_reg));
    assert(index != TCG_AREG0);
    tci_reg[index] = value;
}

static void tci_write_reg16s(uint32_t index, int16_t value)
{
    assert(index < ARRAY_SIZE(tci_reg));
    assert(index != TCG_AREG0);
    tci_reg[index] = value;
}

static void tci_write_reg16(uint32_t index, uint16_t value)
{
    assert(index < ARRAY_SIZE(tci_reg));
    assert(index != TCG_AREG0);
    tci_reg[index] = value;
}

static void tci_write_reg32(uint32_t index, uint32_t value)
{
    assert(index < ARRAY_SIZE(tci_reg));
    assert(index != TCG_AREG0);
    tci_reg[index] = value;
}

#if TCG_TARGET_REG_BITS == 64
static void tci_write_reg32s(uint32_t index, int32_t value)
{
    assert(index < ARRAY_SIZE(tci_reg));
    assert(index != TCG_AREG0);
    tci_reg[index] = value;
}

static void tci_write_reg64(uint32_t index, uint64_t value)
{
    assert(index < ARRAY_SIZE(tci_reg));
    assert(index != TCG_AREG0);
    tci_reg[index] = value;
}
#endif

#if TCG_TARGET_REG_BITS == 64
/* Read constant (native size) from bytecode. */
static tcg_target_ulong tci_read_i(uint8_t **tb_ptr)
{
    tcg_target_ulong value = *(tcg_target_ulong *)(*tb_ptr);
    *tb_ptr += sizeof(tcg_target_ulong);
    return value;
}
#endif

/* Read constant (32 bit) from bytecode. */
static uint32_t tci_read_i32(uint8_t **tb_ptr)
{
    uint32_t value = *(uint32_t *)(*tb_ptr);
    *tb_ptr += 4;
    return value;
}

/* Read constant (64 bit) from bytecode. */
static uint64_t tci_read_i64(uint8_t **tb_ptr)
{
    uint64_t value = *(uint64_t *)(*tb_ptr);
    *tb_ptr += 8;
    return value;
}

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

/* Read indexed register (8 bit signed) from bytecode. */
static int8_t tci_read_r8s(uint8_t **tb_ptr)
{
    int8_t value = tci_read_reg8s(**tb_ptr);
    *tb_ptr += 1;
    return value;
}

/* Read indexed register (16 bit) from bytecode. */
static uint16_t tci_read_r16(uint8_t **tb_ptr)
{
    uint16_t value = tci_read_reg16(**tb_ptr);
    *tb_ptr += 1;
    return value;
}

/* Read indexed register (32 bit) from bytecode. */
static uint32_t tci_read_r32(uint8_t **tb_ptr)
{
    uint32_t value = tci_read_reg32(**tb_ptr);
    *tb_ptr += 1;
    return value;
}

#if TCG_TARGET_REG_BITS == 64
/* Read indexed register (32 bit signed) from bytecode. */
static int32_t tci_read_r32s(uint8_t **tb_ptr)
{
    int32_t value = tci_read_reg32s(**tb_ptr);
    *tb_ptr += 1;
    return value;
}
#endif

/* Read indexed register (64 bit) from bytecode. */
static uint64_t tci_read_r64(uint8_t **tb_ptr)
{
    uint64_t value = tci_read_reg64(**tb_ptr);
    *tb_ptr += 1;
    return value;
}

/* Read indexed register or constant (32 bit) from bytecode. */
static uint32_t tci_read_ri32(uint8_t **tb_ptr)
{
    bool const_arg;
    uint32_t value;
    const_arg = **tb_ptr;
    *tb_ptr += 1;
    if (const_arg) {
        value = tci_read_i32(tb_ptr);
    } else {
        value = tci_read_r32(tb_ptr);
    }
    return value;
}

/* Read indexed register or constant (64 bit) from bytecode. */
static uint64_t tci_read_ri64(uint8_t **tb_ptr)
{
    bool const_arg;
    uint64_t value;
    const_arg = **tb_ptr;
    *tb_ptr += 1;
    if (const_arg) {
        value = tci_read_i64(tb_ptr);
    } else {
        value = tci_read_r64(tb_ptr);
    }
    return value;
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

#if TCG_TARGET_REG_BITS == 64
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
#endif

/* Interpret pseudo code in tb. */
unsigned long tcg_qemu_tb_exec(uint8_t *tb_ptr)
{
    unsigned long next_tb = 0;

    TRACE();

    tci_reg[TCG_AREG0] = (tcg_target_ulong)env;

    for (;;) {
        uint8_t opc = *(uint8_t *)tb_ptr++;
        tcg_target_ulong t0, t1, t2;
        tcg_target_ulong label;
        TCGCond condition;
        tci_disas(opc);

        if (opc == INDEX_op_exit_tb) {
            next_tb = *(uint64_t *)tb_ptr;
            break;
        }

        switch (opc) {
        case INDEX_op_end:
        case INDEX_op_nop:
            break;
        case INDEX_op_nop1:
        case INDEX_op_nop2:
        case INDEX_op_nop3:
        case INDEX_op_nopn:
        case INDEX_op_discard:
            TODO();
            break;
        case INDEX_op_set_label:
            TODO();
            break;
        case INDEX_op_call:
            t0 = tci_read_ri64(&tb_ptr);
            t0 = ((helper_function)t0)(tci_read_reg(TCG_REG_R0),
                                       tci_read_reg(TCG_REG_R1),
                                       tci_read_reg(TCG_REG_R2),
                                       tci_read_reg(TCG_REG_R3));
            tci_write_reg32(TCG_REG_R0, t0);
            break;
        case INDEX_op_jmp:
        case INDEX_op_br:
            t0 = *(uint64_t *)tb_ptr;
            tb_ptr = (uint8_t *)t0;
            break;
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
    /* Load/store operations. */
        case INDEX_op_ld8u_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_i32(&tb_ptr);
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
            t2 = tci_read_i32(&tb_ptr);
            tci_write_reg32(t0, *(uint32_t *)(t1 + t2));
            break;
        case INDEX_op_st8_i32:
            t0 = tci_read_r8(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_i32(&tb_ptr);
            *(uint8_t *)(t1 + t2) = t0;
            break;
        case INDEX_op_st16_i32:
            t0 = tci_read_r16(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_i32(&tb_ptr);
            *(uint16_t *)(t1 + t2) = t0;
            break;
        case INDEX_op_st_i32:
            t0 = tci_read_r32(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_i32(&tb_ptr);
            *(uint32_t *)(t1 + t2) = t0;
            break;
    /* Arithmetic operations. */
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
#ifdef TCG_TARGET_HAS_div_i32
        case INDEX_op_div_i32:
        case INDEX_op_divu_i32:
        case INDEX_op_rem_i32:
        case INDEX_op_remu_i32:
            TODO();
            break;
#else
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
    /* Shift/rotate operations. */
        case INDEX_op_shl_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, t1 << t2);
            break;
        case INDEX_op_shr_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, t1 >> t2);
            break;
        case INDEX_op_sar_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_ri32(&tb_ptr);
            t2 = tci_read_ri32(&tb_ptr);
            tci_write_reg32(t0, (t1 >> t2) | (t1 & (1UL << 31)));
            break;
#ifdef TCG_TARGET_HAS_rot_i32
        case INDEX_op_rotl_i32:
        case INDEX_op_rotr_i32:
            TODO();
            break;
#endif
        case INDEX_op_brcond_i32:
            t0 = tci_read_r32(&tb_ptr);
            t1 = tci_read_ri32(&tb_ptr);
            condition = *tb_ptr++;
            label = tci_read_i64(&tb_ptr);
            if (tci_compare32(t0, t1, condition)) {
                tb_ptr = (uint8_t *)label;
            }
            break;
#if TCG_TARGET_REG_BITS == 32
        case INDEX_op_add2_i32:
        case INDEX_op_sub2_i32:
        case INDEX_op_brcond2_i32:
            TODO();
            break;
        case INDEX_op_mulu2_i32:
            TODO();
            break;
#endif
#ifdef TCG_TARGET_HAS_ext8s_i32
        case INDEX_op_ext8s_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r8s(&tb_ptr);
            tci_write_reg32(t0, t1);
            break;
#endif
#ifdef TCG_TARGET_HAS_ext16s_i32
        case INDEX_op_ext16s_i32:
            TODO();
            break;
#endif
#ifdef TCG_TARGET_HAS_bswap16_i32
        case INDEX_op_bswap16_i32:
            TODO();
            break;
#endif
#ifdef TCG_TARGET_HAS_bswap32_i32
        case INDEX_op_bswap32_i32:
            TODO();
            break;
#endif
#ifdef TCG_TARGET_HAS_not_i32
        case INDEX_op_not_i32:
            t0 = *tb_ptr++;
            t1 = tci_read_r32(&tb_ptr);
            tci_write_reg32(t0, ~t1);
            break;
#endif
#ifdef TCG_TARGET_HAS_neg_i32
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
    /* Load/store operations. */
        case INDEX_op_ld8u_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_i32(&tb_ptr);
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
            t2 = tci_read_i32(&tb_ptr);
            tci_write_reg32(t0, *(uint32_t *)(t1 + t2));
            break;
        case INDEX_op_ld32s_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_i32(&tb_ptr);
            tci_write_reg32s(t0, *(int32_t *)(t1 + t2));
            break;
        case INDEX_op_ld_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_i32(&tb_ptr);
            tci_write_reg64(t0, *(uint64_t *)(t1 + t2));
            break;
        case INDEX_op_st8_i64:
            t0 = tci_read_r8(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_i32(&tb_ptr);
            *(uint8_t *)(t1 + t2) = t0;
            break;
        case INDEX_op_st16_i64:
            t0 = tci_read_r16(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_i32(&tb_ptr);
            *(uint16_t *)(t1 + t2) = t0;
            break;
        case INDEX_op_st32_i64:
            t0 = tci_read_r32(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_i32(&tb_ptr);
            *(uint32_t *)(t1 + t2) = t0;
            break;
        case INDEX_op_st_i64:
            t0 = tci_read_r64(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
            t2 = tci_read_i32(&tb_ptr);
            *(uint64_t *)(t1 + t2) = t0;
            break;
    /* Arithmetic operations. */
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
#ifdef TCG_TARGET_HAS_div_i64
        case INDEX_op_div_i64:
        case INDEX_op_divu_i64:
        case INDEX_op_rem_i64:
        case INDEX_op_remu_i64:
            TODO();
            break;
#else
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
    /* Shift/rotate operations. */
        case INDEX_op_shl_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, t1 << t2);
            break;
        case INDEX_op_shr_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, t1 >> t2);
            break;
        case INDEX_op_sar_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_ri64(&tb_ptr);
            t2 = tci_read_ri64(&tb_ptr);
            tci_write_reg64(t0, (t1 >> t2) | (t1 & (1ULL << 63)));
            break;
#ifdef TCG_TARGET_HAS_rot_i64
        case INDEX_op_rotl_i64:
        case INDEX_op_rotr_i64:
            TODO();
            break;
#endif
        case INDEX_op_brcond_i64:
            t0 = tci_read_r64(&tb_ptr);
            t1 = tci_read_ri64(&tb_ptr);
            condition = *tb_ptr++;
            label = tci_read_i(&tb_ptr);
            if (tci_compare64(t0, t1, condition)) {
                tb_ptr = (uint8_t *)label;
            }
            break;
#ifdef TCG_TARGET_HAS_ext8s_i64
        case INDEX_op_ext8s_i64:
            TODO();
            break;
#endif
#ifdef TCG_TARGET_HAS_ext16s_i64
        case INDEX_op_ext16s_i64:
            TODO();
            break;
#endif
#ifdef TCG_TARGET_HAS_ext32s_i64
        case INDEX_op_ext32s_i64:
            t0 = *tb_ptr++;
            t1 = tci_read_r32s(&tb_ptr);
            tci_write_reg64(t0, t1);
            break;
#endif
#ifdef TCG_TARGET_HAS_bswap16_i64
        case INDEX_op_bswap16_i64:
            TODO();
            break;
#endif
#ifdef TCG_TARGET_HAS_bswap32_i64
        case INDEX_op_bswap32_i64:
            TODO();
            break;
#endif
#ifdef TCG_TARGET_HAS_bswap64_i64
        case INDEX_op_bswap64_i64:
            TODO();
            break;
#endif
#ifdef TCG_TARGET_HAS_not_i64
        case INDEX_op_not_i64:
            TODO();
            break;
#endif
#ifdef TCG_TARGET_HAS_neg_i64
        case INDEX_op_neg_i64:
            TODO();
            break;
#endif
#endif /* TCG_TARGET_REG_BITS == 64 */
    /* QEMU specific */
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
        case INDEX_op_debug_insn_start:
            TODO();
            break;
#else
        case INDEX_op_debug_insn_start:
            TODO();
            break;
#endif
        case INDEX_op_exit_tb:
            TODO();
            break;
        case INDEX_op_goto_tb:
            t0 = tci_read_i32(&tb_ptr);
            tb_ptr += (int32_t)t0;
            break;
#if TCG_TARGET_REG_BITS == 32
        case INDEX_op_qemu_ld8u:
            /* Same code for 32 or 64 bit? */
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            tci_write_reg8(t0, __ldb_mmu(t1, t2));
#else
            tci_write_reg8(t0, *(uint8_t *)(t1 + GUEST_BASE));
#endif
            break;
        case INDEX_op_qemu_ld8s:
            /* Same code for 32 or 64 bit? */
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            tci_write_reg8s(t0, __ldb_mmu(t1, t2));
#else
            tci_write_reg8s(t0, *(int8_t *)(t1 + GUEST_BASE));
#endif
            break;
        case INDEX_op_qemu_ld16u:
            /* Same code for 32 or 64 bit? */
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            tci_write_reg16(t0, __ldw_mmu(t1, t2));
#else
            tci_write_reg16(t0, *(uint16_t *)(t1 + GUEST_BASE));
#endif
            break;
        case INDEX_op_qemu_ld16s:
            /* Same code for 32 or 64 bit? */
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            tci_write_reg16s(t0, __ldw_mmu(t1, t2));
#else
            tci_write_reg16s(t0, *(int16_t *)(t1 + GUEST_BASE));
#endif
            break;
        case INDEX_op_qemu_ld32u:
            /* Same code for 32 or 64 bit? */
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            tci_write_reg32(t0, __ldl_mmu(t1, t2));
#else
            tci_write_reg32(t0, *(uint32_t *)(t1 + GUEST_BASE));
#endif
            break;
        case INDEX_op_qemu_ld32s:
            TODO();
            break;
        case INDEX_op_qemu_ld64:
            TODO();
            break;
#else /* TCG_TARGET_REG_BITS == 32 */
        case INDEX_op_qemu_ld8u:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            tci_write_reg8(t0, __ldb_mmu(t1, t2));
#else
            tci_write_reg8(t0, *(uint8_t *)(t1 + GUEST_BASE));
#endif
            break;
        case INDEX_op_qemu_ld8s:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            tci_write_reg8s(t0, __ldb_mmu(t1, t2));
#else
            tci_write_reg8s(t0, *(int8_t *)(t1 + GUEST_BASE));
#endif
            break;
        case INDEX_op_qemu_ld16u:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            tci_write_reg16(t0, __ldw_mmu(t1, t2));
#else
            tci_write_reg16(t0, *(uint16_t *)(t1 + GUEST_BASE));
#endif
            break;
        case INDEX_op_qemu_ld16s:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            tci_write_reg16s(t0, __ldw_mmu(t1, t2));
#else
            tci_write_reg16s(t0, *(int16_t *)(t1 + GUEST_BASE));
#endif
            break;
        case INDEX_op_qemu_ld32u:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            tci_write_reg32(t0, __ldl_mmu(t1, t2));
#else
            tci_write_reg32(t0, *(uint32_t *)(t1 + GUEST_BASE));
#endif
            break;
        case INDEX_op_qemu_ld32s:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            tci_write_reg32s(t0, __ldl_mmu(t1, t2));
#else
            tci_write_reg32s(t0, *(int32_t *)(t1 + GUEST_BASE));
#endif
            break;
        case INDEX_op_qemu_ld64:
            t0 = *tb_ptr++;
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            tci_write_reg64(t0, __ldq_mmu(t1, t2));
#else
            tci_write_reg64(t0, *(uint64_t *)(t1 + GUEST_BASE));
#endif
            break;
#endif /* TCG_TARGET_REG_BITS != 32 */
        case INDEX_op_qemu_st8:
            t0 = tci_read_r8(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            __stb_mmu(t1, t0, t2);
#else
            *(uint8_t *)(t1 + GUEST_BASE) = t0;
#endif
            break;
        case INDEX_op_qemu_st16:
            t0 = tci_read_r16(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            __stw_mmu(t1, t0, t2);
#else
            *(uint16_t *)(t1 + GUEST_BASE) = t0;
#endif
            break;
        case INDEX_op_qemu_st32:
            t0 = tci_read_r32(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            __stl_mmu(t1, t0, t2);
#else
            *(uint32_t *)(t1 + GUEST_BASE) = t0;
#endif
            break;
        case INDEX_op_qemu_st64:
            t0 = tci_read_r64(&tb_ptr);
            t1 = tci_read_r(&tb_ptr);
#ifdef CONFIG_SOFTMMU
            t2 = tci_read_i(&tb_ptr);
            __stq_mmu(t1, t0, t2);
#else
            *(uint64_t *)(t1 + GUEST_BASE) = t0;
#endif
            break;
        default:
            TODO();
            break;
        }
    }
    return next_tb;
}
