/*
 * QEMU S/390 Interrupt support
 *
 * Copyright IBM, Corp. 2012
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 */

#include "cpu.h"
#include "sysemu/kvm.h"

#if !defined(CONFIG_USER_ONLY)
/* service interrupts are floating therefore we must not pass an cpustate */
void s390_sclp_extint(uint32_t parm)
{
    S390CPU *dummy_cpu = s390_cpu_addr2state(0);
    CPUS390XState *env = &dummy_cpu->env;

    if (kvm_enabled()) {
#ifdef CONFIG_KVM
        kvm_s390_interrupt_internal(dummy_cpu, KVM_S390_INT_SERVICE, parm,
                                    0, 1);
#endif
    } else {
        env->psw.addr += 4;
        cpu_inject_ext(dummy_cpu, EXT_SERVICE, parm, 0);
    }
}
#endif
