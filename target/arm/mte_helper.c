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
#include "cpu.h"
#include "internals.h"
#include "exec/exec-all.h"
#include "exec/ram_addr.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
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
 * allocation_tag_mem:
 * @env: the cpu environment
 * @ptr_mmu_idx: the addressing regime to use for the virtual address
 * @ptr: the virtual address for which to look up tag memory
 * @ptr_access: the access to use for the virtual address
 * @ptr_size: the number of bytes in the normal memory access
 * @tag_access: the access to use for the tag memory
 * @tag_size: the number of bytes in the tag memory access
 * @ra: the return address for exception handling
 *
 * Our tag memory is formatted as a sequence of little-endian nibbles.
 * That is, the byte at (addr >> (LOG2_TAG_GRANULE + 1)) contains two
 * tags, with the tag at [3:0] for the lower addr and the tag at [7:4]
 * for the higher addr.
 *
 * Here, resolve the physical address from the virtual address, and return
 * a pointer to the corresponding tag byte.  Exit with exception if the
 * virtual address is not accessible for @ptr_access.
 *
 * The @ptr_size and @tag_size values may not have an obvious relation
 * due to the alignment of @ptr, and the number of tag checks required.
 *
 * If there is no tag storage corresponding to @ptr, return NULL.
 */
static uint8_t *allocation_tag_mem(CPUARMState *env, int ptr_mmu_idx,
                                   uint64_t ptr, MMUAccessType ptr_access,
                                   int ptr_size, MMUAccessType tag_access,
                                   int tag_size, uintptr_t ra)
{
#ifdef CONFIG_USER_ONLY
    /* Tag storage not implemented.  */
    return NULL;
#else
    uintptr_t index;
    CPUIOTLBEntry *iotlbentry;
    int in_page, flags;
    ram_addr_t ptr_ra;
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
     * When RA == 0, this is for mte_probe1.  The page is expected to be
     * valid.  Indicate to probe_access_flags no-fault, then assert that
     * we received a valid page.
     */
    flags = probe_access_flags(env, ptr, ptr_access, ptr_mmu_idx,
                               ra == 0, &host, ra);
    assert(!(flags & TLB_INVALID_MASK));

    /*
     * Find the iotlbentry for ptr.  This *must* be present in the TLB
     * because we just found the mapping.
     * TODO: Perhaps there should be a cputlb helper that returns a
     * matching tlb entry + iotlb entry.
     */
    index = tlb_index(env, ptr_mmu_idx, ptr);
# ifdef CONFIG_DEBUG_TCG
    {
        CPUTLBEntry *entry = tlb_entry(env, ptr_mmu_idx, ptr);
        target_ulong comparator = (ptr_access == MMU_DATA_LOAD
                                   ? entry->addr_read
                                   : tlb_addr_write(entry));
        g_assert(tlb_hit(comparator, ptr));
    }
# endif
    iotlbentry = &env_tlb(env)->d[ptr_mmu_idx].iotlb[index];

    /* If the virtual page MemAttr != Tagged, access unchecked. */
    if (!arm_tlb_mte_tagged(&iotlbentry->attrs)) {
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
     * The Normal memory access can extend to the next page.  E.g. a single
     * 8-byte access to the last byte of a page will check only the last
     * tag on the first page.
     * Any page access exception has priority over tag check exception.
     */
    in_page = -(ptr | TARGET_PAGE_MASK);
    if (unlikely(ptr_size > in_page)) {
        void *ignore;
        flags |= probe_access_flags(env, ptr + in_page, ptr_access,
                                    ptr_mmu_idx, ra == 0, &ignore, ra);
        assert(!(flags & TLB_INVALID_MASK));
    }

    /* Any debug exception has priority over a tag check exception. */
    if (unlikely(flags & TLB_WATCHPOINT)) {
        int wp = ptr_access == MMU_DATA_LOAD ? BP_MEM_READ : BP_MEM_WRITE;
        assert(ra != 0);
        cpu_check_watchpoint(env_cpu(env), ptr, ptr_size,
                             iotlbentry->attrs, wp, ra);
    }

    /*
     * Find the physical address within the normal mem space.
     * The memory region lookup must succeed because TLB_MMIO was
     * not set in the cputlb lookup above.
     */
    mr = memory_region_from_host(host, &ptr_ra);
    tcg_debug_assert(mr != NULL);
    tcg_debug_assert(memory_region_is_ram(mr));
    ptr_paddr = ptr_ra;
    do {
        ptr_paddr += mr->addr;
        mr = mr->container;
    } while (mr);

    /* Convert to the physical address in tag space.  */
    tag_paddr = ptr_paddr >> (LOG2_TAG_GRANULE + 1);

    /* Look up the address in tag space. */
    tag_asi = iotlbentry->attrs.secure ? ARMASIdx_TagS : ARMASIdx_TagNS;
    tag_as = cpu_get_address_space(env_cpu(env), tag_asi);
    mr = address_space_translate(tag_as, tag_paddr, &xlat, NULL,
                                 tag_access == MMU_DATA_STORE,
                                 iotlbentry->attrs);

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
    int mmu_idx = cpu_mmu_index(env, false);
    uint8_t *mem;
    int rtag = 0;

    /* Trap if accessing an invalid page.  */
    mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_LOAD, 1,
                             MMU_DATA_LOAD, 1, GETPC());

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
                                    cpu_mmu_index(env, false), ra);
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
    uint8_t old = atomic_read(mem);

    while (1) {
        uint8_t new = deposit32(old, ofs, 4, tag);
        uint8_t cmp = atomic_cmpxchg(mem, old, new);
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
    int mmu_idx = cpu_mmu_index(env, false);
    uint8_t *mem;

    check_tag_aligned(env, ptr, ra);

    /* Trap if accessing an invalid page.  */
    mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_STORE, TAG_GRANULE,
                             MMU_DATA_STORE, 1, ra);

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
    int mmu_idx = cpu_mmu_index(env, false);
    uintptr_t ra = GETPC();

    check_tag_aligned(env, ptr, ra);
    probe_write(env, ptr, TAG_GRANULE, mmu_idx, ra);
}

static inline void do_st2g(CPUARMState *env, uint64_t ptr, uint64_t xt,
                           uintptr_t ra, stg_store1 store1)
{
    int mmu_idx = cpu_mmu_index(env, false);
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
                                  TAG_GRANULE, MMU_DATA_STORE, 1, ra);
        mem2 = allocation_tag_mem(env, mmu_idx, ptr + TAG_GRANULE,
                                  MMU_DATA_STORE, TAG_GRANULE,
                                  MMU_DATA_STORE, 1, ra);

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
                                  2 * TAG_GRANULE, MMU_DATA_STORE, 1, ra);
        if (mem1) {
            tag |= tag << 4;
            atomic_set(mem1, tag);
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
    int mmu_idx = cpu_mmu_index(env, false);
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

#define LDGM_STGM_SIZE  (4 << GMID_EL1_BS)

uint64_t HELPER(ldgm)(CPUARMState *env, uint64_t ptr)
{
    int mmu_idx = cpu_mmu_index(env, false);
    uintptr_t ra = GETPC();
    void *tag_mem;

    ptr = QEMU_ALIGN_DOWN(ptr, LDGM_STGM_SIZE);

    /* Trap if accessing an invalid page.  */
    tag_mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_LOAD,
                                 LDGM_STGM_SIZE, MMU_DATA_LOAD,
                                 LDGM_STGM_SIZE / (2 * TAG_GRANULE), ra);

    /* The tag is squashed to zero if the page does not support tags.  */
    if (!tag_mem) {
        return 0;
    }

    QEMU_BUILD_BUG_ON(GMID_EL1_BS != 6);
    /*
     * We are loading 64-bits worth of tags.  The ordering of elements
     * within the word corresponds to a 64-bit little-endian operation.
     */
    return ldq_le_p(tag_mem);
}

void HELPER(stgm)(CPUARMState *env, uint64_t ptr, uint64_t val)
{
    int mmu_idx = cpu_mmu_index(env, false);
    uintptr_t ra = GETPC();
    void *tag_mem;

    ptr = QEMU_ALIGN_DOWN(ptr, LDGM_STGM_SIZE);

    /* Trap if accessing an invalid page.  */
    tag_mem = allocation_tag_mem(env, mmu_idx, ptr, MMU_DATA_STORE,
                                 LDGM_STGM_SIZE, MMU_DATA_LOAD,
                                 LDGM_STGM_SIZE / (2 * TAG_GRANULE), ra);

    /*
     * Tag store only happens if the page support tags,
     * and if the OS has enabled access to the tags.
     */
    if (!tag_mem) {
        return;
    }

    QEMU_BUILD_BUG_ON(GMID_EL1_BS != 6);
    /*
     * We are storing 64-bits worth of tags.  The ordering of elements
     * within the word corresponds to a 64-bit little-endian operation.
     */
    stq_le_p(tag_mem, val);
}

void HELPER(stzgm_tags)(CPUARMState *env, uint64_t ptr, uint64_t val)
{
    uintptr_t ra = GETPC();
    int mmu_idx = cpu_mmu_index(env, false);
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
                             MMU_DATA_STORE, tag_bytes, ra);
    if (mem) {
        int tag_pair = (val & 0xf) * 0x11;
        memset(mem, tag_pair, tag_bytes);
    }
}

/* Record a tag check failure.  */
static void mte_check_fail(CPUARMState *env, int mmu_idx,
                           uint64_t dirty_ptr, uintptr_t ra)
{
    ARMMMUIdx arm_mmu_idx = core_to_aa64_mmu_idx(mmu_idx);
    int el, reg_el, tcf, select;
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
        /*
         * Tag check fail causes a synchronous exception.
         *
         * In restore_state_to_opc, we set the exception syndrome
         * for the load or store operation.  Unwind first so we
         * may overwrite that with the syndrome for the tag check.
         */
        cpu_restore_state(env_cpu(env), ra, true);
        env->exception.vaddress = dirty_ptr;
        raise_exception(env, EXCP_DATA_ABORT,
                        syn_data_abort_no_iss(el != 0, 0, 0, 0, 0, 0, 0x11),
                        exception_target_el(env));
        /* noreturn, but fall through to the assert anyway */

    case 0:
        /*
         * Tag check fail does not affect the PE.
         * We eliminate this case by not setting MTE_ACTIVE
         * in tb_flags, so that we never make this runtime call.
         */
        g_assert_not_reached();

    case 2:
        /* Tag check fail causes asynchronous flag set.  */
        mmu_idx = arm_mmu_idx_el(env, el);
        if (regime_has_2_ranges(mmu_idx)) {
            select = extract64(dirty_ptr, 55, 1);
        } else {
            select = 0;
        }
        env->cp15.tfsr_el[el] |= 1 << select;
        break;

    default:
        /* Case 3: Reserved. */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Tag check failure with SCTLR_EL%d.TCF%s "
                      "set to reserved value %d\n",
                      reg_el, el ? "" : "0", tcf);
        break;
    }
}

/*
 * Perform an MTE checked access for a single logical or atomic access.
 */
static bool mte_probe1_int(CPUARMState *env, uint32_t desc, uint64_t ptr,
                           uintptr_t ra, int bit55)
{
    int mem_tag, mmu_idx, ptr_tag, size;
    MMUAccessType type;
    uint8_t *mem;

    ptr_tag = allocation_tag_from_addr(ptr);

    if (tcma_check(desc, bit55, ptr_tag)) {
        return true;
    }

    mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    type = FIELD_EX32(desc, MTEDESC, WRITE) ? MMU_DATA_STORE : MMU_DATA_LOAD;
    size = FIELD_EX32(desc, MTEDESC, ESIZE);

    mem = allocation_tag_mem(env, mmu_idx, ptr, type, size,
                             MMU_DATA_LOAD, 1, ra);
    if (!mem) {
        return true;
    }

    mem_tag = load_tag1(ptr, mem);
    return ptr_tag == mem_tag;
}

/*
 * No-fault version of mte_check1, to be used by SVE for MemSingleNF.
 * Returns false if the access is Checked and the check failed.  This
 * is only intended to probe the tag -- the validity of the page must
 * be checked beforehand.
 */
bool mte_probe1(CPUARMState *env, uint32_t desc, uint64_t ptr)
{
    int bit55 = extract64(ptr, 55, 1);

    /* If TBI is disabled, the access is unchecked. */
    if (unlikely(!tbi_check(desc, bit55))) {
        return true;
    }

    return mte_probe1_int(env, desc, ptr, 0, bit55);
}

uint64_t mte_check1(CPUARMState *env, uint32_t desc,
                    uint64_t ptr, uintptr_t ra)
{
    int bit55 = extract64(ptr, 55, 1);

    /* If TBI is disabled, the access is unchecked, and ptr is not dirty. */
    if (unlikely(!tbi_check(desc, bit55))) {
        return ptr;
    }

    if (unlikely(!mte_probe1_int(env, desc, ptr, ra, bit55))) {
        int mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
        mte_check_fail(env, mmu_idx, ptr, ra);
    }

    return useronly_clean_ptr(ptr);
}

uint64_t HELPER(mte_check1)(CPUARMState *env, uint32_t desc, uint64_t ptr)
{
    return mte_check1(env, desc, ptr, GETPC());
}

/*
 * Perform an MTE checked access for multiple logical accesses.
 */

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

uint64_t mte_checkN(CPUARMState *env, uint32_t desc,
                    uint64_t ptr, uintptr_t ra)
{
    int mmu_idx, ptr_tag, bit55;
    uint64_t ptr_last, ptr_end, prev_page, next_page;
    uint64_t tag_first, tag_end;
    uint64_t tag_byte_first, tag_byte_end;
    uint32_t esize, total, tag_count, tag_size, n, c;
    uint8_t *mem1, *mem2;
    MMUAccessType type;

    bit55 = extract64(ptr, 55, 1);

    /* If TBI is disabled, the access is unchecked, and ptr is not dirty. */
    if (unlikely(!tbi_check(desc, bit55))) {
        return ptr;
    }

    ptr_tag = allocation_tag_from_addr(ptr);

    if (tcma_check(desc, bit55, ptr_tag)) {
        goto done;
    }

    mmu_idx = FIELD_EX32(desc, MTEDESC, MIDX);
    type = FIELD_EX32(desc, MTEDESC, WRITE) ? MMU_DATA_STORE : MMU_DATA_LOAD;
    esize = FIELD_EX32(desc, MTEDESC, ESIZE);
    total = FIELD_EX32(desc, MTEDESC, TSIZE);

    /* Find the addr of the end of the access, and of the last element. */
    ptr_end = ptr + total;
    ptr_last = ptr_end - esize;

    /* Round the bounds to the tag granule, and compute the number of tags. */
    tag_first = QEMU_ALIGN_DOWN(ptr, TAG_GRANULE);
    tag_end = QEMU_ALIGN_UP(ptr_last, TAG_GRANULE);
    tag_count = (tag_end - tag_first) / TAG_GRANULE;

    /* Round the bounds to twice the tag granule, and compute the bytes. */
    tag_byte_first = QEMU_ALIGN_DOWN(ptr, 2 * TAG_GRANULE);
    tag_byte_end = QEMU_ALIGN_UP(ptr_last, 2 * TAG_GRANULE);

    /* Locate the page boundaries. */
    prev_page = ptr & TARGET_PAGE_MASK;
    next_page = prev_page + TARGET_PAGE_SIZE;

    if (likely(tag_end - prev_page <= TARGET_PAGE_SIZE)) {
        /* Memory access stays on one page. */
        tag_size = (tag_byte_end - tag_byte_first) / (2 * TAG_GRANULE);
        mem1 = allocation_tag_mem(env, mmu_idx, ptr, type, total,
                                  MMU_DATA_LOAD, tag_size, ra);
        if (!mem1) {
            goto done;
        }
        /* Perform all of the comparisons. */
        n = checkN(mem1, ptr & TAG_GRANULE, ptr_tag, tag_count);
    } else {
        /* Memory access crosses to next page. */
        tag_size = (next_page - tag_byte_first) / (2 * TAG_GRANULE);
        mem1 = allocation_tag_mem(env, mmu_idx, ptr, type, next_page - ptr,
                                  MMU_DATA_LOAD, tag_size, ra);

        tag_size = (tag_byte_end - next_page) / (2 * TAG_GRANULE);
        mem2 = allocation_tag_mem(env, mmu_idx, next_page, type,
                                  ptr_end - next_page,
                                  MMU_DATA_LOAD, tag_size, ra);

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
                goto done;
            }
            n += checkN(mem2, 0, ptr_tag, tag_count - c);
        }
    }

    /*
     * If we failed, we know which granule.  Compute the element that
     * is first in that granule, and signal failure on that element.
     */
    if (unlikely(n < tag_count)) {
        uint64_t fail_ofs;

        fail_ofs = tag_first + n * TAG_GRANULE - ptr;
        fail_ofs = ROUND_UP(fail_ofs, esize);
        mte_check_fail(env, mmu_idx, ptr + fail_ofs, ra);
    }

 done:
    return useronly_clean_ptr(ptr);
}

uint64_t HELPER(mte_checkN)(CPUARMState *env, uint32_t desc, uint64_t ptr)
{
    return mte_checkN(env, desc, ptr, GETPC());
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
                             dcz_bytes, MMU_DATA_LOAD, tag_bytes, ra);
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
    mte_check_fail(env, mmu_idx, align_ptr + i * TAG_GRANULE, ra);

 done:
    return useronly_clean_ptr(ptr);
}
