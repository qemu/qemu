#define __USER_CS	(0x33)
#define __USER_DS	(0x2B)

struct target_pt_regs {
	target_ulong r15;
	target_ulong r14;
	target_ulong r13;
	target_ulong r12;
	target_ulong rbp;
	target_ulong rbx;
/* arguments: non interrupts/non tracing syscalls only save upto here*/
 	target_ulong r11;
	target_ulong r10;
	target_ulong r9;
	target_ulong r8;
	target_ulong rax;
	target_ulong rcx;
	target_ulong rdx;
	target_ulong rsi;
	target_ulong rdi;
	target_ulong orig_rax;
/* end of arguments */
/* cpu exception frame or undefined */
	target_ulong rip;
	target_ulong cs;
	target_ulong eflags;
	target_ulong rsp;
	target_ulong ss;
/* top of stack page */
};

/* Maximum number of LDT entries supported. */
#define TARGET_LDT_ENTRIES	8192
/* The size of each LDT entry. */
#define TARGET_LDT_ENTRY_SIZE	8

#define TARGET_GDT_ENTRY_TLS_ENTRIES 3
#define TARGET_GDT_ENTRY_TLS_MIN 12
#define TARGET_GDT_ENTRY_TLS_MAX 14

#if 0 // Redefine this
struct target_modify_ldt_ldt_s {
	unsigned int  entry_number;
	target_ulong  base_addr;
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
	target_ulong  base_addr;
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
	target_ulong		__unused1;
	target_ulong		__unused2;
};

struct target_msqid64_ds {
	struct target_ipc64_perm msg_perm;
	unsigned int msg_stime;	/* last msgsnd time */
	unsigned int msg_rtime;	/* last msgrcv time */
	unsigned int msg_ctime;	/* last change time */
	target_ulong  msg_cbytes;	/* current number of bytes on queue */
	target_ulong  msg_qnum;	/* number of messages in queue */
	target_ulong  msg_qbytes;	/* max number of bytes on queue */
	unsigned int msg_lspid;	/* pid of last msgsnd */
	unsigned int msg_lrpid;	/* last receive pid */
	target_ulong  __unused4;
	target_ulong  __unused5;
};

#define UNAME_MACHINE "x86_64"
