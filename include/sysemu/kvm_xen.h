/*
 * Xen HVM emulation support in KVM
 *
 * Copyright © 2019 Oracle and/or its affiliates. All rights reserved.
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_SYSEMU_KVM_XEN_H
#define QEMU_SYSEMU_KVM_XEN_H

uint32_t kvm_xen_get_caps(void);

#define kvm_xen_has_cap(cap) (!!(kvm_xen_get_caps() &           \
                                 KVM_XEN_HVM_CONFIG_ ## cap))

#endif /* QEMU_SYSEMU_KVM_XEN_H */
