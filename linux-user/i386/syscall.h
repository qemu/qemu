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

#define TARGET_GDT_ENTRY_TLS_ENTRIES   3
#define TARGET_GDT_ENTRY_TLS_MIN       6
#define TARGET_GDT_ENTRY_TLS_MAX       (TARGET_GDT_ENTRY_TLS_MIN + TARGET_GDT_ENTRY_TLS_ENTRIES - 1)

struct target_modify_ldt_ldt_s {
    unsigned int  entry_number;
    target_ulong base_addr;
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
	target_long ebx;
	target_long ecx;
	target_long edx;
	target_long esi;
	target_long edi;
	target_long ebp;
	target_long eax;
	target_long __null_ds;
	target_long __null_es;
	target_long __null_fs;
	target_long __null_gs;
	target_long orig_eax;
	target_long eip;
	unsigned short cs, __csh;
	target_long eflags;
	target_long esp;
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
	target_ulong __map[8];			/* 256 bits */
};

struct target_vm86_struct {
	struct target_vm86_regs regs;
	target_ulong flags;
	target_ulong screen_bitmap;
	target_ulong cpu_type;
	struct target_revectored_struct int_revectored;
	struct target_revectored_struct int21_revectored;
};

/*
 * flags masks
 */
#define TARGET_VM86_SCREEN_BITMAP	0x0001

struct target_vm86plus_info_struct {
        target_ulong flags;
#define TARGET_force_return_for_pic (1 << 0)
#define TARGET_vm86dbg_active       (1 << 1)  /* for debugger */
#define TARGET_vm86dbg_TFpendig     (1 << 2)  /* for debugger */
#define TARGET_is_vm86pus           (1 << 31) /* for vm86 internal use */
	unsigned char vm86dbg_intxxtab[32];   /* for debugger */
};

struct target_vm86plus_struct {
	struct target_vm86_regs regs;
	target_ulong flags;
	target_ulong screen_bitmap;
	target_ulong cpu_type;
	struct target_revectored_struct int_revectored;
	struct target_revectored_struct int21_revectored;
	struct target_vm86plus_info_struct vm86plus;
};

/* ipcs */

#define TARGET_SEMOP           1
#define TARGET_SEMGET          2
#define TARGET_SEMCTL          3 
#define TARGET_MSGSND          11 
#define TARGET_MSGRCV          12
#define TARGET_MSGGET          13
#define TARGET_MSGCTL          14
#define TARGET_SHMAT           21
#define TARGET_SHMDT           22
#define TARGET_SHMGET          23
#define TARGET_SHMCTL          24

struct target_msgbuf {
	int mtype;
	char mtext[1];
};

struct target_ipc_kludge {
	unsigned int	msgp;	/* Really (struct msgbuf *) */
	int msgtyp;
};	

struct target_ipc_perm {
	int	key;
	unsigned short	uid;
	unsigned short	gid;
	unsigned short	cuid;
	unsigned short	cgid;
	unsigned short	mode;
	unsigned short	seq;
};

struct target_msqid_ds {
	struct target_ipc_perm	msg_perm;
	unsigned int		msg_first;	/* really struct target_msg* */
	unsigned int		msg_last;	/* really struct target_msg* */
	unsigned int		msg_stime;	/* really target_time_t */
	unsigned int		msg_rtime;	/* really target_time_t */
	unsigned int		msg_ctime;	/* really target_time_t */
	unsigned int		wwait;		/* really struct wait_queue* */
	unsigned int		rwait;		/* really struct wait_queue* */
	unsigned short		msg_cbytes;
	unsigned short		msg_qnum;
	unsigned short		msg_qbytes;
	unsigned short		msg_lspid;
	unsigned short		msg_lrpid;
};

struct target_shmid_ds {
	struct target_ipc_perm	shm_perm;
	int			shm_segsz;
	unsigned int		shm_atime;	/* really target_time_t */
	unsigned int		shm_dtime;	/* really target_time_t */
	unsigned int		shm_ctime;	/* really target_time_t */
	unsigned short		shm_cpid;
	unsigned short		shm_lpid;
	short			shm_nattch;
	unsigned short		shm_npages;
	unsigned long		*shm_pages;
	void 			*attaches;	/* really struct shm_desc * */
};

#define TARGET_IPC_RMID	0
#define TARGET_IPC_SET	1
#define TARGET_IPC_STAT	2

union target_semun {
    int val;
    unsigned int buf;	/* really struct semid_ds * */
    unsigned int array; /* really unsigned short * */
    unsigned int __buf;	/* really struct seminfo * */
    unsigned int __pad;	/* really void* */
};

#define UNAME_MACHINE "i686"
