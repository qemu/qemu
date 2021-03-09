/*
 * Memory region management for Tiny Code Generator for QEMU
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

#include "qemu/osdep.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/boards.h"
#endif
#include "tcg-internal.h"


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

static struct tcg_region_tree *tc_ptr_to_region_tree(const void *p)
{
    size_t region_idx;

    /*
     * Like tcg_splitwx_to_rw, with no assert.  The pc may come from
     * a signal handler over which the caller has no control.
     */
    if (!in_code_gen_buffer(p)) {
        p -= tcg_splitwx_diff;
        if (!in_code_gen_buffer(p)) {
            return NULL;
        }
    }

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

    g_assert(rt != NULL);
    qemu_mutex_lock(&rt->lock);
    g_tree_insert(rt->tree, &tb->tc, tb);
    qemu_mutex_unlock(&rt->lock);
}

void tcg_tb_remove(TranslationBlock *tb)
{
    struct tcg_region_tree *rt = tc_ptr_to_region_tree(tb->tc.ptr);

    g_assert(rt != NULL);
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

    if (rt == NULL) {
        return NULL;
    }

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
bool tcg_region_alloc(TCGContext *s)
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
static void tcg_region_initial_alloc__locked(TCGContext *s)
{
    bool err = tcg_region_alloc__locked(s);
    g_assert(!err);
}

void tcg_region_initial_alloc(TCGContext *s)
{
    qemu_mutex_lock(&region.lock);
    tcg_region_initial_alloc__locked(s);
    qemu_mutex_unlock(&region.lock);
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
        tcg_region_initial_alloc__locked(s);
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

    /*
     * Set guard pages in the rw buffer, as that's the one into which
     * buffer overruns could occur.  Do not set guard pages in the rx
     * buffer -- let that one use hugepages throughout.
     */
    for (i = 0; i < region.n; i++) {
        void *start, *end;

        tcg_region_bounds(i, &start, &end);

        /*
         * macOS 11.2 has a bug (Apple Feedback FB8994773) in which mprotect
         * rejects a permission change from RWX -> NONE.  Guard pages are
         * nice for bug detection but are not essential; ignore any failure.
         */
        (void)qemu_mprotect_none(end, page_size);
    }

    tcg_region_trees_init();

    /*
     * Leave the initial context initialized to the first region.
     * This will be the context into which we generate the prologue.
     * It is also the only context for CONFIG_USER_ONLY.
     */
    tcg_region_initial_alloc__locked(&tcg_init_ctx);
}

void tcg_region_prologue_set(TCGContext *s)
{
    /* Deduct the prologue from the first region.  */
    g_assert(region.start == s->code_gen_buffer);
    region.start = s->code_ptr;

    /* Recompute boundaries of the first region. */
    tcg_region_assign(s, 0);

    /* Register the balance of the buffer with gdb. */
    tcg_register_jit(tcg_splitwx_to_rx(region.start),
                     region.end - region.start);
}

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
