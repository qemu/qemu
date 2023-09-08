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

#ifndef HW_I386_KVM_CLOCK_H
#define HW_I386_KVM_CLOCK_H

void kvmclock_create(bool create_always);

#endif
