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

typedef struct KVMOpenPICState {
    SysBusDevice busdev;
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

    attr.group = KVM_DEV_MPIC_GRP_MISC;
    attr.attr = KVM_DEV_MPIC_BASE_ADDR;
    attr.addr = (uint64_t)(unsigned long)&reg_base;

    ret = ioctl(opp->fd, KVM_SET_DEVICE_ATTR, &attr);
    if (ret < 0) {
        fprintf(stderr, "%s: %s %" PRIx64 "\n", __func__,
                strerror(errno), reg_base);
    }
}

static int kvm_openpic_init(SysBusDevice *dev)
{
    KVMState *s = kvm_state;
    KVMOpenPICState *opp = FROM_SYSBUS(typeof(*opp), dev);
    int kvm_openpic_model;
    struct kvm_create_device cd = {0};
    int ret, i;

    if (!kvm_check_extension(s, KVM_CAP_DEVICE_CTRL)) {
        return -EINVAL;
    }

    switch (opp->model) {
    case OPENPIC_MODEL_FSL_MPIC_20:
        kvm_openpic_model = KVM_DEV_TYPE_FSL_MPIC_20;
        break;

    case OPENPIC_MODEL_FSL_MPIC_42:
        kvm_openpic_model = KVM_DEV_TYPE_FSL_MPIC_42;
        break;

    default:
        return -EINVAL;
    }

    cd.type = kvm_openpic_model;
    ret = kvm_vm_ioctl(s, KVM_CREATE_DEVICE, &cd);
    if (ret < 0) {
        qemu_log_mask(LOG_UNIMP, "%s: can't create device %d: %s\n",
                      __func__, cd.type, strerror(errno));
        return -EINVAL;
    }
    opp->fd = cd.fd;

    memory_region_init_io(&opp->mem, &kvm_openpic_mem_ops, opp,
                          "kvm-openpic", 0x40000);

    sysbus_init_mmio(dev, &opp->mem);
    qdev_init_gpio_in(&dev->qdev, kvm_openpic_set_irq, OPENPIC_MAX_IRQ);

    opp->mem_listener.region_add = kvm_openpic_region_add;
    opp->mem_listener.region_add = kvm_openpic_region_del;
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

    return 0;
}

int kvm_openpic_connect_vcpu(DeviceState *d, CPUState *cs)
{
    KVMOpenPICState *opp = FROM_SYSBUS(typeof(*opp), SYS_BUS_DEVICE(d));
    struct kvm_enable_cap encap = {};

    encap.cap = KVM_CAP_IRQ_MPIC;
    encap.args[0] = opp->fd;
    encap.args[1] = cs->cpu_index;

    return kvm_vcpu_ioctl(cs, KVM_ENABLE_CAP, &encap);
}

static Property kvm_openpic_properties[] = {
    DEFINE_PROP_UINT32("model", KVMOpenPICState, model,
                       OPENPIC_MODEL_FSL_MPIC_20),
    DEFINE_PROP_END_OF_LIST(),
};

static void kvm_openpic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = kvm_openpic_init;
    dc->props = kvm_openpic_properties;
    dc->reset = kvm_openpic_reset;
}

static const TypeInfo kvm_openpic_info = {
    .name          = "kvm-openpic",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(KVMOpenPICState),
    .class_init    = kvm_openpic_class_init,
};

static void kvm_openpic_register_types(void)
{
    type_register_static(&kvm_openpic_info);
}

type_init(kvm_openpic_register_types)
