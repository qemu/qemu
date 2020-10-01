/*
 * QEMU KVM support, paravirtual clock device
 *
 * Copyright (C) 2011 Siemens AG
 *
 * Authors:
 *  Jan Kiszka        <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_KVM_CLOCK_H
#define HW_KVM_CLOCK_H

#ifdef CONFIG_KVM

void kvmclock_create(bool create_always);

#else /* CONFIG_KVM */

static inline void kvmclock_create(bool create_always)
{
}

#endif /* !CONFIG_KVM */

#endif
