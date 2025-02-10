/*
 * KVM-based ITS implementation for a GICv3-based system
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Written by Pavel Fedin <p.fedin@samsung.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "hw/qdev-properties.h"
#include "system/runstate.h"
#include "system/kvm.h"
#include "kvm_arm.h"
#include "migration/blocker.h"
#include "qom/object.h"

#define TYPE_KVM_ARM_ITS "arm-its-kvm"
typedef struct KVMARMITSClass KVMARMITSClass;
/* This is reusing the GICv3ITSState typedef from ARM_GICV3_ITS_COMMON */
DECLARE_OBJ_CHECKERS(GICv3ITSState, KVMARMITSClass,
                     KVM_ARM_ITS, TYPE_KVM_ARM_ITS)

struct KVMARMITSClass {
    GICv3ITSCommonClass parent_class;
    ResettablePhases parent_phases;
};


static int kvm_its_send_msi(GICv3ITSState *s, uint32_t value, uint16_t devid)
{
    struct kvm_msi msi;

    if (unlikely(!s->translater_gpa_known)) {
        MemoryRegion *mr = &s->iomem_its_translation;
        MemoryRegionSection mrs;

        mrs = memory_region_find(mr, 0, 1);
        memory_region_unref(mrs.mr);
        s->gits_translater_gpa = mrs.offset_within_address_space + 0x40;
        s->translater_gpa_known = true;
    }

    msi.address_lo = extract64(s->gits_translater_gpa, 0, 32);
    msi.address_hi = extract64(s->gits_translater_gpa, 32, 32);
    msi.data = le32_to_cpu(value);
    msi.flags = KVM_MSI_VALID_DEVID;
    msi.devid = devid;
    memset(msi.pad, 0, sizeof(msi.pad));

    return kvm_vm_ioctl(kvm_state, KVM_SIGNAL_MSI, &msi);
}

/**
 * vm_change_state_handler - VM change state callback aiming at flushing
 * ITS tables into guest RAM
 *
 * The tables get flushed to guest RAM whenever the VM gets stopped.
 */
static void vm_change_state_handler(void *opaque, bool running,
                                    RunState state)
{
    GICv3ITSState *s = (GICv3ITSState *)opaque;
    Error *err = NULL;

    if (running) {
        return;
    }

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
                      KVM_DEV_ARM_ITS_SAVE_TABLES, NULL, true, &err);
    if (err) {
        error_report_err(err);
    }
}

static void kvm_arm_its_realize(DeviceState *dev, Error **errp)
{
    GICv3ITSState *s = ARM_GICV3_ITS_COMMON(dev);

    s->dev_fd = kvm_create_device(kvm_state, KVM_DEV_TYPE_ARM_VGIC_ITS, false);
    if (s->dev_fd < 0) {
        error_setg_errno(errp, -s->dev_fd, "error creating in-kernel ITS");
        return;
    }

    /* explicit init of the ITS */
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
                      KVM_DEV_ARM_VGIC_CTRL_INIT, NULL, true, &error_abort);

    /* register the base address */
    kvm_arm_register_device(&s->iomem_its_cntrl, -1, KVM_DEV_ARM_VGIC_GRP_ADDR,
                            KVM_VGIC_ITS_ADDR_TYPE, s->dev_fd, 0);

    gicv3_add_its(s->gicv3, dev);

    gicv3_its_init_mmio(s, NULL, NULL);

    if (!kvm_device_check_attr(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
        GITS_CTLR)) {
        error_setg(&s->migration_blocker, "This operating system kernel "
                   "does not support vITS migration");
        if (migrate_add_blocker(&s->migration_blocker, errp) < 0) {
            return;
        }
    } else {
        qemu_add_vm_change_state_handler(vm_change_state_handler, s);
    }

    kvm_msi_use_devid = true;
    kvm_gsi_direct_mapping = false;
    kvm_msi_via_irqfd_allowed = true;
}

/**
 * kvm_arm_its_pre_save - handles the saving of ITS registers.
 * ITS tables are flushed into guest RAM separately and earlier,
 * through the VM change state handler, since at the moment pre_save()
 * is called, the guest RAM has already been saved.
 */
static void kvm_arm_its_pre_save(GICv3ITSState *s)
{
    int i;

    for (i = 0; i < 8; i++) {
        kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                          GITS_BASER + i * 8, &s->baser[i], false,
                          &error_abort);
    }

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                      GITS_CTLR, &s->ctlr, false, &error_abort);

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                      GITS_CBASER, &s->cbaser, false, &error_abort);

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                      GITS_CREADR, &s->creadr, false, &error_abort);

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                      GITS_CWRITER, &s->cwriter, false, &error_abort);

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                      GITS_IIDR, &s->iidr, false, &error_abort);
}

/**
 * kvm_arm_its_post_load - Restore both the ITS registers and tables
 */
static void kvm_arm_its_post_load(GICv3ITSState *s)
{
    int i;

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                      GITS_IIDR, &s->iidr, true, &error_abort);

    /*
     * must be written before GITS_CREADR since GITS_CBASER write
     * access resets GITS_CREADR.
     */
    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                      GITS_CBASER, &s->cbaser, true, &error_abort);

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                      GITS_CREADR, &s->creadr, true, &error_abort);

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                      GITS_CWRITER, &s->cwriter, true, &error_abort);


    for (i = 0; i < 8; i++) {
        kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                          GITS_BASER + i * 8, &s->baser[i], true,
                          &error_abort);
    }

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
                      KVM_DEV_ARM_ITS_RESTORE_TABLES, NULL, true,
                      &error_abort);

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                      GITS_CTLR, &s->ctlr, true, &error_abort);
}

static void kvm_arm_its_reset_hold(Object *obj, ResetType type)
{
    GICv3ITSState *s = ARM_GICV3_ITS_COMMON(obj);
    KVMARMITSClass *c = KVM_ARM_ITS_GET_CLASS(s);
    int i;

    if (c->parent_phases.hold) {
        c->parent_phases.hold(obj, type);
    }

    if (kvm_device_check_attr(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
                               KVM_DEV_ARM_ITS_CTRL_RESET)) {
        kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
                          KVM_DEV_ARM_ITS_CTRL_RESET, NULL, true, &error_abort);
        return;
    }

    warn_report("ITS KVM: full reset is not supported by the host kernel");

    if (!kvm_device_check_attr(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                               GITS_CTLR)) {
        return;
    }

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                      GITS_CTLR, &s->ctlr, true, &error_abort);

    kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                      GITS_CBASER, &s->cbaser, true, &error_abort);

    for (i = 0; i < 8; i++) {
        kvm_device_access(s->dev_fd, KVM_DEV_ARM_VGIC_GRP_ITS_REGS,
                          GITS_BASER + i * 8, &s->baser[i], true,
                          &error_abort);
    }
}

static const Property kvm_arm_its_props[] = {
    DEFINE_PROP_LINK("parent-gicv3", GICv3ITSState, gicv3, "kvm-arm-gicv3",
                     GICv3State *),
};

static void kvm_arm_its_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    GICv3ITSCommonClass *icc = ARM_GICV3_ITS_COMMON_CLASS(klass);
    KVMARMITSClass *ic = KVM_ARM_ITS_CLASS(klass);

    dc->realize = kvm_arm_its_realize;
    device_class_set_props(dc, kvm_arm_its_props);
    resettable_class_set_parent_phases(rc, NULL, kvm_arm_its_reset_hold, NULL,
                                       &ic->parent_phases);
    icc->send_msi = kvm_its_send_msi;
    icc->pre_save = kvm_arm_its_pre_save;
    icc->post_load = kvm_arm_its_post_load;
}

static const TypeInfo kvm_arm_its_info = {
    .name = TYPE_KVM_ARM_ITS,
    .parent = TYPE_ARM_GICV3_ITS_COMMON,
    .instance_size = sizeof(GICv3ITSState),
    .class_init = kvm_arm_its_class_init,
    .class_size = sizeof(KVMARMITSClass),
};

static void kvm_arm_its_register_types(void)
{
    type_register_static(&kvm_arm_its_info);
}

type_init(kvm_arm_its_register_types)
