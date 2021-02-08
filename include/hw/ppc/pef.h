/*
 * PEF (Protected Execution Facility) for POWER support
 *
 * Copyright Red Hat.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_PPC_PEF_H
#define HW_PPC_PEF_H

int pef_kvm_init(ConfidentialGuestSupport *cgs, Error **errp);
int pef_kvm_reset(ConfidentialGuestSupport *cgs, Error **errp);

#endif /* HW_PPC_PEF_H */
