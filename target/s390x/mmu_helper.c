/*
 * S390x MMU related functions
 *
 * Copyright (c) 2011 Alexander Graf
 * Copyright (c) 2015 Thomas Huth, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "system/address-spaces.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "kvm/kvm_s390x.h"
#include "system/kvm.h"
#include "system/tcg.h"
#include "system/memory.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "hw/hw.h"
#include "hw/s390x/storage-keys.h"
#include "hw/boards.h"

/* Fetch/store bits in the translation exception code: */
#define FS_READ  0x800
#define FS_WRITE 0x400

static void trigger_access_exception(CPUS390XState *env, uint32_t type,
                                     uint64_t tec)
{
    S390CPU *cpu = env_archcpu(env);

    if (kvm_enabled()) {
        kvm_s390_access_exception(cpu, type, tec);
    } else {
        CPUState *cs = env_cpu(env);
        if (type != PGM_ADDRESSING) {
            stq_phys(cs->as, env->psa + offsetof(LowCore, trans_exc_code), tec);
        }
        trigger_pgm_exception(env, type);
    }
}

/* check whether the address would be proteted by Low-Address Protection */
static bool is_low_address(uint64_t addr)
{
    return addr <= 511 || (addr >= 4096 && addr <= 4607);
}

/* check whether Low-Address Protection is enabled for mmu_translate() */
static bool lowprot_enabled(const CPUS390XState *env, uint64_t asc)
{
    if (!(env->cregs[0] & CR0_LOWPROT)) {
        return false;
    }
    if (!(env->psw.mask & PSW_MASK_DAT)) {
        return true;
    }

    /* Check the private-space control bit */
    switch (asc) {
    case PSW_ASC_PRIMARY:
        return !(env->cregs[1] & ASCE_PRIVATE_SPACE);
    case PSW_ASC_SECONDARY:
        return !(env->cregs[7] & ASCE_PRIVATE_SPACE);
    case PSW_ASC_HOME:
        return !(env->cregs[13] & ASCE_PRIVATE_SPACE);
    default:
        /* We don't support access register mode */
        error_report("unsupported addressing mode");
        exit(1);
    }
}

/**
 * Translate real address to absolute (= physical)
 * address by taking care of the prefix mapping.
 */
target_ulong mmu_real2abs(CPUS390XState *env, target_ulong raddr)
{
    if (raddr < 0x2000) {
        return raddr + env->psa;    /* Map the lowcore. */
    } else if (raddr >= env->psa && raddr < env->psa + 0x2000) {
        return raddr - env->psa;    /* Map the 0 page. */
    }
    return raddr;
}

bool mmu_absolute_addr_valid(target_ulong addr, bool is_write)
{
    return address_space_access_valid(&address_space_memory,
                                      addr & TARGET_PAGE_MASK,
                                      TARGET_PAGE_SIZE, is_write,
                                      MEMTXATTRS_UNSPECIFIED);
}

static inline bool read_table_entry(CPUS390XState *env, hwaddr gaddr,
                                    uint64_t *entry)
{
    CPUState *cs = env_cpu(env);

    /*
     * According to the PoP, these table addresses are "unpredictably real
     * or absolute". Also, "it is unpredictable whether the address wraps
     * or an addressing exception is recognized".
     *
     * We treat them as absolute addresses and don't wrap them.
     */
    if (unlikely(address_space_read(cs->as, gaddr, MEMTXATTRS_UNSPECIFIED,
                                    entry, sizeof(*entry)) !=
                 MEMTX_OK)) {
        return false;
    }
    *entry = be64_to_cpu(*entry);
    return true;
}

static int mmu_translate_asce(CPUS390XState *env, target_ulong vaddr,
                              uint64_t asc, uint64_t asce, target_ulong *raddr,
                              int *flags)
{
    const bool edat1 = (env->cregs[0] & CR0_EDAT) &&
                       s390_has_feat(S390_FEAT_EDAT);
    const bool edat2 = edat1 && s390_has_feat(S390_FEAT_EDAT_2);
    const bool iep = (env->cregs[0] & CR0_IEP) &&
                     s390_has_feat(S390_FEAT_INSTRUCTION_EXEC_PROT);
    const int asce_tl = asce & ASCE_TABLE_LENGTH;
    const int asce_p = asce & ASCE_PRIVATE_SPACE;
    hwaddr gaddr = asce & ASCE_ORIGIN;
    uint64_t entry;

    if (asce & ASCE_REAL_SPACE) {
        /* direct mapping */
        *raddr = vaddr;
        return 0;
    }

    switch (asce & ASCE_TYPE_MASK) {
    case ASCE_TYPE_REGION1:
        if (VADDR_REGION1_TL(vaddr) > asce_tl) {
            return PGM_REG_FIRST_TRANS;
        }
        gaddr += VADDR_REGION1_TX(vaddr) * 8;
        break;
    case ASCE_TYPE_REGION2:
        if (VADDR_REGION1_TX(vaddr)) {
            return PGM_ASCE_TYPE;
        }
        if (VADDR_REGION2_TL(vaddr) > asce_tl) {
            return PGM_REG_SEC_TRANS;
        }
        gaddr += VADDR_REGION2_TX(vaddr) * 8;
        break;
    case ASCE_TYPE_REGION3:
        if (VADDR_REGION1_TX(vaddr) || VADDR_REGION2_TX(vaddr)) {
            return PGM_ASCE_TYPE;
        }
        if (VADDR_REGION3_TL(vaddr) > asce_tl) {
            return PGM_REG_THIRD_TRANS;
        }
        gaddr += VADDR_REGION3_TX(vaddr) * 8;
        break;
    case ASCE_TYPE_SEGMENT:
        if (VADDR_REGION1_TX(vaddr) || VADDR_REGION2_TX(vaddr) ||
            VADDR_REGION3_TX(vaddr)) {
            return PGM_ASCE_TYPE;
        }
        if (VADDR_SEGMENT_TL(vaddr) > asce_tl) {
            return PGM_SEGMENT_TRANS;
        }
        gaddr += VADDR_SEGMENT_TX(vaddr) * 8;
        break;
    }

    switch (asce & ASCE_TYPE_MASK) {
    case ASCE_TYPE_REGION1:
        if (!read_table_entry(env, gaddr, &entry)) {
            return PGM_ADDRESSING;
        }
        if (entry & REGION_ENTRY_I) {
            return PGM_REG_FIRST_TRANS;
        }
        if ((entry & REGION_ENTRY_TT) != REGION_ENTRY_TT_REGION1) {
            return PGM_TRANS_SPEC;
        }
        if (VADDR_REGION2_TL(vaddr) < (entry & REGION_ENTRY_TF) >> 6 ||
            VADDR_REGION2_TL(vaddr) > (entry & REGION_ENTRY_TL)) {
            return PGM_REG_SEC_TRANS;
        }
        if (edat1 && (entry & REGION_ENTRY_P)) {
            *flags &= ~PAGE_WRITE;
        }
        gaddr = (entry & REGION_ENTRY_ORIGIN) + VADDR_REGION2_TX(vaddr) * 8;
        /* fall through */
    case ASCE_TYPE_REGION2:
        if (!read_table_entry(env, gaddr, &entry)) {
            return PGM_ADDRESSING;
        }
        if (entry & REGION_ENTRY_I) {
            return PGM_REG_SEC_TRANS;
        }
        if ((entry & REGION_ENTRY_TT) != REGION_ENTRY_TT_REGION2) {
            return PGM_TRANS_SPEC;
        }
        if (VADDR_REGION3_TL(vaddr) < (entry & REGION_ENTRY_TF) >> 6 ||
            VADDR_REGION3_TL(vaddr) > (entry & REGION_ENTRY_TL)) {
            return PGM_REG_THIRD_TRANS;
        }
        if (edat1 && (entry & REGION_ENTRY_P)) {
            *flags &= ~PAGE_WRITE;
        }
        gaddr = (entry & REGION_ENTRY_ORIGIN) + VADDR_REGION3_TX(vaddr) * 8;
        /* fall through */
    case ASCE_TYPE_REGION3:
        if (!read_table_entry(env, gaddr, &entry)) {
            return PGM_ADDRESSING;
        }
        if (entry & REGION_ENTRY_I) {
            return PGM_REG_THIRD_TRANS;
        }
        if ((entry & REGION_ENTRY_TT) != REGION_ENTRY_TT_REGION3) {
            return PGM_TRANS_SPEC;
        }
        if (edat2 && (entry & REGION3_ENTRY_CR) && asce_p) {
            return PGM_TRANS_SPEC;
        }
        if (edat1 && (entry & REGION_ENTRY_P)) {
            *flags &= ~PAGE_WRITE;
        }
        if (edat2 && (entry & REGION3_ENTRY_FC)) {
            if (iep && (entry & REGION3_ENTRY_IEP)) {
                *flags &= ~PAGE_EXEC;
            }
            *raddr = (entry & REGION3_ENTRY_RFAA) |
                     (vaddr & ~REGION3_ENTRY_RFAA);
            return 0;
        }
        if (VADDR_SEGMENT_TL(vaddr) < (entry & REGION_ENTRY_TF) >> 6 ||
            VADDR_SEGMENT_TL(vaddr) > (entry & REGION_ENTRY_TL)) {
            return PGM_SEGMENT_TRANS;
        }
        gaddr = (entry & REGION_ENTRY_ORIGIN) + VADDR_SEGMENT_TX(vaddr) * 8;
        /* fall through */
    case ASCE_TYPE_SEGMENT:
        if (!read_table_entry(env, gaddr, &entry)) {
            return PGM_ADDRESSING;
        }
        if (entry & SEGMENT_ENTRY_I) {
            return PGM_SEGMENT_TRANS;
        }
        if ((entry & SEGMENT_ENTRY_TT) != SEGMENT_ENTRY_TT_SEGMENT) {
            return PGM_TRANS_SPEC;
        }
        if ((entry & SEGMENT_ENTRY_CS) && asce_p) {
            return PGM_TRANS_SPEC;
        }
        if (entry & SEGMENT_ENTRY_P) {
            *flags &= ~PAGE_WRITE;
        }
        if (edat1 && (entry & SEGMENT_ENTRY_FC)) {
            if (iep && (entry & SEGMENT_ENTRY_IEP)) {
                *flags &= ~PAGE_EXEC;
            }
            *raddr = (entry & SEGMENT_ENTRY_SFAA) |
                     (vaddr & ~SEGMENT_ENTRY_SFAA);
            return 0;
        }
        gaddr = (entry & SEGMENT_ENTRY_ORIGIN) + VADDR_PAGE_TX(vaddr) * 8;
        break;
    }

    if (!read_table_entry(env, gaddr, &entry)) {
        return PGM_ADDRESSING;
    }
    if (entry & PAGE_ENTRY_I) {
        return PGM_PAGE_TRANS;
    }
    if (entry & PAGE_ENTRY_0) {
        return PGM_TRANS_SPEC;
    }
    if (entry & PAGE_ENTRY_P) {
        *flags &= ~PAGE_WRITE;
    }
    if (iep && (entry & PAGE_ENTRY_IEP)) {
        *flags &= ~PAGE_EXEC;
    }

    *raddr = entry & TARGET_PAGE_MASK;
    return 0;
}

static void mmu_handle_skey(target_ulong addr, int rw, int *flags)
{
    static S390SKeysClass *skeyclass;
    static S390SKeysState *ss;
    uint8_t key, old_key;

    /*
     * We expect to be called with an absolute address that has already been
     * validated, such that we can reliably use it to lookup the storage key.
     */
    if (unlikely(!ss)) {
        ss = s390_get_skeys_device();
        skeyclass = S390_SKEYS_GET_CLASS(ss);
    }

    /*
     * Don't enable storage keys if they are still disabled, i.e., no actual
     * storage key instruction was issued yet.
     */
    if (!skeyclass->skeys_are_enabled(ss)) {
        return;
    }

    /*
     * Whenever we create a new TLB entry, we set the storage key reference
     * bit. In case we allow write accesses, we set the storage key change
     * bit. Whenever the guest changes the storage key, we have to flush the
     * TLBs of all CPUs (the whole TLB or all affected entries), so that the
     * next reference/change will result in an MMU fault and make us properly
     * update the storage key here.
     *
     * Note 1: "record of references ... is not necessarily accurate",
     *         "change bit may be set in case no storing has occurred".
     *         -> We can set reference/change bits even on exceptions.
     * Note 2: certain accesses seem to ignore storage keys. For example,
     *         DAT translation does not set reference bits for table accesses.
     *
     * TODO: key-controlled protection. Only CPU accesses make use of the
     *       PSW key. CSS accesses are different - we have to pass in the key.
     *
     * TODO: we have races between getting and setting the key.
     */
    if (s390_skeys_get(ss, addr / TARGET_PAGE_SIZE, 1, &key)) {
        return;
    }
    old_key = key;

    switch (rw) {
    case MMU_DATA_LOAD:
    case MMU_INST_FETCH:
        /*
         * The TLB entry has to remain write-protected on read-faults if
         * the storage key does not indicate a change already. Otherwise
         * we might miss setting the change bit on write accesses.
         */
        if (!(key & SK_C)) {
            *flags &= ~PAGE_WRITE;
        }
        break;
    case MMU_DATA_STORE:
        key |= SK_C;
        break;
    default:
        g_assert_not_reached();
    }

    /* Any store/fetch sets the reference bit */
    key |= SK_R;

    if (key != old_key) {
        s390_skeys_set(ss, addr / TARGET_PAGE_SIZE, 1, &key);
    }
}

/**
 * Translate a virtual (logical) address into a physical (absolute) address.
 * @param vaddr  the virtual address
 * @param rw     0 = read, 1 = write, 2 = code fetch, < 0 = load real address
 * @param asc    address space control (one of the PSW_ASC_* modes)
 * @param raddr  the translated address is stored to this pointer
 * @param flags  the PAGE_READ/WRITE/EXEC flags are stored to this pointer
 * @param tec    the translation exception code if stored to this pointer if
 *               there is an exception to raise
 * @return       0 = success, != 0, the exception to raise
 */
int mmu_translate(CPUS390XState *env, target_ulong vaddr, int rw, uint64_t asc,
                  target_ulong *raddr, int *flags, uint64_t *tec)
{
    uint64_t asce;
    int r;

    *tec = (vaddr & TARGET_PAGE_MASK) | (asc >> 46) |
            (rw == MMU_DATA_STORE ? FS_WRITE : FS_READ);
    *flags = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

    if (is_low_address(vaddr & TARGET_PAGE_MASK) && lowprot_enabled(env, asc)) {
        /*
         * If any part of this page is currently protected, make sure the
         * TLB entry will not be reused.
         *
         * As the protected range is always the first 512 bytes of the
         * two first pages, we are able to catch all writes to these areas
         * just by looking at the start address (triggering the tlb miss).
         */
        *flags |= PAGE_WRITE_INV;
        if (is_low_address(vaddr) && rw == MMU_DATA_STORE) {
            /* LAP sets bit 56 */
            *tec |= 0x80;
            return PGM_PROTECTION;
        }
    }

    vaddr &= TARGET_PAGE_MASK;

    if (rw != MMU_S390_LRA && !(env->psw.mask & PSW_MASK_DAT)) {
        *raddr = vaddr;
        goto nodat;
    }

    switch (asc) {
    case PSW_ASC_PRIMARY:
        asce = env->cregs[1];
        break;
    case PSW_ASC_HOME:
        asce = env->cregs[13];
        break;
    case PSW_ASC_SECONDARY:
        asce = env->cregs[7];
        break;
    case PSW_ASC_ACCREG:
    default:
        hw_error("guest switched to unknown asc mode\n");
        break;
    }

    /* perform the DAT translation */
    r = mmu_translate_asce(env, vaddr, asc, asce, raddr, flags);
    if (unlikely(r)) {
        return r;
    }

    /* check for DAT protection */
    if (unlikely(rw == MMU_DATA_STORE && !(*flags & PAGE_WRITE))) {
        /* DAT sets bit 61 only */
        *tec |= 0x4;
        return PGM_PROTECTION;
    }

    /* check for Instruction-Execution-Protection */
    if (unlikely(rw == MMU_INST_FETCH && !(*flags & PAGE_EXEC))) {
        /* IEP sets bit 56 and 61 */
        *tec |= 0x84;
        return PGM_PROTECTION;
    }

nodat:
    if (rw >= 0) {
        /* Convert real address -> absolute address */
        *raddr = mmu_real2abs(env, *raddr);

        if (!mmu_absolute_addr_valid(*raddr, rw == MMU_DATA_STORE)) {
            *tec = 0; /* unused */
            return PGM_ADDRESSING;
        }

        mmu_handle_skey(*raddr, rw, flags);
    }
    return 0;
}

/**
 * translate_pages: Translate a set of consecutive logical page addresses
 * to absolute addresses. This function is used for TCG and old KVM without
 * the MEMOP interface.
 */
static int translate_pages(S390CPU *cpu, vaddr addr, int nr_pages,
                           target_ulong *pages, bool is_write, uint64_t *tec)
{
    uint64_t asc = cpu->env.psw.mask & PSW_MASK_ASC;
    CPUS390XState *env = &cpu->env;
    int ret, i, pflags;

    for (i = 0; i < nr_pages; i++) {
        ret = mmu_translate(env, addr, is_write, asc, &pages[i], &pflags, tec);
        if (ret) {
            return ret;
        }
        addr += TARGET_PAGE_SIZE;
    }

    return 0;
}

int s390_cpu_pv_mem_rw(S390CPU *cpu, unsigned int offset, void *hostbuf,
                       int len, bool is_write)
{
    int ret;

    if (kvm_enabled()) {
        ret = kvm_s390_mem_op_pv(cpu, offset, hostbuf, len, is_write);
    } else {
        /* Protected Virtualization is a KVM/Hardware only feature */
        g_assert_not_reached();
    }
    return ret;
}

/**
 * s390_cpu_virt_mem_rw:
 * @laddr:     the logical start address
 * @ar:        the access register number
 * @hostbuf:   buffer in host memory. NULL = do only checks w/o copying
 * @len:       length that should be transferred
 * @is_write:  true = write, false = read
 * Returns:    0 on success, non-zero if an exception occurred
 *
 * Copy from/to guest memory using logical addresses. Note that we inject a
 * program interrupt in case there is an error while accessing the memory.
 *
 * This function will always return (also for TCG), make sure to call
 * s390_cpu_virt_mem_handle_exc() to properly exit the CPU loop.
 */
int s390_cpu_virt_mem_rw(S390CPU *cpu, vaddr laddr, uint8_t ar, void *hostbuf,
                         int len, bool is_write)
{
    const MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    int currlen, nr_pages, i;
    target_ulong *pages;
    uint64_t tec;
    int ret;

    if (kvm_enabled()) {
        ret = kvm_s390_mem_op(cpu, laddr, ar, hostbuf, len, is_write);
        if (ret >= 0) {
            return ret;
        }
    }

    nr_pages = (((laddr & ~TARGET_PAGE_MASK) + len - 1) >> TARGET_PAGE_BITS)
               + 1;
    pages = g_malloc(nr_pages * sizeof(*pages));

    ret = translate_pages(cpu, laddr, nr_pages, pages, is_write, &tec);
    if (ret == 0 && hostbuf != NULL) {
        AddressSpace *as = CPU(cpu)->as;

        /* Copy data by stepping through the area page by page */
        for (i = 0; i < nr_pages; i++) {
            MemTxResult res;

            currlen = MIN(len, TARGET_PAGE_SIZE - (laddr % TARGET_PAGE_SIZE));
            res = address_space_rw(as, pages[i] | (laddr & ~TARGET_PAGE_MASK),
                                   attrs, hostbuf, currlen, is_write);
            if (res != MEMTX_OK) {
                ret = PGM_ADDRESSING;
                break;
            }
            laddr += currlen;
            hostbuf += currlen;
            len -= currlen;
        }
    }
    if (ret) {
        trigger_access_exception(&cpu->env, ret, tec);
    }

    g_free(pages);
    return ret;
}

void s390_cpu_virt_mem_handle_exc(S390CPU *cpu, uintptr_t ra)
{
    /* KVM will handle the interrupt automatically, TCG has to exit the TB */
#ifdef CONFIG_TCG
    if (tcg_enabled()) {
        cpu_loop_exit_restore(CPU(cpu), ra);
    }
#endif
}

/**
 * Translate a real address into a physical (absolute) address.
 * @param raddr  the real address
 * @param rw     0 = read, 1 = write, 2 = code fetch
 * @param addr   the translated address is stored to this pointer
 * @param flags  the PAGE_READ/WRITE/EXEC flags are stored to this pointer
 * @return       0 = success, != 0, the exception to raise
 */
int mmu_translate_real(CPUS390XState *env, target_ulong raddr, int rw,
                       target_ulong *addr, int *flags, uint64_t *tec)
{
    const bool lowprot_enabled = env->cregs[0] & CR0_LOWPROT;

    *flags = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    if (is_low_address(raddr & TARGET_PAGE_MASK) && lowprot_enabled) {
        /* see comment in mmu_translate() how this works */
        *flags |= PAGE_WRITE_INV;
        if (is_low_address(raddr) && rw == MMU_DATA_STORE) {
            /* LAP sets bit 56 */
            *tec = (raddr & TARGET_PAGE_MASK) | FS_WRITE | 0x80;
            return PGM_PROTECTION;
        }
    }

    *addr = mmu_real2abs(env, raddr & TARGET_PAGE_MASK);

    if (!mmu_absolute_addr_valid(*addr, rw == MMU_DATA_STORE)) {
        /* unused */
        *tec = 0;
        return PGM_ADDRESSING;
    }

    mmu_handle_skey(*addr, rw, flags);
    return 0;
}
