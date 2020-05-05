/*
 * KVM in-kernel APIC support
 *
 * Copyright (c) 2011 Siemens AG
 *
 * Authors:
 *  Jan Kiszka          <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "cpu.h"
#include "hw/i386/apic_internal.h"
#include "hw/pci/msi.h"
#include "sysemu/hw_accel.h"
#include "sysemu/kvm.h"
#include "target/i386/kvm_i386.h"

static inline void kvm_apic_set_reg(struct kvm_lapic_state *kapic,
                                    int reg_id, uint32_t val)
{
    *((uint32_t *)(kapic->regs + (reg_id << 4))) = val;
}

static inline uint32_t kvm_apic_get_reg(struct kvm_lapic_state *kapic,
                                        int reg_id)
{
    return *((uint32_t *)(kapic->regs + (reg_id << 4)));
}

static void kvm_put_apic_state(APICCommonState *s, struct kvm_lapic_state *kapic)
{
    int i;

    memset(kapic, 0, sizeof(*kapic));
    if (kvm_has_x2apic_api() && s->apicbase & MSR_IA32_APICBASE_EXTD) {
        kvm_apic_set_reg(kapic, 0x2, s->initial_apic_id);
    } else {
        kvm_apic_set_reg(kapic, 0x2, s->id << 24);
    }
    kvm_apic_set_reg(kapic, 0x8, s->tpr);
    kvm_apic_set_reg(kapic, 0xd, s->log_dest << 24);
    kvm_apic_set_reg(kapic, 0xe, s->dest_mode << 28 | 0x0fffffff);
    kvm_apic_set_reg(kapic, 0xf, s->spurious_vec);
    for (i = 0; i < 8; i++) {
        kvm_apic_set_reg(kapic, 0x10 + i, s->isr[i]);
        kvm_apic_set_reg(kapic, 0x18 + i, s->tmr[i]);
        kvm_apic_set_reg(kapic, 0x20 + i, s->irr[i]);
    }
    kvm_apic_set_reg(kapic, 0x28, s->esr);
    kvm_apic_set_reg(kapic, 0x30, s->icr[0]);
    kvm_apic_set_reg(kapic, 0x31, s->icr[1]);
    for (i = 0; i < APIC_LVT_NB; i++) {
        kvm_apic_set_reg(kapic, 0x32 + i, s->lvt[i]);
    }
    kvm_apic_set_reg(kapic, 0x38, s->initial_count);
    kvm_apic_set_reg(kapic, 0x3e, s->divide_conf);
}

void kvm_get_apic_state(DeviceState *dev, struct kvm_lapic_state *kapic)
{
    APICCommonState *s = APIC_COMMON(dev);
    int i, v;

    if (kvm_has_x2apic_api() && s->apicbase & MSR_IA32_APICBASE_EXTD) {
        assert(kvm_apic_get_reg(kapic, 0x2) == s->initial_apic_id);
    } else {
        s->id = kvm_apic_get_reg(kapic, 0x2) >> 24;
    }
    s->tpr = kvm_apic_get_reg(kapic, 0x8);
    s->arb_id = kvm_apic_get_reg(kapic, 0x9);
    s->log_dest = kvm_apic_get_reg(kapic, 0xd) >> 24;
    s->dest_mode = kvm_apic_get_reg(kapic, 0xe) >> 28;
    s->spurious_vec = kvm_apic_get_reg(kapic, 0xf);
    for (i = 0; i < 8; i++) {
        s->isr[i] = kvm_apic_get_reg(kapic, 0x10 + i);
        s->tmr[i] = kvm_apic_get_reg(kapic, 0x18 + i);
        s->irr[i] = kvm_apic_get_reg(kapic, 0x20 + i);
    }
    s->esr = kvm_apic_get_reg(kapic, 0x28);
    s->icr[0] = kvm_apic_get_reg(kapic, 0x30);
    s->icr[1] = kvm_apic_get_reg(kapic, 0x31);
    for (i = 0; i < APIC_LVT_NB; i++) {
        s->lvt[i] = kvm_apic_get_reg(kapic, 0x32 + i);
    }
    s->initial_count = kvm_apic_get_reg(kapic, 0x38);
    s->divide_conf = kvm_apic_get_reg(kapic, 0x3e);

    v = (s->divide_conf & 3) | ((s->divide_conf >> 1) & 4);
    s->count_shift = (v + 1) & 7;

    s->initial_count_load_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    apic_next_timer(s, s->initial_count_load_time);
}

static void kvm_apic_set_base(APICCommonState *s, uint64_t val)
{
    s->apicbase = val;
}

static void kvm_apic_set_tpr(APICCommonState *s, uint8_t val)
{
    s->tpr = (val & 0x0f) << 4;
}

static uint8_t kvm_apic_get_tpr(APICCommonState *s)
{
    return s->tpr >> 4;
}

static void kvm_apic_enable_tpr_reporting(APICCommonState *s, bool enable)
{
    struct kvm_tpr_access_ctl ctl = {
        .enabled = enable
    };

    kvm_vcpu_ioctl(CPU(s->cpu), KVM_TPR_ACCESS_REPORTING, &ctl);
}

static void kvm_apic_vapic_base_update(APICCommonState *s)
{
    struct kvm_vapic_addr vapid_addr = {
        .vapic_addr = s->vapic_paddr,
    };
    int ret;

    ret = kvm_vcpu_ioctl(CPU(s->cpu), KVM_SET_VAPIC_ADDR, &vapid_addr);
    if (ret < 0) {
        fprintf(stderr, "KVM: setting VAPIC address failed (%s)\n",
                strerror(-ret));
        abort();
    }
}

static void kvm_apic_put(CPUState *cs, run_on_cpu_data data)
{
    APICCommonState *s = data.host_ptr;
    struct kvm_lapic_state kapic;
    int ret;

    kvm_put_apicbase(s->cpu, s->apicbase);
    kvm_put_apic_state(s, &kapic);

    ret = kvm_vcpu_ioctl(CPU(s->cpu), KVM_SET_LAPIC, &kapic);
    if (ret < 0) {
        fprintf(stderr, "KVM_SET_LAPIC failed: %s\n", strerror(ret));
        abort();
    }
}

static void kvm_apic_post_load(APICCommonState *s)
{
    run_on_cpu(CPU(s->cpu), kvm_apic_put, RUN_ON_CPU_HOST_PTR(s));
}

static void do_inject_external_nmi(CPUState *cpu, run_on_cpu_data data)
{
    APICCommonState *s = data.host_ptr;
    uint32_t lvt;
    int ret;

    cpu_synchronize_state(cpu);

    lvt = s->lvt[APIC_LVT_LINT1];
    if (!(lvt & APIC_LVT_MASKED) && ((lvt >> 8) & 7) == APIC_DM_NMI) {
        ret = kvm_vcpu_ioctl(cpu, KVM_NMI);
        if (ret < 0) {
            fprintf(stderr, "KVM: injection failed, NMI lost (%s)\n",
                    strerror(-ret));
        }
    }
}

static void kvm_apic_external_nmi(APICCommonState *s)
{
    run_on_cpu(CPU(s->cpu), do_inject_external_nmi, RUN_ON_CPU_HOST_PTR(s));
}

static void kvm_send_msi(MSIMessage *msg)
{
    int ret;

    ret = kvm_irqchip_send_msi(kvm_state, *msg);
    if (ret < 0) {
        fprintf(stderr, "KVM: injection failed, MSI lost (%s)\n",
                strerror(-ret));
    }
}

static uint64_t kvm_apic_mem_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    return ~(uint64_t)0;
}

static void kvm_apic_mem_write(void *opaque, hwaddr addr,
                               uint64_t data, unsigned size)
{
    MSIMessage msg = { .address = addr, .data = data };

    kvm_send_msi(&msg);
}

static const MemoryRegionOps kvm_apic_io_ops = {
    .read = kvm_apic_mem_read,
    .write = kvm_apic_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void kvm_apic_reset(APICCommonState *s)
{
    /* Not used by KVM, which uses the CPU mp_state instead.  */
    s->wait_for_sipi = 0;

    run_on_cpu(CPU(s->cpu), kvm_apic_put, RUN_ON_CPU_HOST_PTR(s));
}

static void kvm_apic_realize(DeviceState *dev, Error **errp)
{
    APICCommonState *s = APIC_COMMON(dev);

    memory_region_init_io(&s->io_memory, OBJECT(s), &kvm_apic_io_ops, s,
                          "kvm-apic-msi", APIC_SPACE_SIZE);

    if (kvm_has_gsi_routing()) {
        msi_nonbroken = true;
    }
}

static void kvm_apic_unrealize(DeviceState *dev)
{
}

static void kvm_apic_class_init(ObjectClass *klass, void *data)
{
    APICCommonClass *k = APIC_COMMON_CLASS(klass);

    k->realize = kvm_apic_realize;
    k->unrealize = kvm_apic_unrealize;
    k->reset = kvm_apic_reset;
    k->set_base = kvm_apic_set_base;
    k->set_tpr = kvm_apic_set_tpr;
    k->get_tpr = kvm_apic_get_tpr;
    k->post_load = kvm_apic_post_load;
    k->enable_tpr_reporting = kvm_apic_enable_tpr_reporting;
    k->vapic_base_update = kvm_apic_vapic_base_update;
    k->external_nmi = kvm_apic_external_nmi;
    k->send_msi = kvm_send_msi;
}

static const TypeInfo kvm_apic_info = {
    .name = "kvm-apic",
    .parent = TYPE_APIC_COMMON,
    .instance_size = sizeof(APICCommonState),
    .class_init = kvm_apic_class_init,
};

static void kvm_apic_register_types(void)
{
    type_register_static(&kvm_apic_info);
}

type_init(kvm_apic_register_types)
