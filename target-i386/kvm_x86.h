/*
 * QEMU KVM support
 *
 * Copyright (C) 2009 Red Hat Inc.
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef __KVM_X86_H__
#define __KVM_X86_H__

#define ABORT_ON_ERROR  0x01
#define MCE_BROADCAST   0x02

void kvm_inject_x86_mce(CPUState *cenv, int bank, uint64_t status,
                        uint64_t mcg_status, uint64_t addr, uint64_t misc,
                        int flag);

#endif
