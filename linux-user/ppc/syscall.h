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
	target_ulong __map[8];			/* 256 bits */
};

/*
 * flags masks
 */

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

#define UNAME_MACHINE "ppc"
