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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "qemu/timer.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "hw/s390x/ioinst.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/sysemu.h"
#endif

//#define DEBUG_S390
//#define DEBUG_S390_STDOUT

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

S390CPU *cpu_s390x_create(const char *cpu_model, Error **errp)
{
    S390CPU *cpu;

    cpu = S390_CPU(object_new(TYPE_S390_CPU));

    return cpu;
}

S390CPU *s390x_new_cpu(const char *cpu_model, int64_t id, Error **errp)
{
    S390CPU *cpu;
    Error *err = NULL;

    cpu = cpu_s390x_create(cpu_model, &err);
    if (err != NULL) {
        goto out;
    }

    object_property_set_int(OBJECT(cpu), id, "id", &err);
    if (err != NULL) {
        goto out;
    }
    object_property_set_bool(OBJECT(cpu), true, "realized", &err);

out:
    if (err) {
        error_propagate(errp, err);
        object_unref(OBJECT(cpu));
        cpu = NULL;
    }
    return cpu;
}

S390CPU *cpu_s390x_init(const char *cpu_model)
{
    Error *err = NULL;
    S390CPU *cpu;
    /* Use to track CPU ID for linux-user only */
    static int64_t next_cpu_id;

    cpu = s390x_new_cpu(cpu_model, next_cpu_id++, &err);
    if (err) {
        error_report_err(err);
    }
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
void trigger_pgm_exception(CPUS390XState *env, uint32_t code, uint32_t ilen)
{
    CPUState *cs = CPU(s390_env_get_cpu(env));

    cs->exception_index = EXCP_PGM;
    env->int_pgm_code = code;
    env->int_pgm_ilen = ilen;
}

int s390_cpu_handle_mmu_fault(CPUState *cs, vaddr orig_vaddr,
                              int rw, int mmu_idx)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    uint64_t asc = cpu_mmu_idx_to_asc(mmu_idx);
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

    if (mmu_translate(env, vaddr, rw, asc, &raddr, &prot, true)) {
        /* Translation ended in exception */
        return 1;
    }

    /* check out of RAM access */
    if (raddr > ram_size) {
        DPRINTF("%s: raddr %" PRIx64 " > ram_size %" PRIx64 "\n", __func__,
                (uint64_t)raddr, (uint64_t)ram_size);
        trigger_pgm_exception(env, PGM_ADDRESSING, ILEN_LATER);
        return 1;
    }

    qemu_log_mask(CPU_LOG_MMU, "%s: set tlb %" PRIx64 " -> %" PRIx64 " (%x)\n",
            __func__, (uint64_t)vaddr, (uint64_t)raddr, prot);

    tlb_set_page(cs, orig_vaddr, raddr, prot,
                 mmu_idx, TARGET_PAGE_SIZE);

    return 0;
}

hwaddr s390_cpu_get_phys_page_debug(CPUState *cs, vaddr vaddr)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    target_ulong raddr;
    int prot;
    uint64_t asc = env->psw.mask & PSW_MASK_ASC;

    /* 31-Bit mode */
    if (!(env->psw.mask & PSW_MASK_64)) {
        vaddr &= 0x7fffffff;
    }

    if (mmu_translate(env, vaddr, MMU_INST_FETCH, asc, &raddr, &prot, false)) {
        return -1;
    }
    return raddr;
}

hwaddr s390_cpu_get_phys_addr_debug(CPUState *cs, vaddr vaddr)
{
    hwaddr phys_addr;
    target_ulong page;

    page = vaddr & TARGET_PAGE_MASK;
    phys_addr = cpu_get_phys_page_debug(cs, page);
    phys_addr += (vaddr & ~TARGET_PAGE_MASK);

    return phys_addr;
}

void load_psw(CPUS390XState *env, uint64_t mask, uint64_t addr)
{
    uint64_t old_mask = env->psw.mask;

    env->psw.addr = addr;
    env->psw.mask = mask;
    if (tcg_enabled()) {
        env->cc_op = (mask >> 44) & 3;
    }

    if ((old_mask ^ mask) & PSW_MASK_PER) {
        s390_cpu_recompute_watchpoints(CPU(s390_env_get_cpu(env)));
    }

    if (mask & PSW_MASK_WAIT) {
        S390CPU *cpu = s390_env_get_cpu(env);
        if (s390_cpu_halt(cpu) == 0) {
#ifndef CONFIG_USER_ONLY
            qemu_system_shutdown_request();
#endif
        }
    }
}

static uint64_t get_psw_mask(CPUS390XState *env)
{
    uint64_t r = env->psw.mask;

    if (tcg_enabled()) {
        env->cc_op = calc_cc(env, env->cc_op, env->cc_src, env->cc_dst,
                             env->cc_vr);

        r &= ~PSW_MASK_CC;
        assert(!(env->cc_op & ~3));
        r |= (uint64_t)env->cc_op << 44;
    }

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

void do_restart_interrupt(CPUS390XState *env)
{
    uint64_t mask, addr;
    LowCore *lowcore;

    lowcore = cpu_map_lowcore(env);

    lowcore->restart_old_psw.mask = cpu_to_be64(get_psw_mask(env));
    lowcore->restart_old_psw.addr = cpu_to_be64(env->psw.addr);
    mask = be64_to_cpu(lowcore->restart_new_psw.mask);
    addr = be64_to_cpu(lowcore->restart_new_psw.addr);

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

    /* Signal PER events with the exception.  */
    if (env->per_perc_atmid) {
        env->int_pgm_code |= PGM_PER;
        lowcore->per_address = cpu_to_be64(env->per_address);
        lowcore->per_perc_atmid = cpu_to_be16(env->per_perc_atmid);
        env->per_perc_atmid = 0;
    }

    lowcore->pgm_ilen = cpu_to_be16(ilen);
    lowcore->pgm_code = cpu_to_be16(env->int_pgm_code);
    lowcore->program_old_psw.mask = cpu_to_be64(get_psw_mask(env));
    lowcore->program_old_psw.addr = cpu_to_be64(env->psw.addr);
    mask = be64_to_cpu(lowcore->program_new_psw.mask);
    addr = be64_to_cpu(lowcore->program_new_psw.addr);
    lowcore->per_breaking_event_addr = cpu_to_be64(env->gbea);

    cpu_unmap_lowcore(lowcore);

    DPRINTF("%s: %x %x %" PRIx64 " %" PRIx64 "\n", __func__,
            env->int_pgm_code, ilen, env->psw.mask,
            env->psw.addr);

    load_psw(env, mask, addr);
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

    /* When a PER event is pending, the PER exception has to happen
       immediately after the SERVICE CALL one.  */
    if (env->per_perc_atmid) {
        env->int_pgm_code = PGM_PER;
        env->int_pgm_ilen = env->int_svc_ilen;
        do_program_interrupt(env);
    }
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

    if (env->ext_index < 0 || env->ext_index >= MAX_EXT_QUEUE) {
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
        if (env->io_index[isc] >= MAX_IO_QUEUE) {
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

    if (env->mchk_index < 0 || env->mchk_index >= MAX_MCHK_QUEUE) {
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
        lowcore->floating_pt_save_area[i] = cpu_to_be64(get_freg(env, i)->ll);
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

    s390_cpu_set_state(CPU_STATE_OPERATING, cpu);
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

bool s390_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        S390CPU *cpu = S390_CPU(cs);
        CPUS390XState *env = &cpu->env;

        if (env->psw.mask & PSW_MASK_EXT) {
            s390_cpu_do_interrupt(cs);
            return true;
        }
    }
    return false;
}

void s390_cpu_recompute_watchpoints(CPUState *cs)
{
    const int wp_flags = BP_CPU | BP_MEM_WRITE | BP_STOP_BEFORE_ACCESS;
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;

    /* We are called when the watchpoints have changed. First
       remove them all.  */
    cpu_watchpoint_remove_all(cs, BP_CPU);

    /* Return if PER is not enabled */
    if (!(env->psw.mask & PSW_MASK_PER)) {
        return;
    }

    /* Return if storage-alteration event is not enabled.  */
    if (!(env->cregs[9] & PER_CR9_EVENT_STORE)) {
        return;
    }

    if (env->cregs[10] == 0 && env->cregs[11] == -1LL) {
        /* We can't create a watchoint spanning the whole memory range, so
           split it in two parts.   */
        cpu_watchpoint_insert(cs, 0, 1ULL << 63, wp_flags, NULL);
        cpu_watchpoint_insert(cs, 1ULL << 63, 1ULL << 63, wp_flags, NULL);
    } else if (env->cregs[10] > env->cregs[11]) {
        /* The address range loops, create two watchpoints.  */
        cpu_watchpoint_insert(cs, env->cregs[10], -env->cregs[10],
                              wp_flags, NULL);
        cpu_watchpoint_insert(cs, 0, env->cregs[11] + 1, wp_flags, NULL);

    } else {
        /* Default case, create a single watchpoint.  */
        cpu_watchpoint_insert(cs, env->cregs[10],
                              env->cregs[11] - env->cregs[10] + 1,
                              wp_flags, NULL);
    }
}

void s390x_cpu_debug_excp_handler(CPUState *cs)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    CPUWatchpoint *wp_hit = cs->watchpoint_hit;

    if (wp_hit && wp_hit->flags & BP_CPU) {
        /* FIXME: When the storage-alteration-space control bit is set,
           the exception should only be triggered if the memory access
           is done using an address space with the storage-alteration-event
           bit set.  We have no way to detect that with the current
           watchpoint code.  */
        cs->watchpoint_hit = NULL;

        env->per_address = env->psw.addr;
        env->per_perc_atmid |= PER_CODE_EVENT_STORE | get_per_atmid(env);
        /* FIXME: We currently no way to detect the address space used
           to trigger the watchpoint.  For now just consider it is the
           current default ASC. This turn to be true except when MVCP
           and MVCS instrutions are not used.  */
        env->per_perc_atmid |= env->psw.mask & (PSW_MASK_ASC) >> 46;

        /* Remove all watchpoints to re-execute the code.  A PER exception
           will be triggered, it will call load_psw which will recompute
           the watchpoints.  */
        cpu_watchpoint_remove_all(cs, BP_CPU);
        cpu_loop_exit_noexc(cs);
    }
}
#endif /* CONFIG_USER_ONLY */
