/*
 * QEMU S/390 Interrupt support
 *
 * Copyright IBM Corp. 2012, 2014
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 */

#include "cpu.h"
#include "sysemu/kvm.h"

/*
 * All of the following interrupts are floating, i.e. not per-vcpu.
 * We just need a dummy cpustate in order to be able to inject in the
 * non-kvm case.
 */
#if !defined(CONFIG_USER_ONLY)
void s390_sclp_extint(uint32_t parm)
{
    if (kvm_enabled()) {
        kvm_s390_service_interrupt(parm);
    } else {
        S390CPU *dummy_cpu = s390_cpu_addr2state(0);
        CPUS390XState *env = &dummy_cpu->env;

        env->psw.addr += 4;
        cpu_inject_ext(dummy_cpu, EXT_SERVICE, parm, 0);
    }
}

void s390_virtio_irq(int config_change, uint64_t token)
{
    if (kvm_enabled()) {
        kvm_s390_virtio_irq(config_change, token);
    } else {
        S390CPU *dummy_cpu = s390_cpu_addr2state(0);

        cpu_inject_ext(dummy_cpu, EXT_VIRTIO, config_change, token);
    }
}

void s390_io_interrupt(uint16_t subchannel_id, uint16_t subchannel_nr,
                       uint32_t io_int_parm, uint32_t io_int_word)
{
    if (kvm_enabled()) {
        kvm_s390_io_interrupt(subchannel_id, subchannel_nr, io_int_parm,
                              io_int_word);
    } else {
        S390CPU *dummy_cpu = s390_cpu_addr2state(0);

        cpu_inject_io(dummy_cpu, subchannel_id, subchannel_nr, io_int_parm,
                      io_int_word);
    }
}

void s390_crw_mchk(void)
{
    if (kvm_enabled()) {
        kvm_s390_crw_mchk();
    } else {
        S390CPU *dummy_cpu = s390_cpu_addr2state(0);

        cpu_inject_crw_mchk(dummy_cpu);
    }
}

#endif
