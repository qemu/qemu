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

#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "qemu/host-utils.h"
#include "qemu/qemu-print.h"
#include "qemu/timer.h"
#include "qemu/cacheflush.h"

/* Note: the long term plan is to reduce the dependencies on the QEMU
   CPU definitions. Currently they are used for qemu_ld/st
   instructions */
#define NO_CPU_IO_DEFS
#include "cpu.h"

#include "exec/exec-all.h"

#if !defined(CONFIG_USER_ONLY)
#include "hw/boards.h"
#endif

#include "tcg/tcg-op.h"

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
#include "sysemu/sysemu.h"

#include "qemuafl/common.h"

/* Forward declarations for functions declared in tcg-target.c.inc and
   used here. */
static void tcg_target_init(TCGContext *s);
static void tcg_target_qemu_prologue(TCGContext *s);
static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
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

static void tcg_register_jit_int(const void *buf, size_t size,
                                 const void *debug_frame,
                                 size_t debug_frame_size)
    __attribute__((unused));

/* Forward declarations for functions declared and used in tcg-target.c.inc. */
static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1,
                       intptr_t arg2);
static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg);
static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg);
static void tcg_out_op(TCGContext *s, TCGOpcode opc, const TCGArg *args,
                       const int *const_args);
#if TCG_TARGET_MAYBE_vec
static bool tcg_out_dup_vec(TCGContext *s, TCGType type, unsigned vece,
                            TCGReg dst, TCGReg src);
static bool tcg_out_dupm_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg dst, TCGReg base, intptr_t offset);
static void tcg_out_dupi_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg dst, int64_t arg);
static void tcg_out_vec_op(TCGContext *s, TCGOpcode opc, unsigned vecl,
                           unsigned vece, const TCGArg *args,
                           const int *const_args);
#else
static inline bool tcg_out_dup_vec(TCGContext *s, TCGType type, unsigned vece,
                                   TCGReg dst, TCGReg src)
{
    g_assert_not_reached();
}
static inline bool tcg_out_dupm_vec(TCGContext *s, TCGType type, unsigned vece,
                                    TCGReg dst, TCGReg base, intptr_t offset)
{
    g_assert_not_reached();
}
static inline void tcg_out_dupi_vec(TCGContext *s, TCGType type, unsigned vece,
                                    TCGReg dst, int64_t arg)
{
    g_assert_not_reached();
}
static inline void tcg_out_vec_op(TCGContext *s, TCGOpcode opc, unsigned vecl,
                                  unsigned vece, const TCGArg *args,
                                  const int *const_args)
{
    g_assert_not_reached();
}
#endif
static void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg, TCGReg arg1,
                       intptr_t arg2);
static bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                        TCGReg base, intptr_t ofs);
static void tcg_out_call(TCGContext *s, const tcg_insn_unit *target);
static int tcg_target_const_match(tcg_target_long val, TCGType type,
                                  const TCGArgConstraint *arg_ct);
#ifdef TCG_TARGET_NEED_LDST_LABELS
static int tcg_out_ldst_finalize(TCGContext *s);
#endif

#define TCG_HIGHWATER 1024

static TCGContext **tcg_ctxs;
static unsigned int n_tcg_ctxs;
TCGv_env cpu_env = 0;
const void *tcg_code_gen_epilogue;
uintptr_t tcg_splitwx_diff;

#ifndef CONFIG_TCG_INTERPRETER
tcg_prologue_fn *tcg_qemu_tb_exec;
#endif

struct tcg_region_tree {
    QemuMutex lock;
    GTree *tree;
    /* padding to avoid false sharing is computed at run-time */
};

/*
 * We divide code_gen_buffer into equally-sized "regions" that TCG threads
 * dynamically allocate from as demand dictates. Given appropriate region
 * sizing, this minimizes flushes even when some TCG threads generate a lot
 * more code than others.
 */
struct tcg_region_state {
    QemuMutex lock;

    /* fields set at init time */
    void *start;
    void *start_aligned;
    void *end;
    size_t n;
    size_t size; /* size of one region */
    size_t stride; /* .size + guard size */

    /* fields protected by the lock */
    size_t current; /* current region index */
    size_t agg_size_full; /* aggregate size of full regions */
};

static struct tcg_region_state region;
/*
 * This is an array of struct tcg_region_tree's, with padding.
 * We use void * to simplify the computation of region_trees[i]; each
 * struct is found every tree_size bytes.
 */
static void *region_trees;
static size_t tree_size;
static TCGRegSet tcg_target_available_regs[TCG_TYPE_COUNT];
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
    TCGRelocation *r = tcg_malloc(sizeof(TCGRelocation));

    r->type = type;
    r->ptr = code_ptr;
    r->addend = addend;
    QSIMPLEQ_INSERT_TAIL(&l->relocs, r, next);
}

static void tcg_out_label(TCGContext *s, TCGLabel *l)
{
    tcg_debug_assert(!l->has_value);
    l->has_value = 1;
    l->u.value_ptr = tcg_splitwx_to_rx(s->code_ptr);
}

TCGLabel *gen_new_label(void)
{
    TCGContext *s = tcg_ctx;
    TCGLabel *l = tcg_malloc(sizeof(TCGLabel));

    memset(l, 0, sizeof(TCGLabel));
    l->id = s->nb_labels++;
    QSIMPLEQ_INIT(&l->relocs);

    QSIMPLEQ_INSERT_TAIL(&s->labels, l, next);

    return l;
}

static bool tcg_resolve_relocs(TCGContext *s)
{
    TCGLabel *l;

    QSIMPLEQ_FOREACH(l, &s->labels, next) {
        TCGRelocation *r;
        uintptr_t value = l->u.value;

        QSIMPLEQ_FOREACH(r, &l->relocs, next) {
            if (!patch_reloc(r->ptr, r->type, value, r->addend)) {
                return false;
            }
        }
    }
    return true;
}

static void set_jmp_reset_offset(TCGContext *s, int which)
{
    /*
     * We will check for overflow at the end of the opcode loop in
     * tcg_gen_code, where we bound tcg_current_code_size to UINT16_MAX.
     */
    s->tb_jmp_reset_offset[which] = tcg_current_code_size(s);
}

#define C_PFX1(P, A)                    P##A
#define C_PFX2(P, A, B)                 P##A##_##B
#define C_PFX3(P, A, B, C)              P##A##_##B##_##C
#define C_PFX4(P, A, B, C, D)           P##A##_##B##_##C##_##D
#define C_PFX5(P, A, B, C, D, E)        P##A##_##B##_##C##_##D##_##E
#define C_PFX6(P, A, B, C, D, E, F)     P##A##_##B##_##C##_##D##_##E##_##F

/* Define an enumeration for the various combinations. */

#define C_O0_I1(I1)                     C_PFX1(c_o0_i1_, I1),
#define C_O0_I2(I1, I2)                 C_PFX2(c_o0_i2_, I1, I2),
#define C_O0_I3(I1, I2, I3)             C_PFX3(c_o0_i3_, I1, I2, I3),
#define C_O0_I4(I1, I2, I3, I4)         C_PFX4(c_o0_i4_, I1, I2, I3, I4),

#define C_O1_I1(O1, I1)                 C_PFX2(c_o1_i1_, O1, I1),
#define C_O1_I2(O1, I1, I2)             C_PFX3(c_o1_i2_, O1, I1, I2),
#define C_O1_I3(O1, I1, I2, I3)         C_PFX4(c_o1_i3_, O1, I1, I2, I3),
#define C_O1_I4(O1, I1, I2, I3, I4)     C_PFX5(c_o1_i4_, O1, I1, I2, I3, I4),

#define C_N1_I2(O1, I1, I2)             C_PFX3(c_n1_i2_, O1, I1, I2),

#define C_O2_I1(O1, O2, I1)             C_PFX3(c_o2_i1_, O1, O2, I1),
#define C_O2_I2(O1, O2, I1, I2)         C_PFX4(c_o2_i2_, O1, O2, I1, I2),
#define C_O2_I3(O1, O2, I1, I2, I3)     C_PFX5(c_o2_i3_, O1, O2, I1, I2, I3),
#define C_O2_I4(O1, O2, I1, I2, I3, I4) C_PFX6(c_o2_i4_, O1, O2, I1, I2, I3, I4),

typedef enum {
#include "tcg-target-con-set.h"
} TCGConstraintSetIndex;

static TCGConstraintSetIndex tcg_target_op_def(TCGOpcode);

#undef C_O0_I1
#undef C_O0_I2
#undef C_O0_I3
#undef C_O0_I4
#undef C_O1_I1
#undef C_O1_I2
#undef C_O1_I3
#undef C_O1_I4
#undef C_N1_I2
#undef C_O2_I1
#undef C_O2_I2
#undef C_O2_I3
#undef C_O2_I4

/* Put all of the constraint sets into an array, indexed by the enum. */

#define C_O0_I1(I1)                     { .args_ct_str = { #I1 } },
#define C_O0_I2(I1, I2)                 { .args_ct_str = { #I1, #I2 } },
#define C_O0_I3(I1, I2, I3)             { .args_ct_str = { #I1, #I2, #I3 } },
#define C_O0_I4(I1, I2, I3, I4)         { .args_ct_str = { #I1, #I2, #I3, #I4 } },

#define C_O1_I1(O1, I1)                 { .args_ct_str = { #O1, #I1 } },
#define C_O1_I2(O1, I1, I2)             { .args_ct_str = { #O1, #I1, #I2 } },
#define C_O1_I3(O1, I1, I2, I3)         { .args_ct_str = { #O1, #I1, #I2, #I3 } },
#define C_O1_I4(O1, I1, I2, I3, I4)     { .args_ct_str = { #O1, #I1, #I2, #I3, #I4 } },

#define C_N1_I2(O1, I1, I2)             { .args_ct_str = { "&" #O1, #I1, #I2 } },

#define C_O2_I1(O1, O2, I1)             { .args_ct_str = { #O1, #O2, #I1 } },
#define C_O2_I2(O1, O2, I1, I2)         { .args_ct_str = { #O1, #O2, #I1, #I2 } },
#define C_O2_I3(O1, O2, I1, I2, I3)     { .args_ct_str = { #O1, #O2, #I1, #I2, #I3 } },
#define C_O2_I4(O1, O2, I1, I2, I3, I4) { .args_ct_str = { #O1, #O2, #I1, #I2, #I3, #I4 } },

static const TCGTargetOpDef constraint_sets[] = {
#include "tcg-target-con-set.h"
};


#undef C_O0_I1
#undef C_O0_I2
#undef C_O0_I3
#undef C_O0_I4
#undef C_O1_I1
#undef C_O1_I2
#undef C_O1_I3
#undef C_O1_I4
#undef C_N1_I2
#undef C_O2_I1
#undef C_O2_I2
#undef C_O2_I3
#undef C_O2_I4

/* Expand the enumerator to be returned from tcg_target_op_def(). */

#define C_O0_I1(I1)                     C_PFX1(c_o0_i1_, I1)
#define C_O0_I2(I1, I2)                 C_PFX2(c_o0_i2_, I1, I2)
#define C_O0_I3(I1, I2, I3)             C_PFX3(c_o0_i3_, I1, I2, I3)
#define C_O0_I4(I1, I2, I3, I4)         C_PFX4(c_o0_i4_, I1, I2, I3, I4)

#define C_O1_I1(O1, I1)                 C_PFX2(c_o1_i1_, O1, I1)
#define C_O1_I2(O1, I1, I2)             C_PFX3(c_o1_i2_, O1, I1, I2)
#define C_O1_I3(O1, I1, I2, I3)         C_PFX4(c_o1_i3_, O1, I1, I2, I3)
#define C_O1_I4(O1, I1, I2, I3, I4)     C_PFX5(c_o1_i4_, O1, I1, I2, I3, I4)

#define C_N1_I2(O1, I1, I2)             C_PFX3(c_n1_i2_, O1, I1, I2)

#define C_O2_I1(O1, O2, I1)             C_PFX3(c_o2_i1_, O1, O2, I1)
#define C_O2_I2(O1, O2, I1, I2)         C_PFX4(c_o2_i2_, O1, O2, I1, I2)
#define C_O2_I3(O1, O2, I1, I2, I3)     C_PFX5(c_o2_i3_, O1, O2, I1, I2, I3)
#define C_O2_I4(O1, O2, I1, I2, I3, I4) C_PFX6(c_o2_i4_, O1, O2, I1, I2, I3, I4)

#include "tcg-target.c.inc"

/* compare a pointer @ptr and a tb_tc @s */
static int ptr_cmp_tb_tc(const void *ptr, const struct tb_tc *s)
{
    if (ptr >= s->ptr + s->size) {
        return 1;
    } else if (ptr < s->ptr) {
        return -1;
    }
    return 0;
}

static gint tb_tc_cmp(gconstpointer ap, gconstpointer bp)
{
    const struct tb_tc *a = ap;
    const struct tb_tc *b = bp;

    /*
     * When both sizes are set, we know this isn't a lookup.
     * This is the most likely case: every TB must be inserted; lookups
     * are a lot less frequent.
     */
    if (likely(a->size && b->size)) {
        if (a->ptr > b->ptr) {
            return 1;
        } else if (a->ptr < b->ptr) {
            return -1;
        }
        /* a->ptr == b->ptr should happen only on deletions */
        g_assert(a->size == b->size);
        return 0;
    }
    /*
     * All lookups have either .size field set to 0.
     * From the glib sources we see that @ap is always the lookup key. However
     * the docs provide no guarantee, so we just mark this case as likely.
     */
    if (likely(a->size == 0)) {
        return ptr_cmp_tb_tc(a->ptr, b);
    }
    return ptr_cmp_tb_tc(b->ptr, a);
}

static void tcg_region_trees_init(void)
{
    size_t i;

    tree_size = ROUND_UP(sizeof(struct tcg_region_tree), qemu_dcache_linesize);
    region_trees = qemu_memalign(qemu_dcache_linesize, region.n * tree_size);
    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        qemu_mutex_init(&rt->lock);
        rt->tree = g_tree_new(tb_tc_cmp);
    }
}

static struct tcg_region_tree *tc_ptr_to_region_tree(const void *cp)
{
    void *p = tcg_splitwx_to_rw(cp);
    size_t region_idx;

    if (p < region.start_aligned) {
        region_idx = 0;
    } else {
        ptrdiff_t offset = p - region.start_aligned;

        if (offset > region.stride * (region.n - 1)) {
            region_idx = region.n - 1;
        } else {
            region_idx = offset / region.stride;
        }
    }
    return region_trees + region_idx * tree_size;
}

void tcg_tb_insert(TranslationBlock *tb)
{
    struct tcg_region_tree *rt = tc_ptr_to_region_tree(tb->tc.ptr);

    qemu_mutex_lock(&rt->lock);
    g_tree_insert(rt->tree, &tb->tc, tb);
    qemu_mutex_unlock(&rt->lock);
}

void tcg_tb_remove(TranslationBlock *tb)
{
    struct tcg_region_tree *rt = tc_ptr_to_region_tree(tb->tc.ptr);

    qemu_mutex_lock(&rt->lock);
    g_tree_remove(rt->tree, &tb->tc);
    qemu_mutex_unlock(&rt->lock);
}

/*
 * Find the TB 'tb' such that
 * tb->tc.ptr <= tc_ptr < tb->tc.ptr + tb->tc.size
 * Return NULL if not found.
 */
TranslationBlock *tcg_tb_lookup(uintptr_t tc_ptr)
{
    struct tcg_region_tree *rt = tc_ptr_to_region_tree((void *)tc_ptr);
    TranslationBlock *tb;
    struct tb_tc s = { .ptr = (void *)tc_ptr };

    qemu_mutex_lock(&rt->lock);
    tb = g_tree_lookup(rt->tree, &s);
    qemu_mutex_unlock(&rt->lock);
    return tb;
}

static void tcg_region_tree_lock_all(void)
{
    size_t i;

    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        qemu_mutex_lock(&rt->lock);
    }
}

static void tcg_region_tree_unlock_all(void)
{
    size_t i;

    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        qemu_mutex_unlock(&rt->lock);
    }
}

void tcg_tb_foreach(GTraverseFunc func, gpointer user_data)
{
    size_t i;

    tcg_region_tree_lock_all();
    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        g_tree_foreach(rt->tree, func, user_data);
    }
    tcg_region_tree_unlock_all();
}

size_t tcg_nb_tbs(void)
{
    size_t nb_tbs = 0;
    size_t i;

    tcg_region_tree_lock_all();
    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        nb_tbs += g_tree_nnodes(rt->tree);
    }
    tcg_region_tree_unlock_all();
    return nb_tbs;
}

static gboolean tcg_region_tree_traverse(gpointer k, gpointer v, gpointer data)
{
    TranslationBlock *tb = v;

    tb_destroy(tb);
    return FALSE;
}

static void tcg_region_tree_reset_all(void)
{
    size_t i;

    tcg_region_tree_lock_all();
    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        g_tree_foreach(rt->tree, tcg_region_tree_traverse, NULL);
        /* Increment the refcount first so that destroy acts as a reset */
        g_tree_ref(rt->tree);
        g_tree_destroy(rt->tree);
    }
    tcg_region_tree_unlock_all();
}

static void tcg_region_bounds(size_t curr_region, void **pstart, void **pend)
{
    void *start, *end;

    start = region.start_aligned + curr_region * region.stride;
    end = start + region.size;

    if (curr_region == 0) {
        start = region.start;
    }
    if (curr_region == region.n - 1) {
        end = region.end;
    }

    *pstart = start;
    *pend = end;
}

static void tcg_region_assign(TCGContext *s, size_t curr_region)
{
    void *start, *end;

    tcg_region_bounds(curr_region, &start, &end);

    s->code_gen_buffer = start;
    s->code_gen_ptr = start;
    s->code_gen_buffer_size = end - start;
    s->code_gen_highwater = end - TCG_HIGHWATER;
}

static bool tcg_region_alloc__locked(TCGContext *s)
{
    if (region.current == region.n) {
        return true;
    }
    tcg_region_assign(s, region.current);
    region.current++;
    return false;
}

/*
 * Request a new region once the one in use has filled up.
 * Returns true on error.
 */
static bool tcg_region_alloc(TCGContext *s)
{
    bool err;
    /* read the region size now; alloc__locked will overwrite it on success */
    size_t size_full = s->code_gen_buffer_size;

    qemu_mutex_lock(&region.lock);
    err = tcg_region_alloc__locked(s);
    if (!err) {
        region.agg_size_full += size_full - TCG_HIGHWATER;
    }
    qemu_mutex_unlock(&region.lock);
    return err;
}

/*
 * Perform a context's first region allocation.
 * This function does _not_ increment region.agg_size_full.
 */
static inline bool tcg_region_initial_alloc__locked(TCGContext *s)
{
    return tcg_region_alloc__locked(s);
}

/* Call from a safe-work context */
void tcg_region_reset_all(void)
{
    unsigned int n_ctxs = qatomic_read(&n_tcg_ctxs);
    unsigned int i;

    qemu_mutex_lock(&region.lock);
    region.current = 0;
    region.agg_size_full = 0;

    for (i = 0; i < n_ctxs; i++) {
        TCGContext *s = qatomic_read(&tcg_ctxs[i]);
        bool err = tcg_region_initial_alloc__locked(s);

        g_assert(!err);
    }
    qemu_mutex_unlock(&region.lock);

    tcg_region_tree_reset_all();
}

#ifdef CONFIG_USER_ONLY
static size_t tcg_n_regions(void)
{
    return 1;
}
#else
/*
 * It is likely that some vCPUs will translate more code than others, so we
 * first try to set more regions than max_cpus, with those regions being of
 * reasonable size. If that's not possible we make do by evenly dividing
 * the code_gen_buffer among the vCPUs.
 */
static size_t tcg_n_regions(void)
{
    size_t i;

    /* Use a single region if all we have is one vCPU thread */
#if !defined(CONFIG_USER_ONLY)
    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int max_cpus = ms->smp.max_cpus;
#endif
    if (max_cpus == 1 || !qemu_tcg_mttcg_enabled()) {
        return 1;
    }

    /* Try to have more regions than max_cpus, with each region being >= 2 MB */
    for (i = 8; i > 0; i--) {
        size_t regions_per_thread = i;
        size_t region_size;

        region_size = tcg_init_ctx.code_gen_buffer_size;
        region_size /= max_cpus * regions_per_thread;

        if (region_size >= 2 * 1024u * 1024) {
            return max_cpus * regions_per_thread;
        }
    }
    /* If we can't, then just allocate one region per vCPU thread */
    return max_cpus;
}
#endif

/*
 * Initializes region partitioning.
 *
 * Called at init time from the parent thread (i.e. the one calling
 * tcg_context_init), after the target's TCG globals have been set.
 *
 * Region partitioning works by splitting code_gen_buffer into separate regions,
 * and then assigning regions to TCG threads so that the threads can translate
 * code in parallel without synchronization.
 *
 * In softmmu the number of TCG threads is bounded by max_cpus, so we use at
 * least max_cpus regions in MTTCG. In !MTTCG we use a single region.
 * Note that the TCG options from the command-line (i.e. -accel accel=tcg,[...])
 * must have been parsed before calling this function, since it calls
 * qemu_tcg_mttcg_enabled().
 *
 * In user-mode we use a single region.  Having multiple regions in user-mode
 * is not supported, because the number of vCPU threads (recall that each thread
 * spawned by the guest corresponds to a vCPU thread) is only bounded by the
 * OS, and usually this number is huge (tens of thousands is not uncommon).
 * Thus, given this large bound on the number of vCPU threads and the fact
 * that code_gen_buffer is allocated at compile-time, we cannot guarantee
 * that the availability of at least one region per vCPU thread.
 *
 * However, this user-mode limitation is unlikely to be a significant problem
 * in practice. Multi-threaded guests share most if not all of their translated
 * code, which makes parallel code generation less appealing than in softmmu.
 */
void tcg_region_init(void)
{
    void *buf = tcg_init_ctx.code_gen_buffer;
    void *aligned;
    size_t size = tcg_init_ctx.code_gen_buffer_size;
    size_t page_size = qemu_real_host_page_size;
    size_t region_size;
    size_t n_regions;
    size_t i;
    uintptr_t splitwx_diff;

    n_regions = tcg_n_regions();

    /* The first region will be 'aligned - buf' bytes larger than the others */
    aligned = QEMU_ALIGN_PTR_UP(buf, page_size);
    g_assert(aligned < tcg_init_ctx.code_gen_buffer + size);
    /*
     * Make region_size a multiple of page_size, using aligned as the start.
     * As a result of this we might end up with a few extra pages at the end of
     * the buffer; we will assign those to the last region.
     */
    region_size = (size - (aligned - buf)) / n_regions;
    region_size = QEMU_ALIGN_DOWN(region_size, page_size);

    /* A region must have at least 2 pages; one code, one guard */
    g_assert(region_size >= 2 * page_size);

    /* init the region struct */
    qemu_mutex_init(&region.lock);
    region.n = n_regions;
    region.size = region_size - page_size;
    region.stride = region_size;
    region.start = buf;
    region.start_aligned = aligned;
    /* page-align the end, since its last page will be a guard page */
    region.end = QEMU_ALIGN_PTR_DOWN(buf + size, page_size);
    /* account for that last guard page */
    region.end -= page_size;

    /* set guard pages */
    splitwx_diff = tcg_splitwx_diff;
    for (i = 0; i < region.n; i++) {
        void *start, *end;
        int rc;

        tcg_region_bounds(i, &start, &end);
        rc = qemu_mprotect_none(end, page_size);
        g_assert(!rc);
        if (splitwx_diff) {
            rc = qemu_mprotect_none(end + splitwx_diff, page_size);
            g_assert(!rc);
        }
    }

    tcg_region_trees_init();

    /* In user-mode we support only one ctx, so do the initial allocation now */
#ifdef CONFIG_USER_ONLY
    {
        bool err = tcg_region_initial_alloc__locked(tcg_ctx);

        g_assert(!err);
    }
#endif
}

#ifdef CONFIG_DEBUG_TCG
const void *tcg_splitwx_to_rx(void *rw)
{
    /* Pass NULL pointers unchanged. */
    if (rw) {
        g_assert(in_code_gen_buffer(rw));
        rw += tcg_splitwx_diff;
    }
    return rw;
}

void *tcg_splitwx_to_rw(const void *rx)
{
    /* Pass NULL pointers unchanged. */
    if (rx) {
        rx -= tcg_splitwx_diff;
        /* Assert that we end with a pointer in the rw region. */
        g_assert(in_code_gen_buffer(rx));
    }
    return (void *)rx;
}
#endif /* CONFIG_DEBUG_TCG */

static void alloc_tcg_plugin_context(TCGContext *s)
{
#ifdef CONFIG_PLUGIN
    s->plugin_tb = g_new0(struct qemu_plugin_tb, 1);
    s->plugin_tb->insns =
        g_ptr_array_new_with_free_func(qemu_plugin_insn_cleanup_fn);
#endif
}

/*
 * All TCG threads except the parent (i.e. the one that called tcg_context_init
 * and registered the target's TCG globals) must register with this function
 * before initiating translation.
 *
 * In user-mode we just point tcg_ctx to tcg_init_ctx. See the documentation
 * of tcg_region_init() for the reasoning behind this.
 *
 * In softmmu each caller registers its context in tcg_ctxs[]. Note that in
 * softmmu tcg_ctxs[] does not track tcg_ctx_init, since the initial context
 * is not used anymore for translation once this function is called.
 *
 * Not tracking tcg_init_ctx in tcg_ctxs[] in softmmu keeps code that iterates
 * over the array (e.g. tcg_code_size() the same for both softmmu and user-mode.
 */
#ifdef CONFIG_USER_ONLY
void tcg_register_thread(void)
{
    tcg_ctx = &tcg_init_ctx;
}
#else
void tcg_register_thread(void)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    TCGContext *s = g_malloc(sizeof(*s));
    unsigned int i, n;
    bool err;

    *s = tcg_init_ctx;

    /* Relink mem_base.  */
    for (i = 0, n = tcg_init_ctx.nb_globals; i < n; ++i) {
        if (tcg_init_ctx.temps[i].mem_base) {
            ptrdiff_t b = tcg_init_ctx.temps[i].mem_base - tcg_init_ctx.temps;
            tcg_debug_assert(b >= 0 && b < n);
            s->temps[i].mem_base = &s->temps[b];
        }
    }

    /* Claim an entry in tcg_ctxs */
    n = qatomic_fetch_inc(&n_tcg_ctxs);
    g_assert(n < ms->smp.max_cpus);
    qatomic_set(&tcg_ctxs[n], s);

    if (n > 0) {
        alloc_tcg_plugin_context(s);
    }

    tcg_ctx = s;
    qemu_mutex_lock(&region.lock);
    err = tcg_region_initial_alloc__locked(tcg_ctx);
    g_assert(!err);
    qemu_mutex_unlock(&region.lock);
}
#endif /* !CONFIG_USER_ONLY */

/*
 * Returns the size (in bytes) of all translated code (i.e. from all regions)
 * currently in the cache.
 * See also: tcg_code_capacity()
 * Do not confuse with tcg_current_code_size(); that one applies to a single
 * TCG context.
 */
size_t tcg_code_size(void)
{
    unsigned int n_ctxs = qatomic_read(&n_tcg_ctxs);
    unsigned int i;
    size_t total;

    qemu_mutex_lock(&region.lock);
    total = region.agg_size_full;
    for (i = 0; i < n_ctxs; i++) {
        const TCGContext *s = qatomic_read(&tcg_ctxs[i]);
        size_t size;

        size = qatomic_read(&s->code_gen_ptr) - s->code_gen_buffer;
        g_assert(size <= s->code_gen_buffer_size);
        total += size;
    }
    qemu_mutex_unlock(&region.lock);
    return total;
}

/*
 * Returns the code capacity (in bytes) of the entire cache, i.e. including all
 * regions.
 * See also: tcg_code_size()
 */
size_t tcg_code_capacity(void)
{
    size_t guard_size, capacity;

    /* no need for synchronization; these variables are set at init time */
    guard_size = region.stride - region.size;
    capacity = region.end + guard_size - region.start;
    capacity -= region.n * (guard_size + TCG_HIGHWATER);
    return capacity;
}

size_t tcg_tb_phys_invalidate_count(void)
{
    unsigned int n_ctxs = qatomic_read(&n_tcg_ctxs);
    unsigned int i;
    size_t total = 0;

    for (i = 0; i < n_ctxs; i++) {
        const TCGContext *s = qatomic_read(&tcg_ctxs[i]);

        total += qatomic_read(&s->tb_phys_invalidate_count);
    }
    return total;
}

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
static GHashTable *helper_table;

static int indirect_reg_alloc_order[ARRAY_SIZE(tcg_target_reg_alloc_order)];
static void process_op_defs(TCGContext *s);
static TCGTemp *tcg_global_reg_new_internal(TCGContext *s, TCGType type,
                                            TCGReg reg, const char *name);

void tcg_context_init(TCGContext *s)
{
    int op, total_args, n, i;
    TCGOpDef *def;
    TCGArgConstraint *args_ct;
    TCGTemp *ts;

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

    args_ct = g_new0(TCGArgConstraint, total_args);

    for(op = 0; op < NB_OPS; op++) {
        def = &tcg_op_defs[op];
        def->args_ct = args_ct;
        n = def->nb_iargs + def->nb_oargs;
        args_ct += n;
    }

    /* Register helpers.  */
    /* Use g_direct_hash/equal for direct pointer comparisons on func.  */
    helper_table = g_hash_table_new(NULL, NULL);

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

    alloc_tcg_plugin_context(s);

    tcg_ctx = s;
    /*
     * In user-mode we simply share the init context among threads, since we
     * use a single region. See the documentation tcg_region_init() for the
     * reasoning behind this.
     * In softmmu we will have at most max_cpus TCG threads.
     */
#ifdef CONFIG_USER_ONLY
    tcg_ctxs = &tcg_ctx;
    n_tcg_ctxs = 1;
#else
    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int max_cpus = ms->smp.max_cpus;
    tcg_ctxs = g_new(TCGContext *, max_cpus);
#endif

    tcg_debug_assert(!tcg_regset_test_reg(s->reserved_regs, TCG_AREG0));
    ts = tcg_global_reg_new_internal(s, TCG_TYPE_PTR, TCG_AREG0, "env");
    cpu_env = temp_tcgv_ptr(ts);
}

/*
 * Allocate TBs right before their corresponding translated code, making
 * sure that TBs and code are on different cache lines.
 */
TranslationBlock *tcg_tb_alloc(TCGContext *s)
{
    uintptr_t align = qemu_icache_linesize;
    TranslationBlock *tb;
    void *next;

 retry:
    tb = (void *)ROUND_UP((uintptr_t)s->code_gen_ptr, align);
    next = (void *)ROUND_UP((uintptr_t)(tb + 1), align);

    if (unlikely(next > s->code_gen_highwater)) {
        if (tcg_region_alloc(s)) {
            return NULL;
        }
        goto retry;
    }
    qatomic_set(&s->code_gen_ptr, next);
    s->data_gen_ptr = NULL;
    return tb;
}

void tcg_prologue_init(TCGContext *s)
{
    size_t prologue_size, total_size;
    void *buf0, *buf1;

    /* Put the prologue at the beginning of code_gen_buffer.  */
    buf0 = s->code_gen_buffer;
    total_size = s->code_gen_buffer_size;
    s->code_ptr = buf0;
    s->code_buf = buf0;
    s->data_gen_ptr = NULL;

    /*
     * The region trees are not yet configured, but tcg_splitwx_to_rx
     * needs the bounds for an assert.
     */
    region.start = buf0;
    region.end = buf0 + total_size;

#ifndef CONFIG_TCG_INTERPRETER
    tcg_qemu_tb_exec = (tcg_prologue_fn *)tcg_splitwx_to_rx(buf0);
#endif

    /* Compute a high-water mark, at which we voluntarily flush the buffer
       and start over.  The size here is arbitrary, significantly larger
       than we expect the code generation for any one opcode to require.  */
    s->code_gen_highwater = s->code_gen_buffer + (total_size - TCG_HIGHWATER);

#ifdef TCG_TARGET_NEED_POOL_LABELS
    s->pool_labels = NULL;
#endif

    qemu_thread_jit_write();
    /* Generate the prologue.  */
    tcg_target_qemu_prologue(s);

#ifdef TCG_TARGET_NEED_POOL_LABELS
    /* Allow the prologue to put e.g. guest_base into a pool entry.  */
    {
        int result = tcg_out_pool_finalize(s);
        tcg_debug_assert(result == 0);
    }
#endif

    buf1 = s->code_ptr;
#ifndef CONFIG_TCG_INTERPRETER
    flush_idcache_range((uintptr_t)tcg_splitwx_to_rx(buf0), (uintptr_t)buf0,
                        tcg_ptr_byte_diff(buf1, buf0));
#endif

    /* Deduct the prologue from the buffer.  */
    prologue_size = tcg_current_code_size(s);
    s->code_gen_ptr = buf1;
    s->code_gen_buffer = buf1;
    s->code_buf = buf1;
    total_size -= prologue_size;
    s->code_gen_buffer_size = total_size;

    tcg_register_jit(tcg_splitwx_to_rx(s->code_gen_buffer), total_size);

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_OUT_ASM)) {
        FILE *logfile = qemu_log_lock();
        qemu_log("PROLOGUE: [size=%zu]\n", prologue_size);
        if (s->data_gen_ptr) {
            size_t code_size = s->data_gen_ptr - buf0;
            size_t data_size = prologue_size - code_size;
            size_t i;

            log_disas(buf0, code_size);

            for (i = 0; i < data_size; i += sizeof(tcg_target_ulong)) {
                if (sizeof(tcg_target_ulong) == 8) {
                    qemu_log("0x%08" PRIxPTR ":  .quad  0x%016" PRIx64 "\n",
                             (uintptr_t)s->data_gen_ptr + i,
                             *(uint64_t *)(s->data_gen_ptr + i));
                } else {
                    qemu_log("0x%08" PRIxPTR ":  .long  0x%08x\n",
                             (uintptr_t)s->data_gen_ptr + i,
                             *(uint32_t *)(s->data_gen_ptr + i));
                }
            }
        } else {
            log_disas(buf0, prologue_size);
        }
        qemu_log("\n");
        qemu_log_flush();
        qemu_log_unlock(logfile);
    }
#endif

    /* Assert that goto_ptr is implemented completely.  */
    if (TCG_TARGET_HAS_goto_ptr) {
        tcg_debug_assert(tcg_code_gen_epilogue != NULL);
    }
}

void tcg_func_start(TCGContext *s)
{
    tcg_pool_reset(s);
    s->nb_temps = s->nb_globals;

    /* No temps have been previously allocated for size or locality.  */
    memset(s->free_temps, 0, sizeof(s->free_temps));

    /* No constant temps have been previously allocated. */
    for (int i = 0; i < TCG_TYPE_COUNT; ++i) {
        if (s->const_table[i]) {
            g_hash_table_remove_all(s->const_table[i]);
        }
    }

    s->nb_ops = 0;
    s->nb_labels = 0;
    s->current_frame_offset = s->frame_start;

#ifdef CONFIG_DEBUG_TCG
    s->goto_tb_issue_mask = 0;
#endif

    QTAILQ_INIT(&s->ops);
    QTAILQ_INIT(&s->free_ops);
    QSIMPLEQ_INIT(&s->labels);
}

static TCGTemp *tcg_temp_alloc(TCGContext *s)
{
    int n = s->nb_temps++;

    if (n >= TCG_MAX_TEMPS) {
        /* Signal overflow, starting over with fewer guest insns. */
        siglongjmp(s->jmp_trans, -2);
    }
    return memset(&s->temps[n], 0, sizeof(TCGTemp));
}

static TCGTemp *tcg_global_alloc(TCGContext *s)
{
    TCGTemp *ts;

    tcg_debug_assert(s->nb_globals == s->nb_temps);
    tcg_debug_assert(s->nb_globals < TCG_MAX_TEMPS);
    s->nb_globals++;
    ts = tcg_temp_alloc(s);
    ts->kind = TEMP_GLOBAL;

    return ts;
}

static TCGTemp *tcg_global_reg_new_internal(TCGContext *s, TCGType type,
                                            TCGReg reg, const char *name)
{
    TCGTemp *ts;

    if (TCG_TARGET_REG_BITS == 32 && type != TCG_TYPE_I32) {
        tcg_abort();
    }

    ts = tcg_global_alloc(s);
    ts->base_type = type;
    ts->type = type;
    ts->kind = TEMP_FIXED;
    ts->reg = reg;
    ts->name = name;
    tcg_regset_set_reg(s->reserved_regs, reg);

    return ts;
}

void tcg_set_frame(TCGContext *s, TCGReg reg, intptr_t start, intptr_t size)
{
    s->frame_start = start;
    s->frame_end = start + size;
    s->frame_temp
        = tcg_global_reg_new_internal(s, TCG_TYPE_PTR, reg, "_frame");
}

TCGTemp *tcg_global_mem_new_internal(TCGType type, TCGv_ptr base,
                                     intptr_t offset, const char *name)
{
    TCGContext *s = tcg_ctx;
    TCGTemp *base_ts = tcgv_ptr_temp(base);
    TCGTemp *ts = tcg_global_alloc(s);
    int indirect_reg = 0, bigendian = 0;
#ifdef HOST_WORDS_BIGENDIAN
    bigendian = 1;
#endif

    switch (base_ts->kind) {
    case TEMP_FIXED:
        break;
    case TEMP_GLOBAL:
        /* We do not support double-indirect registers.  */
        tcg_debug_assert(!base_ts->indirect_reg);
        base_ts->indirect_base = 1;
        s->nb_indirects += (TCG_TARGET_REG_BITS == 32 && type == TCG_TYPE_I64
                            ? 2 : 1);
        indirect_reg = 1;
        break;
    default:
        g_assert_not_reached();
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
    return ts;
}

TCGTemp *tcg_temp_new_internal(TCGType type, bool temp_local)
{
    TCGContext *s = tcg_ctx;
    TCGTempKind kind = temp_local ? TEMP_LOCAL : TEMP_NORMAL;
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
        tcg_debug_assert(ts->kind == kind);
    } else {
        ts = tcg_temp_alloc(s);
        if (TCG_TARGET_REG_BITS == 32 && type == TCG_TYPE_I64) {
            TCGTemp *ts2 = tcg_temp_alloc(s);

            ts->base_type = type;
            ts->type = TCG_TYPE_I32;
            ts->temp_allocated = 1;
            ts->kind = kind;

            tcg_debug_assert(ts2 == ts + 1);
            ts2->base_type = TCG_TYPE_I64;
            ts2->type = TCG_TYPE_I32;
            ts2->temp_allocated = 1;
            ts2->kind = kind;
        } else {
            ts->base_type = type;
            ts->type = type;
            ts->temp_allocated = 1;
            ts->kind = kind;
        }
    }

#if defined(CONFIG_DEBUG_TCG)
    s->temps_in_use++;
#endif
    return ts;
}

TCGv_vec tcg_temp_new_vec(TCGType type)
{
    TCGTemp *t;

#ifdef CONFIG_DEBUG_TCG
    switch (type) {
    case TCG_TYPE_V64:
        assert(TCG_TARGET_HAS_v64);
        break;
    case TCG_TYPE_V128:
        assert(TCG_TARGET_HAS_v128);
        break;
    case TCG_TYPE_V256:
        assert(TCG_TARGET_HAS_v256);
        break;
    default:
        g_assert_not_reached();
    }
#endif

    t = tcg_temp_new_internal(type, 0);
    return temp_tcgv_vec(t);
}

/* Create a new temp of the same type as an existing temp.  */
TCGv_vec tcg_temp_new_vec_matching(TCGv_vec match)
{
    TCGTemp *t = tcgv_vec_temp(match);

    tcg_debug_assert(t->temp_allocated != 0);

    t = tcg_temp_new_internal(t->base_type, 0);
    return temp_tcgv_vec(t);
}

void tcg_temp_free_internal(TCGTemp *ts)
{
    TCGContext *s = tcg_ctx;
    int k, idx;

    /* In order to simplify users of tcg_constant_*, silently ignore free. */
    if (ts->kind == TEMP_CONST) {
        return;
    }

#if defined(CONFIG_DEBUG_TCG)
    s->temps_in_use--;
    if (s->temps_in_use < 0) {
        fprintf(stderr, "More temporaries freed than allocated!\n");
    }
#endif

    tcg_debug_assert(ts->kind < TEMP_GLOBAL);
    tcg_debug_assert(ts->temp_allocated != 0);
    ts->temp_allocated = 0;

    idx = temp_idx(ts);
    k = ts->base_type + (ts->kind == TEMP_NORMAL ? 0 : TCG_TYPE_COUNT);
    set_bit(idx, s->free_temps[k].l);
}

TCGTemp *tcg_constant_internal(TCGType type, int64_t val)
{
    TCGContext *s = tcg_ctx;
    GHashTable *h = s->const_table[type];
    TCGTemp *ts;

    if (h == NULL) {
        h = g_hash_table_new(g_int64_hash, g_int64_equal);
        s->const_table[type] = h;
    }

    ts = g_hash_table_lookup(h, &val);
    if (ts == NULL) {
        ts = tcg_temp_alloc(s);

        if (TCG_TARGET_REG_BITS == 32 && type == TCG_TYPE_I64) {
            TCGTemp *ts2 = tcg_temp_alloc(s);

            ts->base_type = TCG_TYPE_I64;
            ts->type = TCG_TYPE_I32;
            ts->kind = TEMP_CONST;
            ts->temp_allocated = 1;
            /*
             * Retain the full value of the 64-bit constant in the low
             * part, so that the hash table works.  Actual uses will
             * truncate the value to the low part.
             */
            ts->val = val;

            tcg_debug_assert(ts2 == ts + 1);
            ts2->base_type = TCG_TYPE_I64;
            ts2->type = TCG_TYPE_I32;
            ts2->kind = TEMP_CONST;
            ts2->temp_allocated = 1;
            ts2->val = val >> 32;
        } else {
            ts->base_type = type;
            ts->type = type;
            ts->kind = TEMP_CONST;
            ts->temp_allocated = 1;
            ts->val = val;
        }
        g_hash_table_insert(h, &ts->val, ts);
    }

    return ts;
}

TCGv_vec tcg_constant_vec(TCGType type, unsigned vece, int64_t val)
{
    val = dup_const(vece, val);
    return temp_tcgv_vec(tcg_constant_internal(type, val));
}

TCGv_vec tcg_constant_vec_matching(TCGv_vec match, unsigned vece, int64_t val)
{
    TCGTemp *t = tcgv_vec_temp(match);

    tcg_debug_assert(t->temp_allocated != 0);
    return tcg_constant_vec(t->base_type, vece, val);
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
    TCGContext *s = tcg_ctx;
    s->temps_in_use = 0;
}

int tcg_check_temp_count(void)
{
    TCGContext *s = tcg_ctx;
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

/* Return true if OP may appear in the opcode stream.
   Test the runtime variable that controls each opcode.  */
bool tcg_op_supported(TCGOpcode op)
{
    const bool have_vec
        = TCG_TARGET_HAS_v64 | TCG_TARGET_HAS_v128 | TCG_TARGET_HAS_v256;

    switch (op) {
    case INDEX_op_discard:
    case INDEX_op_set_label:
    case INDEX_op_call:
    case INDEX_op_br:
    case INDEX_op_mb:
    case INDEX_op_insn_start:
    case INDEX_op_exit_tb:
    case INDEX_op_goto_tb:
    case INDEX_op_qemu_ld_i32:
    case INDEX_op_qemu_st_i32:
    case INDEX_op_qemu_ld_i64:
    case INDEX_op_qemu_st_i64:
        return true;

    case INDEX_op_qemu_st8_i32:
        return TCG_TARGET_HAS_qemu_st8_i32;

    case INDEX_op_goto_ptr:
        return TCG_TARGET_HAS_goto_ptr;

    case INDEX_op_mov_i32:
    case INDEX_op_setcond_i32:
    case INDEX_op_brcond_i32:
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld_i32:
    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
    case INDEX_op_add_i32:
    case INDEX_op_sub_i32:
    case INDEX_op_mul_i32:
    case INDEX_op_and_i32:
    case INDEX_op_or_i32:
    case INDEX_op_xor_i32:
    case INDEX_op_shl_i32:
    case INDEX_op_shr_i32:
    case INDEX_op_sar_i32:
        return true;

    case INDEX_op_movcond_i32:
        return TCG_TARGET_HAS_movcond_i32;
    case INDEX_op_div_i32:
    case INDEX_op_divu_i32:
        return TCG_TARGET_HAS_div_i32;
    case INDEX_op_rem_i32:
    case INDEX_op_remu_i32:
        return TCG_TARGET_HAS_rem_i32;
    case INDEX_op_div2_i32:
    case INDEX_op_divu2_i32:
        return TCG_TARGET_HAS_div2_i32;
    case INDEX_op_rotl_i32:
    case INDEX_op_rotr_i32:
        return TCG_TARGET_HAS_rot_i32;
    case INDEX_op_deposit_i32:
        return TCG_TARGET_HAS_deposit_i32;
    case INDEX_op_extract_i32:
        return TCG_TARGET_HAS_extract_i32;
    case INDEX_op_sextract_i32:
        return TCG_TARGET_HAS_sextract_i32;
    case INDEX_op_extract2_i32:
        return TCG_TARGET_HAS_extract2_i32;
    case INDEX_op_add2_i32:
        return TCG_TARGET_HAS_add2_i32;
    case INDEX_op_sub2_i32:
        return TCG_TARGET_HAS_sub2_i32;
    case INDEX_op_mulu2_i32:
        return TCG_TARGET_HAS_mulu2_i32;
    case INDEX_op_muls2_i32:
        return TCG_TARGET_HAS_muls2_i32;
    case INDEX_op_muluh_i32:
        return TCG_TARGET_HAS_muluh_i32;
    case INDEX_op_mulsh_i32:
        return TCG_TARGET_HAS_mulsh_i32;
    case INDEX_op_ext8s_i32:
        return TCG_TARGET_HAS_ext8s_i32;
    case INDEX_op_ext16s_i32:
        return TCG_TARGET_HAS_ext16s_i32;
    case INDEX_op_ext8u_i32:
        return TCG_TARGET_HAS_ext8u_i32;
    case INDEX_op_ext16u_i32:
        return TCG_TARGET_HAS_ext16u_i32;
    case INDEX_op_bswap16_i32:
        return TCG_TARGET_HAS_bswap16_i32;
    case INDEX_op_bswap32_i32:
        return TCG_TARGET_HAS_bswap32_i32;
    case INDEX_op_not_i32:
        return TCG_TARGET_HAS_not_i32;
    case INDEX_op_neg_i32:
        return TCG_TARGET_HAS_neg_i32;
    case INDEX_op_andc_i32:
        return TCG_TARGET_HAS_andc_i32;
    case INDEX_op_orc_i32:
        return TCG_TARGET_HAS_orc_i32;
    case INDEX_op_eqv_i32:
        return TCG_TARGET_HAS_eqv_i32;
    case INDEX_op_nand_i32:
        return TCG_TARGET_HAS_nand_i32;
    case INDEX_op_nor_i32:
        return TCG_TARGET_HAS_nor_i32;
    case INDEX_op_clz_i32:
        return TCG_TARGET_HAS_clz_i32;
    case INDEX_op_ctz_i32:
        return TCG_TARGET_HAS_ctz_i32;
    case INDEX_op_ctpop_i32:
        return TCG_TARGET_HAS_ctpop_i32;

    case INDEX_op_brcond2_i32:
    case INDEX_op_setcond2_i32:
        return TCG_TARGET_REG_BITS == 32;

    case INDEX_op_mov_i64:
    case INDEX_op_setcond_i64:
    case INDEX_op_brcond_i64:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld_i64:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
    case INDEX_op_add_i64:
    case INDEX_op_sub_i64:
    case INDEX_op_mul_i64:
    case INDEX_op_and_i64:
    case INDEX_op_or_i64:
    case INDEX_op_xor_i64:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extu_i32_i64:
        return TCG_TARGET_REG_BITS == 64;

    case INDEX_op_movcond_i64:
        return TCG_TARGET_HAS_movcond_i64;
    case INDEX_op_div_i64:
    case INDEX_op_divu_i64:
        return TCG_TARGET_HAS_div_i64;
    case INDEX_op_rem_i64:
    case INDEX_op_remu_i64:
        return TCG_TARGET_HAS_rem_i64;
    case INDEX_op_div2_i64:
    case INDEX_op_divu2_i64:
        return TCG_TARGET_HAS_div2_i64;
    case INDEX_op_rotl_i64:
    case INDEX_op_rotr_i64:
        return TCG_TARGET_HAS_rot_i64;
    case INDEX_op_deposit_i64:
        return TCG_TARGET_HAS_deposit_i64;
    case INDEX_op_extract_i64:
        return TCG_TARGET_HAS_extract_i64;
    case INDEX_op_sextract_i64:
        return TCG_TARGET_HAS_sextract_i64;
    case INDEX_op_extract2_i64:
        return TCG_TARGET_HAS_extract2_i64;
    case INDEX_op_extrl_i64_i32:
        return TCG_TARGET_HAS_extrl_i64_i32;
    case INDEX_op_extrh_i64_i32:
        return TCG_TARGET_HAS_extrh_i64_i32;
    case INDEX_op_ext8s_i64:
        return TCG_TARGET_HAS_ext8s_i64;
    case INDEX_op_ext16s_i64:
        return TCG_TARGET_HAS_ext16s_i64;
    case INDEX_op_ext32s_i64:
        return TCG_TARGET_HAS_ext32s_i64;
    case INDEX_op_ext8u_i64:
        return TCG_TARGET_HAS_ext8u_i64;
    case INDEX_op_ext16u_i64:
        return TCG_TARGET_HAS_ext16u_i64;
    case INDEX_op_ext32u_i64:
        return TCG_TARGET_HAS_ext32u_i64;
    case INDEX_op_bswap16_i64:
        return TCG_TARGET_HAS_bswap16_i64;
    case INDEX_op_bswap32_i64:
        return TCG_TARGET_HAS_bswap32_i64;
    case INDEX_op_bswap64_i64:
        return TCG_TARGET_HAS_bswap64_i64;
    case INDEX_op_not_i64:
        return TCG_TARGET_HAS_not_i64;
    case INDEX_op_neg_i64:
        return TCG_TARGET_HAS_neg_i64;
    case INDEX_op_andc_i64:
        return TCG_TARGET_HAS_andc_i64;
    case INDEX_op_orc_i64:
        return TCG_TARGET_HAS_orc_i64;
    case INDEX_op_eqv_i64:
        return TCG_TARGET_HAS_eqv_i64;
    case INDEX_op_nand_i64:
        return TCG_TARGET_HAS_nand_i64;
    case INDEX_op_nor_i64:
        return TCG_TARGET_HAS_nor_i64;
    case INDEX_op_clz_i64:
        return TCG_TARGET_HAS_clz_i64;
    case INDEX_op_ctz_i64:
        return TCG_TARGET_HAS_ctz_i64;
    case INDEX_op_ctpop_i64:
        return TCG_TARGET_HAS_ctpop_i64;
    case INDEX_op_add2_i64:
        return TCG_TARGET_HAS_add2_i64;
    case INDEX_op_sub2_i64:
        return TCG_TARGET_HAS_sub2_i64;
    case INDEX_op_mulu2_i64:
        return TCG_TARGET_HAS_mulu2_i64;
    case INDEX_op_muls2_i64:
        return TCG_TARGET_HAS_muls2_i64;
    case INDEX_op_muluh_i64:
        return TCG_TARGET_HAS_muluh_i64;
    case INDEX_op_mulsh_i64:
        return TCG_TARGET_HAS_mulsh_i64;

    case INDEX_op_mov_vec:
    case INDEX_op_dup_vec:
    case INDEX_op_dupm_vec:
    case INDEX_op_ld_vec:
    case INDEX_op_st_vec:
    case INDEX_op_add_vec:
    case INDEX_op_sub_vec:
    case INDEX_op_and_vec:
    case INDEX_op_or_vec:
    case INDEX_op_xor_vec:
    case INDEX_op_cmp_vec:
        return have_vec;
    case INDEX_op_dup2_vec:
        return have_vec && TCG_TARGET_REG_BITS == 32;
    case INDEX_op_not_vec:
        return have_vec && TCG_TARGET_HAS_not_vec;
    case INDEX_op_neg_vec:
        return have_vec && TCG_TARGET_HAS_neg_vec;
    case INDEX_op_abs_vec:
        return have_vec && TCG_TARGET_HAS_abs_vec;
    case INDEX_op_andc_vec:
        return have_vec && TCG_TARGET_HAS_andc_vec;
    case INDEX_op_orc_vec:
        return have_vec && TCG_TARGET_HAS_orc_vec;
    case INDEX_op_mul_vec:
        return have_vec && TCG_TARGET_HAS_mul_vec;
    case INDEX_op_shli_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_sari_vec:
        return have_vec && TCG_TARGET_HAS_shi_vec;
    case INDEX_op_shls_vec:
    case INDEX_op_shrs_vec:
    case INDEX_op_sars_vec:
        return have_vec && TCG_TARGET_HAS_shs_vec;
    case INDEX_op_shlv_vec:
    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
        return have_vec && TCG_TARGET_HAS_shv_vec;
    case INDEX_op_rotli_vec:
        return have_vec && TCG_TARGET_HAS_roti_vec;
    case INDEX_op_rotls_vec:
        return have_vec && TCG_TARGET_HAS_rots_vec;
    case INDEX_op_rotlv_vec:
    case INDEX_op_rotrv_vec:
        return have_vec && TCG_TARGET_HAS_rotv_vec;
    case INDEX_op_ssadd_vec:
    case INDEX_op_usadd_vec:
    case INDEX_op_sssub_vec:
    case INDEX_op_ussub_vec:
        return have_vec && TCG_TARGET_HAS_sat_vec;
    case INDEX_op_smin_vec:
    case INDEX_op_umin_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_umax_vec:
        return have_vec && TCG_TARGET_HAS_minmax_vec;
    case INDEX_op_bitsel_vec:
        return have_vec && TCG_TARGET_HAS_bitsel_vec;
    case INDEX_op_cmpsel_vec:
        return have_vec && TCG_TARGET_HAS_cmpsel_vec;

    default:
        tcg_debug_assert(op > INDEX_op_last_generic && op < NB_OPS);
        return true;
    }
}

void afl_gen_tcg_plain_call(void *func)
{
    TCGOp *op = tcg_emit_op(INDEX_op_call);

    TCGOP_CALLO(op) = 0;

    op->args[0] = (uintptr_t)func;
    op->args[1] = 0;
    TCGOP_CALLI(op) = 0;
}

/* Note: we convert the 64 bit args to 32 bit and do some alignment
   and endian swap. Maybe it would be better to do the alignment
   and endian swap in tcg_reg_alloc_call(). */
void tcg_gen_callN(void *func, TCGTemp *ret, int nargs, TCGTemp **args)
{
    int i, real_args, nb_rets, pi;
    unsigned sizemask, flags;
    TCGHelperInfo *info;
    TCGOp *op;

    info = g_hash_table_lookup(helper_table, (gpointer)func);
    flags = info->flags;
    sizemask = info->sizemask;

#ifdef CONFIG_PLUGIN
    /* detect non-plugin helpers */
    if (tcg_ctx->plugin_insn && unlikely(strncmp(info->name, "plugin_", 7))) {
        tcg_ctx->plugin_insn->calls_helpers = true;
    }
#endif

#if defined(__sparc__) && !defined(__arch64__) \
    && !defined(CONFIG_TCG_INTERPRETER)
    /* We have 64-bit values in one register, but need to pass as two
       separate parameters.  Split them.  */
    int orig_sizemask = sizemask;
    int orig_nargs = nargs;
    TCGv_i64 retl, reth;
    TCGTemp *split_args[MAX_OPC_PARAM];

    retl = NULL;
    reth = NULL;
    if (sizemask != 0) {
        for (i = real_args = 0; i < nargs; ++i) {
            int is_64bit = sizemask & (1 << (i+1)*2);
            if (is_64bit) {
                TCGv_i64 orig = temp_tcgv_i64(args[i]);
                TCGv_i32 h = tcg_temp_new_i32();
                TCGv_i32 l = tcg_temp_new_i32();
                tcg_gen_extr_i64_i32(l, h, orig);
                split_args[real_args++] = tcgv_i32_temp(h);
                split_args[real_args++] = tcgv_i32_temp(l);
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
            TCGv_i64 orig = temp_tcgv_i64(args[i]);
            if (is_signed) {
                tcg_gen_ext32s_i64(temp, orig);
            } else {
                tcg_gen_ext32u_i64(temp, orig);
            }
            args[i] = tcgv_i64_temp(temp);
        }
    }
#endif /* TCG_TARGET_EXTEND_ARGS */

    op = tcg_emit_op(INDEX_op_call);

    pi = 0;
    if (ret != NULL) {
#if defined(__sparc__) && !defined(__arch64__) \
    && !defined(CONFIG_TCG_INTERPRETER)
        if (orig_sizemask & 1) {
            /* The 32-bit ABI is going to return the 64-bit value in
               the %o0/%o1 register pair.  Prepare for this by using
               two return temporaries, and reassemble below.  */
            retl = tcg_temp_new_i64();
            reth = tcg_temp_new_i64();
            op->args[pi++] = tcgv_i64_arg(reth);
            op->args[pi++] = tcgv_i64_arg(retl);
            nb_rets = 2;
        } else {
            op->args[pi++] = temp_arg(ret);
            nb_rets = 1;
        }
#else
        if (TCG_TARGET_REG_BITS < 64 && (sizemask & 1)) {
#ifdef HOST_WORDS_BIGENDIAN
            op->args[pi++] = temp_arg(ret + 1);
            op->args[pi++] = temp_arg(ret);
#else
            op->args[pi++] = temp_arg(ret);
            op->args[pi++] = temp_arg(ret + 1);
#endif
            nb_rets = 2;
        } else {
            op->args[pi++] = temp_arg(ret);
            nb_rets = 1;
        }
#endif
    } else {
        nb_rets = 0;
    }
    TCGOP_CALLO(op) = nb_rets;

    real_args = 0;
    for (i = 0; i < nargs; i++) {
        int is_64bit = sizemask & (1 << (i+1)*2);
        if (TCG_TARGET_REG_BITS < 64 && is_64bit) {
#ifdef TCG_TARGET_CALL_ALIGN_ARGS
            /* some targets want aligned 64 bit args */
            if (real_args & 1) {
                op->args[pi++] = TCG_CALL_DUMMY_ARG;
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
            op->args[pi++] = temp_arg(args[i] + 1);
            op->args[pi++] = temp_arg(args[i]);
#else
            op->args[pi++] = temp_arg(args[i]);
            op->args[pi++] = temp_arg(args[i] + 1);
#endif
            real_args += 2;
            continue;
        }

        op->args[pi++] = temp_arg(args[i]);
        real_args++;
    }
    op->args[pi++] = (uintptr_t)func;
    op->args[pi++] = flags;
    TCGOP_CALLI(op) = real_args;

    /* Make sure the fields didn't overflow.  */
    tcg_debug_assert(TCGOP_CALLI(op) == real_args);
    tcg_debug_assert(pi <= ARRAY_SIZE(op->args));

#if defined(__sparc__) && !defined(__arch64__) \
    && !defined(CONFIG_TCG_INTERPRETER)
    /* Free all of the parts we allocated above.  */
    for (i = real_args = 0; i < orig_nargs; ++i) {
        int is_64bit = orig_sizemask & (1 << (i+1)*2);
        if (is_64bit) {
            tcg_temp_free_internal(args[real_args++]);
            tcg_temp_free_internal(args[real_args++]);
        } else {
            real_args++;
        }
    }
    if (orig_sizemask & 1) {
        /* The 32-bit ABI returned two 32-bit pieces.  Re-assemble them.
           Note that describing these as TCGv_i64 eliminates an unnecessary
           zero-extension that tcg_gen_concat_i32_i64 would create.  */
        tcg_gen_concat32_i64(temp_tcgv_i64(ret), retl, reth);
        tcg_temp_free_i64(retl);
        tcg_temp_free_i64(reth);
    }
#elif defined(TCG_TARGET_EXTEND_ARGS) && TCG_TARGET_REG_BITS == 64
    for (i = 0; i < nargs; ++i) {
        int is_64bit = sizemask & (1 << (i+1)*2);
        if (!is_64bit) {
            tcg_temp_free_internal(args[i]);
        }
    }
#endif /* TCG_TARGET_EXTEND_ARGS */
}

static void tcg_reg_alloc_start(TCGContext *s)
{
    int i, n;

    for (i = 0, n = s->nb_temps; i < n; i++) {
        TCGTemp *ts = &s->temps[i];
        TCGTempVal val = TEMP_VAL_MEM;

        switch (ts->kind) {
        case TEMP_CONST:
            val = TEMP_VAL_CONST;
            break;
        case TEMP_FIXED:
            val = TEMP_VAL_REG;
            break;
        case TEMP_GLOBAL:
            break;
        case TEMP_NORMAL:
            val = TEMP_VAL_DEAD;
            /* fall through */
        case TEMP_LOCAL:
            ts->mem_allocated = 0;
            break;
        default:
            g_assert_not_reached();
        }
        ts->val_type = val;
    }

    memset(s->reg_to_temp, 0, sizeof(s->reg_to_temp));
}

static char *tcg_get_arg_str_ptr(TCGContext *s, char *buf, int buf_size,
                                 TCGTemp *ts)
{
    int idx = temp_idx(ts);

    switch (ts->kind) {
    case TEMP_FIXED:
    case TEMP_GLOBAL:
        pstrcpy(buf, buf_size, ts->name);
        break;
    case TEMP_LOCAL:
        snprintf(buf, buf_size, "loc%d", idx - s->nb_globals);
        break;
    case TEMP_NORMAL:
        snprintf(buf, buf_size, "tmp%d", idx - s->nb_globals);
        break;
    case TEMP_CONST:
        switch (ts->type) {
        case TCG_TYPE_I32:
            snprintf(buf, buf_size, "$0x%x", (int32_t)ts->val);
            break;
#if TCG_TARGET_REG_BITS > 32
        case TCG_TYPE_I64:
            snprintf(buf, buf_size, "$0x%" PRIx64, ts->val);
            break;
#endif
        case TCG_TYPE_V64:
        case TCG_TYPE_V128:
        case TCG_TYPE_V256:
            snprintf(buf, buf_size, "v%d$0x%" PRIx64,
                     64 << (ts->type - TCG_TYPE_V64), ts->val);
            break;
        default:
            g_assert_not_reached();
        }
        break;
    }
    return buf;
}

static char *tcg_get_arg_str(TCGContext *s, char *buf,
                             int buf_size, TCGArg arg)
{
    return tcg_get_arg_str_ptr(s, buf, buf_size, arg_temp(arg));
}

/* Find helper name.  */
static inline const char *tcg_find_helper(TCGContext *s, uintptr_t val)
{
    const char *ret = NULL;
    if (helper_table) {
        TCGHelperInfo *info = g_hash_table_lookup(helper_table, (gpointer)val);
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
#ifdef TARGET_ALIGNED_ONLY
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

static inline bool tcg_regset_single(TCGRegSet d)
{
    return (d & (d - 1)) == 0;
}

static inline TCGReg tcg_regset_first(TCGRegSet d)
{
    if (TCG_TARGET_NB_REGS <= 32) {
        return ctz32(d);
    } else {
        return ctz64(d);
    }
}

static void tcg_dump_ops(TCGContext *s, bool have_prefs)
{
    char buf[128];
    TCGOp *op;

    QTAILQ_FOREACH(op, &s->ops, link) {
        int i, k, nb_oargs, nb_iargs, nb_cargs;
        const TCGOpDef *def;
        TCGOpcode c;
        int col = 0;

        c = op->opc;
        def = &tcg_op_defs[c];

        if (c == INDEX_op_insn_start) {
            nb_oargs = 0;
            col += qemu_log("\n ----");

            for (i = 0; i < TARGET_INSN_START_WORDS; ++i) {
                target_ulong a;
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
                a = deposit64(op->args[i * 2], 32, 32, op->args[i * 2 + 1]);
#else
                a = op->args[i];
#endif
                col += qemu_log(" " TARGET_FMT_lx, a);
            }
        } else if (c == INDEX_op_call) {
            /* variable number of arguments */
            nb_oargs = TCGOP_CALLO(op);
            nb_iargs = TCGOP_CALLI(op);
            nb_cargs = def->nb_cargs;

            /* function name, flags, out args */
            col += qemu_log(" %s %s,$0x%" TCG_PRIlx ",$%d", def->name,
                            tcg_find_helper(s, op->args[nb_oargs + nb_iargs]),
                            op->args[nb_oargs + nb_iargs + 1], nb_oargs);
            for (i = 0; i < nb_oargs; i++) {
                col += qemu_log(",%s", tcg_get_arg_str(s, buf, sizeof(buf),
                                                       op->args[i]));
            }
            for (i = 0; i < nb_iargs; i++) {
                TCGArg arg = op->args[nb_oargs + i];
                const char *t = "<dummy>";
                if (arg != TCG_CALL_DUMMY_ARG) {
                    t = tcg_get_arg_str(s, buf, sizeof(buf), arg);
                }
                col += qemu_log(",%s", t);
            }
        } else {
            col += qemu_log(" %s ", def->name);

            nb_oargs = def->nb_oargs;
            nb_iargs = def->nb_iargs;
            nb_cargs = def->nb_cargs;

            if (def->flags & TCG_OPF_VECTOR) {
                col += qemu_log("v%d,e%d,", 64 << TCGOP_VECL(op),
                                8 << TCGOP_VECE(op));
            }

            k = 0;
            for (i = 0; i < nb_oargs; i++) {
                if (k != 0) {
                    col += qemu_log(",");
                }
                col += qemu_log("%s", tcg_get_arg_str(s, buf, sizeof(buf),
                                                      op->args[k++]));
            }
            for (i = 0; i < nb_iargs; i++) {
                if (k != 0) {
                    col += qemu_log(",");
                }
                col += qemu_log("%s", tcg_get_arg_str(s, buf, sizeof(buf),
                                                      op->args[k++]));
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
            case INDEX_op_cmp_vec:
            case INDEX_op_cmpsel_vec:
                if (op->args[k] < ARRAY_SIZE(cond_name)
                    && cond_name[op->args[k]]) {
                    col += qemu_log(",%s", cond_name[op->args[k++]]);
                } else {
                    col += qemu_log(",$0x%" TCG_PRIlx, op->args[k++]);
                }
                i = 1;
                break;
            case INDEX_op_qemu_ld_i32:
            case INDEX_op_qemu_st_i32:
            case INDEX_op_qemu_st8_i32:
            case INDEX_op_qemu_ld_i64:
            case INDEX_op_qemu_st_i64:
                {
                    TCGMemOpIdx oi = op->args[k++];
                    MemOp op = get_memop(oi);
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
                col += qemu_log("%s$L%d", k ? "," : "",
                                arg_label(op->args[k])->id);
                i++, k++;
                break;
            default:
                break;
            }
            for (; i < nb_cargs; i++, k++) {
                col += qemu_log("%s$0x%" TCG_PRIlx, k ? "," : "", op->args[k]);
            }
        }

        if (have_prefs || op->life) {

            QemuLogFile *logfile;

            rcu_read_lock();
            logfile = qatomic_rcu_read(&qemu_logfile);
            if (logfile) {
                for (; col < 40; ++col) {
                    putc(' ', logfile->fd);
                }
            }
            rcu_read_unlock();
        }

        if (op->life) {
            unsigned life = op->life;

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

        if (have_prefs) {
            for (i = 0; i < nb_oargs; ++i) {
                TCGRegSet set = op->output_pref[i];

                if (i == 0) {
                    qemu_log("  pref=");
                } else {
                    qemu_log(",");
                }
                if (set == 0) {
                    qemu_log("none");
                } else if (set == MAKE_64BIT_MASK(0, TCG_TARGET_NB_REGS)) {
                    qemu_log("all");
#ifdef CONFIG_DEBUG_TCG
                } else if (tcg_regset_single(set)) {
                    TCGReg reg = tcg_regset_first(set);
                    qemu_log("%s", tcg_target_reg_names[reg]);
#endif
                } else if (TCG_TARGET_NB_REGS <= 32) {
                    qemu_log("%#x", (uint32_t)set);
                } else {
                    qemu_log("%#" PRIx64, (uint64_t)set);
                }
            }
        }

        qemu_log("\n");
    }
}

/* we give more priority to constraints with less registers */
static int get_constraint_priority(const TCGOpDef *def, int k)
{
    const TCGArgConstraint *arg_ct = &def->args_ct[k];
    int n;

    if (arg_ct->oalias) {
        /* an alias is equivalent to a single register */
        n = 1;
    } else {
        n = ctpop64(arg_ct->regs);
    }
    return TCG_TARGET_NB_REGS - n + 1;
}

/* sort from highest priority to lowest */
static void sort_constraints(TCGOpDef *def, int start, int n)
{
    int i, j;
    TCGArgConstraint *a = def->args_ct;

    for (i = 0; i < n; i++) {
        a[start + i].sort_index = start + i;
    }
    if (n <= 1) {
        return;
    }
    for (i = 0; i < n - 1; i++) {
        for (j = i + 1; j < n; j++) {
            int p1 = get_constraint_priority(def, a[start + i].sort_index);
            int p2 = get_constraint_priority(def, a[start + j].sort_index);
            if (p1 < p2) {
                int tmp = a[start + i].sort_index;
                a[start + i].sort_index = a[start + j].sort_index;
                a[start + j].sort_index = tmp;
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
        int i, nb_args;

        if (def->flags & TCG_OPF_NOT_PRESENT) {
            continue;
        }

        nb_args = def->nb_iargs + def->nb_oargs;
        if (nb_args == 0) {
            continue;
        }

        /*
         * Macro magic should make it impossible, but double-check that
         * the array index is in range.  Since the signness of an enum
         * is implementation defined, force the result to unsigned.
         */
        unsigned con_set = tcg_target_op_def(op);
        tcg_debug_assert(con_set < ARRAY_SIZE(constraint_sets));
        tdefs = &constraint_sets[con_set];

        for (i = 0; i < nb_args; i++) {
            const char *ct_str = tdefs->args_ct_str[i];
            /* Incomplete TCGTargetOpDef entry. */
            tcg_debug_assert(ct_str != NULL);

            while (*ct_str != '\0') {
                switch(*ct_str) {
                case '0' ... '9':
                    {
                        int oarg = *ct_str - '0';
                        tcg_debug_assert(ct_str == tdefs->args_ct_str[i]);
                        tcg_debug_assert(oarg < def->nb_oargs);
                        tcg_debug_assert(def->args_ct[oarg].regs != 0);
                        def->args_ct[i] = def->args_ct[oarg];
                        /* The output sets oalias.  */
                        def->args_ct[oarg].oalias = true;
                        def->args_ct[oarg].alias_index = i;
                        /* The input sets ialias. */
                        def->args_ct[i].ialias = true;
                        def->args_ct[i].alias_index = oarg;
                    }
                    ct_str++;
                    break;
                case '&':
                    def->args_ct[i].newreg = true;
                    ct_str++;
                    break;
                case 'i':
                    def->args_ct[i].ct |= TCG_CT_CONST;
                    ct_str++;
                    break;

                /* Include all of the target-specific constraints. */

#undef CONST
#define CONST(CASE, MASK) \
    case CASE: def->args_ct[i].ct |= MASK; ct_str++; break;
#define REGS(CASE, MASK) \
    case CASE: def->args_ct[i].regs |= MASK; ct_str++; break;

#include "tcg-target-con-str.h"

#undef REGS
#undef CONST
                default:
                    /* Typo in TCGTargetOpDef constraint. */
                    g_assert_not_reached();
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
    TCGLabel *label;

    switch (op->opc) {
    case INDEX_op_br:
        label = arg_label(op->args[0]);
        label->refs--;
        break;
    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        label = arg_label(op->args[3]);
        label->refs--;
        break;
    case INDEX_op_brcond2_i32:
        label = arg_label(op->args[5]);
        label->refs--;
        break;
    default:
        break;
    }

    QTAILQ_REMOVE(&s->ops, op, link);
    QTAILQ_INSERT_TAIL(&s->free_ops, op, link);
    s->nb_ops--;

#ifdef CONFIG_PROFILER
    qatomic_set(&s->prof.del_op_count, s->prof.del_op_count + 1);
#endif
}

static TCGOp *tcg_op_alloc(TCGOpcode opc)
{
    TCGContext *s = tcg_ctx;
    TCGOp *op;

    if (likely(QTAILQ_EMPTY(&s->free_ops))) {
        op = tcg_malloc(sizeof(TCGOp));
    } else {
        op = QTAILQ_FIRST(&s->free_ops);
        QTAILQ_REMOVE(&s->free_ops, op, link);
    }
    memset(op, 0, offsetof(TCGOp, link));
    op->opc = opc;
    s->nb_ops++;

    return op;
}

TCGOp *tcg_emit_op(TCGOpcode opc)
{
    TCGOp *op = tcg_op_alloc(opc);
    QTAILQ_INSERT_TAIL(&tcg_ctx->ops, op, link);
    return op;
}

TCGOp *tcg_op_insert_before(TCGContext *s, TCGOp *old_op, TCGOpcode opc)
{
    TCGOp *new_op = tcg_op_alloc(opc);
    QTAILQ_INSERT_BEFORE(old_op, new_op, link);
    return new_op;
}

TCGOp *tcg_op_insert_after(TCGContext *s, TCGOp *old_op, TCGOpcode opc)
{
    TCGOp *new_op = tcg_op_alloc(opc);
    QTAILQ_INSERT_AFTER(&s->ops, old_op, new_op, link);
    return new_op;
}

/* Reachable analysis : remove unreachable code.  */
static void reachable_code_pass(TCGContext *s)
{
    TCGOp *op, *op_next;
    bool dead = false;

    QTAILQ_FOREACH_SAFE(op, &s->ops, link, op_next) {
        bool remove = dead;
        TCGLabel *label;
        int call_flags;

        switch (op->opc) {
        case INDEX_op_set_label:
            label = arg_label(op->args[0]);
            if (label->refs == 0) {
                /*
                 * While there is an occasional backward branch, virtually
                 * all branches generated by the translators are forward.
                 * Which means that generally we will have already removed
                 * all references to the label that will be, and there is
                 * little to be gained by iterating.
                 */
                remove = true;
            } else {
                /* Once we see a label, insns become live again.  */
                dead = false;
                remove = false;

                /*
                 * Optimization can fold conditional branches to unconditional.
                 * If we find a label with one reference which is preceded by
                 * an unconditional branch to it, remove both.  This needed to
                 * wait until the dead code in between them was removed.
                 */
                if (label->refs == 1) {
                    TCGOp *op_prev = QTAILQ_PREV(op, link);
                    if (op_prev->opc == INDEX_op_br &&
                        label == arg_label(op_prev->args[0])) {
                        tcg_op_remove(s, op_prev);
                        remove = true;
                    }
                }
            }
            break;

        case INDEX_op_br:
        case INDEX_op_exit_tb:
        case INDEX_op_goto_ptr:
            /* Unconditional branches; everything following is dead.  */
            dead = true;
            break;

        case INDEX_op_call:
            /* Notice noreturn helper calls, raising exceptions.  */
            call_flags = op->args[TCGOP_CALLO(op) + TCGOP_CALLI(op) + 1];
            if (call_flags & TCG_CALL_NO_RETURN) {
                dead = true;
            }
            break;

        case INDEX_op_insn_start:
            /* Never remove -- we need to keep these for unwind.  */
            remove = false;
            break;

        default:
            break;
        }

        if (remove) {
            tcg_op_remove(s, op);
        }
    }
}

#define TS_DEAD  1
#define TS_MEM   2

#define IS_DEAD_ARG(n)   (arg_life & (DEAD_ARG << (n)))
#define NEED_SYNC_ARG(n) (arg_life & (SYNC_ARG << (n)))

/* For liveness_pass_1, the register preferences for a given temp.  */
static inline TCGRegSet *la_temp_pref(TCGTemp *ts)
{
    return ts->state_ptr;
}

/* For liveness_pass_1, reset the preferences for a given temp to the
 * maximal regset for its type.
 */
static inline void la_reset_pref(TCGTemp *ts)
{
    *la_temp_pref(ts)
        = (ts->state == TS_DEAD ? 0 : tcg_target_available_regs[ts->type]);
}

/* liveness analysis: end of function: all temps are dead, and globals
   should be in memory. */
static void la_func_end(TCGContext *s, int ng, int nt)
{
    int i;

    for (i = 0; i < ng; ++i) {
        s->temps[i].state = TS_DEAD | TS_MEM;
        la_reset_pref(&s->temps[i]);
    }
    for (i = ng; i < nt; ++i) {
        s->temps[i].state = TS_DEAD;
        la_reset_pref(&s->temps[i]);
    }
}

/* liveness analysis: end of basic block: all temps are dead, globals
   and local temps should be in memory. */
static void la_bb_end(TCGContext *s, int ng, int nt)
{
    int i;

    for (i = 0; i < nt; ++i) {
        TCGTemp *ts = &s->temps[i];
        int state;

        switch (ts->kind) {
        case TEMP_FIXED:
        case TEMP_GLOBAL:
        case TEMP_LOCAL:
            state = TS_DEAD | TS_MEM;
            break;
        case TEMP_NORMAL:
        case TEMP_CONST:
            state = TS_DEAD;
            break;
        default:
            g_assert_not_reached();
        }
        ts->state = state;
        la_reset_pref(ts);
    }
}

/* liveness analysis: sync globals back to memory.  */
static void la_global_sync(TCGContext *s, int ng)
{
    int i;

    for (i = 0; i < ng; ++i) {
        int state = s->temps[i].state;
        s->temps[i].state = state | TS_MEM;
        if (state == TS_DEAD) {
            /* If the global was previously dead, reset prefs.  */
            la_reset_pref(&s->temps[i]);
        }
    }
}

/*
 * liveness analysis: conditional branch: all temps are dead,
 * globals and local temps should be synced.
 */
static void la_bb_sync(TCGContext *s, int ng, int nt)
{
    la_global_sync(s, ng);

    for (int i = ng; i < nt; ++i) {
        TCGTemp *ts = &s->temps[i];
        int state;

        switch (ts->kind) {
        case TEMP_LOCAL:
            state = ts->state;
            ts->state = state | TS_MEM;
            if (state != TS_DEAD) {
                continue;
            }
            break;
        case TEMP_NORMAL:
            s->temps[i].state = TS_DEAD;
            break;
        case TEMP_CONST:
            continue;
        default:
            g_assert_not_reached();
        }
        la_reset_pref(&s->temps[i]);
    }
}

/* liveness analysis: sync globals back to memory and kill.  */
static void la_global_kill(TCGContext *s, int ng)
{
    int i;

    for (i = 0; i < ng; i++) {
        s->temps[i].state = TS_DEAD | TS_MEM;
        la_reset_pref(&s->temps[i]);
    }
}

/* liveness analysis: note live globals crossing calls.  */
static void la_cross_call(TCGContext *s, int nt)
{
    TCGRegSet mask = ~tcg_target_call_clobber_regs;
    int i;

    for (i = 0; i < nt; i++) {
        TCGTemp *ts = &s->temps[i];
        if (!(ts->state & TS_DEAD)) {
            TCGRegSet *pset = la_temp_pref(ts);
            TCGRegSet set = *pset;

            set &= mask;
            /* If the combination is not possible, restart.  */
            if (set == 0) {
                set = tcg_target_available_regs[ts->type] & mask;
            }
            *pset = set;
        }
    }
}

/* Liveness analysis : update the opc_arg_life array to tell if a
   given input arguments is dead. Instructions updating dead
   temporaries are removed. */
static void liveness_pass_1(TCGContext *s)
{
    int nb_globals = s->nb_globals;
    int nb_temps = s->nb_temps;
    TCGOp *op, *op_prev;
    TCGRegSet *prefs;
    int i;

    prefs = tcg_malloc(sizeof(TCGRegSet) * nb_temps);
    for (i = 0; i < nb_temps; ++i) {
        s->temps[i].state_ptr = prefs + i;
    }

    /* ??? Should be redundant with the exit_tb that ends the TB.  */
    la_func_end(s, nb_globals, nb_temps);

    QTAILQ_FOREACH_REVERSE_SAFE(op, &s->ops, link, op_prev) {
        int nb_iargs, nb_oargs;
        TCGOpcode opc_new, opc_new2;
        bool have_opc_new2;
        TCGLifeData arg_life = 0;
        TCGTemp *ts;
        TCGOpcode opc = op->opc;
        const TCGOpDef *def = &tcg_op_defs[opc];

        switch (opc) {
        case INDEX_op_call:
            {
                int call_flags;
                int nb_call_regs;

                nb_oargs = TCGOP_CALLO(op);
                nb_iargs = TCGOP_CALLI(op);
                call_flags = op->args[nb_oargs + nb_iargs + 1];

                /* pure functions can be removed if their result is unused */
                if (call_flags & TCG_CALL_NO_SIDE_EFFECTS) {
                    for (i = 0; i < nb_oargs; i++) {
                        ts = arg_temp(op->args[i]);
                        if (ts->state != TS_DEAD) {
                            goto do_not_remove_call;
                        }
                    }
                    goto do_remove;
                }
            do_not_remove_call:

                /* Output args are dead.  */
                for (i = 0; i < nb_oargs; i++) {
                    ts = arg_temp(op->args[i]);
                    if (ts->state & TS_DEAD) {
                        arg_life |= DEAD_ARG << i;
                    }
                    if (ts->state & TS_MEM) {
                        arg_life |= SYNC_ARG << i;
                    }
                    ts->state = TS_DEAD;
                    la_reset_pref(ts);

                    /* Not used -- it will be tcg_target_call_oarg_regs[i].  */
                    op->output_pref[i] = 0;
                }

                if (!(call_flags & (TCG_CALL_NO_WRITE_GLOBALS |
                                    TCG_CALL_NO_READ_GLOBALS))) {
                    la_global_kill(s, nb_globals);
                } else if (!(call_flags & TCG_CALL_NO_READ_GLOBALS)) {
                    la_global_sync(s, nb_globals);
                }

                /* Record arguments that die in this helper.  */
                for (i = nb_oargs; i < nb_iargs + nb_oargs; i++) {
                    ts = arg_temp(op->args[i]);
                    if (ts && ts->state & TS_DEAD) {
                        arg_life |= DEAD_ARG << i;
                    }
                }

                /* For all live registers, remove call-clobbered prefs.  */
                la_cross_call(s, nb_temps);

                nb_call_regs = ARRAY_SIZE(tcg_target_call_iarg_regs);

                /* Input arguments are live for preceding opcodes.  */
                for (i = 0; i < nb_iargs; i++) {
                    ts = arg_temp(op->args[i + nb_oargs]);
                    if (ts && ts->state & TS_DEAD) {
                        /* For those arguments that die, and will be allocated
                         * in registers, clear the register set for that arg,
                         * to be filled in below.  For args that will be on
                         * the stack, reset to any available reg.
                         */
                        *la_temp_pref(ts)
                            = (i < nb_call_regs ? 0 :
                               tcg_target_available_regs[ts->type]);
                        ts->state &= ~TS_DEAD;
                    }
                }

                /* For each input argument, add its input register to prefs.
                   If a temp is used once, this produces a single set bit.  */
                for (i = 0; i < MIN(nb_call_regs, nb_iargs); i++) {
                    ts = arg_temp(op->args[i + nb_oargs]);
                    if (ts) {
                        tcg_regset_set_reg(*la_temp_pref(ts),
                                           tcg_target_call_iarg_regs[i]);
                    }
                }
            }
            break;
        case INDEX_op_insn_start:
            break;
        case INDEX_op_discard:
            /* mark the temporary as dead */
            ts = arg_temp(op->args[0]);
            ts->state = TS_DEAD;
            la_reset_pref(ts);
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
            if (arg_temp(op->args[1])->state == TS_DEAD) {
                if (arg_temp(op->args[0])->state == TS_DEAD) {
                    goto do_remove;
                }
                /* Replace the opcode and adjust the args in place,
                   leaving 3 unused args at the end.  */
                op->opc = opc = opc_new;
                op->args[1] = op->args[2];
                op->args[2] = op->args[4];
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
            if (arg_temp(op->args[1])->state == TS_DEAD) {
                if (arg_temp(op->args[0])->state == TS_DEAD) {
                    /* Both parts of the operation are dead.  */
                    goto do_remove;
                }
                /* The high part of the operation is dead; generate the low. */
                op->opc = opc = opc_new;
                op->args[1] = op->args[2];
                op->args[2] = op->args[3];
            } else if (arg_temp(op->args[0])->state == TS_DEAD && have_opc_new2) {
                /* The low part of the operation is dead; generate the high. */
                op->opc = opc = opc_new2;
                op->args[0] = op->args[1];
                op->args[1] = op->args[2];
                op->args[2] = op->args[3];
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
                    if (arg_temp(op->args[i])->state != TS_DEAD) {
                        goto do_not_remove;
                    }
                }
                goto do_remove;
            }
            goto do_not_remove;

        do_remove:
            tcg_op_remove(s, op);
            break;

        do_not_remove:
            for (i = 0; i < nb_oargs; i++) {
                ts = arg_temp(op->args[i]);

                /* Remember the preference of the uses that followed.  */
                op->output_pref[i] = *la_temp_pref(ts);

                /* Output args are dead.  */
                if (ts->state & TS_DEAD) {
                    arg_life |= DEAD_ARG << i;
                }
                if (ts->state & TS_MEM) {
                    arg_life |= SYNC_ARG << i;
                }
                ts->state = TS_DEAD;
                la_reset_pref(ts);
            }

            /* If end of basic block, update.  */
            if (def->flags & TCG_OPF_BB_EXIT) {
                la_func_end(s, nb_globals, nb_temps);
            } else if (def->flags & TCG_OPF_COND_BRANCH) {
                la_bb_sync(s, nb_globals, nb_temps);
            } else if (def->flags & TCG_OPF_BB_END) {
                la_bb_end(s, nb_globals, nb_temps);
            } else if (def->flags & TCG_OPF_SIDE_EFFECTS) {
                la_global_sync(s, nb_globals);
                if (def->flags & TCG_OPF_CALL_CLOBBER) {
                    la_cross_call(s, nb_temps);
                }
            }

            /* Record arguments that die in this opcode.  */
            for (i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
                ts = arg_temp(op->args[i]);
                if (ts->state & TS_DEAD) {
                    arg_life |= DEAD_ARG << i;
                }
            }

            /* Input arguments are live for preceding opcodes.  */
            for (i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
                ts = arg_temp(op->args[i]);
                if (ts->state & TS_DEAD) {
                    /* For operands that were dead, initially allow
                       all regs for the type.  */
                    *la_temp_pref(ts) = tcg_target_available_regs[ts->type];
                    ts->state &= ~TS_DEAD;
                }
            }

            /* Incorporate constraints for this operand.  */
            switch (opc) {
            case INDEX_op_mov_i32:
            case INDEX_op_mov_i64:
                /* Note that these are TCG_OPF_NOT_PRESENT and do not
                   have proper constraints.  That said, special case
                   moves to propagate preferences backward.  */
                if (IS_DEAD_ARG(1)) {
                    *la_temp_pref(arg_temp(op->args[0]))
                        = *la_temp_pref(arg_temp(op->args[1]));
                }
                break;

            default:
                for (i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
                    const TCGArgConstraint *ct = &def->args_ct[i];
                    TCGRegSet set, *pset;

                    ts = arg_temp(op->args[i]);
                    pset = la_temp_pref(ts);
                    set = *pset;

                    set &= ct->regs;
                    if (ct->ialias) {
                        set &= op->output_pref[ct->alias_index];
                    }
                    /* If the combination is not possible, restart.  */
                    if (set == 0) {
                        set = ct->regs;
                    }
                    *pset = set;
                }
                break;
            }
            break;
        }
        op->life = arg_life;
    }
}

/* Liveness analysis: Convert indirect regs to direct temporaries.  */
static bool liveness_pass_2(TCGContext *s)
{
    int nb_globals = s->nb_globals;
    int nb_temps, i;
    bool changes = false;
    TCGOp *op, *op_next;

    /* Create a temporary for each indirect global.  */
    for (i = 0; i < nb_globals; ++i) {
        TCGTemp *its = &s->temps[i];
        if (its->indirect_reg) {
            TCGTemp *dts = tcg_temp_alloc(s);
            dts->type = its->type;
            dts->base_type = its->base_type;
            its->state_ptr = dts;
        } else {
            its->state_ptr = NULL;
        }
        /* All globals begin dead.  */
        its->state = TS_DEAD;
    }
    for (nb_temps = s->nb_temps; i < nb_temps; ++i) {
        TCGTemp *its = &s->temps[i];
        its->state_ptr = NULL;
        its->state = TS_DEAD;
    }

    QTAILQ_FOREACH_SAFE(op, &s->ops, link, op_next) {
        TCGOpcode opc = op->opc;
        const TCGOpDef *def = &tcg_op_defs[opc];
        TCGLifeData arg_life = op->life;
        int nb_iargs, nb_oargs, call_flags;
        TCGTemp *arg_ts, *dir_ts;

        if (opc == INDEX_op_call) {
            nb_oargs = TCGOP_CALLO(op);
            nb_iargs = TCGOP_CALLI(op);
            call_flags = op->args[nb_oargs + nb_iargs + 1];
        } else {
            nb_iargs = def->nb_iargs;
            nb_oargs = def->nb_oargs;

            /* Set flags similar to how calls require.  */
            if (def->flags & TCG_OPF_COND_BRANCH) {
                /* Like reading globals: sync_globals */
                call_flags = TCG_CALL_NO_WRITE_GLOBALS;
            } else if (def->flags & TCG_OPF_BB_END) {
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
            arg_ts = arg_temp(op->args[i]);
            if (arg_ts) {
                dir_ts = arg_ts->state_ptr;
                if (dir_ts && arg_ts->state == TS_DEAD) {
                    TCGOpcode lopc = (arg_ts->type == TCG_TYPE_I32
                                      ? INDEX_op_ld_i32
                                      : INDEX_op_ld_i64);
                    TCGOp *lop = tcg_op_insert_before(s, op, lopc);

                    lop->args[0] = temp_arg(dir_ts);
                    lop->args[1] = temp_arg(arg_ts->mem_base);
                    lop->args[2] = arg_ts->mem_offset;

                    /* Loaded, but synced with memory.  */
                    arg_ts->state = TS_MEM;
                }
            }
        }

        /* Perform input replacement, and mark inputs that became dead.
           No action is required except keeping temp_state up to date
           so that we reload when needed.  */
        for (i = nb_oargs; i < nb_iargs + nb_oargs; i++) {
            arg_ts = arg_temp(op->args[i]);
            if (arg_ts) {
                dir_ts = arg_ts->state_ptr;
                if (dir_ts) {
                    op->args[i] = temp_arg(dir_ts);
                    changes = true;
                    if (IS_DEAD_ARG(i)) {
                        arg_ts->state = TS_DEAD;
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
                arg_ts = &s->temps[i];
                tcg_debug_assert(arg_ts->state_ptr == 0
                                 || arg_ts->state != 0);
            }
        } else {
            for (i = 0; i < nb_globals; ++i) {
                /* Liveness should see that globals are saved back,
                   that is, TS_DEAD, waiting to be reloaded.  */
                arg_ts = &s->temps[i];
                tcg_debug_assert(arg_ts->state_ptr == 0
                                 || arg_ts->state == TS_DEAD);
            }
        }

        /* Outputs become available.  */
        if (opc == INDEX_op_mov_i32 || opc == INDEX_op_mov_i64) {
            arg_ts = arg_temp(op->args[0]);
            dir_ts = arg_ts->state_ptr;
            if (dir_ts) {
                op->args[0] = temp_arg(dir_ts);
                changes = true;

                /* The output is now live and modified.  */
                arg_ts->state = 0;

                if (NEED_SYNC_ARG(0)) {
                    TCGOpcode sopc = (arg_ts->type == TCG_TYPE_I32
                                      ? INDEX_op_st_i32
                                      : INDEX_op_st_i64);
                    TCGOp *sop = tcg_op_insert_after(s, op, sopc);
                    TCGTemp *out_ts = dir_ts;

                    if (IS_DEAD_ARG(0)) {
                        out_ts = arg_temp(op->args[1]);
                        arg_ts->state = TS_DEAD;
                        tcg_op_remove(s, op);
                    } else {
                        arg_ts->state = TS_MEM;
                    }

                    sop->args[0] = temp_arg(out_ts);
                    sop->args[1] = temp_arg(arg_ts->mem_base);
                    sop->args[2] = arg_ts->mem_offset;
                } else {
                    tcg_debug_assert(!IS_DEAD_ARG(0));
                }
            }
        } else {
            for (i = 0; i < nb_oargs; i++) {
                arg_ts = arg_temp(op->args[i]);
                dir_ts = arg_ts->state_ptr;
                if (!dir_ts) {
                    continue;
                }
                op->args[i] = temp_arg(dir_ts);
                changes = true;

                /* The output is now live and modified.  */
                arg_ts->state = 0;

                /* Sync outputs upon their last write.  */
                if (NEED_SYNC_ARG(i)) {
                    TCGOpcode sopc = (arg_ts->type == TCG_TYPE_I32
                                      ? INDEX_op_st_i32
                                      : INDEX_op_st_i64);
                    TCGOp *sop = tcg_op_insert_after(s, op, sopc);

                    sop->args[0] = temp_arg(dir_ts);
                    sop->args[1] = temp_arg(arg_ts->mem_base);
                    sop->args[2] = arg_ts->mem_offset;

                    arg_ts->state = TS_MEM;
                }
                /* Drop outputs that are dead.  */
                if (IS_DEAD_ARG(i)) {
                    arg_ts->state = TS_DEAD;
                }
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
        printf("  %10s: ", tcg_get_arg_str_ptr(s, buf, sizeof(buf), ts));
        switch(ts->val_type) {
        case TEMP_VAL_REG:
            printf("%s", tcg_target_reg_names[ts->reg]);
            break;
        case TEMP_VAL_MEM:
            printf("%d(%s)", (int)ts->mem_offset,
                   tcg_target_reg_names[ts->mem_base->reg]);
            break;
        case TEMP_VAL_CONST:
            printf("$0x%" PRIx64, ts->val);
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
        if (ts->val_type == TEMP_VAL_REG
            && ts->kind != TEMP_FIXED
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

static void temp_allocate_frame(TCGContext *s, TCGTemp *ts)
{
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

static void temp_load(TCGContext *, TCGTemp *, TCGRegSet, TCGRegSet, TCGRegSet);

/* Mark a temporary as free or dead.  If 'free_or_dead' is negative,
   mark it free; otherwise mark it dead.  */
static void temp_free_or_dead(TCGContext *s, TCGTemp *ts, int free_or_dead)
{
    TCGTempVal new_type;

    switch (ts->kind) {
    case TEMP_FIXED:
        return;
    case TEMP_GLOBAL:
    case TEMP_LOCAL:
        new_type = TEMP_VAL_MEM;
        break;
    case TEMP_NORMAL:
        new_type = free_or_dead < 0 ? TEMP_VAL_MEM : TEMP_VAL_DEAD;
        break;
    case TEMP_CONST:
        new_type = TEMP_VAL_CONST;
        break;
    default:
        g_assert_not_reached();
    }
    if (ts->val_type == TEMP_VAL_REG) {
        s->reg_to_temp[ts->reg] = NULL;
    }
    ts->val_type = new_type;
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
static void temp_sync(TCGContext *s, TCGTemp *ts, TCGRegSet allocated_regs,
                      TCGRegSet preferred_regs, int free_or_dead)
{
    if (!temp_readonly(ts) && !ts->mem_coherent) {
        if (!ts->mem_allocated) {
            temp_allocate_frame(s, ts);
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
                      allocated_regs, preferred_regs);
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
        temp_sync(s, ts, allocated_regs, 0, -1);
    }
}

/**
 * tcg_reg_alloc:
 * @required_regs: Set of registers in which we must allocate.
 * @allocated_regs: Set of registers which must be avoided.
 * @preferred_regs: Set of registers we should prefer.
 * @rev: True if we search the registers in "indirect" order.
 *
 * The allocated register must be in @required_regs & ~@allocated_regs,
 * but if we can put it in @preferred_regs we may save a move later.
 */
static TCGReg tcg_reg_alloc(TCGContext *s, TCGRegSet required_regs,
                            TCGRegSet allocated_regs,
                            TCGRegSet preferred_regs, bool rev)
{
    int i, j, f, n = ARRAY_SIZE(tcg_target_reg_alloc_order);
    TCGRegSet reg_ct[2];
    const int *order;

    reg_ct[1] = required_regs & ~allocated_regs;
    tcg_debug_assert(reg_ct[1] != 0);
    reg_ct[0] = reg_ct[1] & preferred_regs;

    /* Skip the preferred_regs option if it cannot be satisfied,
       or if the preference made no difference.  */
    f = reg_ct[0] == 0 || reg_ct[0] == reg_ct[1];

    order = rev ? indirect_reg_alloc_order : tcg_target_reg_alloc_order;

    /* Try free registers, preferences first.  */
    for (j = f; j < 2; j++) {
        TCGRegSet set = reg_ct[j];

        if (tcg_regset_single(set)) {
            /* One register in the set.  */
            TCGReg reg = tcg_regset_first(set);
            if (s->reg_to_temp[reg] == NULL) {
                return reg;
            }
        } else {
            for (i = 0; i < n; i++) {
                TCGReg reg = order[i];
                if (s->reg_to_temp[reg] == NULL &&
                    tcg_regset_test_reg(set, reg)) {
                    return reg;
                }
            }
        }
    }

    /* We must spill something.  */
    for (j = f; j < 2; j++) {
        TCGRegSet set = reg_ct[j];

        if (tcg_regset_single(set)) {
            /* One register in the set.  */
            TCGReg reg = tcg_regset_first(set);
            tcg_reg_free(s, reg, allocated_regs);
            return reg;
        } else {
            for (i = 0; i < n; i++) {
                TCGReg reg = order[i];
                if (tcg_regset_test_reg(set, reg)) {
                    tcg_reg_free(s, reg, allocated_regs);
                    return reg;
                }
            }
        }
    }

    tcg_abort();
}

/* Make sure the temporary is in a register.  If needed, allocate the register
   from DESIRED while avoiding ALLOCATED.  */
static void temp_load(TCGContext *s, TCGTemp *ts, TCGRegSet desired_regs,
                      TCGRegSet allocated_regs, TCGRegSet preferred_regs)
{
    TCGReg reg;

    switch (ts->val_type) {
    case TEMP_VAL_REG:
        return;
    case TEMP_VAL_CONST:
        reg = tcg_reg_alloc(s, desired_regs, allocated_regs,
                            preferred_regs, ts->indirect_base);
        if (ts->type <= TCG_TYPE_I64) {
            tcg_out_movi(s, ts->type, reg, ts->val);
        } else {
            uint64_t val = ts->val;
            MemOp vece = MO_64;

            /*
             * Find the minimal vector element that matches the constant.
             * The targets will, in general, have to do this search anyway,
             * do this generically.
             */
            if (val == dup_const(MO_8, val)) {
                vece = MO_8;
            } else if (val == dup_const(MO_16, val)) {
                vece = MO_16;
            } else if (val == dup_const(MO_32, val)) {
                vece = MO_32;
            }

            tcg_out_dupi_vec(s, ts->type, vece, reg, ts->val);
        }
        ts->mem_coherent = 0;
        break;
    case TEMP_VAL_MEM:
        reg = tcg_reg_alloc(s, desired_regs, allocated_regs,
                            preferred_regs, ts->indirect_base);
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
    tcg_debug_assert(ts->val_type == TEMP_VAL_MEM || temp_readonly(ts));
}

/* save globals to their canonical location and assume they can be
   modified be the following code. 'allocated_regs' is used in case a
   temporary registers needs to be allocated to store a constant. */
static void save_globals(TCGContext *s, TCGRegSet allocated_regs)
{
    int i, n;

    for (i = 0, n = s->nb_globals; i < n; i++) {
        temp_save(s, &s->temps[i], allocated_regs);
    }
}

/* sync globals to their canonical location and assume they can be
   read by the following code. 'allocated_regs' is used in case a
   temporary registers needs to be allocated to store a constant. */
static void sync_globals(TCGContext *s, TCGRegSet allocated_regs)
{
    int i, n;

    for (i = 0, n = s->nb_globals; i < n; i++) {
        TCGTemp *ts = &s->temps[i];
        tcg_debug_assert(ts->val_type != TEMP_VAL_REG
                         || ts->kind == TEMP_FIXED
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

        switch (ts->kind) {
        case TEMP_LOCAL:
            temp_save(s, ts, allocated_regs);
            break;
        case TEMP_NORMAL:
            /* The liveness analysis already ensures that temps are dead.
               Keep an tcg_debug_assert for safety. */
            tcg_debug_assert(ts->val_type == TEMP_VAL_DEAD);
            break;
        case TEMP_CONST:
            /* Similarly, we should have freed any allocated register. */
            tcg_debug_assert(ts->val_type == TEMP_VAL_CONST);
            break;
        default:
            g_assert_not_reached();
        }
    }

    save_globals(s, allocated_regs);
}

/*
 * At a conditional branch, we assume all temporaries are dead and
 * all globals and local temps are synced to their location.
 */
static void tcg_reg_alloc_cbranch(TCGContext *s, TCGRegSet allocated_regs)
{
    sync_globals(s, allocated_regs);

    for (int i = s->nb_globals; i < s->nb_temps; i++) {
        TCGTemp *ts = &s->temps[i];
        /*
         * The liveness analysis already ensures that temps are dead.
         * Keep tcg_debug_asserts for safety.
         */
        switch (ts->kind) {
        case TEMP_LOCAL:
            tcg_debug_assert(ts->val_type != TEMP_VAL_REG || ts->mem_coherent);
            break;
        case TEMP_NORMAL:
            tcg_debug_assert(ts->val_type == TEMP_VAL_DEAD);
            break;
        case TEMP_CONST:
            break;
        default:
            g_assert_not_reached();
        }
    }
}

/*
 * Specialized code generation for INDEX_op_mov_* with a constant.
 */
static void tcg_reg_alloc_do_movi(TCGContext *s, TCGTemp *ots,
                                  tcg_target_ulong val, TCGLifeData arg_life,
                                  TCGRegSet preferred_regs)
{
    /* ENV should not be modified.  */
    tcg_debug_assert(!temp_readonly(ots));

    /* The movi is not explicitly generated here.  */
    if (ots->val_type == TEMP_VAL_REG) {
        s->reg_to_temp[ots->reg] = NULL;
    }
    ots->val_type = TEMP_VAL_CONST;
    ots->val = val;
    ots->mem_coherent = 0;
    if (NEED_SYNC_ARG(0)) {
        temp_sync(s, ots, s->reserved_regs, preferred_regs, IS_DEAD_ARG(0));
    } else if (IS_DEAD_ARG(0)) {
        temp_dead(s, ots);
    }
}

/*
 * Specialized code generation for INDEX_op_mov_*.
 */
static void tcg_reg_alloc_mov(TCGContext *s, const TCGOp *op)
{
    const TCGLifeData arg_life = op->life;
    TCGRegSet allocated_regs, preferred_regs;
    TCGTemp *ts, *ots;
    TCGType otype, itype;

    allocated_regs = s->reserved_regs;
    preferred_regs = op->output_pref[0];
    ots = arg_temp(op->args[0]);
    ts = arg_temp(op->args[1]);

    /* ENV should not be modified.  */
    tcg_debug_assert(!temp_readonly(ots));

    /* Note that otype != itype for no-op truncation.  */
    otype = ots->type;
    itype = ts->type;

    if (ts->val_type == TEMP_VAL_CONST) {
        /* propagate constant or generate sti */
        tcg_target_ulong val = ts->val;
        if (IS_DEAD_ARG(1)) {
            temp_dead(s, ts);
        }
        tcg_reg_alloc_do_movi(s, ots, val, arg_life, preferred_regs);
        return;
    }

    /* If the source value is in memory we're going to be forced
       to have it in a register in order to perform the copy.  Copy
       the SOURCE value into its own register first, that way we
       don't have to reload SOURCE the next time it is used. */
    if (ts->val_type == TEMP_VAL_MEM) {
        temp_load(s, ts, tcg_target_available_regs[itype],
                  allocated_regs, preferred_regs);
    }

    tcg_debug_assert(ts->val_type == TEMP_VAL_REG);
    if (IS_DEAD_ARG(0)) {
        /* mov to a non-saved dead register makes no sense (even with
           liveness analysis disabled). */
        tcg_debug_assert(NEED_SYNC_ARG(0));
        if (!ots->mem_allocated) {
            temp_allocate_frame(s, ots);
        }
        tcg_out_st(s, otype, ts->reg, ots->mem_base->reg, ots->mem_offset);
        if (IS_DEAD_ARG(1)) {
            temp_dead(s, ts);
        }
        temp_dead(s, ots);
    } else {
        if (IS_DEAD_ARG(1) && ts->kind != TEMP_FIXED) {
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
                                         allocated_regs, preferred_regs,
                                         ots->indirect_base);
            }
            if (!tcg_out_mov(s, otype, ots->reg, ts->reg)) {
                /*
                 * Cross register class move not supported.
                 * Store the source register into the destination slot
                 * and leave the destination temp as TEMP_VAL_MEM.
                 */
                assert(!temp_readonly(ots));
                if (!ts->mem_allocated) {
                    temp_allocate_frame(s, ots);
                }
                tcg_out_st(s, ts->type, ts->reg,
                           ots->mem_base->reg, ots->mem_offset);
                ots->mem_coherent = 1;
                temp_free_or_dead(s, ots, -1);
                return;
            }
        }
        ots->val_type = TEMP_VAL_REG;
        ots->mem_coherent = 0;
        s->reg_to_temp[ots->reg] = ots;
        if (NEED_SYNC_ARG(0)) {
            temp_sync(s, ots, allocated_regs, 0, 0);
        }
    }
}

/*
 * Specialized code generation for INDEX_op_dup_vec.
 */
static void tcg_reg_alloc_dup(TCGContext *s, const TCGOp *op)
{
    const TCGLifeData arg_life = op->life;
    TCGRegSet dup_out_regs, dup_in_regs;
    TCGTemp *its, *ots;
    TCGType itype, vtype;
    intptr_t endian_fixup;
    unsigned vece;
    bool ok;

    ots = arg_temp(op->args[0]);
    its = arg_temp(op->args[1]);

    /* ENV should not be modified.  */
    tcg_debug_assert(!temp_readonly(ots));

    itype = its->type;
    vece = TCGOP_VECE(op);
    vtype = TCGOP_VECL(op) + TCG_TYPE_V64;

    if (its->val_type == TEMP_VAL_CONST) {
        /* Propagate constant via movi -> dupi.  */
        tcg_target_ulong val = its->val;
        if (IS_DEAD_ARG(1)) {
            temp_dead(s, its);
        }
        tcg_reg_alloc_do_movi(s, ots, val, arg_life, op->output_pref[0]);
        return;
    }

    dup_out_regs = tcg_op_defs[INDEX_op_dup_vec].args_ct[0].regs;
    dup_in_regs = tcg_op_defs[INDEX_op_dup_vec].args_ct[1].regs;

    /* Allocate the output register now.  */
    if (ots->val_type != TEMP_VAL_REG) {
        TCGRegSet allocated_regs = s->reserved_regs;

        if (!IS_DEAD_ARG(1) && its->val_type == TEMP_VAL_REG) {
            /* Make sure to not spill the input register. */
            tcg_regset_set_reg(allocated_regs, its->reg);
        }
        ots->reg = tcg_reg_alloc(s, dup_out_regs, allocated_regs,
                                 op->output_pref[0], ots->indirect_base);
        ots->val_type = TEMP_VAL_REG;
        ots->mem_coherent = 0;
        s->reg_to_temp[ots->reg] = ots;
    }

    switch (its->val_type) {
    case TEMP_VAL_REG:
        /*
         * The dup constriaints must be broad, covering all possible VECE.
         * However, tcg_op_dup_vec() gets to see the VECE and we allow it
         * to fail, indicating that extra moves are required for that case.
         */
        if (tcg_regset_test_reg(dup_in_regs, its->reg)) {
            if (tcg_out_dup_vec(s, vtype, vece, ots->reg, its->reg)) {
                goto done;
            }
            /* Try again from memory or a vector input register.  */
        }
        if (!its->mem_coherent) {
            /*
             * The input register is not synced, and so an extra store
             * would be required to use memory.  Attempt an integer-vector
             * register move first.  We do not have a TCGRegSet for this.
             */
            if (tcg_out_mov(s, itype, ots->reg, its->reg)) {
                break;
            }
            /* Sync the temp back to its slot and load from there.  */
            temp_sync(s, its, s->reserved_regs, 0, 0);
        }
        /* fall through */

    case TEMP_VAL_MEM:
#ifdef HOST_WORDS_BIGENDIAN
        endian_fixup = itype == TCG_TYPE_I32 ? 4 : 8;
        endian_fixup -= 1 << vece;
#else
        endian_fixup = 0;
#endif
        if (tcg_out_dupm_vec(s, vtype, vece, ots->reg, its->mem_base->reg,
                             its->mem_offset + endian_fixup)) {
            goto done;
        }
        tcg_out_ld(s, itype, ots->reg, its->mem_base->reg, its->mem_offset);
        break;

    default:
        g_assert_not_reached();
    }

    /* We now have a vector input register, so dup must succeed. */
    ok = tcg_out_dup_vec(s, vtype, vece, ots->reg, ots->reg);
    tcg_debug_assert(ok);

 done:
    if (IS_DEAD_ARG(1)) {
        temp_dead(s, its);
    }
    if (NEED_SYNC_ARG(0)) {
        temp_sync(s, ots, s->reserved_regs, 0, 0);
    }
    if (IS_DEAD_ARG(0)) {
        temp_dead(s, ots);
    }
}

static void tcg_reg_alloc_op(TCGContext *s, const TCGOp *op)
{
    const TCGLifeData arg_life = op->life;
    const TCGOpDef * const def = &tcg_op_defs[op->opc];
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
           op->args + nb_oargs + nb_iargs,
           sizeof(TCGArg) * def->nb_cargs);

    i_allocated_regs = s->reserved_regs;
    o_allocated_regs = s->reserved_regs;

    /* satisfy input constraints */ 
    for (k = 0; k < nb_iargs; k++) {
        TCGRegSet i_preferred_regs, o_preferred_regs;

        i = def->args_ct[nb_oargs + k].sort_index;
        arg = op->args[i];
        arg_ct = &def->args_ct[i];
        ts = arg_temp(arg);

        if (ts->val_type == TEMP_VAL_CONST
            && tcg_target_const_match(ts->val, ts->type, arg_ct)) {
            /* constant is OK for instruction */
            const_args[i] = 1;
            new_args[i] = ts->val;
            continue;
        }

        i_preferred_regs = o_preferred_regs = 0;
        if (arg_ct->ialias) {
            o_preferred_regs = op->output_pref[arg_ct->alias_index];

            /*
             * If the input is readonly, then it cannot also be an
             * output and aliased to itself.  If the input is not
             * dead after the instruction, we must allocate a new
             * register and move it.
             */
            if (temp_readonly(ts) || !IS_DEAD_ARG(i)) {
                goto allocate_in_reg;
            }

            /*
             * Check if the current register has already been allocated
             * for another input aliased to an output.
             */
            if (ts->val_type == TEMP_VAL_REG) {
                reg = ts->reg;
                for (int k2 = 0; k2 < k; k2++) {
                    int i2 = def->args_ct[nb_oargs + k2].sort_index;
                    if (def->args_ct[i2].ialias && reg == new_args[i2]) {
                        goto allocate_in_reg;
                    }
                }
            }
            i_preferred_regs = o_preferred_regs;
        }

        temp_load(s, ts, arg_ct->regs, i_allocated_regs, i_preferred_regs);
        reg = ts->reg;

        if (!tcg_regset_test_reg(arg_ct->regs, reg)) {
 allocate_in_reg:
            /*
             * Allocate a new register matching the constraint
             * and move the temporary register into it.
             */
            temp_load(s, ts, tcg_target_available_regs[ts->type],
                      i_allocated_regs, 0);
            reg = tcg_reg_alloc(s, arg_ct->regs, i_allocated_regs,
                                o_preferred_regs, ts->indirect_base);
            if (!tcg_out_mov(s, ts->type, reg, ts->reg)) {
                /*
                 * Cross register class move not supported.  Sync the
                 * temp back to its slot and load from there.
                 */
                temp_sync(s, ts, i_allocated_regs, 0, 0);
                tcg_out_ld(s, ts->type, reg,
                           ts->mem_base->reg, ts->mem_offset);
            }
        }
        new_args[i] = reg;
        const_args[i] = 0;
        tcg_regset_set_reg(i_allocated_regs, reg);
    }
    
    /* mark dead temporaries and free the associated registers */
    for (i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
        if (IS_DEAD_ARG(i)) {
            temp_dead(s, arg_temp(op->args[i]));
        }
    }

    if (def->flags & TCG_OPF_COND_BRANCH) {
        tcg_reg_alloc_cbranch(s, i_allocated_regs);
    } else if (def->flags & TCG_OPF_BB_END) {
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
            i = def->args_ct[k].sort_index;
            arg = op->args[i];
            arg_ct = &def->args_ct[i];
            ts = arg_temp(arg);

            /* ENV should not be modified.  */
            tcg_debug_assert(!temp_readonly(ts));

            if (arg_ct->oalias && !const_args[arg_ct->alias_index]) {
                reg = new_args[arg_ct->alias_index];
            } else if (arg_ct->newreg) {
                reg = tcg_reg_alloc(s, arg_ct->regs,
                                    i_allocated_regs | o_allocated_regs,
                                    op->output_pref[k], ts->indirect_base);
            } else {
                reg = tcg_reg_alloc(s, arg_ct->regs, o_allocated_regs,
                                    op->output_pref[k], ts->indirect_base);
            }
            tcg_regset_set_reg(o_allocated_regs, reg);
            if (ts->val_type == TEMP_VAL_REG) {
                s->reg_to_temp[ts->reg] = NULL;
            }
            ts->val_type = TEMP_VAL_REG;
            ts->reg = reg;
            /*
             * Temp value is modified, so the value kept in memory is
             * potentially not the same.
             */
            ts->mem_coherent = 0;
            s->reg_to_temp[reg] = ts;
            new_args[i] = reg;
        }
    }

    /* emit instruction */
    if (def->flags & TCG_OPF_VECTOR) {
        tcg_out_vec_op(s, op->opc, TCGOP_VECL(op), TCGOP_VECE(op),
                       new_args, const_args);
    } else {
        tcg_out_op(s, op->opc, new_args, const_args);
    }

    /* move the outputs in the correct register if needed */
    for(i = 0; i < nb_oargs; i++) {
        ts = arg_temp(op->args[i]);

        /* ENV should not be modified.  */
        tcg_debug_assert(!temp_readonly(ts));

        if (NEED_SYNC_ARG(i)) {
            temp_sync(s, ts, o_allocated_regs, 0, IS_DEAD_ARG(i));
        } else if (IS_DEAD_ARG(i)) {
            temp_dead(s, ts);
        }
    }
}

static bool tcg_reg_alloc_dup2(TCGContext *s, const TCGOp *op)
{
    const TCGLifeData arg_life = op->life;
    TCGTemp *ots, *itsl, *itsh;
    TCGType vtype = TCGOP_VECL(op) + TCG_TYPE_V64;

    /* This opcode is only valid for 32-bit hosts, for 64-bit elements. */
    tcg_debug_assert(TCG_TARGET_REG_BITS == 32);
    tcg_debug_assert(TCGOP_VECE(op) == MO_64);

    ots = arg_temp(op->args[0]);
    itsl = arg_temp(op->args[1]);
    itsh = arg_temp(op->args[2]);

    /* ENV should not be modified.  */
    tcg_debug_assert(!temp_readonly(ots));

    /* Allocate the output register now.  */
    if (ots->val_type != TEMP_VAL_REG) {
        TCGRegSet allocated_regs = s->reserved_regs;
        TCGRegSet dup_out_regs =
            tcg_op_defs[INDEX_op_dup_vec].args_ct[0].regs;

        /* Make sure to not spill the input registers. */
        if (!IS_DEAD_ARG(1) && itsl->val_type == TEMP_VAL_REG) {
            tcg_regset_set_reg(allocated_regs, itsl->reg);
        }
        if (!IS_DEAD_ARG(2) && itsh->val_type == TEMP_VAL_REG) {
            tcg_regset_set_reg(allocated_regs, itsh->reg);
        }

        ots->reg = tcg_reg_alloc(s, dup_out_regs, allocated_regs,
                                 op->output_pref[0], ots->indirect_base);
        ots->val_type = TEMP_VAL_REG;
        ots->mem_coherent = 0;
        s->reg_to_temp[ots->reg] = ots;
    }

    /* Promote dup2 of immediates to dupi_vec. */
    if (itsl->val_type == TEMP_VAL_CONST && itsh->val_type == TEMP_VAL_CONST) {
        uint64_t val = deposit64(itsl->val, 32, 32, itsh->val);
        MemOp vece = MO_64;

        if (val == dup_const(MO_8, val)) {
            vece = MO_8;
        } else if (val == dup_const(MO_16, val)) {
            vece = MO_16;
        } else if (val == dup_const(MO_32, val)) {
            vece = MO_32;
        }

        tcg_out_dupi_vec(s, vtype, vece, ots->reg, val);
        goto done;
    }

    /* If the two inputs form one 64-bit value, try dupm_vec. */
    if (itsl + 1 == itsh && itsl->base_type == TCG_TYPE_I64) {
        if (!itsl->mem_coherent) {
            temp_sync(s, itsl, s->reserved_regs, 0, 0);
        }
        if (!itsh->mem_coherent) {
            temp_sync(s, itsh, s->reserved_regs, 0, 0);
        }
#ifdef HOST_WORDS_BIGENDIAN
        TCGTemp *its = itsh;
#else
        TCGTemp *its = itsl;
#endif
        if (tcg_out_dupm_vec(s, vtype, MO_64, ots->reg,
                             its->mem_base->reg, its->mem_offset)) {
            goto done;
        }
    }

    /* Fall back to generic expansion. */
    return false;

 done:
    if (IS_DEAD_ARG(1)) {
        temp_dead(s, itsl);
    }
    if (IS_DEAD_ARG(2)) {
        temp_dead(s, itsh);
    }
    if (NEED_SYNC_ARG(0)) {
        temp_sync(s, ots, s->reserved_regs, 0, IS_DEAD_ARG(0));
    } else if (IS_DEAD_ARG(0)) {
        temp_dead(s, ots);
    }
    return true;
}

#ifdef TCG_TARGET_STACK_GROWSUP
#define STACK_DIR(x) (-(x))
#else
#define STACK_DIR(x) (x)
#endif

static void tcg_reg_alloc_call(TCGContext *s, TCGOp *op)
{
    const int nb_oargs = TCGOP_CALLO(op);
    const int nb_iargs = TCGOP_CALLI(op);
    const TCGLifeData arg_life = op->life;
    int flags, nb_regs, i;
    TCGReg reg;
    TCGArg arg;
    TCGTemp *ts;
    intptr_t stack_offset;
    size_t call_stack_size;
    tcg_insn_unit *func_addr;
    int allocate_args;
    TCGRegSet allocated_regs;

    func_addr = (tcg_insn_unit *)(intptr_t)op->args[nb_oargs + nb_iargs];
    flags = op->args[nb_oargs + nb_iargs + 1];

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
    for (i = nb_regs; i < nb_iargs; i++) {
        arg = op->args[nb_oargs + i];
#ifdef TCG_TARGET_STACK_GROWSUP
        stack_offset -= sizeof(tcg_target_long);
#endif
        if (arg != TCG_CALL_DUMMY_ARG) {
            ts = arg_temp(arg);
            temp_load(s, ts, tcg_target_available_regs[ts->type],
                      s->reserved_regs, 0);
            tcg_out_st(s, ts->type, ts->reg, TCG_REG_CALL_STACK, stack_offset);
        }
#ifndef TCG_TARGET_STACK_GROWSUP
        stack_offset += sizeof(tcg_target_long);
#endif
    }
    
    /* assign input registers */
    allocated_regs = s->reserved_regs;
    for (i = 0; i < nb_regs; i++) {
        arg = op->args[nb_oargs + i];
        if (arg != TCG_CALL_DUMMY_ARG) {
            ts = arg_temp(arg);
            reg = tcg_target_call_iarg_regs[i];

            if (ts->val_type == TEMP_VAL_REG) {
                if (ts->reg != reg) {
                    tcg_reg_free(s, reg, allocated_regs);
                    if (!tcg_out_mov(s, ts->type, reg, ts->reg)) {
                        /*
                         * Cross register class move not supported.  Sync the
                         * temp back to its slot and load from there.
                         */
                        temp_sync(s, ts, allocated_regs, 0, 0);
                        tcg_out_ld(s, ts->type, reg,
                                   ts->mem_base->reg, ts->mem_offset);
                    }
                }
            } else {
                TCGRegSet arg_set = 0;

                tcg_reg_free(s, reg, allocated_regs);
                tcg_regset_set_reg(arg_set, reg);
                temp_load(s, ts, arg_set, allocated_regs, 0);
            }

            tcg_regset_set_reg(allocated_regs, reg);
        }
    }
    
    /* mark dead temporaries and free the associated registers */
    for (i = nb_oargs; i < nb_iargs + nb_oargs; i++) {
        if (IS_DEAD_ARG(i)) {
            temp_dead(s, arg_temp(op->args[i]));
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
        arg = op->args[i];
        ts = arg_temp(arg);

        /* ENV should not be modified.  */
        tcg_debug_assert(!temp_readonly(ts));

        reg = tcg_target_call_oarg_regs[i];
        tcg_debug_assert(s->reg_to_temp[reg] == NULL);
        if (ts->val_type == TEMP_VAL_REG) {
            s->reg_to_temp[ts->reg] = NULL;
        }
        ts->val_type = TEMP_VAL_REG;
        ts->reg = reg;
        ts->mem_coherent = 0;
        s->reg_to_temp[reg] = ts;
        if (NEED_SYNC_ARG(i)) {
            temp_sync(s, ts, allocated_regs, 0, IS_DEAD_ARG(i));
        } else if (IS_DEAD_ARG(i)) {
            temp_dead(s, ts);
        }
    }
}

#ifdef CONFIG_PROFILER

/* avoid copy/paste errors */
#define PROF_ADD(to, from, field)                       \
    do {                                                \
        (to)->field += qatomic_read(&((from)->field));  \
    } while (0)

#define PROF_MAX(to, from, field)                                       \
    do {                                                                \
        typeof((from)->field) val__ = qatomic_read(&((from)->field));   \
        if (val__ > (to)->field) {                                      \
            (to)->field = val__;                                        \
        }                                                               \
    } while (0)

/* Pass in a zero'ed @prof */
static inline
void tcg_profile_snapshot(TCGProfile *prof, bool counters, bool table)
{
    unsigned int n_ctxs = qatomic_read(&n_tcg_ctxs);
    unsigned int i;

    for (i = 0; i < n_ctxs; i++) {
        TCGContext *s = qatomic_read(&tcg_ctxs[i]);
        const TCGProfile *orig = &s->prof;

        if (counters) {
            PROF_ADD(prof, orig, cpu_exec_time);
            PROF_ADD(prof, orig, tb_count1);
            PROF_ADD(prof, orig, tb_count);
            PROF_ADD(prof, orig, op_count);
            PROF_MAX(prof, orig, op_count_max);
            PROF_ADD(prof, orig, temp_count);
            PROF_MAX(prof, orig, temp_count_max);
            PROF_ADD(prof, orig, del_op_count);
            PROF_ADD(prof, orig, code_in_len);
            PROF_ADD(prof, orig, code_out_len);
            PROF_ADD(prof, orig, search_out_len);
            PROF_ADD(prof, orig, interm_time);
            PROF_ADD(prof, orig, code_time);
            PROF_ADD(prof, orig, la_time);
            PROF_ADD(prof, orig, opt_time);
            PROF_ADD(prof, orig, restore_count);
            PROF_ADD(prof, orig, restore_time);
        }
        if (table) {
            int i;

            for (i = 0; i < NB_OPS; i++) {
                PROF_ADD(prof, orig, table_op_count[i]);
            }
        }
    }
}

#undef PROF_ADD
#undef PROF_MAX

static void tcg_profile_snapshot_counters(TCGProfile *prof)
{
    tcg_profile_snapshot(prof, true, false);
}

static void tcg_profile_snapshot_table(TCGProfile *prof)
{
    tcg_profile_snapshot(prof, false, true);
}

void tcg_dump_op_count(void)
{
    TCGProfile prof = {};
    int i;

    tcg_profile_snapshot_table(&prof);
    for (i = 0; i < NB_OPS; i++) {
        qemu_printf("%s %" PRId64 "\n", tcg_op_defs[i].name,
                    prof.table_op_count[i]);
    }
}

int64_t tcg_cpu_exec_time(void)
{
    unsigned int n_ctxs = qatomic_read(&n_tcg_ctxs);
    unsigned int i;
    int64_t ret = 0;

    for (i = 0; i < n_ctxs; i++) {
        const TCGContext *s = qatomic_read(&tcg_ctxs[i]);
        const TCGProfile *prof = &s->prof;

        ret += qatomic_read(&prof->cpu_exec_time);
    }
    return ret;
}
#else
void tcg_dump_op_count(void)
{
    qemu_printf("[TCG profiler not compiled]\n");
}

int64_t tcg_cpu_exec_time(void)
{
    error_report("%s: TCG profiler not compiled", __func__);
    exit(EXIT_FAILURE);
}
#endif


int tcg_gen_code(TCGContext *s, TranslationBlock *tb)
{
#ifdef CONFIG_PROFILER
    TCGProfile *prof = &s->prof;
#endif
    int i, num_insns;
    TCGOp *op;

#ifdef CONFIG_PROFILER
    {
        int n = 0;

        QTAILQ_FOREACH(op, &s->ops, link) {
            n++;
        }
        qatomic_set(&prof->op_count, prof->op_count + n);
        if (n > prof->op_count_max) {
            qatomic_set(&prof->op_count_max, n);
        }

        n = s->nb_temps;
        qatomic_set(&prof->temp_count, prof->temp_count + n);
        if (n > prof->temp_count_max) {
            qatomic_set(&prof->temp_count_max, n);
        }
    }
#endif

#ifdef DEBUG_DISAS
    if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP)
                 && qemu_log_in_addr_range(tb->pc))) {
        FILE *logfile = qemu_log_lock();
        qemu_log("OP:\n");
        tcg_dump_ops(s, false);
        qemu_log("\n");
        qemu_log_unlock(logfile);
    }
#endif

#ifdef CONFIG_DEBUG_TCG
    /* Ensure all labels referenced have been emitted.  */
    {
        TCGLabel *l;
        bool error = false;

        QSIMPLEQ_FOREACH(l, &s->labels, next) {
            if (unlikely(!l->present) && l->refs) {
                qemu_log_mask(CPU_LOG_TB_OP,
                              "$L%d referenced but not present.\n", l->id);
                error = true;
            }
        }
        assert(!error);
    }
#endif

#ifdef CONFIG_PROFILER
    qatomic_set(&prof->opt_time, prof->opt_time - profile_getclock());
#endif

#ifdef USE_TCG_OPTIMIZATIONS
    tcg_optimize(s);
#endif

#ifdef CONFIG_PROFILER
    qatomic_set(&prof->opt_time, prof->opt_time + profile_getclock());
    qatomic_set(&prof->la_time, prof->la_time - profile_getclock());
#endif

    reachable_code_pass(s);
    liveness_pass_1(s);

    if (s->nb_indirects > 0) {
#ifdef DEBUG_DISAS
        if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP_IND)
                     && qemu_log_in_addr_range(tb->pc))) {
            FILE *logfile = qemu_log_lock();
            qemu_log("OP before indirect lowering:\n");
            tcg_dump_ops(s, false);
            qemu_log("\n");
            qemu_log_unlock(logfile);
        }
#endif
        /* Replace indirect temps with direct temps.  */
        if (liveness_pass_2(s)) {
            /* If changes were made, re-run liveness.  */
            liveness_pass_1(s);
        }
    }

#ifdef CONFIG_PROFILER
    qatomic_set(&prof->la_time, prof->la_time + profile_getclock());
#endif

#ifdef DEBUG_DISAS
    if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP_OPT)
                 && qemu_log_in_addr_range(tb->pc))) {
        FILE *logfile = qemu_log_lock();
        qemu_log("OP after optimization and liveness analysis:\n");
        tcg_dump_ops(s, true);
        qemu_log("\n");
        qemu_log_unlock(logfile);
    }
#endif

    tcg_reg_alloc_start(s);

    /*
     * Reset the buffer pointers when restarting after overflow.
     * TODO: Move this into translate-all.c with the rest of the
     * buffer management.  Having only this done here is confusing.
     */
    s->code_buf = tcg_splitwx_to_rw(tb->tc.ptr);
    s->code_ptr = s->code_buf;

#ifdef TCG_TARGET_NEED_LDST_LABELS
    QSIMPLEQ_INIT(&s->ldst_labels);
#endif
#ifdef TCG_TARGET_NEED_POOL_LABELS
    s->pool_labels = NULL;
#endif

    num_insns = -1;
    QTAILQ_FOREACH(op, &s->ops, link) {
        TCGOpcode opc = op->opc;

#ifdef CONFIG_PROFILER
        qatomic_set(&prof->table_op_count[opc], prof->table_op_count[opc] + 1);
#endif

        switch (opc) {
        case INDEX_op_mov_i32:
        case INDEX_op_mov_i64:
        case INDEX_op_mov_vec:
            tcg_reg_alloc_mov(s, op);
            break;
        case INDEX_op_dup_vec:
            tcg_reg_alloc_dup(s, op);
            break;
        case INDEX_op_insn_start:
            if (num_insns >= 0) {
                size_t off = tcg_current_code_size(s);
                s->gen_insn_end_off[num_insns] = off;
                /* Assert that we do not overflow our stored offset.  */
                assert(s->gen_insn_end_off[num_insns] == off);
            }
            num_insns++;
            for (i = 0; i < TARGET_INSN_START_WORDS; ++i) {
                target_ulong a;
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
                a = deposit64(op->args[i * 2], 32, 32, op->args[i * 2 + 1]);
#else
                a = op->args[i];
#endif
                s->gen_insn_data[num_insns][i] = a;
            }
            break;
        case INDEX_op_discard:
            temp_dead(s, arg_temp(op->args[0]));
            break;
        case INDEX_op_set_label:
            tcg_reg_alloc_bb_end(s, s->reserved_regs);
            tcg_out_label(s, arg_label(op->args[0]));
            break;
        case INDEX_op_call:
            tcg_reg_alloc_call(s, op);
            break;
        case INDEX_op_dup2_vec:
            if (tcg_reg_alloc_dup2(s, op)) {
                break;
            }
            /* fall through */
        default:
            /* Sanity check that we've not introduced any unhandled opcodes. */
            tcg_debug_assert(tcg_op_supported(opc));
            /* Note: in order to speed up the code, it would be much
               faster to have specialized register allocator functions for
               some common argument patterns */
            tcg_reg_alloc_op(s, op);
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
        /* Test for TB overflow, as seen by gen_insn_end_off.  */
        if (unlikely(tcg_current_code_size(s) > UINT16_MAX)) {
            return -2;
        }
    }
    tcg_debug_assert(num_insns >= 0);
    s->gen_insn_end_off[num_insns] = tcg_current_code_size(s);

    /* Generate TB finalization at the end of block */
#ifdef TCG_TARGET_NEED_LDST_LABELS
    i = tcg_out_ldst_finalize(s);
    if (i < 0) {
        return i;
    }
#endif
#ifdef TCG_TARGET_NEED_POOL_LABELS
    i = tcg_out_pool_finalize(s);
    if (i < 0) {
        return i;
    }
#endif
    if (!tcg_resolve_relocs(s)) {
        return -2;
    }

#ifndef CONFIG_TCG_INTERPRETER
    /* flush instruction cache */
    flush_idcache_range((uintptr_t)tcg_splitwx_to_rx(s->code_buf),
                        (uintptr_t)s->code_buf,
                        tcg_ptr_byte_diff(s->code_ptr, s->code_buf));
#endif

    return tcg_current_code_size(s);
}

#ifdef CONFIG_PROFILER
void tcg_dump_info(void)
{
    TCGProfile prof = {};
    const TCGProfile *s;
    int64_t tb_count;
    int64_t tb_div_count;
    int64_t tot;

    tcg_profile_snapshot_counters(&prof);
    s = &prof;
    tb_count = s->tb_count;
    tb_div_count = tb_count ? tb_count : 1;
    tot = s->interm_time + s->code_time;

    qemu_printf("JIT cycles          %" PRId64 " (%0.3f s at 2.4 GHz)\n",
                tot, tot / 2.4e9);
    qemu_printf("translated TBs      %" PRId64 " (aborted=%" PRId64
                " %0.1f%%)\n",
                tb_count, s->tb_count1 - tb_count,
                (double)(s->tb_count1 - s->tb_count)
                / (s->tb_count1 ? s->tb_count1 : 1) * 100.0);
    qemu_printf("avg ops/TB          %0.1f max=%d\n",
                (double)s->op_count / tb_div_count, s->op_count_max);
    qemu_printf("deleted ops/TB      %0.2f\n",
                (double)s->del_op_count / tb_div_count);
    qemu_printf("avg temps/TB        %0.2f max=%d\n",
                (double)s->temp_count / tb_div_count, s->temp_count_max);
    qemu_printf("avg host code/TB    %0.1f\n",
                (double)s->code_out_len / tb_div_count);
    qemu_printf("avg search data/TB  %0.1f\n",
                (double)s->search_out_len / tb_div_count);
    
    qemu_printf("cycles/op           %0.1f\n",
                s->op_count ? (double)tot / s->op_count : 0);
    qemu_printf("cycles/in byte      %0.1f\n",
                s->code_in_len ? (double)tot / s->code_in_len : 0);
    qemu_printf("cycles/out byte     %0.1f\n",
                s->code_out_len ? (double)tot / s->code_out_len : 0);
    qemu_printf("cycles/search byte     %0.1f\n",
                s->search_out_len ? (double)tot / s->search_out_len : 0);
    if (tot == 0) {
        tot = 1;
    }
    qemu_printf("  gen_interm time   %0.1f%%\n",
                (double)s->interm_time / tot * 100.0);
    qemu_printf("  gen_code time     %0.1f%%\n",
                (double)s->code_time / tot * 100.0);
    qemu_printf("optim./code time    %0.1f%%\n",
                (double)s->opt_time / (s->code_time ? s->code_time : 1)
                * 100.0);
    qemu_printf("liveness/code time  %0.1f%%\n",
                (double)s->la_time / (s->code_time ? s->code_time : 1) * 100.0);
    qemu_printf("cpu_restore count   %" PRId64 "\n",
                s->restore_count);
    qemu_printf("  avg cycles        %0.1f\n",
                s->restore_count ? (double)s->restore_time / s->restore_count : 0);
}
#else
void tcg_dump_info(void)
{
    qemu_printf("[TCG profiler not compiled]\n");
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

static void tcg_register_jit_int(const void *buf_ptr, size_t buf_size,
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

static void tcg_register_jit_int(const void *buf, size_t size,
                                 const void *debug_frame,
                                 size_t debug_frame_size)
{
}

void tcg_register_jit(const void *buf, size_t buf_size)
{
}
#endif /* ELF_HOST_MACHINE */

#if !TCG_TARGET_MAYBE_vec
void tcg_expand_vec_op(TCGOpcode o, TCGType t, unsigned e, TCGArg a0, ...)
{
    g_assert_not_reached();
}
#endif
