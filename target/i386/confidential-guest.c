/*
 * QEMU Confidential Guest support
 *
 * Copyright (C) 2024 Red Hat, Inc.
 *
 * Authors:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "confidential-guest.h"

OBJECT_DEFINE_ABSTRACT_TYPE(X86ConfidentialGuest,
                            x86_confidential_guest,
                            X86_CONFIDENTIAL_GUEST,
                            CONFIDENTIAL_GUEST_SUPPORT)

static void x86_confidential_guest_class_init(ObjectClass *oc, void *data)
{
}

static void x86_confidential_guest_init(Object *obj)
{
}

static void x86_confidential_guest_finalize(Object *obj)
{
}
