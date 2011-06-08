/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __POWERPC_KVM_PARA_H__
#define __POWERPC_KVM_PARA_H__

#include <linux/types.h>

struct kvm_vcpu_arch_shared {
	__u64 scratch1;
	__u64 scratch2;
	__u64 scratch3;
	__u64 critical;		/* Guest may not get interrupts if == r1 */
	__u64 sprg0;
	__u64 sprg1;
	__u64 sprg2;
	__u64 sprg3;
	__u64 srr0;
	__u64 srr1;
	__u64 dar;
	__u64 msr;
	__u32 dsisr;
	__u32 int_pending;	/* Tells the guest if we have an interrupt */
	__u32 sr[16];
};

#define KVM_SC_MAGIC_R0		0x4b564d21 /* "KVM!" */
#define HC_VENDOR_KVM		(42 << 16)
#define HC_EV_SUCCESS		0
#define HC_EV_UNIMPLEMENTED	12

#define KVM_FEATURE_MAGIC_PAGE	1

#define KVM_MAGIC_FEAT_SR	(1 << 0)


#endif /* __POWERPC_KVM_PARA_H__ */
