/*
 * Guest driven VM launch component update (using IGVM) device
 * For details and specification, please look at docs/specs/vmlaunchupdate.rst.
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * Authors: Ani Sinha <anisinha@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "system/physmem.h"
#include "system/reset.h"
#include "qemu/target-info-qapi.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/core/qdev-properties.h"
#include "hw/i386/pc.h"
#include "exec/cpu-common.h"
#include "hw/misc/vmlaunchupdate.h"
#include "system/igvm.h"
#include "system/igvm-internal.h"
#include "qemu/error-report.h"
#include "trace.h"

/* returns NULL unless there is exactly one device */
static VMLaunchUpdateState *vm_launchupdate_find(void)
{
    Object *o = object_resolve_path_type("", TYPE_VMLAUNCHUPDATE, NULL);

    return o ? VMLAUNCHUPDATE(o) : NULL;
}

static bool vmlaunchupdate_supported(void)
{
    return target_arch() == SYS_EMU_TARGET_X86_64;
}

static void init_vm_launch_update(VMLaunchUpdateState *s)
{
    s->launch_update.capabilities = VM_LAUNCHUPDATE_FORMAT_IGVM;
    s->launch_update.control = 0;

    if (s->disabled) {
        s->launch_update.control |= VM_LAUNCHUPDATE_CTL_DISABLE;
    }

    s->launch_update.version = VM_LAUNCHUPDATE_VERSION;
    return;
}

static void clear_init_vm_launch_update(VMLaunchUpdateState *s)
{
    memset(&s->launch_update, 0, sizeof(s->launch_update));
    init_vm_launch_update(s);
}

static bool no_igvmcfg(X86MachineState *x86m)
{
    IgvmCfg *igvmc;

    if (!x86m) {
        return true;
    }

    igvmc = x86m->igvm;

    if (!igvmc) {
        /* The VM was not started with an IGVM, bail */
        info_report("guest was not initially started with IGVM, "
                    "not changing launch state.");
        return true;
    }
    return false;
}

static int process_x86_igvm(VMLaunchUpdateState *s,
                            uint64_t fw_image_addr, uint64_t fw_image_size)
{
    X86MachineState *x86machine = X86_MACHINE(qdev_get_machine());
    IgvmCfg *igvmc = x86machine->igvm;
    IgvmHandle igvm;
    void *image_addr_ptr;
    hwaddr len;

    if (no_igvmcfg(x86machine)) {
        return -2;
    }

    if (!fw_image_addr || !fw_image_size) {
        return -1;
    }

    len = (hwaddr) fw_image_size;
    image_addr_ptr = physical_memory_map((hwaddr) fw_image_addr,
                                         (hwaddr *) &len, 0);

    if (!image_addr_ptr || (len < fw_image_size)) {
        warn_report("vmlaunchupdate: Invalid guest addresses.");
        goto err;
    }

    igvm = igvm_new_from_binary(image_addr_ptr, fw_image_size);
    if (igvm < 0) {
        warn_report("vmlaunchupdate: Unable to parse IGVM file %"
                    PRIx64 ": %" PRIx64, fw_image_addr, fw_image_size);
        goto err;
    }

    /* free previous file context */
    if (igvmc->file >= 0) {
        igvm_free(igvmc->file);
    }
    /* set new context */
    igvmc->file = igvm;

    physical_memory_unmap(image_addr_ptr, len, 0, 0);
    info_report("vmlaunchupdate: new IGVM context set.");

    return 0;
 err:
    if (image_addr_ptr) {
        physical_memory_unmap(image_addr_ptr, len, 0, 0);
    }
    return -1;
}

static void restore_host_x86_igvm(void)
{
    X86MachineState *x86machine = X86_MACHINE(qdev_get_machine());
    IgvmCfg *igvmc = x86machine->igvm;
    Error *errp = NULL;

    if (no_igvmcfg(x86machine)) {
        return;
    }

    /* free previous file context */
    if (igvmc->file >= 0) {
        igvm_free(igvmc->file);
    }

    info_report("restoring original host IGVM: %s", igvmc->filename);
    igvmc->file = qigvm_file_init(igvmc->filename, &errp);
    assert(!errp);

    info_report("vmlaunchupdate: host IGVM context set.");

    trace_restore_host_x86_igvm();

    return;
}

static bool fw_address_cleared(VMLaunchUpdateState *s)
{
    return !s->launch_update.fw_image_addr &&
        !s->launch_update.fw_image_size;
}

static void launch_update_write(void *dev, off_t offset, size_t len)
{
    VMLaunchUpdateState *s = VMLAUNCHUPDATE(dev);
    uint64_t addr;
    uint64_t size;
    int rc;

    s->launch_update.status = VM_LAUNCHUPDATE_SUCCESS;

    if (s->disabled) {
        goto end;
    }

    if (s->launch_update.control & VM_LAUNCHUPDATE_CTL_DISABLE) {
        s->disabled = true;
        goto end;
    }

    if (fw_address_cleared(s) &&
        (s->launch_update.control & VM_LAUNCHUPDATE_CTL_HOST_IGVM)) {
        /* restore host IGVM on immediate next reset */
        s->host_igvm_on_reset = true;
        goto end;
    }

    if (!(s->launch_update.control & VM_LAUNCHUPDATE_FORMAT_IGVM) &&
        !fw_address_cleared(s)) {
        /* at least one address provided but the format is not IGVM */
        s->launch_update.status = VM_LAUNCHUPDATE_LOAD_FAIL;
        goto end;
    }

    /* process guest provided IGVM image */
    if (s->launch_update.control & VM_LAUNCHUPDATE_FORMAT_IGVM) {
        if (target_arch() == SYS_EMU_TARGET_X86_64) {
            addr = le64_to_cpu(s->launch_update.fw_image_addr);
            size = le64_to_cpu(s->launch_update.fw_image_size);
            rc = process_x86_igvm(s, addr, size);
            if (rc < 0) {
                switch (rc) {
                case -2:
                    s->launch_update.status = VM_LAUNCHUPDATE_NOT_IGVM_INIT;
                    break;
                default:
                    s->launch_update.status = VM_LAUNCHUPDATE_LOAD_FAIL;
                }
                goto end;
            }
        }
        /* process other machines here when support is added */
    }

    /* clear the addresses */
    s->launch_update.fw_image_addr = 0x0;
    s->launch_update.fw_image_size = 0x0;

 end:
    trace_launch_update_write();
    return;
}

static void launch_update_select(void *dev)
{
    VMLaunchUpdateState *s = VMLAUNCHUPDATE(dev);
    init_vm_launch_update(s);
}

static void vmlaunch_reset_enter(Object *obj, ResetType type)
{
    VMLaunchUpdateState *s = VMLAUNCHUPDATE(obj);

    if (target_arch() != SYS_EMU_TARGET_X86_64) {
        return;
    }

    if (s->host_igvm_on_reset) {
        restore_host_x86_igvm();
        s->host_igvm_on_reset = false;
        /* restoring host igvm enables the interface again */
        s->disabled = false;
        /* clear the host IGVM ctrl bit */
        s->launch_update.control &= ~VM_LAUNCHUPDATE_CTL_HOST_IGVM;
    }

    if ((s->launch_update.control & VM_LAUNCHUPDATE_CTL_HOST_IGVM) &&
        (s->launch_update.status == VM_LAUNCHUPDATE_SUCCESS)) {
        info_report("vmlaunchupdate: next reset will use host igvm");
        s->host_igvm_on_reset = true;
    }

    trace_vmlaunch_reset_enter();
}

static ResettableState *vmlaunch_reset_state(Object *obj)
{
    VMLaunchUpdateState *s = VMLAUNCHUPDATE(obj);

    return &s->reset_state;
}

static void vm_launchupdate_realize(DeviceState *dev, Error **errp)
{
    VMLaunchUpdateState *s = VMLAUNCHUPDATE(dev);
    FWCfgState *fw_cfg = fw_cfg_find();

    /* multiple devices are not supported */
    if (!vm_launchupdate_find()) {
        error_setg(errp, "at most one %s device is permitted",
                   TYPE_VMLAUNCHUPDATE);
        return;
    }

    /* if current machine is not supported, do not initialize */
    if (!vmlaunchupdate_supported()) {
        error_setg(errp,
                   "This machine does not support vm-launch-update device");
        return;
    }

    /* fw_cfg with DMA support is necessary to support this device */
    if (!fw_cfg || !fw_cfg_dma_enabled(fw_cfg)) {
        error_setg(errp, "%s device requires fw_cfg",
                   TYPE_VMLAUNCHUPDATE);
        return;
    }

    fw_cfg_add_file_callback(fw_cfg, FILE_VMLAUNCHUPDATE,
                             launch_update_select, launch_update_write, s,
                             &s->launch_update,
                             sizeof(s->launch_update),
                             false);

    clear_init_vm_launch_update(s);
    /*
     * This device requires to register a global reset because it is
     * not plugged to a bus (which, as its QOM parent, would reset it).
     */
    qemu_register_resettable(OBJECT(s));
}

static void vm_launchupdate_finalize(Object *obj)
{
    qemu_unregister_resettable(obj);
    trace_vm_launchupdate_finalize();
}

static void vmlaunchupdate_device_class_init(ObjectClass *klass,
                                             const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    /* we are not interested in migration - so no need to populate dc->vmsd */
    dc->desc = "VM launch state update device";
    dc->realize = vm_launchupdate_realize;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    rc->phases.enter = vmlaunch_reset_enter;
    rc->get_state = vmlaunch_reset_state;
}

static const TypeInfo vmlaunchupdate_device_types[] = {
    {
        .name              = TYPE_VMLAUNCHUPDATE,
        .parent            = TYPE_DEVICE,
        .instance_size     = sizeof(VMLaunchUpdateState),
        .class_init        = vmlaunchupdate_device_class_init,
        .instance_finalize = vm_launchupdate_finalize,
    },
};

DEFINE_TYPES(vmlaunchupdate_device_types)
