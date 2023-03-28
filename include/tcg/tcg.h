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

#ifndef TCG_H
#define TCG_H

#include "cpu.h"
#include "exec/memop.h"
#include "exec/memopidx.h"
#include "qemu/bitops.h"
#include "qemu/plugin.h"
#include "qemu/queue.h"
#include "tcg/tcg-mo.h"
#include "tcg-target.h"
#include "tcg/tcg-cond.h"
#include "tcg/debug-assert.h"

/* XXX: make safe guess about sizes */
#define MAX_OP_PER_INSTR 266

#define MAX_CALL_IARGS  7

#define CPU_TEMP_BUF_NLONGS 128
#define TCG_STATIC_FRAME_SIZE  (CPU_TEMP_BUF_NLONGS * sizeof(long))

/* Default target word size to pointer size.  */
#ifndef TCG_TARGET_REG_BITS
# if UINTPTR_MAX == UINT32_MAX
#  define TCG_TARGET_REG_BITS 32
# elif UINTPTR_MAX == UINT64_MAX
#  define TCG_TARGET_REG_BITS 64
# else
#  error Unknown pointer size for tcg target
# endif
#endif

#if TCG_TARGET_REG_BITS == 32
typedef int32_t tcg_target_long;
typedef uint32_t tcg_target_ulong;
#define TCG_PRIlx PRIx32
#define TCG_PRIld PRId32
#elif TCG_TARGET_REG_BITS == 64
typedef int64_t tcg_target_long;
typedef uint64_t tcg_target_ulong;
#define TCG_PRIlx PRIx64
#define TCG_PRIld PRId64
#else
#error unsupported
#endif

/* Oversized TCG guests make things like MTTCG hard
 * as we can't use atomics for cputlb updates.
 */
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
#define TCG_OVERSIZED_GUEST 1
#else
#define TCG_OVERSIZED_GUEST 0
#endif

#if TCG_TARGET_NB_REGS <= 32
typedef uint32_t TCGRegSet;
#elif TCG_TARGET_NB_REGS <= 64
typedef uint64_t TCGRegSet;
#else
#error unsupported
#endif

#if TCG_TARGET_REG_BITS == 32
/* Turn some undef macros into false macros.  */
#define TCG_TARGET_HAS_extrl_i64_i32    0
#define TCG_TARGET_HAS_extrh_i64_i32    0
#define TCG_TARGET_HAS_div_i64          0
#define TCG_TARGET_HAS_rem_i64          0
#define TCG_TARGET_HAS_div2_i64         0
#define TCG_TARGET_HAS_rot_i64          0
#define TCG_TARGET_HAS_ext8s_i64        0
#define TCG_TARGET_HAS_ext16s_i64       0
#define TCG_TARGET_HAS_ext32s_i64       0
#define TCG_TARGET_HAS_ext8u_i64        0
#define TCG_TARGET_HAS_ext16u_i64       0
#define TCG_TARGET_HAS_ext32u_i64       0
#define TCG_TARGET_HAS_bswap16_i64      0
#define TCG_TARGET_HAS_bswap32_i64      0
#define TCG_TARGET_HAS_bswap64_i64      0
#define TCG_TARGET_HAS_neg_i64          0
#define TCG_TARGET_HAS_not_i64          0
#define TCG_TARGET_HAS_andc_i64         0
#define TCG_TARGET_HAS_orc_i64          0
#define TCG_TARGET_HAS_eqv_i64          0
#define TCG_TARGET_HAS_nand_i64         0
#define TCG_TARGET_HAS_nor_i64          0
#define TCG_TARGET_HAS_clz_i64          0
#define TCG_TARGET_HAS_ctz_i64          0
#define TCG_TARGET_HAS_ctpop_i64        0
#define TCG_TARGET_HAS_deposit_i64      0
#define TCG_TARGET_HAS_extract_i64      0
#define TCG_TARGET_HAS_sextract_i64     0
#define TCG_TARGET_HAS_extract2_i64     0
#define TCG_TARGET_HAS_movcond_i64      0
#define TCG_TARGET_HAS_add2_i64         0
#define TCG_TARGET_HAS_sub2_i64         0
#define TCG_TARGET_HAS_mulu2_i64        0
#define TCG_TARGET_HAS_muls2_i64        0
#define TCG_TARGET_HAS_muluh_i64        0
#define TCG_TARGET_HAS_mulsh_i64        0
/* Turn some undef macros into true macros.  */
#define TCG_TARGET_HAS_add2_i32         1
#define TCG_TARGET_HAS_sub2_i32         1
#endif

#ifndef TCG_TARGET_deposit_i32_valid
#define TCG_TARGET_deposit_i32_valid(ofs, len) 1
#endif
#ifndef TCG_TARGET_deposit_i64_valid
#define TCG_TARGET_deposit_i64_valid(ofs, len) 1
#endif
#ifndef TCG_TARGET_extract_i32_valid
#define TCG_TARGET_extract_i32_valid(ofs, len) 1
#endif
#ifndef TCG_TARGET_extract_i64_valid
#define TCG_TARGET_extract_i64_valid(ofs, len) 1
#endif

/* Only one of DIV or DIV2 should be defined.  */
#if defined(TCG_TARGET_HAS_div_i32)
#define TCG_TARGET_HAS_div2_i32         0
#elif defined(TCG_TARGET_HAS_div2_i32)
#define TCG_TARGET_HAS_div_i32          0
#define TCG_TARGET_HAS_rem_i32          0
#endif
#if defined(TCG_TARGET_HAS_div_i64)
#define TCG_TARGET_HAS_div2_i64         0
#elif defined(TCG_TARGET_HAS_div2_i64)
#define TCG_TARGET_HAS_div_i64          0
#define TCG_TARGET_HAS_rem_i64          0
#endif

#if !defined(TCG_TARGET_HAS_v64) \
    && !defined(TCG_TARGET_HAS_v128) \
    && !defined(TCG_TARGET_HAS_v256)
#define TCG_TARGET_MAYBE_vec            0
#define TCG_TARGET_HAS_abs_vec          0
#define TCG_TARGET_HAS_neg_vec          0
#define TCG_TARGET_HAS_not_vec          0
#define TCG_TARGET_HAS_andc_vec         0
#define TCG_TARGET_HAS_orc_vec          0
#define TCG_TARGET_HAS_nand_vec         0
#define TCG_TARGET_HAS_nor_vec          0
#define TCG_TARGET_HAS_eqv_vec          0
#define TCG_TARGET_HAS_roti_vec         0
#define TCG_TARGET_HAS_rots_vec         0
#define TCG_TARGET_HAS_rotv_vec         0
#define TCG_TARGET_HAS_shi_vec          0
#define TCG_TARGET_HAS_shs_vec          0
#define TCG_TARGET_HAS_shv_vec          0
#define TCG_TARGET_HAS_mul_vec          0
#define TCG_TARGET_HAS_sat_vec          0
#define TCG_TARGET_HAS_minmax_vec       0
#define TCG_TARGET_HAS_bitsel_vec       0
#define TCG_TARGET_HAS_cmpsel_vec       0
#else
#define TCG_TARGET_MAYBE_vec            1
#endif
#ifndef TCG_TARGET_HAS_v64
#define TCG_TARGET_HAS_v64              0
#endif
#ifndef TCG_TARGET_HAS_v128
#define TCG_TARGET_HAS_v128             0
#endif
#ifndef TCG_TARGET_HAS_v256
#define TCG_TARGET_HAS_v256             0
#endif

#ifndef TARGET_INSN_START_EXTRA_WORDS
# define TARGET_INSN_START_WORDS 1
#else
# define TARGET_INSN_START_WORDS (1 + TARGET_INSN_START_EXTRA_WORDS)
#endif

typedef enum TCGOpcode {
#define DEF(name, oargs, iargs, cargs, flags) INDEX_op_ ## name,
#include "tcg/tcg-opc.h"
#undef DEF
    NB_OPS,
} TCGOpcode;

#define tcg_regset_set_reg(d, r)   ((d) |= (TCGRegSet)1 << (r))
#define tcg_regset_reset_reg(d, r) ((d) &= ~((TCGRegSet)1 << (r)))
#define tcg_regset_test_reg(d, r)  (((d) >> (r)) & 1)

#ifndef TCG_TARGET_INSN_UNIT_SIZE
# error "Missing TCG_TARGET_INSN_UNIT_SIZE"
#elif TCG_TARGET_INSN_UNIT_SIZE == 1
typedef uint8_t tcg_insn_unit;
#elif TCG_TARGET_INSN_UNIT_SIZE == 2
typedef uint16_t tcg_insn_unit;
#elif TCG_TARGET_INSN_UNIT_SIZE == 4
typedef uint32_t tcg_insn_unit;
#elif TCG_TARGET_INSN_UNIT_SIZE == 8
typedef uint64_t tcg_insn_unit;
#else
/* The port better have done this.  */
#endif

typedef struct TCGRelocation TCGRelocation;
struct TCGRelocation {
    QSIMPLEQ_ENTRY(TCGRelocation) next;
    tcg_insn_unit *ptr;
    intptr_t addend;
    int type;
};

typedef struct TCGOp TCGOp;
typedef struct TCGLabelUse TCGLabelUse;
struct TCGLabelUse {
    QSIMPLEQ_ENTRY(TCGLabelUse) next;
    TCGOp *op;
};

typedef struct TCGLabel TCGLabel;
struct TCGLabel {
    bool present;
    bool has_value;
    uint16_t id;
    union {
        uintptr_t value;
        const tcg_insn_unit *value_ptr;
    } u;
    QSIMPLEQ_HEAD(, TCGLabelUse) branches;
    QSIMPLEQ_HEAD(, TCGRelocation) relocs;
    QSIMPLEQ_ENTRY(TCGLabel) next;
};

typedef struct TCGPool {
    struct TCGPool *next;
    int size;
    uint8_t data[] __attribute__ ((aligned));
} TCGPool;

#define TCG_POOL_CHUNK_SIZE 32768

#define TCG_MAX_TEMPS 512
#define TCG_MAX_INSNS 512

/* when the size of the arguments of a called function is smaller than
   this value, they are statically allocated in the TB stack frame */
#define TCG_STATIC_CALL_ARGS_SIZE 128

typedef enum TCGType {
    TCG_TYPE_I32,
    TCG_TYPE_I64,
    TCG_TYPE_I128,

    TCG_TYPE_V64,
    TCG_TYPE_V128,
    TCG_TYPE_V256,

    /* Number of different types (integer not enum) */
#define TCG_TYPE_COUNT  (TCG_TYPE_V256 + 1)

    /* An alias for the size of the host register.  */
#if TCG_TARGET_REG_BITS == 32
    TCG_TYPE_REG = TCG_TYPE_I32,
#else
    TCG_TYPE_REG = TCG_TYPE_I64,
#endif

    /* An alias for the size of the native pointer.  */
#if UINTPTR_MAX == UINT32_MAX
    TCG_TYPE_PTR = TCG_TYPE_I32,
#else
    TCG_TYPE_PTR = TCG_TYPE_I64,
#endif

    /* An alias for the size of the target "long", aka register.  */
#if TARGET_LONG_BITS == 64
    TCG_TYPE_TL = TCG_TYPE_I64,
#else
    TCG_TYPE_TL = TCG_TYPE_I32,
#endif
} TCGType;

/**
 * tcg_type_size
 * @t: type
 *
 * Return the size of the type in bytes.
 */
static inline int tcg_type_size(TCGType t)
{
    unsigned i = t;
    if (i >= TCG_TYPE_V64) {
        tcg_debug_assert(i < TCG_TYPE_COUNT);
        i -= TCG_TYPE_V64 - 1;
    }
    return 4 << i;
}

/**
 * get_alignment_bits
 * @memop: MemOp value
 *
 * Extract the alignment size from the memop.
 */
static inline unsigned get_alignment_bits(MemOp memop)
{
    unsigned a = memop & MO_AMASK;

    if (a == MO_UNALN) {
        /* No alignment required.  */
        a = 0;
    } else if (a == MO_ALIGN) {
        /* A natural alignment requirement.  */
        a = memop & MO_SIZE;
    } else {
        /* A specific alignment requirement.  */
        a = a >> MO_ASHIFT;
    }
#if defined(CONFIG_SOFTMMU)
    /* The requested alignment cannot overlap the TLB flags.  */
    tcg_debug_assert((TLB_FLAGS_MASK & ((1 << a) - 1)) == 0);
#endif
    return a;
}

typedef tcg_target_ulong TCGArg;

/* Define type and accessor macros for TCG variables.

   TCG variables are the inputs and outputs of TCG ops, as described
   in tcg/README. Target CPU front-end code uses these types to deal
   with TCG variables as it emits TCG code via the tcg_gen_* functions.
   They come in several flavours:
    * TCGv_i32  : 32 bit integer type
    * TCGv_i64  : 64 bit integer type
    * TCGv_i128 : 128 bit integer type
    * TCGv_ptr  : a host pointer type
    * TCGv_vec  : a host vector type; the exact size is not exposed
                  to the CPU front-end code.
    * TCGv      : an integer type the same size as target_ulong
                  (an alias for either TCGv_i32 or TCGv_i64)
   The compiler's type checking will complain if you mix them
   up and pass the wrong sized TCGv to a function.

   Users of tcg_gen_* don't need to know about any of the internal
   details of these, and should treat them as opaque types.
   You won't be able to look inside them in a debugger either.

   Internal implementation details follow:

   Note that there is no definition of the structs TCGv_i32_d etc anywhere.
   This is deliberate, because the values we store in variables of type
   TCGv_i32 are not really pointers-to-structures. They're just small
   integers, but keeping them in pointer types like this means that the
   compiler will complain if you accidentally pass a TCGv_i32 to a
   function which takes a TCGv_i64, and so on. Only the internals of
   TCG need to care about the actual contents of the types.  */

typedef struct TCGv_i32_d *TCGv_i32;
typedef struct TCGv_i64_d *TCGv_i64;
typedef struct TCGv_i128_d *TCGv_i128;
typedef struct TCGv_ptr_d *TCGv_ptr;
typedef struct TCGv_vec_d *TCGv_vec;
typedef TCGv_ptr TCGv_env;
#if TARGET_LONG_BITS == 32
#define TCGv TCGv_i32
#elif TARGET_LONG_BITS == 64
#define TCGv TCGv_i64
#else
#error Unhandled TARGET_LONG_BITS value
#endif

/* call flags */
/* Helper does not read globals (either directly or through an exception). It
   implies TCG_CALL_NO_WRITE_GLOBALS. */
#define TCG_CALL_NO_READ_GLOBALS    0x0001
/* Helper does not write globals */
#define TCG_CALL_NO_WRITE_GLOBALS   0x0002
/* Helper can be safely suppressed if the return value is not used. */
#define TCG_CALL_NO_SIDE_EFFECTS    0x0004
/* Helper is G_NORETURN.  */
#define TCG_CALL_NO_RETURN          0x0008
/* Helper is part of Plugins.  */
#define TCG_CALL_PLUGIN             0x0010

/* convenience version of most used call flags */
#define TCG_CALL_NO_RWG         TCG_CALL_NO_READ_GLOBALS
#define TCG_CALL_NO_WG          TCG_CALL_NO_WRITE_GLOBALS
#define TCG_CALL_NO_SE          TCG_CALL_NO_SIDE_EFFECTS
#define TCG_CALL_NO_RWG_SE      (TCG_CALL_NO_RWG | TCG_CALL_NO_SE)
#define TCG_CALL_NO_WG_SE       (TCG_CALL_NO_WG | TCG_CALL_NO_SE)

/*
 * Flags for the bswap opcodes.
 * If IZ, the input is zero-extended, otherwise unknown.
 * If OZ or OS, the output is zero- or sign-extended respectively,
 * otherwise the high bits are undefined.
 */
enum {
    TCG_BSWAP_IZ = 1,
    TCG_BSWAP_OZ = 2,
    TCG_BSWAP_OS = 4,
};

typedef enum TCGTempVal {
    TEMP_VAL_DEAD,
    TEMP_VAL_REG,
    TEMP_VAL_MEM,
    TEMP_VAL_CONST,
} TCGTempVal;

typedef enum TCGTempKind {
    /*
     * Temp is dead at the end of the extended basic block (EBB),
     * the single-entry multiple-exit region that falls through
     * conditional branches.
     */
    TEMP_EBB,
    /* Temp is live across the entire translation block, but dead at end. */
    TEMP_TB,
    /* Temp is live across the entire translation block, and between them. */
    TEMP_GLOBAL,
    /* Temp is in a fixed register. */
    TEMP_FIXED,
    /* Temp is a fixed constant. */
    TEMP_CONST,
} TCGTempKind;

typedef struct TCGTemp {
    TCGReg reg:8;
    TCGTempVal val_type:8;
    TCGType base_type:8;
    TCGType type:8;
    TCGTempKind kind:3;
    unsigned int indirect_reg:1;
    unsigned int indirect_base:1;
    unsigned int mem_coherent:1;
    unsigned int mem_allocated:1;
    unsigned int temp_allocated:1;
    unsigned int temp_subindex:1;

    int64_t val;
    struct TCGTemp *mem_base;
    intptr_t mem_offset;
    const char *name;

    /* Pass-specific information that can be stored for a temporary.
       One word worth of integer data, and one pointer to data
       allocated separately.  */
    uintptr_t state;
    void *state_ptr;
} TCGTemp;

typedef struct TCGContext TCGContext;

typedef struct TCGTempSet {
    unsigned long l[BITS_TO_LONGS(TCG_MAX_TEMPS)];
} TCGTempSet;

/*
 * With 1 128-bit output, a 32-bit host requires 4 output parameters,
 * which leaves a maximum of 28 other slots.  Which is enough for 7
 * 128-bit operands.
 */
#define DEAD_ARG  (1 << 4)
#define SYNC_ARG  (1 << 0)
typedef uint32_t TCGLifeData;

struct TCGOp {
    TCGOpcode opc   : 8;
    unsigned nargs  : 8;

    /* Parameters for this opcode.  See below.  */
    unsigned param1 : 8;
    unsigned param2 : 8;

    /* Lifetime data of the operands.  */
    TCGLifeData life;

    /* Next and previous opcodes.  */
    QTAILQ_ENTRY(TCGOp) link;

    /* Register preferences for the output(s).  */
    TCGRegSet output_pref[2];

    /* Arguments for the opcode.  */
    TCGArg args[];
};

#define TCGOP_CALLI(X)    (X)->param1
#define TCGOP_CALLO(X)    (X)->param2

#define TCGOP_VECL(X)     (X)->param1
#define TCGOP_VECE(X)     (X)->param2

/* Make sure operands fit in the bitfields above.  */
QEMU_BUILD_BUG_ON(NB_OPS > (1 << 8));

static inline TCGRegSet output_pref(const TCGOp *op, unsigned i)
{
    return i < ARRAY_SIZE(op->output_pref) ? op->output_pref[i] : 0;
}

typedef struct TCGProfile {
    int64_t cpu_exec_time;
    int64_t tb_count1;
    int64_t tb_count;
    int64_t op_count; /* total insn count */
    int op_count_max; /* max insn per TB */
    int temp_count_max;
    int64_t temp_count;
    int64_t del_op_count;
    int64_t code_in_len;
    int64_t code_out_len;
    int64_t search_out_len;
    int64_t interm_time;
    int64_t code_time;
    int64_t la_time;
    int64_t opt_time;
    int64_t restore_count;
    int64_t restore_time;
    int64_t table_op_count[NB_OPS];
} TCGProfile;

struct TCGContext {
    uint8_t *pool_cur, *pool_end;
    TCGPool *pool_first, *pool_current, *pool_first_large;
    int nb_labels;
    int nb_globals;
    int nb_temps;
    int nb_indirects;
    int nb_ops;
    TCGType addr_type;            /* TCG_TYPE_I32 or TCG_TYPE_I64 */

#ifdef CONFIG_SOFTMMU
    int page_mask;
    uint8_t page_bits;
    uint8_t tlb_dyn_max_bits;
#endif

    TCGRegSet reserved_regs;
    intptr_t current_frame_offset;
    intptr_t frame_start;
    intptr_t frame_end;
    TCGTemp *frame_temp;

    TranslationBlock *gen_tb;     /* tb for which code is being generated */
    tcg_insn_unit *code_buf;      /* pointer for start of tb */
    tcg_insn_unit *code_ptr;      /* pointer for running end of tb */

#ifdef CONFIG_PROFILER
    TCGProfile prof;
#endif

#ifdef CONFIG_DEBUG_TCG
    int goto_tb_issue_mask;
    const TCGOpcode *vecop_list;
#endif

    /* Code generation.  Note that we specifically do not use tcg_insn_unit
       here, because there's too much arithmetic throughout that relies
       on addition and subtraction working on bytes.  Rely on the GCC
       extension that allows arithmetic on void*.  */
    void *code_gen_buffer;
    size_t code_gen_buffer_size;
    void *code_gen_ptr;
    void *data_gen_ptr;

    /* Threshold to flush the translated code buffer.  */
    void *code_gen_highwater;

    /* Track which vCPU triggers events */
    CPUState *cpu;                      /* *_trans */

    /* These structures are private to tcg-target.c.inc.  */
#ifdef TCG_TARGET_NEED_LDST_LABELS
    QSIMPLEQ_HEAD(, TCGLabelQemuLdst) ldst_labels;
#endif
#ifdef TCG_TARGET_NEED_POOL_LABELS
    struct TCGLabelPoolData *pool_labels;
#endif

    TCGLabel *exitreq_label;

#ifdef CONFIG_PLUGIN
    /*
     * We keep one plugin_tb struct per TCGContext. Note that on every TB
     * translation we clear but do not free its contents; this way we
     * avoid a lot of malloc/free churn, since after a few TB's it's
     * unlikely that we'll need to allocate either more instructions or more
     * space for instructions (for variable-instruction-length ISAs).
     */
    struct qemu_plugin_tb *plugin_tb;

    /* descriptor of the instruction being translated */
    struct qemu_plugin_insn *plugin_insn;
#endif

    GHashTable *const_table[TCG_TYPE_COUNT];
    TCGTempSet free_temps[TCG_TYPE_COUNT];
    TCGTemp temps[TCG_MAX_TEMPS]; /* globals first, temps after */

    QTAILQ_HEAD(, TCGOp) ops, free_ops;
    QSIMPLEQ_HEAD(, TCGLabel) labels;

    /* Tells which temporary holds a given register.
       It does not take into account fixed registers */
    TCGTemp *reg_to_temp[TCG_TARGET_NB_REGS];

    uint16_t gen_insn_end_off[TCG_MAX_INSNS];
    uint64_t gen_insn_data[TCG_MAX_INSNS][TARGET_INSN_START_WORDS];

    /* Exit to translator on overflow. */
    sigjmp_buf jmp_trans;
};

static inline bool temp_readonly(TCGTemp *ts)
{
    return ts->kind >= TEMP_FIXED;
}

extern __thread TCGContext *tcg_ctx;
extern const void *tcg_code_gen_epilogue;
extern uintptr_t tcg_splitwx_diff;
extern TCGv_env cpu_env;

bool in_code_gen_buffer(const void *p);

#ifdef CONFIG_DEBUG_TCG
const void *tcg_splitwx_to_rx(void *rw);
void *tcg_splitwx_to_rw(const void *rx);
#else
static inline const void *tcg_splitwx_to_rx(void *rw)
{
    return rw ? rw + tcg_splitwx_diff : NULL;
}

static inline void *tcg_splitwx_to_rw(const void *rx)
{
    return rx ? (void *)rx - tcg_splitwx_diff : NULL;
}
#endif

static inline size_t temp_idx(TCGTemp *ts)
{
    ptrdiff_t n = ts - tcg_ctx->temps;
    tcg_debug_assert(n >= 0 && n < tcg_ctx->nb_temps);
    return n;
}

static inline TCGArg temp_arg(TCGTemp *ts)
{
    return (uintptr_t)ts;
}

static inline TCGTemp *arg_temp(TCGArg a)
{
    return (TCGTemp *)(uintptr_t)a;
}

/* Using the offset of a temporary, relative to TCGContext, rather than
   its index means that we don't use 0.  That leaves offset 0 free for
   a NULL representation without having to leave index 0 unused.  */
static inline TCGTemp *tcgv_i32_temp(TCGv_i32 v)
{
    uintptr_t o = (uintptr_t)v;
    TCGTemp *t = (void *)tcg_ctx + o;
    tcg_debug_assert(offsetof(TCGContext, temps[temp_idx(t)]) == o);
    return t;
}

static inline TCGTemp *tcgv_i64_temp(TCGv_i64 v)
{
    return tcgv_i32_temp((TCGv_i32)v);
}

static inline TCGTemp *tcgv_i128_temp(TCGv_i128 v)
{
    return tcgv_i32_temp((TCGv_i32)v);
}

static inline TCGTemp *tcgv_ptr_temp(TCGv_ptr v)
{
    return tcgv_i32_temp((TCGv_i32)v);
}

static inline TCGTemp *tcgv_vec_temp(TCGv_vec v)
{
    return tcgv_i32_temp((TCGv_i32)v);
}

static inline TCGArg tcgv_i32_arg(TCGv_i32 v)
{
    return temp_arg(tcgv_i32_temp(v));
}

static inline TCGArg tcgv_i64_arg(TCGv_i64 v)
{
    return temp_arg(tcgv_i64_temp(v));
}

static inline TCGArg tcgv_i128_arg(TCGv_i128 v)
{
    return temp_arg(tcgv_i128_temp(v));
}

static inline TCGArg tcgv_ptr_arg(TCGv_ptr v)
{
    return temp_arg(tcgv_ptr_temp(v));
}

static inline TCGArg tcgv_vec_arg(TCGv_vec v)
{
    return temp_arg(tcgv_vec_temp(v));
}

static inline TCGv_i32 temp_tcgv_i32(TCGTemp *t)
{
    (void)temp_idx(t); /* trigger embedded assert */
    return (TCGv_i32)((void *)t - (void *)tcg_ctx);
}

static inline TCGv_i64 temp_tcgv_i64(TCGTemp *t)
{
    return (TCGv_i64)temp_tcgv_i32(t);
}

static inline TCGv_i128 temp_tcgv_i128(TCGTemp *t)
{
    return (TCGv_i128)temp_tcgv_i32(t);
}

static inline TCGv_ptr temp_tcgv_ptr(TCGTemp *t)
{
    return (TCGv_ptr)temp_tcgv_i32(t);
}

static inline TCGv_vec temp_tcgv_vec(TCGTemp *t)
{
    return (TCGv_vec)temp_tcgv_i32(t);
}

static inline TCGArg tcg_get_insn_param(TCGOp *op, int arg)
{
    return op->args[arg];
}

static inline void tcg_set_insn_param(TCGOp *op, int arg, TCGArg v)
{
    op->args[arg] = v;
}

static inline uint64_t tcg_get_insn_start_param(TCGOp *op, int arg)
{
    if (TCG_TARGET_REG_BITS == 64) {
        return tcg_get_insn_param(op, arg);
    } else {
        return deposit64(tcg_get_insn_param(op, arg * 2), 32, 32,
                         tcg_get_insn_param(op, arg * 2 + 1));
    }
}

static inline void tcg_set_insn_start_param(TCGOp *op, int arg, uint64_t v)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_set_insn_param(op, arg, v);
    } else {
        tcg_set_insn_param(op, arg * 2, v);
        tcg_set_insn_param(op, arg * 2 + 1, v >> 32);
    }
}

/* The last op that was emitted.  */
static inline TCGOp *tcg_last_op(void)
{
    return QTAILQ_LAST(&tcg_ctx->ops);
}

/* Test for whether to terminate the TB for using too many opcodes.  */
static inline bool tcg_op_buf_full(void)
{
    /* This is not a hard limit, it merely stops translation when
     * we have produced "enough" opcodes.  We want to limit TB size
     * such that a RISC host can reasonably use a 16-bit signed
     * branch within the TB.  We also need to be mindful of the
     * 16-bit unsigned offsets, TranslationBlock.jmp_reset_offset[]
     * and TCGContext.gen_insn_end_off[].
     */
    return tcg_ctx->nb_ops >= 4000;
}

/* pool based memory allocation */

/* user-mode: mmap_lock must be held for tcg_malloc_internal. */
void *tcg_malloc_internal(TCGContext *s, int size);
void tcg_pool_reset(TCGContext *s);
TranslationBlock *tcg_tb_alloc(TCGContext *s);

void tcg_region_reset_all(void);

size_t tcg_code_size(void);
size_t tcg_code_capacity(void);

void tcg_tb_insert(TranslationBlock *tb);
void tcg_tb_remove(TranslationBlock *tb);
TranslationBlock *tcg_tb_lookup(uintptr_t tc_ptr);
void tcg_tb_foreach(GTraverseFunc func, gpointer user_data);
size_t tcg_nb_tbs(void);

/* user-mode: Called with mmap_lock held.  */
static inline void *tcg_malloc(int size)
{
    TCGContext *s = tcg_ctx;
    uint8_t *ptr, *ptr_end;

    /* ??? This is a weak placeholder for minimum malloc alignment.  */
    size = QEMU_ALIGN_UP(size, 8);

    ptr = s->pool_cur;
    ptr_end = ptr + size;
    if (unlikely(ptr_end > s->pool_end)) {
        return tcg_malloc_internal(tcg_ctx, size);
    } else {
        s->pool_cur = ptr_end;
        return ptr;
    }
}

void tcg_init(size_t tb_size, int splitwx, unsigned max_cpus);
void tcg_register_thread(void);
void tcg_prologue_init(TCGContext *s);
void tcg_func_start(TCGContext *s);

int tcg_gen_code(TCGContext *s, TranslationBlock *tb, uint64_t pc_start);

void tb_target_set_jmp_target(const TranslationBlock *, int,
                              uintptr_t, uintptr_t);

void tcg_set_frame(TCGContext *s, TCGReg reg, intptr_t start, intptr_t size);

TCGTemp *tcg_global_mem_new_internal(TCGType, TCGv_ptr,
                                     intptr_t, const char *);
TCGTemp *tcg_temp_new_internal(TCGType, TCGTempKind);
TCGv_vec tcg_temp_new_vec(TCGType type);
TCGv_vec tcg_temp_new_vec_matching(TCGv_vec match);

static inline TCGv_i32 tcg_global_mem_new_i32(TCGv_ptr reg, intptr_t offset,
                                              const char *name)
{
    TCGTemp *t = tcg_global_mem_new_internal(TCG_TYPE_I32, reg, offset, name);
    return temp_tcgv_i32(t);
}

static inline TCGv_i32 tcg_temp_new_i32(void)
{
    TCGTemp *t = tcg_temp_new_internal(TCG_TYPE_I32, TEMP_TB);
    return temp_tcgv_i32(t);
}

static inline TCGv_i64 tcg_global_mem_new_i64(TCGv_ptr reg, intptr_t offset,
                                              const char *name)
{
    TCGTemp *t = tcg_global_mem_new_internal(TCG_TYPE_I64, reg, offset, name);
    return temp_tcgv_i64(t);
}

static inline TCGv_i64 tcg_temp_new_i64(void)
{
    TCGTemp *t = tcg_temp_new_internal(TCG_TYPE_I64, TEMP_TB);
    return temp_tcgv_i64(t);
}

static inline TCGv_i128 tcg_temp_new_i128(void)
{
    TCGTemp *t = tcg_temp_new_internal(TCG_TYPE_I128, TEMP_TB);
    return temp_tcgv_i128(t);
}

static inline TCGv_ptr tcg_global_mem_new_ptr(TCGv_ptr reg, intptr_t offset,
                                              const char *name)
{
    TCGTemp *t = tcg_global_mem_new_internal(TCG_TYPE_PTR, reg, offset, name);
    return temp_tcgv_ptr(t);
}

static inline TCGv_ptr tcg_temp_new_ptr(void)
{
    TCGTemp *t = tcg_temp_new_internal(TCG_TYPE_PTR, TEMP_TB);
    return temp_tcgv_ptr(t);
}

int64_t tcg_cpu_exec_time(void);
void tcg_dump_info(GString *buf);
void tcg_dump_op_count(GString *buf);

#define TCG_CT_CONST  1 /* any constant of register size */

typedef struct TCGArgConstraint {
    unsigned ct : 16;
    unsigned alias_index : 4;
    unsigned sort_index : 4;
    unsigned pair_index : 4;
    unsigned pair : 2;  /* 0: none, 1: first, 2: second, 3: second alias */
    bool oalias : 1;
    bool ialias : 1;
    bool newreg : 1;
    TCGRegSet regs;
} TCGArgConstraint;

#define TCG_MAX_OP_ARGS 16

/* Bits for TCGOpDef->flags, 8 bits available, all used.  */
enum {
    /* Instruction exits the translation block.  */
    TCG_OPF_BB_EXIT      = 0x01,
    /* Instruction defines the end of a basic block.  */
    TCG_OPF_BB_END       = 0x02,
    /* Instruction clobbers call registers and potentially update globals.  */
    TCG_OPF_CALL_CLOBBER = 0x04,
    /* Instruction has side effects: it cannot be removed if its outputs
       are not used, and might trigger exceptions.  */
    TCG_OPF_SIDE_EFFECTS = 0x08,
    /* Instruction operands are 64-bits (otherwise 32-bits).  */
    TCG_OPF_64BIT        = 0x10,
    /* Instruction is optional and not implemented by the host, or insn
       is generic and should not be implemened by the host.  */
    TCG_OPF_NOT_PRESENT  = 0x20,
    /* Instruction operands are vectors.  */
    TCG_OPF_VECTOR       = 0x40,
    /* Instruction is a conditional branch. */
    TCG_OPF_COND_BRANCH  = 0x80
};

typedef struct TCGOpDef {
    const char *name;
    uint8_t nb_oargs, nb_iargs, nb_cargs, nb_args;
    uint8_t flags;
    TCGArgConstraint *args_ct;
} TCGOpDef;

extern TCGOpDef tcg_op_defs[];
extern const size_t tcg_op_defs_max;

typedef struct TCGTargetOpDef {
    TCGOpcode op;
    const char *args_ct_str[TCG_MAX_OP_ARGS];
} TCGTargetOpDef;

bool tcg_op_supported(TCGOpcode op);

void tcg_gen_callN(void *func, TCGTemp *ret, int nargs, TCGTemp **args);

TCGOp *tcg_emit_op(TCGOpcode opc, unsigned nargs);
void tcg_op_remove(TCGContext *s, TCGOp *op);
TCGOp *tcg_op_insert_before(TCGContext *s, TCGOp *op,
                            TCGOpcode opc, unsigned nargs);
TCGOp *tcg_op_insert_after(TCGContext *s, TCGOp *op,
                           TCGOpcode opc, unsigned nargs);

/**
 * tcg_remove_ops_after:
 * @op: target operation
 *
 * Discard any opcodes emitted since @op.  Expected usage is to save
 * a starting point with tcg_last_op(), speculatively emit opcodes,
 * then decide whether or not to keep those opcodes after the fact.
 */
void tcg_remove_ops_after(TCGOp *op);

void tcg_optimize(TCGContext *s);

/*
 * Locate or create a read-only temporary that is a constant.
 * This kind of temporary need not be freed, but for convenience
 * will be silently ignored by tcg_temp_free_*.
 */
TCGTemp *tcg_constant_internal(TCGType type, int64_t val);

static inline TCGv_i32 tcg_constant_i32(int32_t val)
{
    return temp_tcgv_i32(tcg_constant_internal(TCG_TYPE_I32, val));
}

static inline TCGv_i64 tcg_constant_i64(int64_t val)
{
    return temp_tcgv_i64(tcg_constant_internal(TCG_TYPE_I64, val));
}

TCGv_vec tcg_constant_vec(TCGType type, unsigned vece, int64_t val);
TCGv_vec tcg_constant_vec_matching(TCGv_vec match, unsigned vece, int64_t val);

#if UINTPTR_MAX == UINT32_MAX
# define tcg_constant_ptr(x)     ((TCGv_ptr)tcg_constant_i32((intptr_t)(x)))
#else
# define tcg_constant_ptr(x)     ((TCGv_ptr)tcg_constant_i64((intptr_t)(x)))
#endif

TCGLabel *gen_new_label(void);

/**
 * label_arg
 * @l: label
 *
 * Encode a label for storage in the TCG opcode stream.
 */

static inline TCGArg label_arg(TCGLabel *l)
{
    return (uintptr_t)l;
}

/**
 * arg_label
 * @i: value
 *
 * The opposite of label_arg.  Retrieve a label from the
 * encoding of the TCG opcode stream.
 */

static inline TCGLabel *arg_label(TCGArg i)
{
    return (TCGLabel *)(uintptr_t)i;
}

/**
 * tcg_ptr_byte_diff
 * @a, @b: addresses to be differenced
 *
 * There are many places within the TCG backends where we need a byte
 * difference between two pointers.  While this can be accomplished
 * with local casting, it's easy to get wrong -- especially if one is
 * concerned with the signedness of the result.
 *
 * This version relies on GCC's void pointer arithmetic to get the
 * correct result.
 */

static inline ptrdiff_t tcg_ptr_byte_diff(const void *a, const void *b)
{
    return a - b;
}

/**
 * tcg_pcrel_diff
 * @s: the tcg context
 * @target: address of the target
 *
 * Produce a pc-relative difference, from the current code_ptr
 * to the destination address.
 */

static inline ptrdiff_t tcg_pcrel_diff(TCGContext *s, const void *target)
{
    return tcg_ptr_byte_diff(target, tcg_splitwx_to_rx(s->code_ptr));
}

/**
 * tcg_tbrel_diff
 * @s: the tcg context
 * @target: address of the target
 *
 * Produce a difference, from the beginning of the current TB code
 * to the destination address.
 */
static inline ptrdiff_t tcg_tbrel_diff(TCGContext *s, const void *target)
{
    return tcg_ptr_byte_diff(target, tcg_splitwx_to_rx(s->code_buf));
}

/**
 * tcg_current_code_size
 * @s: the tcg context
 *
 * Compute the current code size within the translation block.
 * This is used to fill in qemu's data structures for goto_tb.
 */

static inline size_t tcg_current_code_size(TCGContext *s)
{
    return tcg_ptr_byte_diff(s->code_ptr, s->code_buf);
}

/**
 * tcg_qemu_tb_exec:
 * @env: pointer to CPUArchState for the CPU
 * @tb_ptr: address of generated code for the TB to execute
 *
 * Start executing code from a given translation block.
 * Where translation blocks have been linked, execution
 * may proceed from the given TB into successive ones.
 * Control eventually returns only when some action is needed
 * from the top-level loop: either control must pass to a TB
 * which has not yet been directly linked, or an asynchronous
 * event such as an interrupt needs handling.
 *
 * Return: The return value is the value passed to the corresponding
 * tcg_gen_exit_tb() at translation time of the last TB attempted to execute.
 * The value is either zero or a 4-byte aligned pointer to that TB combined
 * with additional information in its two least significant bits. The
 * additional information is encoded as follows:
 *  0, 1: the link between this TB and the next is via the specified
 *        TB index (0 or 1). That is, we left the TB via (the equivalent
 *        of) "goto_tb <index>". The main loop uses this to determine
 *        how to link the TB just executed to the next.
 *  2:    we are using instruction counting code generation, and we
 *        did not start executing this TB because the instruction counter
 *        would hit zero midway through it. In this case the pointer
 *        returned is the TB we were about to execute, and the caller must
 *        arrange to execute the remaining count of instructions.
 *  3:    we stopped because the CPU's exit_request flag was set
 *        (usually meaning that there is an interrupt that needs to be
 *        handled). The pointer returned is the TB we were about to execute
 *        when we noticed the pending exit request.
 *
 * If the bottom two bits indicate an exit-via-index then the CPU
 * state is correctly synchronised and ready for execution of the next
 * TB (and in particular the guest PC is the address to execute next).
 * Otherwise, we gave up on execution of this TB before it started, and
 * the caller must fix up the CPU state by calling the CPU's
 * synchronize_from_tb() method with the TB pointer we return (falling
 * back to calling the CPU's set_pc method with tb->pb if no
 * synchronize_from_tb() method exists).
 *
 * Note that TCG targets may use a different definition of tcg_qemu_tb_exec
 * to this default (which just calls the prologue.code emitted by
 * tcg_target_qemu_prologue()).
 */
#define TB_EXIT_MASK      3
#define TB_EXIT_IDX0      0
#define TB_EXIT_IDX1      1
#define TB_EXIT_IDXMAX    1
#define TB_EXIT_REQUESTED 3

#ifdef CONFIG_TCG_INTERPRETER
uintptr_t tcg_qemu_tb_exec(CPUArchState *env, const void *tb_ptr);
#else
typedef uintptr_t tcg_prologue_fn(CPUArchState *env, const void *tb_ptr);
extern tcg_prologue_fn *tcg_qemu_tb_exec;
#endif

void tcg_register_jit(const void *buf, size_t buf_size);

#if TCG_TARGET_MAYBE_vec
/* Return zero if the tuple (opc, type, vece) is unsupportable;
   return > 0 if it is directly supportable;
   return < 0 if we must call tcg_expand_vec_op.  */
int tcg_can_emit_vec_op(TCGOpcode, TCGType, unsigned);
#else
static inline int tcg_can_emit_vec_op(TCGOpcode o, TCGType t, unsigned ve)
{
    return 0;
}
#endif

/* Expand the tuple (opc, type, vece) on the given arguments.  */
void tcg_expand_vec_op(TCGOpcode, TCGType, unsigned, TCGArg, ...);

/* Replicate a constant C accoring to the log2 of the element size.  */
uint64_t dup_const(unsigned vece, uint64_t c);

#define dup_const(VECE, C)                                         \
    (__builtin_constant_p(VECE)                                    \
     ? (  (VECE) == MO_8  ? 0x0101010101010101ull * (uint8_t)(C)   \
        : (VECE) == MO_16 ? 0x0001000100010001ull * (uint16_t)(C)  \
        : (VECE) == MO_32 ? 0x0000000100000001ull * (uint32_t)(C)  \
        : (VECE) == MO_64 ? (uint64_t)(C)                          \
        : (qemu_build_not_reached_always(), 0))                    \
     : dup_const(VECE, C))

#if TARGET_LONG_BITS == 64
# define dup_const_tl  dup_const
#else
# define dup_const_tl(VECE, C)                                     \
    (__builtin_constant_p(VECE)                                    \
     ? (  (VECE) == MO_8  ? 0x01010101ul * (uint8_t)(C)            \
        : (VECE) == MO_16 ? 0x00010001ul * (uint16_t)(C)           \
        : (VECE) == MO_32 ? 0x00000001ul * (uint32_t)(C)           \
        : (qemu_build_not_reached_always(), 0))                    \
     :  (target_long)dup_const(VECE, C))
#endif

#ifdef CONFIG_DEBUG_TCG
void tcg_assert_listed_vecop(TCGOpcode);
#else
static inline void tcg_assert_listed_vecop(TCGOpcode op) { }
#endif

static inline const TCGOpcode *tcg_swap_vecop_list(const TCGOpcode *n)
{
#ifdef CONFIG_DEBUG_TCG
    const TCGOpcode *o = tcg_ctx->vecop_list;
    tcg_ctx->vecop_list = n;
    return o;
#else
    return NULL;
#endif
}

bool tcg_can_emit_vecop_list(const TCGOpcode *, TCGType, unsigned);

#endif /* TCG_H */
