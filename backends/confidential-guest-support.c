/*
 * QEMU Confidential Guest support
 *
 * Copyright Red Hat.
 *
 * Authors:
 *  David Gibson <david@gibson.dropbear.id.au>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "system/confidential-guest-support.h"

OBJECT_DEFINE_ABSTRACT_TYPE(ConfidentialGuestSupport,
                            confidential_guest_support,
                            CONFIDENTIAL_GUEST_SUPPORT,
                            OBJECT)

static void confidential_guest_support_class_init(ObjectClass *oc, void *data)
{
}

static void confidential_guest_support_init(Object *obj)
{
}

static void confidential_guest_support_finalize(Object *obj)
{
}
