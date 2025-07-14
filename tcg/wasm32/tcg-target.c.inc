/* SPDX-License-Identifier: MIT */
/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009, 2011 Stefan Weil
 * Copyright (c) 2018 SiFive, Inc
 * Copyright (c) 2008-2009 Arnaud Patard <arnaud.patard@rtp-net.org>
 * Copyright (c) 2009 Aurelien Jarno <aurelien@aurel32.net>
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Based on tci/tcg-target.c.inc and riscv/tcg-target.c.inc
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

#include "../wasm32.h"

/*
 * This is included to get the max number of threads via tcg_max_ctxs.
 */
#include "../tcg-internal.h"

/* Used for function call generation. */
#define TCG_TARGET_CALL_STACK_OFFSET    0
#define TCG_TARGET_STACK_ALIGN          8
#if TCG_TARGET_REG_BITS == 32
# define TCG_TARGET_CALL_ARG_I32        TCG_CALL_ARG_EVEN
# define TCG_TARGET_CALL_ARG_I64        TCG_CALL_ARG_EVEN
# define TCG_TARGET_CALL_ARG_I128       TCG_CALL_ARG_EVEN
#else
# define TCG_TARGET_CALL_ARG_I32        TCG_CALL_ARG_NORMAL
# define TCG_TARGET_CALL_ARG_I64        TCG_CALL_ARG_NORMAL
# define TCG_TARGET_CALL_ARG_I128       TCG_CALL_ARG_NORMAL
#endif
#define TCG_TARGET_CALL_RET_I128        TCG_CALL_RET_NORMAL

typedef uint32_t tcg_insn_unit_tci;

static TCGConstraintSetIndex
tcg_target_op_def(TCGOpcode op, TCGType type, unsigned flags)
{
    return C_NotImplemented;
}

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_R15,
    /* Either 2 or 4 of these are call clobbered, so use them last. */
    TCG_REG_R3,
    TCG_REG_R2,
    TCG_REG_R1,
    TCG_REG_R0,
};

/* No call arguments via registers.  All will be stored on the "stack". */
static const int tcg_target_call_iarg_regs[] = { };

static TCGReg tcg_target_call_oarg_reg(TCGCallReturnKind kind, int slot)
{
    tcg_debug_assert(kind == TCG_CALL_RET_NORMAL);
    tcg_debug_assert(slot >= 0 && slot < 128 / TCG_TARGET_REG_BITS);
    return TCG_REG_R0 + slot;
}

#ifdef CONFIG_DEBUG_TCG
static const char *const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "r00",
    "r01",
    "r02",
    "r03",
    "r04",
    "r05",
    "r06",
    "r07",
    "r08",
    "r09",
    "r10",
    "r11",
    "r12",
    "r13",
    "r14",
    "r15",
};
#endif

/* converts a TCG register to a wasm variable index */
static const uint8_t tcg_target_reg_index[TCG_TARGET_NB_REGS] = {
    0,  /* TCG_REG_R0 */
    1,  /* TCG_REG_R1 */
    2,  /* TCG_REG_R2 */
    3,  /* TCG_REG_R3 */
    4,  /* TCG_REG_R4 */
    5,  /* TCG_REG_R5 */
    6,  /* TCG_REG_R6 */
    7,  /* TCG_REG_R7 */
    8,  /* TCG_REG_R8 */
    9,  /* TCG_REG_R9 */
    10, /* TCG_REG_R10 */
    11, /* TCG_REG_R11 */
    12, /* TCG_REG_R12 */
    13, /* TCG_REG_R13 */
    14, /* TCG_REG_R14 */
    15, /* TCG_REG_R15 */
};

/*
 * Global variable to store the carry flag
 */
#define CARRY_IDX 16

/*
 * Global variable Index used for storing the current block index
 */
#define BLOCK_PTR_IDX 17

/*
 * pointer to wasmContext
 */
#define CTX_IDX 0

/* Temporary local variables */
#define TMP32_LOCAL_0_IDX 1
#define TMP32_LOCAL_1_IDX 2
#define TMP32_LOCAL_2_IDX 3
#define TMP64_LOCAL_0_IDX 4
#define TMP64_LOCAL_1_IDX 5

/* function index */
#define CHECK_UNWINDING_IDX 0 /* a funtion of checking Asyncify status */
#define HELPER_IDX_START 1 /* helper funcitons */

#if TCG_TARGET_REG_BITS == 32
#define PTR_TYPE 0x7f
#else
#define PTR_TYPE 0x7e
#endif

#define BUF_SIZE 1024
typedef struct LinkedBuf {
    struct LinkedBuf *next;
    uint8_t data[BUF_SIZE];
    uint32_t size;
} LinkedBuf;

static LinkedBuf *new_linked_buf(void)
{
    LinkedBuf *p = tcg_malloc(sizeof(LinkedBuf));
    p->size = 0;
    p->next = NULL;
    return p;
}

static inline LinkedBuf *linked_buf_out8(LinkedBuf *buf, uint8_t v)
{
    if (buf->size == BUF_SIZE) {
        buf->next = new_linked_buf();
        buf = buf->next;
    }
    buf->data[buf->size++] = v;
    return buf;
}

static inline int linked_buf_len(LinkedBuf *buf)
{
    int total = 0;
    for (LinkedBuf *p = buf; p; p = p->next) {
        total += p->size;
    }
    return total;
}

static inline void linked_buf_write(LinkedBuf *buf, void *dst)
{
    for (LinkedBuf *p = buf; p; p = p->next) {
        memcpy(dst, p->data, p->size);
        dst += p->size;
    }
}

/*
 * wasm code is generataed in the dynamically allocated buffer which
 * are managed as a linked list.
 */
__thread LinkedBuf *sub_buf_root;
__thread LinkedBuf *sub_buf_cur;

static void init_sub_buf(void)
{
    sub_buf_root = new_linked_buf();
    sub_buf_cur = sub_buf_root;
}

static inline int sub_buf_len(void)
{
    return linked_buf_len(sub_buf_root);
}

static inline void tcg_wasm_out8(TCGContext *s, uint32_t v)
{
    sub_buf_cur = linked_buf_out8(sub_buf_cur, v);
}

static void tcg_wasm_out_op_i64_and(TCGContext *s)
{
    tcg_wasm_out8(s, 0x83);
}
static void tcg_wasm_out_op_i64_or(TCGContext *s)
{
    tcg_wasm_out8(s, 0x84);
}
static void tcg_wasm_out_op_i64_xor(TCGContext *s)
{
    tcg_wasm_out8(s, 0x85);
}
static void tcg_wasm_out_op_i64_clz(TCGContext *s)
{
    tcg_wasm_out8(s, 0x79);
}
static void tcg_wasm_out_op_i64_ctz(TCGContext *s)
{
    tcg_wasm_out8(s, 0x7a);
}
static void tcg_wasm_out_op_i32_popcnt(TCGContext *s)
{
    tcg_wasm_out8(s, 0x79);
}
static void tcg_wasm_out_op_i64_popcnt(TCGContext *s)
{
    tcg_wasm_out8(s, 0x7b);
}
static void tcg_wasm_out_op_i64_add(TCGContext *s)
{
    tcg_wasm_out8(s, 0x7c);
}
static void tcg_wasm_out_op_i64_sub(TCGContext *s)
{
    tcg_wasm_out8(s, 0x7d);
}
static void tcg_wasm_out_op_i64_mul(TCGContext *s)
{
    tcg_wasm_out8(s, 0x7e);
}
static void tcg_wasm_out_op_i64_div_s(TCGContext *s)
{
    tcg_wasm_out8(s, 0x7f);
}
static void tcg_wasm_out_op_i64_div_u(TCGContext *s)
{
    tcg_wasm_out8(s, 0x80);
}
static void tcg_wasm_out_op_i64_rem_s(TCGContext *s)
{
    tcg_wasm_out8(s, 0x81);
}
static void tcg_wasm_out_op_i64_rem_u(TCGContext *s)
{
    tcg_wasm_out8(s, 0x82);
}
static void tcg_wasm_out_op_i64_shl(TCGContext *s)
{
    tcg_wasm_out8(s, 0x86);
}
static void tcg_wasm_out_op_i64_shr_s(TCGContext *s)
{
    tcg_wasm_out8(s, 0x87);
}
static void tcg_wasm_out_op_i64_shr_u(TCGContext *s)
{
    tcg_wasm_out8(s, 0x88);
}
static void tcg_wasm_out_op_i64_rotl(TCGContext *s)
{
    tcg_wasm_out8(s, 0x89);
}
static void tcg_wasm_out_op_i64_rotr(TCGContext *s)
{
    tcg_wasm_out8(s, 0x8a);
}
static void tcg_wasm_out_op_i32_wrap_i64(TCGContext *s)
{
    tcg_wasm_out8(s, 0xa7);
}
static void tcg_wasm_out_op_i64_extend_i32_s(TCGContext *s)
{
    tcg_wasm_out8(s, 0xac);
}
static void tcg_wasm_out_op_i64_extend_i32_u(TCGContext *s)
{
    tcg_wasm_out8(s, 0xad);
}
static void tcg_wasm_out_op_i64_extend8_s(TCGContext *s)
{
    tcg_wasm_out8(s, 0xc2);
}
static void tcg_wasm_out_op_i64_extend16_s(TCGContext *s)
{
    tcg_wasm_out8(s, 0xc3);
}
static void tcg_wasm_out_op_i32_clz(TCGContext *s)
{
    tcg_wasm_out8(s, 0x67);
}
static void tcg_wasm_out_op_i32_ctz(TCGContext *s)
{
    tcg_wasm_out8(s, 0x68);
}
static void tcg_wasm_out_op_i32_add(TCGContext *s)
{
    tcg_wasm_out8(s, 0x6a);
}
static void tcg_wasm_out_op_i32_and(TCGContext *s)
{
    tcg_wasm_out8(s, 0x71);
}
static void tcg_wasm_out_op_i32_or(TCGContext *s)
{
    tcg_wasm_out8(s, 0x72);
}
static void tcg_wasm_out_op_i32_shl(TCGContext *s)
{
    tcg_wasm_out8(s, 0x74);
}
static void tcg_wasm_out_op_i32_shr_s(TCGContext *s)
{
    tcg_wasm_out8(s, 0x75);
}
static void tcg_wasm_out_op_i32_shr_u(TCGContext *s)
{
    tcg_wasm_out8(s, 0x76);
}
static void tcg_wasm_out_op_i32_rotl(TCGContext *s)
{
    tcg_wasm_out8(s, 0x77);
}
static void tcg_wasm_out_op_i32_rotr(TCGContext *s)
{
    tcg_wasm_out8(s, 0x78);
}
static void tcg_wasm_out_op_i32_eqz(TCGContext *s)
{
    tcg_wasm_out8(s, 0x45);
}
static void tcg_wasm_out_op_i32_eq(TCGContext *s)
{
    tcg_wasm_out8(s, 0x46);
}
static void tcg_wasm_out_op_i32_ne(TCGContext *s)
{
    tcg_wasm_out8(s, 0x47);
}
static void tcg_wasm_out_op_i64_lt_u(TCGContext *s)
{
    tcg_wasm_out8(s, 0x54);
}
static void tcg_wasm_out_op_i64_le_u(TCGContext *s)
{
    tcg_wasm_out8(s, 0x58);
}
static void tcg_wasm_out_op_i64_eqz(TCGContext *s)
{
    tcg_wasm_out8(s, 0x50);
}
static void tcg_wasm_out_op_i64_eq(TCGContext *s)
{
    tcg_wasm_out8(s, 0x51);
}
static void tcg_wasm_out_op_i64_ne(TCGContext *s)
{
    tcg_wasm_out8(s, 0x52);
}
static void tcg_wasm_out_op_br(TCGContext *s, int i)
{
    tcg_wasm_out8(s, 0x0c);
    tcg_wasm_out8(s, i);
}
static void tcg_wasm_out_op_loop_noret(TCGContext *s)
{
    tcg_wasm_out8(s, 0x03);
    tcg_wasm_out8(s, 0x40);
}
static void tcg_wasm_out_op_if_noret(TCGContext *s)
{
    tcg_wasm_out8(s, 0x04);
    tcg_wasm_out8(s, 0x40);
}

static void tcg_wasm_out_op_if_ret_i64(TCGContext *s)
{
    tcg_wasm_out8(s, 0x04);
    tcg_wasm_out8(s, 0x7e);
}
static void tcg_wasm_out_op_if_ret_i32(TCGContext *s)
{
    tcg_wasm_out8(s, 0x04);
    tcg_wasm_out8(s, 0x7f);
}
static void tcg_wasm_out_op_else(TCGContext *s)
{
    tcg_wasm_out8(s, 0x05);
}
static void tcg_wasm_out_op_end(TCGContext *s)
{
    tcg_wasm_out8(s, 0x0b);
}
static void tcg_wasm_out_op_return(TCGContext *s)
{
    tcg_wasm_out8(s, 0x0f);
}
static void tcg_wasm_out_op_var(TCGContext *s, uint8_t instr, uint8_t i)
{
    tcg_wasm_out8(s, instr);
    tcg_wasm_out8(s, i);
}
static void tcg_wasm_out_op_global_get(TCGContext *s, uint8_t i)
{
    tcg_wasm_out_op_var(s, 0x23, i);
}
static void tcg_wasm_out_op_global_set(TCGContext *s, uint8_t i)
{
    tcg_wasm_out_op_var(s, 0x24, i);
}
static void tcg_wasm_out_op_global_get_r(TCGContext *s, TCGReg r0)
{
    tcg_wasm_out_op_global_get(s, tcg_target_reg_index[r0]);
}
static void tcg_wasm_out_op_global_set_r(TCGContext *s, TCGReg r0)
{
    tcg_wasm_out_op_global_set(s, tcg_target_reg_index[r0]);
}
static void tcg_wasm_out_op_global_get_r_wrap(TCGContext *s, TCGType type, TCGReg r0)
{
    tcg_wasm_out_op_global_get_r(s, r0);
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_i32_wrap_i64(s);
        break;
    case TCG_TYPE_I64:
        break;
    default:
        g_assert_not_reached();
    }
}
static void tcg_wasm_out_op_local_get(TCGContext *s, uint8_t i)
{
    tcg_wasm_out_op_var(s, 0x20, i);
}
static void tcg_wasm_out_op_local_set(TCGContext *s, uint8_t i)
{
    tcg_wasm_out_op_var(s, 0x21, i);
}
static void tcg_wasm_out_op_local_tee(TCGContext *s, uint8_t i)
{
    tcg_wasm_out_op_var(s, 0x22, i);
}

#define tcg_wasm_out_i64_calc(op)                                       \
    static void tcg_wasm_out_i64_calc_##op(                             \
        TCGContext *s, TCGReg ret, TCGReg arg1, TCGReg arg2)            \
    {                                                                   \
        tcg_wasm_out_op_global_get_r(s, arg1);                          \
        tcg_wasm_out_op_global_get_r(s, arg2);                          \
        tcg_wasm_out_op_i64_##op(s);                                    \
        tcg_wasm_out_op_global_set_r(s, ret);                           \
    }
tcg_wasm_out_i64_calc(and);
tcg_wasm_out_i64_calc(or);
tcg_wasm_out_i64_calc(xor);
tcg_wasm_out_i64_calc(add);
tcg_wasm_out_i64_calc(sub);
tcg_wasm_out_i64_calc(mul);
tcg_wasm_out_i64_calc(rotl);
tcg_wasm_out_i64_calc(rotr);

static const struct {
    uint8_t i32;
    uint8_t i64;
} tcg_cond_to_inst[] = {
    [TCG_COND_EQ] =  { 0x46 /* i32.eq */   , 0x51 /* i64.eq */},
    [TCG_COND_NE] =  { 0x47 /* i32.ne */   , 0x52 /* i64.ne */},
    [TCG_COND_LT] =  { 0x48 /* i32.lt_s */ , 0x53 /* i64.lt_s */},
    [TCG_COND_GE] =  { 0x4e /* i32.ge_s */ , 0x59 /* i64.ge_s */},
    [TCG_COND_LE] =  { 0x4c /* i32.le_s */ , 0x57 /* i64.le_s */},
    [TCG_COND_GT] =  { 0x4a /* i32.gt_s */ , 0x55 /* i64.gt_s */},
    [TCG_COND_LTU] = { 0x49 /* i32.lt_u */ , 0x54 /* i64.lt_u */},
    [TCG_COND_GEU] = { 0x4f /* i32.ge_u */ , 0x5a /* i64.ge_u */},
    [TCG_COND_LEU] = { 0x4d /* i32.le_u */ , 0x58 /* i64.le_u */},
    [TCG_COND_GTU] = { 0x4b /* i32.gt_u */ , 0x56 /* i64.gt_u */}
};

static void tcg_wasm_out_op_cond_i64(
    TCGContext *s, TCGCond cond, TCGReg arg1, TCGReg arg2)
{
    uint8_t op = tcg_cond_to_inst[cond].i64;
    tcg_wasm_out_op_global_get_r(s, arg1);
    tcg_wasm_out_op_global_get_r(s, arg2);
    tcg_wasm_out8(s, op);
}

static void tcg_wasm_out_op_cond_i32(
    TCGContext *s, TCGCond cond, TCGReg arg1, TCGReg arg2)
{
    uint8_t op = tcg_cond_to_inst[cond].i32;
    tcg_wasm_out_op_global_get_r(s, arg1);
    tcg_wasm_out_op_i32_wrap_i64(s);
    tcg_wasm_out_op_global_get_r(s, arg2);
    tcg_wasm_out_op_i32_wrap_i64(s);
    tcg_wasm_out8(s, op);
}

static void fill_uint32_leb128(uint8_t *b, uint32_t v)
{
    do {
        *b |= v & 0x7f;
        v >>= 7;
        b++;
    } while (v != 0);
}

static int write_uint32_leb128(uint8_t *b, uint32_t v)
{
    uint8_t *base = b;
    do {
        *b = v & 0x7f;
        v >>= 7;
        if (v != 0) {
            *b |= 0x80;
        }
        b++;
    } while (v != 0);

    return (int)(b - base);
}

static void tcg_wasm_out_leb128_sint32_t(TCGContext *s, int32_t v)
{
    bool more = true;
    uint8_t b;
    while (more) {
        b = v & 0x7f;
        v >>= 7;
        if (((v == 0) && ((b & 0x40) == 0)) ||
            ((v == -1) && ((b & 0x40) != 0))) {
            more = false;
        } else {
            b |= 0x80;
        }
        tcg_wasm_out8(s, b);
    }
}

static void tcg_wasm_out_leb128_sint64_t(TCGContext *s, int64_t v)
{
    bool more = true;
    uint8_t b;
    while (more) {
        b = v & 0x7f;
        v >>= 7;
        if (((v == 0) && ((b & 0x40) == 0)) ||
            ((v == -1) && ((b & 0x40) != 0))) {
            more = false;
        } else {
            b |= 0x80;
        }
        tcg_wasm_out8(s, b);
    }
}

static void tcg_wasm_out_leb128_uint32_t(TCGContext *s, uint32_t v)
{
    uint8_t b;
    do {
        b = v & 0x7f;
        v >>= 7;
        if (v != 0) {
            b |= 0x80;
        }
        tcg_wasm_out8(s, b);
    } while (v != 0);
}

static void tcg_wasm_out_leb128_uint64_t(TCGContext *s, uint64_t v)
{
    uint8_t b;
    do {
        b = v & 0x7f;
        v >>= 7;
        if (v != 0) {
            b |= 0x80;
        }
        tcg_wasm_out8(s, b);
    } while (v != 0);
}

static void tcg_wasm_out_op_i32_const(TCGContext *s, int32_t v)
{
    tcg_wasm_out8(s, 0x41);
    tcg_wasm_out_leb128_sint32_t(s, v);
}

static void tcg_wasm_out_op_i64_const(TCGContext *s, int64_t v)
{
    tcg_wasm_out8(s, 0x42);
    tcg_wasm_out_leb128_sint64_t(s, v);
}

static void tcg_wasm_out_leb128_ptr_t(TCGContext *s, intptr_t func_idx)
{
#if TCG_TARGET_REG_BITS == 32
    tcg_wasm_out_leb128_uint32_t(s, func_idx);
#else
    tcg_wasm_out_leb128_uint64_t(s, func_idx);
#endif
}

static void tcg_wasm_out_op_loadstore(
    TCGContext *s, uint8_t instr, uint32_t a, uint32_t o)
{
    tcg_wasm_out8(s, instr);
    tcg_wasm_out_leb128_uint32_t(s, a);
    tcg_wasm_out_leb128_uint32_t(s, o);
}

static void tcg_wasm_out_op_i64_store(TCGContext *s, uint32_t a, uint32_t o)
{
    tcg_wasm_out_op_loadstore(s, 0x37, a, o);
}
static void tcg_wasm_out_op_i64_store8(TCGContext *s, uint32_t a, uint32_t o)
{
    tcg_wasm_out_op_loadstore(s, 0x3c, a, o);
}

static void tcg_wasm_out_op_i64_store16(TCGContext *s, uint32_t a, uint32_t o)
{
    tcg_wasm_out_op_loadstore(s, 0x3d, a, o);
}

static void tcg_wasm_out_op_i64_store32(TCGContext *s, uint32_t a, uint32_t o)
{
    tcg_wasm_out_op_loadstore(s, 0x3e, a, o);
}

static void tcg_wasm_out_op_i64_load(TCGContext *s, uint32_t a, uint32_t o)
{
    tcg_wasm_out_op_loadstore(s, 0x29, a, o);
}

 static void tcg_wasm_out_op_i64_load8_s(TCGContext *s, uint32_t a, uint32_t o)
{
    tcg_wasm_out_op_loadstore(s, 0x30, a, o);
}

static void tcg_wasm_out_op_i64_load8_u(TCGContext *s, uint32_t a, uint32_t o)
{
    tcg_wasm_out_op_loadstore(s, 0x31, a, o);
}

static void tcg_wasm_out_op_i64_load16_s(TCGContext *s, uint32_t a, uint32_t o)
{
    tcg_wasm_out_op_loadstore(s, 0x32, a, o);
}

static void tcg_wasm_out_op_i64_load16_u(TCGContext *s, uint32_t a, uint32_t o)
{
    tcg_wasm_out_op_loadstore(s, 0x33, a, o);
}

static void tcg_wasm_out_op_i64_load32_s(TCGContext *s, uint32_t a, uint32_t o)
{
    tcg_wasm_out_op_loadstore(s, 0x34, a, o);
}

static void tcg_wasm_out_op_i64_load32_u(TCGContext *s, uint32_t a, uint32_t o)
{
    tcg_wasm_out_op_loadstore(s, 0x35, a, o);
}

static void tcg_wasm_out_op_i32_load(TCGContext *s, uint32_t a, uint32_t o)
{
    tcg_wasm_out_op_loadstore(s, 0x28, a, o);
}

static void tcg_wasm_out_op_i32_store(TCGContext *s, uint32_t a, uint32_t o)
{
    tcg_wasm_out_op_loadstore(s, 0x36, a, o);
}

static void tcg_wasm_out_op_call(TCGContext *s, intptr_t func_idx)
{
    tcg_wasm_out8(s, 0x10);
    tcg_wasm_out_leb128_ptr_t(s, func_idx);
}

static void tcg_wasm_out_op_not(TCGContext *s)
{
    tcg_wasm_out_op_i64_const(s, -1);
    tcg_wasm_out_op_i64_xor(s);
}

static void tcg_wasm_out_andc(
    TCGContext *s, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    tcg_wasm_out_op_global_get_r(s, arg1);
    tcg_wasm_out_op_global_get_r(s, arg2);
    tcg_wasm_out_op_not(s);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_global_set_r(s, ret);
}

static void tcg_wasm_out_orc(
    TCGContext *s, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    tcg_wasm_out_op_global_get_r(s, arg1);
    tcg_wasm_out_op_global_get_r(s, arg2);
    tcg_wasm_out_op_not(s);
    tcg_wasm_out_op_i64_or(s);
    tcg_wasm_out_op_global_set_r(s, ret);
}

static void tcg_wasm_out_eqv(
    TCGContext *s, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    tcg_wasm_out_op_global_get_r(s, arg1);
    tcg_wasm_out_op_global_get_r(s, arg2);
    tcg_wasm_out_op_i64_xor(s);
    tcg_wasm_out_op_not(s);
    tcg_wasm_out_op_global_set_r(s, ret);
}

static void tcg_wasm_out_nand(
    TCGContext *s, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    tcg_wasm_out_op_global_get_r(s, arg1);
    tcg_wasm_out_op_global_get_r(s, arg2);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_not(s);
    tcg_wasm_out_op_global_set_r(s, ret);
}

static void tcg_wasm_out_nor(
    TCGContext *s, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    tcg_wasm_out_op_global_get_r(s, arg1);
    tcg_wasm_out_op_global_get_r(s, arg2);
    tcg_wasm_out_op_i64_or(s);
    tcg_wasm_out_op_not(s);
    tcg_wasm_out_op_global_set_r(s, ret);
}

static void tcg_wasm_out_neg(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_wasm_out_op_global_get_r(s, arg);
    tcg_wasm_out_op_not(s);
    tcg_wasm_out_op_i64_const(s, 1);
    tcg_wasm_out_op_i64_add(s);
    tcg_wasm_out_op_global_set_r(s, ret);
}

static void tcg_wasm_out_not(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_wasm_out_op_global_get_r(s, arg);
    tcg_wasm_out_op_not(s);
    tcg_wasm_out_op_global_set_r(s, ret);
}

static void tcg_wasm_out_ctpop(TCGContext *s, TCGReg dest, TCGReg src)
{
    tcg_wasm_out_op_global_get_r(s, src);
#if TCG_TARGET_REG_BITS == 32
    tcg_wasm_out_op_i32_wrap_i64(s);
    tcg_wasm_out_op_i32_popcnt(s);
    tcg_wasm_out_op_i64_extend_i32_u(s);
#else
    tcg_wasm_out_op_i64_popcnt(s);
#endif
    tcg_wasm_out_op_global_set_r(s, dest);
}

static void tcg_wasm_out_shl(TCGContext *s, TCGReg ret,
                             TCGReg arg1, TCGReg arg2)
{
    tcg_wasm_out_op_global_get_r(s, arg1);
    tcg_wasm_out_op_global_get_r(s, arg2);
    tcg_wasm_out_op_i64_shl(s);
    tcg_wasm_out_op_global_set_r(s, ret);
}

static void tcg_wasm_out_shr_u(
    TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i64_const(s, 0xffffffff);
        tcg_wasm_out_op_i64_and(s);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i64_const(s, 0x7f);
        tcg_wasm_out_op_i64_and(s);
        tcg_wasm_out_op_i64_shr_u(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i64_shr_u(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_shr_s(
    TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_i64_extend_i32_s(s);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i64_const(s, 0x7f);
        tcg_wasm_out_op_i64_and(s);
        tcg_wasm_out_op_i64_shr_s(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i64_shr_s(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_setcond(TCGContext *s, TCGType type, TCGReg ret,
                                 TCGReg arg1, TCGReg arg2, TCGCond cond)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_cond_i32(s, cond, arg1, arg2);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_cond_i64(s, cond, arg1, arg2);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_wasm_out_op_i64_extend_i32_u(s);
    tcg_wasm_out_op_global_set_r(s, ret);
}

static void tcg_wasm_out_negsetcond(TCGContext *s, TCGType type, TCGReg ret,
                                    TCGReg arg1, TCGReg arg2, TCGCond cond)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_cond_i32(s, cond, arg1, arg2);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_cond_i64(s, cond, arg1, arg2);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_wasm_out_op_i64_extend_i32_u(s);
    tcg_wasm_out_op_not(s);
    tcg_wasm_out_op_i64_const(s, 1);
    tcg_wasm_out_op_i64_add(s);
    tcg_wasm_out_op_global_set_r(s, ret);
}

static void tcg_wasm_out_movcond(TCGContext *s, TCGType type, TCGReg ret,
                                 TCGReg c1, TCGReg c2,
                                 TCGReg v1, TCGReg v2,
                                 TCGCond cond)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_cond_i32(s, cond, c1, c2);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_cond_i64(s, cond, c1, c2);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_wasm_out_op_if_ret_i64(s);
    tcg_wasm_out_op_global_get_r(s, v1);
    tcg_wasm_out_op_else(s);
    tcg_wasm_out_op_global_get_r(s, v2);
    tcg_wasm_out_op_end(s);
    tcg_wasm_out_op_global_set_r(s, ret);
}

static void tcg_wasm_out_deposit(TCGContext *s,
                                 TCGReg dest, TCGReg arg1, TCGReg arg2,
                                 int pos, int len)
{
    int64_t mask = (((int64_t)1 << len) - 1) << pos;
    tcg_wasm_out_op_global_get_r(s, arg1);
    tcg_wasm_out_op_i64_const(s, ~mask);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_global_get_r(s, arg2);
    tcg_wasm_out_op_i64_const(s, pos);
    tcg_wasm_out_op_i64_shl(s);
    tcg_wasm_out_op_i64_const(s, mask);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_i64_or(s);
    tcg_wasm_out_op_global_set_r(s, dest);
}

static void tcg_wasm_out_extract(TCGContext *s, TCGReg dest, TCGReg arg1,
                                 int pos, int len)
{
    int64_t mask = ~0ULL >> (64 - len);
    tcg_wasm_out_op_global_get_r(s, arg1);
    tcg_wasm_out_op_i64_const(s, pos);
    tcg_wasm_out_op_i64_shr_u(s);
    tcg_wasm_out_op_i64_const(s, mask);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_global_set_r(s, dest);
}

static void tcg_wasm_out_sextract(TCGContext *s, TCGReg dest, TCGReg arg1,
                                  int pos, int len)
{
    int rs = 64 - len;
    int sl = rs - pos;
    tcg_wasm_out_op_global_get_r(s, arg1);
    if (sl > 0) {
        tcg_wasm_out_op_i64_const(s, sl);
        tcg_wasm_out_op_i64_shl(s);
    }
    tcg_wasm_out_op_i64_const(s, rs);
    tcg_wasm_out_op_i64_shr_s(s);
    tcg_wasm_out_op_global_set_r(s, dest);
}

static void tcg_wasm_out_ext8s(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_wasm_out_op_global_get_r(s, rs);
    tcg_wasm_out_op_i64_extend8_s(s);
    tcg_wasm_out_op_global_set_r(s, rd);
}

static void tcg_wasm_out_ext8u(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_wasm_out_op_global_get_r(s, rs);
    tcg_wasm_out_op_i64_const(s, 0xff);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_global_set_r(s, rd);
}

static void tcg_wasm_out_ext16s(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_wasm_out_op_global_get_r(s, rs);
    tcg_wasm_out_op_i64_extend16_s(s);
    tcg_wasm_out_op_global_set_r(s, rd);
}

static void tcg_wasm_out_ext16u(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_wasm_out_op_global_get_r(s, rs);
    tcg_wasm_out_op_i64_const(s, 0xffff);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_global_set_r(s, rd);
}

static void tcg_wasm_out_ext32s(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_wasm_out_op_global_get_r(s, rs);
    tcg_wasm_out_op_i32_wrap_i64(s);
    tcg_wasm_out_op_i64_extend_i32_s(s);
    tcg_wasm_out_op_global_set_r(s, rd);
}

static void tcg_wasm_out_ext32u(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_wasm_out_op_global_get_r(s, rs);
    tcg_wasm_out_op_i64_const(s, 0xffffffff);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_global_set_r(s, rd);
}

static void tcg_wasm_out_extrl_i64_i32(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_wasm_out_op_global_get_r(s, rs);
    tcg_wasm_out_op_i64_const(s, 0xffffffff);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_global_set_r(s, rd);
}

static void tcg_wasm_out_bswap64(
    TCGContext *s, TCGReg dest, TCGReg src)
{
    tcg_wasm_out_op_global_get_r(s, src);
    tcg_wasm_out_op_i64_const(s, 32);
    tcg_wasm_out_op_i64_rotr(s);
    tcg_wasm_out_op_local_set(s, TMP64_LOCAL_0_IDX);

    tcg_wasm_out_op_local_get(s, TMP64_LOCAL_0_IDX);
    tcg_wasm_out_op_i64_const(s, 0xff000000ff000000);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_i64_const(s, 24);
    tcg_wasm_out_op_i64_shr_u(s);

    tcg_wasm_out_op_local_get(s, TMP64_LOCAL_0_IDX);
    tcg_wasm_out_op_i64_const(s, 0x00ff000000ff0000);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_i64_const(s, 8);
    tcg_wasm_out_op_i64_shr_u(s);

    tcg_wasm_out_op_i64_or(s);

    tcg_wasm_out_op_local_get(s, TMP64_LOCAL_0_IDX);
    tcg_wasm_out_op_i64_const(s, 0x0000ff000000ff00);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_i64_const(s, 8);
    tcg_wasm_out_op_i64_shl(s);

    tcg_wasm_out_op_local_get(s, TMP64_LOCAL_0_IDX);
    tcg_wasm_out_op_i64_const(s, 0x000000ff000000ff);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_i64_const(s, 24);
    tcg_wasm_out_op_i64_shl(s);

    tcg_wasm_out_op_i64_or(s);

    tcg_wasm_out_op_i64_or(s);
    tcg_wasm_out_op_global_set_r(s, dest);
}

static void tcg_wasm_out_bswap32(
    TCGContext *s, TCGReg dest, TCGReg src, int flags)
{
    tcg_wasm_out_op_global_get_r(s, src);
    tcg_wasm_out_op_i32_wrap_i64(s);
    tcg_wasm_out_op_local_set(s, TMP32_LOCAL_0_IDX);

    tcg_wasm_out_op_local_get(s, TMP32_LOCAL_0_IDX);
    tcg_wasm_out_op_i32_const(s, 16);
    tcg_wasm_out_op_i32_rotr(s);
    tcg_wasm_out_op_local_set(s, TMP32_LOCAL_0_IDX);

    tcg_wasm_out_op_local_get(s, TMP32_LOCAL_0_IDX);
    tcg_wasm_out_op_i32_const(s, 0xff00ff00);
    tcg_wasm_out_op_i32_and(s);
    tcg_wasm_out_op_i32_const(s, 8);
    tcg_wasm_out_op_i32_shr_u(s);

    tcg_wasm_out_op_local_get(s, TMP32_LOCAL_0_IDX);
    tcg_wasm_out_op_i32_const(s, 0x00ff00ff);
    tcg_wasm_out_op_i32_and(s);
    tcg_wasm_out_op_i32_const(s, 8);
    tcg_wasm_out_op_i32_shl(s);

    tcg_wasm_out_op_i32_or(s);
    if (flags & TCG_BSWAP_OS) {
        tcg_wasm_out_op_i64_extend_i32_s(s);
    } else {
        tcg_wasm_out_op_i64_extend_i32_u(s);
    }
    tcg_wasm_out_op_global_set_r(s, dest);
}

static void tcg_wasm_out_bswap16(
    TCGContext *s, TCGReg dest, TCGReg src, int flags)
{
    tcg_wasm_out_op_global_get_r(s, src);
    tcg_wasm_out_op_i32_wrap_i64(s);
    tcg_wasm_out_op_local_set(s, TMP32_LOCAL_0_IDX);

    tcg_wasm_out_op_local_get(s, TMP32_LOCAL_0_IDX);
    tcg_wasm_out_op_i32_const(s, 8);
    tcg_wasm_out_op_i32_rotr(s);
    tcg_wasm_out_op_local_set(s, TMP32_LOCAL_0_IDX);

    tcg_wasm_out_op_local_get(s, TMP32_LOCAL_0_IDX);
    tcg_wasm_out_op_i32_const(s, 0x000000ff);
    tcg_wasm_out_op_i32_and(s);

    tcg_wasm_out_op_local_get(s, TMP32_LOCAL_0_IDX);
    tcg_wasm_out_op_i32_const(s, 0xff000000);
    tcg_wasm_out_op_i32_and(s);
    tcg_wasm_out_op_i32_const(s, 16);
    if (flags & TCG_BSWAP_OS) {
        tcg_wasm_out_op_i32_shr_s(s);
    } else {
        tcg_wasm_out_op_i32_shr_u(s);
    }

    tcg_wasm_out_op_i32_or(s);
    tcg_wasm_out_op_i64_extend_i32_u(s);
    tcg_wasm_out_op_global_set_r(s, dest);
}

static void tcg_wasm_out_rotl(
    TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_i32_rotl(s);
        tcg_wasm_out_op_i64_extend_i32_s(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_i64_calc_rotl(s, ret, arg1, arg2);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_rotr(
    TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_i32_rotr(s);
        tcg_wasm_out_op_i64_extend_i32_s(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_i64_calc_rotr(s, ret, arg1, arg2);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_clz(
    TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_i32_eqz(s);
        tcg_wasm_out_op_if_ret_i32(s);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_else(s);
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_i32_clz(s);
        tcg_wasm_out_op_end(s);
        tcg_wasm_out_op_i64_extend_i32_s(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i64_eqz(s);
        tcg_wasm_out_op_if_ret_i64(s);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_else(s);
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i64_clz(s);
        tcg_wasm_out_op_end(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_ctz(
    TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_i32_eqz(s);
        tcg_wasm_out_op_if_ret_i32(s);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_else(s);
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_i32_ctz(s);
        tcg_wasm_out_op_end(s);
        tcg_wasm_out_op_i64_extend_i32_s(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i64_eqz(s);
        tcg_wasm_out_op_if_ret_i64(s);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_else(s);
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i64_ctz(s);
        tcg_wasm_out_op_end(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_norm_ptr(TCGContext *s)
{
#if WASM64_MEMORY64 == 2
    tcg_wasm_out_op_i32_wrap_i64(s);
#endif
}

static uintptr_t tcg_wasm_out_calc_ofs(TCGContext *s, intptr_t offset)
{
#if (TCG_TARGET_REG_BITS == 32) || (WASM64_MEMORY64 == 2)
    if (offset < 0) {
        tcg_wasm_out_op_i32_const(s, offset);
        tcg_wasm_out_op_i32_add(s);
        offset = 0;
    }
#else
    if (offset < 0) {
        tcg_wasm_out_op_i64_const(s, offset);
        tcg_wasm_out_op_i64_add(s);
        offset = 0;
    }
#endif
    return offset;
}

static void tcg_wasm_out_i64_load_type_wrap(TCGContext *s, TCGType type, intptr_t offset, bool wrap)
{
    offset = tcg_wasm_out_calc_ofs(s, offset);
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_i32_load(s, 0, offset);
        if (!wrap) {
            tcg_wasm_out_op_i64_extend_i32_u(s);
        }
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_i64_load(s, 0, offset);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_i64_load_type(TCGContext *s, TCGType type, intptr_t offset)
{
    tcg_wasm_out_i64_load_type_wrap(s, type, offset, false);
}

static void tcg_wasm_out_i64_store_type(TCGContext *s, TCGType type, intptr_t offset)
{
    offset = tcg_wasm_out_calc_ofs(s, offset);
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_i32_store(s, 0, offset);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_i64_store(s, 0, offset);
        break;
    default:
        g_assert_not_reached();
    }
}

static uintptr_t tcg_wasm_out_global_get_r_ptr(TCGContext *s, TCGReg r0, intptr_t offset)
{
    tcg_wasm_out_op_global_get_r_wrap(s, TCG_TYPE_PTR, r0);
    tcg_wasm_out_norm_ptr(s);
    return tcg_wasm_out_calc_ofs(s, offset);
}

static void tcg_wasm_out_ld(
    TCGContext *s, TCGType type, TCGReg val, TCGReg base, intptr_t offset)
{
    offset = tcg_wasm_out_global_get_r_ptr(s, base, offset);
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_i64_load32_u(s, 0, offset);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_i64_load(s, 0, offset);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_wasm_out_op_global_set_r(s, val);
}

static void tcg_wasm_out_ld8s(TCGContext *s, TCGReg val,
                              TCGReg base, intptr_t offset)
{
    offset = tcg_wasm_out_global_get_r_ptr(s, base, offset);
    tcg_wasm_out_op_i64_load8_s(s, 0, offset);
    tcg_wasm_out_op_global_set_r(s, val);
}

static void tcg_wasm_out_ld8u(TCGContext *s, TCGReg val,
                              TCGReg base, intptr_t offset)
{
    offset = tcg_wasm_out_global_get_r_ptr(s, base, offset);
    tcg_wasm_out_op_i64_load8_u(s, 0, offset);
    tcg_wasm_out_op_global_set_r(s, val);
}

static void tcg_wasm_out_ld16s(TCGContext *s, TCGReg val,
                               TCGReg base, intptr_t offset)
{
    offset = tcg_wasm_out_global_get_r_ptr(s, base, offset);
    tcg_wasm_out_op_i64_load16_s(s, 0, offset);
    tcg_wasm_out_op_global_set_r(s, val);
}

static void tcg_wasm_out_ld16u(TCGContext *s, TCGReg val,
                               TCGReg base, intptr_t offset)
{
    offset = tcg_wasm_out_global_get_r_ptr(s, base, offset);
    tcg_wasm_out_op_i64_load16_u(s, 0, offset);
    tcg_wasm_out_op_global_set_r(s, val);
}

static void tcg_wasm_out_ld32s(TCGContext *s, TCGReg val,
                               TCGReg base, intptr_t offset)
{
    offset = tcg_wasm_out_global_get_r_ptr(s, base, offset);
    tcg_wasm_out_op_i64_load32_s(s, 0, offset);
    tcg_wasm_out_op_global_set_r(s, val);
}

static void tcg_wasm_out_ld32u(TCGContext *s, TCGReg val,
                               TCGReg base, intptr_t offset)
{
    offset = tcg_wasm_out_global_get_r_ptr(s, base, offset);
    tcg_wasm_out_op_i64_load32_u(s, 0, offset);
    tcg_wasm_out_op_global_set_r(s, val);
}

static void tcg_wasm_out_st(TCGContext *s, TCGType type, TCGReg val,
                            TCGReg base, intptr_t offset)
{
    offset = tcg_wasm_out_global_get_r_ptr(s, base, offset);
    tcg_wasm_out_op_global_get_r(s, val);
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_i64_store32(s, 0, offset);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_i64_store(s, 0, offset);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_st8(TCGContext *s, TCGReg val,
                             TCGReg base, intptr_t offset)
{
    offset = tcg_wasm_out_global_get_r_ptr(s, base, offset);
    tcg_wasm_out_op_global_get_r(s, val);
    tcg_wasm_out_op_i64_store8(s, 0, offset);
}

static void tcg_wasm_out_st16(TCGContext *s, TCGReg val,
                              TCGReg base, intptr_t offset)
{
    offset = tcg_wasm_out_global_get_r_ptr(s, base, offset);
    tcg_wasm_out_op_global_get_r(s, val);
    tcg_wasm_out_op_i64_store16(s, 0, offset);
}

static void tcg_wasm_out_mov(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_wasm_out_op_global_get_r(s, arg);
    tcg_wasm_out_op_global_set_r(s, ret);
}

static void tcg_wasm_out_movi(TCGContext *s, TCGType type,
                              TCGReg ret, tcg_target_long arg)
{
   switch (type) {
   case TCG_TYPE_I32:
       tcg_wasm_out_op_i64_const(s, (int32_t)arg);
       break;
   case TCG_TYPE_I64:
       tcg_wasm_out_op_i64_const(s, arg);
       break;
   default:
       g_assert_not_reached();
   }
   tcg_wasm_out_op_global_set_r(s, ret);
}

static void tcg_wasm_out_rem_s(
    TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_i64_extend_i32_s(s);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_i64_extend_i32_s(s);
        tcg_wasm_out_op_i64_rem_s(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i64_rem_s(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_rem_u(
    TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i64_const(s, 0xffffffff);
        tcg_wasm_out_op_i64_and(s);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i64_const(s, 0xffffffff);
        tcg_wasm_out_op_i64_and(s);
        tcg_wasm_out_op_i64_rem_u(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i64_rem_u(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_div_s(
    TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_i64_extend_i32_s(s);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i32_wrap_i64(s);
        tcg_wasm_out_op_i64_extend_i32_s(s);
        tcg_wasm_out_op_i64_div_s(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i64_div_s(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_div_u(
    TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1, TCGReg arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_i64_const(s, 0xffffffff);
        tcg_wasm_out_op_i64_and(s);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i64_const(s, 0xffffffff);
        tcg_wasm_out_op_i64_and(s);
        tcg_wasm_out_op_i64_div_u(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_global_get_r(s, arg1);
        tcg_wasm_out_op_global_get_r(s, arg2);
        tcg_wasm_out_op_i64_div_u(s);
        tcg_wasm_out_op_global_set_r(s, ret);
        break;
    default:
        g_assert_not_reached();
    }
}

typedef struct LabelInfo {
    struct LabelInfo *next;
    int label;
    int block;
} LabelInfo;

__thread LabelInfo *label_info;

static void init_label_info(void)
{
    label_info = NULL;
}

static void add_label(int label, int block)
{
    LabelInfo *e = tcg_malloc(sizeof(LabelInfo));
    e->label = label;
    e->block = block;
    e->next = NULL;
    if (label_info == NULL) {
        label_info = e;
        return;
    }
    LabelInfo *last = label_info;
    for (LabelInfo *p = last; p; p = p->next) {
        last = p;
    }
    last->next = e;
}

typedef struct BlockPlaceholder {
    struct BlockPlaceholder *next;
    int label;
    int pos;
} BlockPlaceholder;

__thread BlockPlaceholder *block_placeholder;

__thread int block_idx;

static void init_blocks(void)
{
    block_placeholder = NULL;
    block_idx = 0;
}

static void add_block_placeholder(int label, int pos)
{
    BlockPlaceholder *e = tcg_malloc(sizeof(BlockPlaceholder));
    e->label = label;
    e->pos = pos;
    e->next = NULL;
    if (block_placeholder == NULL) {
        block_placeholder = e;
        return;
    }
    BlockPlaceholder *last = block_placeholder;
    for (BlockPlaceholder *p = last; p; p = p->next) {
        last = p;
    }
    last->next = e;
}

static int get_block_of_label(int label)
{
    for (LabelInfo *p = label_info; p; p = p->next) {
        if (p->label == label) {
            return p->block;
        }
    }
    return -1;
}

static void tcg_wasm_out_new_block(TCGContext *s)
{
    tcg_wasm_out_op_end(s); /* close this block */

    /* next block */
    tcg_wasm_out_op_global_get(s, BLOCK_PTR_IDX);
    tcg_wasm_out_op_i64_const(s, ++block_idx);
    tcg_wasm_out_op_i64_le_u(s);
    tcg_wasm_out_op_if_noret(s);
}

static void tcg_out_label_cb(TCGContext *s, TCGLabel *l)
{
    add_label(l->id, block_idx + 1);
    tcg_wasm_out_new_block(s);
}

static void tcg_wasm_out_op_br_to_label(TCGContext *s, TCGLabel *l, bool br_if)
{
    int toploop_depth = 1;
    if (br_if) {
        tcg_wasm_out_op_if_noret(s);
        toploop_depth++;
    }
    tcg_wasm_out8(s, 0x42); /* i64.const */

    add_block_placeholder(l->id, sub_buf_len());

    tcg_wasm_out8(s, 0x80); /* filled before instantiation */
    tcg_wasm_out8(s, 0x80);
    tcg_wasm_out8(s, 0x80);
    tcg_wasm_out8(s, 0x80);
    tcg_wasm_out8(s, 0x00);
    tcg_wasm_out_op_global_set(s, BLOCK_PTR_IDX);
    if (get_block_of_label(l->id) != -1) {
        /*
         * The label is placed before this br, branch to the top of loop
         */
        tcg_wasm_out_op_br(s, toploop_depth);
    } else {
        /*
         * The label will be generated after this br,
         * branch to the end of the current block
         */
        tcg_wasm_out_op_br(s, toploop_depth - 1);
    }
    if (br_if) {
        tcg_wasm_out_op_end(s);
    }
}

static void tcg_wasm_out_br(TCGContext *s, TCGLabel *l)
{
    tcg_wasm_out_op_br_to_label(s, l, false);
}

static void tcg_wasm_out_brcond(TCGContext *s, TCGType type,
                                TCGReg arg1, TCGReg arg2,
                                TCGCond cond, TCGLabel *l)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_wasm_out_op_cond_i32(s, cond, arg1, arg2);
        break;
    case TCG_TYPE_I64:
        tcg_wasm_out_op_cond_i64(s, cond, arg1, arg2);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_wasm_out_op_br_to_label(s, l, true);
}

#define tcg_wasm_out_ctx_i32_load(s, f)                                 \
    do {                                                                \
        tcg_wasm_out_op_local_get(s, CTX_IDX);                          \
        tcg_wasm_out_norm_ptr(s);                                       \
        tcg_wasm_out_op_i32_load(s, 0, offsetof(struct wasmContext, f)); \
    } while (0)
#define tcg_wasm_out_ctx_i32_store_const(s, f, v)                       \
    do {                                                                \
        tcg_wasm_out_op_local_get(s, CTX_IDX);                          \
        tcg_wasm_out_norm_ptr(s);                                       \
        tcg_wasm_out_op_i32_const(s, v);                                \
        tcg_wasm_out_op_i32_store(s, 0, offsetof(struct wasmContext, f)); \
    } while (0)
#define tcg_wasm_out_ctx_ptr_load(s, f)                                 \
    do {                                                                \
        tcg_wasm_out_op_local_get(s, CTX_IDX);                          \
        tcg_wasm_out_norm_ptr(s);                                       \
        tcg_wasm_out_i64_load_type(s, TCG_TYPE_PTR, offsetof(struct wasmContext, f)); \
    } while (0)
#define tcg_wasm_out_ctx_ptr_load_wrap(s, f)                                 \
    do {                                                                \
        tcg_wasm_out_op_local_get(s, CTX_IDX);                          \
        tcg_wasm_out_norm_ptr(s);                                       \
        tcg_wasm_out_i64_load_type_wrap(s, TCG_TYPE_PTR, offsetof(struct wasmContext, f), true); \
    } while (0)
#define tcg_wasm_out_ctx_ptr_store_const(s, f, v)                       \
    do {                                                                \
        tcg_wasm_out_op_local_get(s, CTX_IDX);                          \
        tcg_wasm_out_norm_ptr(s);                                       \
        tcg_wasm_out_op_i64_const(s, v);                                \
        tcg_wasm_out_i64_store_type(s, TCG_TYPE_PTR, offsetof(struct wasmContext, f)); \
    } while (0)
#define tcg_wasm_out_ctx_ptr_store_local64_ptr(s, f, var)               \
    do {                                                                \
        tcg_wasm_out_op_local_get(s, CTX_IDX);                          \
        tcg_wasm_out_norm_ptr(s);                                       \
        tcg_wasm_out_op_local_get(s, var);                              \
        tcg_wasm_out_i64_store_type(s, TCG_TYPE_PTR, offsetof(struct wasmContext, f)); \
    } while (0)
#define tcg_wasm_out_ctx_ptr_store_r(s, f, r)                           \
    do {                                                                \
        tcg_wasm_out_op_local_get(s, CTX_IDX);                          \
        tcg_wasm_out_norm_ptr(s);                                       \
        tcg_wasm_out_op_global_get_r(s, r);                             \
        tcg_wasm_out_i64_store_type(s, TCG_TYPE_PTR, offsetof(struct wasmContext, f)); \
    } while (0)

#if TCG_TARGET_REG_BITS == 32
#define tcg_wasm_out_op_ptr_const(s, v) tcg_wasm_out_op_i32_const(s, v)
#else
#define tcg_wasm_out_op_ptr_const(s, v) tcg_wasm_out_op_i64_const(s, v)
#endif

static void tcg_wasm_out_exit_tb(TCGContext *s, uintptr_t arg)
{
    tcg_wasm_out_ctx_ptr_store_const(s, tb_ptr, 0);
    tcg_wasm_out_op_ptr_const(s, arg);
    tcg_wasm_out_op_return(s);
}

static void tcg_wasm_out_goto_ptr(TCGContext *s, TCGReg arg)
{
    tcg_wasm_out_op_global_get_r(s, arg);
    tcg_wasm_out_ctx_ptr_load(s, tb_ptr);
    tcg_wasm_out_op_i64_eq(s);
    tcg_wasm_out_op_if_noret(s);
    tcg_wasm_out_op_i64_const(s, 0);
    tcg_wasm_out_op_global_set(s, BLOCK_PTR_IDX);
    tcg_wasm_out_op_br(s, 2); /* br to the top of loop */
    tcg_wasm_out_op_end(s);

    tcg_wasm_out_ctx_ptr_store_r(s, tb_ptr, arg);
    tcg_wasm_out_op_ptr_const(s, 0);
    tcg_wasm_out_op_return(s);
}

static void tcg_wasm_out_goto_tb(
    TCGContext *s, int which, uintptr_t cur_reset_ptr)
{
    tcg_wasm_out_op_ptr_const(s, get_jmp_target_addr(s, which));
    tcg_wasm_out_norm_ptr(s);
    tcg_wasm_out_i64_load_type(s, TCG_TYPE_PTR, 0);
    tcg_wasm_out_op_local_set(s, TMP64_LOCAL_0_IDX);

    tcg_wasm_out_op_local_get(s, TMP64_LOCAL_0_IDX);
    tcg_wasm_out_op_i64_const(s, cur_reset_ptr);

    tcg_wasm_out_op_i64_ne(s);
    tcg_wasm_out_op_if_noret(s);

    tcg_wasm_out_op_local_get(s, TMP64_LOCAL_0_IDX);
    tcg_wasm_out_ctx_ptr_load(s, tb_ptr);
    tcg_wasm_out_op_i64_eq(s);

    tcg_wasm_out_op_if_noret(s);
    tcg_wasm_out_op_i64_const(s, 0);
    tcg_wasm_out_op_global_set(s, BLOCK_PTR_IDX);
    tcg_wasm_out_op_br(s, 3); /* br to the top of loop */
    tcg_wasm_out_op_end(s);

    tcg_wasm_out_ctx_ptr_store_local64_ptr(s, tb_ptr, TMP64_LOCAL_0_IDX);
    tcg_wasm_out_op_ptr_const(s, 0);
    tcg_wasm_out_op_return(s);
    tcg_wasm_out_op_end(s);
}

static void push_arg_i64(TCGContext *s, int *stack_offset)
{
    tcg_wasm_out_global_get_r_ptr(s, TCG_REG_CALL_STACK, 0);
    tcg_wasm_out_op_i64_load(s, 0, *stack_offset);
    *stack_offset = *stack_offset + 8;
}

static void gen_call(TCGContext *s, const TCGHelperInfo *info, intptr_t func_idx)
{
    unsigned typemask = info->typemask;
    int rettype = typemask & 7;
    int stack_offset = 0;

    if (rettype ==  dh_typecode_i128) {
        /* receive 128bit return value via the buffer */
        tcg_wasm_out_ctx_ptr_load_wrap(s, buf128);
    }

    for (typemask >>= 3; typemask; typemask >>= 3) {
        int typecode = typemask & 7;
        if (typecode == dh_typecode_void) {
            continue;
        }
        switch (typecode) {
        case dh_typecode_i32:
        case dh_typecode_s32:
            push_arg_i64(s, &stack_offset);
            tcg_wasm_out_op_i32_wrap_i64(s);
            break;
        case dh_typecode_i64:
        case dh_typecode_s64:
            push_arg_i64(s, &stack_offset);
            break;
        case dh_typecode_i128:
            tcg_wasm_out_op_global_get_r(s, TCG_REG_CALL_STACK);
            tcg_wasm_out_op_i64_const(s, stack_offset);
            tcg_wasm_out_op_i64_add(s);
            stack_offset += 16;
            break;
        case dh_typecode_ptr:
            push_arg_i64(s, &stack_offset);
#if TCG_TARGET_REG_BITS == 32
            tcg_wasm_out_op_i32_wrap_i64(s);
#endif
            break;
        default:
            g_assert_not_reached();
        }
    }

    tcg_wasm_out_op_call(s, func_idx);

    if (rettype != dh_typecode_void) {
        switch (rettype) {
        case dh_typecode_i32:
        case dh_typecode_s32:
            tcg_wasm_out_op_i64_extend_i32_s(s);
            tcg_wasm_out_op_global_set_r(s, TCG_REG_R0);
            break;
        case dh_typecode_i64:
        case dh_typecode_s64:
            tcg_wasm_out_op_global_set_r(s, TCG_REG_R0);
            break;
        case dh_typecode_i128:
            tcg_wasm_out_ctx_ptr_load_wrap(s, buf128);
            tcg_wasm_out_norm_ptr(s);
            tcg_wasm_out_i64_load_type(s, TCG_TYPE_I64, 0);
            tcg_wasm_out_op_global_set_r(s, TCG_REG_R0);
            tcg_wasm_out_ctx_ptr_load_wrap(s, buf128);
            tcg_wasm_out_norm_ptr(s);
            tcg_wasm_out_i64_load_type(s, TCG_TYPE_I64, 8);
            tcg_wasm_out_op_global_set_r(s, TCG_REG_R1);
            break;
        case dh_typecode_ptr:
#if TCG_TARGET_REG_BITS == 32
            tcg_wasm_out_op_i64_extend_i32_s(s);
#endif
            tcg_wasm_out_op_global_set_r(s, TCG_REG_R0);
            break;
        default:
            g_assert_not_reached();
        }
    }

    return;
}

__thread LinkedBuf *types_buf_root;
__thread LinkedBuf *types_buf_cur;

static void init_types_buf(void)
{
    types_buf_root = new_linked_buf();
    types_buf_cur = types_buf_root;
}

static inline void types_buf_out8(uint8_t v)
{
    types_buf_cur = linked_buf_out8(types_buf_cur, v);
}

static inline int types_buf_len(void)
{
    return linked_buf_len(types_buf_root);
}

static void types_out_leb128_uint32(uint32_t v)
{
    uint8_t b;
    do {
        b = v & 0x7f;
        v >>= 7;
        if (v != 0) {
            b |= 0x80;
        }
        types_buf_out8(b);
    } while (v != 0);
}

static void gen_func_type_call(TCGContext *s, const TCGHelperInfo *info)
{
    unsigned typemask = info->typemask;
    int rettype = typemask & 7;
    int vec_size = 0;

    if (rettype == dh_typecode_i128) {
        vec_size++;
    }
    for (int m = typemask >> 3; m; m >>= 3) {
        int typecode = m & 7;
        if (typecode != dh_typecode_void) {
            vec_size++;
        }
    }

    types_buf_out8(0x60);
    types_out_leb128_uint32(vec_size);

    if (rettype == dh_typecode_i128) {
        types_buf_out8(PTR_TYPE);
    }

    for (int m = typemask >> 3; m; m >>= 3) {
        int typecode = m & 7;
        if (typecode == dh_typecode_void) {
            continue;
        }
        switch (typecode) {
        case dh_typecode_i32:
        case dh_typecode_s32:
            types_buf_out8(0x7f);
            break;
        case dh_typecode_i64:
        case dh_typecode_s64:
            types_buf_out8(0x7e);
            break;
        case dh_typecode_i128:
            types_buf_out8(0x7e);
            break;
        case dh_typecode_ptr:
            types_buf_out8(PTR_TYPE);
            break;
        default:
            g_assert_not_reached();
        }
    }

    if ((rettype == dh_typecode_void) || (rettype == dh_typecode_i128)) {
        types_buf_out8(0x0);
    } else {
        types_buf_out8(0x1);
        switch (rettype) {
        case dh_typecode_i32:
        case dh_typecode_s32:
            types_buf_out8(0x7f);
            break;
        case dh_typecode_i64:
        case dh_typecode_s64:
            types_buf_out8(0x7e);
            break;
        case dh_typecode_ptr:
            types_buf_out8(PTR_TYPE);
            break;
        default:
            g_assert_not_reached();
        }
    }
    return;
}

typedef struct HelperInfo {
    struct HelperInfo *next;
    intptr_t idx_on_qemu;
} HelperInfo;

__thread HelperInfo *helpers;

static void init_helpers(void)
{
    helpers = NULL;
}

static int register_helper(TCGContext *s, intptr_t helper_idx_on_qemu)
{
    int idx = HELPER_IDX_START;

    tcg_debug_assert(helper_idx_on_qemu >= 0);

    HelperInfo *e = tcg_malloc(sizeof(HelperInfo));
    e->idx_on_qemu = helper_idx_on_qemu;
    e->next = NULL;
    if (helpers == NULL) {
        helpers = e;
        return idx;
    }
    HelperInfo *last = helpers;
    for (HelperInfo *p = last; p; p = p->next) {
        last = p;
        idx++;
    }
    last->next = e;
    return idx;
}

static int helpers_len(void)
{
    int n = 0;
    for (HelperInfo *p = helpers; p; p = p->next) {
        n++;
    }
    return n;
}

static inline int helpers_copy(intptr_t *dst)
{
    void *start = dst;
    for (HelperInfo *p = helpers; p; p = p->next) {
        *dst++ = p->idx_on_qemu;
    }
    return (int)dst - (int)start;
}


static intptr_t get_helper_idx(TCGContext *s, intptr_t helper_idx_on_qemu)
{
    intptr_t idx = HELPER_IDX_START;

    for (HelperInfo *p = helpers; p; p = p->next) {
        if (p->idx_on_qemu == helper_idx_on_qemu) {
            return idx;
        }
        idx++;
    }
    return -1;
}

static void tcg_wasm_out_handle_unwinding(TCGContext *s)
{
    tcg_wasm_out_op_call(s, CHECK_UNWINDING_IDX);
    tcg_wasm_out_op_i32_eqz(s);
    tcg_wasm_out_op_if_noret(s);
    tcg_wasm_out_op_ptr_const(s, 0);
    /* returns if unwinding */
    tcg_wasm_out_op_return(s);
    tcg_wasm_out_op_end(s);
}

static void tcg_wasm_out_call(TCGContext *s, intptr_t func,
                              const TCGHelperInfo *info)
{
    intptr_t func_idx = get_helper_idx(s, func);
    if (func_idx < 0) {
        func_idx = register_helper(s, func);
        gen_func_type_call(s, info);
    }

    tcg_wasm_out_ctx_ptr_load_wrap(s, tci_tb_ptr);
    tcg_wasm_out_norm_ptr(s);
    tcg_wasm_out_op_i64_const(s, (uint64_t)s->code_ptr);
    tcg_wasm_out_i64_store_type(s, TCG_TYPE_PTR, 0);

    /*
     * update the block index so that the possible rewinding will
     * skip this block
     */
    tcg_wasm_out_op_i64_const(s, block_idx + 1);
    tcg_wasm_out_op_global_set(s, BLOCK_PTR_IDX);
    tcg_wasm_out_new_block(s);

    gen_call(s, info, func_idx);
    tcg_wasm_out_handle_unwinding(s);
}

static void gen_func_type_qemu_ld(TCGContext *s, uint32_t oi)
{
    types_buf_out8(0x60);
    types_buf_out8(0x4);
    types_buf_out8(PTR_TYPE);
    types_buf_out8(0x7e);
    types_buf_out8(0x7f);
    types_buf_out8(PTR_TYPE);
    types_buf_out8(0x1);
#if TCG_TARGET_REG_BITS == 32
    MemOp mop = get_memop(oi);
    switch (mop & MO_SSIZE) {
    case MO_UQ:
        types_buf_out8(0x7e);
        break;
    default:
        types_buf_out8(0x7f);
        break;
    }
#else
    types_buf_out8(0x7e);
#endif
}

static void gen_func_type_qemu_st(TCGContext *s, uint32_t oi)
{
    MemOp mop = get_memop(oi);

    types_buf_out8(0x60);
    types_buf_out8(0x5);
    types_buf_out8(PTR_TYPE);
    types_buf_out8(0x7e);
    switch (mop & MO_SSIZE) {
    case MO_UQ:
        types_buf_out8(0x7e);
        break;
    default:
        types_buf_out8(0x7f);
        break;
    }
    types_buf_out8(0x7f);
    types_buf_out8(PTR_TYPE);
    types_buf_out8(0x0);
}

static void *qemu_ld_helper_ptr(uint32_t oi)
{
    MemOp mop = get_memop(oi);
    switch (mop & MO_SSIZE) {
    case MO_UB:
        return helper_ldub_mmu;
    case MO_SB:
        return helper_ldsb_mmu;
    case MO_UW:
        return helper_lduw_mmu;
    case MO_SW:
        return helper_ldsw_mmu;
    case MO_UL:
        return helper_ldul_mmu;
    case MO_SL:
        return helper_ldsl_mmu;
    case MO_UQ:
        return helper_ldq_mmu;
    default:
        g_assert_not_reached();
    }
}

#define MIN_TLB_MASK_TABLE_OFS INT_MIN

static uint8_t prepare_host_addr_wasm(TCGContext *s, uint8_t *hit_var,
                                      TCGReg addr_reg, MemOpIdx oi,
                                      bool is_ld)
{
    MemOp opc = get_memop(oi);
    TCGAtomAlign aa;
    unsigned a_mask;
    unsigned s_bits = opc & MO_SIZE;
    unsigned s_mask = (1u << s_bits) - 1;
    int mem_index = get_mmuidx(oi);
    int fast_ofs = tlb_mask_table_ofs(s, mem_index);
    int mask_ofs = fast_ofs + offsetof(CPUTLBDescFast, mask);
    int table_ofs = fast_ofs + offsetof(CPUTLBDescFast, table);
    int add_off = offsetof(CPUTLBEntry, addend);
    tcg_target_long compare_mask;

#if TCG_TARGET_REG_BITS == 32
    uint8_t tmp1 = TMP32_LOCAL_1_IDX;
    uint8_t tmp2 = TMP32_LOCAL_2_IDX;
#else
    uint8_t tmp1 = TMP64_LOCAL_0_IDX;
    uint8_t tmp2 = TMP64_LOCAL_1_IDX;
#endif

    if (!tcg_use_softmmu) {
        g_assert_not_reached();
    }

    *hit_var = TMP32_LOCAL_0_IDX;
    tcg_wasm_out_op_i32_const(s, 0);
    tcg_wasm_out_op_local_set(s, *hit_var);

    aa = atom_and_align_for_opc(s, opc, MO_ATOM_IFALIGN, false);
    a_mask = (1u << aa.align) - 1;

    /* Get the CPUTLBEntry offset */
    tcg_wasm_out_op_global_get_r(s, addr_reg);
    tcg_wasm_out_op_i64_const(s, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
    tcg_wasm_out_op_i64_shr_u(s);

    tcg_wasm_out_op_global_get_r_wrap(s, TCG_TYPE_PTR, TCG_AREG0);
    tcg_wasm_out_norm_ptr(s);
    tcg_wasm_out_i64_load_type(s, TCG_TYPE_PTR, mask_ofs);
    tcg_wasm_out_op_i64_and(s);

    /* Get the pointer to the target CPUTLBEntry */
    tcg_wasm_out_op_global_get_r_wrap(s, TCG_TYPE_PTR, TCG_AREG0);
    tcg_wasm_out_norm_ptr(s);
    tcg_wasm_out_i64_load_type(s, TCG_TYPE_PTR, table_ofs);
    tcg_wasm_out_op_i64_add(s);
#if TCG_TARGET_REG_BITS == 32
    tcg_wasm_out_op_i32_wrap_i64(s);
#endif
    tcg_wasm_out_op_local_tee(s, tmp1);
    tcg_wasm_out_norm_ptr(s);

    /* Load the tlb copmarator */
    tcg_wasm_out_i64_load_type(s, TCG_TYPE_PTR,
                               is_ld ? offsetof(CPUTLBEntry, addr_read)
                               : offsetof(CPUTLBEntry, addr_write));

    /*
     * For aligned accesses, we check the first byte and include the
     * alignment bits within the address.  For unaligned access, we
     * check that we don't cross pages using the address of the last
     * byte of the access.
     */
    tcg_wasm_out_op_global_get_r(s, addr_reg);
    if (a_mask < s_mask) {
        tcg_wasm_out_op_i64_const(s, s_mask - a_mask);
        tcg_wasm_out_op_i64_add(s);
    }
    compare_mask = (uint64_t)TARGET_PAGE_MASK | a_mask;
    tcg_wasm_out_op_i64_const(s, compare_mask);
    tcg_wasm_out_op_i64_and(s);

    /* Compare masked address with the TLB entry. */
    tcg_wasm_out_op_i64_eq(s);

    tcg_wasm_out_op_if_noret(s);

    /* TLB Hit - translate address using addend.  */
    tcg_wasm_out_op_local_get(s, tmp1);
    tcg_wasm_out_norm_ptr(s);
    tcg_wasm_out_i64_load_type(s, TCG_TYPE_PTR, add_off);
    tcg_wasm_out_op_global_get_r(s, addr_reg);
    tcg_wasm_out_op_i64_add(s);
#if TCG_TARGET_REG_BITS == 32
    tcg_wasm_out_op_i32_wrap_i64(s);
#endif
    tcg_wasm_out_op_local_set(s, tmp2);
    tcg_wasm_out_op_i32_const(s, 1);
    tcg_wasm_out_op_local_set(s, *hit_var);

    tcg_wasm_out_op_end(s);

    return tmp2;
}

static void tcg_wasm_out_qemu_ld_direct(
    TCGContext *s, TCGReg r, uint8_t base, MemOp opc)
{
    switch (opc & (MO_SSIZE)) {
    case MO_UB:
        tcg_wasm_out_op_local_get(s, base);
        tcg_wasm_out_norm_ptr(s);
        tcg_wasm_out_op_i64_load8_u(s, 0, 0);
        tcg_wasm_out_op_global_set_r(s, r);
        break;
    case MO_SB:
        tcg_wasm_out_op_local_get(s, base);
        tcg_wasm_out_norm_ptr(s);
        tcg_wasm_out_op_i64_load8_s(s, 0, 0);
        tcg_wasm_out_op_global_set_r(s, r);
        break;
    case MO_UW:
        tcg_wasm_out_op_local_get(s, base);
        tcg_wasm_out_norm_ptr(s);
        tcg_wasm_out_op_i64_load16_u(s, 0, 0);
        tcg_wasm_out_op_global_set_r(s, r);
        break;
    case MO_SW:
        tcg_wasm_out_op_local_get(s, base);
        tcg_wasm_out_norm_ptr(s);
        tcg_wasm_out_op_i64_load16_s(s, 0, 0);
        tcg_wasm_out_op_global_set_r(s, r);
        break;
    case MO_UL:
        tcg_wasm_out_op_local_get(s, base);
        tcg_wasm_out_norm_ptr(s);
        tcg_wasm_out_op_i64_load32_u(s, 0, 0);
        tcg_wasm_out_op_global_set_r(s, r);
        break;
    case MO_SL:
        tcg_wasm_out_op_local_get(s, base);
        tcg_wasm_out_norm_ptr(s);
        tcg_wasm_out_op_i64_load32_s(s, 0, 0);
        tcg_wasm_out_op_global_set_r(s, r);
        break;
    case MO_UQ:
        tcg_wasm_out_op_local_get(s, base);
        tcg_wasm_out_norm_ptr(s);
        tcg_wasm_out_op_i64_load(s, 0, 0);
        tcg_wasm_out_op_global_set_r(s, r);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_qemu_ld(TCGContext *s, TCGReg data_reg,
                                 TCGReg addr_reg, MemOpIdx oi, TCGReg *data_reg_hi)
{
    intptr_t helper_idx;
    intptr_t func_idx;
    bool addr64 = s->addr_type == TCG_TYPE_I64;
    MemOp mop = get_memop(oi);
    uint8_t base_var, hit_var;

    helper_idx = (intptr_t)qemu_ld_helper_ptr(oi);
    func_idx = get_helper_idx(s, helper_idx);
    if (func_idx < 0) {
        func_idx = register_helper(s, helper_idx);
        gen_func_type_qemu_ld(s, oi);
    }

    if (!addr64) {
        tcg_wasm_out_ext32u(s, TCG_REG_TMP, addr_reg);
        addr_reg = TCG_REG_TMP;
    }

    base_var = prepare_host_addr_wasm(s, &hit_var, addr_reg, oi, true);
    tcg_wasm_out_op_local_get(s, hit_var);
    tcg_wasm_out_op_i32_const(s, 1);
    tcg_wasm_out_op_i32_eq(s);
    tcg_wasm_out_op_if_noret(s);
    tcg_wasm_out_qemu_ld_direct(s, data_reg, base_var, mop); /* fast path */
    tcg_wasm_out_op_end(s);

    /*
     * update the block index so that the possible rewinding will
     * skip this block
     */
    tcg_wasm_out_op_i64_const(s, block_idx + 1);
    tcg_wasm_out_op_global_set(s, BLOCK_PTR_IDX);
    tcg_wasm_out_new_block(s);

    tcg_wasm_out_op_local_get(s, hit_var);
    tcg_wasm_out_op_i32_eqz(s);
    tcg_wasm_out_op_if_noret(s);

    /* call helper */
    tcg_wasm_out_op_global_get_r_wrap(s, TCG_TYPE_PTR, TCG_AREG0);
    tcg_wasm_out_op_global_get_r(s, addr_reg);
    tcg_wasm_out_op_i32_const(s, oi);
    tcg_wasm_out_op_ptr_const(s, (intptr_t)s->code_ptr);

    tcg_wasm_out_op_call(s, func_idx);
#if TCG_TARGET_REG_BITS == 32
    switch (mop & MO_SSIZE) {
    case MO_UQ:
        tcg_wasm_out_op_global_set_r(s, data_reg);
        if (data_reg_hi != NULL) {
            tcg_wasm_out_op_global_get_r(s, data_reg);
            tcg_wasm_out_op_i64_const(s, 32);
            tcg_wasm_out_op_i64_shr_u(s);
            tcg_wasm_out_op_global_set_r(s, *data_reg_hi);
        }
        tcg_wasm_out_op_global_get_r(s, data_reg);
        tcg_wasm_out_op_i64_const(s, 0xffffffff);
        tcg_wasm_out_op_i64_and(s);
        tcg_wasm_out_op_global_set_r(s, data_reg);
        break;
    default:
        tcg_wasm_out_op_i64_extend_i32_u(s);
        tcg_wasm_out_op_global_set_r(s, data_reg);
        if (data_reg_hi != NULL) {
            tcg_wasm_out_op_i64_const(s, 0);
            tcg_wasm_out_op_global_set_r(s, *data_reg_hi);
        }
        break;
    }
#else
    tcg_wasm_out_op_global_set_r(s, data_reg);
#endif
    tcg_wasm_out_handle_unwinding(s);

    tcg_wasm_out_op_end(s);
}

static void *qemu_st_helper_ptr(uint32_t oi)
{
    MemOp mop = get_memop(oi);
    switch (mop & MO_SIZE) {
    case MO_8:
        return helper_stb_mmu;
    case MO_16:
        return helper_stw_mmu;
    case MO_32:
        return helper_stl_mmu;
    case MO_64:
        return helper_stq_mmu;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_qemu_st_direct(
    TCGContext *s, TCGReg lo, uint8_t base, MemOp opc)
{
    switch (opc & (MO_SSIZE)) {
    case MO_8:
        tcg_wasm_out_op_local_get(s, base);
        tcg_wasm_out_norm_ptr(s);
        tcg_wasm_out_op_global_get_r(s, lo);
        tcg_wasm_out_op_i64_store8(s, 0, 0);
        break;
    case MO_16:
        tcg_wasm_out_op_local_get(s, base);
        tcg_wasm_out_norm_ptr(s);
        tcg_wasm_out_op_global_get_r(s, lo);
        tcg_wasm_out_op_i64_store16(s, 0, 0);
        break;
    case MO_32:
        tcg_wasm_out_op_local_get(s, base);
        tcg_wasm_out_norm_ptr(s);
        tcg_wasm_out_op_global_get_r(s, lo);
        tcg_wasm_out_op_i64_store32(s, 0, 0);
        break;
    case MO_64:
        tcg_wasm_out_op_local_get(s, base);
        tcg_wasm_out_norm_ptr(s);
        tcg_wasm_out_op_global_get_r(s, lo);
        tcg_wasm_out_op_i64_store(s, 0, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_wasm_out_qemu_st(TCGContext *s, TCGReg data_reg,
                                 TCGReg addr_reg, MemOpIdx oi, TCGReg *data_reg_hi)
{
    intptr_t helper_idx;
    intptr_t func_idx;
    bool addr64 = s->addr_type == TCG_TYPE_I64;
    MemOp mop = get_memop(oi);
    uint8_t base_var, hit_var;

    helper_idx = (intptr_t)qemu_st_helper_ptr(oi);
    func_idx = get_helper_idx(s, helper_idx);
    if (func_idx < 0) {
        func_idx = register_helper(s, helper_idx);
        gen_func_type_qemu_st(s, oi);
    }

    if (!addr64) {
        tcg_wasm_out_ext32u(s, TCG_REG_TMP, addr_reg);
        addr_reg = TCG_REG_TMP;
    }

    base_var = prepare_host_addr_wasm(s, &hit_var, addr_reg, oi, false);
    tcg_wasm_out_op_local_get(s, hit_var);
    tcg_wasm_out_op_i32_const(s, 1);
    tcg_wasm_out_op_i32_eq(s);
    tcg_wasm_out_op_if_noret(s);
    tcg_wasm_out_qemu_st_direct(s, data_reg, base_var, mop); /* fast path */
    tcg_wasm_out_op_end(s);

    /*
     * update the block index so that the possible rewinding will
     * skip this block
     */
    tcg_wasm_out_op_i64_const(s, block_idx + 1);
    tcg_wasm_out_op_global_set(s, BLOCK_PTR_IDX);
    tcg_wasm_out_new_block(s);

    tcg_wasm_out_op_local_get(s, hit_var);
    tcg_wasm_out_op_i32_eqz(s);
    tcg_wasm_out_op_if_noret(s);

    /* call helper */
    tcg_wasm_out_op_global_get_r_wrap(s, TCG_TYPE_PTR, TCG_AREG0);
    tcg_wasm_out_op_global_get_r(s, addr_reg);
    switch (mop & MO_SSIZE) {
    case MO_UQ:
        tcg_wasm_out_op_global_get_r(s, data_reg);
#if TCG_TARGET_REG_BITS == 32
        tcg_wasm_out_op_i64_const(s, 0xffffffff);
        tcg_wasm_out_op_i64_and(s);
        if (data_reg_hi != NULL) {
            tcg_wasm_out_op_global_get_r(s, *data_reg_hi);
            tcg_wasm_out_op_i64_const(s, 32);
            tcg_wasm_out_op_i64_shl(s);
            tcg_wasm_out_op_i64_or(s);
        }
#endif
        break;
    default:
        tcg_wasm_out_op_global_get_r(s, data_reg);
        tcg_wasm_out_op_i32_wrap_i64(s);
        break;
    }
    tcg_wasm_out_op_i32_const(s, oi);
    tcg_wasm_out_op_ptr_const(s, (intptr_t)s->code_ptr);

    tcg_wasm_out_op_call(s, func_idx);
    tcg_wasm_out_handle_unwinding(s);

    tcg_wasm_out_op_end(s);
}

static bool patch_reloc(tcg_insn_unit *code_ptr_i, int type,
                        intptr_t value, intptr_t addend)
{
    tcg_insn_unit_tci *code_ptr = (tcg_insn_unit_tci *)code_ptr_i;
    intptr_t diff = value - (intptr_t)(code_ptr + 1);

    tcg_debug_assert(addend == 0);
    tcg_debug_assert(type == 20);

    if (diff == sextract32(diff, 0, type)) {
        tcg_patch32((tcg_insn_unit *)code_ptr,
                    deposit32(*code_ptr, 32 - type, type, diff));
        return true;
    }
    return false;
}

static void stack_bounds_check(TCGReg base, intptr_t offset)
{
    if (base == TCG_REG_CALL_STACK) {
        tcg_debug_assert(offset >= 0);
        tcg_debug_assert(offset < (TCG_STATIC_CALL_ARGS_SIZE +
                                   TCG_STATIC_FRAME_SIZE));
    }
}

static void tcg_out_op_l(TCGContext *s, TCGOpcode op, TCGLabel *l0)
{
    tcg_insn_unit_tci insn = 0;

    tcg_out_reloc(s, s->code_ptr, 20, l0, 0);
    insn = deposit32(insn, 0, 8, op);
    tcg_out32(s, insn);
}

static void tcg_out_op_p(TCGContext *s, TCGOpcode op, void *p0)
{
    tcg_insn_unit_tci insn = 0;
    intptr_t diff;

    /* Special case for exit_tb: map null -> 0. */
    if (p0 == NULL) {
        diff = 0;
    } else {
        diff = p0 - (void *)(s->code_ptr + 4);
        tcg_debug_assert(diff != 0);
        if (diff != sextract32(diff, 0, 20)) {
            tcg_raise_tb_overflow(s);
        }
    }
    insn = deposit32(insn, 0, 8, op);
    insn = deposit32(insn, 12, 20, diff);
    tcg_out32(s, insn);
}

static void tcg_out_op_r(TCGContext *s, TCGOpcode op, TCGReg r0)
{
    tcg_insn_unit_tci insn = 0;

    insn = deposit32(insn, 0, 8, op);
    insn = deposit32(insn, 8, 4, r0);
    tcg_out32(s, insn);
}

static void tcg_out_op_v(TCGContext *s, TCGOpcode op)
{
    tcg_out32(s, (uint8_t)op);
}

static void tcg_out_op_ri(TCGContext *s, TCGOpcode op, TCGReg r0, int32_t i1)
{
    tcg_insn_unit_tci insn = 0;

    tcg_debug_assert(i1 == sextract32(i1, 0, 20));
    insn = deposit32(insn, 0, 8, op);
    insn = deposit32(insn, 8, 4, r0);
    insn = deposit32(insn, 12, 20, i1);
    tcg_out32(s, insn);
}

static void tcg_out_op_rl(TCGContext *s, TCGOpcode op, TCGReg r0, TCGLabel *l1)
{
    tcg_insn_unit_tci insn = 0;

    tcg_out_reloc(s, s->code_ptr, 20, l1, 0);
    insn = deposit32(insn, 0, 8, op);
    insn = deposit32(insn, 8, 4, r0);
    tcg_out32(s, insn);
}

static void tcg_out_op_rr(TCGContext *s, TCGOpcode op, TCGReg r0, TCGReg r1)
{
    tcg_insn_unit_tci insn = 0;

    insn = deposit32(insn, 0, 8, op);
    insn = deposit32(insn, 8, 4, r0);
    insn = deposit32(insn, 12, 4, r1);
    tcg_out32(s, insn);
}

static void tcg_out_op_rrm(TCGContext *s, TCGOpcode op,
                           TCGReg r0, TCGReg r1, TCGArg m2)
{
    tcg_insn_unit_tci insn = 0;

    tcg_debug_assert(m2 == extract32(m2, 0, 16));
    insn = deposit32(insn, 0, 8, op);
    insn = deposit32(insn, 8, 4, r0);
    insn = deposit32(insn, 12, 4, r1);
    insn = deposit32(insn, 16, 16, m2);
    tcg_out32(s, insn);
}

static void tcg_out_op_rrr(TCGContext *s, TCGOpcode op,
                           TCGReg r0, TCGReg r1, TCGReg r2)
{
    tcg_insn_unit_tci insn = 0;

    insn = deposit32(insn, 0, 8, op);
    insn = deposit32(insn, 8, 4, r0);
    insn = deposit32(insn, 12, 4, r1);
    insn = deposit32(insn, 16, 4, r2);
    tcg_out32(s, insn);
}

static void tcg_out_op_rrs(TCGContext *s, TCGOpcode op,
                           TCGReg r0, TCGReg r1, intptr_t i2)
{
    tcg_insn_unit_tci insn = 0;

    tcg_debug_assert(i2 == sextract32(i2, 0, 16));
    insn = deposit32(insn, 0, 8, op);
    insn = deposit32(insn, 8, 4, r0);
    insn = deposit32(insn, 12, 4, r1);
    insn = deposit32(insn, 16, 16, i2);
    tcg_out32(s, insn);
}

static void tcg_out_op_rrbb(TCGContext *s, TCGOpcode op, TCGReg r0,
                            TCGReg r1, uint8_t b2, uint8_t b3)
{
    tcg_insn_unit_tci insn = 0;

    tcg_debug_assert(b2 == extract32(b2, 0, 6));
    tcg_debug_assert(b3 == extract32(b3, 0, 6));
    insn = deposit32(insn, 0, 8, op);
    insn = deposit32(insn, 8, 4, r0);
    insn = deposit32(insn, 12, 4, r1);
    insn = deposit32(insn, 16, 6, b2);
    insn = deposit32(insn, 22, 6, b3);
    tcg_out32(s, insn);
}

static void tcg_out_op_rrrc(TCGContext *s, TCGOpcode op,
                            TCGReg r0, TCGReg r1, TCGReg r2, TCGCond c3)
{
    tcg_insn_unit_tci insn = 0;

    insn = deposit32(insn, 0, 8, op);
    insn = deposit32(insn, 8, 4, r0);
    insn = deposit32(insn, 12, 4, r1);
    insn = deposit32(insn, 16, 4, r2);
    insn = deposit32(insn, 20, 4, c3);
    tcg_out32(s, insn);
}

static void tcg_out_op_rrrbb(TCGContext *s, TCGOpcode op, TCGReg r0,
                             TCGReg r1, TCGReg r2, uint8_t b3, uint8_t b4)
{
    tcg_insn_unit_tci insn = 0;

    tcg_debug_assert(b3 == extract32(b3, 0, 6));
    tcg_debug_assert(b4 == extract32(b4, 0, 6));
    insn = deposit32(insn, 0, 8, op);
    insn = deposit32(insn, 8, 4, r0);
    insn = deposit32(insn, 12, 4, r1);
    insn = deposit32(insn, 16, 4, r2);
    insn = deposit32(insn, 20, 6, b3);
    insn = deposit32(insn, 26, 6, b4);
    tcg_out32(s, insn);
}

static void tcg_out_op_rrrr(TCGContext *s, TCGOpcode op,
                            TCGReg r0, TCGReg r1, TCGReg r2, TCGReg r3)
{
    tcg_insn_unit_tci insn = 0;

    insn = deposit32(insn, 0, 8, op);
    insn = deposit32(insn, 8, 4, r0);
    insn = deposit32(insn, 12, 4, r1);
    insn = deposit32(insn, 16, 4, r2);
    insn = deposit32(insn, 20, 4, r3);
    tcg_out32(s, insn);
}

static void tcg_out_op_rrrrrc(TCGContext *s, TCGOpcode op,
                              TCGReg r0, TCGReg r1, TCGReg r2,
                              TCGReg r3, TCGReg r4, TCGCond c5)
{
    tcg_insn_unit_tci insn = 0;

    insn = deposit32(insn, 0, 8, op);
    insn = deposit32(insn, 8, 4, r0);
    insn = deposit32(insn, 12, 4, r1);
    insn = deposit32(insn, 16, 4, r2);
    insn = deposit32(insn, 20, 4, r3);
    insn = deposit32(insn, 24, 4, r4);
    insn = deposit32(insn, 28, 4, c5);
    tcg_out32(s, insn);
}

static void tcg_tci_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg)
{
    switch (type) {
    case TCG_TYPE_I32:
#if TCG_TARGET_REG_BITS == 64
        arg = (int32_t)arg;
        /* fall through */
    case TCG_TYPE_I64:
#endif
        break;
    default:
        g_assert_not_reached();
    }

    if (arg == sextract32(arg, 0, 20)) {
        tcg_out_op_ri(s, INDEX_op_tci_movi, ret, arg);
    } else {
        tcg_insn_unit_tci insn = 0;

        new_pool_label(s, arg, 20, s->code_ptr, 0);
        insn = deposit32(insn, 0, 8, INDEX_op_tci_movl);
        insn = deposit32(insn, 8, 4, ret);
        tcg_out32(s, insn);
    }
}

static void tcg_tci_out_extract(TCGContext *s, TCGType type, TCGReg rd,
                                TCGReg rs, unsigned pos, unsigned len)
{
    tcg_out_op_rrbb(s, INDEX_op_extract, rd, rs, pos, len);
}

static void tcg_out_ldst(TCGContext *s, TCGOpcode op, TCGReg val,
                         TCGReg base, intptr_t offset)
{
    stack_bounds_check(base, offset);
    if (offset != sextract32(offset, 0, 16)) {
        tcg_tci_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP, offset);
        tcg_out_op_rrr(s, INDEX_op_add, TCG_REG_TMP, TCG_REG_TMP, base);
        base = TCG_REG_TMP;
        offset = 0;
    }
    tcg_out_op_rrs(s, op, val, base, offset);
}

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg val, TCGReg base,
                       intptr_t offset)
{
    TCGOpcode op = INDEX_op_ld;

    if (TCG_TARGET_REG_BITS == 64 && type == TCG_TYPE_I32) {
        op = INDEX_op_ld32u;
    }
    tcg_out_ldst(s, op, val, base, offset);
    tcg_wasm_out_ld(s, type, val, base, offset);
}

static void tcg_tci_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    tcg_out_op_rr(s, INDEX_op_mov, ret, arg);
}

static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    tcg_tci_out_mov(s, type, ret, arg);
    tcg_wasm_out_mov(s, ret, arg);
    return true;
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg)
{
    tcg_tci_out_movi(s, type, ret, arg);
    tcg_wasm_out_movi(s, type, ret, arg);
}

static void tcg_out_extract(TCGContext *s, TCGType type, TCGReg rd,
                            TCGReg rs, unsigned pos, unsigned len)
{
    tcg_tci_out_extract(s, type, rd, rs, pos, len);
    tcg_wasm_out_extract(s, rd, rs, pos, len);
}

static const TCGOutOpExtract outop_extract = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tcg_out_extract,
};

static void tcg_tci_out_sextract(TCGContext *s, TCGType type, TCGReg rd,
                                 TCGReg rs, unsigned pos, unsigned len)
{
    tcg_out_op_rrbb(s, INDEX_op_sextract, rd, rs, pos, len);
}

static void tcg_out_sextract(TCGContext *s, TCGType type, TCGReg rd,
                             TCGReg rs, unsigned pos, unsigned len)
{
    tcg_tci_out_sextract(s, type, rd, rs, pos, len);
    tcg_wasm_out_sextract(s, rd, rs, pos, len);
}

static const TCGOutOpExtract outop_sextract = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tcg_out_sextract,
};

static const TCGOutOpExtract2 outop_extract2 = {
    .base.static_constraint = C_NotImplemented,
};

static void tcg_out_ext8s(TCGContext *s, TCGType type, TCGReg rd, TCGReg rs)
{
    tcg_tci_out_sextract(s, type, rd, rs, 0, 8);
    tcg_wasm_out_ext8s(s, rd, rs);
}

static void tcg_out_ext8u(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_tci_out_extract(s, TCG_TYPE_REG, rd, rs, 0, 8);
    tcg_wasm_out_ext8u(s, rd, rs);
}

static void tcg_out_ext16s(TCGContext *s, TCGType type, TCGReg rd, TCGReg rs)
{
    tcg_tci_out_sextract(s, type, rd, rs, 0, 16);
    tcg_wasm_out_ext16s(s, rd, rs);
}

static void tcg_out_ext16u(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_tci_out_extract(s, TCG_TYPE_REG, rd, rs, 0, 16);
    tcg_wasm_out_ext16u(s, rd, rs);
}

static void tcg_out_ext32s(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_debug_assert(TCG_TARGET_REG_BITS == 64);
    tcg_tci_out_sextract(s, TCG_TYPE_I64, rd, rs, 0, 32);
    tcg_wasm_out_ext32s(s, rd, rs);
}

static void tcg_out_ext32u(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_debug_assert(TCG_TARGET_REG_BITS == 64);
    tcg_tci_out_extract(s, TCG_TYPE_I64, rd, rs, 0, 32);
    tcg_wasm_out_ext32u(s, rd, rs);
}

static void tcg_out_exts_i32_i64(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_ext32s(s, rd, rs);
}

static void tcg_out_extu_i32_i64(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_ext32u(s, rd, rs);
}

static void tcg_out_extrl_i64_i32(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_debug_assert(TCG_TARGET_REG_BITS == 64);
    tcg_tci_out_mov(s, TCG_TYPE_I32, rd, rs);
    tcg_wasm_out_extrl_i64_i32(s, rd, rs);
}

static bool tcg_out_xchg(TCGContext *s, TCGType type, TCGReg r1, TCGReg r2)
{
    return false;
}

static void tcg_out_addi_ptr(TCGContext *s, TCGReg rd, TCGReg rs,
                             tcg_target_long imm)
{
    /* This function is only used for passing structs by reference. */
    g_assert_not_reached();
}

static void tcg_out_call(TCGContext *s, const tcg_insn_unit *func,
                         const TCGHelperInfo *info)
{
    ffi_cif *cif = info->cif;
    tcg_insn_unit_tci insn = 0;
    uint8_t which;

    if (cif->rtype == &ffi_type_void) {
        which = 0;
    } else {
        tcg_debug_assert(cif->rtype->size == 4 ||
                         cif->rtype->size == 8 ||
                         cif->rtype->size == 16);
        which = ctz32(cif->rtype->size) - 1;
    }
    new_pool_l2(s, 20, s->code_ptr, 0, (uintptr_t)func, (uintptr_t)cif);
    insn = deposit32(insn, 0, 8, INDEX_op_call);
    insn = deposit32(insn, 8, 4, which);
    tcg_out32(s, insn);
    tcg_wasm_out_call(s, (intptr_t)func, info);
}

static void tcg_out_exit_tb(TCGContext *s, uintptr_t arg)
{
    tcg_out_op_p(s, INDEX_op_exit_tb, (void *)arg);
    tcg_wasm_out_exit_tb(s, arg);
}

static void tcg_out_goto_tb(TCGContext *s, int which)
{
    /* indirect jump method. */
    tcg_out_op_p(s, INDEX_op_goto_tb, (void *)get_jmp_target_addr(s, which));
    set_jmp_reset_offset(s, which);
    tcg_wasm_out_goto_tb(s, which, (intptr_t)s->code_ptr);
}

static void tcg_out_goto_ptr(TCGContext *s, TCGReg a0)
{
    tcg_out_op_r(s, INDEX_op_goto_ptr, a0);
    tcg_wasm_out_goto_ptr(s, a0);
}

void tb_target_set_jmp_target(const TranslationBlock *tb, int n,
                              uintptr_t jmp_rx, uintptr_t jmp_rw)
{
    /* Always indirect, nothing to do */
}

static void tgen_add(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_add, a0, a1, a2);
    tcg_wasm_out_i64_calc_add(s, a0, a1, a2);
}

static const TCGOutOpBinary outop_add = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_add,
};

static TCGConstraintSetIndex cset_addsubcarry(TCGType type, unsigned flags)
{
    return type == TCG_TYPE_REG ? C_O1_I2(r, r, r) : C_NotImplemented;
}

static void tcg_wasm_out_addco(TCGContext *s, TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_wasm_out_op_global_get_r(s, a1);
    tcg_wasm_out_op_global_get_r(s, a2);
    tcg_wasm_out_op_i64_add(s);
    tcg_wasm_out_op_global_set_r(s, a0);
    tcg_wasm_out_op_global_get_r(s, a0);
#if TCG_TARGET_REG_BITS == 32
    tcg_wasm_out_op_i64_const(s, 32);
    tcg_wasm_out_op_i64_shr_u(s);
    tcg_wasm_out_op_global_set(s, CARRY_IDX);
#else
    if (a0 == a1) {
        tcg_wasm_out_op_global_get_r(s, a2);
    } else {
        tcg_wasm_out_op_global_get_r(s, a1);
    }
    tcg_wasm_out_op_i64_lt_u(s);
    tcg_wasm_out_op_i64_extend_i32_s(s);
    tcg_wasm_out_op_global_set(s, CARRY_IDX);
#endif
}

static void tgen_addco(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_addco, a0, a1, a2);
    tcg_wasm_out_addco(s, a0, a1, a2);
}

static const TCGOutOpBinary outop_addco = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_addsubcarry,
    .out_rrr = tgen_addco,
};

static void tcg_wasm_out_addci(TCGContext *s, TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_wasm_out_op_global_get_r(s, a1);
    tcg_wasm_out_op_global_get_r(s, a2);
    tcg_wasm_out_op_i64_add(s);
    tcg_wasm_out_op_global_get(s, CARRY_IDX);
    tcg_wasm_out_op_i64_add(s);
    tcg_wasm_out_op_global_set_r(s, a0);
}

static void tgen_addci(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_addci, a0, a1, a2);
    tcg_wasm_out_addci(s, a0, a1, a2);
}

static const TCGOutOpAddSubCarry outop_addci = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_addsubcarry,
    .out_rrr = tgen_addci,
};

static void tcg_wasm_out_addcio(TCGContext *s, TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_wasm_out_op_global_get(s, CARRY_IDX);
    tcg_wasm_out_op_i32_wrap_i64(s);
    tcg_wasm_out_op_if_noret(s);

    tcg_wasm_out_op_global_get_r(s, a1);
    tcg_wasm_out_op_global_get_r(s, a2);
    tcg_wasm_out_op_i64_add(s);
    tcg_wasm_out_op_i64_const(s, 1);
    tcg_wasm_out_op_i64_add(s);
    tcg_wasm_out_op_global_set_r(s, a0);
    tcg_wasm_out_op_global_get_r(s, a0);
#if TCG_TARGET_REG_BITS == 32
    tcg_wasm_out_op_i64_const(s, 32);
    tcg_wasm_out_op_i64_shr_u(s);
    tcg_wasm_out_op_global_set(s, CARRY_IDX);
#else
    if (a0 == a1) {
        tcg_wasm_out_op_global_get_r(s, a2);
    } else {
        tcg_wasm_out_op_global_get_r(s, a1);
    }
    tcg_wasm_out_op_i64_le_u(s);
    tcg_wasm_out_op_i64_extend_i32_s(s);
    tcg_wasm_out_op_global_set(s, CARRY_IDX);
#endif

    tcg_wasm_out_op_else(s);

    tcg_wasm_out_op_global_get_r(s, a1);
    tcg_wasm_out_op_global_get_r(s, a2);
    tcg_wasm_out_op_i64_add(s);
    tcg_wasm_out_op_global_set_r(s, a0);
    tcg_wasm_out_op_global_get_r(s, a0);
#if TCG_TARGET_REG_BITS == 32
    tcg_wasm_out_op_i64_const(s, 32);
    tcg_wasm_out_op_i64_shr_u(s);
    tcg_wasm_out_op_global_set(s, CARRY_IDX);
#else
    if (a0 == a1) {
        tcg_wasm_out_op_global_get_r(s, a2);
    } else {
        tcg_wasm_out_op_global_get_r(s, a1);
    }
    tcg_wasm_out_op_i64_lt_u(s);
    tcg_wasm_out_op_i64_extend_i32_s(s);
    tcg_wasm_out_op_global_set(s, CARRY_IDX);
#endif

    tcg_wasm_out_op_end(s);
}

static void tgen_addcio(TCGContext *s, TCGType type,
                        TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_addcio, a0, a1, a2);
    tcg_wasm_out_addcio(s, a0, a1, a2);
}

static const TCGOutOpBinary outop_addcio = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_addsubcarry,
    .out_rrr = tgen_addcio,
};

static void tcg_out_set_carry(TCGContext *s)
{
    tcg_out_op_v(s, INDEX_op_tci_setcarry);
    tcg_wasm_out_op_i64_const(s, 1);
    tcg_wasm_out_op_global_set(s, CARRY_IDX);
}

static void tgen_and(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_and, a0, a1, a2);
    tcg_wasm_out_i64_calc_and(s, a0, a1, a2);
}

static const TCGOutOpBinary outop_and = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_and,
};

static void tgen_andc(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_andc, a0, a1, a2);
    tcg_wasm_out_andc(s, a0, a1, a2);
}

static const TCGOutOpBinary outop_andc = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_andc,
};

static void tgen_clz(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    TCGOpcode opc = (type == TCG_TYPE_I32
                     ? INDEX_op_tci_clz32
                     : INDEX_op_clz);
    tcg_out_op_rrr(s, opc, a0, a1, a2);
    tcg_wasm_out_clz(s, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_clz = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_clz,
};

static void tgen_ctz(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    TCGOpcode opc = (type == TCG_TYPE_I32
                     ? INDEX_op_tci_ctz32
                     : INDEX_op_ctz);
    tcg_out_op_rrr(s, opc, a0, a1, a2);
    tcg_wasm_out_ctz(s, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_ctz = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_ctz,
};

static void tgen_deposit(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1,
                         TCGReg a2, unsigned ofs, unsigned len)
{
    tcg_out_op_rrrbb(s, INDEX_op_deposit, a0, a1, a2, ofs, len);
    tcg_wasm_out_deposit(s, a0, a1, a2, ofs, len);
}

static const TCGOutOpDeposit outop_deposit = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_deposit,
};

static void tgen_divs(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    TCGOpcode opc = (type == TCG_TYPE_I32
                     ? INDEX_op_tci_divs32
                     : INDEX_op_divs);
    tcg_out_op_rrr(s, opc, a0, a1, a2);
    tcg_wasm_out_div_s(s, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_divs = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_divs,
};

static const TCGOutOpDivRem outop_divs2 = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_divu(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    TCGOpcode opc = (type == TCG_TYPE_I32
                     ? INDEX_op_tci_divu32
                     : INDEX_op_divu);
    tcg_out_op_rrr(s, opc, a0, a1, a2);
    tcg_wasm_out_div_u(s, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_divu = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_divu,
};

static const TCGOutOpDivRem outop_divu2 = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_eqv(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_eqv, a0, a1, a2);
    tcg_wasm_out_eqv(s, a0, a1, a2);
}

static const TCGOutOpBinary outop_eqv = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_eqv,
};

#if TCG_TARGET_REG_BITS == 64
static void tcg_wasm_out_extrh_i64_i32(TCGContext *s, TCGReg a0, TCGReg a1)
{
    tcg_wasm_out_op_global_get_r(s, a1);
    tcg_wasm_out_op_i64_const(s, 32);
    tcg_wasm_out_op_i64_shr_u(s);
    tcg_wasm_out_op_global_set_r(s, a0);
}

static void tgen_extrh_i64_i32(TCGContext *s, TCGType t, TCGReg a0, TCGReg a1)
{
    tcg_tci_out_extract(s, TCG_TYPE_I64, a0, a1, 32, 32);
    tcg_wasm_out_extrh_i64_i32(s, a0, a1);
}

static const TCGOutOpUnary outop_extrh_i64_i32 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_extrh_i64_i32,
};
#endif

static void tgen_mul(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_mul, a0, a1, a2);
    tcg_wasm_out_i64_calc_mul(s, a0, a1, a2);
}

static const TCGOutOpBinary outop_mul = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_mul,
};

static TCGConstraintSetIndex cset_mul2(TCGType type, unsigned flags)
{
    return type == TCG_TYPE_REG ? C_O2_I2(r, r, r, r) : C_NotImplemented;
}

static void tgen_muls2(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2, TCGReg a3)
{
    tcg_out_op_rrrr(s, INDEX_op_muls2, a0, a1, a2, a3);
    tcg_wasm_out_op_global_get_r(s, a2);
    tcg_wasm_out_op_global_get_r(s, a3);
    tcg_wasm_out_op_i64_mul(s);
    tcg_wasm_out_op_global_set_r(s, a0);

    tcg_wasm_out_op_global_get_r(s, a0);
    tcg_wasm_out_op_i64_const(s, 32);
    tcg_wasm_out_op_i64_shr_s(s);
    tcg_wasm_out_op_global_set_r(s, a1);
}

static const TCGOutOpMul2 outop_muls2 = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_mul2,
    .out_rrrr = tgen_muls2,
};

static const TCGOutOpBinary outop_mulsh = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_mulu2(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2, TCGReg a3)
{
    tcg_out_op_rrrr(s, INDEX_op_mulu2, a0, a1, a2, a3);
    tcg_wasm_out_op_global_get_r(s, a2);
    tcg_wasm_out_op_global_get_r(s, a3);
    tcg_wasm_out_op_i64_mul(s);
    tcg_wasm_out_op_global_set_r(s, a0);

    tcg_wasm_out_op_global_get_r(s, a0);
    tcg_wasm_out_op_i64_const(s, 32);
    tcg_wasm_out_op_i64_shr_u(s);
    tcg_wasm_out_op_global_set_r(s, a1);
}

static const TCGOutOpMul2 outop_mulu2 = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_mul2,
    .out_rrrr = tgen_mulu2,
};

static const TCGOutOpBinary outop_muluh = {
    .base.static_constraint = C_NotImplemented,
};

static void tgen_nand(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_nand, a0, a1, a2);
    tcg_wasm_out_nand(s, a0, a1, a2);
}

static const TCGOutOpBinary outop_nand = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_nand,
};

static void tgen_nor(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_nor, a0, a1, a2);
    tcg_wasm_out_nor(s, a0, a1, a2);
}

static const TCGOutOpBinary outop_nor = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_nor,
};

static void tgen_or(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_or, a0, a1, a2);
    tcg_wasm_out_i64_calc_or(s, a0, a1, a2);
}

static const TCGOutOpBinary outop_or = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_or,
};

static void tgen_orc(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_orc, a0, a1, a2);
    tcg_wasm_out_orc(s, a0, a1, a2);
}

static const TCGOutOpBinary outop_orc = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_orc,
};

static void tgen_rems(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    TCGOpcode opc = (type == TCG_TYPE_I32
                     ? INDEX_op_tci_rems32
                     : INDEX_op_rems);
    tcg_out_op_rrr(s, opc, a0, a1, a2);
    tcg_wasm_out_rem_s(s, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_rems = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_rems,
};

static void tgen_remu(TCGContext *s, TCGType type,
                      TCGReg a0, TCGReg a1, TCGReg a2)
{
    TCGOpcode opc = (type == TCG_TYPE_I32
                     ? INDEX_op_tci_remu32
                     : INDEX_op_remu);
    tcg_out_op_rrr(s, opc, a0, a1, a2);
    tcg_wasm_out_rem_u(s, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_remu = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_remu,
};

static void tgen_rotl(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    TCGOpcode opc = (type == TCG_TYPE_I32
                     ? INDEX_op_tci_rotl32
                     : INDEX_op_rotl);
    tcg_out_op_rrr(s, opc, a0, a1, a2);
    tcg_wasm_out_rotl(s, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_rotl = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_rotl,
};

static void tgen_rotr(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    TCGOpcode opc = (type == TCG_TYPE_I32
                     ? INDEX_op_tci_rotr32
                     : INDEX_op_rotr);
    tcg_out_op_rrr(s, opc, a0, a1, a2);
    tcg_wasm_out_rotr(s, type, a0, a1, a2);
}

static const TCGOutOpBinary outop_rotr = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_rotr,
};

static void tgen_sar(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    TCGReg orig_a1 = a1;
    if (type < TCG_TYPE_REG) {
        tcg_tci_out_sextract(s, TCG_TYPE_I64, TCG_REG_TMP, a1, 0, 32);
        a1 = TCG_REG_TMP;
    }
    tcg_out_op_rrr(s, INDEX_op_sar, a0, a1, a2);
    tcg_wasm_out_shr_s(s, type, a0, orig_a1, a2);
}

static const TCGOutOpBinary outop_sar = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_sar,
};

static void tgen_shl(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_shl, a0, a1, a2);
    tcg_wasm_out_shl(s, a0, a1, a2);
}

static const TCGOutOpBinary outop_shl = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_shl,
};

static void tgen_shr(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    TCGReg orig_a1 = a1;
    if (type < TCG_TYPE_REG) {
        tcg_tci_out_extract(s, TCG_TYPE_I64, TCG_REG_TMP, a1, 0, 32);
        a1 = TCG_REG_TMP;
    }
    tcg_out_op_rrr(s, INDEX_op_shr, a0, a1, a2);
    tcg_wasm_out_shr_u(s, type, a0, orig_a1, a2);
}

static const TCGOutOpBinary outop_shr = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_shr,
};

static void tgen_sub(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_sub, a0, a1, a2);
    tcg_wasm_out_i64_calc_sub(s, a0, a1, a2);
}

static const TCGOutOpSubtract outop_sub = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_sub,
};

static void tcg_wasm_out_subbo(TCGContext *s, TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_wasm_out_op_global_get_r(s, a1);
    tcg_wasm_out_op_global_get_r(s, a2);
    tcg_wasm_out_op_i64_lt_u(s);
    tcg_wasm_out_op_i64_extend_i32_s(s);
    tcg_wasm_out_op_global_set(s, CARRY_IDX);

    tcg_wasm_out_op_global_get_r(s, a1);
    tcg_wasm_out_op_global_get_r(s, a2);
    tcg_wasm_out_op_i64_sub(s);
    tcg_wasm_out_op_global_set_r(s, a0);
}

static void tgen_subbo(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_subbo, a0, a1, a2);
    tcg_wasm_out_subbo(s, a0, a1, a2);
}

static const TCGOutOpAddSubCarry outop_subbo = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_addsubcarry,
    .out_rrr = tgen_subbo,
};

static void tcg_wasm_out_subbi(TCGContext *s, TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_wasm_out_op_global_get_r(s, a1);
    tcg_wasm_out_op_global_get_r(s, a2);
    tcg_wasm_out_op_i64_sub(s);
    tcg_wasm_out_op_global_get(s, CARRY_IDX);
    tcg_wasm_out_op_i64_sub(s);
    tcg_wasm_out_op_global_set_r(s, a0);
}

static void tgen_subbi(TCGContext *s, TCGType type,
                       TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_subbi, a0, a1, a2);
    tcg_wasm_out_subbi(s, a0, a1, a2);
}

static const TCGOutOpAddSubCarry outop_subbi = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_addsubcarry,
    .out_rrr = tgen_subbi,
};

static void tcg_wasm_out_subbio(TCGContext *s, TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_wasm_out_op_global_get(s, CARRY_IDX);
    tcg_wasm_out_op_i32_wrap_i64(s);
    tcg_wasm_out_op_if_noret(s);

    tcg_wasm_out_op_global_get_r(s, a1);
    tcg_wasm_out_op_global_get_r(s, a2);
    tcg_wasm_out_op_i64_le_u(s);
    tcg_wasm_out_op_i64_extend_i32_s(s);
    tcg_wasm_out_op_global_set(s, CARRY_IDX);
    tcg_wasm_out_op_global_get_r(s, a1);
    tcg_wasm_out_op_global_get_r(s, a2);
    tcg_wasm_out_op_i64_sub(s);
    tcg_wasm_out_op_i64_const(s, 1);
    tcg_wasm_out_op_i64_sub(s);
    tcg_wasm_out_op_global_set_r(s, a0);

    tcg_wasm_out_op_else(s);

    tcg_wasm_out_op_global_get_r(s, a1);
    tcg_wasm_out_op_global_get_r(s, a2);
    tcg_wasm_out_op_i64_lt_u(s);
    tcg_wasm_out_op_i64_extend_i32_s(s);
    tcg_wasm_out_op_global_set(s, CARRY_IDX);
    tcg_wasm_out_op_global_get_r(s, a1);
    tcg_wasm_out_op_global_get_r(s, a2);
    tcg_wasm_out_op_i64_sub(s);
    tcg_wasm_out_op_global_set_r(s, a0);

    tcg_wasm_out_op_end(s);
}

static void tgen_subbio(TCGContext *s, TCGType type,
                        TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_subbio, a0, a1, a2);
    tcg_wasm_out_subbio(s, a0, a1, a2);
}

static const TCGOutOpAddSubCarry outop_subbio = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_addsubcarry,
    .out_rrr = tgen_subbio,
};

static void tcg_out_set_borrow(TCGContext *s)
{
    tcg_out_op_v(s, INDEX_op_tci_setcarry);  /* borrow == carry */
    tcg_wasm_out_op_i64_const(s, 1);
    tcg_wasm_out_op_global_set(s, CARRY_IDX);
}

static void tgen_xor(TCGContext *s, TCGType type,
                     TCGReg a0, TCGReg a1, TCGReg a2)
{
    tcg_out_op_rrr(s, INDEX_op_xor, a0, a1, a2);
    tcg_wasm_out_i64_calc_xor(s, a0, a1, a2);
}

static const TCGOutOpBinary outop_xor = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_xor,
};

static void tgen_ctpop(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tcg_out_op_rr(s, INDEX_op_ctpop, a0, a1);
    tcg_wasm_out_ctpop(s, a0, a1);
}

static TCGConstraintSetIndex cset_ctpop(TCGType type, unsigned flags)
{
    return type == TCG_TYPE_REG ? C_O1_I1(r, r) : C_NotImplemented;
}

static const TCGOutOpUnary outop_ctpop = {
    .base.static_constraint = C_Dynamic,
    .base.dynamic_constraint = cset_ctpop,
    .out_rr = tgen_ctpop,
};

static void tgen_bswap16(TCGContext *s, TCGType type,
                         TCGReg a0, TCGReg a1, unsigned flags)
{
    tcg_out_op_rr(s, INDEX_op_bswap16, a0, a1);
    if (flags & TCG_BSWAP_OS) {
        tcg_tci_out_sextract(s, TCG_TYPE_REG, a0, a0, 0, 16);
    }
    tcg_wasm_out_bswap16(s, a0, a1, flags);
}

static const TCGOutOpBswap outop_bswap16 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_bswap16,
};

static void tgen_bswap32(TCGContext *s, TCGType type,
                         TCGReg a0, TCGReg a1, unsigned flags)
{
    tcg_out_op_rr(s, INDEX_op_bswap32, a0, a1);
    if (flags & TCG_BSWAP_OS) {
        tcg_tci_out_sextract(s, TCG_TYPE_REG, a0, a0, 0, 32);
    }
    tcg_wasm_out_bswap32(s, a0, a1, flags);
}

static const TCGOutOpBswap outop_bswap32 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_bswap32,
};

#if TCG_TARGET_REG_BITS == 64
static void tgen_bswap64(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tcg_out_op_rr(s, INDEX_op_bswap64, a0, a1);
    tcg_wasm_out_bswap64(s, a0, a1);
}

static const TCGOutOpUnary outop_bswap64 = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_bswap64,
};
#endif

static void tgen_neg_tci(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tcg_out_op_rr(s, INDEX_op_neg, a0, a1);
 }

static void tgen_neg(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tgen_neg_tci(s, type, a0, a1);
    tcg_wasm_out_neg(s, a0, a1);
}

static const TCGOutOpUnary outop_neg = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_neg,
};

static void tgen_not(TCGContext *s, TCGType type, TCGReg a0, TCGReg a1)
{
    tcg_out_op_rr(s, INDEX_op_not, a0, a1);
    tcg_wasm_out_not(s, a0, a1);
}

static const TCGOutOpUnary outop_not = {
    .base.static_constraint = C_O1_I1(r, r),
    .out_rr = tgen_not,
};

static void tgen_setcond_tci(TCGContext *s, TCGType type, TCGCond cond,
                             TCGReg dest, TCGReg arg1, TCGReg arg2)
{
    TCGOpcode opc = (type == TCG_TYPE_I32
                     ? INDEX_op_tci_setcond32
                     : INDEX_op_setcond);
    tcg_out_op_rrrc(s, opc, dest, arg1, arg2, cond);
}

static void tgen_setcond(TCGContext *s, TCGType type, TCGCond cond,
                         TCGReg dest, TCGReg arg1, TCGReg arg2)
{
    tgen_setcond_tci(s, type, cond, dest, arg1, arg2);
    tcg_wasm_out_setcond(s, type, dest, arg1, arg2, cond);
}

static const TCGOutOpSetcond outop_setcond = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_setcond,
};

static void tgen_negsetcond(TCGContext *s, TCGType type, TCGCond cond,
                            TCGReg dest, TCGReg arg1, TCGReg arg2)
{
    tgen_setcond_tci(s, type, cond, dest, arg1, arg2);
    tgen_neg_tci(s, type, dest, dest);
    tcg_wasm_out_negsetcond(s, type, dest, arg1, arg2, cond);
}

static const TCGOutOpSetcond outop_negsetcond = {
    .base.static_constraint = C_O1_I2(r, r, r),
    .out_rrr = tgen_negsetcond,
};

static void tgen_brcond(TCGContext *s, TCGType type, TCGCond cond,
                        TCGReg arg0, TCGReg arg1, TCGLabel *l)
{
    tgen_setcond_tci(s, type, cond, TCG_REG_TMP, arg0, arg1);
    tcg_out_op_rl(s, INDEX_op_brcond, TCG_REG_TMP, l);
    tcg_wasm_out_brcond(s, type, arg0, arg1, cond, l);
}

static const TCGOutOpBrcond outop_brcond = {
    .base.static_constraint = C_O0_I2(r, r),
    .out_rr = tgen_brcond,
};

static void tgen_movcond(TCGContext *s, TCGType type, TCGCond cond,
                         TCGReg ret, TCGReg c1, TCGArg c2, bool const_c2,
                         TCGArg vt, bool const_vt, TCGArg vf, bool consf_vf)
{
    TCGOpcode opc = (type == TCG_TYPE_I32
                     ? INDEX_op_tci_movcond32
                     : INDEX_op_movcond);
    tcg_out_op_rrrrrc(s, opc, ret, c1, c2, vt, vf, cond);
    tcg_wasm_out_movcond(s, type, ret, c1, c2, vt, vf, cond);
}

static const TCGOutOpMovcond outop_movcond = {
    .base.static_constraint = C_O1_I4(r, r, r, r, r),
    .out = tgen_movcond,
};

static void tgen_brcond2(TCGContext *s, TCGCond cond, TCGReg al, TCGReg ah,
                         TCGArg bl, bool const_bl,
                         TCGArg bh, bool const_bh, TCGLabel *l)
{
    uint8_t op = tcg_cond_to_inst[cond].i64;
    tcg_out_op_rrrrrc(s, INDEX_op_setcond2_i32, TCG_REG_TMP,
                      al, ah, bl, bh, cond);
    tcg_out_op_rl(s, INDEX_op_brcond, TCG_REG_TMP, l);

    tcg_wasm_out_op_global_get_r(s, al);
    tcg_wasm_out_op_i64_const(s, 0xffffffff);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_global_get_r(s, ah);
    tcg_wasm_out_op_i64_const(s, 32);
    tcg_wasm_out_op_i64_shl(s);
    tcg_wasm_out_op_i64_or(s);

    tcg_wasm_out_op_global_get_r(s, bl);
    tcg_wasm_out_op_i64_const(s, 0xffffffff);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_global_get_r(s, bh);
    tcg_wasm_out_op_i64_const(s, 32);
    tcg_wasm_out_op_i64_shl(s);
    tcg_wasm_out_op_i64_or(s);

    tcg_wasm_out8(s, op);

    tcg_wasm_out_op_br_to_label(s, l, true);
}

#if TCG_TARGET_REG_BITS != 32
__attribute__((unused))
#endif
static const TCGOutOpBrcond2 outop_brcond2 = {
    .base.static_constraint = C_O0_I4(r, r, r, r),
    .out = tgen_brcond2,
};

static void tgen_setcond2(TCGContext *s, TCGCond cond, TCGReg ret,
                          TCGReg al, TCGReg ah,
                          TCGArg bl, bool const_bl,
                          TCGArg bh, bool const_bh)
{
    uint8_t op = tcg_cond_to_inst[cond].i64;
    tcg_out_op_rrrrrc(s, INDEX_op_setcond2_i32, ret, al, ah, bl, bh, cond);

    tcg_wasm_out_op_global_get_r(s, al);
    tcg_wasm_out_op_i64_const(s, 0xffffffff);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_global_get_r(s, ah);
    tcg_wasm_out_op_i64_const(s, 32);
    tcg_wasm_out_op_i64_shl(s);
    tcg_wasm_out_op_i64_or(s);

    tcg_wasm_out_op_global_get_r(s, bl);
    tcg_wasm_out_op_i64_const(s, 0xffffffff);
    tcg_wasm_out_op_i64_and(s);
    tcg_wasm_out_op_global_get_r(s, bh);
    tcg_wasm_out_op_i64_const(s, 32);
    tcg_wasm_out_op_i64_shl(s);
    tcg_wasm_out_op_i64_or(s);

    tcg_wasm_out8(s, op);

    tcg_wasm_out_op_i64_extend_i32_u(s);
    tcg_wasm_out_op_global_set_r(s, ret);
}

#if TCG_TARGET_REG_BITS != 32
__attribute__((unused))
#endif
static const TCGOutOpSetcond2 outop_setcond2 = {
    .base.static_constraint = C_O1_I4(r, r, r, r, r),
    .out = tgen_setcond2,
};

static void tcg_out_mb(TCGContext *s, unsigned a0)
{
    tcg_out_op_v(s, INDEX_op_mb);
}

static void tcg_out_br(TCGContext *s, TCGLabel *l)
{
    tcg_out_op_l(s, INDEX_op_br, l);
    tcg_wasm_out_br(s, l);
}

static void tgen_ld8u(TCGContext *s, TCGType type, TCGReg dest,
                      TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, INDEX_op_ld8u, dest, base, offset);
    tcg_wasm_out_ld8u(s, dest, base, offset);
}

static const TCGOutOpLoad outop_ld8u = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld8u,
};

static void tgen_ld8s(TCGContext *s, TCGType type, TCGReg dest,
                      TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, INDEX_op_ld8s, dest, base, offset);
    tcg_wasm_out_ld8s(s, dest, base, offset);
}

static const TCGOutOpLoad outop_ld8s = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld8s,
};

static void tgen_ld16u(TCGContext *s, TCGType type, TCGReg dest,
                       TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, INDEX_op_ld16u, dest, base, offset);
    tcg_wasm_out_ld16u(s, dest, base, offset);
}

static const TCGOutOpLoad outop_ld16u = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld16u,
};

static void tgen_ld16s(TCGContext *s, TCGType type, TCGReg dest,
                       TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, INDEX_op_ld16s, dest, base, offset);
    tcg_wasm_out_ld16s(s, dest, base, offset);
}

static const TCGOutOpLoad outop_ld16s = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld16s,
};

#if TCG_TARGET_REG_BITS == 64
static void tgen_ld32u(TCGContext *s, TCGType type, TCGReg dest,
                       TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, INDEX_op_ld32u, dest, base, offset);
    tcg_wasm_out_ld32u(s, dest, base, offset);
}

static const TCGOutOpLoad outop_ld32u = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld32u,
};

static void tgen_ld32s(TCGContext *s, TCGType type, TCGReg dest,
                       TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, INDEX_op_ld32s, dest, base, offset);
    tcg_wasm_out_ld32s(s, dest, base, offset);
}

static const TCGOutOpLoad outop_ld32s = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_ld32s,
};
#endif

static void tgen_st8(TCGContext *s, TCGType type, TCGReg data,
                     TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, INDEX_op_st8, data, base, offset);
    tcg_wasm_out_st8(s, data, base, offset);
}

static const TCGOutOpStore outop_st8 = {
    .base.static_constraint = C_O0_I2(r, r),
    .out_r = tgen_st8,
};

static void tgen_st16(TCGContext *s, TCGType type, TCGReg data,
                      TCGReg base, ptrdiff_t offset)
{
    tcg_out_ldst(s, INDEX_op_st16, data, base, offset);
    tcg_wasm_out_st16(s, data, base, offset);
}

static const TCGOutOpStore outop_st16 = {
    .base.static_constraint = C_O0_I2(r, r),
    .out_r = tgen_st16,
};

static const TCGOutOpStore outop_st = {
    .base.static_constraint = C_O0_I2(r, r),
    .out_r = tcg_out_st,
};

static void tgen_qemu_ld(TCGContext *s, TCGType type, TCGReg data,
                         TCGReg addr, MemOpIdx oi)
{
    tcg_out_op_rrm(s, INDEX_op_qemu_ld, data, addr, oi);
    tcg_wasm_out_qemu_ld(s, data, addr, oi, NULL);
}

static const TCGOutOpQemuLdSt outop_qemu_ld = {
    .base.static_constraint = C_O1_I1(r, r),
    .out = tgen_qemu_ld,
};

static void tgen_qemu_ld2(TCGContext *s, TCGType type, TCGReg datalo,
                          TCGReg datahi, TCGReg addr, MemOpIdx oi)
{
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_TMP, oi);
    tcg_out_op_rrrr(s, INDEX_op_qemu_ld2, datalo, datahi, addr, TCG_REG_TMP);

    tcg_wasm_out_qemu_ld(s, datalo, addr, oi, &datahi);
}

static const TCGOutOpQemuLdSt2 outop_qemu_ld2 = {
    .base.static_constraint =
        TCG_TARGET_REG_BITS == 64 ? C_NotImplemented : C_O2_I1(r, r, r),
    .out =
        TCG_TARGET_REG_BITS == 64 ? NULL : tgen_qemu_ld2,
};

static void tgen_qemu_st(TCGContext *s, TCGType type, TCGReg data,
                         TCGReg addr, MemOpIdx oi)
{
    tcg_out_op_rrm(s, INDEX_op_qemu_st, data, addr, oi);
    tcg_wasm_out_qemu_st(s, data, addr, oi, NULL);
}

static const TCGOutOpQemuLdSt outop_qemu_st = {
    .base.static_constraint = C_O0_I2(r, r),
    .out = tgen_qemu_st,
};

static void tgen_qemu_st2(TCGContext *s, TCGType type, TCGReg datalo,
                          TCGReg datahi, TCGReg addr, MemOpIdx oi)
{
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_TMP, oi);
    tcg_out_op_rrrr(s, INDEX_op_qemu_st2, datalo, datahi, addr, TCG_REG_TMP);

    tcg_wasm_out_qemu_st(s, datalo, addr, oi, &datahi);
}

static const TCGOutOpQemuLdSt2 outop_qemu_st2 = {
    .base.static_constraint =
        TCG_TARGET_REG_BITS == 64 ? C_NotImplemented : C_O0_I3(r, r, r),
    .out =
        TCG_TARGET_REG_BITS == 64 ? NULL : tgen_qemu_st2,
};

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg val, TCGReg base,
                       intptr_t offset)
{
    TCGOpcode op = INDEX_op_st;

    if (TCG_TARGET_REG_BITS == 64 && type == TCG_TYPE_I32) {
        op = INDEX_op_st32;
    }
    tcg_out_ldst(s, op, val, base, offset);
    tcg_wasm_out_st(s, type, val, base, offset);
}

static inline bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                               TCGReg base, intptr_t ofs)
{
    return false;
}

/* Test if a constant matches the constraint. */
static bool tcg_target_const_match(int64_t val, int ct,
                                   TCGType type, TCGCond cond, int vece)
{
    return ct & TCG_CT_CONST;
}

static void tcg_out_nop_fill(tcg_insn_unit *p, int count)
{
    memset(p, 0, sizeof(*p) * count);
}

static void tcg_target_init(TCGContext *s)
{
    /* The current code uses uint8_t for tcg operations. */
    tcg_debug_assert(tcg_op_defs_max <= UINT8_MAX);

    /* Registers available for 32 bit operations. */
    tcg_target_available_regs[TCG_TYPE_I32] = BIT(TCG_TARGET_NB_REGS) - 1;
    /* Registers available for 64 bit operations. */
    tcg_target_available_regs[TCG_TYPE_I64] = BIT(TCG_TARGET_NB_REGS) - 1;
    /*
     * The interpreter "registers" are in the local stack frame and
     * cannot be clobbered by the called helper functions.  However,
     * the interpreter assumes a 128-bit return value and assigns to
     * the return value registers.
     */
    tcg_target_call_clobber_regs =
        MAKE_64BIT_MASK(TCG_REG_R0, 128 / TCG_TARGET_REG_BITS);

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);

    /* The call arguments come first, followed by the temp storage. */
    tcg_set_frame(s, TCG_REG_CALL_STACK, TCG_STATIC_CALL_ARGS_SIZE,
                  TCG_STATIC_FRAME_SIZE);
}

/* Generate global QEMU prologue and epilogue code. */
static inline void tcg_target_qemu_prologue(TCGContext *s)
{
}

static const uint8_t mod_1[] = {
    0x0, 0x61, 0x73, 0x6d, /* magic */
    0x01, 0x0, 0x0, 0x0,   /* version */

    0x01,                         /* type section */
    0x80, 0x80, 0x80, 0x80, 0x00, /* placehodler for size */
    0x80, 0x80, 0x80, 0x80, 0x00, /* placehodler for num of types vec */
    0x60,                         /* 0: Type of "start" function */
    0x01, PTR_TYPE,               /* arg: ctx pointer (i32) */
    0x01, PTR_TYPE,               /* return: res (i32) */
    0x60,                         /* 1: Type of the asyncify helper */
    0x0,                          /* no argument */
    0x01, 0x7f,                   /* return: res (i32) */
};

static const uint8_t mod_2[] = {
    0x02,                                     /* import section */
    0x80, 0x80, 0x80, 0x80, 0x00,             /* placehodler for size */
    0x80, 0x80, 0x80, 0x80, 0x00,             /* placehodler for imports num */
    0x03, 0x65, 0x6e, 0x76,                   /* module: "env" */
    0x06, 0x62, 0x75, 0x66, 0x66, 0x65, 0x72, /* name: "buffer" */
#if (TCG_TARGET_REG_BITS == 32) || (WASM64_MEMORY64 == 2)
    0x02, 0x03,                               /* shared mem */
    0x00, 0xff, 0xff, 0x03,                   /* min: 0, max: 0xffff pages */
#else
    0x02, 0x07,                               /* shared mem */
    0x00, 0x80, 0x80, 0x10,                   /* min: 0, max: 262144 pages */
#endif
    0x06, 0x68, 0x65, 0x6c, 0x70, 0x65, 0x72, /* module: "helper" */
    0x01, 0x75,                               /* name: "u" */
    0x00, 0x01,                               /* func type 1 */
};

static const uint8_t mod_3[] = {
    0x03,       /* function section */
    2, 1, 0x00, /* function type 0 */

    0x06,                         /* global section */
    0x5b,                         /* section size */
    18,                           /* num of global vars */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */
    0x7e, 0x01, 0x42, 0x00, 0x0b, /* 0-cleared 64bit var */

    0x07,                               /* export section */
    13,                                 /* size of section */
    1,                                  /* num of funcs */
    0x05, 0x73, 0x74, 0x61, 0x72, 0x74, /* "start" function */
    0x00, 0x80, 0x80, 0x80, 0x80, 0x00, /* placeholder for func index*/
};

static const uint8_t mod_4[] = {
    0x0a,                         /* code section */
    0x80, 0x80, 0x80, 0x80, 0x00, /* placeholder for section size*/
    1,                            /* num of codes */
    0x80, 0x80, 0x80, 0x80, 0x00, /* placeholder for code size */
    0x2, 0x3, 0x7f, 0x2, 0x7e,    /* variables (32bit*2, 64bit*2) */
};

static int write_mod_1(TCGContext *s)
{
    void *base = s->code_ptr;
    int helpers_num = helpers_len();

    if (unlikely(((void *)s->code_ptr + sizeof(mod_1) + types_buf_len())
                 > s->code_gen_highwater)) {
        return -1;
    }

    memcpy(s->code_ptr, mod_1, sizeof(mod_1));
    s->code_ptr += sizeof(mod_1);
    linked_buf_write(types_buf_root, s->code_ptr);
    s->code_ptr += types_buf_len();

    uint32_t type_section_size = types_buf_len() + 14;
    fill_uint32_leb128(base + 9, type_section_size);
    fill_uint32_leb128(base + 14, HELPER_IDX_START + helpers_num + 1);

    return 0;
}

static int write_mod_2(TCGContext *s)
{
    void *base = s->code_ptr;
    int helpers_num = helpers_len();

    if (unlikely(((void *)s->code_ptr + sizeof(mod_2))
                 > s->code_gen_highwater)) {
        return -1;
    }

    tcg_debug_assert(helpers_num <= INT_MAX);
    memcpy(s->code_ptr, mod_2, sizeof(mod_2));
    s->code_ptr += sizeof(mod_2);
    for (int i = 0; i < helpers_num; i++) {
        int typeidx = HELPER_IDX_START + i + 1;
        char buf[11]; /* enough for decimal int max + NULL*/
        int n = snprintf(buf, sizeof(buf), "%d", i);
        tcg_debug_assert(n < sizeof(buf));
        *(uint8_t *)s->code_ptr++ = 6; /* helper */
        *(uint8_t *)s->code_ptr++ = 0x68;
        *(uint8_t *)s->code_ptr++ = 0x65;
        *(uint8_t *)s->code_ptr++ = 0x6c;
        *(uint8_t *)s->code_ptr++ = 0x70;
        *(uint8_t *)s->code_ptr++ = 0x65;
        *(uint8_t *)s->code_ptr++ = 0x72;
        s->code_ptr += write_uint32_leb128((uint8_t *)s->code_ptr, n);
        memcpy(s->code_ptr, buf, n);
        s->code_ptr += n;
        *(uint8_t *)s->code_ptr++ = 0x00; /* type(0) */
        s->code_ptr += write_uint32_leb128((uint8_t *)s->code_ptr, typeidx);
        if (unlikely(((void *)s->code_ptr > s->code_gen_highwater))) {
            return -1;
        }
    }

    uint32_t import_section_size = (int)s->code_ptr - (int)base - 6;
    fill_uint32_leb128(base + 1, import_section_size);
    fill_uint32_leb128(base + 6, HELPER_IDX_START + helpers_num + 1);

    return 0;
}

static int write_mod_3(TCGContext *s)
{
    void *base = s->code_ptr;

    if (unlikely(((void *)s->code_ptr + sizeof(mod_3))
                 > s->code_gen_highwater)) {
        return -1;
    }

    memcpy(s->code_ptr, mod_3, sizeof(mod_3));
    s->code_ptr += sizeof(mod_3);

    int startidx = HELPER_IDX_START + helpers_len();
    fill_uint32_leb128(base + 107, startidx);

    return 0;
}

static int write_mod_4(TCGContext *s)
{
    void *base = s->code_ptr;

    if (unlikely(((void *)s->code_ptr + sizeof(mod_4))
                 > s->code_gen_highwater)) {
        return -1;
    }

    memcpy(s->code_ptr, mod_4, sizeof(mod_4));
    s->code_ptr += sizeof(mod_4);

    int code_size = sub_buf_len() + 5;
    fill_uint32_leb128(base + 1, code_size + 6);
    fill_uint32_leb128(base + 7, code_size);

    return 0;
}

static int write_mod_code(TCGContext *s)
{
    void *base = s->code_ptr;
    int code_size = sub_buf_len();

    if (unlikely(((void *)s->code_ptr + code_size) > s->code_gen_highwater)) {
        return -1;
    }
    linked_buf_write(sub_buf_root, s->code_ptr);
    s->code_ptr += code_size;
    for (BlockPlaceholder *p = block_placeholder; p; p = p->next) {
        uint8_t *ph = p->pos + base;
        int blk = get_block_of_label(p->label);
        tcg_debug_assert(blk >= 0);
        fill_uint32_leb128(ph, blk);
    }

    return 0;
}

static void tcg_out_tb_start(TCGContext *s)
{
    int size;
    struct wasmTBHeader *h;

    init_sub_buf();
    init_blocks();
    init_label_info();
    init_helpers();
    init_types_buf();

    /* TB starts from a header */
    h = (struct wasmTBHeader *)(s->code_ptr);
    s->code_ptr += sizeof(struct wasmTBHeader);

    /* locate counters */
    h->counter_ptr = (int32_t *)s->code_ptr;
    size = tcg_max_ctxs * sizeof(int32_t);
    memset(s->code_ptr, 0, size);
    s->code_ptr += size;

    /* locate instance information */
    h->info_ptr = (struct wasmInstanceInfo **)s->code_ptr;
    size = tcg_max_ctxs * sizeof(void *);
    memset(s->code_ptr, 0, size);
    s->code_ptr += size;

    /* Followed by TCI code */
    h->tci_ptr = s->code_ptr;

    /* generate wasm code to initialize fundamental registers */
    tcg_wasm_out_ctx_i32_load(s, do_init);
    tcg_wasm_out_op_i32_const(s, 0);
    tcg_wasm_out_op_i32_ne(s);
    tcg_wasm_out_op_if_noret(s);

    tcg_wasm_out_op_global_get_r(s, TCG_AREG0);
    tcg_wasm_out_op_i64_eqz(s);
    tcg_wasm_out_op_if_noret(s);

    tcg_wasm_out_ctx_ptr_load(s, env);
    tcg_wasm_out_op_global_set_r(s, TCG_AREG0);

    tcg_wasm_out_ctx_ptr_load(s, stack);
    tcg_wasm_out_op_global_set_r(s, TCG_REG_CALL_STACK);
    tcg_wasm_out_op_end(s);

    tcg_wasm_out_ctx_i32_store_const(s, do_init, 0);
    tcg_wasm_out_op_i64_const(s, 0);
    tcg_wasm_out_op_global_set(s, BLOCK_PTR_IDX);
    tcg_wasm_out_op_end(s);

    tcg_wasm_out_op_loop_noret(s);
    tcg_wasm_out_op_global_get(s, BLOCK_PTR_IDX);
    tcg_wasm_out_op_i64_eqz(s);
    tcg_wasm_out_op_if_noret(s);
}

static int tcg_out_tb_end(TCGContext *s)
{
    int res;
    struct wasmTBHeader *h = (struct wasmTBHeader *)(s->code_buf);

    tcg_wasm_out_op_end(s); /* end if */
    tcg_wasm_out_op_end(s); /* end loop */
    tcg_wasm_out8(s, 0x0);  /* unreachable */
    tcg_wasm_out_op_end(s); /* end func */

    /* write wasm blob */
    h->wasm_ptr = s->code_ptr;
    res = write_mod_1(s);
    if (res < 0) {
        return res;
    }
    res = write_mod_2(s);
    if (res < 0) {
        return res;
    }
    res = write_mod_3(s);
    if (res < 0) {
        return res;
    }
    res = write_mod_4(s);
    if (res < 0) {
        return res;
    }
    res = write_mod_code(s);
    if (res < 0) {
        return res;
    }
    h->wasm_size = (int)s->code_ptr - (int)h->wasm_ptr;

    /* record imported helper functions */
    if (unlikely(((void *)s->code_ptr + helpers_len() * 4)
                 > s->code_gen_highwater)) {
        return -1;
    }
    h->import_ptr = s->code_ptr;
    s->code_ptr += helpers_copy((uintptr_t *)s->code_ptr);
    h->import_size = (int)s->code_ptr - (int)h->import_ptr;

    return 0;
}

bool tcg_target_has_memory_bswap(MemOp memop)
{
    return false;
}

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    g_assert_not_reached();
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    g_assert_not_reached();
}
