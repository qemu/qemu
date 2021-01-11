/*
 *  i386 system call definitions
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TARGET_SYSCALL_H
#define TARGET_SYSCALL_H

/* default linux values for the selectors */
#define __USER_CS	(0x23)
#define __USER_DS	(0x2B)

struct target_pt_regs {
	long ebx;
	long ecx;
	long edx;
	long esi;
	long edi;
	long ebp;
	long eax;
	int  xds;
	int  xes;
	long orig_eax;
	long eip;
	int  xcs;
	long eflags;
	long esp;
	int  xss;
};

/* ioctls */

#define TARGET_LDT_ENTRIES      8192
#define TARGET_LDT_ENTRY_SIZE	8

#define TARGET_GDT_ENTRIES             9
#define TARGET_GDT_ENTRY_TLS_ENTRIES   3
#define TARGET_GDT_ENTRY_TLS_MIN       6
#define TARGET_GDT_ENTRY_TLS_MAX       (TARGET_GDT_ENTRY_TLS_MIN + TARGET_GDT_ENTRY_TLS_ENTRIES - 1)

struct target_modify_ldt_ldt_s {
    unsigned int  entry_number;
    abi_ulong base_addr;
    unsigned int limit;
    unsigned int flags;
};

/* vm86 defines */

#define TARGET_BIOSSEG		0x0f000

#define TARGET_CPU_086		0
#define TARGET_CPU_186		1
#define TARGET_CPU_286		2
#define TARGET_CPU_386		3
#define TARGET_CPU_486		4
#define TARGET_CPU_586		5

#define TARGET_VM86_SIGNAL	0	/* return due to signal */
#define TARGET_VM86_UNKNOWN	1	/* unhandled GP fault - IO-instruction or similar */
#define TARGET_VM86_INTx	2	/* int3/int x instruction (ARG = x) */
#define TARGET_VM86_STI	3	/* sti/popf/iret instruction enabled virtual interrupts */

/*
 * Additional return values when invoking new vm86()
 */
#define TARGET_VM86_PICRETURN	4	/* return due to pending PIC request */
#define TARGET_VM86_TRAP	6	/* return due to DOS-debugger request */

/*
 * function codes when invoking new vm86()
 */
#define TARGET_VM86_PLUS_INSTALL_CHECK	0
#define TARGET_VM86_ENTER		1
#define TARGET_VM86_ENTER_NO_BYPASS	2
#define	TARGET_VM86_REQUEST_IRQ	3
#define TARGET_VM86_FREE_IRQ		4
#define TARGET_VM86_GET_IRQ_BITS	5
#define TARGET_VM86_GET_AND_RESET_IRQ	6

/*
 * This is the stack-layout seen by the user space program when we have
 * done a translation of "SAVE_ALL" from vm86 mode. The real kernel layout
 * is 'kernel_vm86_regs' (see below).
 */

struct target_vm86_regs {
/*
 * normal regs, with special meaning for the segment descriptors..
 */
	abi_long ebx;
	abi_long ecx;
	abi_long edx;
	abi_long esi;
	abi_long edi;
	abi_long ebp;
	abi_long eax;
	abi_long __null_ds;
	abi_long __null_es;
	abi_long __null_fs;
	abi_long __null_gs;
	abi_long orig_eax;
	abi_long eip;
	unsigned short cs, __csh;
	abi_long eflags;
	abi_long esp;
	unsigned short ss, __ssh;
/*
 * these are specific to v86 mode:
 */
	unsigned short es, __esh;
	unsigned short ds, __dsh;
	unsigned short fs, __fsh;
	unsigned short gs, __gsh;
};

struct target_revectored_struct {
	abi_ulong __map[8];			/* 256 bits */
};

struct target_vm86_struct {
	struct target_vm86_regs regs;
	abi_ulong flags;
	abi_ulong screen_bitmap;
	abi_ulong cpu_type;
	struct target_revectored_struct int_revectored;
	struct target_revectored_struct int21_revectored;
};

/*
 * flags masks
 */
#define TARGET_VM86_SCREEN_BITMAP	0x0001

struct target_vm86plus_info_struct {
        abi_ulong flags;
#define TARGET_force_return_for_pic (1 << 0)
#define TARGET_vm86dbg_active       (1 << 1)  /* for debugger */
#define TARGET_vm86dbg_TFpendig     (1 << 2)  /* for debugger */
#define TARGET_is_vm86pus           (1 << 31) /* for vm86 internal use */
	unsigned char vm86dbg_intxxtab[32];   /* for debugger */
};

struct target_vm86plus_struct {
	struct target_vm86_regs regs;
	abi_ulong flags;
	abi_ulong screen_bitmap;
	abi_ulong cpu_type;
	struct target_revectored_struct int_revectored;
	struct target_revectored_struct int21_revectored;
	struct target_vm86plus_info_struct vm86plus;
};

/* FreeBSD sysarch(2) */
#define TARGET_FREEBSD_I386_GET_LDT	0
#define TARGET_FREEBSD_I386_SET_LDT	1
				/* I386_IOPL */
#define TARGET_FREEBSD_I386_GET_IOPERM	3
#define TARGET_FREEBSD_I386_SET_IOPERM	4
				/* xxxxx */
#define TARGET_FREEBSD_I386_VM86	6
#define TARGET_FREEBSD_I386_GET_FSBASE	7
#define TARGET_FREEBSD_I386_SET_FSBASE	8
#define TARGET_FREEBSD_I386_GET_GSBASE	9
#define TARGET_FREEBSD_I386_SET_GSBASE	10


#define UNAME_MACHINE "i386"
#define TARGET_HW_MACHINE UNAME_MACHINE
#define TARGET_HW_MACHINE_ARCH UNAME_MACHINE

#endif /* TARGET_SYSCALL_H */
