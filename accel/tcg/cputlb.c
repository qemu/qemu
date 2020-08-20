/*
 *  Common CPU TLB handling
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "exec/cpu_ldst.h"
#include "exec/cputlb.h"
#include "exec/memory-internal.h"
#include "exec/ram_addr.h"
#include "tcg/tcg.h"
#include "qemu/error-report.h"
#include "exec/log.h"
#include "exec/helper-proto.h"
#include "qemu/atomic.h"
#include "qemu/atomic128.h"
#include "translate-all.h"
#include "trace-root.h"
#include "trace/mem.h"
#ifdef CONFIG_PLUGIN
#include "qemu/plugin-memory.h"
#endif

/* DEBUG defines, enable DEBUG_TLB_LOG to log to the CPU_LOG_MMU target */
/* #define DEBUG_TLB */
/* #define DEBUG_TLB_LOG */

#ifdef DEBUG_TLB
# define DEBUG_TLB_GATE 1
# ifdef DEBUG_TLB_LOG
#  define DEBUG_TLB_LOG_GATE 1
# else
#  define DEBUG_TLB_LOG_GATE 0
# endif
#else
# define DEBUG_TLB_GATE 0
# define DEBUG_TLB_LOG_GATE 0
#endif

#define tlb_debug(fmt, ...) do { \
    if (DEBUG_TLB_LOG_GATE) { \
        qemu_log_mask(CPU_LOG_MMU, "%s: " fmt, __func__, \
                      ## __VA_ARGS__); \
    } else if (DEBUG_TLB_GATE) { \
        fprintf(stderr, "%s: " fmt, __func__, ## __VA_ARGS__); \
    } \
} while (0)

#define assert_cpu_is_self(cpu) do {                              \
        if (DEBUG_TLB_GATE) {                                     \
            g_assert(!(cpu)->created || qemu_cpu_is_self(cpu));   \
        }                                                         \
    } while (0)

/* run_on_cpu_data.target_ptr should always be big enough for a
 * target_ulong even on 32 bit builds */
QEMU_BUILD_BUG_ON(sizeof(target_ulong) > sizeof(run_on_cpu_data));

/* We currently can't handle more than 16 bits in the MMUIDX bitmask.
 */
QEMU_BUILD_BUG_ON(NB_MMU_MODES > 16);
#define ALL_MMUIDX_BITS ((1 << NB_MMU_MODES) - 1)

static inline size_t tlb_n_entries(CPUTLBDescFast *fast)
{
    return (fast->mask >> CPU_TLB_ENTRY_BITS) + 1;
}

static inline size_t sizeof_tlb(CPUTLBDescFast *fast)
{
    return fast->mask + (1 << CPU_TLB_ENTRY_BITS);
}

static void tlb_window_reset(CPUTLBDesc *desc, int64_t ns,
                             size_t max_entries)
{
    desc->window_begin_ns = ns;
    desc->window_max_entries = max_entries;
}

/**
 * tlb_mmu_resize_locked() - perform TLB resize bookkeeping; resize if necessary
 * @desc: The CPUTLBDesc portion of the TLB
 * @fast: The CPUTLBDescFast portion of the same TLB
 *
 * Called with tlb_lock_held.
 *
 * We have two main constraints when resizing a TLB: (1) we only resize it
 * on a TLB flush (otherwise we'd have to take a perf hit by either rehashing
 * the array or unnecessarily flushing it), which means we do not control how
 * frequently the resizing can occur; (2) we don't have access to the guest's
 * future scheduling decisions, and therefore have to decide the magnitude of
 * the resize based on past observations.
 *
 * In general, a memory-hungry process can benefit greatly from an appropriately
 * sized TLB, since a guest TLB miss is very expensive. This doesn't mean that
 * we just have to make the TLB as large as possible; while an oversized TLB
 * results in minimal TLB miss rates, it also takes longer to be flushed
 * (flushes can be _very_ frequent), and the reduced locality can also hurt
 * performance.
 *
 * To achieve near-optimal performance for all kinds of workloads, we:
 *
 * 1. Aggressively increase the size of the TLB when the use rate of the
 * TLB being flushed is high, since it is likely that in the near future this
 * memory-hungry process will execute again, and its memory hungriness will
 * probably be similar.
 *
 * 2. Slowly reduce the size of the TLB as the use rate declines over a
 * reasonably large time window. The rationale is that if in such a time window
 * we have not observed a high TLB use rate, it is likely that we won't observe
 * it in the near future. In that case, once a time window expires we downsize
 * the TLB to match the maximum use rate observed in the window.
 *
 * 3. Try to keep the maximum use rate in a time window in the 30-70% range,
 * since in that range performance is likely near-optimal. Recall that the TLB
 * is direct mapped, so we want the use rate to be low (or at least not too
 * high), since otherwise we are likely to have a significant amount of
 * conflict misses.
 */
static void tlb_mmu_resize_locked(CPUTLBDesc *desc, CPUTLBDescFast *fast,
                                  int64_t now)
{
    size_t old_size = tlb_n_entries(fast);
    size_t rate;
    size_t new_size = old_size;
    int64_t window_len_ms = 100;
    int64_t window_len_ns = window_len_ms * 1000 * 1000;
    bool window_expired = now > desc->window_begin_ns + window_len_ns;

    if (desc->n_used_entries > desc->window_max_entries) {
        desc->window_max_entries = desc->n_used_entries;
    }
    rate = desc->window_max_entries * 100 / old_size;

    if (rate > 70) {
        new_size = MIN(old_size << 1, 1 << CPU_TLB_DYN_MAX_BITS);
    } else if (rate < 30 && window_expired) {
        size_t ceil = pow2ceil(desc->window_max_entries);
        size_t expected_rate = desc->window_max_entries * 100 / ceil;

        /*
         * Avoid undersizing when the max number of entries seen is just below
         * a pow2. For instance, if max_entries == 1025, the expected use rate
         * would be 1025/2048==50%. However, if max_entries == 1023, we'd get
         * 1023/1024==99.9% use rate, so we'd likely end up doubling the size
         * later. Thus, make sure that the expected use rate remains below 70%.
         * (and since we double the size, that means the lowest rate we'd
         * expect to get is 35%, which is still in the 30-70% range where
         * we consider that the size is appropriate.)
         */
        if (expected_rate > 70) {
            ceil *= 2;
        }
        new_size = MAX(ceil, 1 << CPU_TLB_DYN_MIN_BITS);
    }

    if (new_size == old_size) {
        if (window_expired) {
            tlb_window_reset(desc, now, desc->n_used_entries);
        }
        return;
    }

    g_free(fast->table);
    g_free(desc->iotlb);

    tlb_window_reset(desc, now, 0);
    /* desc->n_used_entries is cleared by the caller */
    fast->mask = (new_size - 1) << CPU_TLB_ENTRY_BITS;
    fast->table = g_try_new(CPUTLBEntry, new_size);
    desc->iotlb = g_try_new(CPUIOTLBEntry, new_size);

    /*
     * If the allocations fail, try smaller sizes. We just freed some
     * memory, so going back to half of new_size has a good chance of working.
     * Increased memory pressure elsewhere in the system might cause the
     * allocations to fail though, so we progressively reduce the allocation
     * size, aborting if we cannot even allocate the smallest TLB we support.
     */
    while (fast->table == NULL || desc->iotlb == NULL) {
        if (new_size == (1 << CPU_TLB_DYN_MIN_BITS)) {
            error_report("%s: %s", __func__, strerror(errno));
            abort();
        }
        new_size = MAX(new_size >> 1, 1 << CPU_TLB_DYN_MIN_BITS);
        fast->mask = (new_size - 1) << CPU_TLB_ENTRY_BITS;

        g_free(fast->table);
        g_free(desc->iotlb);
        fast->table = g_try_new(CPUTLBEntry, new_size);
        desc->iotlb = g_try_new(CPUIOTLBEntry, new_size);
    }
}

static void tlb_mmu_flush_locked(CPUTLBDesc *desc, CPUTLBDescFast *fast)
{
    desc->n_used_entries = 0;
    desc->large_page_addr = -1;
    desc->large_page_mask = -1;
    desc->vindex = 0;
    memset(fast->table, -1, sizeof_tlb(fast));
    memset(desc->vtable, -1, sizeof(desc->vtable));
}

static void tlb_flush_one_mmuidx_locked(CPUArchState *env, int mmu_idx,
                                        int64_t now)
{
    CPUTLBDesc *desc = &env_tlb(env)->d[mmu_idx];
    CPUTLBDescFast *fast = &env_tlb(env)->f[mmu_idx];

    tlb_mmu_resize_locked(desc, fast, now);
    tlb_mmu_flush_locked(desc, fast);
}

static void tlb_mmu_init(CPUTLBDesc *desc, CPUTLBDescFast *fast, int64_t now)
{
    size_t n_entries = 1 << CPU_TLB_DYN_DEFAULT_BITS;

    tlb_window_reset(desc, now, 0);
    desc->n_used_entries = 0;
    fast->mask = (n_entries - 1) << CPU_TLB_ENTRY_BITS;
    fast->table = g_new(CPUTLBEntry, n_entries);
    desc->iotlb = g_new(CPUIOTLBEntry, n_entries);
    tlb_mmu_flush_locked(desc, fast);
}

static inline void tlb_n_used_entries_inc(CPUArchState *env, uintptr_t mmu_idx)
{
    env_tlb(env)->d[mmu_idx].n_used_entries++;
}

static inline void tlb_n_used_entries_dec(CPUArchState *env, uintptr_t mmu_idx)
{
    env_tlb(env)->d[mmu_idx].n_used_entries--;
}

void tlb_init(CPUState *cpu)
{
    CPUArchState *env = cpu->env_ptr;
    int64_t now = get_clock_realtime();
    int i;

    qemu_spin_init(&env_tlb(env)->c.lock);

    /* All tlbs are initialized flushed. */
    env_tlb(env)->c.dirty = 0;

    for (i = 0; i < NB_MMU_MODES; i++) {
        tlb_mmu_init(&env_tlb(env)->d[i], &env_tlb(env)->f[i], now);
    }
}

void tlb_destroy(CPUState *cpu)
{
    CPUArchState *env = cpu->env_ptr;
    int i;

    qemu_spin_destroy(&env_tlb(env)->c.lock);
    for (i = 0; i < NB_MMU_MODES; i++) {
        CPUTLBDesc *desc = &env_tlb(env)->d[i];
        CPUTLBDescFast *fast = &env_tlb(env)->f[i];

        g_free(fast->table);
        g_free(desc->iotlb);
    }
}

/* flush_all_helper: run fn across all cpus
 *
 * If the wait flag is set then the src cpu's helper will be queued as
 * "safe" work and the loop exited creating a synchronisation point
 * where all queued work will be finished before execution starts
 * again.
 */
static void flush_all_helper(CPUState *src, run_on_cpu_func fn,
                             run_on_cpu_data d)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (cpu != src) {
            async_run_on_cpu(cpu, fn, d);
        }
    }
}

void tlb_flush_counts(size_t *pfull, size_t *ppart, size_t *pelide)
{
    CPUState *cpu;
    size_t full = 0, part = 0, elide = 0;

    CPU_FOREACH(cpu) {
        CPUArchState *env = cpu->env_ptr;

        full += atomic_read(&env_tlb(env)->c.full_flush_count);
        part += atomic_read(&env_tlb(env)->c.part_flush_count);
        elide += atomic_read(&env_tlb(env)->c.elide_flush_count);
    }
    *pfull = full;
    *ppart = part;
    *pelide = elide;
}

static void tlb_flush_by_mmuidx_async_work(CPUState *cpu, run_on_cpu_data data)
{
    CPUArchState *env = cpu->env_ptr;
    uint16_t asked = data.host_int;
    uint16_t all_dirty, work, to_clean;
    int64_t now = get_clock_realtime();

    assert_cpu_is_self(cpu);

    tlb_debug("mmu_idx:0x%04" PRIx16 "\n", asked);

    qemu_spin_lock(&env_tlb(env)->c.lock);

    all_dirty = env_tlb(env)->c.dirty;
    to_clean = asked & all_dirty;
    all_dirty &= ~to_clean;
    env_tlb(env)->c.dirty = all_dirty;

    for (work = to_clean; work != 0; work &= work - 1) {
        int mmu_idx = ctz32(work);
        tlb_flush_one_mmuidx_locked(env, mmu_idx, now);
    }

    qemu_spin_unlock(&env_tlb(env)->c.lock);

    cpu_tb_jmp_cache_clear(cpu);

    if (to_clean == ALL_MMUIDX_BITS) {
        atomic_set(&env_tlb(env)->c.full_flush_count,
                   env_tlb(env)->c.full_flush_count + 1);
    } else {
        atomic_set(&env_tlb(env)->c.part_flush_count,
                   env_tlb(env)->c.part_flush_count + ctpop16(to_clean));
        if (to_clean != asked) {
            atomic_set(&env_tlb(env)->c.elide_flush_count,
                       env_tlb(env)->c.elide_flush_count +
                       ctpop16(asked & ~to_clean));
        }
    }
}

void tlb_flush_by_mmuidx(CPUState *cpu, uint16_t idxmap)
{
    tlb_debug("mmu_idx: 0x%" PRIx16 "\n", idxmap);

    if (cpu->created && !qemu_cpu_is_self(cpu)) {
        async_run_on_cpu(cpu, tlb_flush_by_mmuidx_async_work,
                         RUN_ON_CPU_HOST_INT(idxmap));
    } else {
        tlb_flush_by_mmuidx_async_work(cpu, RUN_ON_CPU_HOST_INT(idxmap));
    }
}

void tlb_flush(CPUState *cpu)
{
    tlb_flush_by_mmuidx(cpu, ALL_MMUIDX_BITS);
}

void tlb_flush_by_mmuidx_all_cpus(CPUState *src_cpu, uint16_t idxmap)
{
    const run_on_cpu_func fn = tlb_flush_by_mmuidx_async_work;

    tlb_debug("mmu_idx: 0x%"PRIx16"\n", idxmap);

    flush_all_helper(src_cpu, fn, RUN_ON_CPU_HOST_INT(idxmap));
    fn(src_cpu, RUN_ON_CPU_HOST_INT(idxmap));
}

void tlb_flush_all_cpus(CPUState *src_cpu)
{
    tlb_flush_by_mmuidx_all_cpus(src_cpu, ALL_MMUIDX_BITS);
}

void tlb_flush_by_mmuidx_all_cpus_synced(CPUState *src_cpu, uint16_t idxmap)
{
    const run_on_cpu_func fn = tlb_flush_by_mmuidx_async_work;

    tlb_debug("mmu_idx: 0x%"PRIx16"\n", idxmap);

    flush_all_helper(src_cpu, fn, RUN_ON_CPU_HOST_INT(idxmap));
    async_safe_run_on_cpu(src_cpu, fn, RUN_ON_CPU_HOST_INT(idxmap));
}

void tlb_flush_all_cpus_synced(CPUState *src_cpu)
{
    tlb_flush_by_mmuidx_all_cpus_synced(src_cpu, ALL_MMUIDX_BITS);
}

static inline bool tlb_hit_page_anyprot(CPUTLBEntry *tlb_entry,
                                        target_ulong page)
{
    return tlb_hit_page(tlb_entry->addr_read, page) ||
           tlb_hit_page(tlb_addr_write(tlb_entry), page) ||
           tlb_hit_page(tlb_entry->addr_code, page);
}

/**
 * tlb_entry_is_empty - return true if the entry is not in use
 * @te: pointer to CPUTLBEntry
 */
static inline bool tlb_entry_is_empty(const CPUTLBEntry *te)
{
    return te->addr_read == -1 && te->addr_write == -1 && te->addr_code == -1;
}

/* Called with tlb_c.lock held */
static inline bool tlb_flush_entry_locked(CPUTLBEntry *tlb_entry,
                                          target_ulong page)
{
    if (tlb_hit_page_anyprot(tlb_entry, page)) {
        memset(tlb_entry, -1, sizeof(*tlb_entry));
        return true;
    }
    return false;
}

/* Called with tlb_c.lock held */
static inline void tlb_flush_vtlb_page_locked(CPUArchState *env, int mmu_idx,
                                              target_ulong page)
{
    CPUTLBDesc *d = &env_tlb(env)->d[mmu_idx];
    int k;

    assert_cpu_is_self(env_cpu(env));
    for (k = 0; k < CPU_VTLB_SIZE; k++) {
        if (tlb_flush_entry_locked(&d->vtable[k], page)) {
            tlb_n_used_entries_dec(env, mmu_idx);
        }
    }
}

static void tlb_flush_page_locked(CPUArchState *env, int midx,
                                  target_ulong page)
{
    target_ulong lp_addr = env_tlb(env)->d[midx].large_page_addr;
    target_ulong lp_mask = env_tlb(env)->d[midx].large_page_mask;

    /* Check if we need to flush due to large pages.  */
    if ((page & lp_mask) == lp_addr) {
        tlb_debug("forcing full flush midx %d ("
                  TARGET_FMT_lx "/" TARGET_FMT_lx ")\n",
                  midx, lp_addr, lp_mask);
        tlb_flush_one_mmuidx_locked(env, midx, get_clock_realtime());
    } else {
        if (tlb_flush_entry_locked(tlb_entry(env, midx, page), page)) {
            tlb_n_used_entries_dec(env, midx);
        }
        tlb_flush_vtlb_page_locked(env, midx, page);
    }
}

/**
 * tlb_flush_page_by_mmuidx_async_0:
 * @cpu: cpu on which to flush
 * @addr: page of virtual address to flush
 * @idxmap: set of mmu_idx to flush
 *
 * Helper for tlb_flush_page_by_mmuidx and friends, flush one page
 * at @addr from the tlbs indicated by @idxmap from @cpu.
 */
static void tlb_flush_page_by_mmuidx_async_0(CPUState *cpu,
                                             target_ulong addr,
                                             uint16_t idxmap)
{
    CPUArchState *env = cpu->env_ptr;
    int mmu_idx;

    assert_cpu_is_self(cpu);

    tlb_debug("page addr:" TARGET_FMT_lx " mmu_map:0x%x\n", addr, idxmap);

    qemu_spin_lock(&env_tlb(env)->c.lock);
    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        if ((idxmap >> mmu_idx) & 1) {
            tlb_flush_page_locked(env, mmu_idx, addr);
        }
    }
    qemu_spin_unlock(&env_tlb(env)->c.lock);

    tb_flush_jmp_cache(cpu, addr);
}

/**
 * tlb_flush_page_by_mmuidx_async_1:
 * @cpu: cpu on which to flush
 * @data: encoded addr + idxmap
 *
 * Helper for tlb_flush_page_by_mmuidx and friends, called through
 * async_run_on_cpu.  The idxmap parameter is encoded in the page
 * offset of the target_ptr field.  This limits the set of mmu_idx
 * that can be passed via this method.
 */
static void tlb_flush_page_by_mmuidx_async_1(CPUState *cpu,
                                             run_on_cpu_data data)
{
    target_ulong addr_and_idxmap = (target_ulong) data.target_ptr;
    target_ulong addr = addr_and_idxmap & TARGET_PAGE_MASK;
    uint16_t idxmap = addr_and_idxmap & ~TARGET_PAGE_MASK;

    tlb_flush_page_by_mmuidx_async_0(cpu, addr, idxmap);
}

typedef struct {
    target_ulong addr;
    uint16_t idxmap;
} TLBFlushPageByMMUIdxData;

/**
 * tlb_flush_page_by_mmuidx_async_2:
 * @cpu: cpu on which to flush
 * @data: allocated addr + idxmap
 *
 * Helper for tlb_flush_page_by_mmuidx and friends, called through
 * async_run_on_cpu.  The addr+idxmap parameters are stored in a
 * TLBFlushPageByMMUIdxData structure that has been allocated
 * specifically for this helper.  Free the structure when done.
 */
static void tlb_flush_page_by_mmuidx_async_2(CPUState *cpu,
                                             run_on_cpu_data data)
{
    TLBFlushPageByMMUIdxData *d = data.host_ptr;

    tlb_flush_page_by_mmuidx_async_0(cpu, d->addr, d->idxmap);
    g_free(d);
}

void tlb_flush_page_by_mmuidx(CPUState *cpu, target_ulong addr, uint16_t idxmap)
{
    tlb_debug("addr: "TARGET_FMT_lx" mmu_idx:%" PRIx16 "\n", addr, idxmap);

    /* This should already be page aligned */
    addr &= TARGET_PAGE_MASK;

    if (qemu_cpu_is_self(cpu)) {
        tlb_flush_page_by_mmuidx_async_0(cpu, addr, idxmap);
    } else if (idxmap < TARGET_PAGE_SIZE) {
        /*
         * Most targets have only a few mmu_idx.  In the case where
         * we can stuff idxmap into the low TARGET_PAGE_BITS, avoid
         * allocating memory for this operation.
         */
        async_run_on_cpu(cpu, tlb_flush_page_by_mmuidx_async_1,
                         RUN_ON_CPU_TARGET_PTR(addr | idxmap));
    } else {
        TLBFlushPageByMMUIdxData *d = g_new(TLBFlushPageByMMUIdxData, 1);

        /* Otherwise allocate a structure, freed by the worker.  */
        d->addr = addr;
        d->idxmap = idxmap;
        async_run_on_cpu(cpu, tlb_flush_page_by_mmuidx_async_2,
                         RUN_ON_CPU_HOST_PTR(d));
    }
}

void tlb_flush_page(CPUState *cpu, target_ulong addr)
{
    tlb_flush_page_by_mmuidx(cpu, addr, ALL_MMUIDX_BITS);
}

void tlb_flush_page_by_mmuidx_all_cpus(CPUState *src_cpu, target_ulong addr,
                                       uint16_t idxmap)
{
    tlb_debug("addr: "TARGET_FMT_lx" mmu_idx:%"PRIx16"\n", addr, idxmap);

    /* This should already be page aligned */
    addr &= TARGET_PAGE_MASK;

    /*
     * Allocate memory to hold addr+idxmap only when needed.
     * See tlb_flush_page_by_mmuidx for details.
     */
    if (idxmap < TARGET_PAGE_SIZE) {
        flush_all_helper(src_cpu, tlb_flush_page_by_mmuidx_async_1,
                         RUN_ON_CPU_TARGET_PTR(addr | idxmap));
    } else {
        CPUState *dst_cpu;

        /* Allocate a separate data block for each destination cpu.  */
        CPU_FOREACH(dst_cpu) {
            if (dst_cpu != src_cpu) {
                TLBFlushPageByMMUIdxData *d
                    = g_new(TLBFlushPageByMMUIdxData, 1);

                d->addr = addr;
                d->idxmap = idxmap;
                async_run_on_cpu(dst_cpu, tlb_flush_page_by_mmuidx_async_2,
                                 RUN_ON_CPU_HOST_PTR(d));
            }
        }
    }

    tlb_flush_page_by_mmuidx_async_0(src_cpu, addr, idxmap);
}

void tlb_flush_page_all_cpus(CPUState *src, target_ulong addr)
{
    tlb_flush_page_by_mmuidx_all_cpus(src, addr, ALL_MMUIDX_BITS);
}

void tlb_flush_page_by_mmuidx_all_cpus_synced(CPUState *src_cpu,
                                              target_ulong addr,
                                              uint16_t idxmap)
{
    tlb_debug("addr: "TARGET_FMT_lx" mmu_idx:%"PRIx16"\n", addr, idxmap);

    /* This should already be page aligned */
    addr &= TARGET_PAGE_MASK;

    /*
     * Allocate memory to hold addr+idxmap only when needed.
     * See tlb_flush_page_by_mmuidx for details.
     */
    if (idxmap < TARGET_PAGE_SIZE) {
        flush_all_helper(src_cpu, tlb_flush_page_by_mmuidx_async_1,
                         RUN_ON_CPU_TARGET_PTR(addr | idxmap));
        async_safe_run_on_cpu(src_cpu, tlb_flush_page_by_mmuidx_async_1,
                              RUN_ON_CPU_TARGET_PTR(addr | idxmap));
    } else {
        CPUState *dst_cpu;
        TLBFlushPageByMMUIdxData *d;

        /* Allocate a separate data block for each destination cpu.  */
        CPU_FOREACH(dst_cpu) {
            if (dst_cpu != src_cpu) {
                d = g_new(TLBFlushPageByMMUIdxData, 1);
                d->addr = addr;
                d->idxmap = idxmap;
                async_run_on_cpu(dst_cpu, tlb_flush_page_by_mmuidx_async_2,
                                 RUN_ON_CPU_HOST_PTR(d));
            }
        }

        d = g_new(TLBFlushPageByMMUIdxData, 1);
        d->addr = addr;
        d->idxmap = idxmap;
        async_safe_run_on_cpu(src_cpu, tlb_flush_page_by_mmuidx_async_2,
                              RUN_ON_CPU_HOST_PTR(d));
    }
}

void tlb_flush_page_all_cpus_synced(CPUState *src, target_ulong addr)
{
    tlb_flush_page_by_mmuidx_all_cpus_synced(src, addr, ALL_MMUIDX_BITS);
}

/* update the TLBs so that writes to code in the virtual page 'addr'
   can be detected */
void tlb_protect_code(ram_addr_t ram_addr)
{
    cpu_physical_memory_test_and_clear_dirty(ram_addr, TARGET_PAGE_SIZE,
                                             DIRTY_MEMORY_CODE);
}

/* update the TLB so that writes in physical page 'phys_addr' are no longer
   tested for self modifying code */
void tlb_unprotect_code(ram_addr_t ram_addr)
{
    cpu_physical_memory_set_dirty_flag(ram_addr, DIRTY_MEMORY_CODE);
}


/*
 * Dirty write flag handling
 *
 * When the TCG code writes to a location it looks up the address in
 * the TLB and uses that data to compute the final address. If any of
 * the lower bits of the address are set then the slow path is forced.
 * There are a number of reasons to do this but for normal RAM the
 * most usual is detecting writes to code regions which may invalidate
 * generated code.
 *
 * Other vCPUs might be reading their TLBs during guest execution, so we update
 * te->addr_write with atomic_set. We don't need to worry about this for
 * oversized guests as MTTCG is disabled for them.
 *
 * Called with tlb_c.lock held.
 */
static void tlb_reset_dirty_range_locked(CPUTLBEntry *tlb_entry,
                                         uintptr_t start, uintptr_t length)
{
    uintptr_t addr = tlb_entry->addr_write;

    if ((addr & (TLB_INVALID_MASK | TLB_MMIO |
                 TLB_DISCARD_WRITE | TLB_NOTDIRTY)) == 0) {
        addr &= TARGET_PAGE_MASK;
        addr += tlb_entry->addend;
        if ((addr - start) < length) {
#if TCG_OVERSIZED_GUEST
            tlb_entry->addr_write |= TLB_NOTDIRTY;
#else
            atomic_set(&tlb_entry->addr_write,
                       tlb_entry->addr_write | TLB_NOTDIRTY);
#endif
        }
    }
}

/*
 * Called with tlb_c.lock held.
 * Called only from the vCPU context, i.e. the TLB's owner thread.
 */
static inline void copy_tlb_helper_locked(CPUTLBEntry *d, const CPUTLBEntry *s)
{
    *d = *s;
}

/* This is a cross vCPU call (i.e. another vCPU resetting the flags of
 * the target vCPU).
 * We must take tlb_c.lock to avoid racing with another vCPU update. The only
 * thing actually updated is the target TLB entry ->addr_write flags.
 */
void tlb_reset_dirty(CPUState *cpu, ram_addr_t start1, ram_addr_t length)
{
    CPUArchState *env;

    int mmu_idx;

    env = cpu->env_ptr;
    qemu_spin_lock(&env_tlb(env)->c.lock);
    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        unsigned int i;
        unsigned int n = tlb_n_entries(&env_tlb(env)->f[mmu_idx]);

        for (i = 0; i < n; i++) {
            tlb_reset_dirty_range_locked(&env_tlb(env)->f[mmu_idx].table[i],
                                         start1, length);
        }

        for (i = 0; i < CPU_VTLB_SIZE; i++) {
            tlb_reset_dirty_range_locked(&env_tlb(env)->d[mmu_idx].vtable[i],
                                         start1, length);
        }
    }
    qemu_spin_unlock(&env_tlb(env)->c.lock);
}

/* Called with tlb_c.lock held */
static inline void tlb_set_dirty1_locked(CPUTLBEntry *tlb_entry,
                                         target_ulong vaddr)
{
    if (tlb_entry->addr_write == (vaddr | TLB_NOTDIRTY)) {
        tlb_entry->addr_write = vaddr;
    }
}

/* update the TLB corresponding to virtual page vaddr
   so that it is no longer dirty */
void tlb_set_dirty(CPUState *cpu, target_ulong vaddr)
{
    CPUArchState *env = cpu->env_ptr;
    int mmu_idx;

    assert_cpu_is_self(cpu);

    vaddr &= TARGET_PAGE_MASK;
    qemu_spin_lock(&env_tlb(env)->c.lock);
    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        tlb_set_dirty1_locked(tlb_entry(env, mmu_idx, vaddr), vaddr);
    }

    for (mmu_idx = 0; mmu_idx < NB_MMU_MODES; mmu_idx++) {
        int k;
        for (k = 0; k < CPU_VTLB_SIZE; k++) {
            tlb_set_dirty1_locked(&env_tlb(env)->d[mmu_idx].vtable[k], vaddr);
        }
    }
    qemu_spin_unlock(&env_tlb(env)->c.lock);
}

/* Our TLB does not support large pages, so remember the area covered by
   large pages and trigger a full TLB flush if these are invalidated.  */
static void tlb_add_large_page(CPUArchState *env, int mmu_idx,
                               target_ulong vaddr, target_ulong size)
{
    target_ulong lp_addr = env_tlb(env)->d[mmu_idx].large_page_addr;
    target_ulong lp_mask = ~(size - 1);

    if (lp_addr == (target_ulong)-1) {
        /* No previous large page.  */
        lp_addr = vaddr;
    } else {
        /* Extend the existing region to include the new page.
           This is a compromise between unnecessary flushes and
           the cost of maintaining a full variable size TLB.  */
        lp_mask &= env_tlb(env)->d[mmu_idx].large_page_mask;
        while (((lp_addr ^ vaddr) & lp_mask) != 0) {
            lp_mask <<= 1;
        }
    }
    env_tlb(env)->d[mmu_idx].large_page_addr = lp_addr & lp_mask;
    env_tlb(env)->d[mmu_idx].large_page_mask = lp_mask;
}

/* Add a new TLB entry. At most one entry for a given virtual address
 * is permitted. Only a single TARGET_PAGE_SIZE region is mapped, the
 * supplied size is only used by tlb_flush_page.
 *
 * Called from TCG-generated code, which is under an RCU read-side
 * critical section.
 */
void tlb_set_page_with_attrs(CPUState *cpu, target_ulong vaddr,
                             hwaddr paddr, MemTxAttrs attrs, int prot,
                             int mmu_idx, target_ulong size)
{
    CPUArchState *env = cpu->env_ptr;
    CPUTLB *tlb = env_tlb(env);
    CPUTLBDesc *desc = &tlb->d[mmu_idx];
    MemoryRegionSection *section;
    unsigned int index;
    target_ulong address;
    target_ulong write_address;
    uintptr_t addend;
    CPUTLBEntry *te, tn;
    hwaddr iotlb, xlat, sz, paddr_page;
    target_ulong vaddr_page;
    int asidx = cpu_asidx_from_attrs(cpu, attrs);
    int wp_flags;
    bool is_ram, is_romd;

    assert_cpu_is_self(cpu);

    if (size <= TARGET_PAGE_SIZE) {
        sz = TARGET_PAGE_SIZE;
    } else {
        tlb_add_large_page(env, mmu_idx, vaddr, size);
        sz = size;
    }
    vaddr_page = vaddr & TARGET_PAGE_MASK;
    paddr_page = paddr & TARGET_PAGE_MASK;

    section = address_space_translate_for_iotlb(cpu, asidx, paddr_page,
                                                &xlat, &sz, attrs, &prot);
    assert(sz >= TARGET_PAGE_SIZE);

    tlb_debug("vaddr=" TARGET_FMT_lx " paddr=0x" TARGET_FMT_plx
              " prot=%x idx=%d\n",
              vaddr, paddr, prot, mmu_idx);

    address = vaddr_page;
    if (size < TARGET_PAGE_SIZE) {
        /* Repeat the MMU check and TLB fill on every access.  */
        address |= TLB_INVALID_MASK;
    }
    if (attrs.byte_swap) {
        address |= TLB_BSWAP;
    }

    is_ram = memory_region_is_ram(section->mr);
    is_romd = memory_region_is_romd(section->mr);

    if (is_ram || is_romd) {
        /* RAM and ROMD both have associated host memory. */
        addend = (uintptr_t)memory_region_get_ram_ptr(section->mr) + xlat;
    } else {
        /* I/O does not; force the host address to NULL. */
        addend = 0;
    }

    write_address = address;
    if (is_ram) {
        iotlb = memory_region_get_ram_addr(section->mr) + xlat;
        /*
         * Computing is_clean is expensive; avoid all that unless
         * the page is actually writable.
         */
        if (prot & PAGE_WRITE) {
            if (section->readonly) {
                write_address |= TLB_DISCARD_WRITE;
            } else if (cpu_physical_memory_is_clean(iotlb)) {
                write_address |= TLB_NOTDIRTY;
            }
        }
    } else {
        /* I/O or ROMD */
        iotlb = memory_region_section_get_iotlb(cpu, section) + xlat;
        /*
         * Writes to romd devices must go through MMIO to enable write.
         * Reads to romd devices go through the ram_ptr found above,
         * but of course reads to I/O must go through MMIO.
         */
        write_address |= TLB_MMIO;
        if (!is_romd) {
            address = write_address;
        }
    }

    wp_flags = cpu_watchpoint_address_matches(cpu, vaddr_page,
                                              TARGET_PAGE_SIZE);

    index = tlb_index(env, mmu_idx, vaddr_page);
    te = tlb_entry(env, mmu_idx, vaddr_page);

    /*
     * Hold the TLB lock for the rest of the function. We could acquire/release
     * the lock several times in the function, but it is faster to amortize the
     * acquisition cost by acquiring it just once. Note that this leads to
     * a longer critical section, but this is not a concern since the TLB lock
     * is unlikely to be contended.
     */
    qemu_spin_lock(&tlb->c.lock);

    /* Note that the tlb is no longer clean.  */
    tlb->c.dirty |= 1 << mmu_idx;

    /* Make sure there's no cached translation for the new page.  */
    tlb_flush_vtlb_page_locked(env, mmu_idx, vaddr_page);

    /*
     * Only evict the old entry to the victim tlb if it's for a
     * different page; otherwise just overwrite the stale data.
     */
    if (!tlb_hit_page_anyprot(te, vaddr_page) && !tlb_entry_is_empty(te)) {
        unsigned vidx = desc->vindex++ % CPU_VTLB_SIZE;
        CPUTLBEntry *tv = &desc->vtable[vidx];

        /* Evict the old entry into the victim tlb.  */
        copy_tlb_helper_locked(tv, te);
        desc->viotlb[vidx] = desc->iotlb[index];
        tlb_n_used_entries_dec(env, mmu_idx);
    }

    /* refill the tlb */
    /*
     * At this point iotlb contains a physical section number in the lower
     * TARGET_PAGE_BITS, and either
     *  + the ram_addr_t of the page base of the target RAM (RAM)
     *  + the offset within section->mr of the page base (I/O, ROMD)
     * We subtract the vaddr_page (which is page aligned and thus won't
     * disturb the low bits) to give an offset which can be added to the
     * (non-page-aligned) vaddr of the eventual memory access to get
     * the MemoryRegion offset for the access. Note that the vaddr we
     * subtract here is that of the page base, and not the same as the
     * vaddr we add back in io_readx()/io_writex()/get_page_addr_code().
     */
    desc->iotlb[index].addr = iotlb - vaddr_page;
    desc->iotlb[index].attrs = attrs;

    /* Now calculate the new entry */
    tn.addend = addend - vaddr_page;
    if (prot & PAGE_READ) {
        tn.addr_read = address;
        if (wp_flags & BP_MEM_READ) {
            tn.addr_read |= TLB_WATCHPOINT;
        }
    } else {
        tn.addr_read = -1;
    }

    if (prot & PAGE_EXEC) {
        tn.addr_code = address;
    } else {
        tn.addr_code = -1;
    }

    tn.addr_write = -1;
    if (prot & PAGE_WRITE) {
        tn.addr_write = write_address;
        if (prot & PAGE_WRITE_INV) {
            tn.addr_write |= TLB_INVALID_MASK;
        }
        if (wp_flags & BP_MEM_WRITE) {
            tn.addr_write |= TLB_WATCHPOINT;
        }
    }

    copy_tlb_helper_locked(te, &tn);
    tlb_n_used_entries_inc(env, mmu_idx);
    qemu_spin_unlock(&tlb->c.lock);
}

/* Add a new TLB entry, but without specifying the memory
 * transaction attributes to be used.
 */
void tlb_set_page(CPUState *cpu, target_ulong vaddr,
                  hwaddr paddr, int prot,
                  int mmu_idx, target_ulong size)
{
    tlb_set_page_with_attrs(cpu, vaddr, paddr, MEMTXATTRS_UNSPECIFIED,
                            prot, mmu_idx, size);
}

static inline ram_addr_t qemu_ram_addr_from_host_nofail(void *ptr)
{
    ram_addr_t ram_addr;

    ram_addr = qemu_ram_addr_from_host(ptr);
    if (ram_addr == RAM_ADDR_INVALID) {
        error_report("Bad ram pointer %p", ptr);
        abort();
    }
    return ram_addr;
}

/*
 * Note: tlb_fill() can trigger a resize of the TLB. This means that all of the
 * caller's prior references to the TLB table (e.g. CPUTLBEntry pointers) must
 * be discarded and looked up again (e.g. via tlb_entry()).
 */
static void tlb_fill(CPUState *cpu, target_ulong addr, int size,
                     MMUAccessType access_type, int mmu_idx, uintptr_t retaddr)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    bool ok;

    /*
     * This is not a probe, so only valid return is success; failure
     * should result in exception + longjmp to the cpu loop.
     */
    ok = cc->tlb_fill(cpu, addr, size, access_type, mmu_idx, false, retaddr);
    assert(ok);
}

static uint64_t io_readx(CPUArchState *env, CPUIOTLBEntry *iotlbentry,
                         int mmu_idx, target_ulong addr, uintptr_t retaddr,
                         MMUAccessType access_type, MemOp op)
{
    CPUState *cpu = env_cpu(env);
    hwaddr mr_offset;
    MemoryRegionSection *section;
    MemoryRegion *mr;
    uint64_t val;
    bool locked = false;
    MemTxResult r;

    section = iotlb_to_section(cpu, iotlbentry->addr, iotlbentry->attrs);
    mr = section->mr;
    mr_offset = (iotlbentry->addr & TARGET_PAGE_MASK) + addr;
    cpu->mem_io_pc = retaddr;
    if (!cpu->can_do_io) {
        cpu_io_recompile(cpu, retaddr);
    }

    if (mr->global_locking && !qemu_mutex_iothread_locked()) {
        qemu_mutex_lock_iothread();
        locked = true;
    }
    r = memory_region_dispatch_read(mr, mr_offset, &val, op, iotlbentry->attrs);
    if (r != MEMTX_OK) {
        hwaddr physaddr = mr_offset +
            section->offset_within_address_space -
            section->offset_within_region;

        cpu_transaction_failed(cpu, physaddr, addr, memop_size(op), access_type,
                               mmu_idx, iotlbentry->attrs, r, retaddr);
    }
    if (locked) {
        qemu_mutex_unlock_iothread();
    }

    return val;
}

/*
 * Save a potentially trashed IOTLB entry for later lookup by plugin.
 * This is read by tlb_plugin_lookup if the iotlb entry doesn't match
 * because of the side effect of io_writex changing memory layout.
 */
static void save_iotlb_data(CPUState *cs, hwaddr addr,
                            MemoryRegionSection *section, hwaddr mr_offset)
{
#ifdef CONFIG_PLUGIN
    SavedIOTLB *saved = &cs->saved_iotlb;
    saved->addr = addr;
    saved->section = section;
    saved->mr_offset = mr_offset;
#endif
}

static void io_writex(CPUArchState *env, CPUIOTLBEntry *iotlbentry,
                      int mmu_idx, uint64_t val, target_ulong addr,
                      uintptr_t retaddr, MemOp op)
{
    CPUState *cpu = env_cpu(env);
    hwaddr mr_offset;
    MemoryRegionSection *section;
    MemoryRegion *mr;
    bool locked = false;
    MemTxResult r;

    section = iotlb_to_section(cpu, iotlbentry->addr, iotlbentry->attrs);
    mr = section->mr;
    mr_offset = (iotlbentry->addr & TARGET_PAGE_MASK) + addr;
    if (!cpu->can_do_io) {
        cpu_io_recompile(cpu, retaddr);
    }
    cpu->mem_io_pc = retaddr;

    /*
     * The memory_region_dispatch may trigger a flush/resize
     * so for plugins we save the iotlb_data just in case.
     */
    save_iotlb_data(cpu, iotlbentry->addr, section, mr_offset);

    if (mr->global_locking && !qemu_mutex_iothread_locked()) {
        qemu_mutex_lock_iothread();
        locked = true;
    }
    r = memory_region_dispatch_write(mr, mr_offset, val, op, iotlbentry->attrs);
    if (r != MEMTX_OK) {
        hwaddr physaddr = mr_offset +
            section->offset_within_address_space -
            section->offset_within_region;

        cpu_transaction_failed(cpu, physaddr, addr, memop_size(op),
                               MMU_DATA_STORE, mmu_idx, iotlbentry->attrs, r,
                               retaddr);
    }
    if (locked) {
        qemu_mutex_unlock_iothread();
    }
}

static inline target_ulong tlb_read_ofs(CPUTLBEntry *entry, size_t ofs)
{
#if TCG_OVERSIZED_GUEST
    return *(target_ulong *)((uintptr_t)entry + ofs);
#else
    /* ofs might correspond to .addr_write, so use atomic_read */
    return atomic_read((target_ulong *)((uintptr_t)entry + ofs));
#endif
}

/* Return true if ADDR is present in the victim tlb, and has been copied
   back to the main tlb.  */
static bool victim_tlb_hit(CPUArchState *env, size_t mmu_idx, size_t index,
                           size_t elt_ofs, target_ulong page)
{
    size_t vidx;

    assert_cpu_is_self(env_cpu(env));
    for (vidx = 0; vidx < CPU_VTLB_SIZE; ++vidx) {
        CPUTLBEntry *vtlb = &env_tlb(env)->d[mmu_idx].vtable[vidx];
        target_ulong cmp;

        /* elt_ofs might correspond to .addr_write, so use atomic_read */
#if TCG_OVERSIZED_GUEST
        cmp = *(target_ulong *)((uintptr_t)vtlb + elt_ofs);
#else
        cmp = atomic_read((target_ulong *)((uintptr_t)vtlb + elt_ofs));
#endif

        if (cmp == page) {
            /* Found entry in victim tlb, swap tlb and iotlb.  */
            CPUTLBEntry tmptlb, *tlb = &env_tlb(env)->f[mmu_idx].table[index];

            qemu_spin_lock(&env_tlb(env)->c.lock);
            copy_tlb_helper_locked(&tmptlb, tlb);
            copy_tlb_helper_locked(tlb, vtlb);
            copy_tlb_helper_locked(vtlb, &tmptlb);
            qemu_spin_unlock(&env_tlb(env)->c.lock);

            CPUIOTLBEntry tmpio, *io = &env_tlb(env)->d[mmu_idx].iotlb[index];
            CPUIOTLBEntry *vio = &env_tlb(env)->d[mmu_idx].viotlb[vidx];
            tmpio = *io; *io = *vio; *vio = tmpio;
            return true;
        }
    }
    return false;
}

/* Macro to call the above, with local variables from the use context.  */
#define VICTIM_TLB_HIT(TY, ADDR) \
  victim_tlb_hit(env, mmu_idx, index, offsetof(CPUTLBEntry, TY), \
                 (ADDR) & TARGET_PAGE_MASK)

/*
 * Return a ram_addr_t for the virtual address for execution.
 *
 * Return -1 if we can't translate and execute from an entire page
 * of RAM.  This will force us to execute by loading and translating
 * one insn at a time, without caching.
 *
 * NOTE: This function will trigger an exception if the page is
 * not executable.
 */
tb_page_addr_t get_page_addr_code_hostp(CPUArchState *env, target_ulong addr,
                                        void **hostp)
{
    uintptr_t mmu_idx = cpu_mmu_index(env, true);
    uintptr_t index = tlb_index(env, mmu_idx, addr);
    CPUTLBEntry *entry = tlb_entry(env, mmu_idx, addr);
    void *p;

    if (unlikely(!tlb_hit(entry->addr_code, addr))) {
        if (!VICTIM_TLB_HIT(addr_code, addr)) {
            tlb_fill(env_cpu(env), addr, 0, MMU_INST_FETCH, mmu_idx, 0);
            index = tlb_index(env, mmu_idx, addr);
            entry = tlb_entry(env, mmu_idx, addr);

            if (unlikely(entry->addr_code & TLB_INVALID_MASK)) {
                /*
                 * The MMU protection covers a smaller range than a target
                 * page, so we must redo the MMU check for every insn.
                 */
                return -1;
            }
        }
        assert(tlb_hit(entry->addr_code, addr));
    }

    if (unlikely(entry->addr_code & TLB_MMIO)) {
        /* The region is not backed by RAM.  */
        if (hostp) {
            *hostp = NULL;
        }
        return -1;
    }

    p = (void *)((uintptr_t)addr + entry->addend);
    if (hostp) {
        *hostp = p;
    }
    return qemu_ram_addr_from_host_nofail(p);
}

tb_page_addr_t get_page_addr_code(CPUArchState *env, target_ulong addr)
{
    return get_page_addr_code_hostp(env, addr, NULL);
}

static void notdirty_write(CPUState *cpu, vaddr mem_vaddr, unsigned size,
                           CPUIOTLBEntry *iotlbentry, uintptr_t retaddr)
{
    ram_addr_t ram_addr = mem_vaddr + iotlbentry->addr;

    trace_memory_notdirty_write_access(mem_vaddr, ram_addr, size);

    if (!cpu_physical_memory_get_dirty_flag(ram_addr, DIRTY_MEMORY_CODE)) {
        struct page_collection *pages
            = page_collection_lock(ram_addr, ram_addr + size);
        tb_invalidate_phys_page_fast(pages, ram_addr, size, retaddr);
        page_collection_unlock(pages);
    }

    /*
     * Set both VGA and migration bits for simplicity and to remove
     * the notdirty callback faster.
     */
    cpu_physical_memory_set_dirty_range(ram_addr, size, DIRTY_CLIENTS_NOCODE);

    /* We remove the notdirty callback only if the code has been flushed. */
    if (!cpu_physical_memory_is_clean(ram_addr)) {
        trace_memory_notdirty_set_dirty(mem_vaddr);
        tlb_set_dirty(cpu, mem_vaddr);
    }
}

static int probe_access_internal(CPUArchState *env, target_ulong addr,
                                 int fault_size, MMUAccessType access_type,
                                 int mmu_idx, bool nonfault,
                                 void **phost, uintptr_t retaddr)
{
    uintptr_t index = tlb_index(env, mmu_idx, addr);
    CPUTLBEntry *entry = tlb_entry(env, mmu_idx, addr);
    target_ulong tlb_addr, page_addr;
    size_t elt_ofs;
    int flags;

    switch (access_type) {
    case MMU_DATA_LOAD:
        elt_ofs = offsetof(CPUTLBEntry, addr_read);
        break;
    case MMU_DATA_STORE:
        elt_ofs = offsetof(CPUTLBEntry, addr_write);
        break;
    case MMU_INST_FETCH:
        elt_ofs = offsetof(CPUTLBEntry, addr_code);
        break;
    default:
        g_assert_not_reached();
    }
    tlb_addr = tlb_read_ofs(entry, elt_ofs);

    page_addr = addr & TARGET_PAGE_MASK;
    if (!tlb_hit_page(tlb_addr, page_addr)) {
        if (!victim_tlb_hit(env, mmu_idx, index, elt_ofs, page_addr)) {
            CPUState *cs = env_cpu(env);
            CPUClass *cc = CPU_GET_CLASS(cs);

            if (!cc->tlb_fill(cs, addr, fault_size, access_type,
                              mmu_idx, nonfault, retaddr)) {
                /* Non-faulting page table read failed.  */
                *phost = NULL;
                return TLB_INVALID_MASK;
            }

            /* TLB resize via tlb_fill may have moved the entry.  */
            entry = tlb_entry(env, mmu_idx, addr);
        }
        tlb_addr = tlb_read_ofs(entry, elt_ofs);
    }
    flags = tlb_addr & TLB_FLAGS_MASK;

    /* Fold all "mmio-like" bits into TLB_MMIO.  This is not RAM.  */
    if (unlikely(flags & ~(TLB_WATCHPOINT | TLB_NOTDIRTY))) {
        *phost = NULL;
        return TLB_MMIO;
    }

    /* Everything else is RAM. */
    *phost = (void *)((uintptr_t)addr + entry->addend);
    return flags;
}

int probe_access_flags(CPUArchState *env, target_ulong addr,
                       MMUAccessType access_type, int mmu_idx,
                       bool nonfault, void **phost, uintptr_t retaddr)
{
    int flags;

    flags = probe_access_internal(env, addr, 0, access_type, mmu_idx,
                                  nonfault, phost, retaddr);

    /* Handle clean RAM pages.  */
    if (unlikely(flags & TLB_NOTDIRTY)) {
        uintptr_t index = tlb_index(env, mmu_idx, addr);
        CPUIOTLBEntry *iotlbentry = &env_tlb(env)->d[mmu_idx].iotlb[index];

        notdirty_write(env_cpu(env), addr, 1, iotlbentry, retaddr);
        flags &= ~TLB_NOTDIRTY;
    }

    return flags;
}

void *probe_access(CPUArchState *env, target_ulong addr, int size,
                   MMUAccessType access_type, int mmu_idx, uintptr_t retaddr)
{
    void *host;
    int flags;

    g_assert(-(addr | TARGET_PAGE_MASK) >= size);

    flags = probe_access_internal(env, addr, size, access_type, mmu_idx,
                                  false, &host, retaddr);

    /* Per the interface, size == 0 merely faults the access. */
    if (size == 0) {
        return NULL;
    }

    if (unlikely(flags & (TLB_NOTDIRTY | TLB_WATCHPOINT))) {
        uintptr_t index = tlb_index(env, mmu_idx, addr);
        CPUIOTLBEntry *iotlbentry = &env_tlb(env)->d[mmu_idx].iotlb[index];

        /* Handle watchpoints.  */
        if (flags & TLB_WATCHPOINT) {
            int wp_access = (access_type == MMU_DATA_STORE
                             ? BP_MEM_WRITE : BP_MEM_READ);
            cpu_check_watchpoint(env_cpu(env), addr, size,
                                 iotlbentry->attrs, wp_access, retaddr);
        }

        /* Handle clean RAM pages.  */
        if (flags & TLB_NOTDIRTY) {
            notdirty_write(env_cpu(env), addr, 1, iotlbentry, retaddr);
        }
    }

    return host;
}

void *tlb_vaddr_to_host(CPUArchState *env, abi_ptr addr,
                        MMUAccessType access_type, int mmu_idx)
{
    void *host;
    int flags;

    flags = probe_access_internal(env, addr, 0, access_type,
                                  mmu_idx, true, &host, 0);

    /* No combination of flags are expected by the caller. */
    return flags ? NULL : host;
}

#ifdef CONFIG_PLUGIN
/*
 * Perform a TLB lookup and populate the qemu_plugin_hwaddr structure.
 * This should be a hot path as we will have just looked this path up
 * in the softmmu lookup code (or helper). We don't handle re-fills or
 * checking the victim table. This is purely informational.
 *
 * This almost never fails as the memory access being instrumented
 * should have just filled the TLB. The one corner case is io_writex
 * which can cause TLB flushes and potential resizing of the TLBs
 * losing the information we need. In those cases we need to recover
 * data from a copy of the iotlbentry. As long as this always occurs
 * from the same thread (which a mem callback will be) this is safe.
 */

bool tlb_plugin_lookup(CPUState *cpu, target_ulong addr, int mmu_idx,
                       bool is_store, struct qemu_plugin_hwaddr *data)
{
    CPUArchState *env = cpu->env_ptr;
    CPUTLBEntry *tlbe = tlb_entry(env, mmu_idx, addr);
    uintptr_t index = tlb_index(env, mmu_idx, addr);
    target_ulong tlb_addr = is_store ? tlb_addr_write(tlbe) : tlbe->addr_read;

    if (likely(tlb_hit(tlb_addr, addr))) {
        /* We must have an iotlb entry for MMIO */
        if (tlb_addr & TLB_MMIO) {
            CPUIOTLBEntry *iotlbentry;
            iotlbentry = &env_tlb(env)->d[mmu_idx].iotlb[index];
            data->is_io = true;
            data->v.io.section = iotlb_to_section(cpu, iotlbentry->addr, iotlbentry->attrs);
            data->v.io.offset = (iotlbentry->addr & TARGET_PAGE_MASK) + addr;
        } else {
            data->is_io = false;
            data->v.ram.hostaddr = addr + tlbe->addend;
        }
        return true;
    } else {
        SavedIOTLB *saved = &cpu->saved_iotlb;
        data->is_io = true;
        data->v.io.section = saved->section;
        data->v.io.offset = saved->mr_offset;
        return true;
    }
}

#endif

/* Probe for a read-modify-write atomic operation.  Do not allow unaligned
 * operations, or io operations to proceed.  Return the host address.  */
static void *atomic_mmu_lookup(CPUArchState *env, target_ulong addr,
                               TCGMemOpIdx oi, uintptr_t retaddr)
{
    size_t mmu_idx = get_mmuidx(oi);
    uintptr_t index = tlb_index(env, mmu_idx, addr);
    CPUTLBEntry *tlbe = tlb_entry(env, mmu_idx, addr);
    target_ulong tlb_addr = tlb_addr_write(tlbe);
    MemOp mop = get_memop(oi);
    int a_bits = get_alignment_bits(mop);
    int s_bits = mop & MO_SIZE;
    void *hostaddr;

    /* Adjust the given return address.  */
    retaddr -= GETPC_ADJ;

    /* Enforce guest required alignment.  */
    if (unlikely(a_bits > 0 && (addr & ((1 << a_bits) - 1)))) {
        /* ??? Maybe indicate atomic op to cpu_unaligned_access */
        cpu_unaligned_access(env_cpu(env), addr, MMU_DATA_STORE,
                             mmu_idx, retaddr);
    }

    /* Enforce qemu required alignment.  */
    if (unlikely(addr & ((1 << s_bits) - 1))) {
        /* We get here if guest alignment was not requested,
           or was not enforced by cpu_unaligned_access above.
           We might widen the access and emulate, but for now
           mark an exception and exit the cpu loop.  */
        goto stop_the_world;
    }

    /* Check TLB entry and enforce page permissions.  */
    if (!tlb_hit(tlb_addr, addr)) {
        if (!VICTIM_TLB_HIT(addr_write, addr)) {
            tlb_fill(env_cpu(env), addr, 1 << s_bits, MMU_DATA_STORE,
                     mmu_idx, retaddr);
            index = tlb_index(env, mmu_idx, addr);
            tlbe = tlb_entry(env, mmu_idx, addr);
        }
        tlb_addr = tlb_addr_write(tlbe) & ~TLB_INVALID_MASK;
    }

    /* Notice an IO access or a needs-MMU-lookup access */
    if (unlikely(tlb_addr & TLB_MMIO)) {
        /* There's really nothing that can be done to
           support this apart from stop-the-world.  */
        goto stop_the_world;
    }

    /* Let the guest notice RMW on a write-only page.  */
    if (unlikely(tlbe->addr_read != (tlb_addr & ~TLB_NOTDIRTY))) {
        tlb_fill(env_cpu(env), addr, 1 << s_bits, MMU_DATA_LOAD,
                 mmu_idx, retaddr);
        /* Since we don't support reads and writes to different addresses,
           and we do have the proper page loaded for write, this shouldn't
           ever return.  But just in case, handle via stop-the-world.  */
        goto stop_the_world;
    }

    hostaddr = (void *)((uintptr_t)addr + tlbe->addend);

    if (unlikely(tlb_addr & TLB_NOTDIRTY)) {
        notdirty_write(env_cpu(env), addr, 1 << s_bits,
                       &env_tlb(env)->d[mmu_idx].iotlb[index], retaddr);
    }

    return hostaddr;

 stop_the_world:
    cpu_loop_exit_atomic(env_cpu(env), retaddr);
}

/*
 * Load Helpers
 *
 * We support two different access types. SOFTMMU_CODE_ACCESS is
 * specifically for reading instructions from system memory. It is
 * called by the translation loop and in some helpers where the code
 * is disassembled. It shouldn't be called directly by guest code.
 */

typedef uint64_t FullLoadHelper(CPUArchState *env, target_ulong addr,
                                TCGMemOpIdx oi, uintptr_t retaddr);

static inline uint64_t QEMU_ALWAYS_INLINE
load_memop(const void *haddr, MemOp op)
{
    switch (op) {
    case MO_UB:
        return ldub_p(haddr);
    case MO_BEUW:
        return lduw_be_p(haddr);
    case MO_LEUW:
        return lduw_le_p(haddr);
    case MO_BEUL:
        return (uint32_t)ldl_be_p(haddr);
    case MO_LEUL:
        return (uint32_t)ldl_le_p(haddr);
    case MO_BEQ:
        return ldq_be_p(haddr);
    case MO_LEQ:
        return ldq_le_p(haddr);
    default:
        qemu_build_not_reached();
    }
}

static inline uint64_t QEMU_ALWAYS_INLINE
load_helper(CPUArchState *env, target_ulong addr, TCGMemOpIdx oi,
            uintptr_t retaddr, MemOp op, bool code_read,
            FullLoadHelper *full_load)
{
    uintptr_t mmu_idx = get_mmuidx(oi);
    uintptr_t index = tlb_index(env, mmu_idx, addr);
    CPUTLBEntry *entry = tlb_entry(env, mmu_idx, addr);
    target_ulong tlb_addr = code_read ? entry->addr_code : entry->addr_read;
    const size_t tlb_off = code_read ?
        offsetof(CPUTLBEntry, addr_code) : offsetof(CPUTLBEntry, addr_read);
    const MMUAccessType access_type =
        code_read ? MMU_INST_FETCH : MMU_DATA_LOAD;
    unsigned a_bits = get_alignment_bits(get_memop(oi));
    void *haddr;
    uint64_t res;
    size_t size = memop_size(op);

    /* Handle CPU specific unaligned behaviour */
    if (addr & ((1 << a_bits) - 1)) {
        cpu_unaligned_access(env_cpu(env), addr, access_type,
                             mmu_idx, retaddr);
    }

    /* If the TLB entry is for a different page, reload and try again.  */
    if (!tlb_hit(tlb_addr, addr)) {
        if (!victim_tlb_hit(env, mmu_idx, index, tlb_off,
                            addr & TARGET_PAGE_MASK)) {
            tlb_fill(env_cpu(env), addr, size,
                     access_type, mmu_idx, retaddr);
            index = tlb_index(env, mmu_idx, addr);
            entry = tlb_entry(env, mmu_idx, addr);
        }
        tlb_addr = code_read ? entry->addr_code : entry->addr_read;
        tlb_addr &= ~TLB_INVALID_MASK;
    }

    /* Handle anything that isn't just a straight memory access.  */
    if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
        CPUIOTLBEntry *iotlbentry;
        bool need_swap;

        /* For anything that is unaligned, recurse through full_load.  */
        if ((addr & (size - 1)) != 0) {
            goto do_unaligned_access;
        }

        iotlbentry = &env_tlb(env)->d[mmu_idx].iotlb[index];

        /* Handle watchpoints.  */
        if (unlikely(tlb_addr & TLB_WATCHPOINT)) {
            /* On watchpoint hit, this will longjmp out.  */
            cpu_check_watchpoint(env_cpu(env), addr, size,
                                 iotlbentry->attrs, BP_MEM_READ, retaddr);
        }

        need_swap = size > 1 && (tlb_addr & TLB_BSWAP);

        /* Handle I/O access.  */
        if (likely(tlb_addr & TLB_MMIO)) {
            return io_readx(env, iotlbentry, mmu_idx, addr, retaddr,
                            access_type, op ^ (need_swap * MO_BSWAP));
        }

        haddr = (void *)((uintptr_t)addr + entry->addend);

        /*
         * Keep these two load_memop separate to ensure that the compiler
         * is able to fold the entire function to a single instruction.
         * There is a build-time assert inside to remind you of this.  ;-)
         */
        if (unlikely(need_swap)) {
            return load_memop(haddr, op ^ MO_BSWAP);
        }
        return load_memop(haddr, op);
    }

    /* Handle slow unaligned access (it spans two pages or IO).  */
    if (size > 1
        && unlikely((addr & ~TARGET_PAGE_MASK) + size - 1
                    >= TARGET_PAGE_SIZE)) {
        target_ulong addr1, addr2;
        uint64_t r1, r2;
        unsigned shift;
    do_unaligned_access:
        addr1 = addr & ~((target_ulong)size - 1);
        addr2 = addr1 + size;
        r1 = full_load(env, addr1, oi, retaddr);
        r2 = full_load(env, addr2, oi, retaddr);
        shift = (addr & (size - 1)) * 8;

        if (memop_big_endian(op)) {
            /* Big-endian combine.  */
            res = (r1 << shift) | (r2 >> ((size * 8) - shift));
        } else {
            /* Little-endian combine.  */
            res = (r1 >> shift) | (r2 << ((size * 8) - shift));
        }
        return res & MAKE_64BIT_MASK(0, size * 8);
    }

    haddr = (void *)((uintptr_t)addr + entry->addend);
    return load_memop(haddr, op);
}

/*
 * For the benefit of TCG generated code, we want to avoid the
 * complication of ABI-specific return type promotion and always
 * return a value extended to the register size of the host. This is
 * tcg_target_long, except in the case of a 32-bit host and 64-bit
 * data, and for that we always have uint64_t.
 *
 * We don't bother with this widened value for SOFTMMU_CODE_ACCESS.
 */

static uint64_t full_ldub_mmu(CPUArchState *env, target_ulong addr,
                              TCGMemOpIdx oi, uintptr_t retaddr)
{
    return load_helper(env, addr, oi, retaddr, MO_UB, false, full_ldub_mmu);
}

tcg_target_ulong helper_ret_ldub_mmu(CPUArchState *env, target_ulong addr,
                                     TCGMemOpIdx oi, uintptr_t retaddr)
{
    return full_ldub_mmu(env, addr, oi, retaddr);
}

static uint64_t full_le_lduw_mmu(CPUArchState *env, target_ulong addr,
                                 TCGMemOpIdx oi, uintptr_t retaddr)
{
    return load_helper(env, addr, oi, retaddr, MO_LEUW, false,
                       full_le_lduw_mmu);
}

tcg_target_ulong helper_le_lduw_mmu(CPUArchState *env, target_ulong addr,
                                    TCGMemOpIdx oi, uintptr_t retaddr)
{
    return full_le_lduw_mmu(env, addr, oi, retaddr);
}

static uint64_t full_be_lduw_mmu(CPUArchState *env, target_ulong addr,
                                 TCGMemOpIdx oi, uintptr_t retaddr)
{
    return load_helper(env, addr, oi, retaddr, MO_BEUW, false,
                       full_be_lduw_mmu);
}

tcg_target_ulong helper_be_lduw_mmu(CPUArchState *env, target_ulong addr,
                                    TCGMemOpIdx oi, uintptr_t retaddr)
{
    return full_be_lduw_mmu(env, addr, oi, retaddr);
}

static uint64_t full_le_ldul_mmu(CPUArchState *env, target_ulong addr,
                                 TCGMemOpIdx oi, uintptr_t retaddr)
{
    return load_helper(env, addr, oi, retaddr, MO_LEUL, false,
                       full_le_ldul_mmu);
}

tcg_target_ulong helper_le_ldul_mmu(CPUArchState *env, target_ulong addr,
                                    TCGMemOpIdx oi, uintptr_t retaddr)
{
    return full_le_ldul_mmu(env, addr, oi, retaddr);
}

static uint64_t full_be_ldul_mmu(CPUArchState *env, target_ulong addr,
                                 TCGMemOpIdx oi, uintptr_t retaddr)
{
    return load_helper(env, addr, oi, retaddr, MO_BEUL, false,
                       full_be_ldul_mmu);
}

tcg_target_ulong helper_be_ldul_mmu(CPUArchState *env, target_ulong addr,
                                    TCGMemOpIdx oi, uintptr_t retaddr)
{
    return full_be_ldul_mmu(env, addr, oi, retaddr);
}

uint64_t helper_le_ldq_mmu(CPUArchState *env, target_ulong addr,
                           TCGMemOpIdx oi, uintptr_t retaddr)
{
    return load_helper(env, addr, oi, retaddr, MO_LEQ, false,
                       helper_le_ldq_mmu);
}

uint64_t helper_be_ldq_mmu(CPUArchState *env, target_ulong addr,
                           TCGMemOpIdx oi, uintptr_t retaddr)
{
    return load_helper(env, addr, oi, retaddr, MO_BEQ, false,
                       helper_be_ldq_mmu);
}

/*
 * Provide signed versions of the load routines as well.  We can of course
 * avoid this for 64-bit data, or for 32-bit data on 32-bit host.
 */


tcg_target_ulong helper_ret_ldsb_mmu(CPUArchState *env, target_ulong addr,
                                     TCGMemOpIdx oi, uintptr_t retaddr)
{
    return (int8_t)helper_ret_ldub_mmu(env, addr, oi, retaddr);
}

tcg_target_ulong helper_le_ldsw_mmu(CPUArchState *env, target_ulong addr,
                                    TCGMemOpIdx oi, uintptr_t retaddr)
{
    return (int16_t)helper_le_lduw_mmu(env, addr, oi, retaddr);
}

tcg_target_ulong helper_be_ldsw_mmu(CPUArchState *env, target_ulong addr,
                                    TCGMemOpIdx oi, uintptr_t retaddr)
{
    return (int16_t)helper_be_lduw_mmu(env, addr, oi, retaddr);
}

tcg_target_ulong helper_le_ldsl_mmu(CPUArchState *env, target_ulong addr,
                                    TCGMemOpIdx oi, uintptr_t retaddr)
{
    return (int32_t)helper_le_ldul_mmu(env, addr, oi, retaddr);
}

tcg_target_ulong helper_be_ldsl_mmu(CPUArchState *env, target_ulong addr,
                                    TCGMemOpIdx oi, uintptr_t retaddr)
{
    return (int32_t)helper_be_ldul_mmu(env, addr, oi, retaddr);
}

/*
 * Load helpers for cpu_ldst.h.
 */

static inline uint64_t cpu_load_helper(CPUArchState *env, abi_ptr addr,
                                       int mmu_idx, uintptr_t retaddr,
                                       MemOp op, FullLoadHelper *full_load)
{
    uint16_t meminfo;
    TCGMemOpIdx oi;
    uint64_t ret;

    meminfo = trace_mem_get_info(op, mmu_idx, false);
    trace_guest_mem_before_exec(env_cpu(env), addr, meminfo);

    op &= ~MO_SIGN;
    oi = make_memop_idx(op, mmu_idx);
    ret = full_load(env, addr, oi, retaddr);

    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, meminfo);

    return ret;
}

uint32_t cpu_ldub_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                            int mmu_idx, uintptr_t ra)
{
    return cpu_load_helper(env, addr, mmu_idx, ra, MO_UB, full_ldub_mmu);
}

int cpu_ldsb_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                       int mmu_idx, uintptr_t ra)
{
    return (int8_t)cpu_load_helper(env, addr, mmu_idx, ra, MO_SB,
                                   full_ldub_mmu);
}

uint32_t cpu_lduw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                               int mmu_idx, uintptr_t ra)
{
    return cpu_load_helper(env, addr, mmu_idx, ra, MO_BEUW, full_be_lduw_mmu);
}

int cpu_ldsw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                          int mmu_idx, uintptr_t ra)
{
    return (int16_t)cpu_load_helper(env, addr, mmu_idx, ra, MO_BESW,
                                    full_be_lduw_mmu);
}

uint32_t cpu_ldl_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra)
{
    return cpu_load_helper(env, addr, mmu_idx, ra, MO_BEUL, full_be_ldul_mmu);
}

uint64_t cpu_ldq_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra)
{
    return cpu_load_helper(env, addr, mmu_idx, ra, MO_BEQ, helper_be_ldq_mmu);
}

uint32_t cpu_lduw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                               int mmu_idx, uintptr_t ra)
{
    return cpu_load_helper(env, addr, mmu_idx, ra, MO_LEUW, full_le_lduw_mmu);
}

int cpu_ldsw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                          int mmu_idx, uintptr_t ra)
{
    return (int16_t)cpu_load_helper(env, addr, mmu_idx, ra, MO_LESW,
                                    full_le_lduw_mmu);
}

uint32_t cpu_ldl_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra)
{
    return cpu_load_helper(env, addr, mmu_idx, ra, MO_LEUL, full_le_ldul_mmu);
}

uint64_t cpu_ldq_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra)
{
    return cpu_load_helper(env, addr, mmu_idx, ra, MO_LEQ, helper_le_ldq_mmu);
}

uint32_t cpu_ldub_data_ra(CPUArchState *env, target_ulong ptr,
                          uintptr_t retaddr)
{
    return cpu_ldub_mmuidx_ra(env, ptr, cpu_mmu_index(env, false), retaddr);
}

int cpu_ldsb_data_ra(CPUArchState *env, target_ulong ptr, uintptr_t retaddr)
{
    return cpu_ldsb_mmuidx_ra(env, ptr, cpu_mmu_index(env, false), retaddr);
}

uint32_t cpu_lduw_be_data_ra(CPUArchState *env, target_ulong ptr,
                             uintptr_t retaddr)
{
    return cpu_lduw_be_mmuidx_ra(env, ptr, cpu_mmu_index(env, false), retaddr);
}

int cpu_ldsw_be_data_ra(CPUArchState *env, target_ulong ptr, uintptr_t retaddr)
{
    return cpu_ldsw_be_mmuidx_ra(env, ptr, cpu_mmu_index(env, false), retaddr);
}

uint32_t cpu_ldl_be_data_ra(CPUArchState *env, target_ulong ptr,
                            uintptr_t retaddr)
{
    return cpu_ldl_be_mmuidx_ra(env, ptr, cpu_mmu_index(env, false), retaddr);
}

uint64_t cpu_ldq_be_data_ra(CPUArchState *env, target_ulong ptr,
                            uintptr_t retaddr)
{
    return cpu_ldq_be_mmuidx_ra(env, ptr, cpu_mmu_index(env, false), retaddr);
}

uint32_t cpu_lduw_le_data_ra(CPUArchState *env, target_ulong ptr,
                             uintptr_t retaddr)
{
    return cpu_lduw_le_mmuidx_ra(env, ptr, cpu_mmu_index(env, false), retaddr);
}

int cpu_ldsw_le_data_ra(CPUArchState *env, target_ulong ptr, uintptr_t retaddr)
{
    return cpu_ldsw_le_mmuidx_ra(env, ptr, cpu_mmu_index(env, false), retaddr);
}

uint32_t cpu_ldl_le_data_ra(CPUArchState *env, target_ulong ptr,
                            uintptr_t retaddr)
{
    return cpu_ldl_le_mmuidx_ra(env, ptr, cpu_mmu_index(env, false), retaddr);
}

uint64_t cpu_ldq_le_data_ra(CPUArchState *env, target_ulong ptr,
                            uintptr_t retaddr)
{
    return cpu_ldq_le_mmuidx_ra(env, ptr, cpu_mmu_index(env, false), retaddr);
}

uint32_t cpu_ldub_data(CPUArchState *env, target_ulong ptr)
{
    return cpu_ldub_data_ra(env, ptr, 0);
}

int cpu_ldsb_data(CPUArchState *env, target_ulong ptr)
{
    return cpu_ldsb_data_ra(env, ptr, 0);
}

uint32_t cpu_lduw_be_data(CPUArchState *env, target_ulong ptr)
{
    return cpu_lduw_be_data_ra(env, ptr, 0);
}

int cpu_ldsw_be_data(CPUArchState *env, target_ulong ptr)
{
    return cpu_ldsw_be_data_ra(env, ptr, 0);
}

uint32_t cpu_ldl_be_data(CPUArchState *env, target_ulong ptr)
{
    return cpu_ldl_be_data_ra(env, ptr, 0);
}

uint64_t cpu_ldq_be_data(CPUArchState *env, target_ulong ptr)
{
    return cpu_ldq_be_data_ra(env, ptr, 0);
}

uint32_t cpu_lduw_le_data(CPUArchState *env, target_ulong ptr)
{
    return cpu_lduw_le_data_ra(env, ptr, 0);
}

int cpu_ldsw_le_data(CPUArchState *env, target_ulong ptr)
{
    return cpu_ldsw_le_data_ra(env, ptr, 0);
}

uint32_t cpu_ldl_le_data(CPUArchState *env, target_ulong ptr)
{
    return cpu_ldl_le_data_ra(env, ptr, 0);
}

uint64_t cpu_ldq_le_data(CPUArchState *env, target_ulong ptr)
{
    return cpu_ldq_le_data_ra(env, ptr, 0);
}

/*
 * Store Helpers
 */

static inline void QEMU_ALWAYS_INLINE
store_memop(void *haddr, uint64_t val, MemOp op)
{
    switch (op) {
    case MO_UB:
        stb_p(haddr, val);
        break;
    case MO_BEUW:
        stw_be_p(haddr, val);
        break;
    case MO_LEUW:
        stw_le_p(haddr, val);
        break;
    case MO_BEUL:
        stl_be_p(haddr, val);
        break;
    case MO_LEUL:
        stl_le_p(haddr, val);
        break;
    case MO_BEQ:
        stq_be_p(haddr, val);
        break;
    case MO_LEQ:
        stq_le_p(haddr, val);
        break;
    default:
        qemu_build_not_reached();
    }
}

static inline void QEMU_ALWAYS_INLINE
store_helper(CPUArchState *env, target_ulong addr, uint64_t val,
             TCGMemOpIdx oi, uintptr_t retaddr, MemOp op)
{
    uintptr_t mmu_idx = get_mmuidx(oi);
    uintptr_t index = tlb_index(env, mmu_idx, addr);
    CPUTLBEntry *entry = tlb_entry(env, mmu_idx, addr);
    target_ulong tlb_addr = tlb_addr_write(entry);
    const size_t tlb_off = offsetof(CPUTLBEntry, addr_write);
    unsigned a_bits = get_alignment_bits(get_memop(oi));
    void *haddr;
    size_t size = memop_size(op);

    /* Handle CPU specific unaligned behaviour */
    if (addr & ((1 << a_bits) - 1)) {
        cpu_unaligned_access(env_cpu(env), addr, MMU_DATA_STORE,
                             mmu_idx, retaddr);
    }

    /* If the TLB entry is for a different page, reload and try again.  */
    if (!tlb_hit(tlb_addr, addr)) {
        if (!victim_tlb_hit(env, mmu_idx, index, tlb_off,
            addr & TARGET_PAGE_MASK)) {
            tlb_fill(env_cpu(env), addr, size, MMU_DATA_STORE,
                     mmu_idx, retaddr);
            index = tlb_index(env, mmu_idx, addr);
            entry = tlb_entry(env, mmu_idx, addr);
        }
        tlb_addr = tlb_addr_write(entry) & ~TLB_INVALID_MASK;
    }

    /* Handle anything that isn't just a straight memory access.  */
    if (unlikely(tlb_addr & ~TARGET_PAGE_MASK)) {
        CPUIOTLBEntry *iotlbentry;
        bool need_swap;

        /* For anything that is unaligned, recurse through byte stores.  */
        if ((addr & (size - 1)) != 0) {
            goto do_unaligned_access;
        }

        iotlbentry = &env_tlb(env)->d[mmu_idx].iotlb[index];

        /* Handle watchpoints.  */
        if (unlikely(tlb_addr & TLB_WATCHPOINT)) {
            /* On watchpoint hit, this will longjmp out.  */
            cpu_check_watchpoint(env_cpu(env), addr, size,
                                 iotlbentry->attrs, BP_MEM_WRITE, retaddr);
        }

        need_swap = size > 1 && (tlb_addr & TLB_BSWAP);

        /* Handle I/O access.  */
        if (tlb_addr & TLB_MMIO) {
            io_writex(env, iotlbentry, mmu_idx, val, addr, retaddr,
                      op ^ (need_swap * MO_BSWAP));
            return;
        }

        /* Ignore writes to ROM.  */
        if (unlikely(tlb_addr & TLB_DISCARD_WRITE)) {
            return;
        }

        /* Handle clean RAM pages.  */
        if (tlb_addr & TLB_NOTDIRTY) {
            notdirty_write(env_cpu(env), addr, size, iotlbentry, retaddr);
        }

        haddr = (void *)((uintptr_t)addr + entry->addend);

        /*
         * Keep these two store_memop separate to ensure that the compiler
         * is able to fold the entire function to a single instruction.
         * There is a build-time assert inside to remind you of this.  ;-)
         */
        if (unlikely(need_swap)) {
            store_memop(haddr, val, op ^ MO_BSWAP);
        } else {
            store_memop(haddr, val, op);
        }
        return;
    }

    /* Handle slow unaligned access (it spans two pages or IO).  */
    if (size > 1
        && unlikely((addr & ~TARGET_PAGE_MASK) + size - 1
                     >= TARGET_PAGE_SIZE)) {
        int i;
        uintptr_t index2;
        CPUTLBEntry *entry2;
        target_ulong page2, tlb_addr2;
        size_t size2;

    do_unaligned_access:
        /*
         * Ensure the second page is in the TLB.  Note that the first page
         * is already guaranteed to be filled, and that the second page
         * cannot evict the first.
         */
        page2 = (addr + size) & TARGET_PAGE_MASK;
        size2 = (addr + size) & ~TARGET_PAGE_MASK;
        index2 = tlb_index(env, mmu_idx, page2);
        entry2 = tlb_entry(env, mmu_idx, page2);
        tlb_addr2 = tlb_addr_write(entry2);
        if (!tlb_hit_page(tlb_addr2, page2)) {
            if (!victim_tlb_hit(env, mmu_idx, index2, tlb_off, page2)) {
                tlb_fill(env_cpu(env), page2, size2, MMU_DATA_STORE,
                         mmu_idx, retaddr);
                index2 = tlb_index(env, mmu_idx, page2);
                entry2 = tlb_entry(env, mmu_idx, page2);
            }
            tlb_addr2 = tlb_addr_write(entry2);
        }

        /*
         * Handle watchpoints.  Since this may trap, all checks
         * must happen before any store.
         */
        if (unlikely(tlb_addr & TLB_WATCHPOINT)) {
            cpu_check_watchpoint(env_cpu(env), addr, size - size2,
                                 env_tlb(env)->d[mmu_idx].iotlb[index].attrs,
                                 BP_MEM_WRITE, retaddr);
        }
        if (unlikely(tlb_addr2 & TLB_WATCHPOINT)) {
            cpu_check_watchpoint(env_cpu(env), page2, size2,
                                 env_tlb(env)->d[mmu_idx].iotlb[index2].attrs,
                                 BP_MEM_WRITE, retaddr);
        }

        /*
         * XXX: not efficient, but simple.
         * This loop must go in the forward direction to avoid issues
         * with self-modifying code in Windows 64-bit.
         */
        for (i = 0; i < size; ++i) {
            uint8_t val8;
            if (memop_big_endian(op)) {
                /* Big-endian extract.  */
                val8 = val >> (((size - 1) * 8) - (i * 8));
            } else {
                /* Little-endian extract.  */
                val8 = val >> (i * 8);
            }
            helper_ret_stb_mmu(env, addr + i, val8, oi, retaddr);
        }
        return;
    }

    haddr = (void *)((uintptr_t)addr + entry->addend);
    store_memop(haddr, val, op);
}

void helper_ret_stb_mmu(CPUArchState *env, target_ulong addr, uint8_t val,
                        TCGMemOpIdx oi, uintptr_t retaddr)
{
    store_helper(env, addr, val, oi, retaddr, MO_UB);
}

void helper_le_stw_mmu(CPUArchState *env, target_ulong addr, uint16_t val,
                       TCGMemOpIdx oi, uintptr_t retaddr)
{
    store_helper(env, addr, val, oi, retaddr, MO_LEUW);
}

void helper_be_stw_mmu(CPUArchState *env, target_ulong addr, uint16_t val,
                       TCGMemOpIdx oi, uintptr_t retaddr)
{
    store_helper(env, addr, val, oi, retaddr, MO_BEUW);
}

void helper_le_stl_mmu(CPUArchState *env, target_ulong addr, uint32_t val,
                       TCGMemOpIdx oi, uintptr_t retaddr)
{
    store_helper(env, addr, val, oi, retaddr, MO_LEUL);
}

void helper_be_stl_mmu(CPUArchState *env, target_ulong addr, uint32_t val,
                       TCGMemOpIdx oi, uintptr_t retaddr)
{
    store_helper(env, addr, val, oi, retaddr, MO_BEUL);
}

void helper_le_stq_mmu(CPUArchState *env, target_ulong addr, uint64_t val,
                       TCGMemOpIdx oi, uintptr_t retaddr)
{
    store_helper(env, addr, val, oi, retaddr, MO_LEQ);
}

void helper_be_stq_mmu(CPUArchState *env, target_ulong addr, uint64_t val,
                       TCGMemOpIdx oi, uintptr_t retaddr)
{
    store_helper(env, addr, val, oi, retaddr, MO_BEQ);
}

/*
 * Store Helpers for cpu_ldst.h
 */

static inline void QEMU_ALWAYS_INLINE
cpu_store_helper(CPUArchState *env, target_ulong addr, uint64_t val,
                 int mmu_idx, uintptr_t retaddr, MemOp op)
{
    TCGMemOpIdx oi;
    uint16_t meminfo;

    meminfo = trace_mem_get_info(op, mmu_idx, true);
    trace_guest_mem_before_exec(env_cpu(env), addr, meminfo);

    oi = make_memop_idx(op, mmu_idx);
    store_helper(env, addr, val, oi, retaddr, op);

    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, meminfo);
}

void cpu_stb_mmuidx_ra(CPUArchState *env, target_ulong addr, uint32_t val,
                       int mmu_idx, uintptr_t retaddr)
{
    cpu_store_helper(env, addr, val, mmu_idx, retaddr, MO_UB);
}

void cpu_stw_be_mmuidx_ra(CPUArchState *env, target_ulong addr, uint32_t val,
                          int mmu_idx, uintptr_t retaddr)
{
    cpu_store_helper(env, addr, val, mmu_idx, retaddr, MO_BEUW);
}

void cpu_stl_be_mmuidx_ra(CPUArchState *env, target_ulong addr, uint32_t val,
                          int mmu_idx, uintptr_t retaddr)
{
    cpu_store_helper(env, addr, val, mmu_idx, retaddr, MO_BEUL);
}

void cpu_stq_be_mmuidx_ra(CPUArchState *env, target_ulong addr, uint64_t val,
                          int mmu_idx, uintptr_t retaddr)
{
    cpu_store_helper(env, addr, val, mmu_idx, retaddr, MO_BEQ);
}

void cpu_stw_le_mmuidx_ra(CPUArchState *env, target_ulong addr, uint32_t val,
                          int mmu_idx, uintptr_t retaddr)
{
    cpu_store_helper(env, addr, val, mmu_idx, retaddr, MO_LEUW);
}

void cpu_stl_le_mmuidx_ra(CPUArchState *env, target_ulong addr, uint32_t val,
                          int mmu_idx, uintptr_t retaddr)
{
    cpu_store_helper(env, addr, val, mmu_idx, retaddr, MO_LEUL);
}

void cpu_stq_le_mmuidx_ra(CPUArchState *env, target_ulong addr, uint64_t val,
                          int mmu_idx, uintptr_t retaddr)
{
    cpu_store_helper(env, addr, val, mmu_idx, retaddr, MO_LEQ);
}

void cpu_stb_data_ra(CPUArchState *env, target_ulong ptr,
                     uint32_t val, uintptr_t retaddr)
{
    cpu_stb_mmuidx_ra(env, ptr, val, cpu_mmu_index(env, false), retaddr);
}

void cpu_stw_be_data_ra(CPUArchState *env, target_ulong ptr,
                        uint32_t val, uintptr_t retaddr)
{
    cpu_stw_be_mmuidx_ra(env, ptr, val, cpu_mmu_index(env, false), retaddr);
}

void cpu_stl_be_data_ra(CPUArchState *env, target_ulong ptr,
                        uint32_t val, uintptr_t retaddr)
{
    cpu_stl_be_mmuidx_ra(env, ptr, val, cpu_mmu_index(env, false), retaddr);
}

void cpu_stq_be_data_ra(CPUArchState *env, target_ulong ptr,
                        uint64_t val, uintptr_t retaddr)
{
    cpu_stq_be_mmuidx_ra(env, ptr, val, cpu_mmu_index(env, false), retaddr);
}

void cpu_stw_le_data_ra(CPUArchState *env, target_ulong ptr,
                        uint32_t val, uintptr_t retaddr)
{
    cpu_stw_le_mmuidx_ra(env, ptr, val, cpu_mmu_index(env, false), retaddr);
}

void cpu_stl_le_data_ra(CPUArchState *env, target_ulong ptr,
                        uint32_t val, uintptr_t retaddr)
{
    cpu_stl_le_mmuidx_ra(env, ptr, val, cpu_mmu_index(env, false), retaddr);
}

void cpu_stq_le_data_ra(CPUArchState *env, target_ulong ptr,
                        uint64_t val, uintptr_t retaddr)
{
    cpu_stq_le_mmuidx_ra(env, ptr, val, cpu_mmu_index(env, false), retaddr);
}

void cpu_stb_data(CPUArchState *env, target_ulong ptr, uint32_t val)
{
    cpu_stb_data_ra(env, ptr, val, 0);
}

void cpu_stw_be_data(CPUArchState *env, target_ulong ptr, uint32_t val)
{
    cpu_stw_be_data_ra(env, ptr, val, 0);
}

void cpu_stl_be_data(CPUArchState *env, target_ulong ptr, uint32_t val)
{
    cpu_stl_be_data_ra(env, ptr, val, 0);
}

void cpu_stq_be_data(CPUArchState *env, target_ulong ptr, uint64_t val)
{
    cpu_stq_be_data_ra(env, ptr, val, 0);
}

void cpu_stw_le_data(CPUArchState *env, target_ulong ptr, uint32_t val)
{
    cpu_stw_le_data_ra(env, ptr, val, 0);
}

void cpu_stl_le_data(CPUArchState *env, target_ulong ptr, uint32_t val)
{
    cpu_stl_le_data_ra(env, ptr, val, 0);
}

void cpu_stq_le_data(CPUArchState *env, target_ulong ptr, uint64_t val)
{
    cpu_stq_le_data_ra(env, ptr, val, 0);
}

/* First set of helpers allows passing in of OI and RETADDR.  This makes
   them callable from other helpers.  */

#define EXTRA_ARGS     , TCGMemOpIdx oi, uintptr_t retaddr
#define ATOMIC_NAME(X) \
    HELPER(glue(glue(glue(atomic_ ## X, SUFFIX), END), _mmu))
#define ATOMIC_MMU_DECLS
#define ATOMIC_MMU_LOOKUP atomic_mmu_lookup(env, addr, oi, retaddr)
#define ATOMIC_MMU_CLEANUP
#define ATOMIC_MMU_IDX   get_mmuidx(oi)

#include "atomic_common.inc.c"

#define DATA_SIZE 1
#include "atomic_template.h"

#define DATA_SIZE 2
#include "atomic_template.h"

#define DATA_SIZE 4
#include "atomic_template.h"

#ifdef CONFIG_ATOMIC64
#define DATA_SIZE 8
#include "atomic_template.h"
#endif

#if HAVE_CMPXCHG128 || HAVE_ATOMIC128
#define DATA_SIZE 16
#include "atomic_template.h"
#endif

/* Second set of helpers are directly callable from TCG as helpers.  */

#undef EXTRA_ARGS
#undef ATOMIC_NAME
#undef ATOMIC_MMU_LOOKUP
#define EXTRA_ARGS         , TCGMemOpIdx oi
#define ATOMIC_NAME(X)     HELPER(glue(glue(atomic_ ## X, SUFFIX), END))
#define ATOMIC_MMU_LOOKUP  atomic_mmu_lookup(env, addr, oi, GETPC())

#define DATA_SIZE 1
#include "atomic_template.h"

#define DATA_SIZE 2
#include "atomic_template.h"

#define DATA_SIZE 4
#include "atomic_template.h"

#ifdef CONFIG_ATOMIC64
#define DATA_SIZE 8
#include "atomic_template.h"
#endif
#undef ATOMIC_MMU_IDX

/* Code access functions.  */

static uint64_t full_ldub_code(CPUArchState *env, target_ulong addr,
                               TCGMemOpIdx oi, uintptr_t retaddr)
{
    return load_helper(env, addr, oi, retaddr, MO_8, true, full_ldub_code);
}

uint32_t cpu_ldub_code(CPUArchState *env, abi_ptr addr)
{
    TCGMemOpIdx oi = make_memop_idx(MO_UB, cpu_mmu_index(env, true));
    return full_ldub_code(env, addr, oi, 0);
}

static uint64_t full_lduw_code(CPUArchState *env, target_ulong addr,
                               TCGMemOpIdx oi, uintptr_t retaddr)
{
    return load_helper(env, addr, oi, retaddr, MO_TEUW, true, full_lduw_code);
}

uint32_t cpu_lduw_code(CPUArchState *env, abi_ptr addr)
{
    TCGMemOpIdx oi = make_memop_idx(MO_TEUW, cpu_mmu_index(env, true));
    return full_lduw_code(env, addr, oi, 0);
}

static uint64_t full_ldl_code(CPUArchState *env, target_ulong addr,
                              TCGMemOpIdx oi, uintptr_t retaddr)
{
    return load_helper(env, addr, oi, retaddr, MO_TEUL, true, full_ldl_code);
}

uint32_t cpu_ldl_code(CPUArchState *env, abi_ptr addr)
{
    TCGMemOpIdx oi = make_memop_idx(MO_TEUL, cpu_mmu_index(env, true));
    return full_ldl_code(env, addr, oi, 0);
}

static uint64_t full_ldq_code(CPUArchState *env, target_ulong addr,
                              TCGMemOpIdx oi, uintptr_t retaddr)
{
    return load_helper(env, addr, oi, retaddr, MO_TEQ, true, full_ldq_code);
}

uint64_t cpu_ldq_code(CPUArchState *env, abi_ptr addr)
{
    TCGMemOpIdx oi = make_memop_idx(MO_TEQ, cpu_mmu_index(env, true));
    return full_ldq_code(env, addr, oi, 0);
}
