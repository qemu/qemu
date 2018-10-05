/*
 * QEMU S/390 Interrupt support
 *
 * Copyright IBM Corp. 2012, 2014
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "kvm_s390x.h"
#include "internal.h"
#include "exec/exec-all.h"
#include "sysemu/kvm.h"
#include "hw/s390x/ioinst.h"
#include "tcg_s390x.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/s390x/s390_flic.h"
#endif

/* Ensure to exit the TB after this call! */
void trigger_pgm_exception(CPUS390XState *env, uint32_t code, uint32_t ilen)
{
    CPUState *cs = CPU(s390_env_get_cpu(env));

    cs->exception_index = EXCP_PGM;
    env->int_pgm_code = code;
    env->int_pgm_ilen = ilen;
}

void s390_program_interrupt(CPUS390XState *env, uint32_t code, int ilen,
                            uintptr_t ra)
{
    S390CPU *cpu = s390_env_get_cpu(env);

    if (kvm_enabled()) {
        kvm_s390_program_interrupt(cpu, code);
    } else if (tcg_enabled()) {
        tcg_s390_program_interrupt(env, code, ilen, ra);
    } else {
        g_assert_not_reached();
    }
}

#if !defined(CONFIG_USER_ONLY)
void cpu_inject_clock_comparator(S390CPU *cpu)
{
    CPUS390XState *env = &cpu->env;

    env->pending_int |= INTERRUPT_EXT_CLOCK_COMPARATOR;
    cpu_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
}

void cpu_inject_cpu_timer(S390CPU *cpu)
{
    CPUS390XState *env = &cpu->env;

    env->pending_int |= INTERRUPT_EXT_CPU_TIMER;
    cpu_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
}

void cpu_inject_emergency_signal(S390CPU *cpu, uint16_t src_cpu_addr)
{
    CPUS390XState *env = &cpu->env;

    g_assert(src_cpu_addr < S390_MAX_CPUS);
    set_bit(src_cpu_addr, env->emergency_signals);

    env->pending_int |= INTERRUPT_EMERGENCY_SIGNAL;
    cpu_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
}

int cpu_inject_external_call(S390CPU *cpu, uint16_t src_cpu_addr)
{
    CPUS390XState *env = &cpu->env;

    g_assert(src_cpu_addr < S390_MAX_CPUS);
    if (env->pending_int & INTERRUPT_EXTERNAL_CALL) {
        return -EBUSY;
    }
    env->external_call_addr = src_cpu_addr;

    env->pending_int |= INTERRUPT_EXTERNAL_CALL;
    cpu_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
    return 0;
}

void cpu_inject_restart(S390CPU *cpu)
{
    CPUS390XState *env = &cpu->env;

    if (kvm_enabled()) {
        kvm_s390_restart_interrupt(cpu);
        return;
    }

    env->pending_int |= INTERRUPT_RESTART;
    cpu_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
}

void cpu_inject_stop(S390CPU *cpu)
{
    CPUS390XState *env = &cpu->env;

    if (kvm_enabled()) {
        kvm_s390_stop_interrupt(cpu);
        return;
    }

    env->pending_int |= INTERRUPT_STOP;
    cpu_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
}

/*
 * All of the following interrupts are floating, i.e. not per-vcpu.
 * We just need a dummy cpustate in order to be able to inject in the
 * non-kvm case.
 */
void s390_sclp_extint(uint32_t parm)
{
    S390FLICState *fs = s390_get_flic();
    S390FLICStateClass *fsc = s390_get_flic_class(fs);

    fsc->inject_service(fs, parm);
}

void s390_io_interrupt(uint16_t subchannel_id, uint16_t subchannel_nr,
                       uint32_t io_int_parm, uint32_t io_int_word)
{
    S390FLICState *fs = s390_get_flic();
    S390FLICStateClass *fsc = s390_get_flic_class(fs);

    fsc->inject_io(fs, subchannel_id, subchannel_nr, io_int_parm, io_int_word);
}

void s390_crw_mchk(void)
{
    S390FLICState *fs = s390_get_flic();
    S390FLICStateClass *fsc = s390_get_flic_class(fs);

    fsc->inject_crw_mchk(fs);
}

bool s390_cpu_has_mcck_int(S390CPU *cpu)
{
    QEMUS390FLICState *flic = s390_get_qemu_flic(s390_get_flic());
    CPUS390XState *env = &cpu->env;

    if (!(env->psw.mask & PSW_MASK_MCHECK)) {
        return false;
    }

    /* for now we only support channel report machine checks (floating) */
    if (qemu_s390_flic_has_crw_mchk(flic) &&
        (env->cregs[14] & CR14_CHANNEL_REPORT_SC)) {
        return true;
    }

    return false;
}

bool s390_cpu_has_ext_int(S390CPU *cpu)
{
    QEMUS390FLICState *flic = s390_get_qemu_flic(s390_get_flic());
    CPUS390XState *env = &cpu->env;

    if (!(env->psw.mask & PSW_MASK_EXT)) {
        return false;
    }

    if ((env->pending_int & INTERRUPT_EMERGENCY_SIGNAL) &&
        (env->cregs[0] & CR0_EMERGENCY_SIGNAL_SC)) {
        return true;
    }

    if ((env->pending_int & INTERRUPT_EXTERNAL_CALL) &&
        (env->cregs[0] & CR0_EXTERNAL_CALL_SC)) {
        return true;
    }

    if ((env->pending_int & INTERRUPT_EXTERNAL_CALL) &&
        (env->cregs[0] & CR0_EXTERNAL_CALL_SC)) {
        return true;
    }

    if ((env->pending_int & INTERRUPT_EXT_CLOCK_COMPARATOR) &&
        (env->cregs[0] & CR0_CKC_SC)) {
        return true;
    }

    if ((env->pending_int & INTERRUPT_EXT_CPU_TIMER) &&
        (env->cregs[0] & CR0_CPU_TIMER_SC)) {
        return true;
    }

    if (qemu_s390_flic_has_service(flic) &&
        (env->cregs[0] & CR0_SERVICE_SC)) {
        return true;
    }

    return false;
}

bool s390_cpu_has_io_int(S390CPU *cpu)
{
    QEMUS390FLICState *flic = s390_get_qemu_flic(s390_get_flic());
    CPUS390XState *env = &cpu->env;

    if (!(env->psw.mask & PSW_MASK_IO)) {
        return false;
    }

    return qemu_s390_flic_has_io(flic, env->cregs[6]);
}

bool s390_cpu_has_restart_int(S390CPU *cpu)
{
    CPUS390XState *env = &cpu->env;

    return env->pending_int & INTERRUPT_RESTART;
}

bool s390_cpu_has_stop_int(S390CPU *cpu)
{
    CPUS390XState *env = &cpu->env;

    return env->pending_int & INTERRUPT_STOP;
}
#endif

bool s390_cpu_has_int(S390CPU *cpu)
{
#ifndef CONFIG_USER_ONLY
    if (!tcg_enabled()) {
        return false;
    }
    return s390_cpu_has_mcck_int(cpu) ||
           s390_cpu_has_ext_int(cpu) ||
           s390_cpu_has_io_int(cpu) ||
           s390_cpu_has_restart_int(cpu) ||
           s390_cpu_has_stop_int(cpu);
#else
    return false;
#endif
}
