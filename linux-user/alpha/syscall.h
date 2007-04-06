/* default linux values for the selectors */
#define __USER_DS	(1)

struct target_pt_regs {
	target_ulong r0;
	target_ulong r1;
	target_ulong r2;
	target_ulong r3;
	target_ulong r4;
	target_ulong r5;
	target_ulong r6;
	target_ulong r7;
	target_ulong r8;
	target_ulong r19;
	target_ulong r20;
	target_ulong r21;
	target_ulong r22;
	target_ulong r23;
	target_ulong r24;
	target_ulong r25;
	target_ulong r26;
	target_ulong r27;
	target_ulong r28;
	target_ulong hae;
/* JRP - These are the values provided to a0-a2 by PALcode */
	target_ulong trap_a0;
	target_ulong trap_a1;
	target_ulong trap_a2;
/* These are saved by PAL-code: */
	target_ulong ps;
	target_ulong pc;
	target_ulong gp;
	target_ulong r16;
	target_ulong r17;
	target_ulong r18;
/* Those is needed by qemu to temporary store the user stack pointer */
        target_ulong usp;
        target_ulong unique;
};

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

#define UNAME_MACHINE "alpha"
