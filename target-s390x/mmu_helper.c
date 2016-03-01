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
#include "exec/address-spaces.h"
#include "cpu.h"
#include "sysemu/kvm.h"
#include "trace.h"
#include "hw/s390x/storage-keys.h"

/* #define DEBUG_S390 */
/* #define DEBUG_S390_PTE */
/* #define DEBUG_S390_STDOUT */

#ifdef DEBUG_S390
#ifdef DEBUG_S390_STDOUT
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); \
         if (qemu_log_separate()) qemu_log(fmt, ##__VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { qemu_log(fmt, ## __VA_ARGS__); } while (0)
#endif
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

#ifdef DEBUG_S390_PTE
#define PTE_DPRINTF DPRINTF
#else
#define PTE_DPRINTF(fmt, ...) \
    do { } while (0)
#endif

/* Fetch/store bits in the translation exception code: */
#define FS_READ  0x800
#define FS_WRITE 0x400

static void trigger_access_exception(CPUS390XState *env, uint32_t type,
                                     uint32_t ilen, uint64_t tec)
{
    S390CPU *cpu = s390_env_get_cpu(env);

    if (kvm_enabled()) {
        kvm_s390_access_exception(cpu, type, tec);
    } else {
        CPUState *cs = CPU(cpu);
        stq_phys(cs->as, env->psa + offsetof(LowCore, trans_exc_code), tec);
        trigger_pgm_exception(env, type, ilen);
    }
}

static void trigger_prot_fault(CPUS390XState *env, target_ulong vaddr,
                               uint64_t asc, int rw, bool exc)
{
    uint64_t tec;

    tec = vaddr | (rw == MMU_DATA_STORE ? FS_WRITE : FS_READ) | 4 | asc >> 46;

    DPRINTF("%s: trans_exc_code=%016" PRIx64 "\n", __func__, tec);

    if (!exc) {
        return;
    }

    trigger_access_exception(env, PGM_PROTECTION, ILEN_LATER_INC, tec);
}

static void trigger_page_fault(CPUS390XState *env, target_ulong vaddr,
                               uint32_t type, uint64_t asc, int rw, bool exc)
{
    int ilen = ILEN_LATER;
    uint64_t tec;

    tec = vaddr | (rw == MMU_DATA_STORE ? FS_WRITE : FS_READ) | asc >> 46;

    DPRINTF("%s: trans_exc_code=%016" PRIx64 "\n", __func__, tec);

    if (!exc) {
        return;
    }

    /* Code accesses have an undefined ilc.  */
    if (rw == MMU_INST_FETCH) {
        ilen = 2;
    }

    trigger_access_exception(env, type, ilen, tec);
}

/**
 * Translate real address to absolute (= physical)
 * address by taking care of the prefix mapping.
 */
static target_ulong mmu_real2abs(CPUS390XState *env, target_ulong raddr)
{
    if (raddr < 0x2000) {
        return raddr + env->psa;    /* Map the lowcore. */
    } else if (raddr >= env->psa && raddr < env->psa + 0x2000) {
        return raddr - env->psa;    /* Map the 0 page. */
    }
    return raddr;
}

/* Decode page table entry (normal 4KB page) */
static int mmu_translate_pte(CPUS390XState *env, target_ulong vaddr,
                             uint64_t asc, uint64_t pt_entry,
                             target_ulong *raddr, int *flags, int rw, bool exc)
{
    if (pt_entry & _PAGE_INVALID) {
        DPRINTF("%s: PTE=0x%" PRIx64 " invalid\n", __func__, pt_entry);
        trigger_page_fault(env, vaddr, PGM_PAGE_TRANS, asc, rw, exc);
        return -1;
    }
    if (pt_entry & _PAGE_RES0) {
        trigger_page_fault(env, vaddr, PGM_TRANS_SPEC, asc, rw, exc);
        return -1;
    }
    if (pt_entry & _PAGE_RO) {
        *flags &= ~PAGE_WRITE;
    }

    *raddr = pt_entry & _ASCE_ORIGIN;

    PTE_DPRINTF("%s: PTE=0x%" PRIx64 "\n", __func__, pt_entry);

    return 0;
}

#define VADDR_PX    0xff000         /* Page index bits */

/* Decode segment table entry */
static int mmu_translate_segment(CPUS390XState *env, target_ulong vaddr,
                                 uint64_t asc, uint64_t st_entry,
                                 target_ulong *raddr, int *flags, int rw,
                                 bool exc)
{
    CPUState *cs = CPU(s390_env_get_cpu(env));
    uint64_t origin, offs, pt_entry;

    if (st_entry & _SEGMENT_ENTRY_RO) {
        *flags &= ~PAGE_WRITE;
    }

    if ((st_entry & _SEGMENT_ENTRY_FC) && (env->cregs[0] & CR0_EDAT)) {
        /* Decode EDAT1 segment frame absolute address (1MB page) */
        *raddr = (st_entry & 0xfffffffffff00000ULL) | (vaddr & 0xfffff);
        PTE_DPRINTF("%s: SEG=0x%" PRIx64 "\n", __func__, st_entry);
        return 0;
    }

    /* Look up 4KB page entry */
    origin = st_entry & _SEGMENT_ENTRY_ORIGIN;
    offs  = (vaddr & VADDR_PX) >> 9;
    pt_entry = ldq_phys(cs->as, origin + offs);
    PTE_DPRINTF("%s: 0x%" PRIx64 " + 0x%" PRIx64 " => 0x%016" PRIx64 "\n",
                __func__, origin, offs, pt_entry);
    return mmu_translate_pte(env, vaddr, asc, pt_entry, raddr, flags, rw, exc);
}

/* Decode region table entries */
static int mmu_translate_region(CPUS390XState *env, target_ulong vaddr,
                                uint64_t asc, uint64_t entry, int level,
                                target_ulong *raddr, int *flags, int rw,
                                bool exc)
{
    CPUState *cs = CPU(s390_env_get_cpu(env));
    uint64_t origin, offs, new_entry;
    const int pchks[4] = {
        PGM_SEGMENT_TRANS, PGM_REG_THIRD_TRANS,
        PGM_REG_SEC_TRANS, PGM_REG_FIRST_TRANS
    };

    PTE_DPRINTF("%s: 0x%" PRIx64 "\n", __func__, entry);

    origin = entry & _REGION_ENTRY_ORIGIN;
    offs = (vaddr >> (17 + 11 * level / 4)) & 0x3ff8;

    new_entry = ldq_phys(cs->as, origin + offs);
    PTE_DPRINTF("%s: 0x%" PRIx64 " + 0x%" PRIx64 " => 0x%016" PRIx64 "\n",
                __func__, origin, offs, new_entry);

    if ((new_entry & _REGION_ENTRY_INV) != 0) {
        DPRINTF("%s: invalid region\n", __func__);
        trigger_page_fault(env, vaddr, pchks[level / 4], asc, rw, exc);
        return -1;
    }

    if ((new_entry & _REGION_ENTRY_TYPE_MASK) != level) {
        trigger_page_fault(env, vaddr, PGM_TRANS_SPEC, asc, rw, exc);
        return -1;
    }

    if (level == _ASCE_TYPE_SEGMENT) {
        return mmu_translate_segment(env, vaddr, asc, new_entry, raddr, flags,
                                     rw, exc);
    }

    /* Check region table offset and length */
    offs = (vaddr >> (28 + 11 * (level - 4) / 4)) & 3;
    if (offs < ((new_entry & _REGION_ENTRY_TF) >> 6)
        || offs > (new_entry & _REGION_ENTRY_LENGTH)) {
        DPRINTF("%s: invalid offset or len (%lx)\n", __func__, new_entry);
        trigger_page_fault(env, vaddr, pchks[level / 4 - 1], asc, rw, exc);
        return -1;
    }

    if ((env->cregs[0] & CR0_EDAT) && (new_entry & _REGION_ENTRY_RO)) {
        *flags &= ~PAGE_WRITE;
    }

    /* yet another region */
    return mmu_translate_region(env, vaddr, asc, new_entry, level - 4,
                                raddr, flags, rw, exc);
}

static int mmu_translate_asce(CPUS390XState *env, target_ulong vaddr,
                              uint64_t asc, uint64_t asce, target_ulong *raddr,
                              int *flags, int rw, bool exc)
{
    int level;
    int r;

    if (asce & _ASCE_REAL_SPACE) {
        /* direct mapping */
        *raddr = vaddr;
        return 0;
    }

    level = asce & _ASCE_TYPE_MASK;
    switch (level) {
    case _ASCE_TYPE_REGION1:
        if ((vaddr >> 62) > (asce & _ASCE_TABLE_LENGTH)) {
            trigger_page_fault(env, vaddr, PGM_REG_FIRST_TRANS, asc, rw, exc);
            return -1;
        }
        break;
    case _ASCE_TYPE_REGION2:
        if (vaddr & 0xffe0000000000000ULL) {
            DPRINTF("%s: vaddr doesn't fit 0x%16" PRIx64
                    " 0xffe0000000000000ULL\n", __func__, vaddr);
            trigger_page_fault(env, vaddr, PGM_ASCE_TYPE, asc, rw, exc);
            return -1;
        }
        if ((vaddr >> 51 & 3) > (asce & _ASCE_TABLE_LENGTH)) {
            trigger_page_fault(env, vaddr, PGM_REG_SEC_TRANS, asc, rw, exc);
            return -1;
        }
        break;
    case _ASCE_TYPE_REGION3:
        if (vaddr & 0xfffffc0000000000ULL) {
            DPRINTF("%s: vaddr doesn't fit 0x%16" PRIx64
                    " 0xfffffc0000000000ULL\n", __func__, vaddr);
            trigger_page_fault(env, vaddr, PGM_ASCE_TYPE, asc, rw, exc);
            return -1;
        }
        if ((vaddr >> 40 & 3) > (asce & _ASCE_TABLE_LENGTH)) {
            trigger_page_fault(env, vaddr, PGM_REG_THIRD_TRANS, asc, rw, exc);
            return -1;
        }
        break;
    case _ASCE_TYPE_SEGMENT:
        if (vaddr & 0xffffffff80000000ULL) {
            DPRINTF("%s: vaddr doesn't fit 0x%16" PRIx64
                    " 0xffffffff80000000ULL\n", __func__, vaddr);
            trigger_page_fault(env, vaddr, PGM_ASCE_TYPE, asc, rw, exc);
            return -1;
        }
        if ((vaddr >> 29 & 3) > (asce & _ASCE_TABLE_LENGTH)) {
            trigger_page_fault(env, vaddr, PGM_SEGMENT_TRANS, asc, rw, exc);
            return -1;
        }
        break;
    }

    r = mmu_translate_region(env, vaddr, asc, asce, level, raddr, flags, rw,
                             exc);
    if (rw == MMU_DATA_STORE && !(*flags & PAGE_WRITE)) {
        trigger_prot_fault(env, vaddr, asc, rw, exc);
        return -1;
    }

    return r;
}

/**
 * Translate a virtual (logical) address into a physical (absolute) address.
 * @param vaddr  the virtual address
 * @param rw     0 = read, 1 = write, 2 = code fetch
 * @param asc    address space control (one of the PSW_ASC_* modes)
 * @param raddr  the translated address is stored to this pointer
 * @param flags  the PAGE_READ/WRITE/EXEC flags are stored to this pointer
 * @param exc    true = inject a program check if a fault occurred
 * @return       0 if the translation was successful, -1 if a fault occurred
 */
int mmu_translate(CPUS390XState *env, target_ulong vaddr, int rw, uint64_t asc,
                  target_ulong *raddr, int *flags, bool exc)
{
    static S390SKeysState *ss;
    static S390SKeysClass *skeyclass;
    int r = -1;
    uint8_t key;

    if (unlikely(!ss)) {
        ss = s390_get_skeys_device();
        skeyclass = S390_SKEYS_GET_CLASS(ss);
    }

    *flags = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    vaddr &= TARGET_PAGE_MASK;

    if (!(env->psw.mask & PSW_MASK_DAT)) {
        *raddr = vaddr;
        r = 0;
        goto out;
    }

    switch (asc) {
    case PSW_ASC_PRIMARY:
        PTE_DPRINTF("%s: asc=primary\n", __func__);
        r = mmu_translate_asce(env, vaddr, asc, env->cregs[1], raddr, flags,
                               rw, exc);
        break;
    case PSW_ASC_HOME:
        PTE_DPRINTF("%s: asc=home\n", __func__);
        r = mmu_translate_asce(env, vaddr, asc, env->cregs[13], raddr, flags,
                               rw, exc);
        break;
    case PSW_ASC_SECONDARY:
        PTE_DPRINTF("%s: asc=secondary\n", __func__);
        /*
         * Instruction: Primary
         * Data: Secondary
         */
        if (rw == MMU_INST_FETCH) {
            r = mmu_translate_asce(env, vaddr, PSW_ASC_PRIMARY, env->cregs[1],
                                   raddr, flags, rw, exc);
            *flags &= ~(PAGE_READ | PAGE_WRITE);
        } else {
            r = mmu_translate_asce(env, vaddr, PSW_ASC_SECONDARY, env->cregs[7],
                                   raddr, flags, rw, exc);
            *flags &= ~(PAGE_EXEC);
        }
        break;
    case PSW_ASC_ACCREG:
    default:
        hw_error("guest switched to unknown asc mode\n");
        break;
    }

 out:
    /* Convert real address -> absolute address */
    *raddr = mmu_real2abs(env, *raddr);

    if (r == 0 && *raddr < ram_size) {
        if (skeyclass->get_skeys(ss, *raddr / TARGET_PAGE_SIZE, 1, &key)) {
            trace_get_skeys_nonzero(r);
            return 0;
        }

        if (*flags & PAGE_READ) {
            key |= SK_R;
        }

        if (*flags & PAGE_WRITE) {
            key |= SK_C;
        }

        if (skeyclass->set_skeys(ss, *raddr / TARGET_PAGE_SIZE, 1, &key)) {
            trace_set_skeys_nonzero(r);
            return 0;
        }
    }

    return r;
}

/**
 * lowprot_enabled: Check whether low-address protection is enabled
 */
static bool lowprot_enabled(const CPUS390XState *env)
{
    if (!(env->cregs[0] & CR0_LOWPROT)) {
        return false;
    }
    if (!(env->psw.mask & PSW_MASK_DAT)) {
        return true;
    }

    /* Check the private-space control bit */
    switch (env->psw.mask & PSW_MASK_ASC) {
    case PSW_ASC_PRIMARY:
        return !(env->cregs[1] & _ASCE_PRIVATE_SPACE);
    case PSW_ASC_SECONDARY:
        return !(env->cregs[7] & _ASCE_PRIVATE_SPACE);
    case PSW_ASC_HOME:
        return !(env->cregs[13] & _ASCE_PRIVATE_SPACE);
    default:
        /* We don't support access register mode */
        error_report("unsupported addressing mode");
        exit(1);
    }
}

/**
 * translate_pages: Translate a set of consecutive logical page addresses
 * to absolute addresses
 */
static int translate_pages(S390CPU *cpu, vaddr addr, int nr_pages,
                           target_ulong *pages, bool is_write)
{
    bool lowprot = is_write && lowprot_enabled(&cpu->env);
    uint64_t asc = cpu->env.psw.mask & PSW_MASK_ASC;
    CPUS390XState *env = &cpu->env;
    int ret, i, pflags;

    for (i = 0; i < nr_pages; i++) {
        /* Low-address protection? */
        if (lowprot && (addr < 512 || (addr >= 4096 && addr < 4096 + 512))) {
            trigger_access_exception(env, PGM_PROTECTION, ILEN_LATER_INC, 0);
            return -EACCES;
        }
        ret = mmu_translate(env, addr, is_write, asc, &pages[i], &pflags, true);
        if (ret) {
            return ret;
        }
        if (!address_space_access_valid(&address_space_memory, pages[i],
                                        TARGET_PAGE_SIZE, is_write)) {
            program_interrupt(env, PGM_ADDRESSING, 0);
            return -EFAULT;
        }
        addr += TARGET_PAGE_SIZE;
    }

    return 0;
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
 */
int s390_cpu_virt_mem_rw(S390CPU *cpu, vaddr laddr, uint8_t ar, void *hostbuf,
                         int len, bool is_write)
{
    int currlen, nr_pages, i;
    target_ulong *pages;
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

    ret = translate_pages(cpu, laddr, nr_pages, pages, is_write);
    if (ret == 0 && hostbuf != NULL) {
        /* Copy data by stepping through the area page by page */
        for (i = 0; i < nr_pages; i++) {
            currlen = MIN(len, TARGET_PAGE_SIZE - (laddr % TARGET_PAGE_SIZE));
            cpu_physical_memory_rw(pages[i] | (laddr & ~TARGET_PAGE_MASK),
                                   hostbuf, currlen, is_write);
            laddr += currlen;
            hostbuf += currlen;
            len -= currlen;
        }
    }

    g_free(pages);
    return ret;
}
