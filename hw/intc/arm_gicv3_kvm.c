/*
 * ARM Generic Interrupt Controller using KVM in-kernel support
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Written by Pavel Fedin
 * Based on vGICv2 code by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/sysbus.h"
#include "sysemu/kvm.h"
#include "kvm_arm.h"
#include "vgic_common.h"
#include "migration/migration.h"

#ifdef DEBUG_GICV3_KVM
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "kvm_gicv3: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

#define TYPE_KVM_ARM_GICV3 "kvm-arm-gicv3"
#define KVM_ARM_GICV3(obj) \
     OBJECT_CHECK(GICv3State, (obj), TYPE_KVM_ARM_GICV3)
#define KVM_ARM_GICV3_CLASS(klass) \
     OBJECT_CLASS_CHECK(KVMARMGICv3Class, (klass), TYPE_KVM_ARM_GICV3)
#define KVM_ARM_GICV3_GET_CLASS(obj) \
     OBJECT_GET_CLASS(KVMARMGICv3Class, (obj), TYPE_KVM_ARM_GICV3)

typedef struct KVMARMGICv3Class {
    ARMGICv3CommonClass parent_class;
    DeviceRealize parent_realize;
    void (*parent_reset)(DeviceState *dev);
} KVMARMGICv3Class;

static void kvm_arm_gicv3_set_irq(void *opaque, int irq, int level)
{
    GICv3State *s = (GICv3State *)opaque;

    kvm_arm_gic_set_irq(s->num_irq, irq, level);
}

static void kvm_arm_gicv3_put(GICv3State *s)
{
    /* TODO */
    DPRINTF("Cannot put kernel gic state, no kernel interface\n");
}

static void kvm_arm_gicv3_get(GICv3State *s)
{
    /* TODO */
    DPRINTF("Cannot get kernel gic state, no kernel interface\n");
}

static void kvm_arm_gicv3_reset(DeviceState *dev)
{
    GICv3State *s = ARM_GICV3_COMMON(dev);
    KVMARMGICv3Class *kgc = KVM_ARM_GICV3_GET_CLASS(s);

    DPRINTF("Reset\n");

    kgc->parent_reset(dev);
    kvm_arm_gicv3_put(s);
}

static void kvm_arm_gicv3_realize(DeviceState *dev, Error **errp)
{
    GICv3State *s = KVM_ARM_GICV3(dev);
    KVMARMGICv3Class *kgc = KVM_ARM_GICV3_GET_CLASS(s);
    Error *local_err = NULL;

    DPRINTF("kvm_arm_gicv3_realize\n");

    kgc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (s->security_extn) {
        error_setg(errp, "the in-kernel VGICv3 does not implement the "
                   "security extensions");
        return;
    }

    gicv3_init_irqs_and_mmio(s, kvm_arm_gicv3_set_irq, NULL);

    /* Try to create the device via the device control API */
    s->dev_fd = kvm_create_device(kvm_state, KVM_DEV_TYPE_ARM_VGIC_V3, false);
    if (s->dev_fd < 0) {
        error_setg_errno(errp, -s->dev_fd, "error creating in-kernel VGIC");
        return;
    }

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS,
                      0, &s->num_irq, true);

    /* Tell the kernel to complete VGIC initialization now */
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
                      KVM_DEV_ARM_VGIC_CTRL_INIT, NULL, true);

    kvm_arm_register_device(&s->iomem_dist, -1, KVM_DEV_ARM_VGIC_GRP_ADDR,
                            KVM_VGIC_V3_ADDR_TYPE_DIST, s->dev_fd);
    kvm_arm_register_device(&s->iomem_redist, -1, KVM_DEV_ARM_VGIC_GRP_ADDR,
                            KVM_VGIC_V3_ADDR_TYPE_REDIST, s->dev_fd);

    /* Block migration of a KVM GICv3 device: the API for saving and restoring
     * the state in the kernel is not yet finalised in the kernel or
     * implemented in QEMU.
     */
    error_setg(&s->migration_blocker, "vGICv3 migration is not implemented");
    migrate_add_blocker(s->migration_blocker);
}

static void kvm_arm_gicv3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ARMGICv3CommonClass *agcc = ARM_GICV3_COMMON_CLASS(klass);
    KVMARMGICv3Class *kgc = KVM_ARM_GICV3_CLASS(klass);

    agcc->pre_save = kvm_arm_gicv3_get;
    agcc->post_load = kvm_arm_gicv3_put;
    kgc->parent_realize = dc->realize;
    kgc->parent_reset = dc->reset;
    dc->realize = kvm_arm_gicv3_realize;
    dc->reset = kvm_arm_gicv3_reset;
}

static const TypeInfo kvm_arm_gicv3_info = {
    .name = TYPE_KVM_ARM_GICV3,
    .parent = TYPE_ARM_GICV3_COMMON,
    .instance_size = sizeof(GICv3State),
    .class_init = kvm_arm_gicv3_class_init,
    .class_size = sizeof(KVMARMGICv3Class),
};

static void kvm_arm_gicv3_register_types(void)
{
    type_register_static(&kvm_arm_gicv3_info);
}

type_init(kvm_arm_gicv3_register_types)
