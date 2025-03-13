/*
 * QEMU Confidential Guest support
 *   This interface describes the common pieces between various
 *   schemes for protecting guest memory or other state against a
 *   compromised hypervisor.  This includes memory encryption (AMD's
 *   SEV and Intel's MKTME) or special protection modes (PEF on POWER,
 *   or PV on s390x).
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
#ifndef QEMU_CONFIDENTIAL_GUEST_SUPPORT_H
#define QEMU_CONFIDENTIAL_GUEST_SUPPORT_H

#include "qom/object.h"

#define TYPE_CONFIDENTIAL_GUEST_SUPPORT "confidential-guest-support"
OBJECT_DECLARE_TYPE(ConfidentialGuestSupport,
                    ConfidentialGuestSupportClass,
                    CONFIDENTIAL_GUEST_SUPPORT)


struct ConfidentialGuestSupport {
    Object parent;

    /*
     * True if the machine should use guest_memfd for RAM.
     */
    bool require_guest_memfd;

    /*
     * ready: flag set by CGS initialization code once it's ready to
     *        start executing instructions in a potentially-secure
     *        guest
     *
     * The definition here is a bit fuzzy, because this is essentially
     * part of a self-sanity-check, rather than a strict mechanism.
     *
     * It's not feasible to have a single point in the common machine
     * init path to configure confidential guest support, because
     * different mechanisms have different interdependencies requiring
     * initialization in different places, often in arch or machine
     * type specific code.  It's also usually not possible to check
     * for invalid configurations until that initialization code.
     * That means it would be very easy to have a bug allowing CGS
     * init to be bypassed entirely in certain configurations.
     *
     * Silently ignoring a requested security feature would be bad, so
     * to avoid that we check late in init that this 'ready' flag is
     * set if CGS was requested.  If the CGS init hasn't happened, and
     * so 'ready' is not set, we'll abort.
     */
    bool ready;
};

typedef struct ConfidentialGuestSupportClass {
    ObjectClass parent;

    int (*kvm_init)(ConfidentialGuestSupport *cgs, Error **errp);
    int (*kvm_reset)(ConfidentialGuestSupport *cgs, Error **errp);
} ConfidentialGuestSupportClass;

static inline int confidential_guest_kvm_init(ConfidentialGuestSupport *cgs,
                                              Error **errp)
{
    ConfidentialGuestSupportClass *klass;

    klass = CONFIDENTIAL_GUEST_SUPPORT_GET_CLASS(cgs);
    if (klass->kvm_init) {
        return klass->kvm_init(cgs, errp);
    }

    return 0;
}

static inline int confidential_guest_kvm_reset(ConfidentialGuestSupport *cgs,
                                               Error **errp)
{
    ConfidentialGuestSupportClass *klass;

    klass = CONFIDENTIAL_GUEST_SUPPORT_GET_CLASS(cgs);
    if (klass->kvm_reset) {
        return klass->kvm_reset(cgs, errp);
    }

    return 0;
}

#endif /* QEMU_CONFIDENTIAL_GUEST_SUPPORT_H */
