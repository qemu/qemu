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
#include "qapi/error.h"

OBJECT_DEFINE_ABSTRACT_TYPE(ConfidentialGuestSupport,
                            confidential_guest_support,
                            CONFIDENTIAL_GUEST_SUPPORT,
                            OBJECT)

static bool check_support(ConfidentialGuestPlatformType platform,
                         uint16_t platform_version, uint8_t highest_vtl,
                         uint64_t shared_gpa_boundary)
{
    /* Default: no support. */
    return false;
}

static int set_guest_state(hwaddr gpa, uint8_t *ptr, uint64_t len,
                           ConfidentialGuestPageType memory_type,
                           uint16_t cpu_index, Error **errp)
{
    error_setg(errp,
               "Setting confidential guest state is not supported for this platform");
    return -1;
}

static int set_guest_policy(ConfidentialGuestPolicyType policy_type,
                            uint64_t policy,
                            void *policy_data1, uint32_t policy_data1_size,
                            void *policy_data2, uint32_t policy_data2_size,
                            Error **errp)
{
    error_setg(errp,
               "Setting confidential guest policy is not supported for this platform");
    return -1;
}

static int get_mem_map_entry(int index, ConfidentialGuestMemoryMapEntry *entry,
                             Error **errp)
{
    error_setg(
        errp,
        "Obtaining the confidential guest memory map is not supported for this platform");
    return -1;
}

static void confidential_guest_support_class_init(ObjectClass *oc,
                                                  const void *data)
{
    ConfidentialGuestSupportClass *cgsc = CONFIDENTIAL_GUEST_SUPPORT_CLASS(oc);
    cgsc->check_support = check_support;
    cgsc->set_guest_state = set_guest_state;
    cgsc->set_guest_policy = set_guest_policy;
    cgsc->get_mem_map_entry = get_mem_map_entry;
}

static void confidential_guest_support_init(Object *obj)
{
}

static void confidential_guest_support_finalize(Object *obj)
{
}
