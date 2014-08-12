/*
 *  PPC emulation for qemu: syscall definitions.
 *
 *  Copyright (c) 2003 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/* XXX: ABSOLUTELY BUGGY:
 * for now, this is quite just a cut-and-paste from i386 target...
 */

/* default linux values for the selectors */
#define __USER_DS	(1)

struct target_pt_regs {
	abi_ulong gpr[32];
	abi_ulong nip;
	abi_ulong msr;
	abi_ulong orig_gpr3;	/* Used for restarting system calls */
	abi_ulong ctr;
	abi_ulong link;
	abi_ulong xer;
	abi_ulong ccr;
#if defined(TARGET_PPC64) && !defined(TARGET_ABI32)
        abi_ulong softe;
#else
	abi_ulong mq;		/* 601 only (not used at present) */
#endif
					/* Used on APUS to hold IPL value. */
	abi_ulong trap;		/* Reason for being here */
	abi_ulong dar;		/* Fault registers */
	abi_ulong dsisr;
	abi_ulong result; 		/* Result of a system call */
};

/* ioctls */
struct target_revectored_struct {
	abi_ulong __map[8];			/* 256 bits */
};

/* Nasty hack: define a fake errno value for use by sigreturn.  */
#define TARGET_QEMU_ESIGRETURN 255

/*
 * flags masks
 */

#if defined(TARGET_PPC64) && !defined(TARGET_ABI32)
#ifdef TARGET_WORDS_BIGENDIAN
#define UNAME_MACHINE "ppc64"
#else
#define UNAME_MACHINE "ppc64le"
#endif
#else
#define UNAME_MACHINE "ppc"
#endif
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_CLONE_BACKWARDS

#define TARGET_MINSIGSTKSZ 2048
