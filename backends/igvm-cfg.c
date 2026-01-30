/*
 * QEMU IGVM interface
 *
 * Copyright (C) 2023-2024 SUSE
 *
 * Authors:
 *  Roy Hopkins <roy.hopkins@randomman.co.uk>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "system/igvm.h"
#include "system/igvm-cfg.h"
#include "system/igvm-internal.h"
#include "system/reset.h"
#include "qom/object_interfaces.h"
#include "hw/core/qdev.h"
#include "hw/core/boards.h"

#include "trace.h"

static char *get_igvm(Object *obj, Error **errp)
{
    IgvmCfg *igvm = IGVM_CFG(obj);
    return g_strdup(igvm->filename);
}

static void set_igvm(Object *obj, const char *value, Error **errp)
{
    IgvmCfg *igvm = IGVM_CFG(obj);
    g_free(igvm->filename);
    igvm->filename = g_strdup(value);
}

static ResettableState *igvm_reset_state(Object *obj)
{
    IgvmCfg *igvm = IGVM_CFG(obj);
    return &igvm->reset_state;
}

static void igvm_reset_enter(Object *obj, ResetType type)
{
    trace_igvm_reset_enter(type);
}

static void igvm_reset_hold(Object *obj, ResetType type)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    IgvmCfg *igvm = IGVM_CFG(obj);

    trace_igvm_reset_hold(type);

    qigvm_process_file(igvm, ms, false, &error_fatal);
}

static void igvm_reset_exit(Object *obj, ResetType type)
{
    trace_igvm_reset_exit(type);
}

static void igvm_complete(UserCreatable *uc, Error **errp)
{
    IgvmCfg *igvm = IGVM_CFG(uc);

    igvm->file = qigvm_file_init(igvm->filename, errp);
}

OBJECT_DEFINE_TYPE_WITH_INTERFACES(IgvmCfg, igvm_cfg, IGVM_CFG, OBJECT,
                                   { TYPE_USER_CREATABLE },
                                   { TYPE_RESETTABLE_INTERFACE },
                                   { NULL })

static void igvm_cfg_class_init(ObjectClass *oc, const void *data)
{
    IgvmCfgClass *igvmc = IGVM_CFG_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    UserCreatableClass *uc = USER_CREATABLE_CLASS(oc);

    object_class_property_add_str(oc, "file", get_igvm, set_igvm);
    object_class_property_set_description(oc, "file",
                                          "Set the IGVM filename to use");

    igvmc->process = qigvm_process_file;

    rc->get_state = igvm_reset_state;
    rc->phases.enter = igvm_reset_enter;
    rc->phases.hold = igvm_reset_hold;
    rc->phases.exit = igvm_reset_exit;

    uc->complete = igvm_complete;
}

static void igvm_cfg_init(Object *obj)
{
    IgvmCfg *igvm = IGVM_CFG(obj);

    igvm->file = -1;
    qemu_register_resettable(obj);
}

static void igvm_cfg_finalize(Object *obj)
{
    IgvmCfg *igvm = IGVM_CFG(obj);

    qemu_unregister_resettable(obj);
    if (igvm->file >= 0) {
        igvm_free(igvm->file);
    }
}
