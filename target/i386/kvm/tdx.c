/*
 * QEMU TDX support
 *
 * Copyright (c) 2025 Intel Corporation
 *
 * Author:
 *      Xiaoyao Li <xiaoyao.li@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qom/object_interfaces.h"

#include "tdx.h"

/* tdx guest */
OBJECT_DEFINE_TYPE_WITH_INTERFACES(TdxGuest,
                                   tdx_guest,
                                   TDX_GUEST,
                                   X86_CONFIDENTIAL_GUEST,
                                   { TYPE_USER_CREATABLE },
                                   { NULL })

static void tdx_guest_init(Object *obj)
{
    ConfidentialGuestSupport *cgs = CONFIDENTIAL_GUEST_SUPPORT(obj);
    TdxGuest *tdx = TDX_GUEST(obj);

    cgs->require_guest_memfd = true;
    tdx->attributes = 0;

    object_property_add_uint64_ptr(obj, "attributes", &tdx->attributes,
                                   OBJ_PROP_FLAG_READWRITE);
}

static void tdx_guest_finalize(Object *obj)
{
}

static void tdx_guest_class_init(ObjectClass *oc, const void *data)
{
}
