/*
 * KVM in-kernel IOPIC support
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
#include "monitor/monitor.h"
#include "hw/i386/x86.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/i386/ioapic_internal.h"
#include "hw/i386/apic_internal.h"
#include "sysemu/kvm.h"

/* PC Utility function */
void kvm_pc_setup_irq_routing(bool pci_enabled)
{
    KVMState *s = kvm_state;
    int i;

    if (kvm_check_extension(s, KVM_CAP_IRQ_ROUTING)) {
        for (i = 0; i < 8; ++i) {
            if (i == 2) {
                continue;
            }
            kvm_irqchip_add_irq_route(s, i, KVM_IRQCHIP_PIC_MASTER, i);
        }
        for (i = 8; i < 16; ++i) {
            kvm_irqchip_add_irq_route(s, i, KVM_IRQCHIP_PIC_SLAVE, i - 8);
        }
        if (pci_enabled) {
            for (i = 0; i < 24; ++i) {
                if (i == 0) {
                    kvm_irqchip_add_irq_route(s, i, KVM_IRQCHIP_IOAPIC, 2);
                } else if (i != 2) {
                    kvm_irqchip_add_irq_route(s, i, KVM_IRQCHIP_IOAPIC, i);
                }
            }
        }
        kvm_irqchip_commit_routes(s);
    }
}

typedef struct KVMIOAPICState KVMIOAPICState;

struct KVMIOAPICState {
    IOAPICCommonState ioapic;
    uint32_t kvm_gsi_base;
};

static void kvm_ioapic_get(IOAPICCommonState *s)
{
    struct kvm_irqchip chip;
    struct kvm_ioapic_state *kioapic;
    int ret, i;

    chip.chip_id = KVM_IRQCHIP_IOAPIC;
    ret = kvm_vm_ioctl(kvm_state, KVM_GET_IRQCHIP, &chip);
    if (ret < 0) {
        fprintf(stderr, "KVM_GET_IRQCHIP failed: %s\n", strerror(ret));
        abort();
    }

    kioapic = &chip.chip.ioapic;

    s->id = kioapic->id;
    s->ioregsel = kioapic->ioregsel;
    s->irr = kioapic->irr;
    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        s->ioredtbl[i] = kioapic->redirtbl[i].bits;
    }
}

static void kvm_ioapic_put(IOAPICCommonState *s)
{
    struct kvm_irqchip chip;
    struct kvm_ioapic_state *kioapic;
    int ret, i;

    chip.chip_id = KVM_IRQCHIP_IOAPIC;
    kioapic = &chip.chip.ioapic;

    kioapic->id = s->id;
    kioapic->ioregsel = s->ioregsel;
    kioapic->base_address = s->busdev.mmio[0].addr;
    kioapic->irr = s->irr;
    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        kioapic->redirtbl[i].bits = s->ioredtbl[i];
    }

    ret = kvm_vm_ioctl(kvm_state, KVM_SET_IRQCHIP, &chip);
    if (ret < 0) {
        fprintf(stderr, "KVM_GET_IRQCHIP failed: %s\n", strerror(ret));
        abort();
    }
}

static void kvm_ioapic_reset(DeviceState *dev)
{
    IOAPICCommonState *s = IOAPIC_COMMON(dev);

    ioapic_reset_common(dev);
    kvm_ioapic_put(s);
}

static void kvm_ioapic_set_irq(void *opaque, int irq, int level)
{
    KVMIOAPICState *s = opaque;
    IOAPICCommonState *common = IOAPIC_COMMON(s);
    int delivered;

    ioapic_stat_update_irq(common, irq, level);
    delivered = kvm_set_irq(kvm_state, s->kvm_gsi_base + irq, level);
    apic_report_irq_delivered(delivered);
}

static void kvm_ioapic_realize(DeviceState *dev, Error **errp)
{
    IOAPICCommonState *s = IOAPIC_COMMON(dev);

    memory_region_init_io(&s->io_memory, OBJECT(dev), NULL, NULL, "kvm-ioapic", 0x1000);
    /*
     * KVM ioapic only supports 0x11 now. This will only be used when
     * we want to dump ioapic version.
     */
    s->version = 0x11;

    qdev_init_gpio_in(dev, kvm_ioapic_set_irq, IOAPIC_NUM_PINS);
}

static Property kvm_ioapic_properties[] = {
    DEFINE_PROP_UINT32("gsi_base", KVMIOAPICState, kvm_gsi_base, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void kvm_ioapic_class_init(ObjectClass *klass, void *data)
{
    IOAPICCommonClass *k = IOAPIC_COMMON_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize   = kvm_ioapic_realize;
    k->pre_save  = kvm_ioapic_get;
    k->post_load = kvm_ioapic_put;
    dc->reset    = kvm_ioapic_reset;
    device_class_set_props(dc, kvm_ioapic_properties);
}

static const TypeInfo kvm_ioapic_info = {
    .name  = TYPE_KVM_IOAPIC,
    .parent = TYPE_IOAPIC_COMMON,
    .instance_size = sizeof(KVMIOAPICState),
    .class_init = kvm_ioapic_class_init,
};

static void kvm_ioapic_register_types(void)
{
    type_register_static(&kvm_ioapic_info);
}

type_init(kvm_ioapic_register_types)
