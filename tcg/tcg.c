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

/* define it to use liveness analysis (better code) */
#define USE_TCG_OPTIMIZATIONS

#include "qemu/osdep.h"

/* Define to jump the ELF file used to communicate with GDB.  */
#undef DEBUG_JIT

#include "qemu/cutils.h"
#include "qemu/host-utils.h"
#include "qemu/timer.h"

/* Note: the long term plan is to reduce the dependencies on the QEMU
   CPU definitions. Currently they are used for qemu_ld/st
   instructions */
#define NO_CPU_IO_DEFS
#include "cpu.h"

#include "exec/cpu-common.h"
#include "exec/exec-all.h"

#include "tcg-op.h"

#if UINTPTR_MAX == UINT32_MAX
# define ELF_CLASS  ELFCLASS32
#else
# define ELF_CLASS  ELFCLASS64
#endif
#ifdef HOST_WORDS_BIGENDIAN
# define ELF_DATA   ELFDATA2MSB
#else
# define ELF_DATA   ELFDATA2LSB
#endif

#include "elf.h"
#include "exec/log.h"

/* Forward declarations for functions declared in tcg-target.inc.c and
   used here. */
static void tcg_target_init(TCGContext *s);
static const TCGTargetOpDef *tcg_target_op_def(TCGOpcode);
static void tcg_target_qemu_prologue(TCGContext *s);
static void patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend);

/* The CIE and FDE header definitions will be common to all hosts.  */
typedef struct {
    uint32_t len __attribute__((aligned((sizeof(void *)))));
    uint32_t id;
    uint8_t version;
    char augmentation[1];
    uint8_t code_align;
    uint8_t data_align;
    uint8_t return_column;
} DebugFrameCIE;

typedef struct QEMU_PACKED {
    uint32_t len __attribute__((aligned((sizeof(void *)))));
    uint32_t cie_offset;
    uintptr_t func_start;
    uintptr_t func_len;
} DebugFrameFDEHeader;

typedef struct QEMU_PACKED {
    DebugFrameCIE cie;
    DebugFrameFDEHeader fde;
} DebugFrameHeader;

static void tcg_register_jit_int(void *buf, size_t size,
                                 const void *debug_frame,
                                 size_t debug_frame_size)
    __attribute__((unused));

/* Forward declarations for functions declared and used in tcg-target.inc.c. */
static const char *target_parse_constraint(TCGArgConstraint *ct,
                                           const char *ct_str, TCGType type);
static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1,
                       intptr_t arg2);
static void tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg);
static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg);
static void tcg_out_op(TCGContext *s, TCGOpcode opc, const TCGArg *args,
                       const int *const_args);
static void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg, TCGReg arg1,
                       intptr_t arg2);
static bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                        TCGReg base, intptr_t ofs);
static void tcg_out_call(TCGContext *s, tcg_insn_unit *target);
static int tcg_target_const_match(tcg_target_long val, TCGType type,
                                  const TCGArgConstraint *arg_ct);
static void tcg_out_tb_init(TCGContext *s);
static bool tcg_out_tb_finalize(TCGContext *s);



static TCGRegSet tcg_target_available_regs[2];
static TCGRegSet tcg_target_call_clobber_regs;

#if TCG_TARGET_INSN_UNIT_SIZE == 1
static __attribute__((unused)) inline void tcg_out8(TCGContext *s, uint8_t v)
{
    *s->code_ptr++ = v;
}

static __attribute__((unused)) inline void tcg_patch8(tcg_insn_unit *p,
                                                      uint8_t v)
{
    *p = v;
}
#endif

#if TCG_TARGET_INSN_UNIT_SIZE <= 2
static __attribute__((unused)) inline void tcg_out16(TCGContext *s, uint16_t v)
{
    if (TCG_TARGET_INSN_UNIT_SIZE == 2) {
        *s->code_ptr++ = v;
    } else {
        tcg_insn_unit *p = s->code_ptr;
        memcpy(p, &v, sizeof(v));
        s->code_ptr = p + (2 / TCG_TARGET_INSN_UNIT_SIZE);
    }
}

static __attribute__((unused)) inline void tcg_patch16(tcg_insn_unit *p,
                                                       uint16_t v)
{
    if (TCG_TARGET_INSN_UNIT_SIZE == 2) {
        *p = v;
    } else {
        memcpy(p, &v, sizeof(v));
    }
}
#endif

#if TCG_TARGET_INSN_UNIT_SIZE <= 4
static __attribute__((unused)) inline void tcg_out32(TCGContext *s, uint32_t v)
{
    if (TCG_TARGET_INSN_UNIT_SIZE == 4) {
        *s->code_ptr++ = v;
    } else {
        tcg_insn_unit *p = s->code_ptr;
        memcpy(p, &v, sizeof(v));
        s->code_ptr = p + (4 / TCG_TARGET_INSN_UNIT_SIZE);
    }
}

static __attribute__((unused)) inline void tcg_patch32(tcg_insn_unit *p,
                                                       uint32_t v)
{
    if (TCG_TARGET_INSN_UNIT_SIZE == 4) {
        *p = v;
    } else {
        memcpy(p, &v, sizeof(v));
    }
}
#endif

#if TCG_TARGET_INSN_UNIT_SIZE <= 8
static __attribute__((unused)) inline void tcg_out64(TCGContext *s, uint64_t v)
{
    if (TCG_TARGET_INSN_UNIT_SIZE == 8) {
        *s->code_ptr++ = v;
    } else {
        tcg_insn_unit *p = s->code_ptr;
        memcpy(p, &v, sizeof(v));
        s->code_ptr = p + (8 / TCG_TARGET_INSN_UNIT_SIZE);
    }
}

static __attribute__((unused)) inline void tcg_patch64(tcg_insn_unit *p,
                                                       uint64_t v)
{
    if (TCG_TARGET_INSN_UNIT_SIZE == 8) {
        *p = v;
    } else {
        memcpy(p, &v, sizeof(v));
    }
}
#endif

/* label relocation processing */

static void tcg_out_reloc(TCGContext *s, tcg_insn_unit *code_ptr, int type,
                          TCGLabel *l, intptr_t addend)
{
    TCGRelocation *r;

    if (l->has_value) {
        /* FIXME: This may break relocations on RISC targets that
           modify instruction fields in place.  The caller may not have 
           written the initial value.  */
        patch_reloc(code_ptr, type, l->u.value, addend);
    } else {
        /* add a new relocation entry */
        r = tcg_malloc(sizeof(TCGRelocation));
        r->type = type;
        r->ptr = code_ptr;
        r->addend = addend;
        r->next = l->u.first_reloc;
        l->u.first_reloc = r;
    }
}

static void tcg_out_label(TCGContext *s, TCGLabel *l, tcg_insn_unit *ptr)
{
    intptr_t value = (intptr_t)ptr;
    TCGRelocation *r;

    tcg_debug_assert(!l->has_value);

    for (r = l->u.first_reloc; r != NULL; r = r->next) {
        patch_reloc(r->ptr, r->type, value, r->addend);
    }

    l->has_value = 1;
    l->u.value_ptr = ptr;
}

TCGLabel *gen_new_label(void)
{
    TCGContext *s = &tcg_ctx;
    TCGLabel *l = tcg_malloc(sizeof(TCGLabel));

    *l = (TCGLabel){
        .id = s->nb_labels++
    };

    return l;
}

#include "tcg-target.inc.c"

/* pool based memory allocation */
void *tcg_malloc_internal(TCGContext *s, int size)
{
    TCGPool *p;
    int pool_size;
    
    if (size > TCG_POOL_CHUNK_SIZE) {
        /* big malloc: insert a new pool (XXX: could optimize) */
        p = g_malloc(sizeof(TCGPool) + size);
        p->size = size;
        p->next = s->pool_first_large;
        s->pool_first_large = p;
        return p->data;
    } else {
        p = s->pool_current;
        if (!p) {
            p = s->pool_first;
            if (!p)
                goto new_pool;
        } else {
            if (!p->next) {
            new_pool:
                pool_size = TCG_POOL_CHUNK_SIZE;
                p = g_malloc(sizeof(TCGPool) + pool_size);
                p->size = pool_size;
                p->next = NULL;
                if (s->pool_current) 
                    s->pool_current->next = p;
                else
                    s->pool_first = p;
            } else {
                p = p->next;
            }
        }
    }
    s->pool_current = p;
    s->pool_cur = p->data + size;
    s->pool_end = p->data + p->size;
    return p->data;
}

void tcg_pool_reset(TCGContext *s)
{
    TCGPool *p, *t;
    for (p = s->pool_first_large; p; p = t) {
        t = p->next;
        g_free(p);
    }
    s->pool_first_large = NULL;
    s->pool_cur = s->pool_end = NULL;
    s->pool_current = NULL;
}

typedef struct TCGHelperInfo {
    void *func;
    const char *name;
    unsigned flags;
    unsigned sizemask;
} TCGHelperInfo;

#include "exec/helper-proto.h"

static const TCGHelperInfo all_helpers[] = {
#include "exec/helper-tcg.h"
};

static int indirect_reg_alloc_order[ARRAY_SIZE(tcg_target_reg_alloc_order)];
static void process_op_defs(TCGContext *s);

void tcg_context_init(TCGContext *s)
{
    int op, total_args, n, i;
    TCGOpDef *def;
    TCGArgConstraint *args_ct;
    int *sorted_args;
    GHashTable *helper_table;

    memset(s, 0, sizeof(*s));
    s->nb_globals = 0;

    /* Count total number of arguments and allocate the corresponding
       space */
    total_args = 0;
    for(op = 0; op < NB_OPS; op++) {
        def = &tcg_op_defs[op];
        n = def->nb_iargs + def->nb_oargs;
        total_args += n;
    }

    args_ct = g_malloc(sizeof(TCGArgConstraint) * total_args);
    sorted_args = g_malloc(sizeof(int) * total_args);

    for(op = 0; op < NB_OPS; op++) {
        def = &tcg_op_defs[op];
        def->args_ct = args_ct;
        def->sorted_args = sorted_args;
        n = def->nb_iargs + def->nb_oargs;
        sorted_args += n;
        args_ct += n;
    }

    /* Register helpers.  */
    /* Use g_direct_hash/equal for direct pointer comparisons on func.  */
    s->helpers = helper_table = g_hash_table_new(NULL, NULL);

    for (i = 0; i < ARRAY_SIZE(all_helpers); ++i) {
        g_hash_table_insert(helper_table, (gpointer)all_helpers[i].func,
                            (gpointer)&all_helpers[i]);
    }

    tcg_target_init(s);
    process_op_defs(s);

    /* Reverse the order of the saved registers, assuming they're all at
       the start of tcg_target_reg_alloc_order.  */
    for (n = 0; n < ARRAY_SIZE(tcg_target_reg_alloc_order); ++n) {
        int r = tcg_target_reg_alloc_order[n];
        if (tcg_regset_test_reg(tcg_target_call_clobber_regs, r)) {
            break;
        }
    }
    for (i = 0; i < n; ++i) {
        indirect_reg_alloc_order[i] = tcg_target_reg_alloc_order[n - 1 - i];
    }
    for (; i < ARRAY_SIZE(tcg_target_reg_alloc_order); ++i) {
        indirect_reg_alloc_order[i] = tcg_target_reg_alloc_order[i];
    }
}

void tcg_prologue_init(TCGContext *s)
{
    size_t prologue_size, total_size;
    void *buf0, *buf1;

    /* Put the prologue at the beginning of code_gen_buffer.  */
    buf0 = s->code_gen_buffer;
    s->code_ptr = buf0;
    s->code_buf = buf0;
    s->code_gen_prologue = buf0;

    /* Generate the prologue.  */
    tcg_target_qemu_prologue(s);
    buf1 = s->code_ptr;
    flush_icache_range((uintptr_t)buf0, (uintptr_t)buf1);

    /* Deduct the prologue from the buffer.  */
    prologue_size = tcg_current_code_size(s);
    s->code_gen_ptr = buf1;
    s->code_gen_buffer = buf1;
    s->code_buf = buf1;
    total_size = s->code_gen_buffer_size - prologue_size;
    s->code_gen_buffer_size = total_size;

    /* Compute a high-water mark, at which we voluntarily flush the buffer
       and start over.  The size here is arbitrary, significantly larger
       than we expect the code generation for any one opcode to require.  */
    s->code_gen_highwater = s->code_gen_buffer + (total_size - 1024);

    tcg_register_jit(s->code_gen_buffer, total_size);

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_OUT_ASM)) {
        qemu_log_lock();
        qemu_log("PROLOGUE: [size=%zu]\n", prologue_size);
        log_disas(buf0, prologue_size);
        qemu_log("\n");
        qemu_log_flush();
        qemu_log_unlock();
    }
#endif
}

void tcg_func_start(TCGContext *s)
{
    tcg_pool_reset(s);
    s->nb_temps = s->nb_globals;

    /* No temps have been previously allocated for size or locality.  */
    memset(s->free_temps, 0, sizeof(s->free_temps));

    s->nb_labels = 0;
    s->current_frame_offset = s->frame_start;

#ifdef CONFIG_DEBUG_TCG
    s->goto_tb_issue_mask = 0;
#endif

    s->gen_op_buf[0].next = 1;
    s->gen_op_buf[0].prev = 0;
    s->gen_next_op_idx = 1;
    s->gen_next_parm_idx = 0;

    s->be = tcg_malloc(sizeof(TCGBackendData));
}

static inline int temp_idx(TCGContext *s, TCGTemp *ts)
{
    ptrdiff_t n = ts - s->temps;
    tcg_debug_assert(n >= 0 && n < s->nb_temps);
    return n;
}

static inline TCGTemp *tcg_temp_alloc(TCGContext *s)
{
    int n = s->nb_temps++;
    tcg_debug_assert(n < TCG_MAX_TEMPS);
    return memset(&s->temps[n], 0, sizeof(TCGTemp));
}

static inline TCGTemp *tcg_global_alloc(TCGContext *s)
{
    tcg_debug_assert(s->nb_globals == s->nb_temps);
    s->nb_globals++;
    return tcg_temp_alloc(s);
}

static int tcg_global_reg_new_internal(TCGContext *s, TCGType type,
                                       TCGReg reg, const char *name)
{
    TCGTemp *ts;

    if (TCG_TARGET_REG_BITS == 32 && type != TCG_TYPE_I32) {
        tcg_abort();
    }

    ts = tcg_global_alloc(s);
    ts->base_type = type;
    ts->type = type;
    ts->fixed_reg = 1;
    ts->reg = reg;
    ts->name = name;
    tcg_regset_set_reg(s->reserved_regs, reg);

    return temp_idx(s, ts);
}

void tcg_set_frame(TCGContext *s, TCGReg reg, intptr_t start, intptr_t size)
{
    int idx;
    s->frame_start = start;
    s->frame_end = start + size;
    idx = tcg_global_reg_new_internal(s, TCG_TYPE_PTR, reg, "_frame");
    s->frame_temp = &s->temps[idx];
}

TCGv_i32 tcg_global_reg_new_i32(TCGReg reg, const char *name)
{
    TCGContext *s = &tcg_ctx;
    int idx;

    if (tcg_regset_test_reg(s->reserved_regs, reg)) {
        tcg_abort();
    }
    idx = tcg_global_reg_new_internal(s, TCG_TYPE_I32, reg, name);
    return MAKE_TCGV_I32(idx);
}

TCGv_i64 tcg_global_reg_new_i64(TCGReg reg, const char *name)
{
    TCGContext *s = &tcg_ctx;
    int idx;

    if (tcg_regset_test_reg(s->reserved_regs, reg)) {
        tcg_abort();
    }
    idx = tcg_global_reg_new_internal(s, TCG_TYPE_I64, reg, name);
    return MAKE_TCGV_I64(idx);
}

int tcg_global_mem_new_internal(TCGType type, TCGv_ptr base,
                                intptr_t offset, const char *name)
{
    TCGContext *s = &tcg_ctx;
    TCGTemp *base_ts = &s->temps[GET_TCGV_PTR(base)];
    TCGTemp *ts = tcg_global_alloc(s);
    int indirect_reg = 0, bigendian = 0;
#ifdef HOST_WORDS_BIGENDIAN
    bigendian = 1;
#endif

    if (!base_ts->fixed_reg) {
        /* We do not support double-indirect registers.  */
        tcg_debug_assert(!base_ts->indirect_reg);
        base_ts->indirect_base = 1;
        s->nb_indirects += (TCG_TARGET_REG_BITS == 32 && type == TCG_TYPE_I64
                            ? 2 : 1);
        indirect_reg = 1;
    }

    if (TCG_TARGET_REG_BITS == 32 && type == TCG_TYPE_I64) {
        TCGTemp *ts2 = tcg_global_alloc(s);
        char buf[64];

        ts->base_type = TCG_TYPE_I64;
        ts->type = TCG_TYPE_I32;
        ts->indirect_reg = indirect_reg;
        ts->mem_allocated = 1;
        ts->mem_base = base_ts;
        ts->mem_offset = offset + bigendian * 4;
        pstrcpy(buf, sizeof(buf), name);
        pstrcat(buf, sizeof(buf), "_0");
        ts->name = strdup(buf);

        tcg_debug_assert(ts2 == ts + 1);
        ts2->base_type = TCG_TYPE_I64;
        ts2->type = TCG_TYPE_I32;
        ts2->indirect_reg = indirect_reg;
        ts2->mem_allocated = 1;
        ts2->mem_base = base_ts;
        ts2->mem_offset = offset + (1 - bigendian) * 4;
        pstrcpy(buf, sizeof(buf), name);
        pstrcat(buf, sizeof(buf), "_1");
        ts2->name = strdup(buf);
    } else {
        ts->base_type = type;
        ts->type = type;
        ts->indirect_reg = indirect_reg;
        ts->mem_allocated = 1;
        ts->mem_base = base_ts;
        ts->mem_offset = offset;
        ts->name = name;
    }
    return temp_idx(s, ts);
}

static int tcg_temp_new_internal(TCGType type, int temp_local)
{
    TCGContext *s = &tcg_ctx;
    TCGTemp *ts;
    int idx, k;

    k = type + (temp_local ? TCG_TYPE_COUNT : 0);
    idx = find_first_bit(s->free_temps[k].l, TCG_MAX_TEMPS);
    if (idx < TCG_MAX_TEMPS) {
        /* There is already an available temp with the right type.  */
        clear_bit(idx, s->free_temps[k].l);

        ts = &s->temps[idx];
        ts->temp_allocated = 1;
        tcg_debug_assert(ts->base_type == type);
        tcg_debug_assert(ts->temp_local == temp_local);
    } else {
        ts = tcg_temp_alloc(s);
        if (TCG_TARGET_REG_BITS == 32 && type == TCG_TYPE_I64) {
            TCGTemp *ts2 = tcg_temp_alloc(s);

            ts->base_type = type;
            ts->type = TCG_TYPE_I32;
            ts->temp_allocated = 1;
            ts->temp_local = temp_local;

            tcg_debug_assert(ts2 == ts + 1);
            ts2->base_type = TCG_TYPE_I64;
            ts2->type = TCG_TYPE_I32;
            ts2->temp_allocated = 1;
            ts2->temp_local = temp_local;
        } else {
            ts->base_type = type;
            ts->type = type;
            ts->temp_allocated = 1;
            ts->temp_local = temp_local;
        }
        idx = temp_idx(s, ts);
    }

#if defined(CONFIG_DEBUG_TCG)
    s->temps_in_use++;
#endif
    return idx;
}

TCGv_i32 tcg_temp_new_internal_i32(int temp_local)
{
    int idx;

    idx = tcg_temp_new_internal(TCG_TYPE_I32, temp_local);
    return MAKE_TCGV_I32(idx);
}

TCGv_i64 tcg_temp_new_internal_i64(int temp_local)
{
    int idx;

    idx = tcg_temp_new_internal(TCG_TYPE_I64, temp_local);
    return MAKE_TCGV_I64(idx);
}

static void tcg_temp_free_internal(int idx)
{
    TCGContext *s = &tcg_ctx;
    TCGTemp *ts;
    int k;

#if defined(CONFIG_DEBUG_TCG)
    s->temps_in_use--;
    if (s->temps_in_use < 0) {
        fprintf(stderr, "More temporaries freed than allocated!\n");
    }
#endif

    tcg_debug_assert(idx >= s->nb_globals && idx < s->nb_temps);
    ts = &s->temps[idx];
    tcg_debug_assert(ts->temp_allocated != 0);
    ts->temp_allocated = 0;

    k = ts->base_type + (ts->temp_local ? TCG_TYPE_COUNT : 0);
    set_bit(idx, s->free_temps[k].l);
}

void tcg_temp_free_i32(TCGv_i32 arg)
{
    tcg_temp_free_internal(GET_TCGV_I32(arg));
}

void tcg_temp_free_i64(TCGv_i64 arg)
{
    tcg_temp_free_internal(GET_TCGV_I64(arg));
}

TCGv_i32 tcg_const_i32(int32_t val)
{
    TCGv_i32 t0;
    t0 = tcg_temp_new_i32();
    tcg_gen_movi_i32(t0, val);
    return t0;
}

TCGv_i64 tcg_const_i64(int64_t val)
{
    TCGv_i64 t0;
    t0 = tcg_temp_new_i64();
    tcg_gen_movi_i64(t0, val);
    return t0;
}

TCGv_i32 tcg_const_local_i32(int32_t val)
{
    TCGv_i32 t0;
    t0 = tcg_temp_local_new_i32();
    tcg_gen_movi_i32(t0, val);
    return t0;
}

TCGv_i64 tcg_const_local_i64(int64_t val)
{
    TCGv_i64 t0;
    t0 = tcg_temp_local_new_i64();
    tcg_gen_movi_i64(t0, val);
    return t0;
}

#if defined(CONFIG_DEBUG_TCG)
void tcg_clear_temp_count(void)
{
    TCGContext *s = &tcg_ctx;
    s->temps_in_use = 0;
}

int tcg_check_temp_count(void)
{
    TCGContext *s = &tcg_ctx;
    if (s->temps_in_use) {
        /* Clear the count so that we don't give another
         * warning immediately next time around.
         */
        s->temps_in_use = 0;
        return 1;
    }
    return 0;
}
#endif

/* Note: we convert the 64 bit args to 32 bit and do some alignment
   and endian swap. Maybe it would be better to do the alignment
   and endian swap in tcg_reg_alloc_call(). */
void tcg_gen_callN(TCGContext *s, void *func, TCGArg ret,
                   int nargs, TCGArg *args)
{
    int i, real_args, nb_rets, pi, pi_first;
    unsigned sizemask, flags;
    TCGHelperInfo *info;

    info = g_hash_table_lookup(s->helpers, (gpointer)func);
    flags = info->flags;
    sizemask = info->sizemask;

#if defined(__sparc__) && !defined(__arch64__) \
    && !defined(CONFIG_TCG_INTERPRETER)
    /* We have 64-bit values in one register, but need to pass as two
       separate parameters.  Split them.  */
    int orig_sizemask = sizemask;
    int orig_nargs = nargs;
    TCGv_i64 retl, reth;

    TCGV_UNUSED_I64(retl);
    TCGV_UNUSED_I64(reth);
    if (sizemask != 0) {
        TCGArg *split_args = __builtin_alloca(sizeof(TCGArg) * nargs * 2);
        for (i = real_args = 0; i < nargs; ++i) {
            int is_64bit = sizemask & (1 << (i+1)*2);
            if (is_64bit) {
                TCGv_i64 orig = MAKE_TCGV_I64(args[i]);
                TCGv_i32 h = tcg_temp_new_i32();
                TCGv_i32 l = tcg_temp_new_i32();
                tcg_gen_extr_i64_i32(l, h, orig);
                split_args[real_args++] = GET_TCGV_I32(h);
                split_args[real_args++] = GET_TCGV_I32(l);
            } else {
                split_args[real_args++] = args[i];
            }
        }
        nargs = real_args;
        args = split_args;
        sizemask = 0;
    }
#elif defined(TCG_TARGET_EXTEND_ARGS) && TCG_TARGET_REG_BITS == 64
    for (i = 0; i < nargs; ++i) {
        int is_64bit = sizemask & (1 << (i+1)*2);
        int is_signed = sizemask & (2 << (i+1)*2);
        if (!is_64bit) {
            TCGv_i64 temp = tcg_temp_new_i64();
            TCGv_i64 orig = MAKE_TCGV_I64(args[i]);
            if (is_signed) {
                tcg_gen_ext32s_i64(temp, orig);
            } else {
                tcg_gen_ext32u_i64(temp, orig);
            }
            args[i] = GET_TCGV_I64(temp);
        }
    }
#endif /* TCG_TARGET_EXTEND_ARGS */

    pi_first = pi = s->gen_next_parm_idx;
    if (ret != TCG_CALL_DUMMY_ARG) {
#if defined(__sparc__) && !defined(__arch64__) \
    && !defined(CONFIG_TCG_INTERPRETER)
        if (orig_sizemask & 1) {
            /* The 32-bit ABI is going to return the 64-bit value in
               the %o0/%o1 register pair.  Prepare for this by using
               two return temporaries, and reassemble below.  */
            retl = tcg_temp_new_i64();
            reth = tcg_temp_new_i64();
            s->gen_opparam_buf[pi++] = GET_TCGV_I64(reth);
            s->gen_opparam_buf[pi++] = GET_TCGV_I64(retl);
            nb_rets = 2;
        } else {
            s->gen_opparam_buf[pi++] = ret;
            nb_rets = 1;
        }
#else
        if (TCG_TARGET_REG_BITS < 64 && (sizemask & 1)) {
#ifdef HOST_WORDS_BIGENDIAN
            s->gen_opparam_buf[pi++] = ret + 1;
            s->gen_opparam_buf[pi++] = ret;
#else
            s->gen_opparam_buf[pi++] = ret;
            s->gen_opparam_buf[pi++] = ret + 1;
#endif
            nb_rets = 2;
        } else {
            s->gen_opparam_buf[pi++] = ret;
            nb_rets = 1;
        }
#endif
    } else {
        nb_rets = 0;
    }
    real_args = 0;
    for (i = 0; i < nargs; i++) {
        int is_64bit = sizemask & (1 << (i+1)*2);
        if (TCG_TARGET_REG_BITS < 64 && is_64bit) {
#ifdef TCG_TARGET_CALL_ALIGN_ARGS
            /* some targets want aligned 64 bit args */
            if (real_args & 1) {
                s->gen_opparam_buf[pi++] = TCG_CALL_DUMMY_ARG;
                real_args++;
            }
#endif
           /* If stack grows up, then we will be placing successive
              arguments at lower addresses, which means we need to
              reverse the order compared to how we would normally
              treat either big or little-endian.  For those arguments
              that will wind up in registers, this still works for
              HPPA (the only current STACK_GROWSUP target) since the
              argument registers are *also* allocated in decreasing
              order.  If another such target is added, this logic may
              have to get more complicated to differentiate between
              stack arguments and register arguments.  */
#if defined(HOST_WORDS_BIGENDIAN) != defined(TCG_TARGET_STACK_GROWSUP)
            s->gen_opparam_buf[pi++] = args[i] + 1;
            s->gen_opparam_buf[pi++] = args[i];
#else
            s->gen_opparam_buf[pi++] = args[i];
            s->gen_opparam_buf[pi++] = args[i] + 1;
#endif
            real_args += 2;
            continue;
        }

        s->gen_opparam_buf[pi++] = args[i];
        real_args++;
    }
    s->gen_opparam_buf[pi++] = (uintptr_t)func;
    s->gen_opparam_buf[pi++] = flags;

    i = s->gen_next_op_idx;
    tcg_debug_assert(i < OPC_BUF_SIZE);
    tcg_debug_assert(pi <= OPPARAM_BUF_SIZE);

    /* Set links for sequential allocation during translation.  */
    s->gen_op_buf[i] = (TCGOp){
        .opc = INDEX_op_call,
        .callo = nb_rets,
        .calli = real_args,
        .args = pi_first,
        .prev = i - 1,
        .next = i + 1
    };

    /* Make sure the calli field didn't overflow.  */
    tcg_debug_assert(s->gen_op_buf[i].calli == real_args);

    s->gen_op_buf[0].prev = i;
    s->gen_next_op_idx = i + 1;
    s->gen_next_parm_idx = pi;

#if defined(__sparc__) && !defined(__arch64__) \
    && !defined(CONFIG_TCG_INTERPRETER)
    /* Free all of the parts we allocated above.  */
    for (i = real_args = 0; i < orig_nargs; ++i) {
        int is_64bit = orig_sizemask & (1 << (i+1)*2);
        if (is_64bit) {
            TCGv_i32 h = MAKE_TCGV_I32(args[real_args++]);
            TCGv_i32 l = MAKE_TCGV_I32(args[real_args++]);
            tcg_temp_free_i32(h);
            tcg_temp_free_i32(l);
        } else {
            real_args++;
        }
    }
    if (orig_sizemask & 1) {
        /* The 32-bit ABI returned two 32-bit pieces.  Re-assemble them.
           Note that describing these as TCGv_i64 eliminates an unnecessary
           zero-extension that tcg_gen_concat_i32_i64 would create.  */
        tcg_gen_concat32_i64(MAKE_TCGV_I64(ret), retl, reth);
        tcg_temp_free_i64(retl);
        tcg_temp_free_i64(reth);
    }
#elif defined(TCG_TARGET_EXTEND_ARGS) && TCG_TARGET_REG_BITS == 64
    for (i = 0; i < nargs; ++i) {
        int is_64bit = sizemask & (1 << (i+1)*2);
        if (!is_64bit) {
            TCGv_i64 temp = MAKE_TCGV_I64(args[i]);
            tcg_temp_free_i64(temp);
        }
    }
#endif /* TCG_TARGET_EXTEND_ARGS */
}

static void tcg_reg_alloc_start(TCGContext *s)
{
    int i;
    TCGTemp *ts;
    for(i = 0; i < s->nb_globals; i++) {
        ts = &s->temps[i];
        if (ts->fixed_reg) {
            ts->val_type = TEMP_VAL_REG;
        } else {
            ts->val_type = TEMP_VAL_MEM;
        }
    }
    for(i = s->nb_globals; i < s->nb_temps; i++) {
        ts = &s->temps[i];
        if (ts->temp_local) {
            ts->val_type = TEMP_VAL_MEM;
        } else {
            ts->val_type = TEMP_VAL_DEAD;
        }
        ts->mem_allocated = 0;
        ts->fixed_reg = 0;
    }

    memset(s->reg_to_temp, 0, sizeof(s->reg_to_temp));
}

static char *tcg_get_arg_str_ptr(TCGContext *s, char *buf, int buf_size,
                                 TCGTemp *ts)
{
    int idx = temp_idx(s, ts);

    if (idx < s->nb_globals) {
        pstrcpy(buf, buf_size, ts->name);
    } else if (ts->temp_local) {
        snprintf(buf, buf_size, "loc%d", idx - s->nb_globals);
    } else {
        snprintf(buf, buf_size, "tmp%d", idx - s->nb_globals);
    }
    return buf;
}

static char *tcg_get_arg_str_idx(TCGContext *s, char *buf,
                                 int buf_size, int idx)
{
    tcg_debug_assert(idx >= 0 && idx < s->nb_temps);
    return tcg_get_arg_str_ptr(s, buf, buf_size, &s->temps[idx]);
}

/* Find helper name.  */
static inline const char *tcg_find_helper(TCGContext *s, uintptr_t val)
{
    const char *ret = NULL;
    if (s->helpers) {
        TCGHelperInfo *info = g_hash_table_lookup(s->helpers, (gpointer)val);
        if (info) {
            ret = info->name;
        }
    }
    return ret;
}

static const char * const cond_name[] =
{
    [TCG_COND_NEVER] = "never",
    [TCG_COND_ALWAYS] = "always",
    [TCG_COND_EQ] = "eq",
    [TCG_COND_NE] = "ne",
    [TCG_COND_LT] = "lt",
    [TCG_COND_GE] = "ge",
    [TCG_COND_LE] = "le",
    [TCG_COND_GT] = "gt",
    [TCG_COND_LTU] = "ltu",
    [TCG_COND_GEU] = "geu",
    [TCG_COND_LEU] = "leu",
    [TCG_COND_GTU] = "gtu"
};

static const char * const ldst_name[] =
{
    [MO_UB]   = "ub",
    [MO_SB]   = "sb",
    [MO_LEUW] = "leuw",
    [MO_LESW] = "lesw",
    [MO_LEUL] = "leul",
    [MO_LESL] = "lesl",
    [MO_LEQ]  = "leq",
    [MO_BEUW] = "beuw",
    [MO_BESW] = "besw",
    [MO_BEUL] = "beul",
    [MO_BESL] = "besl",
    [MO_BEQ]  = "beq",
};

static const char * const alignment_name[(MO_AMASK >> MO_ASHIFT) + 1] = {
#ifdef ALIGNED_ONLY
    [MO_UNALN >> MO_ASHIFT]    = "un+",
    [MO_ALIGN >> MO_ASHIFT]    = "",
#else
    [MO_UNALN >> MO_ASHIFT]    = "",
    [MO_ALIGN >> MO_ASHIFT]    = "al+",
#endif
    [MO_ALIGN_2 >> MO_ASHIFT]  = "al2+",
    [MO_ALIGN_4 >> MO_ASHIFT]  = "al4+",
    [MO_ALIGN_8 >> MO_ASHIFT]  = "al8+",
    [MO_ALIGN_16 >> MO_ASHIFT] = "al16+",
    [MO_ALIGN_32 >> MO_ASHIFT] = "al32+",
    [MO_ALIGN_64 >> MO_ASHIFT] = "al64+",
};

void tcg_dump_ops(TCGContext *s)
{
    char buf[128];
    TCGOp *op;
    int oi;

    for (oi = s->gen_op_buf[0].next; oi != 0; oi = op->next) {
        int i, k, nb_oargs, nb_iargs, nb_cargs;
        const TCGOpDef *def;
        const TCGArg *args;
        TCGOpcode c;
        int col = 0;

        op = &s->gen_op_buf[oi];
        c = op->opc;
        def = &tcg_op_defs[c];
        args = &s->gen_opparam_buf[op->args];

        if (c == INDEX_op_insn_start) {
            col += qemu_log("%s ----", oi != s->gen_op_buf[0].next ? "\n" : "");

            for (i = 0; i < TARGET_INSN_START_WORDS; ++i) {
                target_ulong a;
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
                a = ((target_ulong)args[i * 2 + 1] << 32) | args[i * 2];
#else
                a = args[i];
#endif
                col += qemu_log(" " TARGET_FMT_lx, a);
            }
        } else if (c == INDEX_op_call) {
            /* variable number of arguments */
            nb_oargs = op->callo;
            nb_iargs = op->calli;
            nb_cargs = def->nb_cargs;

            /* function name, flags, out args */
            col += qemu_log(" %s %s,$0x%" TCG_PRIlx ",$%d", def->name,
                            tcg_find_helper(s, args[nb_oargs + nb_iargs]),
                            args[nb_oargs + nb_iargs + 1], nb_oargs);
            for (i = 0; i < nb_oargs; i++) {
                col += qemu_log(",%s", tcg_get_arg_str_idx(s, buf, sizeof(buf),
                                                           args[i]));
            }
            for (i = 0; i < nb_iargs; i++) {
                TCGArg arg = args[nb_oargs + i];
                const char *t = "<dummy>";
                if (arg != TCG_CALL_DUMMY_ARG) {
                    t = tcg_get_arg_str_idx(s, buf, sizeof(buf), arg);
                }
                col += qemu_log(",%s", t);
            }
        } else {
            col += qemu_log(" %s ", def->name);

            nb_oargs = def->nb_oargs;
            nb_iargs = def->nb_iargs;
            nb_cargs = def->nb_cargs;

            k = 0;
            for (i = 0; i < nb_oargs; i++) {
                if (k != 0) {
                    col += qemu_log(",");
                }
                col += qemu_log("%s", tcg_get_arg_str_idx(s, buf, sizeof(buf),
                                                          args[k++]));
            }
            for (i = 0; i < nb_iargs; i++) {
                if (k != 0) {
                    col += qemu_log(",");
                }
                col += qemu_log("%s", tcg_get_arg_str_idx(s, buf, sizeof(buf),
                                                          args[k++]));
            }
            switch (c) {
            case INDEX_op_brcond_i32:
            case INDEX_op_setcond_i32:
            case INDEX_op_movcond_i32:
            case INDEX_op_brcond2_i32:
            case INDEX_op_setcond2_i32:
            case INDEX_op_brcond_i64:
            case INDEX_op_setcond_i64:
            case INDEX_op_movcond_i64:
                if (args[k] < ARRAY_SIZE(cond_name) && cond_name[args[k]]) {
                    col += qemu_log(",%s", cond_name[args[k++]]);
                } else {
                    col += qemu_log(",$0x%" TCG_PRIlx, args[k++]);
                }
                i = 1;
                break;
            case INDEX_op_qemu_ld_i32:
            case INDEX_op_qemu_st_i32:
            case INDEX_op_qemu_ld_i64:
            case INDEX_op_qemu_st_i64:
                {
                    TCGMemOpIdx oi = args[k++];
                    TCGMemOp op = get_memop(oi);
                    unsigned ix = get_mmuidx(oi);

                    if (op & ~(MO_AMASK | MO_BSWAP | MO_SSIZE)) {
                        col += qemu_log(",$0x%x,%u", op, ix);
                    } else {
                        const char *s_al, *s_op;
                        s_al = alignment_name[(op & MO_AMASK) >> MO_ASHIFT];
                        s_op = ldst_name[op & (MO_BSWAP | MO_SSIZE)];
                        col += qemu_log(",%s%s,%u", s_al, s_op, ix);
                    }
                    i = 1;
                }
                break;
            default:
                i = 0;
                break;
            }
            switch (c) {
            case INDEX_op_set_label:
            case INDEX_op_br:
            case INDEX_op_brcond_i32:
            case INDEX_op_brcond_i64:
            case INDEX_op_brcond2_i32:
                col += qemu_log("%s$L%d", k ? "," : "", arg_label(args[k])->id);
                i++, k++;
                break;
            default:
                break;
            }
            for (; i < nb_cargs; i++, k++) {
                col += qemu_log("%s$0x%" TCG_PRIlx, k ? "," : "", args[k]);
            }
        }
        if (op->life) {
            unsigned life = op->life;

            for (; col < 48; ++col) {
                putc(' ', qemu_logfile);
            }

            if (life & (SYNC_ARG * 3)) {
                qemu_log("  sync:");
                for (i = 0; i < 2; ++i) {
                    if (life & (SYNC_ARG << i)) {
                        qemu_log(" %d", i);
                    }
                }
            }
            life /= DEAD_ARG;
            if (life) {
                qemu_log("  dead:");
                for (i = 0; life; ++i, life >>= 1) {
                    if (life & 1) {
                        qemu_log(" %d", i);
                    }
                }
            }
        }
        qemu_log("\n");
    }
}

/* we give more priority to constraints with less registers */
static int get_constraint_priority(const TCGOpDef *def, int k)
{
    const TCGArgConstraint *arg_ct;

    int i, n;
    arg_ct = &def->args_ct[k];
    if (arg_ct->ct & TCG_CT_ALIAS) {
        /* an alias is equivalent to a single register */
        n = 1;
    } else {
        if (!(arg_ct->ct & TCG_CT_REG))
            return 0;
        n = 0;
        for(i = 0; i < TCG_TARGET_NB_REGS; i++) {
            if (tcg_regset_test_reg(arg_ct->u.regs, i))
                n++;
        }
    }
    return TCG_TARGET_NB_REGS - n + 1;
}

/* sort from highest priority to lowest */
static void sort_constraints(TCGOpDef *def, int start, int n)
{
    int i, j, p1, p2, tmp;

    for(i = 0; i < n; i++)
        def->sorted_args[start + i] = start + i;
    if (n <= 1)
        return;
    for(i = 0; i < n - 1; i++) {
        for(j = i + 1; j < n; j++) {
            p1 = get_constraint_priority(def, def->sorted_args[start + i]);
            p2 = get_constraint_priority(def, def->sorted_args[start + j]);
            if (p1 < p2) {
                tmp = def->sorted_args[start + i];
                def->sorted_args[start + i] = def->sorted_args[start + j];
                def->sorted_args[start + j] = tmp;
            }
        }
    }
}

static void process_op_defs(TCGContext *s)
{
    TCGOpcode op;

    for (op = 0; op < NB_OPS; op++) {
        TCGOpDef *def = &tcg_op_defs[op];
        const TCGTargetOpDef *tdefs;
        TCGType type;
        int i, nb_args;

        if (def->flags & TCG_OPF_NOT_PRESENT) {
            continue;
        }

        nb_args = def->nb_iargs + def->nb_oargs;
        if (nb_args == 0) {
            continue;
        }

        tdefs = tcg_target_op_def(op);
        /* Missing TCGTargetOpDef entry. */
        tcg_debug_assert(tdefs != NULL);

        type = (def->flags & TCG_OPF_64BIT ? TCG_TYPE_I64 : TCG_TYPE_I32);
        for (i = 0; i < nb_args; i++) {
            const char *ct_str = tdefs->args_ct_str[i];
            /* Incomplete TCGTargetOpDef entry. */
            tcg_debug_assert(ct_str != NULL);

            tcg_regset_clear(def->args_ct[i].u.regs);
            def->args_ct[i].ct = 0;
            while (*ct_str != '\0') {
                switch(*ct_str) {
                case '0' ... '9':
                    {
                        int oarg = *ct_str - '0';
                        tcg_debug_assert(ct_str == tdefs->args_ct_str[i]);
                        tcg_debug_assert(oarg < def->nb_oargs);
                        tcg_debug_assert(def->args_ct[oarg].ct & TCG_CT_REG);
                        /* TCG_CT_ALIAS is for the output arguments.
                           The input is tagged with TCG_CT_IALIAS. */
                        def->args_ct[i] = def->args_ct[oarg];
                        def->args_ct[oarg].ct |= TCG_CT_ALIAS;
                        def->args_ct[oarg].alias_index = i;
                        def->args_ct[i].ct |= TCG_CT_IALIAS;
                        def->args_ct[i].alias_index = oarg;
                    }
                    ct_str++;
                    break;
                case '&':
                    def->args_ct[i].ct |= TCG_CT_NEWREG;
                    ct_str++;
                    break;
                case 'i':
                    def->args_ct[i].ct |= TCG_CT_CONST;
                    ct_str++;
                    break;
                default:
                    ct_str = target_parse_constraint(&def->args_ct[i],
                                                     ct_str, type);
                    /* Typo in TCGTargetOpDef constraint. */
                    tcg_debug_assert(ct_str != NULL);
                }
            }
        }

        /* TCGTargetOpDef entry with too much information? */
        tcg_debug_assert(i == TCG_MAX_OP_ARGS || tdefs->args_ct_str[i] == NULL);

        /* sort the constraints (XXX: this is just an heuristic) */
        sort_constraints(def, 0, def->nb_oargs);
        sort_constraints(def, def->nb_oargs, def->nb_iargs);
    }
}

void tcg_op_remove(TCGContext *s, TCGOp *op)
{
    int next = op->next;
    int prev = op->prev;

    /* We should never attempt to remove the list terminator.  */
    tcg_debug_assert(op != &s->gen_op_buf[0]);

    s->gen_op_buf[next].prev = prev;
    s->gen_op_buf[prev].next = next;

    memset(op, 0, sizeof(*op));

#ifdef CONFIG_PROFILER
    s->del_op_count++;
#endif
}

TCGOp *tcg_op_insert_before(TCGContext *s, TCGOp *old_op,
                            TCGOpcode opc, int nargs)
{
    int oi = s->gen_next_op_idx;
    int pi = s->gen_next_parm_idx;
    int prev = old_op->prev;
    int next = old_op - s->gen_op_buf;
    TCGOp *new_op;

    tcg_debug_assert(oi < OPC_BUF_SIZE);
    tcg_debug_assert(pi + nargs <= OPPARAM_BUF_SIZE);
    s->gen_next_op_idx = oi + 1;
    s->gen_next_parm_idx = pi + nargs;

    new_op = &s->gen_op_buf[oi];
    *new_op = (TCGOp){
        .opc = opc,
        .args = pi,
        .prev = prev,
        .next = next
    };
    s->gen_op_buf[prev].next = oi;
    old_op->prev = oi;

    return new_op;
}

TCGOp *tcg_op_insert_after(TCGContext *s, TCGOp *old_op,
                           TCGOpcode opc, int nargs)
{
    int oi = s->gen_next_op_idx;
    int pi = s->gen_next_parm_idx;
    int prev = old_op - s->gen_op_buf;
    int next = old_op->next;
    TCGOp *new_op;

    tcg_debug_assert(oi < OPC_BUF_SIZE);
    tcg_debug_assert(pi + nargs <= OPPARAM_BUF_SIZE);
    s->gen_next_op_idx = oi + 1;
    s->gen_next_parm_idx = pi + nargs;

    new_op = &s->gen_op_buf[oi];
    *new_op = (TCGOp){
        .opc = opc,
        .args = pi,
        .prev = prev,
        .next = next
    };
    s->gen_op_buf[next].prev = oi;
    old_op->next = oi;

    return new_op;
}

#define TS_DEAD  1
#define TS_MEM   2

#define IS_DEAD_ARG(n)   (arg_life & (DEAD_ARG << (n)))
#define NEED_SYNC_ARG(n) (arg_life & (SYNC_ARG << (n)))

/* liveness analysis: end of function: all temps are dead, and globals
   should be in memory. */
static inline void tcg_la_func_end(TCGContext *s, uint8_t *temp_state)
{
    memset(temp_state, TS_DEAD | TS_MEM, s->nb_globals);
    memset(temp_state + s->nb_globals, TS_DEAD, s->nb_temps - s->nb_globals);
}

/* liveness analysis: end of basic block: all temps are dead, globals
   and local temps should be in memory. */
static inline void tcg_la_bb_end(TCGContext *s, uint8_t *temp_state)
{
    int i, n;

    tcg_la_func_end(s, temp_state);
    for (i = s->nb_globals, n = s->nb_temps; i < n; i++) {
        if (s->temps[i].temp_local) {
            temp_state[i] |= TS_MEM;
        }
    }
}

/* Liveness analysis : update the opc_arg_life array to tell if a
   given input arguments is dead. Instructions updating dead
   temporaries are removed. */
static void liveness_pass_1(TCGContext *s, uint8_t *temp_state)
{
    int nb_globals = s->nb_globals;
    int oi, oi_prev;

    tcg_la_func_end(s, temp_state);

    for (oi = s->gen_op_buf[0].prev; oi != 0; oi = oi_prev) {
        int i, nb_iargs, nb_oargs;
        TCGOpcode opc_new, opc_new2;
        bool have_opc_new2;
        TCGLifeData arg_life = 0;
        TCGArg arg;

        TCGOp * const op = &s->gen_op_buf[oi];
        TCGArg * const args = &s->gen_opparam_buf[op->args];
        TCGOpcode opc = op->opc;
        const TCGOpDef *def = &tcg_op_defs[opc];

        oi_prev = op->prev;

        switch (opc) {
        case INDEX_op_call:
            {
                int call_flags;

                nb_oargs = op->callo;
                nb_iargs = op->calli;
                call_flags = args[nb_oargs + nb_iargs + 1];

                /* pure functions can be removed if their result is unused */
                if (call_flags & TCG_CALL_NO_SIDE_EFFECTS) {
                    for (i = 0; i < nb_oargs; i++) {
                        arg = args[i];
                        if (temp_state[arg] != TS_DEAD) {
                            goto do_not_remove_call;
                        }
                    }
                    goto do_remove;
                } else {
                do_not_remove_call:

                    /* output args are dead */
                    for (i = 0; i < nb_oargs; i++) {
                        arg = args[i];
                        if (temp_state[arg] & TS_DEAD) {
                            arg_life |= DEAD_ARG << i;
                        }
                        if (temp_state[arg] & TS_MEM) {
                            arg_life |= SYNC_ARG << i;
                        }
                        temp_state[arg] = TS_DEAD;
                    }

                    if (!(call_flags & (TCG_CALL_NO_WRITE_GLOBALS |
                                        TCG_CALL_NO_READ_GLOBALS))) {
                        /* globals should go back to memory */
                        memset(temp_state, TS_DEAD | TS_MEM, nb_globals);
                    } else if (!(call_flags & TCG_CALL_NO_READ_GLOBALS)) {
                        /* globals should be synced to memory */
                        for (i = 0; i < nb_globals; i++) {
                            temp_state[i] |= TS_MEM;
                        }
                    }

                    /* record arguments that die in this helper */
                    for (i = nb_oargs; i < nb_iargs + nb_oargs; i++) {
                        arg = args[i];
                        if (arg != TCG_CALL_DUMMY_ARG) {
                            if (temp_state[arg] & TS_DEAD) {
                                arg_life |= DEAD_ARG << i;
                            }
                        }
                    }
                    /* input arguments are live for preceding opcodes */
                    for (i = nb_oargs; i < nb_iargs + nb_oargs; i++) {
                        arg = args[i];
                        if (arg != TCG_CALL_DUMMY_ARG) {
                            temp_state[arg] &= ~TS_DEAD;
                        }
                    }
                }
            }
            break;
        case INDEX_op_insn_start:
            break;
        case INDEX_op_discard:
            /* mark the temporary as dead */
            temp_state[args[0]] = TS_DEAD;
            break;

        case INDEX_op_add2_i32:
            opc_new = INDEX_op_add_i32;
            goto do_addsub2;
        case INDEX_op_sub2_i32:
            opc_new = INDEX_op_sub_i32;
            goto do_addsub2;
        case INDEX_op_add2_i64:
            opc_new = INDEX_op_add_i64;
            goto do_addsub2;
        case INDEX_op_sub2_i64:
            opc_new = INDEX_op_sub_i64;
        do_addsub2:
            nb_iargs = 4;
            nb_oargs = 2;
            /* Test if the high part of the operation is dead, but not
               the low part.  The result can be optimized to a simple
               add or sub.  This happens often for x86_64 guest when the
               cpu mode is set to 32 bit.  */
            if (temp_state[args[1]] == TS_DEAD) {
                if (temp_state[args[0]] == TS_DEAD) {
                    goto do_remove;
                }
                /* Replace the opcode and adjust the args in place,
                   leaving 3 unused args at the end.  */
                op->opc = opc = opc_new;
                args[1] = args[2];
                args[2] = args[4];
                /* Fall through and mark the single-word operation live.  */
                nb_iargs = 2;
                nb_oargs = 1;
            }
            goto do_not_remove;

        case INDEX_op_mulu2_i32:
            opc_new = INDEX_op_mul_i32;
            opc_new2 = INDEX_op_muluh_i32;
            have_opc_new2 = TCG_TARGET_HAS_muluh_i32;
            goto do_mul2;
        case INDEX_op_muls2_i32:
            opc_new = INDEX_op_mul_i32;
            opc_new2 = INDEX_op_mulsh_i32;
            have_opc_new2 = TCG_TARGET_HAS_mulsh_i32;
            goto do_mul2;
        case INDEX_op_mulu2_i64:
            opc_new = INDEX_op_mul_i64;
            opc_new2 = INDEX_op_muluh_i64;
            have_opc_new2 = TCG_TARGET_HAS_muluh_i64;
            goto do_mul2;
        case INDEX_op_muls2_i64:
            opc_new = INDEX_op_mul_i64;
            opc_new2 = INDEX_op_mulsh_i64;
            have_opc_new2 = TCG_TARGET_HAS_mulsh_i64;
            goto do_mul2;
        do_mul2:
            nb_iargs = 2;
            nb_oargs = 2;
            if (temp_state[args[1]] == TS_DEAD) {
                if (temp_state[args[0]] == TS_DEAD) {
                    /* Both parts of the operation are dead.  */
                    goto do_remove;
                }
                /* The high part of the operation is dead; generate the low. */
                op->opc = opc = opc_new;
                args[1] = args[2];
                args[2] = args[3];
            } else if (temp_state[args[0]] == TS_DEAD && have_opc_new2) {
                /* The low part of the operation is dead; generate the high. */
                op->opc = opc = opc_new2;
                args[0] = args[1];
                args[1] = args[2];
                args[2] = args[3];
            } else {
                goto do_not_remove;
            }
            /* Mark the single-word operation live.  */
            nb_oargs = 1;
            goto do_not_remove;

        default:
            /* XXX: optimize by hardcoding common cases (e.g. triadic ops) */
            nb_iargs = def->nb_iargs;
            nb_oargs = def->nb_oargs;

            /* Test if the operation can be removed because all
               its outputs are dead. We assume that nb_oargs == 0
               implies side effects */
            if (!(def->flags & TCG_OPF_SIDE_EFFECTS) && nb_oargs != 0) {
                for (i = 0; i < nb_oargs; i++) {
                    if (temp_state[args[i]] != TS_DEAD) {
                        goto do_not_remove;
                    }
                }
            do_remove:
                tcg_op_remove(s, op);
            } else {
            do_not_remove:
                /* output args are dead */
                for (i = 0; i < nb_oargs; i++) {
                    arg = args[i];
                    if (temp_state[arg] & TS_DEAD) {
                        arg_life |= DEAD_ARG << i;
                    }
                    if (temp_state[arg] & TS_MEM) {
                        arg_life |= SYNC_ARG << i;
                    }
                    temp_state[arg] = TS_DEAD;
                }

                /* if end of basic block, update */
                if (def->flags & TCG_OPF_BB_END) {
                    tcg_la_bb_end(s, temp_state);
                } else if (def->flags & TCG_OPF_SIDE_EFFECTS) {
                    /* globals should be synced to memory */
                    for (i = 0; i < nb_globals; i++) {
                        temp_state[i] |= TS_MEM;
                    }
                }

                /* record arguments that die in this opcode */
                for (i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
                    arg = args[i];
                    if (temp_state[arg] & TS_DEAD) {
                        arg_life |= DEAD_ARG << i;
                    }
                }
                /* input arguments are live for preceding opcodes */
                for (i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
                    temp_state[args[i]] &= ~TS_DEAD;
                }
            }
            break;
        }
        op->life = arg_life;
    }
}

/* Liveness analysis: Convert indirect regs to direct temporaries.  */
static bool liveness_pass_2(TCGContext *s, uint8_t *temp_state)
{
    int nb_globals = s->nb_globals;
    int16_t *dir_temps;
    int i, oi, oi_next;
    bool changes = false;

    dir_temps = tcg_malloc(nb_globals * sizeof(int16_t));
    memset(dir_temps, 0, nb_globals * sizeof(int16_t));

    /* Create a temporary for each indirect global.  */
    for (i = 0; i < nb_globals; ++i) {
        TCGTemp *its = &s->temps[i];
        if (its->indirect_reg) {
            TCGTemp *dts = tcg_temp_alloc(s);
            dts->type = its->type;
            dts->base_type = its->base_type;
            dir_temps[i] = temp_idx(s, dts);
        }
    }

    memset(temp_state, TS_DEAD, nb_globals);

    for (oi = s->gen_op_buf[0].next; oi != 0; oi = oi_next) {
        TCGOp *op = &s->gen_op_buf[oi];
        TCGArg *args = &s->gen_opparam_buf[op->args];
        TCGOpcode opc = op->opc;
        const TCGOpDef *def = &tcg_op_defs[opc];
        TCGLifeData arg_life = op->life;
        int nb_iargs, nb_oargs, call_flags;
        TCGArg arg, dir;

        oi_next = op->next;

        if (opc == INDEX_op_call) {
            nb_oargs = op->callo;
            nb_iargs = op->calli;
            call_flags = args[nb_oargs + nb_iargs + 1];
        } else {
            nb_iargs = def->nb_iargs;
            nb_oargs = def->nb_oargs;

            /* Set flags similar to how calls require.  */
            if (def->flags & TCG_OPF_BB_END) {
                /* Like writing globals: save_globals */
                call_flags = 0;
            } else if (def->flags & TCG_OPF_SIDE_EFFECTS) {
                /* Like reading globals: sync_globals */
                call_flags = TCG_CALL_NO_WRITE_GLOBALS;
            } else {
                /* No effect on globals.  */
                call_flags = (TCG_CALL_NO_READ_GLOBALS |
                              TCG_CALL_NO_WRITE_GLOBALS);
            }
        }

        /* Make sure that input arguments are available.  */
        for (i = nb_oargs; i < nb_iargs + nb_oargs; i++) {
            arg = args[i];
            /* Note this unsigned test catches TCG_CALL_ARG_DUMMY too.  */
            if (arg < nb_globals) {
                dir = dir_temps[arg];
                if (dir != 0 && temp_state[arg] == TS_DEAD) {
                    TCGTemp *its = &s->temps[arg];
                    TCGOpcode lopc = (its->type == TCG_TYPE_I32
                                      ? INDEX_op_ld_i32
                                      : INDEX_op_ld_i64);
                    TCGOp *lop = tcg_op_insert_before(s, op, lopc, 3);
                    TCGArg *largs = &s->gen_opparam_buf[lop->args];

                    largs[0] = dir;
                    largs[1] = temp_idx(s, its->mem_base);
                    largs[2] = its->mem_offset;

                    /* Loaded, but synced with memory.  */
                    temp_state[arg] = TS_MEM;
                }
            }
        }

        /* Perform input replacement, and mark inputs that became dead.
           No action is required except keeping temp_state up to date
           so that we reload when needed.  */
        for (i = nb_oargs; i < nb_iargs + nb_oargs; i++) {
            arg = args[i];
            if (arg < nb_globals) {
                dir = dir_temps[arg];
                if (dir != 0) {
                    args[i] = dir;
                    changes = true;
                    if (IS_DEAD_ARG(i)) {
                        temp_state[arg] = TS_DEAD;
                    }
                }
            }
        }

        /* Liveness analysis should ensure that the following are
           all correct, for call sites and basic block end points.  */
        if (call_flags & TCG_CALL_NO_READ_GLOBALS) {
            /* Nothing to do */
        } else if (call_flags & TCG_CALL_NO_WRITE_GLOBALS) {
            for (i = 0; i < nb_globals; ++i) {
                /* Liveness should see that globals are synced back,
                   that is, either TS_DEAD or TS_MEM.  */
                tcg_debug_assert(dir_temps[i] == 0
                                 || temp_state[i] != 0);
            }
        } else {
            for (i = 0; i < nb_globals; ++i) {
                /* Liveness should see that globals are saved back,
                   that is, TS_DEAD, waiting to be reloaded.  */
                tcg_debug_assert(dir_temps[i] == 0
                                 || temp_state[i] == TS_DEAD);
            }
        }

        /* Outputs become available.  */
        for (i = 0; i < nb_oargs; i++) {
            arg = args[i];
            if (arg >= nb_globals) {
                continue;
            }
            dir = dir_temps[arg];
            if (dir == 0) {
                continue;
            }
            args[i] = dir;
            changes = true;

            /* The output is now live and modified.  */
            temp_state[arg] = 0;

            /* Sync outputs upon their last write.  */
            if (NEED_SYNC_ARG(i)) {
                TCGTemp *its = &s->temps[arg];
                TCGOpcode sopc = (its->type == TCG_TYPE_I32
                                  ? INDEX_op_st_i32
                                  : INDEX_op_st_i64);
                TCGOp *sop = tcg_op_insert_after(s, op, sopc, 3);
                TCGArg *sargs = &s->gen_opparam_buf[sop->args];

                sargs[0] = dir;
                sargs[1] = temp_idx(s, its->mem_base);
                sargs[2] = its->mem_offset;

                temp_state[arg] = TS_MEM;
            }
            /* Drop outputs that are dead.  */
            if (IS_DEAD_ARG(i)) {
                temp_state[arg] = TS_DEAD;
            }
        }
    }

    return changes;
}

#ifdef CONFIG_DEBUG_TCG
static void dump_regs(TCGContext *s)
{
    TCGTemp *ts;
    int i;
    char buf[64];

    for(i = 0; i < s->nb_temps; i++) {
        ts = &s->temps[i];
        printf("  %10s: ", tcg_get_arg_str_idx(s, buf, sizeof(buf), i));
        switch(ts->val_type) {
        case TEMP_VAL_REG:
            printf("%s", tcg_target_reg_names[ts->reg]);
            break;
        case TEMP_VAL_MEM:
            printf("%d(%s)", (int)ts->mem_offset,
                   tcg_target_reg_names[ts->mem_base->reg]);
            break;
        case TEMP_VAL_CONST:
            printf("$0x%" TCG_PRIlx, ts->val);
            break;
        case TEMP_VAL_DEAD:
            printf("D");
            break;
        default:
            printf("???");
            break;
        }
        printf("\n");
    }

    for(i = 0; i < TCG_TARGET_NB_REGS; i++) {
        if (s->reg_to_temp[i] != NULL) {
            printf("%s: %s\n", 
                   tcg_target_reg_names[i], 
                   tcg_get_arg_str_ptr(s, buf, sizeof(buf), s->reg_to_temp[i]));
        }
    }
}

static void check_regs(TCGContext *s)
{
    int reg;
    int k;
    TCGTemp *ts;
    char buf[64];

    for (reg = 0; reg < TCG_TARGET_NB_REGS; reg++) {
        ts = s->reg_to_temp[reg];
        if (ts != NULL) {
            if (ts->val_type != TEMP_VAL_REG || ts->reg != reg) {
                printf("Inconsistency for register %s:\n", 
                       tcg_target_reg_names[reg]);
                goto fail;
            }
        }
    }
    for (k = 0; k < s->nb_temps; k++) {
        ts = &s->temps[k];
        if (ts->val_type == TEMP_VAL_REG && !ts->fixed_reg
            && s->reg_to_temp[ts->reg] != ts) {
            printf("Inconsistency for temp %s:\n",
                   tcg_get_arg_str_ptr(s, buf, sizeof(buf), ts));
        fail:
            printf("reg state:\n");
            dump_regs(s);
            tcg_abort();
        }
    }
}
#endif

static void temp_allocate_frame(TCGContext *s, int temp)
{
    TCGTemp *ts;
    ts = &s->temps[temp];
#if !(defined(__sparc__) && TCG_TARGET_REG_BITS == 64)
    /* Sparc64 stack is accessed with offset of 2047 */
    s->current_frame_offset = (s->current_frame_offset +
                               (tcg_target_long)sizeof(tcg_target_long) - 1) &
        ~(sizeof(tcg_target_long) - 1);
#endif
    if (s->current_frame_offset + (tcg_target_long)sizeof(tcg_target_long) >
        s->frame_end) {
        tcg_abort();
    }
    ts->mem_offset = s->current_frame_offset;
    ts->mem_base = s->frame_temp;
    ts->mem_allocated = 1;
    s->current_frame_offset += sizeof(tcg_target_long);
}

static void temp_load(TCGContext *, TCGTemp *, TCGRegSet, TCGRegSet);

/* Mark a temporary as free or dead.  If 'free_or_dead' is negative,
   mark it free; otherwise mark it dead.  */
static void temp_free_or_dead(TCGContext *s, TCGTemp *ts, int free_or_dead)
{
    if (ts->fixed_reg) {
        return;
    }
    if (ts->val_type == TEMP_VAL_REG) {
        s->reg_to_temp[ts->reg] = NULL;
    }
    ts->val_type = (free_or_dead < 0
                    || ts->temp_local
                    || temp_idx(s, ts) < s->nb_globals
                    ? TEMP_VAL_MEM : TEMP_VAL_DEAD);
}

/* Mark a temporary as dead.  */
static inline void temp_dead(TCGContext *s, TCGTemp *ts)
{
    temp_free_or_dead(s, ts, 1);
}

/* Sync a temporary to memory. 'allocated_regs' is used in case a temporary
   registers needs to be allocated to store a constant.  If 'free_or_dead'
   is non-zero, subsequently release the temporary; if it is positive, the
   temp is dead; if it is negative, the temp is free.  */
static void temp_sync(TCGContext *s, TCGTemp *ts,
                      TCGRegSet allocated_regs, int free_or_dead)
{
    if (ts->fixed_reg) {
        return;
    }
    if (!ts->mem_coherent) {
        if (!ts->mem_allocated) {
            temp_allocate_frame(s, temp_idx(s, ts));
        }
        switch (ts->val_type) {
        case TEMP_VAL_CONST:
            /* If we're going to free the temp immediately, then we won't
               require it later in a register, so attempt to store the
               constant to memory directly.  */
            if (free_or_dead
                && tcg_out_sti(s, ts->type, ts->val,
                               ts->mem_base->reg, ts->mem_offset)) {
                break;
            }
            temp_load(s, ts, tcg_target_available_regs[ts->type],
                      allocated_regs);
            /* fallthrough */

        case TEMP_VAL_REG:
            tcg_out_st(s, ts->type, ts->reg,
                       ts->mem_base->reg, ts->mem_offset);
            break;

        case TEMP_VAL_MEM:
            break;

        case TEMP_VAL_DEAD:
        default:
            tcg_abort();
        }
        ts->mem_coherent = 1;
    }
    if (free_or_dead) {
        temp_free_or_dead(s, ts, free_or_dead);
    }
}

/* free register 'reg' by spilling the corresponding temporary if necessary */
static void tcg_reg_free(TCGContext *s, TCGReg reg, TCGRegSet allocated_regs)
{
    TCGTemp *ts = s->reg_to_temp[reg];
    if (ts != NULL) {
        temp_sync(s, ts, allocated_regs, -1);
    }
}

/* Allocate a register belonging to reg1 & ~reg2 */
static TCGReg tcg_reg_alloc(TCGContext *s, TCGRegSet desired_regs,
                            TCGRegSet allocated_regs, bool rev)
{
    int i, n = ARRAY_SIZE(tcg_target_reg_alloc_order);
    const int *order;
    TCGReg reg;
    TCGRegSet reg_ct;

    tcg_regset_andnot(reg_ct, desired_regs, allocated_regs);
    order = rev ? indirect_reg_alloc_order : tcg_target_reg_alloc_order;

    /* first try free registers */
    for(i = 0; i < n; i++) {
        reg = order[i];
        if (tcg_regset_test_reg(reg_ct, reg) && s->reg_to_temp[reg] == NULL)
            return reg;
    }

    /* XXX: do better spill choice */
    for(i = 0; i < n; i++) {
        reg = order[i];
        if (tcg_regset_test_reg(reg_ct, reg)) {
            tcg_reg_free(s, reg, allocated_regs);
            return reg;
        }
    }

    tcg_abort();
}

/* Make sure the temporary is in a register.  If needed, allocate the register
   from DESIRED while avoiding ALLOCATED.  */
static void temp_load(TCGContext *s, TCGTemp *ts, TCGRegSet desired_regs,
                      TCGRegSet allocated_regs)
{
    TCGReg reg;

    switch (ts->val_type) {
    case TEMP_VAL_REG:
        return;
    case TEMP_VAL_CONST:
        reg = tcg_reg_alloc(s, desired_regs, allocated_regs, ts->indirect_base);
        tcg_out_movi(s, ts->type, reg, ts->val);
        ts->mem_coherent = 0;
        break;
    case TEMP_VAL_MEM:
        reg = tcg_reg_alloc(s, desired_regs, allocated_regs, ts->indirect_base);
        tcg_out_ld(s, ts->type, reg, ts->mem_base->reg, ts->mem_offset);
        ts->mem_coherent = 1;
        break;
    case TEMP_VAL_DEAD:
    default:
        tcg_abort();
    }
    ts->reg = reg;
    ts->val_type = TEMP_VAL_REG;
    s->reg_to_temp[reg] = ts;
}

/* Save a temporary to memory. 'allocated_regs' is used in case a
   temporary registers needs to be allocated to store a constant.  */
static void temp_save(TCGContext *s, TCGTemp *ts, TCGRegSet allocated_regs)
{
    /* The liveness analysis already ensures that globals are back
       in memory. Keep an tcg_debug_assert for safety. */
    tcg_debug_assert(ts->val_type == TEMP_VAL_MEM || ts->fixed_reg);
}

/* save globals to their canonical location and assume they can be
   modified be the following code. 'allocated_regs' is used in case a
   temporary registers needs to be allocated to store a constant. */
static void save_globals(TCGContext *s, TCGRegSet allocated_regs)
{
    int i;

    for (i = 0; i < s->nb_globals; i++) {
        temp_save(s, &s->temps[i], allocated_regs);
    }
}

/* sync globals to their canonical location and assume they can be
   read by the following code. 'allocated_regs' is used in case a
   temporary registers needs to be allocated to store a constant. */
static void sync_globals(TCGContext *s, TCGRegSet allocated_regs)
{
    int i;

    for (i = 0; i < s->nb_globals; i++) {
        TCGTemp *ts = &s->temps[i];
        tcg_debug_assert(ts->val_type != TEMP_VAL_REG
                         || ts->fixed_reg
                         || ts->mem_coherent);
    }
}

/* at the end of a basic block, we assume all temporaries are dead and
   all globals are stored at their canonical location. */
static void tcg_reg_alloc_bb_end(TCGContext *s, TCGRegSet allocated_regs)
{
    int i;

    for (i = s->nb_globals; i < s->nb_temps; i++) {
        TCGTemp *ts = &s->temps[i];
        if (ts->temp_local) {
            temp_save(s, ts, allocated_regs);
        } else {
            /* The liveness analysis already ensures that temps are dead.
               Keep an tcg_debug_assert for safety. */
            tcg_debug_assert(ts->val_type == TEMP_VAL_DEAD);
        }
    }

    save_globals(s, allocated_regs);
}

static void tcg_reg_alloc_do_movi(TCGContext *s, TCGTemp *ots,
                                  tcg_target_ulong val, TCGLifeData arg_life)
{
    if (ots->fixed_reg) {
        /* For fixed registers, we do not do any constant propagation.  */
        tcg_out_movi(s, ots->type, ots->reg, val);
        return;
    }

    /* The movi is not explicitly generated here.  */
    if (ots->val_type == TEMP_VAL_REG) {
        s->reg_to_temp[ots->reg] = NULL;
    }
    ots->val_type = TEMP_VAL_CONST;
    ots->val = val;
    ots->mem_coherent = 0;
    if (NEED_SYNC_ARG(0)) {
        temp_sync(s, ots, s->reserved_regs, IS_DEAD_ARG(0));
    } else if (IS_DEAD_ARG(0)) {
        temp_dead(s, ots);
    }
}

static void tcg_reg_alloc_movi(TCGContext *s, const TCGArg *args,
                               TCGLifeData arg_life)
{
    TCGTemp *ots = &s->temps[args[0]];
    tcg_target_ulong val = args[1];

    tcg_reg_alloc_do_movi(s, ots, val, arg_life);
}

static void tcg_reg_alloc_mov(TCGContext *s, const TCGOpDef *def,
                              const TCGArg *args, TCGLifeData arg_life)
{
    TCGRegSet allocated_regs;
    TCGTemp *ts, *ots;
    TCGType otype, itype;

    tcg_regset_set(allocated_regs, s->reserved_regs);
    ots = &s->temps[args[0]];
    ts = &s->temps[args[1]];

    /* Note that otype != itype for no-op truncation.  */
    otype = ots->type;
    itype = ts->type;

    if (ts->val_type == TEMP_VAL_CONST) {
        /* propagate constant or generate sti */
        tcg_target_ulong val = ts->val;
        if (IS_DEAD_ARG(1)) {
            temp_dead(s, ts);
        }
        tcg_reg_alloc_do_movi(s, ots, val, arg_life);
        return;
    }

    /* If the source value is in memory we're going to be forced
       to have it in a register in order to perform the copy.  Copy
       the SOURCE value into its own register first, that way we
       don't have to reload SOURCE the next time it is used. */
    if (ts->val_type == TEMP_VAL_MEM) {
        temp_load(s, ts, tcg_target_available_regs[itype], allocated_regs);
    }

    tcg_debug_assert(ts->val_type == TEMP_VAL_REG);
    if (IS_DEAD_ARG(0) && !ots->fixed_reg) {
        /* mov to a non-saved dead register makes no sense (even with
           liveness analysis disabled). */
        tcg_debug_assert(NEED_SYNC_ARG(0));
        if (!ots->mem_allocated) {
            temp_allocate_frame(s, args[0]);
        }
        tcg_out_st(s, otype, ts->reg, ots->mem_base->reg, ots->mem_offset);
        if (IS_DEAD_ARG(1)) {
            temp_dead(s, ts);
        }
        temp_dead(s, ots);
    } else {
        if (IS_DEAD_ARG(1) && !ts->fixed_reg && !ots->fixed_reg) {
            /* the mov can be suppressed */
            if (ots->val_type == TEMP_VAL_REG) {
                s->reg_to_temp[ots->reg] = NULL;
            }
            ots->reg = ts->reg;
            temp_dead(s, ts);
        } else {
            if (ots->val_type != TEMP_VAL_REG) {
                /* When allocating a new register, make sure to not spill the
                   input one. */
                tcg_regset_set_reg(allocated_regs, ts->reg);
                ots->reg = tcg_reg_alloc(s, tcg_target_available_regs[otype],
                                         allocated_regs, ots->indirect_base);
            }
            tcg_out_mov(s, otype, ots->reg, ts->reg);
        }
        ots->val_type = TEMP_VAL_REG;
        ots->mem_coherent = 0;
        s->reg_to_temp[ots->reg] = ots;
        if (NEED_SYNC_ARG(0)) {
            temp_sync(s, ots, allocated_regs, 0);
        }
    }
}

static void tcg_reg_alloc_op(TCGContext *s, 
                             const TCGOpDef *def, TCGOpcode opc,
                             const TCGArg *args, TCGLifeData arg_life)
{
    TCGRegSet i_allocated_regs;
    TCGRegSet o_allocated_regs;
    int i, k, nb_iargs, nb_oargs;
    TCGReg reg;
    TCGArg arg;
    const TCGArgConstraint *arg_ct;
    TCGTemp *ts;
    TCGArg new_args[TCG_MAX_OP_ARGS];
    int const_args[TCG_MAX_OP_ARGS];

    nb_oargs = def->nb_oargs;
    nb_iargs = def->nb_iargs;

    /* copy constants */
    memcpy(new_args + nb_oargs + nb_iargs, 
           args + nb_oargs + nb_iargs, 
           sizeof(TCGArg) * def->nb_cargs);

    tcg_regset_set(i_allocated_regs, s->reserved_regs);
    tcg_regset_set(o_allocated_regs, s->reserved_regs);

    /* satisfy input constraints */ 
    for(k = 0; k < nb_iargs; k++) {
        i = def->sorted_args[nb_oargs + k];
        arg = args[i];
        arg_ct = &def->args_ct[i];
        ts = &s->temps[arg];

        if (ts->val_type == TEMP_VAL_CONST
            && tcg_target_const_match(ts->val, ts->type, arg_ct)) {
            /* constant is OK for instruction */
            const_args[i] = 1;
            new_args[i] = ts->val;
            goto iarg_end;
        }

        temp_load(s, ts, arg_ct->u.regs, i_allocated_regs);

        if (arg_ct->ct & TCG_CT_IALIAS) {
            if (ts->fixed_reg) {
                /* if fixed register, we must allocate a new register
                   if the alias is not the same register */
                if (arg != args[arg_ct->alias_index])
                    goto allocate_in_reg;
            } else {
                /* if the input is aliased to an output and if it is
                   not dead after the instruction, we must allocate
                   a new register and move it */
                if (!IS_DEAD_ARG(i)) {
                    goto allocate_in_reg;
                }
                /* check if the current register has already been allocated
                   for another input aliased to an output */
                int k2, i2;
                for (k2 = 0 ; k2 < k ; k2++) {
                    i2 = def->sorted_args[nb_oargs + k2];
                    if ((def->args_ct[i2].ct & TCG_CT_IALIAS) &&
                        (new_args[i2] == ts->reg)) {
                        goto allocate_in_reg;
                    }
                }
            }
        }
        reg = ts->reg;
        if (tcg_regset_test_reg(arg_ct->u.regs, reg)) {
            /* nothing to do : the constraint is satisfied */
        } else {
        allocate_in_reg:
            /* allocate a new register matching the constraint 
               and move the temporary register into it */
            reg = tcg_reg_alloc(s, arg_ct->u.regs, i_allocated_regs,
                                ts->indirect_base);
            tcg_out_mov(s, ts->type, reg, ts->reg);
        }
        new_args[i] = reg;
        const_args[i] = 0;
        tcg_regset_set_reg(i_allocated_regs, reg);
    iarg_end: ;
    }
    
    /* mark dead temporaries and free the associated registers */
    for (i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
        if (IS_DEAD_ARG(i)) {
            temp_dead(s, &s->temps[args[i]]);
        }
    }

    if (def->flags & TCG_OPF_BB_END) {
        tcg_reg_alloc_bb_end(s, i_allocated_regs);
    } else {
        if (def->flags & TCG_OPF_CALL_CLOBBER) {
            /* XXX: permit generic clobber register list ? */ 
            for (i = 0; i < TCG_TARGET_NB_REGS; i++) {
                if (tcg_regset_test_reg(tcg_target_call_clobber_regs, i)) {
                    tcg_reg_free(s, i, i_allocated_regs);
                }
            }
        }
        if (def->flags & TCG_OPF_SIDE_EFFECTS) {
            /* sync globals if the op has side effects and might trigger
               an exception. */
            sync_globals(s, i_allocated_regs);
        }
        
        /* satisfy the output constraints */
        for(k = 0; k < nb_oargs; k++) {
            i = def->sorted_args[k];
            arg = args[i];
            arg_ct = &def->args_ct[i];
            ts = &s->temps[arg];
            if ((arg_ct->ct & TCG_CT_ALIAS)
                && !const_args[arg_ct->alias_index]) {
                reg = new_args[arg_ct->alias_index];
            } else if (arg_ct->ct & TCG_CT_NEWREG) {
                reg = tcg_reg_alloc(s, arg_ct->u.regs,
                                    i_allocated_regs | o_allocated_regs,
                                    ts->indirect_base);
            } else {
                /* if fixed register, we try to use it */
                reg = ts->reg;
                if (ts->fixed_reg &&
                    tcg_regset_test_reg(arg_ct->u.regs, reg)) {
                    goto oarg_end;
                }
                reg = tcg_reg_alloc(s, arg_ct->u.regs, o_allocated_regs,
                                    ts->indirect_base);
            }
            tcg_regset_set_reg(o_allocated_regs, reg);
            /* if a fixed register is used, then a move will be done afterwards */
            if (!ts->fixed_reg) {
                if (ts->val_type == TEMP_VAL_REG) {
                    s->reg_to_temp[ts->reg] = NULL;
                }
                ts->val_type = TEMP_VAL_REG;
                ts->reg = reg;
                /* temp value is modified, so the value kept in memory is
                   potentially not the same */
                ts->mem_coherent = 0;
                s->reg_to_temp[reg] = ts;
            }
        oarg_end:
            new_args[i] = reg;
        }
    }

    /* emit instruction */
    tcg_out_op(s, opc, new_args, const_args);
    
    /* move the outputs in the correct register if needed */
    for(i = 0; i < nb_oargs; i++) {
        ts = &s->temps[args[i]];
        reg = new_args[i];
        if (ts->fixed_reg && ts->reg != reg) {
            tcg_out_mov(s, ts->type, ts->reg, reg);
        }
        if (NEED_SYNC_ARG(i)) {
            temp_sync(s, ts, o_allocated_regs, IS_DEAD_ARG(i));
        } else if (IS_DEAD_ARG(i)) {
            temp_dead(s, ts);
        }
    }
}

#ifdef TCG_TARGET_STACK_GROWSUP
#define STACK_DIR(x) (-(x))
#else
#define STACK_DIR(x) (x)
#endif

static void tcg_reg_alloc_call(TCGContext *s, int nb_oargs, int nb_iargs,
                               const TCGArg * const args, TCGLifeData arg_life)
{
    int flags, nb_regs, i;
    TCGReg reg;
    TCGArg arg;
    TCGTemp *ts;
    intptr_t stack_offset;
    size_t call_stack_size;
    tcg_insn_unit *func_addr;
    int allocate_args;
    TCGRegSet allocated_regs;

    func_addr = (tcg_insn_unit *)(intptr_t)args[nb_oargs + nb_iargs];
    flags = args[nb_oargs + nb_iargs + 1];

    nb_regs = ARRAY_SIZE(tcg_target_call_iarg_regs);
    if (nb_regs > nb_iargs) {
        nb_regs = nb_iargs;
    }

    /* assign stack slots first */
    call_stack_size = (nb_iargs - nb_regs) * sizeof(tcg_target_long);
    call_stack_size = (call_stack_size + TCG_TARGET_STACK_ALIGN - 1) & 
        ~(TCG_TARGET_STACK_ALIGN - 1);
    allocate_args = (call_stack_size > TCG_STATIC_CALL_ARGS_SIZE);
    if (allocate_args) {
        /* XXX: if more than TCG_STATIC_CALL_ARGS_SIZE is needed,
           preallocate call stack */
        tcg_abort();
    }

    stack_offset = TCG_TARGET_CALL_STACK_OFFSET;
    for(i = nb_regs; i < nb_iargs; i++) {
        arg = args[nb_oargs + i];
#ifdef TCG_TARGET_STACK_GROWSUP
        stack_offset -= sizeof(tcg_target_long);
#endif
        if (arg != TCG_CALL_DUMMY_ARG) {
            ts = &s->temps[arg];
            temp_load(s, ts, tcg_target_available_regs[ts->type],
                      s->reserved_regs);
            tcg_out_st(s, ts->type, ts->reg, TCG_REG_CALL_STACK, stack_offset);
        }
#ifndef TCG_TARGET_STACK_GROWSUP
        stack_offset += sizeof(tcg_target_long);
#endif
    }
    
    /* assign input registers */
    tcg_regset_set(allocated_regs, s->reserved_regs);
    for(i = 0; i < nb_regs; i++) {
        arg = args[nb_oargs + i];
        if (arg != TCG_CALL_DUMMY_ARG) {
            ts = &s->temps[arg];
            reg = tcg_target_call_iarg_regs[i];
            tcg_reg_free(s, reg, allocated_regs);

            if (ts->val_type == TEMP_VAL_REG) {
                if (ts->reg != reg) {
                    tcg_out_mov(s, ts->type, reg, ts->reg);
                }
            } else {
                TCGRegSet arg_set;

                tcg_regset_clear(arg_set);
                tcg_regset_set_reg(arg_set, reg);
                temp_load(s, ts, arg_set, allocated_regs);
            }

            tcg_regset_set_reg(allocated_regs, reg);
        }
    }
    
    /* mark dead temporaries and free the associated registers */
    for(i = nb_oargs; i < nb_iargs + nb_oargs; i++) {
        if (IS_DEAD_ARG(i)) {
            temp_dead(s, &s->temps[args[i]]);
        }
    }
    
    /* clobber call registers */
    for (i = 0; i < TCG_TARGET_NB_REGS; i++) {
        if (tcg_regset_test_reg(tcg_target_call_clobber_regs, i)) {
            tcg_reg_free(s, i, allocated_regs);
        }
    }

    /* Save globals if they might be written by the helper, sync them if
       they might be read. */
    if (flags & TCG_CALL_NO_READ_GLOBALS) {
        /* Nothing to do */
    } else if (flags & TCG_CALL_NO_WRITE_GLOBALS) {
        sync_globals(s, allocated_regs);
    } else {
        save_globals(s, allocated_regs);
    }

    tcg_out_call(s, func_addr);

    /* assign output registers and emit moves if needed */
    for(i = 0; i < nb_oargs; i++) {
        arg = args[i];
        ts = &s->temps[arg];
        reg = tcg_target_call_oarg_regs[i];
        tcg_debug_assert(s->reg_to_temp[reg] == NULL);

        if (ts->fixed_reg) {
            if (ts->reg != reg) {
                tcg_out_mov(s, ts->type, ts->reg, reg);
            }
        } else {
            if (ts->val_type == TEMP_VAL_REG) {
                s->reg_to_temp[ts->reg] = NULL;
            }
            ts->val_type = TEMP_VAL_REG;
            ts->reg = reg;
            ts->mem_coherent = 0;
            s->reg_to_temp[reg] = ts;
            if (NEED_SYNC_ARG(i)) {
                temp_sync(s, ts, allocated_regs, IS_DEAD_ARG(i));
            } else if (IS_DEAD_ARG(i)) {
                temp_dead(s, ts);
            }
        }
    }
}

#ifdef CONFIG_PROFILER

static int64_t tcg_table_op_count[NB_OPS];

void tcg_dump_op_count(FILE *f, fprintf_function cpu_fprintf)
{
    int i;

    for (i = 0; i < NB_OPS; i++) {
        cpu_fprintf(f, "%s %" PRId64 "\n", tcg_op_defs[i].name,
                    tcg_table_op_count[i]);
    }
}
#else
void tcg_dump_op_count(FILE *f, fprintf_function cpu_fprintf)
{
    cpu_fprintf(f, "[TCG profiler not compiled]\n");
}
#endif


int tcg_gen_code(TCGContext *s, TranslationBlock *tb)
{
    int i, oi, oi_next, num_insns;

#ifdef CONFIG_PROFILER
    {
        int n;

        n = s->gen_op_buf[0].prev + 1;
        s->op_count += n;
        if (n > s->op_count_max) {
            s->op_count_max = n;
        }

        n = s->nb_temps;
        s->temp_count += n;
        if (n > s->temp_count_max) {
            s->temp_count_max = n;
        }
    }
#endif

#ifdef DEBUG_DISAS
    if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP)
                 && qemu_log_in_addr_range(tb->pc))) {
        qemu_log_lock();
        qemu_log("OP:\n");
        tcg_dump_ops(s);
        qemu_log("\n");
        qemu_log_unlock();
    }
#endif

#ifdef CONFIG_PROFILER
    s->opt_time -= profile_getclock();
#endif

#ifdef USE_TCG_OPTIMIZATIONS
    tcg_optimize(s);
#endif

#ifdef CONFIG_PROFILER
    s->opt_time += profile_getclock();
    s->la_time -= profile_getclock();
#endif

    {
        uint8_t *temp_state = tcg_malloc(s->nb_temps + s->nb_indirects);

        liveness_pass_1(s, temp_state);

        if (s->nb_indirects > 0) {
#ifdef DEBUG_DISAS
            if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP_IND)
                         && qemu_log_in_addr_range(tb->pc))) {
                qemu_log_lock();
                qemu_log("OP before indirect lowering:\n");
                tcg_dump_ops(s);
                qemu_log("\n");
                qemu_log_unlock();
            }
#endif
            /* Replace indirect temps with direct temps.  */
            if (liveness_pass_2(s, temp_state)) {
                /* If changes were made, re-run liveness.  */
                liveness_pass_1(s, temp_state);
            }
        }
    }

#ifdef CONFIG_PROFILER
    s->la_time += profile_getclock();
#endif

#ifdef DEBUG_DISAS
    if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP_OPT)
                 && qemu_log_in_addr_range(tb->pc))) {
        qemu_log_lock();
        qemu_log("OP after optimization and liveness analysis:\n");
        tcg_dump_ops(s);
        qemu_log("\n");
        qemu_log_unlock();
    }
#endif

    tcg_reg_alloc_start(s);

    s->code_buf = tb->tc_ptr;
    s->code_ptr = tb->tc_ptr;

    tcg_out_tb_init(s);

    num_insns = -1;
    for (oi = s->gen_op_buf[0].next; oi != 0; oi = oi_next) {
        TCGOp * const op = &s->gen_op_buf[oi];
        TCGArg * const args = &s->gen_opparam_buf[op->args];
        TCGOpcode opc = op->opc;
        const TCGOpDef *def = &tcg_op_defs[opc];
        TCGLifeData arg_life = op->life;

        oi_next = op->next;
#ifdef CONFIG_PROFILER
        tcg_table_op_count[opc]++;
#endif

        switch (opc) {
        case INDEX_op_mov_i32:
        case INDEX_op_mov_i64:
            tcg_reg_alloc_mov(s, def, args, arg_life);
            break;
        case INDEX_op_movi_i32:
        case INDEX_op_movi_i64:
            tcg_reg_alloc_movi(s, args, arg_life);
            break;
        case INDEX_op_insn_start:
            if (num_insns >= 0) {
                s->gen_insn_end_off[num_insns] = tcg_current_code_size(s);
            }
            num_insns++;
            for (i = 0; i < TARGET_INSN_START_WORDS; ++i) {
                target_ulong a;
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
                a = ((target_ulong)args[i * 2 + 1] << 32) | args[i * 2];
#else
                a = args[i];
#endif
                s->gen_insn_data[num_insns][i] = a;
            }
            break;
        case INDEX_op_discard:
            temp_dead(s, &s->temps[args[0]]);
            break;
        case INDEX_op_set_label:
            tcg_reg_alloc_bb_end(s, s->reserved_regs);
            tcg_out_label(s, arg_label(args[0]), s->code_ptr);
            break;
        case INDEX_op_call:
            tcg_reg_alloc_call(s, op->callo, op->calli, args, arg_life);
            break;
        default:
            /* Sanity check that we've not introduced any unhandled opcodes. */
            if (def->flags & TCG_OPF_NOT_PRESENT) {
                tcg_abort();
            }
            /* Note: in order to speed up the code, it would be much
               faster to have specialized register allocator functions for
               some common argument patterns */
            tcg_reg_alloc_op(s, def, opc, args, arg_life);
            break;
        }
#ifdef CONFIG_DEBUG_TCG
        check_regs(s);
#endif
        /* Test for (pending) buffer overflow.  The assumption is that any
           one operation beginning below the high water mark cannot overrun
           the buffer completely.  Thus we can test for overflow after
           generating code without having to check during generation.  */
        if (unlikely((void *)s->code_ptr > s->code_gen_highwater)) {
            return -1;
        }
    }
    tcg_debug_assert(num_insns >= 0);
    s->gen_insn_end_off[num_insns] = tcg_current_code_size(s);

    /* Generate TB finalization at the end of block */
    if (!tcg_out_tb_finalize(s)) {
        return -1;
    }

    /* flush instruction cache */
    flush_icache_range((uintptr_t)s->code_buf, (uintptr_t)s->code_ptr);

    return tcg_current_code_size(s);
}

#ifdef CONFIG_PROFILER
void tcg_dump_info(FILE *f, fprintf_function cpu_fprintf)
{
    TCGContext *s = &tcg_ctx;
    int64_t tb_count = s->tb_count;
    int64_t tb_div_count = tb_count ? tb_count : 1;
    int64_t tot = s->interm_time + s->code_time;

    cpu_fprintf(f, "JIT cycles          %" PRId64 " (%0.3f s at 2.4 GHz)\n",
                tot, tot / 2.4e9);
    cpu_fprintf(f, "translated TBs      %" PRId64 " (aborted=%" PRId64 " %0.1f%%)\n", 
                tb_count, s->tb_count1 - tb_count,
                (double)(s->tb_count1 - s->tb_count)
                / (s->tb_count1 ? s->tb_count1 : 1) * 100.0);
    cpu_fprintf(f, "avg ops/TB          %0.1f max=%d\n", 
                (double)s->op_count / tb_div_count, s->op_count_max);
    cpu_fprintf(f, "deleted ops/TB      %0.2f\n",
                (double)s->del_op_count / tb_div_count);
    cpu_fprintf(f, "avg temps/TB        %0.2f max=%d\n",
                (double)s->temp_count / tb_div_count, s->temp_count_max);
    cpu_fprintf(f, "avg host code/TB    %0.1f\n",
                (double)s->code_out_len / tb_div_count);
    cpu_fprintf(f, "avg search data/TB  %0.1f\n",
                (double)s->search_out_len / tb_div_count);
    
    cpu_fprintf(f, "cycles/op           %0.1f\n", 
                s->op_count ? (double)tot / s->op_count : 0);
    cpu_fprintf(f, "cycles/in byte      %0.1f\n", 
                s->code_in_len ? (double)tot / s->code_in_len : 0);
    cpu_fprintf(f, "cycles/out byte     %0.1f\n", 
                s->code_out_len ? (double)tot / s->code_out_len : 0);
    cpu_fprintf(f, "cycles/search byte     %0.1f\n",
                s->search_out_len ? (double)tot / s->search_out_len : 0);
    if (tot == 0) {
        tot = 1;
    }
    cpu_fprintf(f, "  gen_interm time   %0.1f%%\n", 
                (double)s->interm_time / tot * 100.0);
    cpu_fprintf(f, "  gen_code time     %0.1f%%\n", 
                (double)s->code_time / tot * 100.0);
    cpu_fprintf(f, "optim./code time    %0.1f%%\n",
                (double)s->opt_time / (s->code_time ? s->code_time : 1)
                * 100.0);
    cpu_fprintf(f, "liveness/code time  %0.1f%%\n", 
                (double)s->la_time / (s->code_time ? s->code_time : 1) * 100.0);
    cpu_fprintf(f, "cpu_restore count   %" PRId64 "\n",
                s->restore_count);
    cpu_fprintf(f, "  avg cycles        %0.1f\n",
                s->restore_count ? (double)s->restore_time / s->restore_count : 0);
}
#else
void tcg_dump_info(FILE *f, fprintf_function cpu_fprintf)
{
    cpu_fprintf(f, "[TCG profiler not compiled]\n");
}
#endif

#ifdef ELF_HOST_MACHINE
/* In order to use this feature, the backend needs to do three things:

   (1) Define ELF_HOST_MACHINE to indicate both what value to
       put into the ELF image and to indicate support for the feature.

   (2) Define tcg_register_jit.  This should create a buffer containing
       the contents of a .debug_frame section that describes the post-
       prologue unwind info for the tcg machine.

   (3) Call tcg_register_jit_int, with the constructed .debug_frame.
*/

/* Begin GDB interface.  THE FOLLOWING MUST MATCH GDB DOCS.  */
typedef enum {
    JIT_NOACTION = 0,
    JIT_REGISTER_FN,
    JIT_UNREGISTER_FN
} jit_actions_t;

struct jit_code_entry {
    struct jit_code_entry *next_entry;
    struct jit_code_entry *prev_entry;
    const void *symfile_addr;
    uint64_t symfile_size;
};

struct jit_descriptor {
    uint32_t version;
    uint32_t action_flag;
    struct jit_code_entry *relevant_entry;
    struct jit_code_entry *first_entry;
};

void __jit_debug_register_code(void) __attribute__((noinline));
void __jit_debug_register_code(void)
{
    asm("");
}

/* Must statically initialize the version, because GDB may check
   the version before we can set it.  */
struct jit_descriptor __jit_debug_descriptor = { 1, 0, 0, 0 };

/* End GDB interface.  */

static int find_string(const char *strtab, const char *str)
{
    const char *p = strtab + 1;

    while (1) {
        if (strcmp(p, str) == 0) {
            return p - strtab;
        }
        p += strlen(p) + 1;
    }
}

static void tcg_register_jit_int(void *buf_ptr, size_t buf_size,
                                 const void *debug_frame,
                                 size_t debug_frame_size)
{
    struct __attribute__((packed)) DebugInfo {
        uint32_t  len;
        uint16_t  version;
        uint32_t  abbrev;
        uint8_t   ptr_size;
        uint8_t   cu_die;
        uint16_t  cu_lang;
        uintptr_t cu_low_pc;
        uintptr_t cu_high_pc;
        uint8_t   fn_die;
        char      fn_name[16];
        uintptr_t fn_low_pc;
        uintptr_t fn_high_pc;
        uint8_t   cu_eoc;
    };

    struct ElfImage {
        ElfW(Ehdr) ehdr;
        ElfW(Phdr) phdr;
        ElfW(Shdr) shdr[7];
        ElfW(Sym)  sym[2];
        struct DebugInfo di;
        uint8_t    da[24];
        char       str[80];
    };

    struct ElfImage *img;

    static const struct ElfImage img_template = {
        .ehdr = {
            .e_ident[EI_MAG0] = ELFMAG0,
            .e_ident[EI_MAG1] = ELFMAG1,
            .e_ident[EI_MAG2] = ELFMAG2,
            .e_ident[EI_MAG3] = ELFMAG3,
            .e_ident[EI_CLASS] = ELF_CLASS,
            .e_ident[EI_DATA] = ELF_DATA,
            .e_ident[EI_VERSION] = EV_CURRENT,
            .e_type = ET_EXEC,
            .e_machine = ELF_HOST_MACHINE,
            .e_version = EV_CURRENT,
            .e_phoff = offsetof(struct ElfImage, phdr),
            .e_shoff = offsetof(struct ElfImage, shdr),
            .e_ehsize = sizeof(ElfW(Shdr)),
            .e_phentsize = sizeof(ElfW(Phdr)),
            .e_phnum = 1,
            .e_shentsize = sizeof(ElfW(Shdr)),
            .e_shnum = ARRAY_SIZE(img->shdr),
            .e_shstrndx = ARRAY_SIZE(img->shdr) - 1,
#ifdef ELF_HOST_FLAGS
            .e_flags = ELF_HOST_FLAGS,
#endif
#ifdef ELF_OSABI
            .e_ident[EI_OSABI] = ELF_OSABI,
#endif
        },
        .phdr = {
            .p_type = PT_LOAD,
            .p_flags = PF_X,
        },
        .shdr = {
            [0] = { .sh_type = SHT_NULL },
            /* Trick: The contents of code_gen_buffer are not present in
               this fake ELF file; that got allocated elsewhere.  Therefore
               we mark .text as SHT_NOBITS (similar to .bss) so that readers
               will not look for contents.  We can record any address.  */
            [1] = { /* .text */
                .sh_type = SHT_NOBITS,
                .sh_flags = SHF_EXECINSTR | SHF_ALLOC,
            },
            [2] = { /* .debug_info */
                .sh_type = SHT_PROGBITS,
                .sh_offset = offsetof(struct ElfImage, di),
                .sh_size = sizeof(struct DebugInfo),
            },
            [3] = { /* .debug_abbrev */
                .sh_type = SHT_PROGBITS,
                .sh_offset = offsetof(struct ElfImage, da),
                .sh_size = sizeof(img->da),
            },
            [4] = { /* .debug_frame */
                .sh_type = SHT_PROGBITS,
                .sh_offset = sizeof(struct ElfImage),
            },
            [5] = { /* .symtab */
                .sh_type = SHT_SYMTAB,
                .sh_offset = offsetof(struct ElfImage, sym),
                .sh_size = sizeof(img->sym),
                .sh_info = 1,
                .sh_link = ARRAY_SIZE(img->shdr) - 1,
                .sh_entsize = sizeof(ElfW(Sym)),
            },
            [6] = { /* .strtab */
                .sh_type = SHT_STRTAB,
                .sh_offset = offsetof(struct ElfImage, str),
                .sh_size = sizeof(img->str),
            }
        },
        .sym = {
            [1] = { /* code_gen_buffer */
                .st_info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC),
                .st_shndx = 1,
            }
        },
        .di = {
            .len = sizeof(struct DebugInfo) - 4,
            .version = 2,
            .ptr_size = sizeof(void *),
            .cu_die = 1,
            .cu_lang = 0x8001,  /* DW_LANG_Mips_Assembler */
            .fn_die = 2,
            .fn_name = "code_gen_buffer"
        },
        .da = {
            1,          /* abbrev number (the cu) */
            0x11, 1,    /* DW_TAG_compile_unit, has children */
            0x13, 0x5,  /* DW_AT_language, DW_FORM_data2 */
            0x11, 0x1,  /* DW_AT_low_pc, DW_FORM_addr */
            0x12, 0x1,  /* DW_AT_high_pc, DW_FORM_addr */
            0, 0,       /* end of abbrev */
            2,          /* abbrev number (the fn) */
            0x2e, 0,    /* DW_TAG_subprogram, no children */
            0x3, 0x8,   /* DW_AT_name, DW_FORM_string */
            0x11, 0x1,  /* DW_AT_low_pc, DW_FORM_addr */
            0x12, 0x1,  /* DW_AT_high_pc, DW_FORM_addr */
            0, 0,       /* end of abbrev */
            0           /* no more abbrev */
        },
        .str = "\0" ".text\0" ".debug_info\0" ".debug_abbrev\0"
               ".debug_frame\0" ".symtab\0" ".strtab\0" "code_gen_buffer",
    };

    /* We only need a single jit entry; statically allocate it.  */
    static struct jit_code_entry one_entry;

    uintptr_t buf = (uintptr_t)buf_ptr;
    size_t img_size = sizeof(struct ElfImage) + debug_frame_size;
    DebugFrameHeader *dfh;

    img = g_malloc(img_size);
    *img = img_template;

    img->phdr.p_vaddr = buf;
    img->phdr.p_paddr = buf;
    img->phdr.p_memsz = buf_size;

    img->shdr[1].sh_name = find_string(img->str, ".text");
    img->shdr[1].sh_addr = buf;
    img->shdr[1].sh_size = buf_size;

    img->shdr[2].sh_name = find_string(img->str, ".debug_info");
    img->shdr[3].sh_name = find_string(img->str, ".debug_abbrev");

    img->shdr[4].sh_name = find_string(img->str, ".debug_frame");
    img->shdr[4].sh_size = debug_frame_size;

    img->shdr[5].sh_name = find_string(img->str, ".symtab");
    img->shdr[6].sh_name = find_string(img->str, ".strtab");

    img->sym[1].st_name = find_string(img->str, "code_gen_buffer");
    img->sym[1].st_value = buf;
    img->sym[1].st_size = buf_size;

    img->di.cu_low_pc = buf;
    img->di.cu_high_pc = buf + buf_size;
    img->di.fn_low_pc = buf;
    img->di.fn_high_pc = buf + buf_size;

    dfh = (DebugFrameHeader *)(img + 1);
    memcpy(dfh, debug_frame, debug_frame_size);
    dfh->fde.func_start = buf;
    dfh->fde.func_len = buf_size;

#ifdef DEBUG_JIT
    /* Enable this block to be able to debug the ELF image file creation.
       One can use readelf, objdump, or other inspection utilities.  */
    {
        FILE *f = fopen("/tmp/qemu.jit", "w+b");
        if (f) {
            if (fwrite(img, img_size, 1, f) != img_size) {
                /* Avoid stupid unused return value warning for fwrite.  */
            }
            fclose(f);
        }
    }
#endif

    one_entry.symfile_addr = img;
    one_entry.symfile_size = img_size;

    __jit_debug_descriptor.action_flag = JIT_REGISTER_FN;
    __jit_debug_descriptor.relevant_entry = &one_entry;
    __jit_debug_descriptor.first_entry = &one_entry;
    __jit_debug_register_code();
}
#else
/* No support for the feature.  Provide the entry point expected by exec.c,
   and implement the internal function we declared earlier.  */

static void tcg_register_jit_int(void *buf, size_t size,
                                 const void *debug_frame,
                                 size_t debug_frame_size)
{
}

void tcg_register_jit(void *buf, size_t buf_size)
{
}
#endif /* ELF_HOST_MACHINE */
