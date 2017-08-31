/*
 * QEMU KVM support -- s390x specific function stubs.
 *
 * Copyright (c) 2009 Ulrich Hecht
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "kvm_s390x.h"

void kvm_s390_service_interrupt(uint32_t parm)
{
}

void kvm_s390_access_exception(S390CPU *cpu, uint16_t code, uint64_t te_code)
{
}

int kvm_s390_mem_op(S390CPU *cpu, vaddr addr, uint8_t ar, void *hostbuf,
                    int len, bool is_write)
{
    return -ENOSYS;
}

void kvm_s390_program_interrupt(S390CPU *cpu, uint16_t code)
{
}

void kvm_s390_io_interrupt(uint16_t subchannel_id, uint16_t subchannel_nr,
                           uint32_t io_int_parm, uint32_t io_int_word)
{
}

void kvm_s390_crw_mchk(void)
{
}

int kvm_s390_set_cpu_state(S390CPU *cpu, uint8_t cpu_state)
{
    return -ENOSYS;
}

void kvm_s390_vcpu_interrupt_pre_save(S390CPU *cpu)
{
}

int kvm_s390_vcpu_interrupt_post_load(S390CPU *cpu)
{
    return 0;
}

int kvm_s390_get_ri(void)
{
    return 0;
}

int kvm_s390_get_gs(void)
{
    return 0;
}

int kvm_s390_get_clock(uint8_t *tod_high, uint64_t *tod_low)
{
    return -ENOSYS;
}

int kvm_s390_set_clock(uint8_t *tod_high, uint64_t *tod_low)
{
    return -ENOSYS;
}

void kvm_s390_enable_css_support(S390CPU *cpu)
{
}

int kvm_s390_assign_subch_ioeventfd(EventNotifier *notifier, uint32_t sch,
                                    int vq, bool assign)
{
    return -ENOSYS;
}

int kvm_s390_cpu_restart(S390CPU *cpu)
{
    return -ENOSYS;
}

void kvm_s390_cmma_reset(void)
{
}

int kvm_s390_get_memslot_count(void)
{
  return MAX_AVAIL_SLOTS;
}

void kvm_s390_reset_vcpu(S390CPU *cpu)
{
}

int kvm_s390_set_mem_limit(uint64_t new_limit, uint64_t *hw_limit)
{
    return 0;
}

void kvm_s390_crypto_reset(void)
{
}
