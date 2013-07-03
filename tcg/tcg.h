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
#include "qemu-common.h"

/* Target word size (must be identical to pointer size). */
#if UINTPTR_MAX == UINT32_MAX
# define TCG_TARGET_REG_BITS 32
#elif UINTPTR_MAX == UINT64_MAX
# define TCG_TARGET_REG_BITS 64
#else
# error Unknown pointer size for tcg target
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

#include "tcg-target.h"
#include "tcg-runtime.h"

#if TCG_TARGET_NB_REGS <= 32
typedef uint32_t TCGRegSet;
#elif TCG_TARGET_NB_REGS <= 64
typedef uint64_t TCGRegSet;
#else
#error unsupported
#endif

#if TCG_TARGET_REG_BITS == 32
/* Turn some undef macros into false macros.  */
#define TCG_TARGET_HAS_div_i64          0
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
#define TCG_TARGET_HAS_deposit_i64      0
#define TCG_TARGET_HAS_movcond_i64      0
#define TCG_TARGET_HAS_add2_i64         0
#define TCG_TARGET_HAS_sub2_i64         0
#define TCG_TARGET_HAS_mulu2_i64        0
#define TCG_TARGET_HAS_muls2_i64        0
/* Turn some undef macros into true macros.  */
#define TCG_TARGET_HAS_add2_i32         1
#define TCG_TARGET_HAS_sub2_i32         1
#define TCG_TARGET_HAS_mulu2_i32        1
#endif

#ifndef TCG_TARGET_deposit_i32_valid
#define TCG_TARGET_deposit_i32_valid(ofs, len) 1
#endif
#ifndef TCG_TARGET_deposit_i64_valid
#define TCG_TARGET_deposit_i64_valid(ofs, len) 1
#endif

/* Only one of DIV or DIV2 should be defined.  */
#if defined(TCG_TARGET_HAS_div_i32)
#define TCG_TARGET_HAS_div2_i32         0
#elif defined(TCG_TARGET_HAS_div2_i32)
#define TCG_TARGET_HAS_div_i32          0
#endif
#if defined(TCG_TARGET_HAS_div_i64)
#define TCG_TARGET_HAS_div2_i64         0
#elif defined(TCG_TARGET_HAS_div2_i64)
#define TCG_TARGET_HAS_div_i64          0
#endif

typedef enum TCGOpcode {
#define DEF(name, oargs, iargs, cargs, flags) INDEX_op_ ## name,
#include "tcg-opc.h"
#undef DEF
    NB_OPS,
} TCGOpcode;

#define tcg_regset_clear(d) (d) = 0
#define tcg_regset_set(d, s) (d) = (s)
#define tcg_regset_set32(d, reg, val32) (d) |= (val32) << (reg)
#define tcg_regset_set_reg(d, r) (d) |= 1L << (r)
#define tcg_regset_reset_reg(d, r) (d) &= ~(1L << (r))
#define tcg_regset_test_reg(d, r) (((d) >> (r)) & 1)
#define tcg_regset_or(d, a, b) (d) = (a) | (b)
#define tcg_regset_and(d, a, b) (d) = (a) & (b)
#define tcg_regset_andnot(d, a, b) (d) = (a) & ~(b)
#define tcg_regset_not(d, a) (d) = ~(a)

typedef struct TCGRelocation {
    struct TCGRelocation *next;
    int type;
    uint8_t *ptr;
    tcg_target_long addend;
} TCGRelocation; 

typedef struct TCGLabel {
    int has_value;
    union {
        tcg_target_ulong value;
        TCGRelocation *first_reloc;
    } u;
} TCGLabel;

typedef struct TCGPool {
    struct TCGPool *next;
    int size;
    uint8_t data[0] __attribute__ ((aligned));
} TCGPool;

#define TCG_POOL_CHUNK_SIZE 32768

#define TCG_MAX_LABELS 512

#define TCG_MAX_TEMPS 512

/* when the size of the arguments of a called function is smaller than
   this value, they are statically allocated in the TB stack frame */
#define TCG_STATIC_CALL_ARGS_SIZE 128

typedef enum TCGType {
    TCG_TYPE_I32,
    TCG_TYPE_I64,
    TCG_TYPE_COUNT, /* number of different types */

    /* An alias for the size of the host register.  */
#if TCG_TARGET_REG_BITS == 32
    TCG_TYPE_REG = TCG_TYPE_I32,
#else
    TCG_TYPE_REG = TCG_TYPE_I64,
#endif

    /* An alias for the size of the native pointer.  We don't currently
       support any hosts with 64-bit registers and 32-bit pointers.  */
    TCG_TYPE_PTR = TCG_TYPE_REG,

    /* An alias for the size of the target "long", aka register.  */
#if TARGET_LONG_BITS == 64
    TCG_TYPE_TL = TCG_TYPE_I64,
#else
    TCG_TYPE_TL = TCG_TYPE_I32,
#endif
} TCGType;

typedef tcg_target_ulong TCGArg;

/* Define a type and accessor macros for variables.  Using a struct is
   nice because it gives some level of type safely.  Ideally the compiler
   be able to see through all this.  However in practice this is not true,
   especially on targets with braindamaged ABIs (e.g. i386).
   We use plain int by default to avoid this runtime overhead.
   Users of tcg_gen_* don't need to know about any of this, and should
   treat TCGv as an opaque type.
   In addition we do typechecking for different types of variables.  TCGv_i32
   and TCGv_i64 are 32/64-bit variables respectively.  TCGv and TCGv_ptr
   are aliases for target_ulong and host pointer sized values respectively.
 */

#if defined(CONFIG_QEMU_LDST_OPTIMIZATION) && defined(CONFIG_SOFTMMU)
/* Macros/structures for qemu_ld/st IR code optimization:
   TCG_MAX_HELPER_LABELS is defined as same as OPC_BUF_SIZE in exec-all.h. */
#define TCG_MAX_QEMU_LDST       640

typedef struct TCGLabelQemuLdst {
    int is_ld:1;            /* qemu_ld: 1, qemu_st: 0 */
    int opc:4;
    int addrlo_reg;         /* reg index for low word of guest virtual addr */
    int addrhi_reg;         /* reg index for high word of guest virtual addr */
    int datalo_reg;         /* reg index for low word to be loaded or stored */
    int datahi_reg;         /* reg index for high word to be loaded or stored */
    int mem_index;          /* soft MMU memory index */
    uint8_t *raddr;         /* gen code addr of the next IR of qemu_ld/st IR */
    uint8_t *label_ptr[2];  /* label pointers to be updated */
} TCGLabelQemuLdst;
#endif

#ifdef CONFIG_DEBUG_TCG
#define DEBUG_TCGV 1
#endif

#ifdef DEBUG_TCGV

typedef struct
{
    int i32;
} TCGv_i32;

typedef struct
{
    int i64;
} TCGv_i64;

typedef struct {
    int iptr;
} TCGv_ptr;

#define MAKE_TCGV_I32(i) __extension__                  \
    ({ TCGv_i32 make_tcgv_tmp = {i}; make_tcgv_tmp;})
#define MAKE_TCGV_I64(i) __extension__                  \
    ({ TCGv_i64 make_tcgv_tmp = {i}; make_tcgv_tmp;})
#define MAKE_TCGV_PTR(i) __extension__                  \
    ({ TCGv_ptr make_tcgv_tmp = {i}; make_tcgv_tmp; })
#define GET_TCGV_I32(t) ((t).i32)
#define GET_TCGV_I64(t) ((t).i64)
#define GET_TCGV_PTR(t) ((t).iptr)
#if TCG_TARGET_REG_BITS == 32
#define TCGV_LOW(t) MAKE_TCGV_I32(GET_TCGV_I64(t))
#define TCGV_HIGH(t) MAKE_TCGV_I32(GET_TCGV_I64(t) + 1)
#endif

#else /* !DEBUG_TCGV */

typedef int TCGv_i32;
typedef int TCGv_i64;
#if TCG_TARGET_REG_BITS == 32
#define TCGv_ptr TCGv_i32
#else
#define TCGv_ptr TCGv_i64
#endif
#define MAKE_TCGV_I32(x) (x)
#define MAKE_TCGV_I64(x) (x)
#define MAKE_TCGV_PTR(x) (x)
#define GET_TCGV_I32(t) (t)
#define GET_TCGV_I64(t) (t)
#define GET_TCGV_PTR(t) (t)

#if TCG_TARGET_REG_BITS == 32
#define TCGV_LOW(t) (t)
#define TCGV_HIGH(t) ((t) + 1)
#endif

#endif /* DEBUG_TCGV */

#define TCGV_EQUAL_I32(a, b) (GET_TCGV_I32(a) == GET_TCGV_I32(b))
#define TCGV_EQUAL_I64(a, b) (GET_TCGV_I64(a) == GET_TCGV_I64(b))

/* Dummy definition to avoid compiler warnings.  */
#define TCGV_UNUSED_I32(x) x = MAKE_TCGV_I32(-1)
#define TCGV_UNUSED_I64(x) x = MAKE_TCGV_I64(-1)

#define TCGV_IS_UNUSED_I32(x) (GET_TCGV_I32(x) == -1)
#define TCGV_IS_UNUSED_I64(x) (GET_TCGV_I64(x) == -1)

/* call flags */
/* Helper does not read globals (either directly or through an exception). It
   implies TCG_CALL_NO_WRITE_GLOBALS. */
#define TCG_CALL_NO_READ_GLOBALS    0x0010
/* Helper does not write globals */
#define TCG_CALL_NO_WRITE_GLOBALS   0x0020
/* Helper can be safely suppressed if the return value is not used. */
#define TCG_CALL_NO_SIDE_EFFECTS    0x0040

/* convenience version of most used call flags */
#define TCG_CALL_NO_RWG         TCG_CALL_NO_READ_GLOBALS
#define TCG_CALL_NO_WG          TCG_CALL_NO_WRITE_GLOBALS
#define TCG_CALL_NO_SE          TCG_CALL_NO_SIDE_EFFECTS
#define TCG_CALL_NO_RWG_SE      (TCG_CALL_NO_RWG | TCG_CALL_NO_SE)
#define TCG_CALL_NO_WG_SE       (TCG_CALL_NO_WG | TCG_CALL_NO_SE)

/* used to align parameters */
#define TCG_CALL_DUMMY_TCGV     MAKE_TCGV_I32(-1)
#define TCG_CALL_DUMMY_ARG      ((TCGArg)(-1))

/* Conditions.  Note that these are laid out for easy manipulation by
   the functions below:
     bit 0 is used for inverting;
     bit 1 is signed,
     bit 2 is unsigned,
     bit 3 is used with bit 0 for swapping signed/unsigned.  */
typedef enum {
    /* non-signed */
    TCG_COND_NEVER  = 0 | 0 | 0 | 0,
    TCG_COND_ALWAYS = 0 | 0 | 0 | 1,
    TCG_COND_EQ     = 8 | 0 | 0 | 0,
    TCG_COND_NE     = 8 | 0 | 0 | 1,
    /* signed */
    TCG_COND_LT     = 0 | 0 | 2 | 0,
    TCG_COND_GE     = 0 | 0 | 2 | 1,
    TCG_COND_LE     = 8 | 0 | 2 | 0,
    TCG_COND_GT     = 8 | 0 | 2 | 1,
    /* unsigned */
    TCG_COND_LTU    = 0 | 4 | 0 | 0,
    TCG_COND_GEU    = 0 | 4 | 0 | 1,
    TCG_COND_LEU    = 8 | 4 | 0 | 0,
    TCG_COND_GTU    = 8 | 4 | 0 | 1,
} TCGCond;

/* Invert the sense of the comparison.  */
static inline TCGCond tcg_invert_cond(TCGCond c)
{
    return (TCGCond)(c ^ 1);
}

/* Swap the operands in a comparison.  */
static inline TCGCond tcg_swap_cond(TCGCond c)
{
    return c & 6 ? (TCGCond)(c ^ 9) : c;
}

/* Create an "unsigned" version of a "signed" comparison.  */
static inline TCGCond tcg_unsigned_cond(TCGCond c)
{
    return c & 2 ? (TCGCond)(c ^ 6) : c;
}

/* Must a comparison be considered unsigned?  */
static inline bool is_unsigned_cond(TCGCond c)
{
    return (c & 4) != 0;
}

/* Create a "high" version of a double-word comparison.
   This removes equality from a LTE or GTE comparison.  */
static inline TCGCond tcg_high_cond(TCGCond c)
{
    switch (c) {
    case TCG_COND_GE:
    case TCG_COND_LE:
    case TCG_COND_GEU:
    case TCG_COND_LEU:
        return (TCGCond)(c ^ 8);
    default:
        return c;
    }
}

#define TEMP_VAL_DEAD  0
#define TEMP_VAL_REG   1
#define TEMP_VAL_MEM   2
#define TEMP_VAL_CONST 3

/* XXX: optimize memory layout */
typedef struct TCGTemp {
    TCGType base_type;
    TCGType type;
    int val_type;
    int reg;
    tcg_target_long val;
    int mem_reg;
    tcg_target_long mem_offset;
    unsigned int fixed_reg:1;
    unsigned int mem_coherent:1;
    unsigned int mem_allocated:1;
    unsigned int temp_local:1; /* If true, the temp is saved across
                                  basic blocks. Otherwise, it is not
                                  preserved across basic blocks. */
    unsigned int temp_allocated:1; /* never used for code gen */
    /* index of next free temp of same base type, -1 if end */
    int next_free_temp;
    const char *name;
} TCGTemp;

typedef struct TCGHelperInfo {
    tcg_target_ulong func;
    const char *name;
} TCGHelperInfo;

typedef struct TCGContext TCGContext;

struct TCGContext {
    uint8_t *pool_cur, *pool_end;
    TCGPool *pool_first, *pool_current, *pool_first_large;
    TCGLabel *labels;
    int nb_labels;
    int nb_globals;
    int nb_temps;
    /* index of free temps, -1 if none */
    int first_free_temp[TCG_TYPE_COUNT * 2]; 

    /* goto_tb support */
    uint8_t *code_buf;
    uintptr_t *tb_next;
    uint16_t *tb_next_offset;
    uint16_t *tb_jmp_offset; /* != NULL if USE_DIRECT_JUMP */

    /* liveness analysis */
    uint16_t *op_dead_args; /* for each operation, each bit tells if the
                               corresponding argument is dead */
    uint8_t *op_sync_args;  /* for each operation, each bit tells if the
                               corresponding output argument needs to be
                               sync to memory. */
    
    /* tells in which temporary a given register is. It does not take
       into account fixed registers */
    int reg_to_temp[TCG_TARGET_NB_REGS];
    TCGRegSet reserved_regs;
    tcg_target_long current_frame_offset;
    tcg_target_long frame_start;
    tcg_target_long frame_end;
    int frame_reg;

    uint8_t *code_ptr;
    TCGTemp temps[TCG_MAX_TEMPS]; /* globals first, temps after */

    TCGHelperInfo *helpers;
    int nb_helpers;
    int allocated_helpers;
    int helpers_sorted;

#ifdef CONFIG_PROFILER
    /* profiling info */
    int64_t tb_count1;
    int64_t tb_count;
    int64_t op_count; /* total insn count */
    int op_count_max; /* max insn per TB */
    int64_t temp_count;
    int temp_count_max;
    int64_t del_op_count;
    int64_t code_in_len;
    int64_t code_out_len;
    int64_t interm_time;
    int64_t code_time;
    int64_t la_time;
    int64_t opt_time;
    int64_t restore_count;
    int64_t restore_time;
#endif

#ifdef CONFIG_DEBUG_TCG
    int temps_in_use;
    int goto_tb_issue_mask;
#endif

    uint16_t gen_opc_buf[OPC_BUF_SIZE];
    TCGArg gen_opparam_buf[OPPARAM_BUF_SIZE];

    uint16_t *gen_opc_ptr;
    TCGArg *gen_opparam_ptr;
    target_ulong gen_opc_pc[OPC_BUF_SIZE];
    uint16_t gen_opc_icount[OPC_BUF_SIZE];
    uint8_t gen_opc_instr_start[OPC_BUF_SIZE];

    /* Code generation */
    int code_gen_max_blocks;
    uint8_t *code_gen_prologue;
    uint8_t *code_gen_buffer;
    size_t code_gen_buffer_size;
    /* threshold to flush the translated code buffer */
    size_t code_gen_buffer_max_size;
    uint8_t *code_gen_ptr;

    TBContext tb_ctx;

#if defined(CONFIG_QEMU_LDST_OPTIMIZATION) && defined(CONFIG_SOFTMMU)
    /* labels info for qemu_ld/st IRs
       The labels help to generate TLB miss case codes at the end of TB */
    TCGLabelQemuLdst *qemu_ldst_labels;
    int nb_qemu_ldst_labels;
#endif
};

extern TCGContext tcg_ctx;

/* pool based memory allocation */

void *tcg_malloc_internal(TCGContext *s, int size);
void tcg_pool_reset(TCGContext *s);
void tcg_pool_delete(TCGContext *s);

static inline void *tcg_malloc(int size)
{
    TCGContext *s = &tcg_ctx;
    uint8_t *ptr, *ptr_end;
    size = (size + sizeof(long) - 1) & ~(sizeof(long) - 1);
    ptr = s->pool_cur;
    ptr_end = ptr + size;
    if (unlikely(ptr_end > s->pool_end)) {
        return tcg_malloc_internal(&tcg_ctx, size);
    } else {
        s->pool_cur = ptr_end;
        return ptr;
    }
}

void tcg_context_init(TCGContext *s);
void tcg_prologue_init(TCGContext *s);
void tcg_func_start(TCGContext *s);

int tcg_gen_code(TCGContext *s, uint8_t *gen_code_buf);
int tcg_gen_code_search_pc(TCGContext *s, uint8_t *gen_code_buf, long offset);

void tcg_set_frame(TCGContext *s, int reg,
                   tcg_target_long start, tcg_target_long size);

TCGv_i32 tcg_global_reg_new_i32(int reg, const char *name);
TCGv_i32 tcg_global_mem_new_i32(int reg, tcg_target_long offset,
                                const char *name);
TCGv_i32 tcg_temp_new_internal_i32(int temp_local);
static inline TCGv_i32 tcg_temp_new_i32(void)
{
    return tcg_temp_new_internal_i32(0);
}
static inline TCGv_i32 tcg_temp_local_new_i32(void)
{
    return tcg_temp_new_internal_i32(1);
}
void tcg_temp_free_i32(TCGv_i32 arg);
char *tcg_get_arg_str_i32(TCGContext *s, char *buf, int buf_size, TCGv_i32 arg);

TCGv_i64 tcg_global_reg_new_i64(int reg, const char *name);
TCGv_i64 tcg_global_mem_new_i64(int reg, tcg_target_long offset,
                                const char *name);
TCGv_i64 tcg_temp_new_internal_i64(int temp_local);
static inline TCGv_i64 tcg_temp_new_i64(void)
{
    return tcg_temp_new_internal_i64(0);
}
static inline TCGv_i64 tcg_temp_local_new_i64(void)
{
    return tcg_temp_new_internal_i64(1);
}
void tcg_temp_free_i64(TCGv_i64 arg);
char *tcg_get_arg_str_i64(TCGContext *s, char *buf, int buf_size, TCGv_i64 arg);

#if defined(CONFIG_DEBUG_TCG)
/* If you call tcg_clear_temp_count() at the start of a section of
 * code which is not supposed to leak any TCG temporaries, then
 * calling tcg_check_temp_count() at the end of the section will
 * return 1 if the section did in fact leak a temporary.
 */
void tcg_clear_temp_count(void);
int tcg_check_temp_count(void);
#else
#define tcg_clear_temp_count() do { } while (0)
#define tcg_check_temp_count() 0
#endif

void tcg_dump_info(FILE *f, fprintf_function cpu_fprintf);

#define TCG_CT_ALIAS  0x80
#define TCG_CT_IALIAS 0x40
#define TCG_CT_REG    0x01
#define TCG_CT_CONST  0x02 /* any constant of register size */

typedef struct TCGArgConstraint {
    uint16_t ct;
    uint8_t alias_index;
    union {
        TCGRegSet regs;
    } u;
} TCGArgConstraint;

#define TCG_MAX_OP_ARGS 16

/* Bits for TCGOpDef->flags, 8 bits available.  */
enum {
    /* Instruction defines the end of a basic block.  */
    TCG_OPF_BB_END       = 0x01,
    /* Instruction clobbers call registers and potentially update globals.  */
    TCG_OPF_CALL_CLOBBER = 0x02,
    /* Instruction has side effects: it cannot be removed if its outputs
       are not used, and might trigger exceptions.  */
    TCG_OPF_SIDE_EFFECTS = 0x04,
    /* Instruction operands are 64-bits (otherwise 32-bits).  */
    TCG_OPF_64BIT        = 0x08,
    /* Instruction is optional and not implemented by the host.  */
    TCG_OPF_NOT_PRESENT  = 0x10,
};

typedef struct TCGOpDef {
    const char *name;
    uint8_t nb_oargs, nb_iargs, nb_cargs, nb_args;
    uint8_t flags;
    TCGArgConstraint *args_ct;
    int *sorted_args;
#if defined(CONFIG_DEBUG_TCG)
    int used;
#endif
} TCGOpDef;

extern TCGOpDef tcg_op_defs[];
extern const size_t tcg_op_defs_max;

typedef struct TCGTargetOpDef {
    TCGOpcode op;
    const char *args_ct_str[TCG_MAX_OP_ARGS];
} TCGTargetOpDef;

#define tcg_abort() \
do {\
    fprintf(stderr, "%s:%d: tcg fatal error\n", __FILE__, __LINE__);\
    abort();\
} while (0)

#ifdef CONFIG_DEBUG_TCG
# define tcg_debug_assert(X) do { assert(X); } while (0)
#elif QEMU_GNUC_PREREQ(4, 5)
# define tcg_debug_assert(X) \
    do { if (!(X)) { __builtin_unreachable(); } } while (0)
#else
# define tcg_debug_assert(X) do { (void)(X); } while (0)
#endif

void tcg_add_target_add_op_defs(const TCGTargetOpDef *tdefs);

#if TCG_TARGET_REG_BITS == 32
#define TCGV_NAT_TO_PTR(n) MAKE_TCGV_PTR(GET_TCGV_I32(n))
#define TCGV_PTR_TO_NAT(n) MAKE_TCGV_I32(GET_TCGV_PTR(n))

#define tcg_const_ptr(V) TCGV_NAT_TO_PTR(tcg_const_i32((tcg_target_long)(V)))
#define tcg_global_reg_new_ptr(R, N) \
    TCGV_NAT_TO_PTR(tcg_global_reg_new_i32((R), (N)))
#define tcg_global_mem_new_ptr(R, O, N) \
    TCGV_NAT_TO_PTR(tcg_global_mem_new_i32((R), (O), (N)))
#define tcg_temp_new_ptr() TCGV_NAT_TO_PTR(tcg_temp_new_i32())
#define tcg_temp_free_ptr(T) tcg_temp_free_i32(TCGV_PTR_TO_NAT(T))
#else
#define TCGV_NAT_TO_PTR(n) MAKE_TCGV_PTR(GET_TCGV_I64(n))
#define TCGV_PTR_TO_NAT(n) MAKE_TCGV_I64(GET_TCGV_PTR(n))

#define tcg_const_ptr(V) TCGV_NAT_TO_PTR(tcg_const_i64((tcg_target_long)(V)))
#define tcg_global_reg_new_ptr(R, N) \
    TCGV_NAT_TO_PTR(tcg_global_reg_new_i64((R), (N)))
#define tcg_global_mem_new_ptr(R, O, N) \
    TCGV_NAT_TO_PTR(tcg_global_mem_new_i64((R), (O), (N)))
#define tcg_temp_new_ptr() TCGV_NAT_TO_PTR(tcg_temp_new_i64())
#define tcg_temp_free_ptr(T) tcg_temp_free_i64(TCGV_PTR_TO_NAT(T))
#endif

void tcg_gen_callN(TCGContext *s, TCGv_ptr func, unsigned int flags,
                   int sizemask, TCGArg ret, int nargs, TCGArg *args);

void tcg_gen_shifti_i64(TCGv_i64 ret, TCGv_i64 arg1,
                        int c, int right, int arith);

TCGArg *tcg_optimize(TCGContext *s, uint16_t *tcg_opc_ptr, TCGArg *args,
                     TCGOpDef *tcg_op_def);

/* only used for debugging purposes */
void tcg_register_helper(void *func, const char *name);
const char *tcg_helper_get_name(TCGContext *s, void *func);
void tcg_dump_ops(TCGContext *s);

void dump_ops(const uint16_t *opc_buf, const TCGArg *opparam_buf);
TCGv_i32 tcg_const_i32(int32_t val);
TCGv_i64 tcg_const_i64(int64_t val);
TCGv_i32 tcg_const_local_i32(int32_t val);
TCGv_i64 tcg_const_local_i64(int64_t val);

/**
 * tcg_qemu_tb_exec:
 * @env: CPUArchState * for the CPU
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
 * The return value is a pointer to the next TB to execute
 * (if known; otherwise zero). This pointer is assumed to be
 * 4-aligned, and the bottom two bits are used to return further
 * information:
 *  0, 1: the link between this TB and the next is via the specified
 *        TB index (0 or 1). That is, we left the TB via (the equivalent
 *        of) "goto_tb <index>". The main loop uses this to determine
 *        how to link the TB just executed to the next.
 *  2:    we are using instruction counting code generation, and we
 *        did not start executing this TB because the instruction counter
 *        would hit zero midway through it. In this case the next-TB pointer
 *        returned is the TB we were about to execute, and the caller must
 *        arrange to execute the remaining count of instructions.
 *  3:    we stopped because the CPU's exit_request flag was set
 *        (usually meaning that there is an interrupt that needs to be
 *        handled). The next-TB pointer returned is the TB we were
 *        about to execute when we noticed the pending exit request.
 *
 * If the bottom two bits indicate an exit-via-index then the CPU
 * state is correctly synchronised and ready for execution of the next
 * TB (and in particular the guest PC is the address to execute next).
 * Otherwise, we gave up on execution of this TB before it started, and
 * the caller must fix up the CPU state by calling cpu_pc_from_tb()
 * with the next-TB pointer we return.
 *
 * Note that TCG targets may use a different definition of tcg_qemu_tb_exec
 * to this default (which just calls the prologue.code emitted by
 * tcg_target_qemu_prologue()).
 */
#define TB_EXIT_MASK 3
#define TB_EXIT_IDX0 0
#define TB_EXIT_IDX1 1
#define TB_EXIT_ICOUNT_EXPIRED 2
#define TB_EXIT_REQUESTED 3

#if !defined(tcg_qemu_tb_exec)
# define tcg_qemu_tb_exec(env, tb_ptr) \
    ((tcg_target_ulong (*)(void *, void *))tcg_ctx.code_gen_prologue)(env, \
                                                                      tb_ptr)
#endif

void tcg_register_jit(void *buf, size_t buf_size);

#if defined(CONFIG_QEMU_LDST_OPTIMIZATION) && defined(CONFIG_SOFTMMU)
/* Generate TB finalization at the end of block */
void tcg_out_tb_finalize(TCGContext *s);
#endif
