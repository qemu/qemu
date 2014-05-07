/*
 *  S/390 helpers
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2011 Alexander Graf
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "exec/gdbstub.h"
#include "qemu/timer.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/sysemu.h"
#endif

//#define DEBUG_S390
//#define DEBUG_S390_PTE
//#define DEBUG_S390_STDOUT

#ifdef DEBUG_S390
#ifdef DEBUG_S390_STDOUT
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); \
         qemu_log(fmt, ##__VA_ARGS__); } while (0)
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

#ifndef CONFIG_USER_ONLY
void s390x_tod_timer(void *opaque)
{
    S390CPU *cpu = opaque;
    CPUS390XState *env = &cpu->env;

    env->pending_int |= INTERRUPT_TOD;
    cpu_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
}

void s390x_cpu_timer(void *opaque)
{
    S390CPU *cpu = opaque;
    CPUS390XState *env = &cpu->env;

    env->pending_int |= INTERRUPT_CPUTIMER;
    cpu_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
}
#endif

S390CPU *cpu_s390x_init(const char *cpu_model)
{
    S390CPU *cpu;

    cpu = S390_CPU(object_new(TYPE_S390_CPU));

    object_property_set_bool(OBJECT(cpu), true, "realized", NULL);

    return cpu;
}

#if defined(CONFIG_USER_ONLY)

void s390_cpu_do_interrupt(CPUState *cs)
{
    cs->exception_index = -1;
}

int s390_cpu_handle_mmu_fault(CPUState *cs, vaddr address,
                              int rw, int mmu_idx)
{
    S390CPU *cpu = S390_CPU(cs);

    cs->exception_index = EXCP_PGM;
    cpu->env.int_pgm_code = PGM_ADDRESSING;
    /* On real machines this value is dropped into LowMem.  Since this
       is userland, simply put this someplace that cpu_loop can find it.  */
    cpu->env.__excp_addr = address;
    return 1;
}

#else /* !CONFIG_USER_ONLY */

/* Ensure to exit the TB after this call! */
static void trigger_pgm_exception(CPUS390XState *env, uint32_t code,
                                  uint32_t ilen)
{
    CPUState *cs = CPU(s390_env_get_cpu(env));

    cs->exception_index = EXCP_PGM;
    env->int_pgm_code = code;
    env->int_pgm_ilen = ilen;
}

static int trans_bits(CPUS390XState *env, uint64_t mode)
{
    S390CPU *cpu = s390_env_get_cpu(env);
    int bits = 0;

    switch (mode) {
    case PSW_ASC_PRIMARY:
        bits = 1;
        break;
    case PSW_ASC_SECONDARY:
        bits = 2;
        break;
    case PSW_ASC_HOME:
        bits = 3;
        break;
    default:
        cpu_abort(CPU(cpu), "unknown asc mode\n");
        break;
    }

    return bits;
}

static void trigger_prot_fault(CPUS390XState *env, target_ulong vaddr,
                               uint64_t mode)
{
    CPUState *cs = CPU(s390_env_get_cpu(env));
    int ilen = ILEN_LATER_INC;
    int bits = trans_bits(env, mode) | 4;

    DPRINTF("%s: vaddr=%016" PRIx64 " bits=%d\n", __func__, vaddr, bits);

    stq_phys(cs->as,
             env->psa + offsetof(LowCore, trans_exc_code), vaddr | bits);
    trigger_pgm_exception(env, PGM_PROTECTION, ilen);
}

static void trigger_page_fault(CPUS390XState *env, target_ulong vaddr,
                               uint32_t type, uint64_t asc, int rw)
{
    CPUState *cs = CPU(s390_env_get_cpu(env));
    int ilen = ILEN_LATER;
    int bits = trans_bits(env, asc);

    /* Code accesses have an undefined ilc.  */
    if (rw == 2) {
        ilen = 2;
    }

    DPRINTF("%s: vaddr=%016" PRIx64 " bits=%d\n", __func__, vaddr, bits);

    stq_phys(cs->as,
             env->psa + offsetof(LowCore, trans_exc_code), vaddr | bits);
    trigger_pgm_exception(env, type, ilen);
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
                             uint64_t asc, uint64_t asce,
                             target_ulong *raddr, int *flags, int rw)
{
    if (asce & _PAGE_INVALID) {
        DPRINTF("%s: PTE=0x%" PRIx64 " invalid\n", __func__, asce);
        trigger_page_fault(env, vaddr, PGM_PAGE_TRANS, asc, rw);
        return -1;
    }

    if (asce & _PAGE_RO) {
        *flags &= ~PAGE_WRITE;
    }

    *raddr = asce & _ASCE_ORIGIN;

    PTE_DPRINTF("%s: PTE=0x%" PRIx64 "\n", __func__, asce);

    return 0;
}

/* Decode EDAT1 segment frame absolute address (1MB page) */
static int mmu_translate_sfaa(CPUS390XState *env, target_ulong vaddr,
                              uint64_t asc, uint64_t asce, target_ulong *raddr,
                              int *flags, int rw)
{
    if (asce & _SEGMENT_ENTRY_INV) {
        DPRINTF("%s: SEG=0x%" PRIx64 " invalid\n", __func__, asce);
        trigger_page_fault(env, vaddr, PGM_SEGMENT_TRANS, asc, rw);
        return -1;
    }

    if (asce & _SEGMENT_ENTRY_RO) {
        *flags &= ~PAGE_WRITE;
    }

    *raddr = (asce & 0xfffffffffff00000ULL) | (vaddr & 0xfffff);

    PTE_DPRINTF("%s: SEG=0x%" PRIx64 "\n", __func__, asce);

    return 0;
}

static int mmu_translate_asce(CPUS390XState *env, target_ulong vaddr,
                              uint64_t asc, uint64_t asce, int level,
                              target_ulong *raddr, int *flags, int rw)
{
    CPUState *cs = CPU(s390_env_get_cpu(env));
    uint64_t offs = 0;
    uint64_t origin;
    uint64_t new_asce;

    PTE_DPRINTF("%s: 0x%" PRIx64 "\n", __func__, asce);

    if (((level != _ASCE_TYPE_SEGMENT) && (asce & _REGION_ENTRY_INV)) ||
        ((level == _ASCE_TYPE_SEGMENT) && (asce & _SEGMENT_ENTRY_INV))) {
        /* XXX different regions have different faults */
        DPRINTF("%s: invalid region\n", __func__);
        trigger_page_fault(env, vaddr, PGM_SEGMENT_TRANS, asc, rw);
        return -1;
    }

    if ((level <= _ASCE_TYPE_MASK) && ((asce & _ASCE_TYPE_MASK) != level)) {
        trigger_page_fault(env, vaddr, PGM_TRANS_SPEC, asc, rw);
        return -1;
    }

    if (asce & _ASCE_REAL_SPACE) {
        /* direct mapping */

        *raddr = vaddr;
        return 0;
    }

    origin = asce & _ASCE_ORIGIN;

    switch (level) {
    case _ASCE_TYPE_REGION1 + 4:
        offs = (vaddr >> 50) & 0x3ff8;
        break;
    case _ASCE_TYPE_REGION1:
        offs = (vaddr >> 39) & 0x3ff8;
        break;
    case _ASCE_TYPE_REGION2:
        offs = (vaddr >> 28) & 0x3ff8;
        break;
    case _ASCE_TYPE_REGION3:
        offs = (vaddr >> 17) & 0x3ff8;
        break;
    case _ASCE_TYPE_SEGMENT:
        offs = (vaddr >> 9) & 0x07f8;
        origin = asce & _SEGMENT_ENTRY_ORIGIN;
        break;
    }

    /* XXX region protection flags */
    /* *flags &= ~PAGE_WRITE */

    new_asce = ldq_phys(cs->as, origin + offs);
    PTE_DPRINTF("%s: 0x%" PRIx64 " + 0x%" PRIx64 " => 0x%016" PRIx64 "\n",
                __func__, origin, offs, new_asce);

    if (level == _ASCE_TYPE_SEGMENT) {
        /* 4KB page */
        return mmu_translate_pte(env, vaddr, asc, new_asce, raddr, flags, rw);
    } else if (level - 4 == _ASCE_TYPE_SEGMENT &&
               (new_asce & _SEGMENT_ENTRY_FC) && (env->cregs[0] & CR0_EDAT)) {
        /* 1MB page */
        return mmu_translate_sfaa(env, vaddr, asc, new_asce, raddr, flags, rw);
    } else {
        /* yet another region */
        return mmu_translate_asce(env, vaddr, asc, new_asce, level - 4, raddr,
                                  flags, rw);
    }
}

static int mmu_translate_asc(CPUS390XState *env, target_ulong vaddr,
                             uint64_t asc, target_ulong *raddr, int *flags,
                             int rw)
{
    uint64_t asce = 0;
    int level, new_level;
    int r;

    switch (asc) {
    case PSW_ASC_PRIMARY:
        PTE_DPRINTF("%s: asc=primary\n", __func__);
        asce = env->cregs[1];
        break;
    case PSW_ASC_SECONDARY:
        PTE_DPRINTF("%s: asc=secondary\n", __func__);
        asce = env->cregs[7];
        break;
    case PSW_ASC_HOME:
        PTE_DPRINTF("%s: asc=home\n", __func__);
        asce = env->cregs[13];
        break;
    }

    switch (asce & _ASCE_TYPE_MASK) {
    case _ASCE_TYPE_REGION1:
        break;
    case _ASCE_TYPE_REGION2:
        if (vaddr & 0xffe0000000000000ULL) {
            DPRINTF("%s: vaddr doesn't fit 0x%16" PRIx64
                    " 0xffe0000000000000ULL\n", __func__, vaddr);
            trigger_page_fault(env, vaddr, PGM_TRANS_SPEC, asc, rw);
            return -1;
        }
        break;
    case _ASCE_TYPE_REGION3:
        if (vaddr & 0xfffffc0000000000ULL) {
            DPRINTF("%s: vaddr doesn't fit 0x%16" PRIx64
                    " 0xfffffc0000000000ULL\n", __func__, vaddr);
            trigger_page_fault(env, vaddr, PGM_TRANS_SPEC, asc, rw);
            return -1;
        }
        break;
    case _ASCE_TYPE_SEGMENT:
        if (vaddr & 0xffffffff80000000ULL) {
            DPRINTF("%s: vaddr doesn't fit 0x%16" PRIx64
                    " 0xffffffff80000000ULL\n", __func__, vaddr);
            trigger_page_fault(env, vaddr, PGM_TRANS_SPEC, asc, rw);
            return -1;
        }
        break;
    }

    /* fake level above current */
    level = asce & _ASCE_TYPE_MASK;
    new_level = level + 4;
    asce = (asce & ~_ASCE_TYPE_MASK) | (new_level & _ASCE_TYPE_MASK);

    r = mmu_translate_asce(env, vaddr, asc, asce, new_level, raddr, flags, rw);

    if ((rw == 1) && !(*flags & PAGE_WRITE)) {
        trigger_prot_fault(env, vaddr, asc);
        return -1;
    }

    return r;
}

int mmu_translate(CPUS390XState *env, target_ulong vaddr, int rw, uint64_t asc,
                  target_ulong *raddr, int *flags)
{
    int r = -1;
    uint8_t *sk;

    *flags = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    vaddr &= TARGET_PAGE_MASK;

    if (!(env->psw.mask & PSW_MASK_DAT)) {
        *raddr = vaddr;
        r = 0;
        goto out;
    }

    switch (asc) {
    case PSW_ASC_PRIMARY:
    case PSW_ASC_HOME:
        r = mmu_translate_asc(env, vaddr, asc, raddr, flags, rw);
        break;
    case PSW_ASC_SECONDARY:
        /*
         * Instruction: Primary
         * Data: Secondary
         */
        if (rw == 2) {
            r = mmu_translate_asc(env, vaddr, PSW_ASC_PRIMARY, raddr, flags,
                                  rw);
            *flags &= ~(PAGE_READ | PAGE_WRITE);
        } else {
            r = mmu_translate_asc(env, vaddr, PSW_ASC_SECONDARY, raddr, flags,
                                  rw);
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

    if (*raddr <= ram_size) {
        sk = &env->storage_keys[*raddr / TARGET_PAGE_SIZE];
        if (*flags & PAGE_READ) {
            *sk |= SK_R;
        }

        if (*flags & PAGE_WRITE) {
            *sk |= SK_C;
        }
    }

    return r;
}

int s390_cpu_handle_mmu_fault(CPUState *cs, vaddr orig_vaddr,
                              int rw, int mmu_idx)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    uint64_t asc = env->psw.mask & PSW_MASK_ASC;
    target_ulong vaddr, raddr;
    int prot;

    DPRINTF("%s: address 0x%" VADDR_PRIx " rw %d mmu_idx %d\n",
            __func__, orig_vaddr, rw, mmu_idx);

    orig_vaddr &= TARGET_PAGE_MASK;
    vaddr = orig_vaddr;

    /* 31-Bit mode */
    if (!(env->psw.mask & PSW_MASK_64)) {
        vaddr &= 0x7fffffff;
    }

    if (mmu_translate(env, vaddr, rw, asc, &raddr, &prot)) {
        /* Translation ended in exception */
        return 1;
    }

    /* check out of RAM access */
    if (raddr > (ram_size + virtio_size)) {
        DPRINTF("%s: raddr %" PRIx64 " > ram_size %" PRIx64 "\n", __func__,
                (uint64_t)raddr, (uint64_t)ram_size);
        trigger_pgm_exception(env, PGM_ADDRESSING, ILEN_LATER);
        return 1;
    }

    DPRINTF("%s: set tlb %" PRIx64 " -> %" PRIx64 " (%x)\n", __func__,
            (uint64_t)vaddr, (uint64_t)raddr, prot);

    tlb_set_page(cs, orig_vaddr, raddr, prot,
                 mmu_idx, TARGET_PAGE_SIZE);

    return 0;
}

hwaddr s390_cpu_get_phys_page_debug(CPUState *cs, vaddr vaddr)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    target_ulong raddr;
    int prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    int old_exc = cs->exception_index;
    uint64_t asc = env->psw.mask & PSW_MASK_ASC;

    /* 31-Bit mode */
    if (!(env->psw.mask & PSW_MASK_64)) {
        vaddr &= 0x7fffffff;
    }

    mmu_translate(env, vaddr, 2, asc, &raddr, &prot);
    cs->exception_index = old_exc;

    return raddr;
}

void load_psw(CPUS390XState *env, uint64_t mask, uint64_t addr)
{
    if (mask & PSW_MASK_WAIT) {
        S390CPU *cpu = s390_env_get_cpu(env);
        CPUState *cs = CPU(cpu);
        if (!(mask & (PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK))) {
            if (s390_del_running_cpu(cpu) == 0) {
#ifndef CONFIG_USER_ONLY
                qemu_system_shutdown_request();
#endif
            }
        }
        cs->halted = 1;
        cs->exception_index = EXCP_HLT;
    }

    env->psw.addr = addr;
    env->psw.mask = mask;
    env->cc_op = (mask >> 44) & 3;
}

static uint64_t get_psw_mask(CPUS390XState *env)
{
    uint64_t r;

    env->cc_op = calc_cc(env, env->cc_op, env->cc_src, env->cc_dst, env->cc_vr);

    r = env->psw.mask;
    r &= ~PSW_MASK_CC;
    assert(!(env->cc_op & ~3));
    r |= (uint64_t)env->cc_op << 44;

    return r;
}

static LowCore *cpu_map_lowcore(CPUS390XState *env)
{
    S390CPU *cpu = s390_env_get_cpu(env);
    LowCore *lowcore;
    hwaddr len = sizeof(LowCore);

    lowcore = cpu_physical_memory_map(env->psa, &len, 1);

    if (len < sizeof(LowCore)) {
        cpu_abort(CPU(cpu), "Could not map lowcore\n");
    }

    return lowcore;
}

static void cpu_unmap_lowcore(LowCore *lowcore)
{
    cpu_physical_memory_unmap(lowcore, sizeof(LowCore), 1, sizeof(LowCore));
}

void *s390_cpu_physical_memory_map(CPUS390XState *env, hwaddr addr, hwaddr *len,
                                   int is_write)
{
    hwaddr start = addr;

    /* Mind the prefix area. */
    if (addr < 8192) {
        /* Map the lowcore. */
        start += env->psa;
        *len = MIN(*len, 8192 - addr);
    } else if ((addr >= env->psa) && (addr < env->psa + 8192)) {
        /* Map the 0 page. */
        start -= env->psa;
        *len = MIN(*len, 8192 - start);
    }

    return cpu_physical_memory_map(start, len, is_write);
}

void s390_cpu_physical_memory_unmap(CPUS390XState *env, void *addr, hwaddr len,
                                    int is_write)
{
    cpu_physical_memory_unmap(addr, len, is_write, len);
}

static void do_svc_interrupt(CPUS390XState *env)
{
    uint64_t mask, addr;
    LowCore *lowcore;

    lowcore = cpu_map_lowcore(env);

    lowcore->svc_code = cpu_to_be16(env->int_svc_code);
    lowcore->svc_ilen = cpu_to_be16(env->int_svc_ilen);
    lowcore->svc_old_psw.mask = cpu_to_be64(get_psw_mask(env));
    lowcore->svc_old_psw.addr = cpu_to_be64(env->psw.addr + env->int_svc_ilen);
    mask = be64_to_cpu(lowcore->svc_new_psw.mask);
    addr = be64_to_cpu(lowcore->svc_new_psw.addr);

    cpu_unmap_lowcore(lowcore);

    load_psw(env, mask, addr);
}

static void do_program_interrupt(CPUS390XState *env)
{
    uint64_t mask, addr;
    LowCore *lowcore;
    int ilen = env->int_pgm_ilen;

    switch (ilen) {
    case ILEN_LATER:
        ilen = get_ilen(cpu_ldub_code(env, env->psw.addr));
        break;
    case ILEN_LATER_INC:
        ilen = get_ilen(cpu_ldub_code(env, env->psw.addr));
        env->psw.addr += ilen;
        break;
    default:
        assert(ilen == 2 || ilen == 4 || ilen == 6);
    }

    qemu_log_mask(CPU_LOG_INT, "%s: code=0x%x ilen=%d\n",
                  __func__, env->int_pgm_code, ilen);

    lowcore = cpu_map_lowcore(env);

    lowcore->pgm_ilen = cpu_to_be16(ilen);
    lowcore->pgm_code = cpu_to_be16(env->int_pgm_code);
    lowcore->program_old_psw.mask = cpu_to_be64(get_psw_mask(env));
    lowcore->program_old_psw.addr = cpu_to_be64(env->psw.addr);
    mask = be64_to_cpu(lowcore->program_new_psw.mask);
    addr = be64_to_cpu(lowcore->program_new_psw.addr);

    cpu_unmap_lowcore(lowcore);

    DPRINTF("%s: %x %x %" PRIx64 " %" PRIx64 "\n", __func__,
            env->int_pgm_code, ilen, env->psw.mask,
            env->psw.addr);

    load_psw(env, mask, addr);
}

#define VIRTIO_SUBCODE_64 0x0D00

static void do_ext_interrupt(CPUS390XState *env)
{
    S390CPU *cpu = s390_env_get_cpu(env);
    uint64_t mask, addr;
    LowCore *lowcore;
    ExtQueue *q;

    if (!(env->psw.mask & PSW_MASK_EXT)) {
        cpu_abort(CPU(cpu), "Ext int w/o ext mask\n");
    }

    if (env->ext_index < 0 || env->ext_index > MAX_EXT_QUEUE) {
        cpu_abort(CPU(cpu), "Ext queue overrun: %d\n", env->ext_index);
    }

    q = &env->ext_queue[env->ext_index];
    lowcore = cpu_map_lowcore(env);

    lowcore->ext_int_code = cpu_to_be16(q->code);
    lowcore->ext_params = cpu_to_be32(q->param);
    lowcore->ext_params2 = cpu_to_be64(q->param64);
    lowcore->external_old_psw.mask = cpu_to_be64(get_psw_mask(env));
    lowcore->external_old_psw.addr = cpu_to_be64(env->psw.addr);
    lowcore->cpu_addr = cpu_to_be16(env->cpu_num | VIRTIO_SUBCODE_64);
    mask = be64_to_cpu(lowcore->external_new_psw.mask);
    addr = be64_to_cpu(lowcore->external_new_psw.addr);

    cpu_unmap_lowcore(lowcore);

    env->ext_index--;
    if (env->ext_index == -1) {
        env->pending_int &= ~INTERRUPT_EXT;
    }

    DPRINTF("%s: %" PRIx64 " %" PRIx64 "\n", __func__,
            env->psw.mask, env->psw.addr);

    load_psw(env, mask, addr);
}

static void do_io_interrupt(CPUS390XState *env)
{
    S390CPU *cpu = s390_env_get_cpu(env);
    LowCore *lowcore;
    IOIntQueue *q;
    uint8_t isc;
    int disable = 1;
    int found = 0;

    if (!(env->psw.mask & PSW_MASK_IO)) {
        cpu_abort(CPU(cpu), "I/O int w/o I/O mask\n");
    }

    for (isc = 0; isc < ARRAY_SIZE(env->io_index); isc++) {
        uint64_t isc_bits;

        if (env->io_index[isc] < 0) {
            continue;
        }
        if (env->io_index[isc] > MAX_IO_QUEUE) {
            cpu_abort(CPU(cpu), "I/O queue overrun for isc %d: %d\n",
                      isc, env->io_index[isc]);
        }

        q = &env->io_queue[env->io_index[isc]][isc];
        isc_bits = ISC_TO_ISC_BITS(IO_INT_WORD_ISC(q->word));
        if (!(env->cregs[6] & isc_bits)) {
            disable = 0;
            continue;
        }
        if (!found) {
            uint64_t mask, addr;

            found = 1;
            lowcore = cpu_map_lowcore(env);

            lowcore->subchannel_id = cpu_to_be16(q->id);
            lowcore->subchannel_nr = cpu_to_be16(q->nr);
            lowcore->io_int_parm = cpu_to_be32(q->parm);
            lowcore->io_int_word = cpu_to_be32(q->word);
            lowcore->io_old_psw.mask = cpu_to_be64(get_psw_mask(env));
            lowcore->io_old_psw.addr = cpu_to_be64(env->psw.addr);
            mask = be64_to_cpu(lowcore->io_new_psw.mask);
            addr = be64_to_cpu(lowcore->io_new_psw.addr);

            cpu_unmap_lowcore(lowcore);

            env->io_index[isc]--;

            DPRINTF("%s: %" PRIx64 " %" PRIx64 "\n", __func__,
                    env->psw.mask, env->psw.addr);
            load_psw(env, mask, addr);
        }
        if (env->io_index[isc] >= 0) {
            disable = 0;
        }
        continue;
    }

    if (disable) {
        env->pending_int &= ~INTERRUPT_IO;
    }

}

static void do_mchk_interrupt(CPUS390XState *env)
{
    S390CPU *cpu = s390_env_get_cpu(env);
    uint64_t mask, addr;
    LowCore *lowcore;
    MchkQueue *q;
    int i;

    if (!(env->psw.mask & PSW_MASK_MCHECK)) {
        cpu_abort(CPU(cpu), "Machine check w/o mchk mask\n");
    }

    if (env->mchk_index < 0 || env->mchk_index > MAX_MCHK_QUEUE) {
        cpu_abort(CPU(cpu), "Mchk queue overrun: %d\n", env->mchk_index);
    }

    q = &env->mchk_queue[env->mchk_index];

    if (q->type != 1) {
        /* Don't know how to handle this... */
        cpu_abort(CPU(cpu), "Unknown machine check type %d\n", q->type);
    }
    if (!(env->cregs[14] & (1 << 28))) {
        /* CRW machine checks disabled */
        return;
    }

    lowcore = cpu_map_lowcore(env);

    for (i = 0; i < 16; i++) {
        lowcore->floating_pt_save_area[i] = cpu_to_be64(env->fregs[i].ll);
        lowcore->gpregs_save_area[i] = cpu_to_be64(env->regs[i]);
        lowcore->access_regs_save_area[i] = cpu_to_be32(env->aregs[i]);
        lowcore->cregs_save_area[i] = cpu_to_be64(env->cregs[i]);
    }
    lowcore->prefixreg_save_area = cpu_to_be32(env->psa);
    lowcore->fpt_creg_save_area = cpu_to_be32(env->fpc);
    lowcore->tod_progreg_save_area = cpu_to_be32(env->todpr);
    lowcore->cpu_timer_save_area[0] = cpu_to_be32(env->cputm >> 32);
    lowcore->cpu_timer_save_area[1] = cpu_to_be32((uint32_t)env->cputm);
    lowcore->clock_comp_save_area[0] = cpu_to_be32(env->ckc >> 32);
    lowcore->clock_comp_save_area[1] = cpu_to_be32((uint32_t)env->ckc);

    lowcore->mcck_interruption_code[0] = cpu_to_be32(0x00400f1d);
    lowcore->mcck_interruption_code[1] = cpu_to_be32(0x40330000);
    lowcore->mcck_old_psw.mask = cpu_to_be64(get_psw_mask(env));
    lowcore->mcck_old_psw.addr = cpu_to_be64(env->psw.addr);
    mask = be64_to_cpu(lowcore->mcck_new_psw.mask);
    addr = be64_to_cpu(lowcore->mcck_new_psw.addr);

    cpu_unmap_lowcore(lowcore);

    env->mchk_index--;
    if (env->mchk_index == -1) {
        env->pending_int &= ~INTERRUPT_MCHK;
    }

    DPRINTF("%s: %" PRIx64 " %" PRIx64 "\n", __func__,
            env->psw.mask, env->psw.addr);

    load_psw(env, mask, addr);
}

void s390_cpu_do_interrupt(CPUState *cs)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;

    qemu_log_mask(CPU_LOG_INT, "%s: %d at pc=%" PRIx64 "\n",
                  __func__, cs->exception_index, env->psw.addr);

    s390_add_running_cpu(cpu);
    /* handle machine checks */
    if ((env->psw.mask & PSW_MASK_MCHECK) &&
        (cs->exception_index == -1)) {
        if (env->pending_int & INTERRUPT_MCHK) {
            cs->exception_index = EXCP_MCHK;
        }
    }
    /* handle external interrupts */
    if ((env->psw.mask & PSW_MASK_EXT) &&
        cs->exception_index == -1) {
        if (env->pending_int & INTERRUPT_EXT) {
            /* code is already in env */
            cs->exception_index = EXCP_EXT;
        } else if (env->pending_int & INTERRUPT_TOD) {
            cpu_inject_ext(cpu, 0x1004, 0, 0);
            cs->exception_index = EXCP_EXT;
            env->pending_int &= ~INTERRUPT_EXT;
            env->pending_int &= ~INTERRUPT_TOD;
        } else if (env->pending_int & INTERRUPT_CPUTIMER) {
            cpu_inject_ext(cpu, 0x1005, 0, 0);
            cs->exception_index = EXCP_EXT;
            env->pending_int &= ~INTERRUPT_EXT;
            env->pending_int &= ~INTERRUPT_TOD;
        }
    }
    /* handle I/O interrupts */
    if ((env->psw.mask & PSW_MASK_IO) &&
        (cs->exception_index == -1)) {
        if (env->pending_int & INTERRUPT_IO) {
            cs->exception_index = EXCP_IO;
        }
    }

    switch (cs->exception_index) {
    case EXCP_PGM:
        do_program_interrupt(env);
        break;
    case EXCP_SVC:
        do_svc_interrupt(env);
        break;
    case EXCP_EXT:
        do_ext_interrupt(env);
        break;
    case EXCP_IO:
        do_io_interrupt(env);
        break;
    case EXCP_MCHK:
        do_mchk_interrupt(env);
        break;
    }
    cs->exception_index = -1;

    if (!env->pending_int) {
        cs->interrupt_request &= ~CPU_INTERRUPT_HARD;
    }
}

#endif /* CONFIG_USER_ONLY */
