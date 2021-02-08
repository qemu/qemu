/*
 * PEF (Protected Execution Facility) for POWER support
 *
 * Copyright Red Hat.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "sysemu/kvm.h"
#include "migration/blocker.h"
#include "exec/confidential-guest-support.h"
#include "hw/ppc/pef.h"

#define TYPE_PEF_GUEST "pef-guest"
OBJECT_DECLARE_SIMPLE_TYPE(PefGuest, PEF_GUEST)

typedef struct PefGuest PefGuest;
typedef struct PefGuestClass PefGuestClass;

struct PefGuestClass {
    ConfidentialGuestSupportClass parent_class;
};

/**
 * PefGuest:
 *
 * The PefGuest object is used for creating and managing a PEF
 * guest.
 *
 * # $QEMU \
 *         -object pef-guest,id=pef0 \
 *         -machine ...,confidential-guest-support=pef0
 */
struct PefGuest {
    ConfidentialGuestSupport parent_obj;
};

static int kvmppc_svm_init(Error **errp)
{
#ifdef CONFIG_KVM
    static Error *pef_mig_blocker;

    if (!kvm_check_extension(kvm_state, KVM_CAP_PPC_SECURE_GUEST)) {
        error_setg(errp,
                   "KVM implementation does not support Secure VMs (is an ultravisor running?)");
        return -1;
    } else {
        int ret = kvm_vm_enable_cap(kvm_state, KVM_CAP_PPC_SECURE_GUEST, 0, 1);

        if (ret < 0) {
            error_setg(errp,
                       "Error enabling PEF with KVM");
            return -1;
        }
    }

    /* add migration blocker */
    error_setg(&pef_mig_blocker, "PEF: Migration is not implemented");
    /* NB: This can fail if --only-migratable is used */
    migrate_add_blocker(pef_mig_blocker, &error_fatal);

    return 0;
#else
    g_assert_not_reached();
#endif
}

/*
 * Don't set error if KVM_PPC_SVM_OFF ioctl is invoked on kernels
 * that don't support this ioctl.
 */
static int kvmppc_svm_off(Error **errp)
{
#ifdef CONFIG_KVM
    int rc;

    rc = kvm_vm_ioctl(KVM_STATE(current_accel()), KVM_PPC_SVM_OFF);
    if (rc && rc != -ENOTTY) {
        error_setg_errno(errp, -rc, "KVM_PPC_SVM_OFF ioctl failed");
        return rc;
    }
    return 0;
#else
    g_assert_not_reached();
#endif
}

int pef_kvm_init(ConfidentialGuestSupport *cgs, Error **errp)
{
    if (!object_dynamic_cast(OBJECT(cgs), TYPE_PEF_GUEST)) {
        return 0;
    }

    if (!kvm_enabled()) {
        error_setg(errp, "PEF requires KVM");
        return -1;
    }

    return kvmppc_svm_init(errp);
}

int pef_kvm_reset(ConfidentialGuestSupport *cgs, Error **errp)
{
    if (!object_dynamic_cast(OBJECT(cgs), TYPE_PEF_GUEST)) {
        return 0;
    }

    /*
     * If we don't have KVM we should never have been able to
     * initialize PEF, so we should never get this far
     */
    assert(kvm_enabled());

    return kvmppc_svm_off(errp);
}

OBJECT_DEFINE_TYPE_WITH_INTERFACES(PefGuest,
                                   pef_guest,
                                   PEF_GUEST,
                                   CONFIDENTIAL_GUEST_SUPPORT,
                                   { TYPE_USER_CREATABLE },
                                   { NULL })

static void pef_guest_class_init(ObjectClass *oc, void *data)
{
}

static void pef_guest_init(Object *obj)
{
}

static void pef_guest_finalize(Object *obj)
{
}
