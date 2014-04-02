/*
 * KVM in-kernel OpenPIC
 *
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <sys/ioctl.h>
#include "exec/address-spaces.h"
#include "hw/hw.h"
#include "hw/ppc/openpic.h"
#include "hw/pci/msi.h"
#include "hw/sysbus.h"
#include "sysemu/kvm.h"
#include "qemu/log.h"

#define KVM_OPENPIC(obj) \
    OBJECT_CHECK(KVMOpenPICState, (obj), TYPE_KVM_OPENPIC)

typedef struct KVMOpenPICState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;
    MemoryListener mem_listener;
    uint32_t fd;
    uint32_t model;
} KVMOpenPICState;

static void kvm_openpic_set_irq(void *opaque, int n_IRQ, int level)
{
    kvm_set_irq(kvm_state, n_IRQ, level);
}

static void kvm_openpic_reset(DeviceState *d)
{
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented\n", __func__);
}

static void kvm_openpic_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    KVMOpenPICState *opp = opaque;
    struct kvm_device_attr attr;
    uint32_t val32 = val;
    int ret;

    attr.group = KVM_DEV_MPIC_GRP_REGISTER;
    attr.attr = addr;
    attr.addr = (uint64_t)(unsigned long)&val32;

    ret = ioctl(opp->fd, KVM_SET_DEVICE_ATTR, &attr);
    if (ret < 0) {
        qemu_log_mask(LOG_UNIMP, "%s: %s %" PRIx64 "\n", __func__,
                      strerror(errno), attr.attr);
    }
}

static uint64_t kvm_openpic_read(void *opaque, hwaddr addr, unsigned size)
{
    KVMOpenPICState *opp = opaque;
    struct kvm_device_attr attr;
    uint32_t val = 0xdeadbeef;
    int ret;

    attr.group = KVM_DEV_MPIC_GRP_REGISTER;
    attr.attr = addr;
    attr.addr = (uint64_t)(unsigned long)&val;

    ret = ioctl(opp->fd, KVM_GET_DEVICE_ATTR, &attr);
    if (ret < 0) {
        qemu_log_mask(LOG_UNIMP, "%s: %s %" PRIx64 "\n", __func__,
                      strerror(errno), attr.attr);
        return 0;
    }

    return val;
}

static const MemoryRegionOps kvm_openpic_mem_ops = {
    .write = kvm_openpic_write,
    .read  = kvm_openpic_read,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void kvm_openpic_region_add(MemoryListener *listener,
                                   MemoryRegionSection *section)
{
    KVMOpenPICState *opp = container_of(listener, KVMOpenPICState,
                                        mem_listener);
    struct kvm_device_attr attr;
    uint64_t reg_base;
    int ret;

    if (section->address_space != &address_space_memory) {
        abort();
    }

    /* Ignore events on regions that are not us */
    if (section->mr != &opp->mem) {
        return;
    }

    reg_base = section->offset_within_address_space;

    attr.group = KVM_DEV_MPIC_GRP_MISC;
    attr.attr = KVM_DEV_MPIC_BASE_ADDR;
    attr.addr = (uint64_t)(unsigned long)&reg_base;

    ret = ioctl(opp->fd, KVM_SET_DEVICE_ATTR, &attr);
    if (ret < 0) {
        fprintf(stderr, "%s: %s %" PRIx64 "\n", __func__,
                strerror(errno), reg_base);
    }
}

static void kvm_openpic_region_del(MemoryListener *listener,
                                   MemoryRegionSection *section)
{
    KVMOpenPICState *opp = container_of(listener, KVMOpenPICState,
                                        mem_listener);
    struct kvm_device_attr attr;
    uint64_t reg_base = 0;
    int ret;

    /* Ignore events on regions that are not us */
    if (section->mr != &opp->mem) {
        return;
    }

    attr.group = KVM_DEV_MPIC_GRP_MISC;
    attr.attr = KVM_DEV_MPIC_BASE_ADDR;
    attr.addr = (uint64_t)(unsigned long)&reg_base;

    ret = ioctl(opp->fd, KVM_SET_DEVICE_ATTR, &attr);
    if (ret < 0) {
        fprintf(stderr, "%s: %s %" PRIx64 "\n", __func__,
                strerror(errno), reg_base);
    }
}

static void kvm_openpic_init(Object *obj)
{
    KVMOpenPICState *opp = KVM_OPENPIC(obj);

    memory_region_init_io(&opp->mem, OBJECT(opp), &kvm_openpic_mem_ops, opp,
                          "kvm-openpic", 0x40000);
}

static void kvm_openpic_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    KVMOpenPICState *opp = KVM_OPENPIC(dev);
    KVMState *s = kvm_state;
    int kvm_openpic_model;
    struct kvm_create_device cd = {0};
    int ret, i;

    if (!kvm_check_extension(s, KVM_CAP_DEVICE_CTRL)) {
        error_setg(errp, "Kernel is lacking Device Control API");
        return;
    }

    switch (opp->model) {
    case OPENPIC_MODEL_FSL_MPIC_20:
        kvm_openpic_model = KVM_DEV_TYPE_FSL_MPIC_20;
        break;

    case OPENPIC_MODEL_FSL_MPIC_42:
        kvm_openpic_model = KVM_DEV_TYPE_FSL_MPIC_42;
        break;

    default:
        error_setg(errp, "Unsupported OpenPIC model %" PRIu32, opp->model);
        return;
    }

    cd.type = kvm_openpic_model;
    ret = kvm_vm_ioctl(s, KVM_CREATE_DEVICE, &cd);
    if (ret < 0) {
        error_setg(errp, "Can't create device %d: %s",
                   cd.type, strerror(errno));
        return;
    }
    opp->fd = cd.fd;

    sysbus_init_mmio(d, &opp->mem);
    qdev_init_gpio_in(dev, kvm_openpic_set_irq, OPENPIC_MAX_IRQ);

    opp->mem_listener.region_add = kvm_openpic_region_add;
    opp->mem_listener.region_del = kvm_openpic_region_del;
    memory_listener_register(&opp->mem_listener, &address_space_memory);

    /* indicate pic capabilities */
    msi_supported = true;
    kvm_kernel_irqchip = true;
    kvm_async_interrupts_allowed = true;

    /* set up irq routing */
    kvm_init_irq_routing(kvm_state);
    for (i = 0; i < 256; ++i) {
        kvm_irqchip_add_irq_route(kvm_state, i, 0, i);
    }

    kvm_irqfds_allowed = true;
    kvm_msi_via_irqfd_allowed = true;
    kvm_gsi_routing_allowed = true;

    kvm_irqchip_commit_routes(s);
}

int kvm_openpic_connect_vcpu(DeviceState *d, CPUState *cs)
{
    KVMOpenPICState *opp = KVM_OPENPIC(d);
    struct kvm_enable_cap encap = {};

    encap.cap = KVM_CAP_IRQ_MPIC;
    encap.args[0] = opp->fd;
    encap.args[1] = kvm_arch_vcpu_id(cs);

    return kvm_vcpu_ioctl(cs, KVM_ENABLE_CAP, &encap);
}

static Property kvm_openpic_properties[] = {
    DEFINE_PROP_UINT32("model", KVMOpenPICState, model,
                       OPENPIC_MODEL_FSL_MPIC_20),
    DEFINE_PROP_END_OF_LIST(),
};

static void kvm_openpic_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = kvm_openpic_realize;
    dc->props = kvm_openpic_properties;
    dc->reset = kvm_openpic_reset;
}

static const TypeInfo kvm_openpic_info = {
    .name          = TYPE_KVM_OPENPIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(KVMOpenPICState),
    .instance_init = kvm_openpic_init,
    .class_init    = kvm_openpic_class_init,
};

static void kvm_openpic_register_types(void)
{
    type_register_static(&kvm_openpic_info);
}

type_init(kvm_openpic_register_types)
