/*
 * QEMU Hyper-V support
 *
 * Copyright Red Hat, Inc. 2011
 *
 * Author: Vadim Rozenfeld     <vrozenfe@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_HW_HYPERV_H
#define QEMU_HW_HYPERV_H 1

#include "qemu-common.h"
#ifdef CONFIG_KVM
#include <asm/hyperv.h>
#endif

#ifndef HYPERV_SPINLOCK_NEVER_RETRY
#define HYPERV_SPINLOCK_NEVER_RETRY             0xFFFFFFFF
#endif

#ifndef KVM_CPUID_SIGNATURE_NEXT
#define KVM_CPUID_SIGNATURE_NEXT                0x40000100
#endif

#if !defined(CONFIG_USER_ONLY) && defined(CONFIG_KVM)
void hyperv_enable_vapic_recommended(bool val);
void hyperv_enable_relaxed_timing(bool val);
void hyperv_set_spinlock_retries(int val);
#else
static inline void hyperv_enable_vapic_recommended(bool val) { }
static inline void hyperv_enable_relaxed_timing(bool val) { }
static inline void hyperv_set_spinlock_retries(int val) { }
#endif

bool hyperv_enabled(void);
bool hyperv_hypercall_available(void);
bool hyperv_vapic_recommended(void);
bool hyperv_relaxed_timing_enabled(void);
int hyperv_get_spinlock_retries(void);

#endif /* QEMU_HW_HYPERV_H */
