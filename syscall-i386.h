/* from linux/unistd.h */

#define TARGET_NR_exit		  1
#define TARGET_NR_fork		  2
#define TARGET_NR_read		  3
#define TARGET_NR_write		  4
#define TARGET_NR_open		  5
#define TARGET_NR_close		  6
#define TARGET_NR_waitpid		  7
#define TARGET_NR_creat		  8
#define TARGET_NR_link		  9
#define TARGET_NR_unlink		 10
#define TARGET_NR_execve		 11
#define TARGET_NR_chdir		 12
#define TARGET_NR_time		 13
#define TARGET_NR_mknod		 14
#define TARGET_NR_chmod		 15
#define TARGET_NR_lchown		 16
#define TARGET_NR_break		 17
#define TARGET_NR_oldstat		 18
#define TARGET_NR_lseek		 19
#define TARGET_NR_getpid		 20
#define TARGET_NR_mount		 21
#define TARGET_NR_umount		 22
#define TARGET_NR_setuid		 23
#define TARGET_NR_getuid		 24
#define TARGET_NR_stime		 25
#define TARGET_NR_ptrace		 26
#define TARGET_NR_alarm		 27
#define TARGET_NR_oldfstat		 28
#define TARGET_NR_pause		 29
#define TARGET_NR_utime		 30
#define TARGET_NR_stty		 31
#define TARGET_NR_gtty		 32
#define TARGET_NR_access		 33
#define TARGET_NR_nice		 34
#define TARGET_NR_ftime		 35
#define TARGET_NR_sync		 36
#define TARGET_NR_kill		 37
#define TARGET_NR_rename		 38
#define TARGET_NR_mkdir		 39
#define TARGET_NR_rmdir		 40
#define TARGET_NR_dup		 41
#define TARGET_NR_pipe		 42
#define TARGET_NR_times		 43
#define TARGET_NR_prof		 44
#define TARGET_NR_brk		 45
#define TARGET_NR_setgid		 46
#define TARGET_NR_getgid		 47
#define TARGET_NR_signal		 48
#define TARGET_NR_geteuid		 49
#define TARGET_NR_getegid		 50
#define TARGET_NR_acct		 51
#define TARGET_NR_umount2		 52
#define TARGET_NR_lock		 53
#define TARGET_NR_ioctl		 54
#define TARGET_NR_fcntl		 55
#define TARGET_NR_mpx		 56
#define TARGET_NR_setpgid		 57
#define TARGET_NR_ulimit		 58
#define TARGET_NR_oldolduname	 59
#define TARGET_NR_umask		 60
#define TARGET_NR_chroot		 61
#define TARGET_NR_ustat		 62
#define TARGET_NR_dup2		 63
#define TARGET_NR_getppid		 64
#define TARGET_NR_getpgrp		 65
#define TARGET_NR_setsid		 66
#define TARGET_NR_sigaction		 67
#define TARGET_NR_sgetmask		 68
#define TARGET_NR_ssetmask		 69
#define TARGET_NR_setreuid		 70
#define TARGET_NR_setregid		 71
#define TARGET_NR_sigsuspend		 72
#define TARGET_NR_sigpending		 73
#define TARGET_NR_sethostname	 74
#define TARGET_NR_setrlimit		 75
#define TARGET_NR_getrlimit		 76	/* Back compatible 2Gig limited rlimit */
#define TARGET_NR_getrusage		 77
#define TARGET_NR_gettimeofday	 78
#define TARGET_NR_settimeofday	 79
#define TARGET_NR_getgroups		 80
#define TARGET_NR_setgroups		 81
#define TARGET_NR_select		 82
#define TARGET_NR_symlink		 83
#define TARGET_NR_oldlstat		 84
#define TARGET_NR_readlink		 85
#define TARGET_NR_uselib		 86
#define TARGET_NR_swapon		 87
#define TARGET_NR_reboot		 88
#define TARGET_NR_readdir		 89
#define TARGET_NR_mmap		 90
#define TARGET_NR_munmap		 91
#define TARGET_NR_truncate		 92
#define TARGET_NR_ftruncate		 93
#define TARGET_NR_fchmod		 94
#define TARGET_NR_fchown		 95
#define TARGET_NR_getpriority	 96
#define TARGET_NR_setpriority	 97
#define TARGET_NR_profil		 98
#define TARGET_NR_statfs		 99
#define TARGET_NR_fstatfs		100
#define TARGET_NR_ioperm		101
#define TARGET_NR_socketcall		102
#define TARGET_NR_syslog		103
#define TARGET_NR_setitimer		104
#define TARGET_NR_getitimer		105
#define TARGET_NR_stat		106
#define TARGET_NR_lstat		107
#define TARGET_NR_fstat		108
#define TARGET_NR_olduname		109
#define TARGET_NR_iopl		110
#define TARGET_NR_vhangup		111
#define TARGET_NR_idle		112
#define TARGET_NR_vm86old		113
#define TARGET_NR_wait4		114
#define TARGET_NR_swapoff		115
#define TARGET_NR_sysinfo		116
#define TARGET_NR_ipc		117
#define TARGET_NR_fsync		118
#define TARGET_NR_sigreturn		119
#define TARGET_NR_clone		120
#define TARGET_NR_setdomainname	121
#define TARGET_NR_uname		122
#define TARGET_NR_modify_ldt		123
#define TARGET_NR_adjtimex		124
#define TARGET_NR_mprotect		125
#define TARGET_NR_sigprocmask	126
#define TARGET_NR_create_module	127
#define TARGET_NR_init_module	128
#define TARGET_NR_delete_module	129
#define TARGET_NR_get_kernel_syms	130
#define TARGET_NR_quotactl		131
#define TARGET_NR_getpgid		132
#define TARGET_NR_fchdir		133
#define TARGET_NR_bdflush		134
#define TARGET_NR_sysfs		135
#define TARGET_NR_personality	136
#define TARGET_NR_afs_syscall	137 /* Syscall for Andrew File System */
#define TARGET_NR_setfsuid		138
#define TARGET_NR_setfsgid		139
#define TARGET_NR__llseek		140
#define TARGET_NR_getdents		141
#define TARGET_NR__newselect		142
#define TARGET_NR_flock		143
#define TARGET_NR_msync		144
#define TARGET_NR_readv		145
#define TARGET_NR_writev		146
#define TARGET_NR_getsid		147
#define TARGET_NR_fdatasync		148
#define TARGET_NR__sysctl		149
#define TARGET_NR_mlock		150
#define TARGET_NR_munlock		151
#define TARGET_NR_mlockall		152
#define TARGET_NR_munlockall		153
#define TARGET_NR_sched_setparam		154
#define TARGET_NR_sched_getparam		155
#define TARGET_NR_sched_setscheduler		156
#define TARGET_NR_sched_getscheduler		157
#define TARGET_NR_sched_yield		158
#define TARGET_NR_sched_get_priority_max	159
#define TARGET_NR_sched_get_priority_min	160
#define TARGET_NR_sched_rr_get_interval	161
#define TARGET_NR_nanosleep		162
#define TARGET_NR_mremap		163
#define TARGET_NR_setresuid		164
#define TARGET_NR_getresuid		165
#define TARGET_NR_vm86		166
#define TARGET_NR_query_module	167
#define TARGET_NR_poll		168
#define TARGET_NR_nfsservctl		169
#define TARGET_NR_setresgid		170
#define TARGET_NR_getresgid		171
#define TARGET_NR_prctl              172
#define TARGET_NR_rt_sigreturn	173
#define TARGET_NR_rt_sigaction	174
#define TARGET_NR_rt_sigprocmask	175
#define TARGET_NR_rt_sigpending	176
#define TARGET_NR_rt_sigtimedwait	177
#define TARGET_NR_rt_sigqueueinfo	178
#define TARGET_NR_rt_sigsuspend	179
#define TARGET_NR_pread		180
#define TARGET_NR_pwrite		181
#define TARGET_NR_chown		182
#define TARGET_NR_getcwd		183
#define TARGET_NR_capget		184
#define TARGET_NR_capset		185
#define TARGET_NR_sigaltstack	186
#define TARGET_NR_sendfile		187
#define TARGET_NR_getpmsg		188	/* some people actually want streams */
#define TARGET_NR_putpmsg		189	/* some people actually want streams */
#define TARGET_NR_vfork		190
#define TARGET_NR_ugetrlimit		191	/* SuS compliant getrlimit */
#define TARGET_NR_mmap2		192
#define TARGET_NR_truncate64		193
#define TARGET_NR_ftruncate64	194
#define TARGET_NR_stat64		195
#define TARGET_NR_lstat64		196
#define TARGET_NR_fstat64		197
#define TARGET_NR_lchown32		198
#define TARGET_NR_getuid32		199
#define TARGET_NR_getgid32		200
#define TARGET_NR_geteuid32		201
#define TARGET_NR_getegid32		202
#define TARGET_NR_setreuid32		203
#define TARGET_NR_setregid32		204
#define TARGET_NR_getgroups32	205
#define TARGET_NR_setgroups32	206
#define TARGET_NR_fchown32		207
#define TARGET_NR_setresuid32	208
#define TARGET_NR_getresuid32	209
#define TARGET_NR_setresgid32	210
#define TARGET_NR_getresgid32	211
#define TARGET_NR_chown32		212
#define TARGET_NR_setuid32		213
#define TARGET_NR_setgid32		214
#define TARGET_NR_setfsuid32		215
#define TARGET_NR_setfsgid32		216
#define TARGET_NR_pivot_root		217
#define TARGET_NR_mincore		218
#define TARGET_NR_madvise		219
#define TARGET_NR_madvise1		219	/* delete when C lib stub is removed */
#define TARGET_NR_getdents64		220
#define TARGET_NR_fcntl64		221
#define TARGET_NR_security		223	/* syscall for security modules */
#define TARGET_NR_gettid		224
#define TARGET_NR_readahead		225
#define TARGET_NR_setxattr		226
#define TARGET_NR_lsetxattr		227
#define TARGET_NR_fsetxattr		228
#define TARGET_NR_getxattr		229
#define TARGET_NR_lgetxattr		230
#define TARGET_NR_fgetxattr		231
#define TARGET_NR_listxattr		232
#define TARGET_NR_llistxattr		233
#define TARGET_NR_flistxattr		234
#define TARGET_NR_removexattr	235
#define TARGET_NR_lremovexattr	236
#define TARGET_NR_fremovexattr	237
#define TARGET_NR_tkill		238
#define TARGET_NR_sendfile64		239
#define TARGET_NR_futex		240
#define TARGET_NR_sched_setaffinity	241
#define TARGET_NR_sched_getaffinity	242
#define TARGET_NR_set_thread_area	243
#define TARGET_NR_get_thread_area	244
#define TARGET_NR_io_setup		245
#define TARGET_NR_io_destroy		246
#define TARGET_NR_io_getevents	247
#define TARGET_NR_io_submit		248
#define TARGET_NR_io_cancel		249
#define TARGET_NR_fadvise64		250

#define TARGET_NR_exit_group		252
#define TARGET_NR_lookup_dcookie	253
#define TARGET_NR_epoll_create	254
#define TARGET_NR_epoll_ctl		255
#define TARGET_NR_epoll_wait		256
#define TARGET_NR_remap_file_pages	257
#define TARGET_NR_set_tid_address	258
#define TARGET_NR_timer_create	259
#define TARGET_NR_timer_settime	(TARGET_NR_timer_create+1)
#define TARGET_NR_timer_gettime	(TARGET_NR_timer_create+2)
#define TARGET_NR_timer_getoverrun	(TARGET_NR_timer_create+3)
#define TARGET_NR_timer_delete	(TARGET_NR_timer_create+4)
#define TARGET_NR_clock_settime	(TARGET_NR_timer_create+5)
#define TARGET_NR_clock_gettime	(TARGET_NR_timer_create+6)
#define TARGET_NR_clock_getres	(TARGET_NR_timer_create+7)
#define TARGET_NR_clock_nanosleep	(TARGET_NR_timer_create+8)

#define TARGET_SIG_BLOCK          0    /* for blocking signals */
#define TARGET_SIG_UNBLOCK        1    /* for unblocking signals */
#define TARGET_SIG_SETMASK        2    /* for setting the signal mask */

struct target_stat {
	unsigned short st_dev;
	unsigned short __pad1;
	target_ulong st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned short __pad2;
	target_ulong  st_size;
	target_ulong  st_blksize;
	target_ulong  st_blocks;
	target_ulong  target_st_atime;
	target_ulong  __unused1;
	target_ulong  target_st_mtime;
	target_ulong  __unused2;
	target_ulong  target_st_ctime;
	target_ulong  __unused3;
	target_ulong  __unused4;
	target_ulong  __unused5;
};

/* This matches struct stat64 in glibc2.1, hence the absolutely
 * insane amounts of padding around dev_t's.
 */
struct target_stat64 {
	unsigned short	st_dev;
	unsigned char	__pad0[10];

#define TARGET_STAT64_HAS_BROKEN_ST_INO	1
	target_ulong	__st_ino;

	unsigned int	st_mode;
	unsigned int	st_nlink;

	target_ulong	st_uid;
	target_ulong	st_gid;

	unsigned short	st_rdev;
	unsigned char	__pad3[10];

	long long	st_size;
	target_ulong	st_blksize;

	target_ulong	st_blocks;	/* Number 512-byte blocks allocated. */
	target_ulong	__pad4;		/* future possible st_blocks high bits */

	target_ulong	target_st_atime;
	target_ulong	__pad5;

	target_ulong	target_st_mtime;
	target_ulong	__pad6;

	target_ulong	target_st_ctime;
	target_ulong	__pad7;		/* will be high 32 bits of ctime someday */

	unsigned long long	st_ino;
};

#define TARGET_SA_NOCLDSTOP	0x00000001
#define TARGET_SA_NOCLDWAIT	0x00000002 /* not supported yet */
#define TARGET_SA_SIGINFO	0x00000004
#define TARGET_SA_ONSTACK	0x08000000
#define TARGET_SA_RESTART	0x10000000
#define TARGET_SA_NODEFER	0x40000000
#define TARGET_SA_RESETHAND	0x80000000
#define TARGET_SA_RESTORER	0x04000000

#define TARGET_SIGHUP		 1
#define TARGET_SIGINT		 2
#define TARGET_SIGQUIT		 3
#define TARGET_SIGILL		 4
#define TARGET_SIGTRAP		 5
#define TARGET_SIGABRT		 6
#define TARGET_SIGIOT		 6
#define TARGET_SIGBUS		 7
#define TARGET_SIGFPE		 8
#define TARGET_SIGKILL		 9
#define TARGET_SIGUSR1		10
#define TARGET_SIGSEGV		11
#define TARGET_SIGUSR2		12
#define TARGET_SIGPIPE		13
#define TARGET_SIGALRM		14
#define TARGET_SIGTERM		15
#define TARGET_SIGSTKFLT	16
#define TARGET_SIGCHLD		17
#define TARGET_SIGCONT		18
#define TARGET_SIGSTOP		19
#define TARGET_SIGTSTP		20
#define TARGET_SIGTTIN		21
#define TARGET_SIGTTOU		22
#define TARGET_SIGURG		23
#define TARGET_SIGXCPU		24
#define TARGET_SIGXFSZ		25
#define TARGET_SIGVTALRM	26
#define TARGET_SIGPROF		27
#define TARGET_SIGWINCH	        28
#define TARGET_SIGIO		29
#define TARGET_SIGRTMIN         32

struct target_old_sigaction {
        target_ulong _sa_handler;
        target_ulong sa_mask;
        target_ulong sa_flags;
        target_ulong sa_restorer;
};

struct target_sigaction {
        target_ulong _sa_handler;
        target_ulong sa_flags;
        target_ulong sa_restorer;
        target_sigset_t sa_mask;
};

typedef union target_sigval {
	int sival_int;
        target_ulong sival_ptr;
} target_sigval_t;

#define TARGET_SI_MAX_SIZE	128
#define TARGET_SI_PAD_SIZE	((TARGET_SI_MAX_SIZE/sizeof(int)) - 3)

typedef struct target_siginfo {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[TARGET_SI_PAD_SIZE];

		/* kill() */
		struct {
			pid_t _pid;		/* sender's pid */
			uid_t _uid;		/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			unsigned int _timer1;
			unsigned int _timer2;
		} _timer;

		/* POSIX.1b signals */
		struct {
			pid_t _pid;		/* sender's pid */
			uid_t _uid;		/* sender's uid */
			target_sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			pid_t _pid;		/* which child */
			uid_t _uid;		/* sender's uid */
			int _status;		/* exit code */
			target_clock_t _utime;
                        target_clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			target_ulong _addr; /* faulting insn/memory ref. */
		} _sigfault;

		/* SIGPOLL */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;
	} _sifields;
} target_siginfo_t;

/*
 * SIGILL si_codes
 */
#define TARGET_ILL_ILLOPN	(2)	/* illegal operand */

/*
 * SIGFPE si_codes
 */
#define TARGET_FPE_INTDIV      (1)  /* integer divide by zero */
#define TARGET_FPE_INTOVF      (2)  /* integer overflow */
#define TARGET_FPE_FLTDIV      (3)  /* floating point divide by zero */
#define TARGET_FPE_FLTOVF      (4)  /* floating point overflow */
#define TARGET_FPE_FLTUND      (5)  /* floating point underflow */
#define TARGET_FPE_FLTRES      (6)  /* floating point inexact result */
#define TARGET_FPE_FLTINV      (7)  /* floating point invalid operation */
#define TARGET_FPE_FLTSUB      (8)  /* subscript out of range */
#define TARGET_NSIGFPE         8

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

/*
 * The following is for compatibility across the various Linux
 * platforms.  The i386 ioctl numbering scheme doesn't really enforce
 * a type field.  De facto, however, the top 8 bits of the lower 16
 * bits are indeed used as a type field, so we might just as well make
 * this explicit here.  Please be sure to use the decoding macros
 * below from now on.
 */
#define TARGET_IOC_NRBITS	8
#define TARGET_IOC_TYPEBITS	8
#define TARGET_IOC_SIZEBITS	14
#define TARGET_IOC_DIRBITS	2

#define TARGET_IOC_NRMASK	((1 << TARGET_IOC_NRBITS)-1)
#define TARGET_IOC_TYPEMASK	((1 << TARGET_IOC_TYPEBITS)-1)
#define TARGET_IOC_SIZEMASK	((1 << TARGET_IOC_SIZEBITS)-1)
#define TARGET_IOC_DIRMASK	((1 << TARGET_IOC_DIRBITS)-1)

#define TARGET_IOC_NRSHIFT	0
#define TARGET_IOC_TYPESHIFT	(TARGET_IOC_NRSHIFT+TARGET_IOC_NRBITS)
#define TARGET_IOC_SIZESHIFT	(TARGET_IOC_TYPESHIFT+TARGET_IOC_TYPEBITS)
#define TARGET_IOC_DIRSHIFT	(TARGET_IOC_SIZESHIFT+TARGET_IOC_SIZEBITS)

/*
 * Direction bits.
 */
#define TARGET_IOC_NONE	0U
#define TARGET_IOC_WRITE	1U
#define TARGET_IOC_READ	2U

#define TARGET_IOC(dir,type,nr,size) \
	(((dir)  << TARGET_IOC_DIRSHIFT) | \
	 ((type) << TARGET_IOC_TYPESHIFT) | \
	 ((nr)   << TARGET_IOC_NRSHIFT) | \
	 ((size) << TARGET_IOC_SIZESHIFT))

/* used to create numbers */
#define TARGET_IO(type,nr)		TARGET_IOC(TARGET_IOC_NONE,(type),(nr),0)
#define TARGET_IOR(type,nr,size)	TARGET_IOC(TARGET_IOC_READ,(type),(nr),sizeof(size))
#define TARGET_IOW(type,nr,size)	TARGET_IOC(TARGET_IOC_WRITE,(type),(nr),sizeof(size))
#define TARGET_IOWR(type,nr,size)	TARGET_IOC(TARGET_IOC_READ|TARGET_IOC_WRITE,(type),(nr),sizeof(size))

/* 0x54 is just a magic number to make these relatively unique ('T') */

#define TARGET_TCGETS		0x5401
#define TARGET_TCSETS		0x5402
#define TARGET_TCSETSW		0x5403
#define TARGET_TCSETSF		0x5404
#define TARGET_TCGETA		0x5405
#define TARGET_TCSETA		0x5406
#define TARGET_TCSETAW		0x5407
#define TARGET_TCSETAF		0x5408
#define TARGET_TCSBRK		0x5409
#define TARGET_TCXONC		0x540A
#define TARGET_TCFLSH		0x540B
#define TARGET_TIOCEXCL	0x540C
#define TARGET_TIOCNXCL	0x540D
#define TARGET_TIOCSCTTY	0x540E
#define TARGET_TIOCGPGRP	0x540F
#define TARGET_TIOCSPGRP	0x5410
#define TARGET_TIOCOUTQ	0x5411
#define TARGET_TIOCSTI		0x5412
#define TARGET_TIOCGWINSZ	0x5413
#define TARGET_TIOCSWINSZ	0x5414
#define TARGET_TIOCMGET	0x5415
#define TARGET_TIOCMBIS	0x5416
#define TARGET_TIOCMBIC	0x5417
#define TARGET_TIOCMSET	0x5418
#define TARGET_TIOCGSOFTCAR	0x5419
#define TARGET_TIOCSSOFTCAR	0x541A
#define TARGET_FIONREAD	0x541B
#define TARGET_TIOCINQ		FIONREAD
#define TARGET_TIOCLINUX	0x541C
#define TARGET_TIOCCONS	0x541D
#define TARGET_TIOCGSERIAL	0x541E
#define TARGET_TIOCSSERIAL	0x541F
#define TARGET_TIOCPKT		0x5420
#define TARGET_FIONBIO		0x5421
#define TARGET_TIOCNOTTY	0x5422
#define TARGET_TIOCSETD	0x5423
#define TARGET_TIOCGETD	0x5424
#define TARGET_TCSBRKP		0x5425	/* Needed for POSIX tcsendbreak() */
#define TARGET_TIOCTTYGSTRUCT	0x5426  /* For debugging only */
#define TARGET_TIOCSBRK	0x5427  /* BSD compatibility */
#define TARGET_TIOCCBRK	0x5428  /* BSD compatibility */
#define TARGET_TIOCGSID	0x5429  /* Return the session ID of FD */
#define TARGET_TIOCGPTN	TARGET_IOR('T',0x30, unsigned int) /* Get Pty Number (of pty-mux device) */
#define TARGET_TIOCSPTLCK	TARGET_IOW('T',0x31, int)  /* Lock/unlock Pty */

#define TARGET_FIONCLEX	0x5450  /* these numbers need to be adjusted. */
#define TARGET_FIOCLEX		0x5451
#define TARGET_FIOASYNC	0x5452
#define TARGET_TIOCSERCONFIG	0x5453
#define TARGET_TIOCSERGWILD	0x5454
#define TARGET_TIOCSERSWILD	0x5455
#define TARGET_TIOCGLCKTRMIOS	0x5456
#define TARGET_TIOCSLCKTRMIOS	0x5457
#define TARGET_TIOCSERGSTRUCT	0x5458 /* For debugging only */
#define TARGET_TIOCSERGETLSR   0x5459 /* Get line status register */
#define TARGET_TIOCSERGETMULTI 0x545A /* Get multiport config  */
#define TARGET_TIOCSERSETMULTI 0x545B /* Set multiport config */

#define TARGET_TIOCMIWAIT	0x545C	/* wait for a change on serial input line(s) */
#define TARGET_TIOCGICOUNT	0x545D	/* read serial port inline interrupt counts */
#define TARGET_TIOCGHAYESESP   0x545E  /* Get Hayes ESP configuration */
#define TARGET_TIOCSHAYESESP   0x545F  /* Set Hayes ESP configuration */

/* Used for packet mode */
#define TARGET_TIOCPKT_DATA		 0
#define TARGET_TIOCPKT_FLUSHREAD	 1
#define TARGET_TIOCPKT_FLUSHWRITE	 2
#define TARGET_TIOCPKT_STOP		 4
#define TARGET_TIOCPKT_START		 8
#define TARGET_TIOCPKT_NOSTOP		16
#define TARGET_TIOCPKT_DOSTOP		32

#define TARGET_TIOCSER_TEMT    0x01	/* Transmitter physically empty */

/* from asm/termbits.h */

#define TARGET_NCCS 19

struct target_termios {
    unsigned int c_iflag;               /* input mode flags */
    unsigned int c_oflag;               /* output mode flags */
    unsigned int c_cflag;               /* control mode flags */
    unsigned int c_lflag;               /* local mode flags */
    unsigned char c_line;                    /* line discipline */
    unsigned char c_cc[TARGET_NCCS];                /* control characters */
};

/* c_iflag bits */
#define TARGET_IGNBRK  0000001
#define TARGET_BRKINT  0000002
#define TARGET_IGNPAR  0000004
#define TARGET_PARMRK  0000010
#define TARGET_INPCK   0000020
#define TARGET_ISTRIP  0000040
#define TARGET_INLCR   0000100
#define TARGET_IGNCR   0000200
#define TARGET_ICRNL   0000400
#define TARGET_IUCLC   0001000
#define TARGET_IXON    0002000
#define TARGET_IXANY   0004000
#define TARGET_IXOFF   0010000
#define TARGET_IMAXBEL 0020000

/* c_oflag bits */
#define TARGET_OPOST   0000001
#define TARGET_OLCUC   0000002
#define TARGET_ONLCR   0000004
#define TARGET_OCRNL   0000010
#define TARGET_ONOCR   0000020
#define TARGET_ONLRET  0000040
#define TARGET_OFILL   0000100
#define TARGET_OFDEL   0000200
#define TARGET_NLDLY   0000400
#define   TARGET_NL0   0000000
#define   TARGET_NL1   0000400
#define TARGET_CRDLY   0003000
#define   TARGET_CR0   0000000
#define   TARGET_CR1   0001000
#define   TARGET_CR2   0002000
#define   TARGET_CR3   0003000
#define TARGET_TABDLY  0014000
#define   TARGET_TAB0  0000000
#define   TARGET_TAB1  0004000
#define   TARGET_TAB2  0010000
#define   TARGET_TAB3  0014000
#define   TARGET_XTABS 0014000
#define TARGET_BSDLY   0020000
#define   TARGET_BS0   0000000
#define   TARGET_BS1   0020000
#define TARGET_VTDLY   0040000
#define   TARGET_VT0   0000000
#define   TARGET_VT1   0040000
#define TARGET_FFDLY   0100000
#define   TARGET_FF0   0000000
#define   TARGET_FF1   0100000

/* c_cflag bit meaning */
#define TARGET_CBAUD   0010017
#define  TARGET_B0     0000000         /* hang up */
#define  TARGET_B50    0000001
#define  TARGET_B75    0000002
#define  TARGET_B110   0000003
#define  TARGET_B134   0000004
#define  TARGET_B150   0000005
#define  TARGET_B200   0000006
#define  TARGET_B300   0000007
#define  TARGET_B600   0000010
#define  TARGET_B1200  0000011
#define  TARGET_B1800  0000012
#define  TARGET_B2400  0000013
#define  TARGET_B4800  0000014
#define  TARGET_B9600  0000015
#define  TARGET_B19200 0000016
#define  TARGET_B38400 0000017
#define TARGET_EXTA B19200
#define TARGET_EXTB B38400
#define TARGET_CSIZE   0000060
#define   TARGET_CS5   0000000
#define   TARGET_CS6   0000020
#define   TARGET_CS7   0000040
#define   TARGET_CS8   0000060
#define TARGET_CSTOPB  0000100
#define TARGET_CREAD   0000200
#define TARGET_PARENB  0000400
#define TARGET_PARODD  0001000
#define TARGET_HUPCL   0002000
#define TARGET_CLOCAL  0004000
#define TARGET_CBAUDEX 0010000
#define  TARGET_B57600  0010001
#define  TARGET_B115200 0010002
#define  TARGET_B230400 0010003
#define  TARGET_B460800 0010004
#define TARGET_CIBAUD    002003600000  /* input baud rate (not used) */
#define TARGET_CRTSCTS   020000000000          /* flow control */

/* c_lflag bits */
#define TARGET_ISIG    0000001
#define TARGET_ICANON  0000002
#define TARGET_XCASE   0000004
#define TARGET_ECHO    0000010
#define TARGET_ECHOE   0000020
#define TARGET_ECHOK   0000040
#define TARGET_ECHONL  0000100
#define TARGET_NOFLSH  0000200
#define TARGET_TOSTOP  0000400
#define TARGET_ECHOCTL 0001000
#define TARGET_ECHOPRT 0002000
#define TARGET_ECHOKE  0004000
#define TARGET_FLUSHO  0010000
#define TARGET_PENDIN  0040000
#define TARGET_IEXTEN  0100000

/* c_cc character offsets */
#define TARGET_VINTR	0
#define TARGET_VQUIT	1
#define TARGET_VERASE	2
#define TARGET_VKILL	3
#define TARGET_VEOF	4
#define TARGET_VTIME	5
#define TARGET_VMIN	6
#define TARGET_VSWTC	7
#define TARGET_VSTART	8
#define TARGET_VSTOP	9
#define TARGET_VSUSP	10
#define TARGET_VEOL	11
#define TARGET_VREPRINT	12
#define TARGET_VDISCARD	13
#define TARGET_VWERASE	14
#define TARGET_VLNEXT	15
#define TARGET_VEOL2	16

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

struct alpha_msgbuf {
	long	mtype;
	char	mtext[4096];
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

struct target_flock {
	short l_type;
	short l_whence;
	target_ulong l_start;
	target_ulong l_len;
	int l_pid;
};

struct target_flock64 {
	short  l_type;
	short  l_whence;
	unsigned long long l_start;
	unsigned long long l_len;
	int  l_pid;
};

/* soundcard defines (XXX: move them to generic file syscall_defs.h) */

#define TARGET_SNDCTL_COPR_HALT           0xc0144307
#define TARGET_SNDCTL_COPR_LOAD           0xcfb04301
#define TARGET_SNDCTL_COPR_RCODE          0xc0144303
#define TARGET_SNDCTL_COPR_RCVMSG         0x8fa44309
#define TARGET_SNDCTL_COPR_RDATA          0xc0144302
#define TARGET_SNDCTL_COPR_RESET          0x00004300
#define TARGET_SNDCTL_COPR_RUN            0xc0144306
#define TARGET_SNDCTL_COPR_SENDMSG        0xcfa44308
#define TARGET_SNDCTL_COPR_WCODE          0x40144305
#define TARGET_SNDCTL_COPR_WDATA          0x40144304
#define TARGET_SNDCTL_DSP_CHANNELS        0xc0045006
#define TARGET_SNDCTL_DSP_GETBLKSIZE      0xc0045004
#define TARGET_SNDCTL_DSP_GETCAPS         0x8004500f
#define TARGET_SNDCTL_DSP_GETFMTS         0x8004500b
#define TARGET_SNDCTL_DSP_GETIPTR         0x800c5011
#define TARGET_SNDCTL_DSP_GETISPACE       0x8010500d
#define TARGET_SNDCTL_DSP_GETOPTR         0x800c5012
#define TARGET_SNDCTL_DSP_GETOSPACE       0x8010500c
#define TARGET_SNDCTL_DSP_GETTRIGGER      0x80045010
#define TARGET_SNDCTL_DSP_MAPINBUF        0x80085013
#define TARGET_SNDCTL_DSP_MAPOUTBUF       0x80085014
#define TARGET_SNDCTL_DSP_NONBLOCK        0x0000500e
#define TARGET_SNDCTL_DSP_POST            0x00005008
#define TARGET_SNDCTL_DSP_RESET           0x00005000
#define TARGET_SNDCTL_DSP_SAMPLESIZE      0xc0045005
#define TARGET_SNDCTL_DSP_SETDUPLEX       0x00005016
#define TARGET_SNDCTL_DSP_SETFMT          0xc0045005
#define TARGET_SNDCTL_DSP_SETFRAGMENT     0xc004500a
#define TARGET_SNDCTL_DSP_SETSYNCRO       0x00005015
#define TARGET_SNDCTL_DSP_SETTRIGGER      0x40045010
#define TARGET_SNDCTL_DSP_SPEED           0xc0045002
#define TARGET_SNDCTL_DSP_STEREO          0xc0045003
#define TARGET_SNDCTL_DSP_SUBDIVIDE       0xc0045009
#define TARGET_SNDCTL_DSP_SYNC            0x00005001
#define TARGET_SNDCTL_FM_4OP_ENABLE       0x4004510f
#define TARGET_SNDCTL_FM_LOAD_INSTR       0x40285107
#define TARGET_SNDCTL_MIDI_INFO           0xc074510c
#define TARGET_SNDCTL_MIDI_MPUCMD         0xc0216d02
#define TARGET_SNDCTL_MIDI_MPUMODE        0xc0046d01
#define TARGET_SNDCTL_MIDI_PRETIME        0xc0046d00
#define TARGET_SNDCTL_PMGR_ACCESS         0xcfb85110
#define TARGET_SNDCTL_PMGR_IFACE          0xcfb85001
#define TARGET_SNDCTL_SEQ_CTRLRATE        0xc0045103
#define TARGET_SNDCTL_SEQ_GETINCOUNT      0x80045105
#define TARGET_SNDCTL_SEQ_GETOUTCOUNT     0x80045104
#define TARGET_SNDCTL_SEQ_NRMIDIS         0x8004510b
#define TARGET_SNDCTL_SEQ_NRSYNTHS        0x8004510a
#define TARGET_SNDCTL_SEQ_OUTOFBAND       0x40085112
#define TARGET_SNDCTL_SEQ_PANIC           0x00005111
#define TARGET_SNDCTL_SEQ_PERCMODE        0x40045106
#define TARGET_SNDCTL_SEQ_RESET           0x00005100
#define TARGET_SNDCTL_SEQ_RESETSAMPLES    0x40045109
#define TARGET_SNDCTL_SEQ_SYNC            0x00005101
#define TARGET_SNDCTL_SEQ_TESTMIDI        0x40045108
#define TARGET_SNDCTL_SEQ_THRESHOLD       0x4004510d
#define TARGET_SNDCTL_SEQ_TRESHOLD        0x4004510d
#define TARGET_SNDCTL_SYNTH_INFO          0xc08c5102
#define TARGET_SNDCTL_SYNTH_MEMAVL        0xc004510e
#define TARGET_SNDCTL_TMR_CONTINUE        0x00005404
#define TARGET_SNDCTL_TMR_METRONOME       0x40045407
#define TARGET_SNDCTL_TMR_SELECT          0x40045408
#define TARGET_SNDCTL_TMR_SOURCE          0xc0045406
#define TARGET_SNDCTL_TMR_START           0x00005402
#define TARGET_SNDCTL_TMR_STOP            0x00005403
#define TARGET_SNDCTL_TMR_TEMPO           0xc0045405
#define TARGET_SNDCTL_TMR_TIMEBASE        0xc0045401
#define TARGET_SOUND_PCM_WRITE_FILTER     0xc0045007
#define TARGET_SOUND_PCM_READ_RATE        0x80045002
#define TARGET_SOUND_PCM_READ_CHANNELS    0x80045006
#define TARGET_SOUND_PCM_READ_BITS        0x80045005
#define TARGET_SOUND_PCM_READ_FILTER      0x80045007
#define TARGET_SOUND_MIXER_INFO           0x80304d65
#define TARGET_SOUND_MIXER_ACCESS         0xc0804d66
#define TARGET_SOUND_MIXER_PRIVATE1       0xc0044d6f
#define TARGET_SOUND_MIXER_PRIVATE2       0xc0044d70
#define TARGET_SOUND_MIXER_PRIVATE3       0xc0044d71
#define TARGET_SOUND_MIXER_PRIVATE4       0xc0044d72
#define TARGET_SOUND_MIXER_PRIVATE5       0xc0044d73
#define TARGET_SOUND_MIXER_READ_VOLUME    0x80044d00
#define TARGET_SOUND_MIXER_READ_BASS      0x80044d01
#define TARGET_SOUND_MIXER_READ_TREBLE    0x80044d02
#define TARGET_SOUND_MIXER_READ_SYNTH     0x80044d03
#define TARGET_SOUND_MIXER_READ_PCM       0x80044d04
#define TARGET_SOUND_MIXER_READ_SPEAKER   0x80044d05
#define TARGET_SOUND_MIXER_READ_LINE      0x80044d06
#define TARGET_SOUND_MIXER_READ_MIC       0x80044d07
#define TARGET_SOUND_MIXER_READ_CD        0x80044d08
#define TARGET_SOUND_MIXER_READ_IMIX      0x80044d09
#define TARGET_SOUND_MIXER_READ_ALTPCM    0x80044d0a
#define TARGET_SOUND_MIXER_READ_RECLEV    0x80044d0b
#define TARGET_SOUND_MIXER_READ_IGAIN     0x80044d0c
#define TARGET_SOUND_MIXER_READ_OGAIN     0x80044d0d
#define TARGET_SOUND_MIXER_READ_LINE1     0x80044d0e
#define TARGET_SOUND_MIXER_READ_LINE2     0x80044d0f
#define TARGET_SOUND_MIXER_READ_LINE3     0x80044d10
#define TARGET_SOUND_MIXER_READ_MUTE      0x80044d1f
#define TARGET_SOUND_MIXER_READ_ENHANCE   0x80044d1f
#define TARGET_SOUND_MIXER_READ_LOUD      0x80044d1f
#define TARGET_SOUND_MIXER_READ_RECSRC    0x80044dff
#define TARGET_SOUND_MIXER_READ_DEVMASK   0x80044dfe
#define TARGET_SOUND_MIXER_READ_RECMASK   0x80044dfd
#define TARGET_SOUND_MIXER_READ_STEREODEVS  0x80044dfb
#define TARGET_SOUND_MIXER_READ_CAPS      0x80044dfc
#define TARGET_SOUND_MIXER_WRITE_VOLUME   0xc0044d00
#define TARGET_SOUND_MIXER_WRITE_BASS     0xc0044d01
#define TARGET_SOUND_MIXER_WRITE_TREBLE   0xc0044d02
#define TARGET_SOUND_MIXER_WRITE_SYNTH    0xc0044d03
#define TARGET_SOUND_MIXER_WRITE_PCM      0xc0044d04
#define TARGET_SOUND_MIXER_WRITE_SPEAKER  0xc0044d05
#define TARGET_SOUND_MIXER_WRITE_LINE     0xc0044d06
#define TARGET_SOUND_MIXER_WRITE_MIC      0xc0044d07
#define TARGET_SOUND_MIXER_WRITE_CD       0xc0044d08
#define TARGET_SOUND_MIXER_WRITE_IMIX     0xc0044d09
#define TARGET_SOUND_MIXER_WRITE_ALTPCM   0xc0044d0a
#define TARGET_SOUND_MIXER_WRITE_RECLEV   0xc0044d0b
#define TARGET_SOUND_MIXER_WRITE_IGAIN    0xc0044d0c
#define TARGET_SOUND_MIXER_WRITE_OGAIN    0xc0044d0d
#define TARGET_SOUND_MIXER_WRITE_LINE1    0xc0044d0e
#define TARGET_SOUND_MIXER_WRITE_LINE2    0xc0044d0f
#define TARGET_SOUND_MIXER_WRITE_LINE3    0xc0044d10
#define TARGET_SOUND_MIXER_WRITE_MUTE     0xc0044d1f
#define TARGET_SOUND_MIXER_WRITE_ENHANCE  0xc0044d1f
#define TARGET_SOUND_MIXER_WRITE_LOUD     0xc0044d1f
#define TARGET_SOUND_MIXER_WRITE_RECSRC   0xc0044dff

#define TARGET_VFAT_IOCTL_READDIR_BOTH    0x82187201
#define TARGET_VFAT_IOCTL_READDIR_SHORT   0x82187202

#define TARGET_SIOCATMARK	0x8905
