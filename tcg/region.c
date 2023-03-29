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
#include "qemu/units.h"
#include "qemu/madvise.h"
#include "qemu/mprotect.h"
#include "qemu/memalign.h"
#include "qemu/cacheinfo.h"
#include "qemu/qtree.h"
#include "qapi/error.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#include "tcg-internal.h"


struct tcg_region_tree {
    QemuMutex lock;
    QTree *tree;
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
    void *start_aligned;
    void *after_prologue;
    size_t n;
    size_t size; /* size of one region */
    size_t stride; /* .size + guard size */
    size_t total_size; /* size of entire buffer, >= n * stride */

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

bool in_code_gen_buffer(const void *p)
{
    /*
     * Much like it is valid to have a pointer to the byte past the
     * end of an array (so long as you don't dereference it), allow
     * a pointer to the byte past the end of the code gen buffer.
     */
    return (size_t)(p - region.start_aligned) <= region.total_size;
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

static gint tb_tc_cmp(gconstpointer ap, gconstpointer bp, gpointer userdata)
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

static void tb_destroy(gpointer value)
{
    TranslationBlock *tb = value;
    qemu_spin_destroy(&tb->jmp_lock);
}

static void tcg_region_trees_init(void)
{
    size_t i;

    tree_size = ROUND_UP(sizeof(struct tcg_region_tree), qemu_dcache_linesize);
    region_trees = qemu_memalign(qemu_dcache_linesize, region.n * tree_size);
    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        qemu_mutex_init(&rt->lock);
        rt->tree = q_tree_new_full(tb_tc_cmp, NULL, NULL, tb_destroy);
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
    q_tree_insert(rt->tree, &tb->tc, tb);
    qemu_mutex_unlock(&rt->lock);
}

void tcg_tb_remove(TranslationBlock *tb)
{
    struct tcg_region_tree *rt = tc_ptr_to_region_tree(tb->tc.ptr);

    g_assert(rt != NULL);
    qemu_mutex_lock(&rt->lock);
    q_tree_remove(rt->tree, &tb->tc);
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
    tb = q_tree_lookup(rt->tree, &s);
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

        q_tree_foreach(rt->tree, func, user_data);
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

        nb_tbs += q_tree_nnodes(rt->tree);
    }
    tcg_region_tree_unlock_all();
    return nb_tbs;
}

static void tcg_region_tree_reset_all(void)
{
    size_t i;

    tcg_region_tree_lock_all();
    for (i = 0; i < region.n; i++) {
        struct tcg_region_tree *rt = region_trees + i * tree_size;

        /* Increment the refcount first so that destroy acts as a reset */
        q_tree_ref(rt->tree);
        q_tree_destroy(rt->tree);
    }
    tcg_region_tree_unlock_all();
}

static void tcg_region_bounds(size_t curr_region, void **pstart, void **pend)
{
    void *start, *end;

    start = region.start_aligned + curr_region * region.stride;
    end = start + region.size;

    if (curr_region == 0) {
        start = region.after_prologue;
    }
    /* The final region may have a few extra pages due to earlier rounding. */
    if (curr_region == region.n - 1) {
        end = region.start_aligned + region.total_size;
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
    unsigned int n_ctxs = qatomic_read(&tcg_cur_ctxs);
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

static size_t tcg_n_regions(size_t tb_size, unsigned max_cpus)
{
#ifdef CONFIG_USER_ONLY
    return 1;
#else
    size_t n_regions;

    /*
     * It is likely that some vCPUs will translate more code than others,
     * so we first try to set more regions than max_cpus, with those regions
     * being of reasonable size. If that's not possible we make do by evenly
     * dividing the code_gen_buffer among the vCPUs.
     */
    /* Use a single region if all we have is one vCPU thread */
    if (max_cpus == 1 || !qemu_tcg_mttcg_enabled()) {
        return 1;
    }

    /*
     * Try to have more regions than max_cpus, with each region being >= 2 MB.
     * If we can't, then just allocate one region per vCPU thread.
     */
    n_regions = tb_size / (2 * MiB);
    if (n_regions <= max_cpus) {
        return max_cpus;
    }
    return MIN(n_regions, max_cpus * 8);
#endif
}

/*
 * Minimum size of the code gen buffer.  This number is randomly chosen,
 * but not so small that we can't have a fair number of TB's live.
 *
 * Maximum size, MAX_CODE_GEN_BUFFER_SIZE, is defined in tcg-target.h.
 * Unless otherwise indicated, this is constrained by the range of
 * direct branches on the host cpu, as used by the TCG implementation
 * of goto_tb.
 */
#define MIN_CODE_GEN_BUFFER_SIZE     (1 * MiB)

#if TCG_TARGET_REG_BITS == 32
#define DEFAULT_CODE_GEN_BUFFER_SIZE_1 (32 * MiB)
#ifdef CONFIG_USER_ONLY
/*
 * For user mode on smaller 32 bit systems we may run into trouble
 * allocating big chunks of data in the right place. On these systems
 * we utilise a static code generation buffer directly in the binary.
 */
#define USE_STATIC_CODE_GEN_BUFFER
#endif
#else /* TCG_TARGET_REG_BITS == 64 */
#ifdef CONFIG_USER_ONLY
/*
 * As user-mode emulation typically means running multiple instances
 * of the translator don't go too nuts with our default code gen
 * buffer lest we make things too hard for the OS.
 */
#define DEFAULT_CODE_GEN_BUFFER_SIZE_1 (128 * MiB)
#else
/*
 * We expect most system emulation to run one or two guests per host.
 * Users running large scale system emulation may want to tweak their
 * runtime setup via the tb-size control on the command line.
 */
#define DEFAULT_CODE_GEN_BUFFER_SIZE_1 (1 * GiB)
#endif
#endif

#define DEFAULT_CODE_GEN_BUFFER_SIZE \
  (DEFAULT_CODE_GEN_BUFFER_SIZE_1 < MAX_CODE_GEN_BUFFER_SIZE \
   ? DEFAULT_CODE_GEN_BUFFER_SIZE_1 : MAX_CODE_GEN_BUFFER_SIZE)

#ifdef USE_STATIC_CODE_GEN_BUFFER
static uint8_t static_code_gen_buffer[DEFAULT_CODE_GEN_BUFFER_SIZE]
    __attribute__((aligned(CODE_GEN_ALIGN)));

static int alloc_code_gen_buffer(size_t tb_size, int splitwx, Error **errp)
{
    void *buf, *end;
    size_t size;

    if (splitwx > 0) {
        error_setg(errp, "jit split-wx not supported");
        return -1;
    }

    /* page-align the beginning and end of the buffer */
    buf = static_code_gen_buffer;
    end = static_code_gen_buffer + sizeof(static_code_gen_buffer);
    buf = QEMU_ALIGN_PTR_UP(buf, qemu_real_host_page_size());
    end = QEMU_ALIGN_PTR_DOWN(end, qemu_real_host_page_size());

    size = end - buf;

    /* Honor a command-line option limiting the size of the buffer.  */
    if (size > tb_size) {
        size = QEMU_ALIGN_DOWN(tb_size, qemu_real_host_page_size());
    }

    region.start_aligned = buf;
    region.total_size = size;

    return PROT_READ | PROT_WRITE;
}
#elif defined(_WIN32)
static int alloc_code_gen_buffer(size_t size, int splitwx, Error **errp)
{
    void *buf;

    if (splitwx > 0) {
        error_setg(errp, "jit split-wx not supported");
        return -1;
    }

    buf = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT,
                             PAGE_EXECUTE_READWRITE);
    if (buf == NULL) {
        error_setg_win32(errp, GetLastError(),
                         "allocate %zu bytes for jit buffer", size);
        return false;
    }

    region.start_aligned = buf;
    region.total_size = size;

    return PAGE_READ | PAGE_WRITE | PAGE_EXEC;
}
#else
static int alloc_code_gen_buffer_anon(size_t size, int prot,
                                      int flags, Error **errp)
{
    void *buf;

    buf = mmap(NULL, size, prot, flags, -1, 0);
    if (buf == MAP_FAILED) {
        error_setg_errno(errp, errno,
                         "allocate %zu bytes for jit buffer", size);
        return -1;
    }

    region.start_aligned = buf;
    region.total_size = size;
    return prot;
}

#ifndef CONFIG_TCG_INTERPRETER
#ifdef CONFIG_POSIX
#include "qemu/memfd.h"

static int alloc_code_gen_buffer_splitwx_memfd(size_t size, Error **errp)
{
    void *buf_rw = NULL, *buf_rx = MAP_FAILED;
    int fd = -1;

    buf_rw = qemu_memfd_alloc("tcg-jit", size, 0, &fd, errp);
    if (buf_rw == NULL) {
        goto fail;
    }

    buf_rx = mmap(NULL, size, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
    if (buf_rx == MAP_FAILED) {
        goto fail_rx;
    }

    close(fd);
    region.start_aligned = buf_rw;
    region.total_size = size;
    tcg_splitwx_diff = buf_rx - buf_rw;

    return PROT_READ | PROT_WRITE;

 fail_rx:
    error_setg_errno(errp, errno, "failed to map shared memory for execute");
 fail:
    if (buf_rx != MAP_FAILED) {
        munmap(buf_rx, size);
    }
    if (buf_rw) {
        munmap(buf_rw, size);
    }
    if (fd >= 0) {
        close(fd);
    }
    return -1;
}
#endif /* CONFIG_POSIX */

#ifdef CONFIG_DARWIN
#include <mach/mach.h>

extern kern_return_t mach_vm_remap(vm_map_t target_task,
                                   mach_vm_address_t *target_address,
                                   mach_vm_size_t size,
                                   mach_vm_offset_t mask,
                                   int flags,
                                   vm_map_t src_task,
                                   mach_vm_address_t src_address,
                                   boolean_t copy,
                                   vm_prot_t *cur_protection,
                                   vm_prot_t *max_protection,
                                   vm_inherit_t inheritance);

static int alloc_code_gen_buffer_splitwx_vmremap(size_t size, Error **errp)
{
    kern_return_t ret;
    mach_vm_address_t buf_rw, buf_rx;
    vm_prot_t cur_prot, max_prot;

    /* Map the read-write portion via normal anon memory. */
    if (!alloc_code_gen_buffer_anon(size, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, errp)) {
        return -1;
    }

    buf_rw = (mach_vm_address_t)region.start_aligned;
    buf_rx = 0;
    ret = mach_vm_remap(mach_task_self(),
                        &buf_rx,
                        size,
                        0,
                        VM_FLAGS_ANYWHERE,
                        mach_task_self(),
                        buf_rw,
                        false,
                        &cur_prot,
                        &max_prot,
                        VM_INHERIT_NONE);
    if (ret != KERN_SUCCESS) {
        /* TODO: Convert "ret" to a human readable error message. */
        error_setg(errp, "vm_remap for jit splitwx failed");
        munmap((void *)buf_rw, size);
        return -1;
    }

    if (mprotect((void *)buf_rx, size, PROT_READ | PROT_EXEC) != 0) {
        error_setg_errno(errp, errno, "mprotect for jit splitwx");
        munmap((void *)buf_rx, size);
        munmap((void *)buf_rw, size);
        return -1;
    }

    tcg_splitwx_diff = buf_rx - buf_rw;
    return PROT_READ | PROT_WRITE;
}
#endif /* CONFIG_DARWIN */
#endif /* CONFIG_TCG_INTERPRETER */

static int alloc_code_gen_buffer_splitwx(size_t size, Error **errp)
{
#ifndef CONFIG_TCG_INTERPRETER
# ifdef CONFIG_DARWIN
    return alloc_code_gen_buffer_splitwx_vmremap(size, errp);
# endif
# ifdef CONFIG_POSIX
    return alloc_code_gen_buffer_splitwx_memfd(size, errp);
# endif
#endif
    error_setg(errp, "jit split-wx not supported");
    return -1;
}

static int alloc_code_gen_buffer(size_t size, int splitwx, Error **errp)
{
    ERRP_GUARD();
    int prot, flags;

    if (splitwx) {
        prot = alloc_code_gen_buffer_splitwx(size, errp);
        if (prot >= 0) {
            return prot;
        }
        /*
         * If splitwx force-on (1), fail;
         * if splitwx default-on (-1), fall through to splitwx off.
         */
        if (splitwx > 0) {
            return -1;
        }
        error_free_or_abort(errp);
    }

    /*
     * macOS 11.2 has a bug (Apple Feedback FB8994773) in which mprotect
     * rejects a permission change from RWX -> NONE when reserving the
     * guard pages later.  We can go the other way with the same number
     * of syscalls, so always begin with PROT_NONE.
     */
    prot = PROT_NONE;
    flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef CONFIG_DARWIN
    /* Applicable to both iOS and macOS (Apple Silicon). */
    if (!splitwx) {
        flags |= MAP_JIT;
    }
#endif

    return alloc_code_gen_buffer_anon(size, prot, flags, errp);
}
#endif /* USE_STATIC_CODE_GEN_BUFFER, WIN32, POSIX */

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
void tcg_region_init(size_t tb_size, int splitwx, unsigned max_cpus)
{
    const size_t page_size = qemu_real_host_page_size();
    size_t region_size;
    int have_prot, need_prot;

    /* Size the buffer.  */
    if (tb_size == 0) {
        size_t phys_mem = qemu_get_host_physmem();
        if (phys_mem == 0) {
            tb_size = DEFAULT_CODE_GEN_BUFFER_SIZE;
        } else {
            tb_size = QEMU_ALIGN_DOWN(phys_mem / 8, page_size);
            tb_size = MIN(DEFAULT_CODE_GEN_BUFFER_SIZE, tb_size);
        }
    }
    if (tb_size < MIN_CODE_GEN_BUFFER_SIZE) {
        tb_size = MIN_CODE_GEN_BUFFER_SIZE;
    }
    if (tb_size > MAX_CODE_GEN_BUFFER_SIZE) {
        tb_size = MAX_CODE_GEN_BUFFER_SIZE;
    }

    have_prot = alloc_code_gen_buffer(tb_size, splitwx, &error_fatal);
    assert(have_prot >= 0);

    /* Request large pages for the buffer and the splitwx.  */
    qemu_madvise(region.start_aligned, region.total_size, QEMU_MADV_HUGEPAGE);
    if (tcg_splitwx_diff) {
        qemu_madvise(region.start_aligned + tcg_splitwx_diff,
                     region.total_size, QEMU_MADV_HUGEPAGE);
    }

    /*
     * Make region_size a multiple of page_size, using aligned as the start.
     * As a result of this we might end up with a few extra pages at the end of
     * the buffer; we will assign those to the last region.
     */
    region.n = tcg_n_regions(tb_size, max_cpus);
    region_size = tb_size / region.n;
    region_size = QEMU_ALIGN_DOWN(region_size, page_size);

    /* A region must have at least 2 pages; one code, one guard */
    g_assert(region_size >= 2 * page_size);
    region.stride = region_size;

    /* Reserve space for guard pages. */
    region.size = region_size - page_size;
    region.total_size -= page_size;

    /*
     * The first region will be smaller than the others, via the prologue,
     * which has yet to be allocated.  For now, the first region begins at
     * the page boundary.
     */
    region.after_prologue = region.start_aligned;

    /* init the region struct */
    qemu_mutex_init(&region.lock);

    /*
     * Set guard pages in the rw buffer, as that's the one into which
     * buffer overruns could occur.  Do not set guard pages in the rx
     * buffer -- let that one use hugepages throughout.
     * Work with the page protections set up with the initial mapping.
     */
    need_prot = PAGE_READ | PAGE_WRITE;
#ifndef CONFIG_TCG_INTERPRETER
    if (tcg_splitwx_diff == 0) {
        need_prot |= PAGE_EXEC;
    }
#endif
    for (size_t i = 0, n = region.n; i < n; i++) {
        void *start, *end;

        tcg_region_bounds(i, &start, &end);
        if (have_prot != need_prot) {
            int rc;

            if (need_prot == (PAGE_READ | PAGE_WRITE | PAGE_EXEC)) {
                rc = qemu_mprotect_rwx(start, end - start);
            } else if (need_prot == (PAGE_READ | PAGE_WRITE)) {
                rc = qemu_mprotect_rw(start, end - start);
            } else {
                g_assert_not_reached();
            }
            if (rc) {
                error_setg_errno(&error_fatal, errno,
                                 "mprotect of jit buffer");
            }
        }
        if (have_prot != 0) {
            /* Guard pages are nice for bug detection but are not essential. */
            (void)qemu_mprotect_none(end, page_size);
        }
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
    g_assert(region.start_aligned == s->code_gen_buffer);
    region.after_prologue = s->code_ptr;

    /* Recompute boundaries of the first region. */
    tcg_region_assign(s, 0);

    /* Register the balance of the buffer with gdb. */
    tcg_register_jit(tcg_splitwx_to_rx(region.after_prologue),
                     region.start_aligned + region.total_size -
                     region.after_prologue);
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
    unsigned int n_ctxs = qatomic_read(&tcg_cur_ctxs);
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
    capacity = region.total_size;
    capacity -= (region.n - 1) * guard_size;
    capacity -= region.n * TCG_HIGHWATER;

    return capacity;
}
