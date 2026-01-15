/*
 * ARM security space helpers
 *
 * Provide ARMSecuritySpace and helpers for code that is not tied to CPU.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_ARM_SECURITY_H
#define HW_ARM_ARM_SECURITY_H

/*
 * ARM v9 security states.
 * The ordering of the enumeration corresponds to the low 2 bits
 * of the GPI value, and (except for Root) the concat of NSE:NS.
 */

 typedef enum ARMSecuritySpace {
    ARMSS_Secure     = 0,
    ARMSS_NonSecure  = 1,
    ARMSS_Root       = 2,
    ARMSS_Realm      = 3,
} ARMSecuritySpace;

/* Return true if @space is secure, in the pre-v9 sense. */
static inline bool arm_space_is_secure(ARMSecuritySpace space)
{
    return space == ARMSS_Secure || space == ARMSS_Root;
}

/* Return the ARMSecuritySpace for @secure, assuming !RME or EL[0-2]. */
static inline ARMSecuritySpace arm_secure_to_space(bool secure)
{
    return secure ? ARMSS_Secure : ARMSS_NonSecure;
}

#endif /* HW_ARM_ARM_SECURITY_H */
