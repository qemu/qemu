#ifndef X86_64_TARGET_SYSCALL_H
#define X86_64_TARGET_SYSCALL_H

#define __USER_CS	(0x33)
#define __USER_DS	(0x2B)

struct target_pt_regs {
	abi_ulong r15;
	abi_ulong r14;
	abi_ulong r13;
	abi_ulong r12;
	abi_ulong rbp;
	abi_ulong rbx;
/* arguments: non interrupts/non tracing syscalls only save up to here */
	abi_ulong r11;
	abi_ulong r10;
	abi_ulong r9;
	abi_ulong r8;
	abi_ulong rax;
	abi_ulong rcx;
	abi_ulong rdx;
	abi_ulong rsi;
	abi_ulong rdi;
	abi_ulong orig_rax;
/* end of arguments */
/* cpu exception frame or undefined */
	abi_ulong rip;
	abi_ulong cs;
	abi_ulong eflags;
	abi_ulong rsp;
	abi_ulong ss;
/* top of stack page */
};

/* Maximum number of LDT entries supported. */
#define TARGET_LDT_ENTRIES	8192
/* The size of each LDT entry. */
#define TARGET_LDT_ENTRY_SIZE	8

#define TARGET_GDT_ENTRIES 16
#define TARGET_GDT_ENTRY_TLS_ENTRIES 3
#define TARGET_GDT_ENTRY_TLS_MIN 12
#define TARGET_GDT_ENTRY_TLS_MAX 14

#if 0 // Redefine this
struct target_modify_ldt_ldt_s {
	unsigned int  entry_number;
        abi_ulong     base_addr;
	unsigned int  limit;
	unsigned int  seg_32bit:1;
	unsigned int  contents:2;
	unsigned int  read_exec_only:1;
	unsigned int  limit_in_pages:1;
	unsigned int  seg_not_present:1;
	unsigned int  useable:1;
	unsigned int  lm:1;
};
#else
struct target_modify_ldt_ldt_s {
	unsigned int  entry_number;
        abi_ulong     base_addr;
	unsigned int  limit;
        unsigned int flags;
};
#endif

struct target_ipc64_perm
{
	int		key;
	uint32_t	uid;
	uint32_t	gid;
	uint32_t	cuid;
	uint32_t	cgid;
	unsigned short		mode;
	unsigned short		__pad1;
	unsigned short		seq;
	unsigned short		__pad2;
	abi_ulong		__unused1;
	abi_ulong		__unused2;
};

struct target_msqid64_ds {
	struct target_ipc64_perm msg_perm;
	unsigned int msg_stime;	/* last msgsnd time */
	unsigned int msg_rtime;	/* last msgrcv time */
	unsigned int msg_ctime;	/* last change time */
	abi_ulong  msg_cbytes;	/* current number of bytes on queue */
	abi_ulong  msg_qnum;	/* number of messages in queue */
	abi_ulong  msg_qbytes;	/* max number of bytes on queue */
	unsigned int msg_lspid;	/* pid of last msgsnd */
	unsigned int msg_lrpid;	/* last receive pid */
	abi_ulong  __unused4;
	abi_ulong  __unused5;
};

#define UNAME_MACHINE "x86_64"
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_ARCH_SET_GS 0x1001
#define TARGET_ARCH_SET_FS 0x1002
#define TARGET_ARCH_GET_FS 0x1003
#define TARGET_ARCH_GET_GS 0x1004
#define TARGET_MINSIGSTKSZ 2048
#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4

#endif /* X86_64_TARGET_SYSCALL_H */
