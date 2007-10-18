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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* XXX: ABSOLUTELY BUGGY:
 * for now, this is quite just a cut-and-paste from i386 target...
 */

/* default linux values for the selectors */
#define __USER_DS	(1)

struct target_pt_regs {
	unsigned long gpr[32];
	unsigned long nip;
	unsigned long msr;
	unsigned long orig_gpr3;	/* Used for restarting system calls */
	unsigned long ctr;
	unsigned long link;
	unsigned long xer;
	unsigned long ccr;
	unsigned long mq;		/* 601 only (not used at present) */
					/* Used on APUS to hold IPL value. */
	unsigned long trap;		/* Reason for being here */
	unsigned long dar;		/* Fault registers */
	unsigned long dsisr;
	unsigned long result; 		/* Result of a system call */
};

/* ioctls */
struct target_revectored_struct {
	abi_ulong __map[8];			/* 256 bits */
};

/*
 * flags masks
 */

#if defined(TARGET_PPC64) && !defined(TARGET_ABI32)
#define UNAME_MACHINE "ppc64"
#else
#define UNAME_MACHINE "ppc"
#endif
