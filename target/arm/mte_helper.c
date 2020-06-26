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
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"


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
    /* Tag storage not implemented.  */
    return NULL;
}

uint64_t HELPER(irg)(CPUARMState *env, uint64_t rn, uint64_t rm)
{
    int rtag;

    /*
     * Our IMPDEF choice for GCR_EL1.RRND==1 is to behave as if
     * GCR_EL1.RRND==0, always producing deterministic results.
     */
    uint16_t exclude = extract32(rm | env->cp15.gcr_el1, 0, 16);
    int start = extract32(env->cp15.rgsr_el1, 0, 4);
    int seed = extract32(env->cp15.rgsr_el1, 8, 16);
    int offset, i;

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
