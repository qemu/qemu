/*
 * ARM v8.5-MemTag Operations
 *
 * Copyright (c) 2020 Linaro, Ltd.
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
#include "qemu/log.h"
#include "cpu.h"
#include "internals.h"
#include "exec/exec-all.h"
#include "exec/ram_addr.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "hw/core/tcg-cpu-ops.h"
#include "qapi/error.h"
#include "qemu/guest-random.h"


static int choose_nonexcluded_tag(int tag, int offset, uint16_t exclude)
{
    if (exclude == 0xffff) {
        return 0;
    }
    if (offset == 0) {
        while (exclude & (1 << tag)) {
            tag = (tag + 1) & 15;
        }
    } else {
        do {
            do {
                tag = (tag + 1) & 15;
            } while (exclude & (1 << tag));
        } while (--offset > 0);
    }
    return tag;
}

/**
 * allocation_tag_mem_probe:
 * @env: the cpu environment
 * @ptr_mmu_idx: the addressing regime to use for the virtual address
 * @ptr: the virtual address for which to look up tag memory
 * @ptr_access: the access to use for the virtual address
 * @ptr_size: the number of bytes in the normal memory access
 * @tag_access: the access to use for the tag memory
 * @probe: true to merely probe, never taking an exception
 * @ra: the return address for exception handling
 *
 * Our tag memory is formatted as a sequence of little-endian nibbles.
 * That is, the byte at (addr >> (LOG2_TAG_GRANULE + 1)) contains two
 * tags, with the tag at [3:0] for the lower addr and the tag at [7:4]
 * for the higher addr.
 *
 * Here, resolve the physical address from the virtual address, and return
 * a pointer to the corresponding tag byte.
 *
 * If there is no tag storage corresponding to @ptr, return NULL.
 *
 * If the page is inaccessible for @ptr_access, or has a watchpoint, there are
 * three options:
 * (1) probe = true, ra = 0 : pure probe -- we return NULL if the page is not
 *     accessible, and do not take watchpoint traps. The calling code must
 *     handle those cases in the right priority compared to MTE traps.
 * (2) probe = false, ra = 0 : probe, no fault expected -- the caller guarantees
 *     that the page is going to be accessible. We will take watchpoint traps.
 * (3) probe = false, ra != 0 : non-probe -- we will take both memory access
 *     traps and watchpoint traps.
 * (probe = true, ra != 0 is invalid and will assert.)
 */
static uint8_t *allocation_tag_mem_probe(CPUARMState *env, int ptr_mmu_idx,
                                         uint64_t ptr, MMUAccessType ptr_access,
                                         int ptr_size, MMUAccessType tag_access,
                                         bool probe, uintptr_t ra)
{
#ifdef CONFIG_USER_ONLY
    uint64_t clean_ptr = useronly_clean_ptr(ptr);
    int flags = page_get_flags(clean_ptr);
    uint8_t *tags;
    uintptr_t index;

    assert(!(probe && ra));

    if (!(flags & (ptr_access == MMU_DATA_STORE ? PAGE_WRITE_ORG : PAGE_READ))) {
        cpu_loop_exit_sigsegv(env_cpu(env), ptr, ptr_access,
                              !(flags & PAGE_VALID), ra);
    }

    /* Require both MAP_ANON and PROT_MTE for the page. */
    if (!(flags & PAGE_ANON) || !(flags & PAGE_MTE)) {
        return NULL;
    }

    tags = page_get_target_data(clean_ptr);

    index = extract32(ptr, LOG2_TAG_GRANULE + 1,
                      TARGET_PAGE_BITS - LOG2_TAG_GRANULE - 1);
    return tags + index;
#else
    CPUTLBEntryFull *full;
    MemTxAttrs attrs;
    int in_page, flags;
    hwaddr ptr_paddr, tag_paddr, xlat;
    MemoryRegion *mr;
    ARMASIdx tag_asi;
    AddressSpace *tag_as;
    void *host;

    /*
     * Probe the first byte of the virtual address.  This raises an
     * exception for inaccessible pages, and resolves the virtual address
     * into the softmmu tlb.
     *
     * When RA == 0, this is either a pure probe or a no-fault-expected probe.
     * Indicate to probe_access_flags no-fault, then either return NULL
     * for the pure probe, or assert that we received a valid page for the
     * no-fault-expected probe.
     */
    flags = probe_access_full(env, ptr, 0, ptr_access, ptr_mmu_idx,
                              ra == 0, &host, &full, ra);
    if (probe && (flags & TLB_INVALID_MASK)) {
        return NULL;
    }
    assert(!(flags & TLB_INVALID_MASK));

    /* If the virtual page MemAttr != Tagged, access unchecked. */
    if (full->extra.arm.pte_attrs != 0xf0) {
        return NULL;
    }

    /*
     * If not backed by host ram, there is no tag storage: access unchecked.
     * This is probably a guest os bug though, so log it.
     */
    if (unlikely(flags & TLB_MMIO)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Page @ 0x%" PRIx64 " indicates Tagged Normal memory "
                      "but is not backed by host ram\n", ptr);
        return NULL;
    }

    /*
     * Remember these values across the second lookup below,
     * which may invalidate this pointer via tlb resize.
     */
    ptr_paddr = full->phys_addr | (ptr & ~TARGET_PAGE_MASK);
    attrs = full->attrs;
    full = NULL;

    /*
     * The Normal memory access can extend to the next page.  E.g. a single
     * 8-byte access to the last byte of a page will check only the last
     * tag on the first page.
     * Any page access exception has priority over tag check exception.
     */
    in_page = -(ptr | TARGET_PAGE_MASK);
    if (unlikely(ptr_size > in_page)) {
        flags |= probe_access_full(env, ptr + in_page, 0, ptr_access,
                                   ptr_mmu_idx, ra == 0, &host, &full, ra);
        assert(!(flags & TLB_INVALID_MASK));
    }

    /* Any debug exception has priority over a tag check exception. */
    if (!probe && unlikely(flags & TLB_WATCHPOINT)) {
        int wp = ptr_access == MMU_DATA_LOAD ? BP_MEM_READ : BP_MEM_WRITE;
        assert(ra != 0);
        cpu_check_watchpoint(env_cpu(env), ptr, ptr_size, attrs, wp, ra);
    }

    /* Convert to the physical address in tag space.  */
    tag_paddr = ptr_paddr >> (LOG2_TAG_GRANULE + 1);

    /* Look up the address in tag space. */
    tag_asi = attrs.secure ? ARMASIdx_TagS : ARMASIdx_TagNS;
    tag_as = cpu_get_address_space(env_cpu(env), tag_asi);
    mr = address_space_translate(tag_as, tag_paddr, &xlat, NULL,
                                 tag_access == MMU_DATA_STORE, attrs);

    /*
     * Note that @mr will never be NULL.  If there is nothing in the address
     * space at @tag_paddr, the translation will return the unallocated memory
     * region.  For our purposes, the result must be ram.
     */
    if (unlikely(!memory_region_is_ram(mr))) {
        /* ??? Failure is a board configuration error. */
        qemu_log_mask(LOG_UNIMP,
                      "Tag Memory @ 0x%" HWADDR_PRIx " not found for "
                      "Normal Memory @ 0x%" HWADDR_PRIx "\n",
                      tag_paddr, ptr_paddr);
        return NULL;
    }

    /*
     * Ensure the tag memory is dirty on write, for migration.
     * Tag memory can never contain code or display memory (vga).
     */
    if (tag_access == MMU_DATA_STORE) {
        ram_addr_t tag_ra = memory_region_get_ram_addr(mr) + xlat;
        cpu_physical_memory_set_dirty_flag(tag_ra, DIRTY_MEMORY_MIGRATION);
    }

    return memory_region_get_ram_ptr(mr) + xlat;
#endif
}

static uint8_t *allocation_tag_mem(CPUARMState *env, int ptr_mmu_idx,
                                   uint64_t ptr, MMUAccessType ptr_access,
                                   int ptr_size, MMUAccessType tag_access,
                                   uintptr_t ra)
{
    return allocation_tag_mem_probe(env, ptr_mmu_idx, ptr, ptr_access,
                                    ptr_size, tag_access, false, ra);
}

uint64_t HELPER(irg)(CPUARMState *env, uint64_t rn, uint64_t rm)
{
    uint16_t exclude = extract32(rm | env->cp15.gcr_el1, 0, 16);
    int rrnd = extract32(env->cp15.gcr_el1, 16, 1);
    int start = extract32(env->cp15.rgsr_el1, 0, 4);
    int seed = extract32(env->cp15.rgsr_el1, 8, 16);
    int offset, i, rtag;

    /*
     * Our IMPDEF choice for GCR_EL1.RRND==1 is to continue to use the
     * deterministic algorithm.  Except that with RRND==1 the kernel is
     * not required to have set RGSR_EL1.SEED != 0, which is required for
     * the deterministic algorithm to function.  So we force a non-zero
     * SEED for that case.
     */
    if (unlikely(seed == 0) && rrnd) {
        do {
            Error *err = NULL;
            uint16_t two;

            if (qemu_guest_getrandom(&two, sizeof(two), &err) < 0) {
                /*
                 * Failed, for unknown reasons in the crypto subsystem.
                 * Best we can do is log the reason and use a constant seed.
                 */
                qemu_log_mask(LOG_UNIMP, "IRG: Crypto failure: %s\n",
                              error_get_pretty(err));
                error_free(err);
                two = 1;
            }
            seed = two;
        } while (seed == 0);
    }

    /* RandomTag */
    for (i = offset = 0; i < 4; ++i) {
        /* NextRandomTagBit */
        int top = (extract32(seed, 5, 1) ^ extract32(seed, 3, 1) ^
                   extract32(seed, 2, 1) ^ extract32(seed, 0, 1));
        seed = (top << 15) | (seed >> 1);
        offset |= top << i;
    }
    rtag = choose_nonexcluded_tag(start, offset, exclude);
    env->cp15.rgsr_el1 = rtag | (seed << 8);

    return address_with_allocation_tag(rn, rtag);
}

uint64_t HELPER(addsubg)(CPUARMState *env, uint64_t ptr,
                         int32_t offset, uint32_t tag_offset)
{
    int start_tag = allocation_tag_from_addr(ptr);
    uint16_t exclude = extract32(env->cp15.gcr_el1, 0, 16);
    int rtag = choose_nonexcluded_tag(start_tag, tag_offset, exclude);

    return address_with_allocation_tag(ptr + offset, rtag);
}

static int load_tag1(uint64_t ptr, uint8_t *mem)
{
    int ofs = extract32(ptr, LOG2_TAG_GRANULE, 1) * 4;
    return extract32(*mem, ofs, 4);
}

uint64_t HELPER(ldg)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    int mmu_idx = arm_env_mmu_index(env);
    uint8_t *mem;
    int rtag = 0;

    /* Trap if accessing an invalid page.  */
    mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_LOAD, 1,
                             MMU_DATA_LOAD, GETPC());

    /* Load if page supports tags. */
    if (mem) {
        rtag = load_tag1(ptr, mem);
    }

    return address_with_allocation_tag(xt, rtag);
}

static void check_tag_aligned(CPUARMState *env, uint64_t ptr, uintptr_t ra)
{
    if (unlikely(!QEMU_IS_ALIGNED(ptr, TAG_GRANULE))) {
        arm_cpu_do_unaligned_access(env_cpu(env), ptr, MMU_DATA_STORE,
                                    arm_env_mmu_index(env), ra);
        g_assert_not_reached();
    }
}

/* For use in a non-parallel context, store to the given nibble.  */
static void store_tag1(uint64_t ptr, uint8_t *mem, int tag)
{
    int ofs = extract32(ptr, LOG2_TAG_GRANULE, 1) * 4;
    *mem = deposit32(*mem, ofs, 4, tag);
}

/* For use in a parallel context, atomically store to the given nibble.  */
static void store_tag1_parallel(uint64_t ptr, uint8_t *mem, int tag)
{
    int ofs = extract32(ptr, LOG2_TAG_GRANULE, 1) * 4;
    uint8_t old = qatomic_read(mem);

    while (1) {
        uint8_t new = deposit32(old, ofs, 4, tag);
        uint8_t cmp = qatomic_cmpxchg(mem, old, new);
        if (likely(cmp == old)) {
            return;
        }
        old = cmp;
    }
}

typedef void stg_store1(uint64_t, uint8_t *, int);

static inline void do_stg(CPUARMState *env, uint64_t ptr, uint64_t xt,
                          uintptr_t ra, stg_store1 store1)
{
    int mmu_idx = arm_env_mmu_index(env);
    uint8_t *mem;

    check_tag_aligned(env, ptr, ra);

    /* Trap if accessing an invalid page.  */
    mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_STORE, TAG_GRANULE,
                             MMU_DATA_STORE, ra);

    /* Store if page supports tags. */
    if (mem) {
        store1(ptr, mem, allocation_tag_from_addr(xt));
    }
}

void HELPER(stg)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    do_stg(env, ptr, xt, GETPC(), store_tag1);
}

void HELPER(stg_parallel)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    do_stg(env, ptr, xt, GETPC(), store_tag1_parallel);
}

void HELPER(stg_stub)(CPUARMState *env, uint64_t ptr)
{
    int mmu_idx = arm_env_mmu_index(env);
    uintptr_t ra = GETPC();

    check_tag_aligned(env, ptr, ra);
    probe_write(env, ptr, TAG_GRANULE, mmu_idx, ra);
}

static inline void do_st2g(CPUARMState *env, uint64_t ptr, uint64_t xt,
                           uintptr_t ra, stg_store1 store1)
{
    int mmu_idx = arm_env_mmu_index(env);
    int tag = allocation_tag_from_addr(xt);
    uint8_t *mem1, *mem2;

    check_tag_aligned(env, ptr, ra);

    /*
     * Trap if accessing an invalid page(s).
     * This takes priority over !allocation_tag_access_enabled.
     */
    if (ptr & TAG_GRANULE) {
        /* Two stores unaligned mod TAG_GRANULE*2 -- modify two bytes. */
        mem1 = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_STORE,
                                  TAG_GRANULE, MMU_DATA_STORE, ra);
        mem2 = allocation_tag_mem(env, mmu_idx, ptr + TAG_GRANULE,
                                  MMU_DATA_STORE, TAG_GRANULE,
                                  MMU_DATA_STORE, ra);

        /* Store if page(s) support tags. */
        if (mem1) {
            store1(TAG_GRANULE, mem1, tag);
        }
        if (mem2) {
            store1(0, mem2, tag);
        }
    } else {
        /* Two stores aligned mod TAG_GRANULE*2 -- modify one byte. */
        mem1 = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_STORE,
                                  2 * TAG_GRANULE, MMU_DATA_STORE, ra);
        if (mem1) {
            tag |= tag << 4;
            qatomic_set(mem1, tag);
        }
    }
}

void HELPER(st2g)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    do_st2g(env, ptr, xt, GETPC(), store_tag1);
}

void HELPER(st2g_parallel)(CPUARMState *env, uint64_t ptr, uint64_t xt)
{
    do_st2g(env, ptr, xt, GETPC(), store_tag1_parallel);
}

void HELPER(st2g_stub)(CPUARMState *env, uint64_t ptr)
{
    int mmu_idx = arm_env_mmu_index(env);
    uintptr_t ra = GETPC();
    int in_page = -(ptr | TARGET_PAGE_MASK);

    check_tag_aligned(env, ptr, ra);

    if (likely(in_page >= 2 * TAG_GRANULE)) {
        probe_write(env, ptr, 2 * TAG_GRANULE, mmu_idx, ra);
    } else {
        probe_write(env, ptr, TAG_GRANULE, mmu_idx, ra);
        probe_write(env, ptr + TAG_GRANULE, TAG_GRANULE, mmu_idx, ra);
    }
}

uint64_t HELPER(ldgm)(CPUARMState *env, uint64_t ptr)
{
    int mmu_idx = arm_env_mmu_index(env);
    uintptr_t ra = GETPC();
    int gm_bs = env_archcpu(env)->gm_blocksize;
    int gm_bs_bytes = 4 << gm_bs;
    void *tag_mem;
    uint64_t ret;
    int shift;

    ptr = QEMU_ALIGN_DOWN(ptr, gm_bs_bytes);

    /* Trap if accessing an invalid page.  */
    tag_mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_LOAD,
                                 gm_bs_bytes, MMU_DATA_LOAD, ra);

    /* The tag is squashed to zero if the page does not support tags.  */
    if (!tag_mem) {
        return 0;
    }

    /*
     * The ordering of elements within the word corresponds to
     * a little-endian operation.  Computation of shift comes from
     *
     *     index = address<LOG2_TAG_GRANULE+3:LOG2_TAG_GRANULE>
     *     data<index*4+3:index*4> = tag
     *
     * Because of the alignment of ptr above, BS=6 has shift=0.
     * All memory operations are aligned.  Defer support for BS=2,
     * requiring insertion or extraction of a nibble, until we
     * support a cpu that requires it.
     */
    switch (gm_bs) {
    case 3:
        /* 32 bytes -> 2 tags -> 8 result bits */
        ret = *(uint8_t *)tag_mem;
        break;
    case 4:
        /* 64 bytes -> 4 tags -> 16 result bits */
        ret = cpu_to_le16(*(uint16_t *)tag_mem);
        break;
    case 5:
        /* 128 bytes -> 8 tags -> 32 result bits */
        ret = cpu_to_le32(*(uint32_t *)tag_mem);
        break;
    case 6:
        /* 256 bytes -> 16 tags -> 64 result bits */
        return cpu_to_le64(*(uint64_t *)tag_mem);
    default:
        /*
         * CPU configured with unsupported/invalid gm blocksize.
         * This is detected early in arm_cpu_realizefn.
         */
        g_assert_not_reached();
    }
    shift = extract64(ptr, LOG2_TAG_GRANULE, 4) * 4;
    return ret << shift;
}

void HELPER(stgm)(CPUARMState *env, uint64_t ptr, uint64_t val)
{
    int mmu_idx = arm_env_mmu_index(env);
    uintptr_t ra = GETPC();
    int gm_bs = env_archcpu(env)->gm_blocksize;
    int gm_bs_bytes = 4 << gm_bs;
    void *tag_mem;
    int shift;

    ptr = QEMU_ALIGN_DOWN(ptr, gm_bs_bytes);

    /* Trap if accessing an invalid page.  */
    tag_mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_STORE,
                                 gm_bs_bytes, MMU_DATA_LOAD, ra);

    /*
     * Tag store only happens if the page support tags,
     * and if the OS has enabled access to the tags.
     */
    if (!tag_mem) {
        return;
    }

    /* See LDGM for comments on BS and on shift.  */
    shift = extract64(ptr, LOG2_TAG_GRANULE, 4) * 4;
    val >>= shift;
    switch (gm_bs) {
    case 3:
        /* 32 bytes -> 2 tags -> 8 result bits */
        *(uint8_t *)tag_mem = val;
        break;
    case 4:
        /* 64 bytes -> 4 tags -> 16 result bits */
        *(uint16_t *)tag_mem = cpu_to_le16(val);
        break;
    case 5:
        /* 128 bytes -> 8 tags -> 32 result bits */
        *(uint32_t *)tag_mem = cpu_to_le32(val);
        break;
    case 6:
        /* 256 bytes -> 16 tags -> 64 result bits */
        *(uint64_t *)tag_mem = cpu_to_le64(val);
        break;
    default:
        /* cpu configured with unsupported gm blocksize. */
        g_assert_not_reached();
    }
}

void HELPER(stzgm_tags)(CPUARMState *env, uint64_t ptr, uint64_t val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = arm_env_mmu_index(env);
    int log2_dcz_bytes, log2_tag_bytes;
    intptr_t dcz_bytes, tag_bytes;
    uint8_t *mem;

    /*
     * In arm_cpu_realizefn, we assert that dcz > LOG2_TAG_GRANULE+1,
     * i.e. 32 bytes, which is an unreasonably small dcz anyway,
     * to make sure that we can access one complete tag byte here.
     */
    log2_dcz_bytes = env_archcpu(env)->dcz_blocksize + 2;
    log2_tag_bytes = log2_dcz_bytes - (LOG2_TAG_GRANULE + 1);
    dcz_bytes = (intptr_t)1 << log2_dcz_bytes;
    tag_bytes = (intptr_t)1 << log2_tag_bytes;
    ptr &= -dcz_bytes;

    mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_STORE, dcz_bytes,
                             MMU_DATA_STORE, ra);
    if (mem) {
        int tag_pair = (val & 0xf) * 0x11;
        memset(mem, tag_pair, tag_bytes);
    }
}

static void mte_sync_check_fail(CPUARMState *env, uint32_t desc,
                                uint64_t dirty_ptr, uintptr_t ra)
{
    int is_write, syn;

    env->exception.vaddress = dirty_ptr;

    is_write = FIELD_EX32(desc, MTEDESC, WRITE);
    syn = syn_data_abort_no_iss(arm_current_el(env) != 0, 0, 0, 0, 0, is_write,
                                0x11);
    raise_exception_ra(env, EXCP_DATA_ABORT, syn, exception_target_el(env), ra);
    g_assert_not_reached();
}

static void mte_async_check_fail(CPUARMState *env, uint64_t dirty_ptr,
                                 uintptr_t ra, ARMMMUIdx arm_mmu_idx, int el)
{
    int select;

    if (regime_has_2_ranges(arm_mmu_idx)) {
        select = extract64(dirty_ptr, 55, 1);
    } else {
        select = 0;
    }
    env->cp15.tfsr_el[el] |= 1 << select;
#ifdef CONFIG_USER_ONLY
    /*
     * Stand in for a timer irq, setting _TIF_MTE_ASYNC_FAULT,
     * which then sends a SIGSEGV when the thread is next scheduled.
     * This cpu will return to the main loop at the end of the TB,
     * which is rather sooner than "normal".  But the alternative
     * is waiting until the next syscall.
     */
    qemu_cpu_kick(env_cpu(env));
#endif
}

/* Record a tag check failure.  */
void mte_check_fail(CPUARMState *env, uint32_t desc,
                    uint64_t dirty_ptr, uintptr_t ra)
{
    int mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    ARMMMUIdx arm_mmu_idx = core_to_aa64_mmu_idx(mmu_idx);
    int el, reg_el, tcf;
    uint64_t sctlr;

    reg_el = regime_el(env, arm_mmu_idx);
    sctlr = env->cp15.sctlr_el[reg_el];

    switch (arm_mmu_idx) {
    case ARMMMUIdx_E10_0:
    case ARMMMUIdx_E20_0:
        el = 0;
        tcf = extract64(sctlr, 38, 2);
        break;
    default:
        el = reg_el;
        tcf = extract64(sctlr, 40, 2);
    }

    switch (tcf) {
    case 1:
        /* Tag check fail causes a synchronous exception. */
        mte_sync_check_fail(env, desc, dirty_ptr, ra);
        break;

    case 0:
        /*
         * Tag check fail does not affect the PE.
         * We eliminate this case by not setting MTE_ACTIVE
         * in tb_flags, so that we never make this runtime call.
         */
        g_assert_not_reached();

    case 2:
        /* Tag check fail causes asynchronous flag set.  */
        mte_async_check_fail(env, dirty_ptr, ra, arm_mmu_idx, el);
        break;

    case 3:
        /*
         * Tag check fail causes asynchronous flag set for stores, or
         * a synchronous exception for loads.
         */
        if (FIELD_EX32(desc, MTEDESC, WRITE)) {
            mte_async_check_fail(env, dirty_ptr, ra, arm_mmu_idx, el);
        } else {
            mte_sync_check_fail(env, desc, dirty_ptr, ra);
        }
        break;
    }
}

/**
 * checkN:
 * @tag: tag memory to test
 * @odd: true to begin testing at tags at odd nibble
 * @cmp: the tag to compare against
 * @count: number of tags to test
 *
 * Return the number of successful tests.
 * Thus a return value < @count indicates a failure.
 *
 * A note about sizes: count is expected to be small.
 *
 * The most common use will be LDP/STP of two integer registers,
 * which means 16 bytes of memory touching at most 2 tags, but
 * often the access is aligned and thus just 1 tag.
 *
 * Using AdvSIMD LD/ST (multiple), one can access 64 bytes of memory,
 * touching at most 5 tags.  SVE LDR/STR (vector) with the default
 * vector length is also 64 bytes; the maximum architectural length
 * is 256 bytes touching at most 9 tags.
 *
 * The loop below uses 7 logical operations and 1 memory operation
 * per tag pair.  An implementation that loads an aligned word and
 * uses masking to ignore adjacent tags requires 18 logical operations
 * and thus does not begin to pay off until 6 tags.
 * Which, according to the survey above, is unlikely to be common.
 */
static int checkN(uint8_t *mem, int odd, int cmp, int count)
{
    int n = 0, diff;

    /* Replicate the test tag and compare.  */
    cmp *= 0x11;
    diff = *mem++ ^ cmp;

    if (odd) {
        goto start_odd;
    }

    while (1) {
        /* Test even tag. */
        if (unlikely((diff) & 0x0f)) {
            break;
        }
        if (++n == count) {
            break;
        }

    start_odd:
        /* Test odd tag. */
        if (unlikely((diff) & 0xf0)) {
            break;
        }
        if (++n == count) {
            break;
        }

        diff = *mem++ ^ cmp;
    }
    return n;
}

/**
 * checkNrev:
 * @tag: tag memory to test
 * @odd: true to begin testing at tags at odd nibble
 * @cmp: the tag to compare against
 * @count: number of tags to test
 *
 * Return the number of successful tests.
 * Thus a return value < @count indicates a failure.
 *
 * This is like checkN, but it runs backwards, checking the
 * tags starting with @tag and then the tags preceding it.
 * This is needed by the backwards-memory-copying operations.
 */
static int checkNrev(uint8_t *mem, int odd, int cmp, int count)
{
    int n = 0, diff;

    /* Replicate the test tag and compare.  */
    cmp *= 0x11;
    diff = *mem-- ^ cmp;

    if (!odd) {
        goto start_even;
    }

    while (1) {
        /* Test odd tag. */
        if (unlikely((diff) & 0xf0)) {
            break;
        }
        if (++n == count) {
            break;
        }

    start_even:
        /* Test even tag. */
        if (unlikely((diff) & 0x0f)) {
            break;
        }
        if (++n == count) {
            break;
        }

        diff = *mem-- ^ cmp;
    }
    return n;
}

/**
 * mte_probe_int() - helper for mte_probe and mte_check
 * @env: CPU environment
 * @desc: MTEDESC descriptor
 * @ptr: virtual address of the base of the access
 * @fault: return virtual address of the first check failure
 *
 * Internal routine for both mte_probe and mte_check.
 * Return zero on failure, filling in *fault.
 * Return negative on trivial success for tbi disabled.
 * Return positive on success with tbi enabled.
 */
static int mte_probe_int(CPUARMState *env, uint32_t desc, uint64_t ptr,
                         uintptr_t ra, uint64_t *fault)
{
    int mmu_idx, ptr_tag, bit55;
    uint64_t ptr_last, prev_page, next_page;
    uint64_t tag_first, tag_last;
    uint32_t sizem1, tag_count, n, c;
    uint8_t *mem1, *mem2;
    MMUAccessType type;

    bit55 = extract64(ptr, 55, 1);
    *fault = ptr;

    /* If TBI is disabled, the access is unchecked, and ptr is not dirty. */
    if (unlikely(!tbi_check(desc, bit55))) {
        return -1;
    }

    ptr_tag = allocation_tag_from_addr(ptr);

    if (tcma_check(desc, bit55, ptr_tag)) {
        return 1;
    }

    mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    type = FIELD_EX32(desc, MTEDESC, WRITE) ? MMU_DATA_STORE : MMU_DATA_LOAD;
    sizem1 = FIELD_EX32(desc, MTEDESC, SIZEM1);

    /* Find the addr of the end of the access */
    ptr_last = ptr + sizem1;

    /* Round the bounds to the tag granule, and compute the number of tags. */
    tag_first = QEMU_ALIGN_DOWN(ptr, TAG_GRANULE);
    tag_last = QEMU_ALIGN_DOWN(ptr_last, TAG_GRANULE);
    tag_count = ((tag_last - tag_first) / TAG_GRANULE) + 1;

    /* Locate the page boundaries. */
    prev_page = ptr & TARGET_PAGE_MASK;
    next_page = prev_page + TARGET_PAGE_SIZE;

    if (likely(tag_last - prev_page < TARGET_PAGE_SIZE)) {
        /* Memory access stays on one page. */
        mem1 = allocation_tag_mem(env, mmu_idx, ptr, type, sizem1 + 1,
                                  MMU_DATA_LOAD, ra);
        if (!mem1) {
            return 1;
        }
        /* Perform all of the comparisons. */
        n = checkN(mem1, ptr & TAG_GRANULE, ptr_tag, tag_count);
    } else {
        /* Memory access crosses to next page. */
        mem1 = allocation_tag_mem(env, mmu_idx, ptr, type, next_page - ptr,
                                  MMU_DATA_LOAD, ra);

        mem2 = allocation_tag_mem(env, mmu_idx, next_page, type,
                                  ptr_last - next_page + 1,
                                  MMU_DATA_LOAD, ra);

        /*
         * Perform all of the comparisons.
         * Note the possible but unlikely case of the operation spanning
         * two pages that do not both have tagging enabled.
         */
        n = c = (next_page - tag_first) / TAG_GRANULE;
        if (mem1) {
            n = checkN(mem1, ptr & TAG_GRANULE, ptr_tag, c);
        }
        if (n == c) {
            if (!mem2) {
                return 1;
            }
            n += checkN(mem2, 0, ptr_tag, tag_count - c);
        }
    }

    if (likely(n == tag_count)) {
        return 1;
    }

    /*
     * If we failed, we know which granule.  For the first granule, the
     * failure address is @ptr, the first byte accessed.  Otherwise the
     * failure address is the first byte of the nth granule.
     */
    if (n > 0) {
        *fault = tag_first + n * TAG_GRANULE;
    }
    return 0;
}

uint64_t mte_check(CPUARMState *env, uint32_t desc, uint64_t ptr, uintptr_t ra)
{
    uint64_t fault;
    int ret = mte_probe_int(env, desc, ptr, ra, &fault);

    if (unlikely(ret == 0)) {
        mte_check_fail(env, desc, fault, ra);
    } else if (ret < 0) {
        return ptr;
    }
    return useronly_clean_ptr(ptr);
}

uint64_t HELPER(mte_check)(CPUARMState *env, uint32_t desc, uint64_t ptr)
{
    /*
     * R_XCHFJ: Alignment check not caused by memory type is priority 1,
     * higher than any translation fault.  When MTE is disabled, tcg
     * performs the alignment check during the code generated for the
     * memory access.  With MTE enabled, we must check this here before
     * raising any translation fault in allocation_tag_mem.
     */
    unsigned align = FIELD_EX32(desc, MTEDESC, ALIGN);
    if (unlikely(align)) {
        align = (1u << align) - 1;
        if (unlikely(ptr & align)) {
            int idx = FIELD_EX32(desc, MTEDESC, MIDX);
            bool w = FIELD_EX32(desc, MTEDESC, WRITE);
            MMUAccessType type = w ? MMU_DATA_STORE : MMU_DATA_LOAD;
            arm_cpu_do_unaligned_access(env_cpu(env), ptr, type, idx, GETPC());
        }
    }

    return mte_check(env, desc, ptr, GETPC());
}

/*
 * No-fault version of mte_check, to be used by SVE for MemSingleNF.
 * Returns false if the access is Checked and the check failed.  This
 * is only intended to probe the tag -- the validity of the page must
 * be checked beforehand.
 */
bool mte_probe(CPUARMState *env, uint32_t desc, uint64_t ptr)
{
    uint64_t fault;
    int ret = mte_probe_int(env, desc, ptr, 0, &fault);

    return ret != 0;
}

/*
 * Perform an MTE checked access for DC_ZVA.
 */
uint64_t HELPER(mte_check_zva)(CPUARMState *env, uint32_t desc, uint64_t ptr)
{
    uintptr_t ra = GETPC();
    int log2_dcz_bytes, log2_tag_bytes;
    int mmu_idx, bit55;
    intptr_t dcz_bytes, tag_bytes, i;
    void *mem;
    uint64_t ptr_tag, mem_tag, align_ptr;

    bit55 = extract64(ptr, 55, 1);

    /* If TBI is disabled, the access is unchecked, and ptr is not dirty. */
    if (unlikely(!tbi_check(desc, bit55))) {
        return ptr;
    }

    ptr_tag = allocation_tag_from_addr(ptr);

    if (tcma_check(desc, bit55, ptr_tag)) {
        goto done;
    }

    /*
     * In arm_cpu_realizefn, we asserted that dcz > LOG2_TAG_GRANULE+1,
     * i.e. 32 bytes, which is an unreasonably small dcz anyway, to make
     * sure that we can access one complete tag byte here.
     */
    log2_dcz_bytes = env_archcpu(env)->dcz_blocksize + 2;
    log2_tag_bytes = log2_dcz_bytes - (LOG2_TAG_GRANULE + 1);
    dcz_bytes = (intptr_t)1 << log2_dcz_bytes;
    tag_bytes = (intptr_t)1 << log2_tag_bytes;
    align_ptr = ptr & -dcz_bytes;

    /*
     * Trap if accessing an invalid page.  DC_ZVA requires that we supply
     * the original pointer for an invalid page.  But watchpoints require
     * that we probe the actual space.  So do both.
     */
    mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    (void) probe_write(env, ptr, 1, mmu_idx, ra);
    mem = allocation_tag_mem(env, mmu_idx, align_ptr, MMU_DATA_STORE,
                             dcz_bytes, MMU_DATA_LOAD, ra);
    if (!mem) {
        goto done;
    }

    /*
     * Unlike the reasoning for checkN, DC_ZVA is always aligned, and thus
     * it is quite easy to perform all of the comparisons at once without
     * any extra masking.
     *
     * The most common zva block size is 64; some of the thunderx cpus use
     * a block size of 128.  For user-only, aarch64_max_initfn will set the
     * block size to 512.  Fill out the other cases for future-proofing.
     *
     * In order to be able to find the first miscompare later, we want the
     * tag bytes to be in little-endian order.
     */
    switch (log2_tag_bytes) {
    case 0: /* zva_blocksize 32 */
        mem_tag = *(uint8_t *)mem;
        ptr_tag *= 0x11u;
        break;
    case 1: /* zva_blocksize 64 */
        mem_tag = cpu_to_le16(*(uint16_t *)mem);
        ptr_tag *= 0x1111u;
        break;
    case 2: /* zva_blocksize 128 */
        mem_tag = cpu_to_le32(*(uint32_t *)mem);
        ptr_tag *= 0x11111111u;
        break;
    case 3: /* zva_blocksize 256 */
        mem_tag = cpu_to_le64(*(uint64_t *)mem);
        ptr_tag *= 0x1111111111111111ull;
        break;

    default: /* zva_blocksize 512, 1024, 2048 */
        ptr_tag *= 0x1111111111111111ull;
        i = 0;
        do {
            mem_tag = cpu_to_le64(*(uint64_t *)(mem + i));
            if (unlikely(mem_tag != ptr_tag)) {
                goto fail;
            }
            i += 8;
            align_ptr += 16 * TAG_GRANULE;
        } while (i < tag_bytes);
        goto done;
    }

    if (likely(mem_tag == ptr_tag)) {
        goto done;
    }

 fail:
    /* Locate the first nibble that differs. */
    i = ctz64(mem_tag ^ ptr_tag) >> 4;
    mte_check_fail(env, desc, align_ptr + i * TAG_GRANULE, ra);

 done:
    return useronly_clean_ptr(ptr);
}

uint64_t mte_mops_probe(CPUARMState *env, uint64_t ptr, uint64_t size,
                        uint32_t desc)
{
    int mmu_idx, tag_count;
    uint64_t ptr_tag, tag_first, tag_last;
    void *mem;
    bool w = FIELD_EX32(desc, MTEDESC, WRITE);
    uint32_t n;

    mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    /* True probe; this will never fault */
    mem = allocation_tag_mem_probe(env, mmu_idx, ptr,
                                   w ? MMU_DATA_STORE : MMU_DATA_LOAD,
                                   size, MMU_DATA_LOAD, true, 0);
    if (!mem) {
        return size;
    }

    /*
     * TODO: checkN() is not designed for checks of the size we expect
     * for FEAT_MOPS operations, so we should implement this differently.
     * Maybe we should do something like
     *   if (region start and size are aligned nicely) {
     *      do direct loads of 64 tag bits at a time;
     *   } else {
     *      call checkN()
     *   }
     */
    /* Round the bounds to the tag granule, and compute the number of tags. */
    ptr_tag = allocation_tag_from_addr(ptr);
    tag_first = QEMU_ALIGN_DOWN(ptr, TAG_GRANULE);
    tag_last = QEMU_ALIGN_DOWN(ptr + size - 1, TAG_GRANULE);
    tag_count = ((tag_last - tag_first) / TAG_GRANULE) + 1;
    n = checkN(mem, ptr & TAG_GRANULE, ptr_tag, tag_count);
    if (likely(n == tag_count)) {
        return size;
    }

    /*
     * Failure; for the first granule, it's at @ptr. Otherwise
     * it's at the first byte of the nth granule. Calculate how
     * many bytes we can access without hitting that failure.
     */
    if (n == 0) {
        return 0;
    } else {
        return n * TAG_GRANULE - (ptr - tag_first);
    }
}

uint64_t mte_mops_probe_rev(CPUARMState *env, uint64_t ptr, uint64_t size,
                            uint32_t desc)
{
    int mmu_idx, tag_count;
    uint64_t ptr_tag, tag_first, tag_last;
    void *mem;
    bool w = FIELD_EX32(desc, MTEDESC, WRITE);
    uint32_t n;

    mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    /*
     * True probe; this will never fault. Note that our caller passes
     * us a pointer to the end of the region, but allocation_tag_mem_probe()
     * wants a pointer to the start. Because we know we don't span a page
     * boundary and that allocation_tag_mem_probe() doesn't otherwise care
     * about the size, pass in a size of 1 byte. This is simpler than
     * adjusting the ptr to point to the start of the region and then having
     * to adjust the returned 'mem' to get the end of the tag memory.
     */
    mem = allocation_tag_mem_probe(env, mmu_idx, ptr,
                                   w ? MMU_DATA_STORE : MMU_DATA_LOAD,
                                   1, MMU_DATA_LOAD, true, 0);
    if (!mem) {
        return size;
    }

    /*
     * TODO: checkNrev() is not designed for checks of the size we expect
     * for FEAT_MOPS operations, so we should implement this differently.
     * Maybe we should do something like
     *   if (region start and size are aligned nicely) {
     *      do direct loads of 64 tag bits at a time;
     *   } else {
     *      call checkN()
     *   }
     */
    /* Round the bounds to the tag granule, and compute the number of tags. */
    ptr_tag = allocation_tag_from_addr(ptr);
    tag_first = QEMU_ALIGN_DOWN(ptr - (size - 1), TAG_GRANULE);
    tag_last = QEMU_ALIGN_DOWN(ptr, TAG_GRANULE);
    tag_count = ((tag_last - tag_first) / TAG_GRANULE) + 1;
    n = checkNrev(mem, ptr & TAG_GRANULE, ptr_tag, tag_count);
    if (likely(n == tag_count)) {
        return size;
    }

    /*
     * Failure; for the first granule, it's at @ptr. Otherwise
     * it's at the last byte of the nth granule. Calculate how
     * many bytes we can access without hitting that failure.
     */
    if (n == 0) {
        return 0;
    } else {
        return (n - 1) * TAG_GRANULE + ((ptr + 1) - tag_last);
    }
}

void mte_mops_set_tags(CPUARMState *env, uint64_t ptr, uint64_t size,
                       uint32_t desc)
{
    int mmu_idx, tag_count;
    uint64_t ptr_tag;
    void *mem;

    if (!desc) {
        /* Tags not actually enabled */
        return;
    }

    mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    /* True probe: this will never fault */
    mem = allocation_tag_mem_probe(env, mmu_idx, ptr, MMU_DATA_STORE, size,
                                   MMU_DATA_STORE, true, 0);
    if (!mem) {
        return;
    }

    /*
     * We know that ptr and size are both TAG_GRANULE aligned; store
     * the tag from the pointer value into the tag memory.
     */
    ptr_tag = allocation_tag_from_addr(ptr);
    tag_count = size / TAG_GRANULE;
    if (ptr & TAG_GRANULE) {
        /* Not 2*TAG_GRANULE-aligned: store tag to first nibble */
        store_tag1_parallel(TAG_GRANULE, mem, ptr_tag);
        mem++;
        tag_count--;
    }
    memset(mem, ptr_tag | (ptr_tag << 4), tag_count / 2);
    if (tag_count & 1) {
        /* Final trailing unaligned nibble */
        mem += tag_count / 2;
        store_tag1_parallel(0, mem, ptr_tag);
    }
}
