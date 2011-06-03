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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "exec-all.h"
#include "gdbstub.h"
#include "qemu-common.h"
#include "qemu-timer.h"

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
static void s390x_tod_timer(void *opaque)
{
    CPUState *env = opaque;

    env->pending_int |= INTERRUPT_TOD;
    cpu_interrupt(env, CPU_INTERRUPT_HARD);
}

static void s390x_cpu_timer(void *opaque)
{
    CPUState *env = opaque;

    env->pending_int |= INTERRUPT_CPUTIMER;
    cpu_interrupt(env, CPU_INTERRUPT_HARD);
}
#endif

CPUS390XState *cpu_s390x_init(const char *cpu_model)
{
    CPUS390XState *env;
#if !defined (CONFIG_USER_ONLY)
    struct tm tm;
#endif
    static int inited = 0;
    static int cpu_num = 0;

    env = qemu_mallocz(sizeof(CPUS390XState));
    cpu_exec_init(env);
    if (!inited) {
        inited = 1;
        s390x_translate_init();
    }

#if !defined(CONFIG_USER_ONLY)
    qemu_get_timedate(&tm, 0);
    env->tod_offset = TOD_UNIX_EPOCH +
                      (time2tod(mktimegm(&tm)) * 1000000000ULL);
    env->tod_basetime = 0;
    env->tod_timer = qemu_new_timer_ns(vm_clock, s390x_tod_timer, env);
    env->cpu_timer = qemu_new_timer_ns(vm_clock, s390x_cpu_timer, env);
#endif
    env->cpu_model_str = cpu_model;
    env->cpu_num = cpu_num++;
    env->ext_index = -1;
    cpu_reset(env);
    qemu_init_vcpu(env);
    return env;
}

#if defined(CONFIG_USER_ONLY)

void do_interrupt (CPUState *env)
{
    env->exception_index = -1;
}

int cpu_s390x_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int mmu_idx, int is_softmmu)
{
    /* fprintf(stderr,"%s: address 0x%lx rw %d mmu_idx %d is_softmmu %d\n",
            __FUNCTION__, address, rw, mmu_idx, is_softmmu); */
    env->exception_index = EXCP_ADDR;
    env->__excp_addr = address; /* FIXME: find out how this works on a real machine */
    return 1;
}

#endif /* CONFIG_USER_ONLY */

void cpu_reset(CPUS390XState *env)
{
    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", env->cpu_index);
        log_cpu_state(env, 0);
    }

    memset(env, 0, offsetof(CPUS390XState, breakpoints));
    /* FIXME: reset vector? */
    tlb_flush(env, 1);
}

#ifndef CONFIG_USER_ONLY

/* Ensure to exit the TB after this call! */
static void trigger_pgm_exception(CPUState *env, uint32_t code, uint32_t ilc)
{
    env->exception_index = EXCP_PGM;
    env->int_pgm_code = code;
    env->int_pgm_ilc = ilc;
}

static int trans_bits(CPUState *env, uint64_t mode)
{
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
        cpu_abort(env, "unknown asc mode\n");
        break;
    }

    return bits;
}

static void trigger_prot_fault(CPUState *env, target_ulong vaddr, uint64_t mode)
{
    int ilc = ILC_LATER_INC_2;
    int bits = trans_bits(env, mode) | 4;

    DPRINTF("%s: vaddr=%016" PRIx64 " bits=%d\n", __FUNCTION__, vaddr, bits);

    stq_phys(env->psa + offsetof(LowCore, trans_exc_code), vaddr | bits);
    trigger_pgm_exception(env, PGM_PROTECTION, ilc);
}

static void trigger_page_fault(CPUState *env, target_ulong vaddr, uint32_t type,
                               uint64_t asc, int rw)
{
    int ilc = ILC_LATER;
    int bits = trans_bits(env, asc);

    if (rw == 2) {
        /* code has is undefined ilc */
        ilc = 2;
    }

    DPRINTF("%s: vaddr=%016" PRIx64 " bits=%d\n", __FUNCTION__, vaddr, bits);

    stq_phys(env->psa + offsetof(LowCore, trans_exc_code), vaddr | bits);
    trigger_pgm_exception(env, type, ilc);
}

static int mmu_translate_asce(CPUState *env, target_ulong vaddr, uint64_t asc,
                              uint64_t asce, int level, target_ulong *raddr,
                              int *flags, int rw)
{
    uint64_t offs = 0;
    uint64_t origin;
    uint64_t new_asce;

    PTE_DPRINTF("%s: 0x%" PRIx64 "\n", __FUNCTION__, asce);

    if (((level != _ASCE_TYPE_SEGMENT) && (asce & _REGION_ENTRY_INV)) ||
        ((level == _ASCE_TYPE_SEGMENT) && (asce & _SEGMENT_ENTRY_INV))) {
        /* XXX different regions have different faults */
        DPRINTF("%s: invalid region\n", __FUNCTION__);
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

    new_asce = ldq_phys(origin + offs);
    PTE_DPRINTF("%s: 0x%" PRIx64 " + 0x%" PRIx64 " => 0x%016" PRIx64 "\n",
                __FUNCTION__, origin, offs, new_asce);

    if (level != _ASCE_TYPE_SEGMENT) {
        /* yet another region */
        return mmu_translate_asce(env, vaddr, asc, new_asce, level - 4, raddr,
                                  flags, rw);
    }

    /* PTE */
    if (new_asce & _PAGE_INVALID) {
        DPRINTF("%s: PTE=0x%" PRIx64 " invalid\n", __FUNCTION__, new_asce);
        trigger_page_fault(env, vaddr, PGM_PAGE_TRANS, asc, rw);
        return -1;
    }

    if (new_asce & _PAGE_RO) {
        *flags &= ~PAGE_WRITE;
    }

    *raddr = new_asce & _ASCE_ORIGIN;

    PTE_DPRINTF("%s: PTE=0x%" PRIx64 "\n", __FUNCTION__, new_asce);

    return 0;
}

static int mmu_translate_asc(CPUState *env, target_ulong vaddr, uint64_t asc,
                             target_ulong *raddr, int *flags, int rw)
{
    uint64_t asce = 0;
    int level, new_level;
    int r;

    switch (asc) {
    case PSW_ASC_PRIMARY:
        PTE_DPRINTF("%s: asc=primary\n", __FUNCTION__);
        asce = env->cregs[1];
        break;
    case PSW_ASC_SECONDARY:
        PTE_DPRINTF("%s: asc=secondary\n", __FUNCTION__);
        asce = env->cregs[7];
        break;
    case PSW_ASC_HOME:
        PTE_DPRINTF("%s: asc=home\n", __FUNCTION__);
        asce = env->cregs[13];
        break;
    }

    switch (asce & _ASCE_TYPE_MASK) {
    case _ASCE_TYPE_REGION1:
        break;
    case _ASCE_TYPE_REGION2:
        if (vaddr & 0xffe0000000000000ULL) {
            DPRINTF("%s: vaddr doesn't fit 0x%16" PRIx64
                        " 0xffe0000000000000ULL\n", __FUNCTION__,
                        vaddr);
            trigger_page_fault(env, vaddr, PGM_TRANS_SPEC, asc, rw);
            return -1;
        }
        break;
    case _ASCE_TYPE_REGION3:
        if (vaddr & 0xfffffc0000000000ULL) {
            DPRINTF("%s: vaddr doesn't fit 0x%16" PRIx64
                        " 0xfffffc0000000000ULL\n", __FUNCTION__,
                        vaddr);
            trigger_page_fault(env, vaddr, PGM_TRANS_SPEC, asc, rw);
            return -1;
        }
        break;
    case _ASCE_TYPE_SEGMENT:
        if (vaddr & 0xffffffff80000000ULL) {
            DPRINTF("%s: vaddr doesn't fit 0x%16" PRIx64
                        " 0xffffffff80000000ULL\n", __FUNCTION__,
                        vaddr);
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

int mmu_translate(CPUState *env, target_ulong vaddr, int rw, uint64_t asc,
                  target_ulong *raddr, int *flags)
{
    int r = -1;

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
    if (*raddr < 0x2000) {
        *raddr = *raddr + env->psa;
    }

    return r;
}

int cpu_s390x_handle_mmu_fault (CPUState *env, target_ulong _vaddr, int rw,
                                int mmu_idx, int is_softmmu)
{
    uint64_t asc = env->psw.mask & PSW_MASK_ASC;
    target_ulong vaddr, raddr;
    int prot;

    DPRINTF("%s: address 0x%" PRIx64 " rw %d mmu_idx %d is_softmmu %d\n",
            __FUNCTION__, _vaddr, rw, mmu_idx, is_softmmu);

    _vaddr &= TARGET_PAGE_MASK;
    vaddr = _vaddr;

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
        DPRINTF("%s: aaddr %" PRIx64 " > ram_size %" PRIx64 "\n", __FUNCTION__,
                (uint64_t)aaddr, (uint64_t)ram_size);
        trigger_pgm_exception(env, PGM_ADDRESSING, ILC_LATER);
        return 1;
    }

    DPRINTF("%s: set tlb %" PRIx64 " -> %" PRIx64 " (%x)\n", __FUNCTION__,
            (uint64_t)vaddr, (uint64_t)raddr, prot);

    tlb_set_page(env, _vaddr, raddr, prot,
                 mmu_idx, TARGET_PAGE_SIZE);

    return 0;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong vaddr)
{
    target_ulong raddr;
    int prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    int old_exc = env->exception_index;
    uint64_t asc = env->psw.mask & PSW_MASK_ASC;

    /* 31-Bit mode */
    if (!(env->psw.mask & PSW_MASK_64)) {
        vaddr &= 0x7fffffff;
    }

    mmu_translate(env, vaddr, 2, asc, &raddr, &prot);
    env->exception_index = old_exc;

    return raddr;
}

void load_psw(CPUState *env, uint64_t mask, uint64_t addr)
{
    if (mask & PSW_MASK_WAIT) {
        env->halted = 1;
        env->exception_index = EXCP_HLT;
        if (!(mask & (PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK))) {
            /* XXX disabled wait state - CPU is dead */
        }
    }

    env->psw.addr = addr;
    env->psw.mask = mask;
    env->cc_op = (mask >> 13) & 3;
}

static uint64_t get_psw_mask(CPUState *env)
{
    uint64_t r = env->psw.mask;

    env->cc_op = calc_cc(env, env->cc_op, env->cc_src, env->cc_dst, env->cc_vr);

    r &= ~(3ULL << 13);
    assert(!(env->cc_op & ~3));
    r |= env->cc_op << 13;

    return r;
}

static void do_svc_interrupt(CPUState *env)
{
    uint64_t mask, addr;
    LowCore *lowcore;
    target_phys_addr_t len = TARGET_PAGE_SIZE;

    lowcore = cpu_physical_memory_map(env->psa, &len, 1);

    lowcore->svc_code = cpu_to_be16(env->int_svc_code);
    lowcore->svc_ilc = cpu_to_be16(env->int_svc_ilc);
    lowcore->svc_old_psw.mask = cpu_to_be64(get_psw_mask(env));
    lowcore->svc_old_psw.addr = cpu_to_be64(env->psw.addr + (env->int_svc_ilc));
    mask = be64_to_cpu(lowcore->svc_new_psw.mask);
    addr = be64_to_cpu(lowcore->svc_new_psw.addr);

    cpu_physical_memory_unmap(lowcore, len, 1, len);

    load_psw(env, mask, addr);
}

static void do_program_interrupt(CPUState *env)
{
    uint64_t mask, addr;
    LowCore *lowcore;
    target_phys_addr_t len = TARGET_PAGE_SIZE;
    int ilc = env->int_pgm_ilc;

    switch (ilc) {
    case ILC_LATER:
        ilc = get_ilc(ldub_code(env->psw.addr));
        break;
    case ILC_LATER_INC:
        ilc = get_ilc(ldub_code(env->psw.addr));
        env->psw.addr += ilc * 2;
        break;
    case ILC_LATER_INC_2:
        ilc = get_ilc(ldub_code(env->psw.addr)) * 2;
        env->psw.addr += ilc;
        break;
    }

    qemu_log("%s: code=0x%x ilc=%d\n", __FUNCTION__, env->int_pgm_code, ilc);

    lowcore = cpu_physical_memory_map(env->psa, &len, 1);

    lowcore->pgm_ilc = cpu_to_be16(ilc);
    lowcore->pgm_code = cpu_to_be16(env->int_pgm_code);
    lowcore->program_old_psw.mask = cpu_to_be64(get_psw_mask(env));
    lowcore->program_old_psw.addr = cpu_to_be64(env->psw.addr);
    mask = be64_to_cpu(lowcore->program_new_psw.mask);
    addr = be64_to_cpu(lowcore->program_new_psw.addr);

    cpu_physical_memory_unmap(lowcore, len, 1, len);

    DPRINTF("%s: %x %x %" PRIx64 " %" PRIx64 "\n", __FUNCTION__,
            env->int_pgm_code, ilc, env->psw.mask,
            env->psw.addr);

    load_psw(env, mask, addr);
}

#define VIRTIO_SUBCODE_64 0x0D00

static void do_ext_interrupt(CPUState *env)
{
    uint64_t mask, addr;
    LowCore *lowcore;
    target_phys_addr_t len = TARGET_PAGE_SIZE;
    ExtQueue *q;

    if (!(env->psw.mask & PSW_MASK_EXT)) {
        cpu_abort(env, "Ext int w/o ext mask\n");
    }

    if (env->ext_index < 0 || env->ext_index > MAX_EXT_QUEUE) {
        cpu_abort(env, "Ext queue overrun: %d\n", env->ext_index);
    }

    q = &env->ext_queue[env->ext_index];
    lowcore = cpu_physical_memory_map(env->psa, &len, 1);

    lowcore->ext_int_code = cpu_to_be16(q->code);
    lowcore->ext_params = cpu_to_be32(q->param);
    lowcore->ext_params2 = cpu_to_be64(q->param64);
    lowcore->external_old_psw.mask = cpu_to_be64(get_psw_mask(env));
    lowcore->external_old_psw.addr = cpu_to_be64(env->psw.addr);
    lowcore->cpu_addr = cpu_to_be16(env->cpu_num | VIRTIO_SUBCODE_64);
    mask = be64_to_cpu(lowcore->external_new_psw.mask);
    addr = be64_to_cpu(lowcore->external_new_psw.addr);

    cpu_physical_memory_unmap(lowcore, len, 1, len);

    env->ext_index--;
    if (env->ext_index == -1) {
        env->pending_int &= ~INTERRUPT_EXT;
    }

    DPRINTF("%s: %" PRIx64 " %" PRIx64 "\n", __FUNCTION__,
            env->psw.mask, env->psw.addr);

    load_psw(env, mask, addr);
}

void do_interrupt (CPUState *env)
{
    qemu_log("%s: %d at pc=%" PRIx64 "\n", __FUNCTION__, env->exception_index,
             env->psw.addr);

    /* handle external interrupts */
    if ((env->psw.mask & PSW_MASK_EXT) &&
        env->exception_index == -1) {
        if (env->pending_int & INTERRUPT_EXT) {
            /* code is already in env */
            env->exception_index = EXCP_EXT;
        } else if (env->pending_int & INTERRUPT_TOD) {
            cpu_inject_ext(env, 0x1004, 0, 0);
            env->exception_index = EXCP_EXT;
            env->pending_int &= ~INTERRUPT_EXT;
            env->pending_int &= ~INTERRUPT_TOD;
        } else if (env->pending_int & INTERRUPT_CPUTIMER) {
            cpu_inject_ext(env, 0x1005, 0, 0);
            env->exception_index = EXCP_EXT;
            env->pending_int &= ~INTERRUPT_EXT;
            env->pending_int &= ~INTERRUPT_TOD;
        }
    }

    switch (env->exception_index) {
    case EXCP_PGM:
        do_program_interrupt(env);
        break;
    case EXCP_SVC:
        do_svc_interrupt(env);
        break;
    case EXCP_EXT:
        do_ext_interrupt(env);
        break;
    }
    env->exception_index = -1;

    if (!env->pending_int) {
        env->interrupt_request &= ~CPU_INTERRUPT_HARD;
    }
}

#endif /* CONFIG_USER_ONLY */
