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
#include "exec/hwaddr.h"

#define TYPE_CONFIDENTIAL_GUEST_SUPPORT "confidential-guest-support"
OBJECT_DECLARE_TYPE(ConfidentialGuestSupport,
                    ConfidentialGuestSupportClass,
                    CONFIDENTIAL_GUEST_SUPPORT)


typedef enum ConfidentialGuestPlatformType {
    CGS_PLATFORM_SEV,
    CGS_PLATFORM_SEV_ES,
    CGS_PLATFORM_SEV_SNP,
} ConfidentialGuestPlatformType;

typedef enum ConfidentialGuestMemoryType {
    CGS_MEM_RAM,
    CGS_MEM_RESERVED,
    CGS_MEM_ACPI,
    CGS_MEM_NVS,
    CGS_MEM_UNUSABLE,
} ConfidentialGuestMemoryType;

typedef struct ConfidentialGuestMemoryMapEntry {
    uint64_t gpa;
    uint64_t size;
    ConfidentialGuestMemoryType type;
} ConfidentialGuestMemoryMapEntry;

typedef enum ConfidentialGuestPageType {
    CGS_PAGE_TYPE_NORMAL,
    CGS_PAGE_TYPE_VMSA,
    CGS_PAGE_TYPE_ZERO,
    CGS_PAGE_TYPE_UNMEASURED,
    CGS_PAGE_TYPE_SECRETS,
    CGS_PAGE_TYPE_CPUID,
    CGS_PAGE_TYPE_REQUIRED_MEMORY,
} ConfidentialGuestPageType;

typedef enum ConfidentialGuestPolicyType {
    GUEST_POLICY_SEV,
} ConfidentialGuestPolicyType;

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

    /*
     * Check to see if this confidential guest supports a particular
     * platform or configuration.
     *
     * Return true if supported or false if not supported.
     */
    bool (*check_support)(ConfidentialGuestPlatformType platform,
                         uint16_t platform_version, uint8_t highest_vtl,
                         uint64_t shared_gpa_boundary);

    /*
     * Configure part of the state of a guest for a particular set of data, page
     * type and gpa. This can be used for example to pre-populate and measure
     * guest memory contents, define private ranges or set the initial CPU state
     * for one or more CPUs.
     *
     * If memory_type is CGS_PAGE_TYPE_VMSA then ptr points to the initial CPU
     * context for a virtual CPU. The format of the data depends on the type of
     * confidential virtual machine. For example, for SEV-ES ptr will point to a
     * vmcb_save_area structure that should be copied into guest memory at the
     * address specified in gpa. The cpu_index parameter contains the index of
     * the CPU the VMSA applies to.
     */
    int (*set_guest_state)(hwaddr gpa, uint8_t *ptr, uint64_t len,
                           ConfidentialGuestPageType memory_type,
                           uint16_t cpu_index, Error **errp);

    /*
     * Set the guest policy. The policy can be used to configure the
     * confidential platform, such as if debug is enabled or not and can contain
     * information about expected launch measurements, signed verification of
     * guest configuration and other platform data.
     *
     * The format of the policy data is specific to each platform. For example,
     * SEV-SNP uses a policy bitfield in the 'policy' argument and provides an
     * ID block and ID authentication in the 'policy_data' parameters. The type
     * of policy data is identified by the 'policy_type' argument.
     */
    int (*set_guest_policy)(ConfidentialGuestPolicyType policy_type,
                            uint64_t policy,
                            void *policy_data1, uint32_t policy_data1_size,
                            void *policy_data2, uint32_t policy_data2_size,
                            Error **errp);

    /*
     * Iterate the system memory map, getting the entry with the given index
     * that can be populated into guest memory.
     *
     * Returns 0 for ok, 1 if the index is out of range and -1 on error.
     */
    int (*get_mem_map_entry)(int index, ConfidentialGuestMemoryMapEntry *entry,
                             Error **errp);
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
