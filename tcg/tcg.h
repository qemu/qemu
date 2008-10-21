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
#include "tcg-target.h"

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

#if TCG_TARGET_NB_REGS <= 32
typedef uint32_t TCGRegSet;
#elif TCG_TARGET_NB_REGS <= 64
typedef uint64_t TCGRegSet;
#else
#error unsupported
#endif

enum {
#define DEF(s, n, copy_size) INDEX_op_ ## s,
#include "tcg-opc.h"
#undef DEF
    NB_OPS,
};

#define tcg_regset_clear(d) (d) = 0
#define tcg_regset_set(d, s) (d) = (s)
#define tcg_regset_set32(d, reg, val32) (d) |= (val32) << (reg)
#define tcg_regset_set_reg(d, r) (d) |= 1 << (r)
#define tcg_regset_reset_reg(d, r) (d) &= ~(1 << (r))
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

typedef int TCGType;

#define TCG_TYPE_I32 0
#define TCG_TYPE_I64 1
#define TCG_TYPE_COUNT 2 /* number of different types */

#if TCG_TARGET_REG_BITS == 32
#define TCG_TYPE_PTR TCG_TYPE_I32
#else
#define TCG_TYPE_PTR TCG_TYPE_I64
#endif

typedef tcg_target_ulong TCGArg;

/* Define a type and accessor macros for varables.  Using a struct is
   nice because it gives some level of type safely.  Ideally the compiler
   be able to see through all this.  However in practice this is not true,
   expecially on targets with braindamaged ABIs (e.g. i386).
   We use plain int by default to avoid this runtime overhead.
   Users of tcg_gen_* don't need to know about any of this, and should
   treat TCGv as an opaque type.  */

//#define DEBUG_TCGV 1

#ifdef DEBUG_TCGV

typedef struct
{
    int n;
} TCGv;

#define MAKE_TCGV(i) __extension__ \
  ({ TCGv make_tcgv_tmp = {i}; make_tcgv_tmp;})
#define GET_TCGV(t) ((t).n)
#if TCG_TARGET_REG_BITS == 32
#define TCGV_HIGH(t) MAKE_TCGV(GET_TCGV(t) + 1)
#endif

#else /* !DEBUG_TCGV */

typedef int TCGv;
#define MAKE_TCGV(x) (x)
#define GET_TCGV(t) (t)
#if TCG_TARGET_REG_BITS == 32
#define TCGV_HIGH(t) ((t) + 1)
#endif

#endif /* DEBUG_TCGV */

/* Dummy definition to avoid compiler warnings.  */
#define TCGV_UNUSED(x) x = MAKE_TCGV(-1)

/* call flags */
#define TCG_CALL_TYPE_MASK      0x000f
#define TCG_CALL_TYPE_STD       0x0000 /* standard C call */
#define TCG_CALL_TYPE_REGPARM_1 0x0001 /* i386 style regparm call (1 reg) */
#define TCG_CALL_TYPE_REGPARM_2 0x0002 /* i386 style regparm call (2 regs) */
#define TCG_CALL_TYPE_REGPARM   0x0003 /* i386 style regparm call (3 regs) */
/* A pure function only reads its arguments and globals variables and
   cannot raise exceptions. Hence a call to a pure function can be
   safely suppressed if the return value is not used. */
#define TCG_CALL_PURE           0x0010 

/* used to align parameters */
#define TCG_CALL_DUMMY_TCGV     MAKE_TCGV(-1)
#define TCG_CALL_DUMMY_ARG      ((TCGArg)(-1))

typedef enum {
    TCG_COND_EQ,
    TCG_COND_NE,
    TCG_COND_LT,
    TCG_COND_GE,
    TCG_COND_LE,
    TCG_COND_GT,
    /* unsigned */
    TCG_COND_LTU,
    TCG_COND_GEU,
    TCG_COND_LEU,
    TCG_COND_GTU,
} TCGCond;

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
    unsigned int temp_local:1; /* If true, the temp is saved accross
                                  basic blocks. Otherwise, it is not
                                  preserved accross basic blocks. */
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
    TCGPool *pool_first, *pool_current;
    TCGLabel *labels;
    int nb_labels;
    TCGTemp *temps; /* globals first, temps after */
    int nb_globals;
    int nb_temps;
    /* index of free temps, -1 if none */
    int first_free_temp[TCG_TYPE_COUNT * 2]; 

    /* goto_tb support */
    uint8_t *code_buf;
    unsigned long *tb_next;
    uint16_t *tb_next_offset;
    uint16_t *tb_jmp_offset; /* != NULL if USE_DIRECT_JUMP */

    /* liveness analysis */
    uint16_t *op_dead_iargs; /* for each operation, each bit tells if the
                                corresponding input argument is dead */
    
    /* tells in which temporary a given register is. It does not take
       into account fixed registers */
    int reg_to_temp[TCG_TARGET_NB_REGS];
    TCGRegSet reserved_regs;
    tcg_target_long current_frame_offset;
    tcg_target_long frame_start;
    tcg_target_long frame_end;
    int frame_reg;

    uint8_t *code_ptr;
    TCGTemp static_temps[TCG_MAX_TEMPS];

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
    int64_t old_op_count;
    int64_t del_op_count;
    int64_t code_in_len;
    int64_t code_out_len;
    int64_t interm_time;
    int64_t code_time;
    int64_t la_time;
    int64_t restore_count;
    int64_t restore_time;
#endif
};

extern TCGContext tcg_ctx;
extern uint16_t *gen_opc_ptr;
extern TCGArg *gen_opparam_ptr;
extern uint16_t gen_opc_buf[];
extern TCGArg gen_opparam_buf[];

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
void tcg_func_start(TCGContext *s);

int dyngen_code(TCGContext *s, uint8_t *gen_code_buf);
int dyngen_code_search_pc(TCGContext *s, uint8_t *gen_code_buf, long offset);

void tcg_set_frame(TCGContext *s, int reg,
                   tcg_target_long start, tcg_target_long size);
TCGv tcg_global_reg_new(TCGType type, int reg, const char *name);
TCGv tcg_global_reg2_new_hack(TCGType type, int reg1, int reg2, 
                              const char *name);
TCGv tcg_global_mem_new(TCGType type, int reg, tcg_target_long offset,
                        const char *name);
TCGv tcg_temp_new_internal(TCGType type, int temp_local);
static inline TCGv tcg_temp_new(TCGType type)
{
    return tcg_temp_new_internal(type, 0);
}
static inline TCGv tcg_temp_local_new(TCGType type)
{
    return tcg_temp_new_internal(type, 1);
}
void tcg_temp_free(TCGv arg);
char *tcg_get_arg_str(TCGContext *s, char *buf, int buf_size, TCGv arg);
void tcg_dump_info(FILE *f,
                   int (*cpu_fprintf)(FILE *f, const char *fmt, ...));

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

#define TCG_OPF_BB_END     0x01 /* instruction defines the end of a basic
                                   block */
#define TCG_OPF_CALL_CLOBBER 0x02 /* instruction clobbers call registers 
                                   and potentially update globals. */
#define TCG_OPF_SIDE_EFFECTS 0x04 /* instruction has side effects : it
                                     cannot be removed if its output
                                     are not used */

typedef struct TCGOpDef {
    const char *name;
    uint8_t nb_oargs, nb_iargs, nb_cargs, nb_args;
    uint8_t flags;
    uint16_t copy_size;
    TCGArgConstraint *args_ct;
    int *sorted_args;
} TCGOpDef;
        
typedef struct TCGTargetOpDef {
    int op;
    const char *args_ct_str[TCG_MAX_OP_ARGS];
} TCGTargetOpDef;

extern TCGOpDef tcg_op_defs[];

void tcg_target_init(TCGContext *s);
void tcg_target_qemu_prologue(TCGContext *s);

#define tcg_abort() \
do {\
    fprintf(stderr, "%s:%d: tcg fatal error\n", __FILE__, __LINE__);\
    abort();\
} while (0)

void tcg_add_target_add_op_defs(const TCGTargetOpDef *tdefs);

void tcg_gen_call(TCGContext *s, TCGv func, unsigned int flags,
                  unsigned int nb_rets, const TCGv *rets,
                  unsigned int nb_params, const TCGv *args1);
void tcg_gen_shifti_i64(TCGv ret, TCGv arg1, 
                        int c, int right, int arith);

/* only used for debugging purposes */
void tcg_register_helper(void *func, const char *name);
#define TCG_HELPER(func) tcg_register_helper(func, #func)
const char *tcg_helper_get_name(TCGContext *s, void *func);
void tcg_dump_ops(TCGContext *s, FILE *outfile);

void dump_ops(const uint16_t *opc_buf, const TCGArg *opparam_buf);
TCGv tcg_const_i32(int32_t val);
TCGv tcg_const_i64(int64_t val);
TCGv tcg_const_local_i32(int32_t val);
TCGv tcg_const_local_i64(int64_t val);

#if TCG_TARGET_REG_BITS == 32
#define tcg_const_ptr tcg_const_i32
#define tcg_add_ptr tcg_add_i32
#define tcg_sub_ptr tcg_sub_i32
#else
#define tcg_const_ptr tcg_const_i64
#define tcg_add_ptr tcg_add_i64
#define tcg_sub_ptr tcg_sub_i64
#endif

void tcg_out_reloc(TCGContext *s, uint8_t *code_ptr, int type, 
                   int label_index, long addend);
const TCGArg *tcg_gen_code_op(TCGContext *s, int opc, const TCGArg *args1,
                              unsigned int dead_iargs);

const TCGArg *dyngen_op(TCGContext *s, int opc, const TCGArg *opparam_ptr);

/* tcg-runtime.c */
int64_t tcg_helper_shl_i64(int64_t arg1, int64_t arg2);
int64_t tcg_helper_shr_i64(int64_t arg1, int64_t arg2);
int64_t tcg_helper_sar_i64(int64_t arg1, int64_t arg2);
int64_t tcg_helper_div_i64(int64_t arg1, int64_t arg2);
int64_t tcg_helper_rem_i64(int64_t arg1, int64_t arg2);
uint64_t tcg_helper_divu_i64(uint64_t arg1, uint64_t arg2);
uint64_t tcg_helper_remu_i64(uint64_t arg1, uint64_t arg2);

extern uint8_t code_gen_prologue[];
#if defined(__powerpc__) && !defined(__powerpc64__)
#define tcg_qemu_tb_exec(tb_ptr) \
    ((long REGPARM __attribute__ ((longcall)) (*)(void *))code_gen_prologue)(tb_ptr)
#else
#define tcg_qemu_tb_exec(tb_ptr) ((long REGPARM (*)(void *))code_gen_prologue)(tb_ptr)
#endif
