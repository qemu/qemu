/* common syscall defines for all architectures */

/* Note: although the syscall numbers change between architectures,
   most of them stay the same, so we handle it by putting ifdefs if
   necessary */

#ifndef SYSCALL_DEFS_H
#define SYSCALL_DEFS_H

#include "syscall_nr.h"


/* socket operations for socketcall() */
#define TARGET_SYS_SOCKET       1         /* socket()              */
#define TARGET_SYS_BIND         2         /* bind()                */
#define TARGET_SYS_CONNECT      3         /* connect()             */
#define TARGET_SYS_LISTEN       4         /* listen()              */
#define TARGET_SYS_ACCEPT       5         /* accept()              */
#define TARGET_SYS_GETSOCKNAME  6         /* getsockname()         */
#define TARGET_SYS_GETPEERNAME  7         /* getpeername()         */
#define TARGET_SYS_SOCKETPAIR   8         /* socketpair()          */
#define TARGET_SYS_SEND         9         /* send()                */
#define TARGET_SYS_RECV         10        /* recv()                */
#define TARGET_SYS_SENDTO       11        /* sendto()              */
#define TARGET_SYS_RECVFROM     12        /* recvfrom()            */
#define TARGET_SYS_SHUTDOWN     13        /* shutdown()            */
#define TARGET_SYS_SETSOCKOPT   14        /* setsockopt()          */
#define TARGET_SYS_GETSOCKOPT   15        /* getsockopt()          */
#define TARGET_SYS_SENDMSG      16        /* sendmsg()             */
#define TARGET_SYS_RECVMSG      17        /* recvmsg()             */
#define TARGET_SYS_ACCEPT4      18        /* accept4()             */
#define TARGET_SYS_RECVMMSG     19        /* recvmmsg()            */
#define TARGET_SYS_SENDMMSG     20        /* sendmmsg()            */

#define IPCOP_CALL(VERSION, OP) ((VERSION) << 16 | (OP))
#define IPCOP_semop		1
#define IPCOP_semget		2
#define IPCOP_semctl		3
#define IPCOP_semtimedop	4
#define IPCOP_msgsnd		11
#define IPCOP_msgrcv		12
#define IPCOP_msgget		13
#define IPCOP_msgctl		14
#define IPCOP_shmat		21
#define IPCOP_shmdt		22
#define IPCOP_shmget		23
#define IPCOP_shmctl		24

#define TARGET_SEMOPM     500

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

#if (defined(TARGET_I386) && defined(TARGET_ABI32)) \
    || (defined(TARGET_ARM) && defined(TARGET_ABI32)) \
    || defined(TARGET_SPARC) \
    || defined(TARGET_M68K) || defined(TARGET_SH4) || defined(TARGET_CRIS)
    /* 16 bit uid wrappers emulation */
#define USE_UID16
#define target_id uint16_t
#else
#define target_id uint32_t
#endif

#if defined(TARGET_I386) || defined(TARGET_ARM) || defined(TARGET_SH4) \
    || defined(TARGET_M68K) || defined(TARGET_CRIS) \
    || defined(TARGET_S390X) || defined(TARGET_OPENRISC) \
    || defined(TARGET_NIOS2) || defined(TARGET_RISCV) \
    || defined(TARGET_XTENSA)

#define TARGET_IOC_SIZEBITS	14
#define TARGET_IOC_DIRBITS	2

#define TARGET_IOC_NONE	  0U
#define TARGET_IOC_WRITE  1U
#define TARGET_IOC_READ	  2U

#elif defined(TARGET_PPC) || defined(TARGET_ALPHA) || \
      defined(TARGET_SPARC) || defined(TARGET_MICROBLAZE) || \
      defined(TARGET_MIPS)

#define TARGET_IOC_SIZEBITS	13
#define TARGET_IOC_DIRBITS	3

#define TARGET_IOC_NONE	  1U
#define TARGET_IOC_READ	  2U
#define TARGET_IOC_WRITE  4U

#elif defined(TARGET_HPPA)

#define TARGET_IOC_SIZEBITS  14
#define TARGET_IOC_DIRBITS    2

#define TARGET_IOC_NONE   0U
#define TARGET_IOC_WRITE  2U
#define TARGET_IOC_READ   1U

#elif defined(TARGET_HEXAGON)

#define TARGET_IOC_SIZEBITS     14

#define TARGET_IOC_NONE   0U
#define TARGET_IOC_WRITE  1U
#define TARGET_IOC_READ          2U

#else
#error unsupported CPU
#endif

#define TARGET_IOC_NRMASK	((1 << TARGET_IOC_NRBITS)-1)
#define TARGET_IOC_TYPEMASK	((1 << TARGET_IOC_TYPEBITS)-1)
#define TARGET_IOC_SIZEMASK	((1 << TARGET_IOC_SIZEBITS)-1)
#define TARGET_IOC_DIRMASK	((1 << TARGET_IOC_DIRBITS)-1)

#define TARGET_IOC_NRSHIFT	0
#define TARGET_IOC_TYPESHIFT	(TARGET_IOC_NRSHIFT+TARGET_IOC_NRBITS)
#define TARGET_IOC_SIZESHIFT	(TARGET_IOC_TYPESHIFT+TARGET_IOC_TYPEBITS)
#define TARGET_IOC_DIRSHIFT	(TARGET_IOC_SIZESHIFT+TARGET_IOC_SIZEBITS)

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

/* the size is automatically computed for these defines */
#define TARGET_IORU(type,nr)	TARGET_IOC(TARGET_IOC_READ,(type),(nr),TARGET_IOC_SIZEMASK)
#define TARGET_IOWU(type,nr)	TARGET_IOC(TARGET_IOC_WRITE,(type),(nr),TARGET_IOC_SIZEMASK)
#define TARGET_IOWRU(type,nr)	TARGET_IOC(TARGET_IOC_READ|TARGET_IOC_WRITE,(type),(nr),TARGET_IOC_SIZEMASK)

struct target_sockaddr {
    abi_ushort sa_family;
    uint8_t sa_data[14];
};

struct target_sockaddr_ll {
    abi_ushort sll_family;   /* Always AF_PACKET */
    abi_ushort sll_protocol; /* Physical layer protocol */
    abi_int    sll_ifindex;  /* Interface number */
    abi_ushort sll_hatype;   /* ARP hardware type */
    uint8_t    sll_pkttype;  /* Packet type */
    uint8_t    sll_halen;    /* Length of address */
    uint8_t    sll_addr[8];  /* Physical layer address */
};

struct target_sockaddr_un {
    abi_ushort su_family;
    uint8_t sun_path[108];
};

struct target_sockaddr_nl {
    abi_ushort nl_family;   /* AF_NETLINK */
    abi_ushort __pad;
    abi_uint nl_pid;
    abi_uint nl_groups;
};

struct target_in_addr {
    abi_uint s_addr; /* big endian */
};

struct target_sockaddr_in {
  abi_ushort sin_family;
  abi_short sin_port; /* big endian */
  struct target_in_addr sin_addr;
  uint8_t __pad[sizeof(struct target_sockaddr) -
                sizeof(abi_ushort) - sizeof(abi_short) -
                sizeof(struct target_in_addr)];
};

struct target_sockaddr_in6 {
    abi_ushort sin6_family;
    abi_ushort sin6_port; /* big endian */
    abi_uint sin6_flowinfo; /* big endian */
    struct in6_addr sin6_addr; /* IPv6 address, big endian */
    abi_uint sin6_scope_id;
};

struct target_sock_filter {
    abi_ushort code;
    uint8_t jt;
    uint8_t jf;
    abi_uint k;
};

struct target_sock_fprog {
    abi_ushort len;
    abi_ulong filter;
};

struct target_ip_mreq {
    struct target_in_addr imr_multiaddr;
    struct target_in_addr imr_address;
};

struct target_ip_mreqn {
    struct target_in_addr imr_multiaddr;
    struct target_in_addr imr_address;
    abi_long imr_ifindex;
};

struct target_ip_mreq_source {
    /* big endian */
    uint32_t imr_multiaddr;
    uint32_t imr_interface;
    uint32_t imr_sourceaddr;
};

struct target_linger {
    abi_int l_onoff;        /* Linger active                */
    abi_int l_linger;       /* How long to linger for       */
};

#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
struct target_timeval {
    abi_long tv_sec;
    abi_int tv_usec;
};
#define target__kernel_sock_timeval target_timeval
#else
struct target_timeval {
    abi_long tv_sec;
    abi_long tv_usec;
};

struct target__kernel_sock_timeval {
    abi_llong tv_sec;
    abi_llong tv_usec;
};
#endif

struct target_timespec {
    abi_long tv_sec;
    abi_long tv_nsec;
};

struct target__kernel_timespec {
    abi_llong tv_sec;
    abi_llong tv_nsec;
};

struct target_timezone {
    abi_int tz_minuteswest;
    abi_int tz_dsttime;
};

struct target_itimerval {
    struct target_timeval it_interval;
    struct target_timeval it_value;
};

struct target_itimerspec {
    struct target_timespec it_interval;
    struct target_timespec it_value;
};

struct target__kernel_itimerspec {
    struct target__kernel_timespec it_interval;
    struct target__kernel_timespec it_value;
};

struct target_timex {
    abi_uint modes;              /* Mode selector */
    abi_long offset;             /* Time offset */
    abi_long freq;               /* Frequency offset */
    abi_long maxerror;           /* Maximum error (microseconds) */
    abi_long esterror;           /* Estimated error (microseconds) */
    abi_int status;              /* Clock command/status */
    abi_long constant;           /* PLL (phase-locked loop) time constant */
    abi_long precision;          /* Clock precision (microseconds, ro) */
    abi_long tolerance;          /* Clock freq. tolerance (ppm, ro) */
    struct target_timeval time;  /* Current time */
    abi_long tick;               /* Microseconds between clock ticks */
    abi_long ppsfreq;            /* PPS (pulse per second) frequency */
    abi_long jitter;             /* PPS jitter (ro); nanoseconds */
    abi_int shift;               /* PPS interval duration (seconds) */
    abi_long stabil;             /* PPS stability */
    abi_long jitcnt;             /* PPS jitter limit exceeded (ro) */
    abi_long calcnt;             /* PPS calibration intervals */
    abi_long errcnt;             /* PPS calibration errors */
    abi_long stbcnt;             /* PPS stability limit exceeded */
    abi_int tai;                 /* TAI offset */

    /* Further padding bytes to allow for future expansion */
    abi_int:32; abi_int:32; abi_int:32; abi_int:32;
    abi_int:32; abi_int:32; abi_int:32; abi_int:32;
    abi_int:32; abi_int:32; abi_int:32;
};

struct target__kernel_timex {
    abi_uint modes;               /* Mode selector */
    abi_int: 32;                  /* pad */
    abi_llong offset;             /* Time offset */
    abi_llong freq;               /* Frequency offset */
    abi_llong maxerror;           /* Maximum error (microseconds) */
    abi_llong esterror;           /* Estimated error (microseconds) */
    abi_int status;               /* Clock command/status */
    abi_int: 32;                  /* pad */
    abi_llong constant;           /* PLL (phase-locked loop) time constant */
    abi_llong precision;          /* Clock precision (microseconds, ro) */
    abi_llong tolerance;          /* Clock freq. tolerance (ppm, ro) */
    struct target__kernel_sock_timeval time;  /* Current time */
    abi_llong tick;               /* Microseconds between clock ticks */
    abi_llong ppsfreq;            /* PPS (pulse per second) frequency */
    abi_llong jitter;             /* PPS jitter (ro); nanoseconds */
    abi_int shift;                /* PPS interval duration (seconds) */
    abi_int: 32;                  /* pad */
    abi_llong stabil;             /* PPS stability */
    abi_llong jitcnt;             /* PPS jitter limit exceeded (ro) */
    abi_llong calcnt;             /* PPS calibration intervals */
    abi_llong errcnt;             /* PPS calibration errors */
    abi_llong stbcnt;             /* PPS stability limit exceeded */
    abi_int tai;                  /* TAI offset */

    /* Further padding bytes to allow for future expansion */
    abi_int:32; abi_int:32; abi_int:32; abi_int:32;
    abi_int:32; abi_int:32; abi_int:32; abi_int:32;
    abi_int:32; abi_int:32; abi_int:32;
};

typedef abi_long target_clock_t;

#define TARGET_HZ 100

struct target_tms {
    target_clock_t tms_utime;
    target_clock_t tms_stime;
    target_clock_t tms_cutime;
    target_clock_t tms_cstime;
};

struct target_utimbuf {
    abi_long actime;
    abi_long modtime;
};

struct target_sel_arg_struct {
    abi_long n;
    abi_long inp, outp, exp;
    abi_long tvp;
};

struct target_iovec {
    abi_long iov_base;   /* Starting address */
    abi_long iov_len;   /* Number of bytes */
};

struct target_msghdr {
    abi_long	 msg_name;	 /* Socket name			*/
    int		 msg_namelen;	 /* Length of name		*/
    abi_long	 msg_iov;	 /* Data blocks			*/
    abi_long	 msg_iovlen;	 /* Number of blocks		*/
    abi_long     msg_control;	 /* Per protocol magic (eg BSD file descriptor passing) */
    abi_long	 msg_controllen; /* Length of cmsg list */
    unsigned int msg_flags;
};

struct target_cmsghdr {
    abi_long     cmsg_len;
    int          cmsg_level;
    int          cmsg_type;
};

#define TARGET_CMSG_DATA(cmsg) ((unsigned char *) ((struct target_cmsghdr *) (cmsg) + 1))
#define TARGET_CMSG_NXTHDR(mhdr, cmsg, cmsg_start) \
                               __target_cmsg_nxthdr(mhdr, cmsg, cmsg_start)
#define TARGET_CMSG_ALIGN(len) (((len) + sizeof (abi_long) - 1) \
                               & (size_t) ~(sizeof (abi_long) - 1))
#define TARGET_CMSG_SPACE(len) (sizeof(struct target_cmsghdr) + \
                                TARGET_CMSG_ALIGN(len))
#define TARGET_CMSG_LEN(len) (sizeof(struct target_cmsghdr) + (len))

static __inline__ struct target_cmsghdr *
__target_cmsg_nxthdr(struct target_msghdr *__mhdr,
                     struct target_cmsghdr *__cmsg,
                     struct target_cmsghdr *__cmsg_start)
{
  struct target_cmsghdr *__ptr;

  __ptr = (struct target_cmsghdr *)((unsigned char *) __cmsg
                                    + TARGET_CMSG_ALIGN (tswapal(__cmsg->cmsg_len)));
  if ((unsigned long)((char *)(__ptr+1) - (char *)__cmsg_start)
      > tswapal(__mhdr->msg_controllen)) {
    /* No more entries.  */
    return (struct target_cmsghdr *)0;
  }
  return __ptr;
}

struct target_mmsghdr {
    struct target_msghdr msg_hdr;              /* Message header */
    unsigned int         msg_len;              /* Number of bytes transmitted */
};

struct  target_rusage {
        struct target_timeval ru_utime;        /* user time used */
        struct target_timeval ru_stime;        /* system time used */
        abi_long    ru_maxrss;                 /* maximum resident set size */
        abi_long    ru_ixrss;                  /* integral shared memory size */
        abi_long    ru_idrss;                  /* integral unshared data size */
        abi_long    ru_isrss;                  /* integral unshared stack size */
        abi_long    ru_minflt;                 /* page reclaims */
        abi_long    ru_majflt;                 /* page faults */
        abi_long    ru_nswap;                  /* swaps */
        abi_long    ru_inblock;                /* block input operations */
        abi_long    ru_oublock;                /* block output operations */
        abi_long    ru_msgsnd;                 /* messages sent */
        abi_long    ru_msgrcv;                 /* messages received */
        abi_long    ru_nsignals;               /* signals received */
        abi_long    ru_nvcsw;                  /* voluntary context switches */
        abi_long    ru_nivcsw;                 /* involuntary " */
};

typedef struct {
        int     val[2];
} kernel_fsid_t;

struct target_dirent {
        abi_long        d_ino;
        abi_long        d_off;
        unsigned short  d_reclen;
        char            d_name[];
};

struct target_dirent64 {
	abi_ullong      d_ino;
	abi_llong       d_off;
	abi_ushort      d_reclen;
	unsigned char	d_type;
	char		d_name[];
};


/* mostly generic signal stuff */
#define TARGET_SIG_DFL	((abi_long)0)	/* default signal handling */
#define TARGET_SIG_IGN	((abi_long)1)	/* ignore signal */
#define TARGET_SIG_ERR	((abi_long)-1)	/* error return from signal */

#ifdef TARGET_MIPS
#define TARGET_NSIG	   128
#else
#define TARGET_NSIG	   64
#endif
#define TARGET_NSIG_BPW	   TARGET_ABI_BITS
#define TARGET_NSIG_WORDS  (TARGET_NSIG / TARGET_NSIG_BPW)

typedef struct {
    abi_ulong sig[TARGET_NSIG_WORDS];
} target_sigset_t;

#ifdef BSWAP_NEEDED
static inline void tswap_sigset(target_sigset_t *d, const target_sigset_t *s)
{
    int i;
    for(i = 0;i < TARGET_NSIG_WORDS; i++)
        d->sig[i] = tswapal(s->sig[i]);
}
#else
static inline void tswap_sigset(target_sigset_t *d, const target_sigset_t *s)
{
    *d = *s;
}
#endif

static inline void target_siginitset(target_sigset_t *d, abi_ulong set)
{
    int i;
    d->sig[0] = set;
    for(i = 1;i < TARGET_NSIG_WORDS; i++)
        d->sig[i] = 0;
}

void host_to_target_sigset(target_sigset_t *d, const sigset_t *s);
void target_to_host_sigset(sigset_t *d, const target_sigset_t *s);
void host_to_target_old_sigset(abi_ulong *old_sigset,
                               const sigset_t *sigset);
void target_to_host_old_sigset(sigset_t *sigset,
                               const abi_ulong *old_sigset);
struct target_sigaction;
int do_sigaction(int sig, const struct target_sigaction *act,
                 struct target_sigaction *oact, abi_ulong ka_restorer);

#include "target_signal.h"

#ifdef TARGET_SA_RESTORER
#define TARGET_ARCH_HAS_SA_RESTORER 1
#endif

#if defined(TARGET_ALPHA)
typedef int32_t target_old_sa_flags;
#else
typedef abi_ulong target_old_sa_flags;
#endif

#if defined(TARGET_MIPS)
struct target_sigaction {
	uint32_t	sa_flags;
#if defined(TARGET_ABI_MIPSN32)
	uint32_t	_sa_handler;
#else
	abi_ulong	_sa_handler;
#endif
	target_sigset_t	sa_mask;
#ifdef TARGET_ARCH_HAS_SA_RESTORER
        /* ??? This is always present, but ignored unless O32.  */
        abi_ulong sa_restorer;
#endif
};
#else
struct target_old_sigaction {
        abi_ulong _sa_handler;
        abi_ulong sa_mask;
        target_old_sa_flags sa_flags;
#ifdef TARGET_ARCH_HAS_SA_RESTORER
        abi_ulong sa_restorer;
#endif
};

struct target_sigaction {
        abi_ulong _sa_handler;
        abi_ulong sa_flags;
#ifdef TARGET_ARCH_HAS_SA_RESTORER
        abi_ulong sa_restorer;
#endif
        target_sigset_t sa_mask;
#ifdef TARGET_ARCH_HAS_KA_RESTORER
        abi_ulong ka_restorer;
#endif
};
#endif

typedef union target_sigval {
	int sival_int;
        abi_ulong sival_ptr;
} target_sigval_t;
#if 0
#if defined (TARGET_SPARC)
typedef struct {
	struct {
		abi_ulong psr;
		abi_ulong pc;
		abi_ulong npc;
		abi_ulong y;
		abi_ulong u_regs[16]; /* globals and ins */
	}		si_regs;
	int		si_mask;
} __siginfo_t;

typedef struct {
	unsigned   long si_float_regs [32];
	unsigned   long si_fsr;
	unsigned   long si_fpqdepth;
	struct {
		unsigned long *insn_addr;
		unsigned long insn;
	} si_fpqueue [16];
} __siginfo_fpu_t;
#endif
#endif

#define TARGET_SI_MAX_SIZE	128

#if TARGET_ABI_BITS == 32
#define TARGET_SI_PREAMBLE_SIZE (3 * sizeof(int))
#else
#define TARGET_SI_PREAMBLE_SIZE (4 * sizeof(int))
#endif

#define TARGET_SI_PAD_SIZE ((TARGET_SI_MAX_SIZE - TARGET_SI_PREAMBLE_SIZE) / sizeof(int))

/* Within QEMU the top 16 bits of si_code indicate which of the parts of
 * the union in target_siginfo is valid. This only applies between
 * host_to_target_siginfo_noswap() and tswap_siginfo(); it does not
 * appear either within host siginfo_t or in target_siginfo structures
 * which we get from the guest userspace program. (The Linux kernel
 * does a similar thing with using the top bits for its own internal
 * purposes but not letting them be visible to userspace.)
 */
#define QEMU_SI_KILL 0
#define QEMU_SI_TIMER 1
#define QEMU_SI_POLL 2
#define QEMU_SI_FAULT 3
#define QEMU_SI_CHLD 4
#define QEMU_SI_RT 5

typedef struct target_siginfo {
#ifdef TARGET_MIPS
	int si_signo;
	int si_code;
	int si_errno;
#else
	int si_signo;
	int si_errno;
	int si_code;
#endif

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
			abi_ulong _addr; /* faulting insn/memory ref. */
		} _sigfault;

		/* SIGPOLL */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;
	} _sifields;
} target_siginfo_t;

/*
 * si_code values
 * Digital reserves positive values for kernel-generated signals.
 */
#define TARGET_SI_USER		0	/* sent by kill, sigsend, raise */
#define TARGET_SI_KERNEL	0x80	/* sent by the kernel from somewhere */
#define TARGET_SI_QUEUE	-1		/* sent by sigqueue */
#define TARGET_SI_TIMER -2              /* sent by timer expiration */
#define TARGET_SI_MESGQ	-3		/* sent by real time mesq state change */
#define TARGET_SI_ASYNCIO	-4	/* sent by AIO completion */
#define TARGET_SI_SIGIO	-5		/* sent by queued SIGIO */

/*
 * SIGILL si_codes
 */
#define TARGET_ILL_ILLOPC	(1)	/* illegal opcode */
#define TARGET_ILL_ILLOPN	(2)	/* illegal operand */
#define TARGET_ILL_ILLADR	(3)	/* illegal addressing mode */
#define TARGET_ILL_ILLTRP	(4)	/* illegal trap */
#define TARGET_ILL_PRVOPC	(5)	/* privileged opcode */
#define TARGET_ILL_PRVREG	(6)	/* privileged register */
#define TARGET_ILL_COPROC	(7)	/* coprocessor error */
#define TARGET_ILL_BADSTK	(8)	/* internal stack error */

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
#define TARGET_FPE_FLTUNK      (14) /* undiagnosed fp exception */
#define TARGET_FPE_CONDTRAP    (15) /* trap on condition */

/*
 * SIGSEGV si_codes
 */
#define TARGET_SEGV_MAPERR     (1)  /* address not mapped to object */
#define TARGET_SEGV_ACCERR     (2)  /* invalid permissions for mapped object */
#define TARGET_SEGV_BNDERR     (3)  /* failed address bound checks */

/*
 * SIGBUS si_codes
 */
#define TARGET_BUS_ADRALN       (1)	/* invalid address alignment */
#define TARGET_BUS_ADRERR       (2)	/* non-existent physical address */
#define TARGET_BUS_OBJERR       (3)	/* object specific hardware error */
/* hardware memory error consumed on a machine check: action required */
#define TARGET_BUS_MCEERR_AR    (4)
/* hardware memory error detected in process but not consumed: action optional*/
#define TARGET_BUS_MCEERR_AO    (5)

/*
 * SIGTRAP si_codes
 */
#define TARGET_TRAP_BRKPT	(1)	/* process breakpoint */
#define TARGET_TRAP_TRACE	(2)	/* process trace trap */
#define TARGET_TRAP_BRANCH      (3)     /* process taken branch trap */
#define TARGET_TRAP_HWBKPT      (4)     /* hardware breakpoint/watchpoint */
#define TARGET_TRAP_UNK         (5)     /* undiagnosed trap */

#include "target_resource.h"

struct target_pollfd {
    int fd;           /* file descriptor */
    short events;     /* requested events */
    short revents;    /* returned events */
};

/* virtual terminal ioctls */
#define TARGET_KIOCSOUND       0x4B2F	/* start sound generation (0 for off) */
#define TARGET_KDMKTONE	       0x4B30	/* generate tone */
#define TARGET_KDGKBTYPE       0x4b33
#define TARGET_KDSETMODE       0x4b3a
#define TARGET_KDGKBMODE       0x4b44
#define TARGET_KDSKBMODE       0x4b45
#define TARGET_KDGKBENT	       0x4B46	/* gets one entry in translation table */
#define TARGET_KDGKBSENT       0x4B48	/* gets one function key string entry */
#define TARGET_KDGKBLED        0x4B64	/* get led flags (not lights) */
#define TARGET_KDSKBLED        0x4B65	/* set led flags (not lights) */
#define TARGET_KDGETLED        0x4B31	/* return current led state */
#define TARGET_KDSETLED        0x4B32	/* set led state [lights, not flags] */
#define TARGET_KDSIGACCEPT     0x4B4E

struct target_rtc_pll_info {
    int pll_ctrl;
    int pll_value;
    int pll_max;
    int pll_min;
    int pll_posmult;
    int pll_negmult;
    abi_long pll_clock;
};

/* real time clock ioctls */
#define TARGET_RTC_AIE_ON           TARGET_IO('p', 0x01)
#define TARGET_RTC_AIE_OFF          TARGET_IO('p', 0x02)
#define TARGET_RTC_UIE_ON           TARGET_IO('p', 0x03)
#define TARGET_RTC_UIE_OFF          TARGET_IO('p', 0x04)
#define TARGET_RTC_PIE_ON           TARGET_IO('p', 0x05)
#define TARGET_RTC_PIE_OFF          TARGET_IO('p', 0x06)
#define TARGET_RTC_WIE_ON           TARGET_IO('p', 0x0f)
#define TARGET_RTC_WIE_OFF          TARGET_IO('p', 0x10)
#define TARGET_RTC_ALM_READ         TARGET_IOR('p', 0x08, struct rtc_time)
#define TARGET_RTC_ALM_SET          TARGET_IOW('p', 0x07, struct rtc_time)
#define TARGET_RTC_RD_TIME          TARGET_IOR('p', 0x09, struct rtc_time)
#define TARGET_RTC_SET_TIME         TARGET_IOW('p', 0x0a, struct rtc_time)
#define TARGET_RTC_IRQP_READ        TARGET_IOR('p', 0x0b, abi_ulong)
#define TARGET_RTC_IRQP_SET         TARGET_IOW('p', 0x0c, abi_ulong)
#define TARGET_RTC_EPOCH_READ       TARGET_IOR('p', 0x0d, abi_ulong)
#define TARGET_RTC_EPOCH_SET        TARGET_IOW('p', 0x0e, abi_ulong)
#define TARGET_RTC_WKALM_RD         TARGET_IOR('p', 0x10, struct rtc_wkalrm)
#define TARGET_RTC_WKALM_SET        TARGET_IOW('p', 0x0f, struct rtc_wkalrm)
#define TARGET_RTC_PLL_GET          TARGET_IOR('p', 0x11,                      \
                                               struct target_rtc_pll_info)
#define TARGET_RTC_PLL_SET          TARGET_IOW('p', 0x12,                      \
                                               struct target_rtc_pll_info)
#define TARGET_RTC_VL_READ          TARGET_IOR('p', 0x13, int)
#define TARGET_RTC_VL_CLR           TARGET_IO('p', 0x14)

#if defined(TARGET_ALPHA) || defined(TARGET_MIPS) || defined(TARGET_SH4) ||    \
       defined(TARGET_XTENSA)
#define TARGET_FIOGETOWN       TARGET_IOR('f', 123, int)
#define TARGET_FIOSETOWN       TARGET_IOW('f', 124, int)
#define TARGET_SIOCATMARK      TARGET_IOR('s', 7, int)
#define TARGET_SIOCSPGRP       TARGET_IOW('s', 8, pid_t)
#define TARGET_SIOCGPGRP       TARGET_IOR('s', 9, pid_t)
#else
#define TARGET_FIOGETOWN       0x8903
#define TARGET_FIOSETOWN       0x8901
#define TARGET_SIOCATMARK      0x8905
#define TARGET_SIOCSPGRP       0x8902
#define TARGET_SIOCGPGRP       0x8904
#endif

#if defined(TARGET_SH4)
#define TARGET_SIOCGSTAMP_OLD   TARGET_IOR('s', 100, struct target_timeval)
#define TARGET_SIOCGSTAMPNS_OLD TARGET_IOR('s', 101, struct target_timespec)
#else
#define TARGET_SIOCGSTAMP_OLD   0x8906
#define TARGET_SIOCGSTAMPNS_OLD 0x8907
#endif

#define TARGET_SIOCGSTAMP_NEW   TARGET_IOR(0x89, 0x06, abi_llong[2])
#define TARGET_SIOCGSTAMPNS_NEW TARGET_IOR(0x89, 0x07, abi_llong[2])

/* Networking ioctls */
#define TARGET_SIOCADDRT       0x890B          /* add routing table entry */
#define TARGET_SIOCDELRT       0x890C          /* delete routing table entry */
#define TARGET_SIOCGIFNAME     0x8910          /* get iface name               */
#define TARGET_SIOCSIFLINK     0x8911          /* set iface channel            */
#define TARGET_SIOCGIFCONF     0x8912          /* get iface list               */
#define TARGET_SIOCGIFFLAGS    0x8913          /* get flags                    */
#define TARGET_SIOCSIFFLAGS    0x8914          /* set flags                    */
#define TARGET_SIOCGIFADDR     0x8915          /* get PA address               */
#define TARGET_SIOCSIFADDR     0x8916          /* set PA address               */
#define TARGET_SIOCGIFDSTADDR  0x8917          /* get remote PA address        */
#define TARGET_SIOCSIFDSTADDR  0x8918          /* set remote PA address        */
#define TARGET_SIOCGIFBRDADDR  0x8919          /* get broadcast PA address     */
#define TARGET_SIOCSIFBRDADDR  0x891a          /* set broadcast PA address     */
#define TARGET_SIOCGIFNETMASK  0x891b          /* get network PA mask          */
#define TARGET_SIOCSIFNETMASK  0x891c          /* set network PA mask          */
#define TARGET_SIOCGIFMETRIC   0x891d          /* get metric                   */
#define TARGET_SIOCSIFMETRIC   0x891e          /* set metric                   */
#define TARGET_SIOCGIFMEM      0x891f          /* get memory address (BSD)     */
#define TARGET_SIOCSIFMEM      0x8920          /* set memory address (BSD)     */
#define TARGET_SIOCGIFMTU      0x8921          /* get MTU size                 */
#define TARGET_SIOCSIFMTU      0x8922          /* set MTU size                 */
#define TARGET_SIOCSIFHWADDR   0x8924          /* set hardware address (NI)    */
#define TARGET_SIOCGIFENCAP    0x8925          /* get/set slip encapsulation   */
#define TARGET_SIOCSIFENCAP    0x8926
#define TARGET_SIOCGIFHWADDR   0x8927          /* Get hardware address         */
#define TARGET_SIOCGIFSLAVE    0x8929          /* Driver slaving support       */
#define TARGET_SIOCSIFSLAVE    0x8930
#define TARGET_SIOCADDMULTI    0x8931          /* Multicast address lists      */
#define TARGET_SIOCDELMULTI    0x8932
#define TARGET_SIOCGIFINDEX    0x8933
#define TARGET_SIOCSIFPFLAGS   0x8934          /* set extended flags          */
#define TARGET_SIOCGIFPFLAGS   0x8935          /* get extended flags          */

/* Bridging control calls */
#define TARGET_SIOCGIFBR       0x8940          /* Bridging support             */
#define TARGET_SIOCSIFBR       0x8941          /* Set bridging options         */

#define TARGET_SIOCGIFTXQLEN   0x8942          /* Get the tx queue length      */
#define TARGET_SIOCSIFTXQLEN   0x8943          /* Set the tx queue length      */

/* ARP cache control calls. */
#define TARGET_OLD_SIOCDARP    0x8950          /* old delete ARP table entry   */
#define TARGET_OLD_SIOCGARP    0x8951          /* old get ARP table entry      */
#define TARGET_OLD_SIOCSARP    0x8952          /* old set ARP table entry      */
#define TARGET_SIOCDARP        0x8953          /* delete ARP table entry       */
#define TARGET_SIOCGARP        0x8954          /* get ARP table entry          */
#define TARGET_SIOCSARP        0x8955          /* set ARP table entry          */

/* RARP cache control calls. */
#define TARGET_SIOCDRARP       0x8960          /* delete RARP table entry      */
#define TARGET_SIOCGRARP       0x8961          /* get RARP table entry         */
#define TARGET_SIOCSRARP       0x8962          /* set RARP table entry         */

/* Driver configuration calls */
#define TARGET_SIOCGIFMAP      0x8970          /* Get device parameters        */
#define TARGET_SIOCSIFMAP      0x8971          /* Set device parameters        */

/* DLCI configuration calls */
#define TARGET_SIOCADDDLCI     0x8980          /* Create new DLCI device       */
#define TARGET_SIOCDELDLCI     0x8981          /* Delete DLCI device           */

/* From <linux/wireless.h> */

#define TARGET_SIOCGIWNAME     0x8B01          /* get name == wireless protocol */

/* From <linux/if_tun.h> */

#define TARGET_TUNSETDEBUG        TARGET_IOW('T', 201, int)
#define TARGET_TUNSETIFF          TARGET_IOW('T', 202, int)
#define TARGET_TUNSETPERSIST      TARGET_IOW('T', 203, int)
#define TARGET_TUNSETOWNER        TARGET_IOW('T', 204, int)
#define TARGET_TUNSETLINK         TARGET_IOW('T', 205, int)
#define TARGET_TUNSETGROUP        TARGET_IOW('T', 206, int)
#define TARGET_TUNGETFEATURES     TARGET_IOR('T', 207, unsigned int)
#define TARGET_TUNSETOFFLOAD      TARGET_IOW('T', 208, unsigned int)
#define TARGET_TUNSETTXFILTER     TARGET_IOW('T', 209, unsigned int)
#define TARGET_TUNGETIFF          TARGET_IOR('T', 210, unsigned int)
#define TARGET_TUNGETSNDBUF       TARGET_IOR('T', 211, int)
#define TARGET_TUNSETSNDBUF       TARGET_IOW('T', 212, int)
/*
 * TUNATTACHFILTER and TUNDETACHFILTER are not supported. Linux kernel keeps a
 * user pointer in TUNATTACHFILTER, which we are not able to correctly handle.
 */
#define TARGET_TUNGETVNETHDRSZ    TARGET_IOR('T', 215, int)
#define TARGET_TUNSETVNETHDRSZ    TARGET_IOW('T', 216, int)
#define TARGET_TUNSETQUEUE        TARGET_IOW('T', 217, int)
#define TARGET_TUNSETIFINDEX      TARGET_IOW('T', 218, unsigned int)
/* TUNGETFILTER is not supported: see TUNATTACHFILTER. */
#define TARGET_TUNSETVNETLE       TARGET_IOW('T', 220, int)
#define TARGET_TUNGETVNETLE       TARGET_IOR('T', 221, int)
#define TARGET_TUNSETVNETBE       TARGET_IOW('T', 222, int)
#define TARGET_TUNGETVNETBE       TARGET_IOR('T', 223, int)
#define TARGET_TUNSETSTEERINGEBPF TARGET_IOR('T', 224, int)
#define TARGET_TUNSETFILTEREBPF   TARGET_IOR('T', 225, int)
#define TARGET_TUNSETCARRIER      TARGET_IOW('T', 226, int)
#define TARGET_TUNGETDEVNETNS     TARGET_IO('T', 227)

/* From <linux/random.h> */

#define TARGET_RNDGETENTCNT    TARGET_IOR('R', 0x00, int)
#define TARGET_RNDADDTOENTCNT  TARGET_IOW('R', 0x01, int)
#define TARGET_RNDZAPENTCNT    TARGET_IO('R', 0x04)
#define TARGET_RNDCLEARPOOL    TARGET_IO('R', 0x06)
#define TARGET_RNDRESEEDCRNG   TARGET_IO('R', 0x07)

/* From <linux/fs.h> */

#define TARGET_BLKROSET   TARGET_IO(0x12,93) /* set device read-only (0 = read-write) */
#define TARGET_BLKROGET   TARGET_IO(0x12,94) /* get read-only status (0 = read_write) */
#define TARGET_BLKRRPART  TARGET_IO(0x12,95) /* re-read partition table */
#define TARGET_BLKGETSIZE TARGET_IO(0x12,96) /* return device size /512 (long *arg) */
#define TARGET_BLKFLSBUF  TARGET_IO(0x12,97) /* flush buffer cache */
#define TARGET_BLKRASET   TARGET_IO(0x12,98) /* Set read ahead for block device */
#define TARGET_BLKRAGET   TARGET_IO(0x12,99) /* get current read ahead setting */
#define TARGET_BLKFRASET  TARGET_IO(0x12,100)/* set filesystem (mm/filemap.c) read-ahead */
#define TARGET_BLKFRAGET  TARGET_IO(0x12,101)/* get filesystem (mm/filemap.c) read-ahead */
#define TARGET_BLKSECTSET TARGET_IO(0x12,102)/* set max sectors per request (ll_rw_blk.c) */
#define TARGET_BLKSECTGET TARGET_IO(0x12,103)/* get max sectors per request (ll_rw_blk.c) */
#define TARGET_BLKSSZGET  TARGET_IO(0x12,104)/* get block device sector size */
#define TARGET_BLKPG      TARGET_IO(0x12,105)/* Partition table and disk geometry handling */
/* A jump here: 108-111 have been used for various private purposes. */
#define TARGET_BLKBSZGET  TARGET_IOR(0x12, 112, abi_ulong)
#define TARGET_BLKBSZSET  TARGET_IOW(0x12, 113, abi_ulong)
#define TARGET_BLKGETSIZE64 TARGET_IOR(0x12,114,abi_ulong)
                                             /* return device size in bytes
                                                (u64 *arg) */

#define TARGET_BLKDISCARD TARGET_IO(0x12, 119)
#define TARGET_BLKIOMIN TARGET_IO(0x12, 120)
#define TARGET_BLKIOOPT TARGET_IO(0x12, 121)
#define TARGET_BLKALIGNOFF TARGET_IO(0x12, 122)
#define TARGET_BLKPBSZGET TARGET_IO(0x12, 123)
#define TARGET_BLKDISCARDZEROES TARGET_IO(0x12, 124)
#define TARGET_BLKSECDISCARD TARGET_IO(0x12, 125)
#define TARGET_BLKROTATIONAL TARGET_IO(0x12, 126)
#define TARGET_BLKZEROOUT TARGET_IO(0x12, 127)

/* From <linux/fd.h> */

#define TARGET_FDMSGON        TARGET_IO(2, 0x45)
#define TARGET_FDMSGOFF       TARGET_IO(2, 0x46)
#define TARGET_FDFMTBEG       TARGET_IO(2, 0x47)
#define TARGET_FDFMTTRK      TARGET_IOW(2, 0x48, struct format_descr)
#define TARGET_FDFMTEND       TARGET_IO(2, 0x49)
#define TARGET_FDSETEMSGTRESH TARGET_IO(2, 0x4a)
#define TARGET_FDFLUSH        TARGET_IO(2, 0x4b)
#define TARGET_FDSETMAXERRS  TARGET_IOW(2, 0x4c, struct floppy_max_errors)
#define TARGET_FDGETMAXERRS  TARGET_IOR(2, 0x0e, struct floppy_max_errors)
#define TARGET_FDRESET        TARGET_IO(2, 0x54)
#define TARGET_FDRAWCMD       TARGET_IO(2, 0x58)
#define TARGET_FDTWADDLE      TARGET_IO(2, 0x59)
#define TARGET_FDEJECT        TARGET_IO(2, 0x5a)

#define TARGET_FIBMAP     TARGET_IO(0x00,1)  /* bmap access */
#define TARGET_FIGETBSZ   TARGET_IO(0x00,2)  /* get the block size used for bmap */

#define TARGET_FICLONE    TARGET_IOW(0x94, 9, int)
#define TARGET_FICLONERANGE TARGET_IOW(0x94, 13, struct file_clone_range)

/*
 * Note that the ioctl numbers for FS_IOC_<GET|SET><FLAGS|VERSION>
 * claim type "long" but the actual type used by the kernel is "int".
 */
#define TARGET_FS_IOC_GETFLAGS TARGET_IOR('f', 1, abi_long)
#define TARGET_FS_IOC_SETFLAGS TARGET_IOW('f', 2, abi_long)
#define TARGET_FS_IOC_GETVERSION TARGET_IOR('v', 1, abi_long)
#define TARGET_FS_IOC_SETVERSION TARGET_IOW('v', 2, abi_long)
#define TARGET_FS_IOC_FIEMAP TARGET_IOWR('f',11,struct fiemap)
#define TARGET_FS_IOC32_GETFLAGS TARGET_IOR('f', 1, int)
#define TARGET_FS_IOC32_SETFLAGS TARGET_IOW('f', 2, int)
#define TARGET_FS_IOC32_GETVERSION TARGET_IOR('v', 1, int)
#define TARGET_FS_IOC32_SETVERSION TARGET_IOW('v', 2, int)

/* btrfs ioctls */
#ifdef HAVE_BTRFS_H
#define TARGET_BTRFS_IOC_SNAP_CREATE            TARGET_IOWU(BTRFS_IOCTL_MAGIC, 1)
#define TARGET_BTRFS_IOC_SCAN_DEV               TARGET_IOWU(BTRFS_IOCTL_MAGIC, 4)
#define TARGET_BTRFS_IOC_FORGET_DEV             TARGET_IOWU(BTRFS_IOCTL_MAGIC, 5)
#define TARGET_BTRFS_IOC_ADD_DEV                TARGET_IOWU(BTRFS_IOCTL_MAGIC, 10)
#define TARGET_BTRFS_IOC_RM_DEV                 TARGET_IOWU(BTRFS_IOCTL_MAGIC, 11)
#define TARGET_BTRFS_IOC_SUBVOL_CREATE          TARGET_IOWU(BTRFS_IOCTL_MAGIC, 14)
#define TARGET_BTRFS_IOC_SNAP_DESTROY           TARGET_IOWU(BTRFS_IOCTL_MAGIC, 15)
#define TARGET_BTRFS_IOC_INO_LOOKUP             TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 18)
#define TARGET_BTRFS_IOC_DEFAULT_SUBVOL         TARGET_IOW(BTRFS_IOCTL_MAGIC, 19,\
                                                           abi_ullong)
#define TARGET_BTRFS_IOC_SUBVOL_GETFLAGS        TARGET_IOR(BTRFS_IOCTL_MAGIC, 25,\
                                                           abi_ullong)
#define TARGET_BTRFS_IOC_SUBVOL_SETFLAGS        TARGET_IOW(BTRFS_IOCTL_MAGIC, 26,\
                                                           abi_ullong)
#define TARGET_BTRFS_IOC_SCRUB                  TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 27)
#define TARGET_BTRFS_IOC_SCRUB_CANCEL           TARGET_IO(BTRFS_IOCTL_MAGIC, 28)
#define TARGET_BTRFS_IOC_SCRUB_PROGRESS         TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 29)
#define TARGET_BTRFS_IOC_DEV_INFO               TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 30)
#define TARGET_BTRFS_IOC_INO_PATHS              TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 35)
#define TARGET_BTRFS_IOC_LOGICAL_INO            TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 36)
#define TARGET_BTRFS_IOC_QUOTA_CTL              TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 40)
#define TARGET_BTRFS_IOC_QGROUP_ASSIGN          TARGET_IOWU(BTRFS_IOCTL_MAGIC, 41)
#define TARGET_BTRFS_IOC_QGROUP_CREATE          TARGET_IOWU(BTRFS_IOCTL_MAGIC, 42)
#define TARGET_BTRFS_IOC_QGROUP_LIMIT           TARGET_IORU(BTRFS_IOCTL_MAGIC, 43)
#define TARGET_BTRFS_IOC_QUOTA_RESCAN           TARGET_IOWU(BTRFS_IOCTL_MAGIC, 44)
#define TARGET_BTRFS_IOC_QUOTA_RESCAN_STATUS    TARGET_IORU(BTRFS_IOCTL_MAGIC, 45)
#define TARGET_BTRFS_IOC_QUOTA_RESCAN_WAIT      TARGET_IO(BTRFS_IOCTL_MAGIC, 46)
#define TARGET_BTRFS_IOC_GET_DEV_STATS          TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 52)
#define TARGET_BTRFS_IOC_GET_FEATURES           TARGET_IORU(BTRFS_IOCTL_MAGIC, 57)
#define TARGET_BTRFS_IOC_SET_FEATURES           TARGET_IOWU(BTRFS_IOCTL_MAGIC, 57)
#define TARGET_BTRFS_IOC_GET_SUPPORTED_FEATURES TARGET_IORU(BTRFS_IOCTL_MAGIC, 57)
#define TARGET_BTRFS_IOC_LOGICAL_INO_V2         TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 59)
#define TARGET_BTRFS_IOC_GET_SUBVOL_INFO        TARGET_IORU(BTRFS_IOCTL_MAGIC, 60)
#define TARGET_BTRFS_IOC_GET_SUBVOL_ROOTREF     TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 61)
#define TARGET_BTRFS_IOC_INO_LOOKUP_USER        TARGET_IOWRU(BTRFS_IOCTL_MAGIC, 62)
#endif

/* usb ioctls */
#define TARGET_USBDEVFS_CONTROL TARGET_IOWRU('U', 0)
#define TARGET_USBDEVFS_BULK TARGET_IOWRU('U', 2)
#define TARGET_USBDEVFS_RESETEP TARGET_IORU('U', 3)
#define TARGET_USBDEVFS_SETINTERFACE TARGET_IORU('U', 4)
#define TARGET_USBDEVFS_SETCONFIGURATION TARGET_IORU('U',  5)
#define TARGET_USBDEVFS_GETDRIVER TARGET_IOWU('U', 8)
#define TARGET_USBDEVFS_SUBMITURB TARGET_IORU('U', 10)
#define TARGET_USBDEVFS_DISCARDURB TARGET_IO('U', 11)
#define TARGET_USBDEVFS_REAPURB TARGET_IOWU('U', 12)
#define TARGET_USBDEVFS_REAPURBNDELAY TARGET_IOWU('U', 13)
#define TARGET_USBDEVFS_DISCSIGNAL TARGET_IORU('U', 14)
#define TARGET_USBDEVFS_CLAIMINTERFACE TARGET_IORU('U', 15)
#define TARGET_USBDEVFS_RELEASEINTERFACE TARGET_IORU('U', 16)
#define TARGET_USBDEVFS_CONNECTINFO TARGET_IOWU('U', 17)
#define TARGET_USBDEVFS_IOCTL TARGET_IOWRU('U', 18)
#define TARGET_USBDEVFS_HUB_PORTINFO TARGET_IORU('U', 19)
#define TARGET_USBDEVFS_RESET TARGET_IO('U', 20)
#define TARGET_USBDEVFS_CLEAR_HALT TARGET_IORU('U', 21)
#define TARGET_USBDEVFS_DISCONNECT TARGET_IO('U', 22)
#define TARGET_USBDEVFS_CONNECT TARGET_IO('U', 23)
#define TARGET_USBDEVFS_CLAIM_PORT TARGET_IORU('U', 24)
#define TARGET_USBDEVFS_RELEASE_PORT TARGET_IORU('U', 25)
#define TARGET_USBDEVFS_GET_CAPABILITIES TARGET_IORU('U', 26)
#define TARGET_USBDEVFS_DISCONNECT_CLAIM TARGET_IORU('U', 27)
#define TARGET_USBDEVFS_DROP_PRIVILEGES TARGET_IOWU('U', 30)
#define TARGET_USBDEVFS_GET_SPEED TARGET_IO('U', 31)

/* cdrom commands */
#define TARGET_CDROMPAUSE		0x5301 /* Pause Audio Operation */
#define TARGET_CDROMRESUME		0x5302 /* Resume paused Audio Operation */
#define TARGET_CDROMPLAYMSF		0x5303 /* Play Audio MSF (struct cdrom_msf) */
#define TARGET_CDROMPLAYTRKIND		0x5304 /* Play Audio Track/index
                                           (struct cdrom_ti) */
#define TARGET_CDROMREADTOCHDR		0x5305 /* Read TOC header
                                           (struct cdrom_tochdr) */
#define TARGET_CDROMREADTOCENTRY	0x5306 /* Read TOC entry
                                           (struct cdrom_tocentry) */
#define TARGET_CDROMSTOP		0x5307 /* Stop the cdrom drive */
#define TARGET_CDROMSTART		0x5308 /* Start the cdrom drive */
#define TARGET_CDROMEJECT		0x5309 /* Ejects the cdrom media */
#define TARGET_CDROMVOLCTRL		0x530a /* Control output volume
                                           (struct cdrom_volctrl) */
#define TARGET_CDROMSUBCHNL		0x530b /* Read subchannel data
                                           (struct cdrom_subchnl) */
#define TARGET_CDROMREADMODE2		0x530c /* Read TARGET_CDROM mode 2 data (2336 Bytes)
                                           (struct cdrom_read) */
#define TARGET_CDROMREADMODE1		0x530d /* Read TARGET_CDROM mode 1 data (2048 Bytes)
                                           (struct cdrom_read) */
#define TARGET_CDROMREADAUDIO		0x530e /* (struct cdrom_read_audio) */
#define TARGET_CDROMEJECT_SW		0x530f /* enable(1)/disable(0) auto-ejecting */
#define TARGET_CDROMMULTISESSION	0x5310 /* Obtain the start-of-last-session
                                           address of multi session disks
                                           (struct cdrom_multisession) */
#define TARGET_CDROM_GET_MCN		0x5311 /* Obtain the "Universal Product Code"
                                           if available (struct cdrom_mcn) */
#define TARGET_CDROM_GET_UPC		TARGET_CDROM_GET_MCN  /* This one is deprecated,
                                          but here anyway for compatibility */
#define TARGET_CDROMRESET		0x5312 /* hard-reset the drive */
#define TARGET_CDROMVOLREAD		0x5313 /* Get the drive's volume setting
                                          (struct cdrom_volctrl) */
#define TARGET_CDROMREADRAW		0x5314	/* read data in raw mode (2352 Bytes)
                                           (struct cdrom_read) */
/*
 * These ioctls are used only used in aztcd.c and optcd.c
 */
#define TARGET_CDROMREADCOOKED		0x5315	/* read data in cooked mode */
#define TARGET_CDROMSEEK		0x5316  /* seek msf address */

/*
 * This ioctl is only used by the scsi-cd driver.
   It is for playing audio in logical block addressing mode.
 */
#define TARGET_CDROMPLAYBLK		0x5317	/* (struct cdrom_blk) */

/*
 * These ioctls are only used in optcd.c
 */
#define TARGET_CDROMREADALL		0x5318	/* read all 2646 bytes */

/*
 * These ioctls are (now) only in ide-cd.c for controlling
 * drive spindown time.  They should be implemented in the
 * Uniform driver, via generic packet commands, GPCMD_MODE_SELECT_10,
 * GPCMD_MODE_SENSE_10 and the GPMODE_POWER_PAGE...
 *  -Erik
 */
#define TARGET_CDROMGETSPINDOWN        0x531d
#define TARGET_CDROMSETSPINDOWN        0x531e

/*
 * These ioctls are implemented through the uniform CD-ROM driver
 * They _will_ be adopted by all CD-ROM drivers, when all the CD-ROM
 * drivers are eventually ported to the uniform CD-ROM driver interface.
 */
#define TARGET_CDROMCLOSETRAY		0x5319	/* pendant of CDROMEJECT */
#define TARGET_CDROM_SET_OPTIONS	0x5320  /* Set behavior options */
#define TARGET_CDROM_CLEAR_OPTIONS	0x5321  /* Clear behavior options */
#define TARGET_CDROM_SELECT_SPEED	0x5322  /* Set the CD-ROM speed */
#define TARGET_CDROM_SELECT_DISC	0x5323  /* Select disc (for juke-boxes) */
#define TARGET_CDROM_MEDIA_CHANGED	0x5325  /* Check is media changed  */
#define TARGET_CDROM_DRIVE_STATUS	0x5326  /* Get tray position, etc. */
#define TARGET_CDROM_DISC_STATUS	0x5327  /* Get disc type, etc. */
#define TARGET_CDROM_CHANGER_NSLOTS    0x5328  /* Get number of slots */
#define TARGET_CDROM_LOCKDOOR		0x5329  /* lock or unlock door */
#define TARGET_CDROM_DEBUG		0x5330	/* Turn debug messages on/off */
#define TARGET_CDROM_GET_CAPABILITY	0x5331	/* get capabilities */

/* Note that scsi/scsi_ioctl.h also uses 0x5382 - 0x5386.
 * Future CDROM ioctls should be kept below 0x537F
 */

/* This ioctl is only used by sbpcd at the moment */
#define TARGET_CDROMAUDIOBUFSIZ        0x5382	/* set the audio buffer size */
					/* conflict with SCSI_IOCTL_GET_IDLUN */

/* DVD-ROM Specific ioctls */
#define TARGET_DVD_READ_STRUCT		0x5390  /* Read structure */
#define TARGET_DVD_WRITE_STRUCT	0x5391  /* Write structure */
#define TARGET_DVD_AUTH		0x5392  /* Authentication */

#define TARGET_CDROM_SEND_PACKET	0x5393	/* send a packet to the drive */
#define TARGET_CDROM_NEXT_WRITABLE	0x5394	/* get next writable block */
#define TARGET_CDROM_LAST_WRITTEN	0x5395	/* get last block written on disc */

/* HD commands */

/* hd/ide ctl's that pass (arg) ptrs to user space are numbered 0x030n/0x031n */
#define TARGET_HDIO_GETGEO            0x0301  /* get device geometry */
#define TARGET_HDIO_GET_UNMASKINTR    0x0302  /* get current unmask setting */
#define TARGET_HDIO_GET_MULTCOUNT     0x0304  /* get current IDE blockmode setting */
#define TARGET_HDIO_GET_KEEPSETTINGS  0x0308  /* get keep-settings-on-reset flag */
#define TARGET_HDIO_GET_32BIT         0x0309  /* get current io_32bit setting */
#define TARGET_HDIO_GET_NOWERR        0x030a  /* get ignore-write-error flag */
#define TARGET_HDIO_GET_DMA           0x030b  /* get use-dma flag */
#define TARGET_HDIO_GET_IDENTITY      0x030d  /* get IDE identification info */
#define TARGET_HDIO_DRIVE_CMD         0x031f  /* execute a special drive command */

/* hd/ide ctl's that pass (arg) non-ptr values are numbered 0x032n/0x033n */
#define TARGET_HDIO_SET_MULTCOUNT     0x0321  /* change IDE blockmode */
#define TARGET_HDIO_SET_UNMASKINTR    0x0322  /* permit other irqs during I/O */
#define TARGET_HDIO_SET_KEEPSETTINGS  0x0323  /* keep ioctl settings on reset */
#define TARGET_HDIO_SET_32BIT         0x0324  /* change io_32bit flags */
#define TARGET_HDIO_SET_NOWERR        0x0325  /* change ignore-write-error flag */
#define TARGET_HDIO_SET_DMA           0x0326  /* change use-dma flag */
#define TARGET_HDIO_SET_PIO_MODE      0x0327  /* reconfig interface to new speed */

/* loop ioctls */
#define TARGET_LOOP_SET_FD            0x4C00
#define TARGET_LOOP_CLR_FD            0x4C01
#define TARGET_LOOP_SET_STATUS        0x4C02
#define TARGET_LOOP_GET_STATUS        0x4C03
#define TARGET_LOOP_SET_STATUS64      0x4C04
#define TARGET_LOOP_GET_STATUS64      0x4C05
#define TARGET_LOOP_CHANGE_FD         0x4C06
#define TARGET_LOOP_SET_CAPACITY      0x4C07
#define TARGET_LOOP_SET_DIRECT_IO     0x4C08
#define TARGET_LOOP_SET_BLOCK_SIZE    0x4C09
#define TARGET_LOOP_CONFIGURE         0x4C0A

#define TARGET_LOOP_CTL_ADD           0x4C80
#define TARGET_LOOP_CTL_REMOVE        0x4C81
#define TARGET_LOOP_CTL_GET_FREE      0x4C82

/* fb ioctls */
#define TARGET_FBIOGET_VSCREENINFO    0x4600
#define TARGET_FBIOPUT_VSCREENINFO    0x4601
#define TARGET_FBIOGET_FSCREENINFO    0x4602
#define TARGET_FBIOGETCMAP            0x4604
#define TARGET_FBIOPUTCMAP            0x4605
#define TARGET_FBIOPAN_DISPLAY        0x4606
#define TARGET_FBIOGET_CON2FBMAP      0x460F
#define TARGET_FBIOPUT_CON2FBMAP      0x4610

/* vt ioctls */
#define TARGET_VT_OPENQRY             0x5600
#define TARGET_VT_GETSTATE            0x5603
#define TARGET_VT_ACTIVATE            0x5606
#define TARGET_VT_WAITACTIVE          0x5607
#define TARGET_VT_LOCKSWITCH          0x560b
#define TARGET_VT_UNLOCKSWITCH        0x560c
#define TARGET_VT_GETMODE             0x5601
#define TARGET_VT_SETMODE             0x5602
#define TARGET_VT_RELDISP             0x5605
#define TARGET_VT_DISALLOCATE         0x5608

/* device mapper */
#define TARGET_DM_VERSION             TARGET_IOWRU(0xfd, 0x00)
#define TARGET_DM_REMOVE_ALL          TARGET_IOWRU(0xfd, 0x01)
#define TARGET_DM_LIST_DEVICES        TARGET_IOWRU(0xfd, 0x02)
#define TARGET_DM_DEV_CREATE          TARGET_IOWRU(0xfd, 0x03)
#define TARGET_DM_DEV_REMOVE          TARGET_IOWRU(0xfd, 0x04)
#define TARGET_DM_DEV_RENAME          TARGET_IOWRU(0xfd, 0x05)
#define TARGET_DM_DEV_SUSPEND         TARGET_IOWRU(0xfd, 0x06)
#define TARGET_DM_DEV_STATUS          TARGET_IOWRU(0xfd, 0x07)
#define TARGET_DM_DEV_WAIT            TARGET_IOWRU(0xfd, 0x08)
#define TARGET_DM_TABLE_LOAD          TARGET_IOWRU(0xfd, 0x09)
#define TARGET_DM_TABLE_CLEAR         TARGET_IOWRU(0xfd, 0x0a)
#define TARGET_DM_TABLE_DEPS          TARGET_IOWRU(0xfd, 0x0b)
#define TARGET_DM_TABLE_STATUS        TARGET_IOWRU(0xfd, 0x0c)
#define TARGET_DM_LIST_VERSIONS       TARGET_IOWRU(0xfd, 0x0d)
#define TARGET_DM_TARGET_MSG          TARGET_IOWRU(0xfd, 0x0e)
#define TARGET_DM_DEV_SET_GEOMETRY    TARGET_IOWRU(0xfd, 0x0f)

/* drm ioctls */
#define TARGET_DRM_IOCTL_VERSION      TARGET_IOWRU('d', 0x00)

/* drm i915 ioctls */
#define TARGET_DRM_IOCTL_I915_GETPARAM              TARGET_IOWRU('d', 0x46)

/* from asm/termbits.h */

#define TARGET_NCC 8
struct target_termio {
	unsigned short c_iflag;		/* input mode flags */
	unsigned short c_oflag;		/* output mode flags */
	unsigned short c_cflag;		/* control mode flags */
	unsigned short c_lflag;		/* local mode flags */
	unsigned char c_line;		/* line discipline */
	unsigned char c_cc[TARGET_NCC];	/* control characters */
};

struct target_winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

#include "termbits.h"

#if defined(TARGET_MIPS)
#define TARGET_PROT_SEM         0x10
#else
#define TARGET_PROT_SEM         0x08
#endif

#ifdef TARGET_AARCH64
#define TARGET_PROT_BTI         0x10
#define TARGET_PROT_MTE         0x20
#endif

/* Common */
#define TARGET_MAP_SHARED	0x01		/* Share changes */
#define TARGET_MAP_PRIVATE	0x02		/* Changes are private */
#if defined(TARGET_HPPA)
#define TARGET_MAP_TYPE         0x03		/* Mask for type of mapping */
#else
#define TARGET_MAP_TYPE         0x0f		/* Mask for type of mapping */
#endif

/* Target specific */
#if defined(TARGET_MIPS)
#define TARGET_MAP_FIXED	0x10		/* Interpret addr exactly */
#define TARGET_MAP_ANONYMOUS	0x0800		/* don't use a file */
#define TARGET_MAP_GROWSDOWN	0x1000		/* stack-like segment */
#define TARGET_MAP_DENYWRITE	0x2000		/* ETXTBSY */
#define TARGET_MAP_EXECUTABLE	0x4000		/* mark it as an executable */
#define TARGET_MAP_LOCKED	0x8000		/* pages are locked */
#define TARGET_MAP_NORESERVE	0x0400		/* don't check for reservations */
#define TARGET_MAP_POPULATE	0x10000		/* populate (prefault) pagetables */
#define TARGET_MAP_NONBLOCK	0x20000		/* do not block on IO */
#define TARGET_MAP_STACK        0x40000         /* ignored */
#define TARGET_MAP_HUGETLB      0x80000         /* create a huge page mapping */
#elif defined(TARGET_PPC)
#define TARGET_MAP_FIXED	0x10		/* Interpret addr exactly */
#define TARGET_MAP_ANONYMOUS	0x20		/* don't use a file */
#define TARGET_MAP_GROWSDOWN	0x0100		/* stack-like segment */
#define TARGET_MAP_DENYWRITE	0x0800		/* ETXTBSY */
#define TARGET_MAP_EXECUTABLE	0x1000		/* mark it as an executable */
#define TARGET_MAP_LOCKED	0x0080		/* pages are locked */
#define TARGET_MAP_NORESERVE	0x0040		/* don't check for reservations */
#define TARGET_MAP_POPULATE	0x8000		/* populate (prefault) pagetables */
#define TARGET_MAP_NONBLOCK	0x10000		/* do not block on IO */
#define TARGET_MAP_STACK        0x20000         /* ignored */
#define TARGET_MAP_HUGETLB      0x40000         /* create a huge page mapping */
#elif defined(TARGET_ALPHA)
#define TARGET_MAP_ANONYMOUS	0x10		/* don't use a file */
#define TARGET_MAP_FIXED	0x100		/* Interpret addr exactly */
#define TARGET_MAP_GROWSDOWN	0x01000		/* stack-like segment */
#define TARGET_MAP_DENYWRITE	0x02000		/* ETXTBSY */
#define TARGET_MAP_EXECUTABLE	0x04000		/* mark it as an executable */
#define TARGET_MAP_LOCKED	0x08000		/* lock the mapping */
#define TARGET_MAP_NORESERVE	0x10000		/* no check for reservations */
#define TARGET_MAP_POPULATE	0x20000		/* pop (prefault) pagetables */
#define TARGET_MAP_NONBLOCK	0x40000		/* do not block on IO */
#define TARGET_MAP_STACK        0x80000         /* ignored */
#define TARGET_MAP_HUGETLB      0x100000        /* create a huge page mapping */
#elif defined(TARGET_HPPA)
#define TARGET_MAP_ANONYMOUS	0x10		/* don't use a file */
#define TARGET_MAP_FIXED	0x04		/* Interpret addr exactly */
#define TARGET_MAP_GROWSDOWN	0x08000		/* stack-like segment */
#define TARGET_MAP_DENYWRITE	0x00800		/* ETXTBSY */
#define TARGET_MAP_EXECUTABLE	0x01000		/* mark it as an executable */
#define TARGET_MAP_LOCKED	0x02000		/* lock the mapping */
#define TARGET_MAP_NORESERVE	0x04000		/* no check for reservations */
#define TARGET_MAP_POPULATE	0x10000		/* pop (prefault) pagetables */
#define TARGET_MAP_NONBLOCK	0x20000		/* do not block on IO */
#define TARGET_MAP_STACK        0x40000         /* ignored */
#define TARGET_MAP_HUGETLB      0x80000         /* create a huge page mapping */
#elif defined(TARGET_XTENSA)
#define TARGET_MAP_FIXED	0x10		/* Interpret addr exactly */
#define TARGET_MAP_ANONYMOUS	0x0800		/* don't use a file */
#define TARGET_MAP_GROWSDOWN	0x1000		/* stack-like segment */
#define TARGET_MAP_DENYWRITE	0x2000		/* ETXTBSY */
#define TARGET_MAP_EXECUTABLE	0x4000		/* mark it as an executable */
#define TARGET_MAP_LOCKED	0x8000		/* pages are locked */
#define TARGET_MAP_NORESERVE	0x0400		/* don't check for reservations */
#define TARGET_MAP_POPULATE	0x10000		/* populate (prefault) pagetables */
#define TARGET_MAP_NONBLOCK	0x20000		/* do not block on IO */
#define TARGET_MAP_STACK	0x40000
#define TARGET_MAP_HUGETLB  0x80000         /* create a huge page mapping */
#else
#define TARGET_MAP_FIXED	0x10		/* Interpret addr exactly */
#define TARGET_MAP_ANONYMOUS	0x20		/* don't use a file */
#define TARGET_MAP_GROWSDOWN	0x0100		/* stack-like segment */
#define TARGET_MAP_DENYWRITE	0x0800		/* ETXTBSY */
#define TARGET_MAP_EXECUTABLE	0x1000		/* mark it as an executable */
#define TARGET_MAP_LOCKED	0x2000		/* pages are locked */
#define TARGET_MAP_NORESERVE	0x4000		/* don't check for reservations */
#define TARGET_MAP_POPULATE	0x8000		/* populate (prefault) pagetables */
#define TARGET_MAP_NONBLOCK	0x10000		/* do not block on IO */
#define TARGET_MAP_STACK        0x20000         /* ignored */
#define TARGET_MAP_HUGETLB      0x40000         /* create a huge page mapping */
#define TARGET_MAP_UNINITIALIZED 0x4000000	/* for anonymous mmap, memory could be uninitialized */
#endif

#if (defined(TARGET_I386) && defined(TARGET_ABI32)) \
    || (defined(TARGET_ARM) && defined(TARGET_ABI32)) \
    || defined(TARGET_CRIS)
#define TARGET_STAT_HAVE_NSEC
struct target_stat {
	unsigned short st_dev;
	unsigned short __pad1;
	abi_ulong st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned short __pad2;
	abi_ulong  st_size;
	abi_ulong  st_blksize;
	abi_ulong  st_blocks;
	abi_ulong  target_st_atime;
	abi_ulong  target_st_atime_nsec;
	abi_ulong  target_st_mtime;
	abi_ulong  target_st_mtime_nsec;
	abi_ulong  target_st_ctime;
	abi_ulong  target_st_ctime_nsec;
	abi_ulong  __unused4;
	abi_ulong  __unused5;
};

/* This matches struct stat64 in glibc2.1, hence the absolutely
 * insane amounts of padding around dev_t's.
 */
#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
	unsigned short	st_dev;
	unsigned char	__pad0[10];

#define TARGET_STAT64_HAS_BROKEN_ST_INO	1
	abi_ulong	__st_ino;

	unsigned int	st_mode;
	unsigned int	st_nlink;

	abi_ulong	st_uid;
	abi_ulong	st_gid;

	unsigned short	st_rdev;
	unsigned char	__pad3[10];

	long long	st_size;
	abi_ulong	st_blksize;

	abi_ulong	st_blocks;	/* Number 512-byte blocks allocated. */
	abi_ulong	__pad4;		/* future possible st_blocks high bits */

	abi_ulong	target_st_atime;
	abi_ulong	target_st_atime_nsec;

	abi_ulong	target_st_mtime;
	abi_ulong	target_st_mtime_nsec;

	abi_ulong	target_st_ctime;
	abi_ulong	target_st_ctime_nsec;

	unsigned long long	st_ino;
} QEMU_PACKED;

#ifdef TARGET_ARM
#define TARGET_HAS_STRUCT_STAT64
struct target_eabi_stat64 {
        unsigned long long st_dev;
        unsigned int    __pad1;
        abi_ulong    __st_ino;
        unsigned int    st_mode;
        unsigned int    st_nlink;

        abi_ulong    st_uid;
        abi_ulong    st_gid;

        unsigned long long st_rdev;
        unsigned int    __pad2[2];

        long long       st_size;
        abi_ulong    st_blksize;
        unsigned int    __pad3;
        unsigned long long st_blocks;

        abi_ulong    target_st_atime;
        abi_ulong    target_st_atime_nsec;

        abi_ulong    target_st_mtime;
        abi_ulong    target_st_mtime_nsec;

        abi_ulong    target_st_ctime;
        abi_ulong    target_st_ctime_nsec;

        unsigned long long st_ino;
} QEMU_PACKED;
#endif

#elif defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
struct target_stat {
	unsigned int	st_dev;
	abi_ulong	st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;
	unsigned int	st_uid;
	unsigned int	st_gid;
	unsigned int	st_rdev;
	abi_long	st_size;
	abi_long	target_st_atime;
	abi_long	target_st_mtime;
	abi_long	target_st_ctime;
	abi_long	st_blksize;
	abi_long	st_blocks;
	abi_ulong	__unused4[2];
};

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
	unsigned char	__pad0[6];
	unsigned short	st_dev;

	uint64_t	st_ino;
	uint64_t	st_nlink;

	unsigned int	st_mode;

	unsigned int	st_uid;
	unsigned int	st_gid;

	unsigned char	__pad2[6];
	unsigned short	st_rdev;

        int64_t		st_size;
	int64_t		st_blksize;

	unsigned char	__pad4[4];
	unsigned int	st_blocks;

	abi_ulong	target_st_atime;
	abi_ulong	target_st_atime_nsec;

	abi_ulong	target_st_mtime;
	abi_ulong	target_st_mtime_nsec;

	abi_ulong	target_st_ctime;
	abi_ulong	target_st_ctime_nsec;

	abi_ulong	__unused4[3];
};

#elif defined(TARGET_SPARC)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
	unsigned short	st_dev;
	abi_ulong	st_ino;
	unsigned short	st_mode;
	short		st_nlink;
	unsigned short	st_uid;
	unsigned short	st_gid;
	unsigned short	st_rdev;
	abi_long	st_size;
	abi_long	target_st_atime;
	abi_ulong	target_st_atime_nsec;
	abi_long	target_st_mtime;
	abi_ulong	target_st_mtime_nsec;
	abi_long	target_st_ctime;
	abi_ulong	target_st_ctime_nsec;
	abi_long	st_blksize;
	abi_long	st_blocks;
	abi_ulong	__unused1[2];
};

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
	unsigned char	__pad0[6];
	unsigned short	st_dev;

	uint64_t st_ino;

	unsigned int	st_mode;
	unsigned int	st_nlink;

	unsigned int	st_uid;
	unsigned int	st_gid;

	unsigned char	__pad2[6];
	unsigned short	st_rdev;

	unsigned char	__pad3[8];

        int64_t	st_size;
	unsigned int	st_blksize;

	unsigned char	__pad4[8];
	unsigned int	st_blocks;

	unsigned int	target_st_atime;
	unsigned int	target_st_atime_nsec;

	unsigned int	target_st_mtime;
	unsigned int	target_st_mtime_nsec;

	unsigned int	target_st_ctime;
	unsigned int	target_st_ctime_nsec;

	unsigned int	__unused1;
	unsigned int	__unused2;
};

#elif defined(TARGET_PPC)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
	abi_ulong st_dev;
	abi_ulong st_ino;
#if defined(TARGET_PPC64)
	abi_ulong st_nlink;
	unsigned int st_mode;
#else
	unsigned int st_mode;
	unsigned short st_nlink;
#endif
	unsigned int st_uid;
	unsigned int st_gid;
	abi_ulong  st_rdev;
	abi_ulong  st_size;
	abi_ulong  st_blksize;
	abi_ulong  st_blocks;
	abi_ulong  target_st_atime;
	abi_ulong  target_st_atime_nsec;
	abi_ulong  target_st_mtime;
	abi_ulong  target_st_mtime_nsec;
	abi_ulong  target_st_ctime;
	abi_ulong  target_st_ctime_nsec;
	abi_ulong  __unused4;
	abi_ulong  __unused5;
#if defined(TARGET_PPC64)
	abi_ulong  __unused6;
#endif
};

#if !defined(TARGET_PPC64)
#define TARGET_HAS_STRUCT_STAT64
struct QEMU_PACKED target_stat64 {
	unsigned long long st_dev;
        unsigned long long st_ino;
	unsigned int st_mode;
	unsigned int st_nlink;
	unsigned int st_uid;
	unsigned int st_gid;
	unsigned long long st_rdev;
	unsigned long long __pad0;
	long long      st_size;
	int	       st_blksize;
	unsigned int   __pad1;
	long long      st_blocks;	/* Number 512-byte blocks allocated. */
	int	       target_st_atime;
        unsigned int   target_st_atime_nsec;
	int	       target_st_mtime;
        unsigned int   target_st_mtime_nsec;
	int            target_st_ctime;
        unsigned int   target_st_ctime_nsec;
        unsigned int   __unused4;
        unsigned int   __unused5;
};
#endif

#elif defined(TARGET_MICROBLAZE)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
	abi_ulong st_dev;
	abi_ulong st_ino;
	unsigned int st_mode;
	unsigned short st_nlink;
	unsigned int st_uid;
	unsigned int st_gid;
	abi_ulong  st_rdev;
	abi_ulong  st_size;
	abi_ulong  st_blksize;
	abi_ulong  st_blocks;
	abi_ulong  target_st_atime;
	abi_ulong  target_st_atime_nsec;
	abi_ulong  target_st_mtime;
	abi_ulong  target_st_mtime_nsec;
	abi_ulong  target_st_ctime;
	abi_ulong  target_st_ctime_nsec;
	abi_ulong  __unused4;
	abi_ulong  __unused5;
};

/* FIXME: Microblaze no-mmu user-space has a difference stat64 layout...  */
#define TARGET_HAS_STRUCT_STAT64
struct QEMU_PACKED target_stat64 {
	uint64_t st_dev;
#define TARGET_STAT64_HAS_BROKEN_ST_INO 1
	uint32_t pad0;
	uint32_t __st_ino;

	uint32_t st_mode;
	uint32_t st_nlink;
	uint32_t st_uid;
	uint32_t st_gid;
	uint64_t st_rdev;
	uint64_t __pad1;

	int64_t  st_size;
	int32_t  st_blksize;
	uint32_t __pad2;
	int64_t st_blocks;	/* Number 512-byte blocks allocated. */

	int	       target_st_atime;
	unsigned int   target_st_atime_nsec;
	int	       target_st_mtime;
	unsigned int   target_st_mtime_nsec;
	int            target_st_ctime;
	unsigned int   target_st_ctime_nsec;
	uint64_t st_ino;
};

#elif defined(TARGET_M68K)

struct target_stat {
	unsigned short st_dev;
	unsigned short __pad1;
	abi_ulong st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned short __pad2;
	abi_ulong  st_size;
	abi_ulong  st_blksize;
	abi_ulong  st_blocks;
	abi_ulong  target_st_atime;
	abi_ulong  __unused1;
	abi_ulong  target_st_mtime;
	abi_ulong  __unused2;
	abi_ulong  target_st_ctime;
	abi_ulong  __unused3;
	abi_ulong  __unused4;
	abi_ulong  __unused5;
};

/* This matches struct stat64 in glibc2.1, hence the absolutely
 * insane amounts of padding around dev_t's.
 */
#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
	unsigned long long	st_dev;
	unsigned char	__pad1[2];

#define TARGET_STAT64_HAS_BROKEN_ST_INO	1
	abi_ulong	__st_ino;

	unsigned int	st_mode;
	unsigned int	st_nlink;

	abi_ulong	st_uid;
	abi_ulong	st_gid;

	unsigned long long	st_rdev;
	unsigned char	__pad3[2];

	long long	st_size;
	abi_ulong	st_blksize;

	abi_ulong	__pad4;		/* future possible st_blocks high bits */
	abi_ulong	st_blocks;	/* Number 512-byte blocks allocated. */

	abi_ulong	target_st_atime;
	abi_ulong	target_st_atime_nsec;

	abi_ulong	target_st_mtime;
	abi_ulong	target_st_mtime_nsec;

	abi_ulong	target_st_ctime;
	abi_ulong	target_st_ctime_nsec;

	unsigned long long	st_ino;
} QEMU_PACKED;

#elif defined(TARGET_ABI_MIPSN64)

#define TARGET_STAT_HAVE_NSEC
/* The memory layout is the same as of struct stat64 of the 32-bit kernel.  */
struct target_stat {
	unsigned int		st_dev;
	unsigned int		st_pad0[3]; /* Reserved for st_dev expansion */

	abi_ulong		st_ino;

	unsigned int		st_mode;
	unsigned int		st_nlink;

	int			st_uid;
	int			st_gid;

	unsigned int		st_rdev;
	unsigned int		st_pad1[3]; /* Reserved for st_rdev expansion */

	abi_ulong		st_size;

	/*
	 * Actually this should be timestruc_t st_atime, st_mtime and st_ctime
	 * but we don't have it under Linux.
	 */
	unsigned int		target_st_atime;
	unsigned int		target_st_atime_nsec;

	unsigned int		target_st_mtime;
	unsigned int		target_st_mtime_nsec;

	unsigned int		target_st_ctime;
	unsigned int		target_st_ctime_nsec;

	unsigned int		st_blksize;
	unsigned int		st_pad2;

	abi_ulong		st_blocks;
};

#elif defined(TARGET_ABI_MIPSN32)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
        abi_ulong    st_dev;
        abi_ulong    st_pad0[3]; /* Reserved for st_dev expansion */
        uint64_t     st_ino;
        unsigned int st_mode;
        unsigned int st_nlink;
        int          st_uid;
        int          st_gid;
        abi_ulong    st_rdev;
        abi_ulong    st_pad1[3]; /* Reserved for st_rdev expansion */
        int64_t      st_size;
        abi_long     target_st_atime;
        abi_ulong    target_st_atime_nsec; /* Reserved for st_atime expansion */
        abi_long     target_st_mtime;
        abi_ulong    target_st_mtime_nsec; /* Reserved for st_mtime expansion */
        abi_long     target_st_ctime;
        abi_ulong    target_st_ctime_nsec; /* Reserved for st_ctime expansion */
        abi_ulong    st_blksize;
        abi_ulong    st_pad2;
        int64_t      st_blocks;
};

#elif defined(TARGET_ABI_MIPSO32)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
	unsigned	st_dev;
	abi_long	st_pad1[3];		/* Reserved for network id */
	abi_ulong	st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;
	int		st_uid;
	int		st_gid;
	unsigned 	st_rdev;
	abi_long	st_pad2[2];
	abi_long	st_size;
	abi_long	st_pad3;
	/*
	 * Actually this should be timestruc_t st_atime, st_mtime and st_ctime
	 * but we don't have it under Linux.
	 */
	abi_long		target_st_atime;
	abi_long		target_st_atime_nsec;
	abi_long		target_st_mtime;
	abi_long		target_st_mtime_nsec;
	abi_long		target_st_ctime;
	abi_long		target_st_ctime_nsec;
	abi_long		st_blksize;
	abi_long		st_blocks;
	abi_long		st_pad4[14];
};

/*
 * This matches struct stat64 in glibc2.1, hence the absolutely insane
 * amounts of padding around dev_t's.  The memory layout is the same as of
 * struct stat of the 64-bit kernel.
 */

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
	abi_ulong	st_dev;
	abi_ulong	st_pad0[3];	/* Reserved for st_dev expansion  */

	uint64_t	st_ino;

        unsigned int	st_mode;
        unsigned int	st_nlink;

	int		st_uid;
	int		st_gid;

	abi_ulong	st_rdev;
	abi_ulong	st_pad1[3];	/* Reserved for st_rdev expansion  */

	int64_t 	st_size;

	/*
	 * Actually this should be timestruc_t st_atime, st_mtime and st_ctime
	 * but we don't have it under Linux.
	 */
	abi_long	target_st_atime;
	abi_ulong	target_st_atime_nsec;	/* Reserved for st_atime expansion  */

	abi_long	target_st_mtime;
	abi_ulong	target_st_mtime_nsec;	/* Reserved for st_mtime expansion  */

	abi_long	target_st_ctime;
	abi_ulong	target_st_ctime_nsec;	/* Reserved for st_ctime expansion  */

	abi_ulong	st_blksize;
	abi_ulong	st_pad2;

	int64_t  	st_blocks;
};

#elif defined(TARGET_ALPHA)

struct target_stat {
       unsigned int    st_dev;
       unsigned int    st_ino;
       unsigned int    st_mode;
       unsigned int    st_nlink;
       unsigned int    st_uid;
       unsigned int    st_gid;
       unsigned int    st_rdev;
       abi_long     st_size;
       abi_ulong    target_st_atime;
       abi_ulong    target_st_mtime;
       abi_ulong    target_st_ctime;
       unsigned int    st_blksize;
       unsigned int    st_blocks;
       unsigned int    st_flags;
       unsigned int    st_gen;
};

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
       abi_ulong    st_dev;
       abi_ulong    st_ino;
       abi_ulong    st_rdev;
       abi_long     st_size;
       abi_ulong    st_blocks;

       unsigned int    st_mode;
       unsigned int    st_uid;
       unsigned int    st_gid;
       unsigned int    st_blksize;
       unsigned int    st_nlink;
       unsigned int    __pad0;

       abi_ulong    target_st_atime;
       abi_ulong    target_st_atime_nsec;
       abi_ulong    target_st_mtime;
       abi_ulong    target_st_mtime_nsec;
       abi_ulong    target_st_ctime;
       abi_ulong    target_st_ctime_nsec;
       abi_long     __unused[3];
};

#elif defined(TARGET_SH4)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
	abi_ulong  st_dev;
	abi_ulong  st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	abi_ulong  st_rdev;
	abi_ulong  st_size;
	abi_ulong  st_blksize;
	abi_ulong  st_blocks;
	abi_ulong  target_st_atime;
	abi_ulong  target_st_atime_nsec;
	abi_ulong  target_st_mtime;
	abi_ulong  target_st_mtime_nsec;
	abi_ulong  target_st_ctime;
	abi_ulong  target_st_ctime_nsec;
	abi_ulong  __unused4;
	abi_ulong  __unused5;
};

/* This matches struct stat64 in glibc2.1, hence the absolutely
 * insane amounts of padding around dev_t's.
 */
#define TARGET_HAS_STRUCT_STAT64
struct QEMU_PACKED target_stat64 {
	unsigned long long	st_dev;
	unsigned char	__pad0[4];

#define TARGET_STAT64_HAS_BROKEN_ST_INO	1
	abi_ulong	__st_ino;

	unsigned int	st_mode;
	unsigned int	st_nlink;

	abi_ulong	st_uid;
	abi_ulong	st_gid;

	unsigned long long	st_rdev;
	unsigned char	__pad3[4];

	long long	st_size;
	abi_ulong	st_blksize;

	unsigned long long	st_blocks;	/* Number 512-byte blocks allocated. */

	abi_ulong	target_st_atime;
	abi_ulong	target_st_atime_nsec;

	abi_ulong	target_st_mtime;
	abi_ulong	target_st_mtime_nsec;

	abi_ulong	target_st_ctime;
	abi_ulong	target_st_ctime_nsec;

	unsigned long long	st_ino;
};

#elif defined(TARGET_I386) && !defined(TARGET_ABI32)
#define TARGET_STAT_HAVE_NSEC
struct target_stat {
	abi_ulong	st_dev;
	abi_ulong	st_ino;
	abi_ulong	st_nlink;

	unsigned int	st_mode;
	unsigned int	st_uid;
	unsigned int	st_gid;
	unsigned int	__pad0;
	abi_ulong	st_rdev;
	abi_long	st_size;
	abi_long	st_blksize;
	abi_long	st_blocks;	/* Number 512-byte blocks allocated. */

	abi_ulong	target_st_atime;
	abi_ulong 	target_st_atime_nsec; 
	abi_ulong	target_st_mtime;
	abi_ulong	target_st_mtime_nsec;
	abi_ulong	target_st_ctime;
	abi_ulong       target_st_ctime_nsec;

	abi_long	__unused[3];
};
#elif defined(TARGET_S390X)
struct target_stat {
    abi_ulong  st_dev;
    abi_ulong  st_ino;
    abi_ulong  st_nlink;
    unsigned int   st_mode;
    unsigned int   st_uid;
    unsigned int   st_gid;
    unsigned int   __pad1;
    abi_ulong  st_rdev;
    abi_ulong  st_size;
    abi_ulong  target_st_atime;
    abi_ulong  target_st_atime_nsec;
    abi_ulong  target_st_mtime;
    abi_ulong  target_st_mtime_nsec;
    abi_ulong  target_st_ctime;
    abi_ulong  target_st_ctime_nsec;
    abi_ulong  st_blksize;
    abi_long       st_blocks;
    abi_ulong  __unused[3];
};
#elif defined(TARGET_AARCH64)
#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_ulong  st_dev;
    abi_ulong  st_ino;
    unsigned int st_mode;
    unsigned int st_nlink;
    unsigned int   st_uid;
    unsigned int   st_gid;
    abi_ulong  st_rdev;
    abi_ulong  _pad1;
    abi_long  st_size;
    int        st_blksize;
    int        __pad2;
    abi_long   st_blocks;
    abi_long  target_st_atime;
    abi_ulong  target_st_atime_nsec;
    abi_long  target_st_mtime;
    abi_ulong  target_st_mtime_nsec;
    abi_long  target_st_ctime;
    abi_ulong  target_st_ctime_nsec;
    unsigned int __unused[2];
};
#elif defined(TARGET_XTENSA)
#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_ulong       st_dev;
    abi_ulong       st_ino;
    unsigned int    st_mode;
    unsigned int    st_nlink;
    unsigned int    st_uid;
    unsigned int    st_gid;
    abi_ulong       st_rdev;
    abi_long        st_size;
    abi_ulong       st_blksize;
    abi_ulong       st_blocks;
    abi_ulong       target_st_atime;
    abi_ulong       target_st_atime_nsec;
    abi_ulong       target_st_mtime;
    abi_ulong       target_st_mtime_nsec;
    abi_ulong       target_st_ctime;
    abi_ulong       target_st_ctime_nsec;
    abi_ulong       __unused4;
    abi_ulong       __unused5;
};

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64  {
    uint64_t st_dev;            /* Device */
    uint64_t st_ino;            /* File serial number */
    unsigned int  st_mode;      /* File mode. */
    unsigned int  st_nlink;     /* Link count. */
    unsigned int  st_uid;       /* User ID of the file's owner. */
    unsigned int  st_gid;       /* Group ID of the file's group. */
    uint64_t st_rdev;           /* Device number, if device. */
    int64_t st_size;            /* Size of file, in bytes. */
    abi_ulong st_blksize;       /* Optimal block size for I/O. */
    abi_ulong __unused2;
    uint64_t st_blocks;         /* Number 512-byte blocks allocated. */
    abi_ulong target_st_atime;  /* Time of last access. */
    abi_ulong target_st_atime_nsec;
    abi_ulong target_st_mtime;  /* Time of last modification. */
    abi_ulong target_st_mtime_nsec;
    abi_ulong target_st_ctime;  /* Time of last status change. */
    abi_ulong target_st_ctime_nsec;
    abi_ulong __unused4;
    abi_ulong __unused5;
};

#elif defined(TARGET_OPENRISC) || defined(TARGET_NIOS2) \
        || defined(TARGET_RISCV) || defined(TARGET_HEXAGON)

/* These are the asm-generic versions of the stat and stat64 structures */

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_ulong st_dev;
    abi_ulong st_ino;
    unsigned int st_mode;
    unsigned int st_nlink;
    unsigned int st_uid;
    unsigned int st_gid;
    abi_ulong st_rdev;
    abi_ulong __pad1;
    abi_long st_size;
    int st_blksize;
    int __pad2;
    abi_long st_blocks;
    abi_long target_st_atime;
    abi_ulong target_st_atime_nsec;
    abi_long target_st_mtime;
    abi_ulong target_st_mtime_nsec;
    abi_long target_st_ctime;
    abi_ulong target_st_ctime_nsec;
    unsigned int __unused4;
    unsigned int __unused5;
};

#if !defined(TARGET_RISCV64)
#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
    uint64_t st_dev;
    uint64_t st_ino;
    unsigned int st_mode;
    unsigned int st_nlink;
    unsigned int st_uid;
    unsigned int st_gid;
    uint64_t st_rdev;
    uint64_t __pad1;
    int64_t st_size;
    int st_blksize;
    int __pad2;
    int64_t st_blocks;
    int target_st_atime;
    unsigned int target_st_atime_nsec;
    int target_st_mtime;
    unsigned int target_st_mtime_nsec;
    int target_st_ctime;
    unsigned int target_st_ctime_nsec;
    unsigned int __unused4;
    unsigned int __unused5;
};
#endif

#elif defined(TARGET_HPPA)

#define TARGET_STAT_HAVE_NSEC
struct target_stat {
    abi_uint   st_dev;
    abi_uint   st_ino;
    abi_ushort st_mode;
    abi_ushort st_nlink;
    abi_ushort _res1;
    abi_ushort _res2;
    abi_uint   st_rdev;
    abi_int    st_size;
    abi_int    target_st_atime;
    abi_uint   target_st_atime_nsec;
    abi_int    target_st_mtime;
    abi_uint   target_st_mtime_nsec;
    abi_int    target_st_ctime;
    abi_uint   target_st_ctime_nsec;
    abi_int    st_blksize;
    abi_int    st_blocks;
    abi_uint   _unused1;
    abi_uint   _unused2;
    abi_uint   _unused3;
    abi_uint   _unused4;
    abi_ushort _unused5;
    abi_short  st_fstype;
    abi_uint   st_realdev;
    abi_ushort st_basemode;
    abi_ushort _unused6;
    abi_uint   st_uid;
    abi_uint   st_gid;
    abi_uint   _unused7[3];
};

#define TARGET_HAS_STRUCT_STAT64
struct target_stat64 {
    uint64_t   st_dev;
    abi_uint   _pad1;
    abi_uint   _res1;
    abi_uint   st_mode;
    abi_uint   st_nlink;
    abi_uint   st_uid;
    abi_uint   st_gid;
    uint64_t   st_rdev;
    abi_uint   _pad2;
    int64_t    st_size;
    abi_int    st_blksize;
    int64_t    st_blocks;
    abi_int    target_st_atime;
    abi_uint   target_st_atime_nsec;
    abi_int    target_st_mtime;
    abi_uint   target_st_mtime_nsec;
    abi_int    target_st_ctime;
    abi_uint   target_st_ctime_nsec;
    uint64_t   st_ino;
};

#else
#error unsupported CPU
#endif

typedef struct {
        int     val[2];
} target_fsid_t;

#ifdef TARGET_MIPS
#ifdef TARGET_ABI_MIPSN32
struct target_statfs {
	int32_t			f_type;
	int32_t			f_bsize;
	int32_t			f_frsize;	/* Fragment size - unsupported */
	int32_t			f_blocks;
	int32_t			f_bfree;
	int32_t			f_files;
	int32_t			f_ffree;
	int32_t			f_bavail;

	/* Linux specials */
	target_fsid_t		f_fsid;
	int32_t			f_namelen;
	int32_t			f_flags;
	int32_t			f_spare[5];
};
#else
struct target_statfs {
	abi_long		f_type;
	abi_long		f_bsize;
	abi_long		f_frsize;	/* Fragment size - unsupported */
	abi_long		f_blocks;
	abi_long		f_bfree;
	abi_long		f_files;
	abi_long		f_ffree;
	abi_long		f_bavail;

	/* Linux specials */
	target_fsid_t		f_fsid;
	abi_long		f_namelen;
	abi_long		f_flags;
	abi_long		f_spare[5];
};
#endif

struct target_statfs64 {
	uint32_t	f_type;
	uint32_t	f_bsize;
	uint32_t	f_frsize;	/* Fragment size - unsupported */
	uint32_t	__pad;
	uint64_t	f_blocks;
	uint64_t	f_bfree;
	uint64_t	f_files;
	uint64_t	f_ffree;
	uint64_t	f_bavail;
	target_fsid_t	f_fsid;
	uint32_t	f_namelen;
	uint32_t	f_flags;
	uint32_t	f_spare[5];
};
#elif (defined(TARGET_PPC64) || defined(TARGET_X86_64) || \
       defined(TARGET_SPARC64) || defined(TARGET_AARCH64) || \
       defined(TARGET_RISCV)) && !defined(TARGET_ABI32)
struct target_statfs {
	abi_long f_type;
	abi_long f_bsize;
	abi_long f_blocks;
	abi_long f_bfree;
	abi_long f_bavail;
	abi_long f_files;
	abi_long f_ffree;
	target_fsid_t f_fsid;
	abi_long f_namelen;
	abi_long f_frsize;
	abi_long f_flags;
	abi_long f_spare[4];
};

struct target_statfs64 {
	abi_long f_type;
	abi_long f_bsize;
	abi_long f_blocks;
	abi_long f_bfree;
	abi_long f_bavail;
	abi_long f_files;
	abi_long f_ffree;
	target_fsid_t f_fsid;
	abi_long f_namelen;
	abi_long f_frsize;
	abi_long f_flags;
	abi_long f_spare[4];
};
#elif defined(TARGET_S390X)
struct target_statfs {
    int32_t  f_type;
    int32_t  f_bsize;
    abi_long f_blocks;
    abi_long f_bfree;
    abi_long f_bavail;
    abi_long f_files;
    abi_long f_ffree;
    kernel_fsid_t f_fsid;
    int32_t  f_namelen;
    int32_t  f_frsize;
    int32_t  f_flags;
    int32_t  f_spare[4];

};

struct target_statfs64 {
    int32_t  f_type;
    int32_t  f_bsize;
    abi_long f_blocks;
    abi_long f_bfree;
    abi_long f_bavail;
    abi_long f_files;
    abi_long f_ffree;
    kernel_fsid_t f_fsid;
    int32_t  f_namelen;
    int32_t  f_frsize;
    int32_t  f_flags;
    int32_t  f_spare[4];
};
#else
struct target_statfs {
	uint32_t f_type;
	uint32_t f_bsize;
	uint32_t f_blocks;
	uint32_t f_bfree;
	uint32_t f_bavail;
	uint32_t f_files;
	uint32_t f_ffree;
	target_fsid_t f_fsid;
	uint32_t f_namelen;
	uint32_t f_frsize;
	uint32_t f_flags;
	uint32_t f_spare[4];
};

struct target_statfs64 {
	uint32_t f_type;
	uint32_t f_bsize;
	uint64_t f_blocks;
	uint64_t f_bfree;
	uint64_t f_bavail;
	uint64_t f_files;
	uint64_t f_ffree;
	target_fsid_t f_fsid;
        uint32_t f_namelen;
	uint32_t f_frsize;
	uint32_t f_flags;
	uint32_t f_spare[4];
};
#endif

#define TARGET_F_LINUX_SPECIFIC_BASE 1024
#define TARGET_F_SETLEASE            (TARGET_F_LINUX_SPECIFIC_BASE + 0)
#define TARGET_F_GETLEASE            (TARGET_F_LINUX_SPECIFIC_BASE + 1)
#define TARGET_F_DUPFD_CLOEXEC       (TARGET_F_LINUX_SPECIFIC_BASE + 6)
#define TARGET_F_NOTIFY              (TARGET_F_LINUX_SPECIFIC_BASE + 2)
#define TARGET_F_SETPIPE_SZ          (TARGET_F_LINUX_SPECIFIC_BASE + 7)
#define TARGET_F_GETPIPE_SZ          (TARGET_F_LINUX_SPECIFIC_BASE + 8)
#define TARGET_F_ADD_SEALS           (TARGET_F_LINUX_SPECIFIC_BASE + 9)
#define TARGET_F_GET_SEALS           (TARGET_F_LINUX_SPECIFIC_BASE + 10)

#include "target_fcntl.h"

/* soundcard defines */
/* XXX: convert them all to arch independent entries */
#define TARGET_SNDCTL_COPR_HALT           TARGET_IOWR('C',  7, int);
#define TARGET_SNDCTL_COPR_LOAD           0xcfb04301
#define TARGET_SNDCTL_COPR_RCODE          0xc0144303
#define TARGET_SNDCTL_COPR_RCVMSG         0x8fa44309
#define TARGET_SNDCTL_COPR_RDATA          0xc0144302
#define TARGET_SNDCTL_COPR_RESET          0x00004300
#define TARGET_SNDCTL_COPR_RUN            0xc0144306
#define TARGET_SNDCTL_COPR_SENDMSG        0xcfa44308
#define TARGET_SNDCTL_COPR_WCODE          0x40144305
#define TARGET_SNDCTL_COPR_WDATA          0x40144304
#define TARGET_SNDCTL_DSP_RESET           TARGET_IO('P', 0)
#define TARGET_SNDCTL_DSP_SYNC            TARGET_IO('P', 1)
#define TARGET_SNDCTL_DSP_SPEED           TARGET_IOWR('P', 2, int)
#define TARGET_SNDCTL_DSP_STEREO          TARGET_IOWR('P', 3, int)
#define TARGET_SNDCTL_DSP_GETBLKSIZE      TARGET_IOWR('P', 4, int)
#define TARGET_SNDCTL_DSP_SETFMT          TARGET_IOWR('P', 5, int)
#define TARGET_SNDCTL_DSP_CHANNELS        TARGET_IOWR('P', 6, int)
#define TARGET_SOUND_PCM_WRITE_FILTER     TARGET_IOWR('P', 7, int)
#define TARGET_SNDCTL_DSP_POST            TARGET_IO('P', 8)
#define TARGET_SNDCTL_DSP_SUBDIVIDE       TARGET_IOWR('P', 9, int)
#define TARGET_SNDCTL_DSP_SETFRAGMENT     TARGET_IOWR('P',10, int)
#define TARGET_SNDCTL_DSP_GETFMTS         TARGET_IOR('P', 11, int)
#define TARGET_SNDCTL_DSP_GETOSPACE       TARGET_IORU('P',12)
#define TARGET_SNDCTL_DSP_GETISPACE       TARGET_IORU('P',13)
#define TARGET_SNDCTL_DSP_GETCAPS         TARGET_IOR('P', 15, int)
#define TARGET_SNDCTL_DSP_GETTRIGGER      TARGET_IOR('P',16, int)
#define TARGET_SNDCTL_DSP_GETIPTR         TARGET_IORU('P',17)
#define TARGET_SNDCTL_DSP_GETOPTR         TARGET_IORU('P',18)
#define TARGET_SNDCTL_DSP_MAPINBUF        TARGET_IORU('P', 19)
#define TARGET_SNDCTL_DSP_MAPOUTBUF       TARGET_IORU('P', 20)
#define TARGET_SNDCTL_DSP_NONBLOCK        0x0000500e
#define TARGET_SNDCTL_DSP_SAMPLESIZE      0xc0045005
#define TARGET_SNDCTL_DSP_SETDUPLEX       0x00005016
#define TARGET_SNDCTL_DSP_SETSYNCRO       0x00005015
#define TARGET_SNDCTL_DSP_SETTRIGGER      0x40045010
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
#define TARGET_SOUND_PCM_READ_RATE        0x80045002
#define TARGET_SOUND_PCM_READ_CHANNELS    0x80045006
#define TARGET_SOUND_PCM_READ_BITS        0x80045005
#define TARGET_SOUND_PCM_READ_FILTER      0x80045007
#define TARGET_SOUND_MIXER_INFO           TARGET_IOR ('M', 101, mixer_info)
#define TARGET_SOUND_MIXER_ACCESS         0xc0804d66
#define TARGET_SOUND_MIXER_PRIVATE1       TARGET_IOWR('M', 111, int)
#define TARGET_SOUND_MIXER_PRIVATE2       TARGET_IOWR('M', 112, int)
#define TARGET_SOUND_MIXER_PRIVATE3       TARGET_IOWR('M', 113, int)
#define TARGET_SOUND_MIXER_PRIVATE4       TARGET_IOWR('M', 114, int)
#define TARGET_SOUND_MIXER_PRIVATE5       TARGET_IOWR('M', 115, int)

#define TARGET_MIXER_READ(dev)	TARGET_IOR('M', dev, int)

#define TARGET_SOUND_MIXER_READ_VOLUME		TARGET_MIXER_READ(SOUND_MIXER_VOLUME)
#define TARGET_SOUND_MIXER_READ_BASS		TARGET_MIXER_READ(SOUND_MIXER_BASS)
#define TARGET_SOUND_MIXER_READ_TREBLE		TARGET_MIXER_READ(SOUND_MIXER_TREBLE)
#define TARGET_SOUND_MIXER_READ_SYNTH		TARGET_MIXER_READ(SOUND_MIXER_SYNTH)
#define TARGET_SOUND_MIXER_READ_PCM		TARGET_MIXER_READ(SOUND_MIXER_PCM)
#define TARGET_SOUND_MIXER_READ_SPEAKER	        TARGET_MIXER_READ(SOUND_MIXER_SPEAKER)
#define TARGET_SOUND_MIXER_READ_LINE		TARGET_MIXER_READ(SOUND_MIXER_LINE)
#define TARGET_SOUND_MIXER_READ_MIC		TARGET_MIXER_READ(SOUND_MIXER_MIC)
#define TARGET_SOUND_MIXER_READ_CD		TARGET_MIXER_READ(SOUND_MIXER_CD)
#define TARGET_SOUND_MIXER_READ_IMIX		TARGET_MIXER_READ(SOUND_MIXER_IMIX)
#define TARGET_SOUND_MIXER_READ_ALTPCM		TARGET_MIXER_READ(SOUND_MIXER_ALTPCM)
#define TARGET_SOUND_MIXER_READ_RECLEV		TARGET_MIXER_READ(SOUND_MIXER_RECLEV)
#define TARGET_SOUND_MIXER_READ_IGAIN		TARGET_MIXER_READ(SOUND_MIXER_IGAIN)
#define TARGET_SOUND_MIXER_READ_OGAIN		TARGET_MIXER_READ(SOUND_MIXER_OGAIN)
#define TARGET_SOUND_MIXER_READ_LINE1		TARGET_MIXER_READ(SOUND_MIXER_LINE1)
#define TARGET_SOUND_MIXER_READ_LINE2		TARGET_MIXER_READ(SOUND_MIXER_LINE2)
#define TARGET_SOUND_MIXER_READ_LINE3		TARGET_MIXER_READ(SOUND_MIXER_LINE3)

/* Obsolete macros */
#define TARGET_SOUND_MIXER_READ_MUTE		TARGET_MIXER_READ(SOUND_MIXER_MUTE)
#define TARGET_SOUND_MIXER_READ_ENHANCE	        TARGET_MIXER_READ(SOUND_MIXER_ENHANCE)
#define TARGET_SOUND_MIXER_READ_LOUD		TARGET_MIXER_READ(SOUND_MIXER_LOUD)

#define TARGET_SOUND_MIXER_READ_RECSRC		TARGET_MIXER_READ(SOUND_MIXER_RECSRC)
#define TARGET_SOUND_MIXER_READ_DEVMASK	        TARGET_MIXER_READ(SOUND_MIXER_DEVMASK)
#define TARGET_SOUND_MIXER_READ_RECMASK	        TARGET_MIXER_READ(SOUND_MIXER_RECMASK)
#define TARGET_SOUND_MIXER_READ_STEREODEVS	TARGET_MIXER_READ(SOUND_MIXER_STEREODEVS)
#define TARGET_SOUND_MIXER_READ_CAPS		TARGET_MIXER_READ(SOUND_MIXER_CAPS)

#define TARGET_MIXER_WRITE(dev)		TARGET_IOWR('M', dev, int)

#define TARGET_SOUND_MIXER_WRITE_VOLUME	TARGET_MIXER_WRITE(SOUND_MIXER_VOLUME)
#define TARGET_SOUND_MIXER_WRITE_BASS		TARGET_MIXER_WRITE(SOUND_MIXER_BASS)
#define TARGET_SOUND_MIXER_WRITE_TREBLE	TARGET_MIXER_WRITE(SOUND_MIXER_TREBLE)
#define TARGET_SOUND_MIXER_WRITE_SYNTH		TARGET_MIXER_WRITE(SOUND_MIXER_SYNTH)
#define TARGET_SOUND_MIXER_WRITE_PCM		TARGET_MIXER_WRITE(SOUND_MIXER_PCM)
#define TARGET_SOUND_MIXER_WRITE_SPEAKER	TARGET_MIXER_WRITE(SOUND_MIXER_SPEAKER)
#define TARGET_SOUND_MIXER_WRITE_LINE		TARGET_MIXER_WRITE(SOUND_MIXER_LINE)
#define TARGET_SOUND_MIXER_WRITE_MIC		TARGET_MIXER_WRITE(SOUND_MIXER_MIC)
#define TARGET_SOUND_MIXER_WRITE_CD		TARGET_MIXER_WRITE(SOUND_MIXER_CD)
#define TARGET_SOUND_MIXER_WRITE_IMIX		TARGET_MIXER_WRITE(SOUND_MIXER_IMIX)
#define TARGET_SOUND_MIXER_WRITE_ALTPCM	TARGET_MIXER_WRITE(SOUND_MIXER_ALTPCM)
#define TARGET_SOUND_MIXER_WRITE_RECLEV	TARGET_MIXER_WRITE(SOUND_MIXER_RECLEV)
#define TARGET_SOUND_MIXER_WRITE_IGAIN		TARGET_MIXER_WRITE(SOUND_MIXER_IGAIN)
#define TARGET_SOUND_MIXER_WRITE_OGAIN		TARGET_MIXER_WRITE(SOUND_MIXER_OGAIN)
#define TARGET_SOUND_MIXER_WRITE_LINE1		TARGET_MIXER_WRITE(SOUND_MIXER_LINE1)
#define TARGET_SOUND_MIXER_WRITE_LINE2		TARGET_MIXER_WRITE(SOUND_MIXER_LINE2)
#define TARGET_SOUND_MIXER_WRITE_LINE3		TARGET_MIXER_WRITE(SOUND_MIXER_LINE3)

/* Obsolete macros */
#define TARGET_SOUND_MIXER_WRITE_MUTE		TARGET_MIXER_WRITE(SOUND_MIXER_MUTE)
#define TARGET_SOUND_MIXER_WRITE_ENHANCE	TARGET_MIXER_WRITE(SOUND_MIXER_ENHANCE)
#define TARGET_SOUND_MIXER_WRITE_LOUD		TARGET_MIXER_WRITE(SOUND_MIXER_LOUD)

#define TARGET_SOUND_MIXER_WRITE_RECSRC	TARGET_MIXER_WRITE(SOUND_MIXER_RECSRC)

struct target_snd_timer_id {
    int dev_class;
    int dev_sclass;
    int card;
    int device;
    int subdevice;
};

struct target_snd_timer_ginfo {
    struct target_snd_timer_id tid;
    unsigned int flags;
    int card;
    unsigned char id[64];
    unsigned char name[80];
    abi_ulong reserved0;
    abi_ulong resolution;
    abi_ulong resolution_min;
    abi_ulong resolution_max;
    unsigned int clients;
    unsigned char reserved[32];
};

struct target_snd_timer_gparams {
    struct target_snd_timer_id tid;
    abi_ulong period_num;
    abi_ulong period_den;
    unsigned char reserved[32];
};

struct target_snd_timer_gstatus {
    struct target_snd_timer_id tid;
    abi_ulong resolution;
    abi_ulong resolution_num;
    abi_ulong resolution_den;
    unsigned char reserved[32];
};

struct target_snd_timer_select {
    struct target_snd_timer_id id;
    unsigned char reserved[32];
};

struct target_snd_timer_info {
    unsigned int flags;
    int card;
    unsigned char id[64];
    unsigned char name[80];
    abi_ulong reserved0;
    abi_ulong resolution;
    unsigned char reserved[64];
};

struct target_snd_timer_status {
    struct target_timespec tstamp;
    unsigned int resolution;
    unsigned int lost;
    unsigned int overrun;
    unsigned int queue;
    unsigned char reserved[64];
};

/* alsa timer ioctls */
#define TARGET_SNDRV_TIMER_IOCTL_PVERSION     TARGET_IOR('T', 0x00, int)
#define TARGET_SNDRV_TIMER_IOCTL_NEXT_DEVICE  TARGET_IOWR('T', 0x01,           \
                                                struct snd_timer_id)
#define TARGET_SNDRV_TIMER_IOCTL_GINFO        TARGET_IOWR('T', 0x03,           \
                                                struct target_snd_timer_ginfo)
#define TARGET_SNDRV_TIMER_IOCTL_GPARAMS      TARGET_IOW('T', 0x04,            \
                                                struct target_snd_timer_gparams)
#define TARGET_SNDRV_TIMER_IOCTL_GSTATUS      TARGET_IOWR('T', 0x05,           \
                                                struct target_snd_timer_gstatus)
#define TARGET_SNDRV_TIMER_IOCTL_SELECT       TARGET_IOW('T', 0x10,            \
                                                struct target_snd_timer_select)
#define TARGET_SNDRV_TIMER_IOCTL_INFO         TARGET_IOR('T', 0x11,            \
                                                struct target_snd_timer_info)
#define TARGET_SNDRV_TIMER_IOCTL_PARAMS       TARGET_IOW('T', 0x12,            \
                                                struct snd_timer_params)
#define TARGET_SNDRV_TIMER_IOCTL_STATUS       TARGET_IOR('T', 0x14,            \
                                                struct target_snd_timer_status)
#define TARGET_SNDRV_TIMER_IOCTL_START        TARGET_IO('T', 0xa0)
#define TARGET_SNDRV_TIMER_IOCTL_STOP         TARGET_IO('T', 0xa1)
#define TARGET_SNDRV_TIMER_IOCTL_CONTINUE     TARGET_IO('T', 0xa2)
#define TARGET_SNDRV_TIMER_IOCTL_PAUSE        TARGET_IO('T', 0xa3)

/* vfat ioctls */
#define TARGET_VFAT_IOCTL_READDIR_BOTH    TARGET_IORU('r', 1)
#define TARGET_VFAT_IOCTL_READDIR_SHORT   TARGET_IORU('r', 2)

struct target_mtop {
    abi_short mt_op;
    abi_int mt_count;
};

#if defined(TARGET_SPARC) || defined(TARGET_MIPS)
typedef abi_long target_kernel_daddr_t;
#else
typedef abi_int target_kernel_daddr_t;
#endif

struct target_mtget {
    abi_long mt_type;
    abi_long mt_resid;
    abi_long mt_dsreg;
    abi_long mt_gstat;
    abi_long mt_erreg;
    target_kernel_daddr_t mt_fileno;
    target_kernel_daddr_t mt_blkno;
};

struct target_mtpos {
    abi_long mt_blkno;
};

#define TARGET_MTIOCTOP        TARGET_IOW('m', 1, struct target_mtop)
#define TARGET_MTIOCGET        TARGET_IOR('m', 2, struct target_mtget)
#define TARGET_MTIOCPOS        TARGET_IOR('m', 3, struct target_mtpos)

/* kcov ioctls */
#define TARGET_KCOV_ENABLE     TARGET_IO('c', 100)
#define TARGET_KCOV_DISABLE    TARGET_IO('c', 101)
#define TARGET_KCOV_INIT_TRACE TARGET_IOR('c', 1, abi_ulong)

struct target_sysinfo {
    abi_long uptime;                /* Seconds since boot */
    abi_ulong loads[3];             /* 1, 5, and 15 minute load averages */
    abi_ulong totalram;             /* Total usable main memory size */
    abi_ulong freeram;              /* Available memory size */
    abi_ulong sharedram;            /* Amount of shared memory */
    abi_ulong bufferram;            /* Memory used by buffers */
    abi_ulong totalswap;            /* Total swap space size */
    abi_ulong freeswap;             /* swap space still available */
    unsigned short procs;           /* Number of current processes */
    unsigned short pad;             /* explicit padding for m68k */
    abi_ulong totalhigh;            /* Total high memory size */
    abi_ulong freehigh;             /* Available high memory size */
    unsigned int mem_unit;          /* Memory unit size in bytes */
    char _f[20-2*sizeof(abi_long)-sizeof(int)]; /* Padding: libc5 uses this.. */
};

struct linux_dirent {
    long            d_ino;
    unsigned long   d_off;
    unsigned short  d_reclen;
    char            d_name[];
};

struct linux_dirent64 {
    uint64_t        d_ino;
    int64_t         d_off;
    unsigned short  d_reclen;
    unsigned char   d_type;
    char            d_name[];
};

struct target_mq_attr {
    abi_long mq_flags;
    abi_long mq_maxmsg;
    abi_long mq_msgsize;
    abi_long mq_curmsgs;
};

struct target_drm_version {
    int version_major;
    int version_minor;
    int version_patchlevel;
    abi_ulong name_len;
    abi_ulong name;
    abi_ulong date_len;
    abi_ulong date;
    abi_ulong desc_len;
    abi_ulong desc;
};

struct target_drm_i915_getparam {
    int param;
    abi_ulong value;
};

#include "socket.h"

#include "target_errno_defs.h"

#define FUTEX_WAIT              0
#define FUTEX_WAKE              1
#define FUTEX_FD                2
#define FUTEX_REQUEUE           3
#define FUTEX_CMP_REQUEUE       4
#define FUTEX_WAKE_OP           5
#define FUTEX_LOCK_PI           6
#define FUTEX_UNLOCK_PI         7
#define FUTEX_TRYLOCK_PI        8
#define FUTEX_WAIT_BITSET       9
#define FUTEX_WAKE_BITSET       10

#define FUTEX_PRIVATE_FLAG      128
#define FUTEX_CLOCK_REALTIME    256
#define FUTEX_CMD_MASK          ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)

#ifdef CONFIG_EPOLL
#if defined(TARGET_X86_64)
#define TARGET_EPOLL_PACKED QEMU_PACKED
#else
#define TARGET_EPOLL_PACKED
#endif

typedef union target_epoll_data {
    abi_ulong ptr;
    abi_int fd;
    abi_uint u32;
    abi_ullong u64;
} target_epoll_data_t;

struct target_epoll_event {
    abi_uint events;
    target_epoll_data_t data;
} TARGET_EPOLL_PACKED;

#define TARGET_EP_MAX_EVENTS (INT_MAX / sizeof(struct target_epoll_event))

#endif

struct target_ucred {
    uint32_t pid;
    uint32_t uid;
    uint32_t gid;
};

typedef int32_t target_timer_t;

#define TARGET_SIGEV_MAX_SIZE 64

/* This is architecture-specific but most architectures use the default */
#ifdef TARGET_MIPS
#define TARGET_SIGEV_PREAMBLE_SIZE (sizeof(int32_t) * 2 + sizeof(abi_long))
#else
#define TARGET_SIGEV_PREAMBLE_SIZE (sizeof(int32_t) * 2 \
                                    + sizeof(target_sigval_t))
#endif

#define TARGET_SIGEV_PAD_SIZE ((TARGET_SIGEV_MAX_SIZE \
                                - TARGET_SIGEV_PREAMBLE_SIZE) \
                               / sizeof(int32_t))

struct target_sigevent {
    target_sigval_t sigev_value;
    abi_int sigev_signo;
    abi_int sigev_notify;
    union {
        abi_int _pad[TARGET_SIGEV_PAD_SIZE];
        abi_int _tid;

        /* The kernel (and thus QEMU) never looks at these;
         * they're only used as part of the ABI between a
         * userspace program and libc.
         */
        struct {
            abi_ulong _function;
            abi_ulong _attribute;
        } _sigev_thread;
    } _sigev_un;
};

struct target_user_cap_header {
    uint32_t version;
    int pid;
};

struct target_user_cap_data {
    uint32_t effective;
    uint32_t permitted;
    uint32_t inheritable;
};

/* from kernel's include/linux/syslog.h */

/* Close the log.  Currently a NOP. */
#define TARGET_SYSLOG_ACTION_CLOSE          0
/* Open the log. Currently a NOP. */
#define TARGET_SYSLOG_ACTION_OPEN           1
/* Read from the log. */
#define TARGET_SYSLOG_ACTION_READ           2
/* Read all messages remaining in the ring buffer. */
#define TARGET_SYSLOG_ACTION_READ_ALL       3
/* Read and clear all messages remaining in the ring buffer */
#define TARGET_SYSLOG_ACTION_READ_CLEAR     4
/* Clear ring buffer. */
#define TARGET_SYSLOG_ACTION_CLEAR          5
/* Disable printk's to console */
#define TARGET_SYSLOG_ACTION_CONSOLE_OFF    6
/* Enable printk's to console */
#define TARGET_SYSLOG_ACTION_CONSOLE_ON     7
/* Set level of messages printed to console */
#define TARGET_SYSLOG_ACTION_CONSOLE_LEVEL  8
/* Return number of unread characters in the log buffer */
#define TARGET_SYSLOG_ACTION_SIZE_UNREAD    9
/* Return size of the log buffer */
#define TARGET_SYSLOG_ACTION_SIZE_BUFFER   10

struct target_statx_timestamp {
   int64_t tv_sec;
   uint32_t tv_nsec;
   int32_t __reserved;
};

struct target_statx {
   /* 0x00 */
   uint32_t stx_mask;       /* What results were written [uncond] */
   uint32_t stx_blksize;    /* Preferred general I/O size [uncond] */
   uint64_t stx_attributes; /* Flags conveying information about the file */
   /* 0x10 */
   uint32_t stx_nlink;      /* Number of hard links */
   uint32_t stx_uid;        /* User ID of owner */
   uint32_t stx_gid;        /* Group ID of owner */
   uint16_t stx_mode;       /* File mode */
   uint16_t __spare0[1];
   /* 0x20 */
   uint64_t stx_ino;        /* Inode number */
   uint64_t stx_size;       /* File size */
   uint64_t stx_blocks;     /* Number of 512-byte blocks allocated */
   uint64_t stx_attributes_mask; /* Mask to show what is supported */
   /* 0x40 */
   struct target_statx_timestamp  stx_atime;  /* Last access time */
   struct target_statx_timestamp  stx_btime;  /* File creation time */
   struct target_statx_timestamp  stx_ctime;  /* Last attribute change time */
   struct target_statx_timestamp  stx_mtime;  /* Last data modification time */
   /* 0x80 */
   uint32_t stx_rdev_major;   /* Device ID of special file [if bdev/cdev] */
   uint32_t stx_rdev_minor;
   uint32_t stx_dev_major; /* ID of device containing file [uncond] */
   uint32_t stx_dev_minor;
   /* 0x90 */
   uint64_t __spare2[14];  /* Spare space for future expansion */
   /* 0x100 */
};

/* from kernel's include/linux/sched/types.h */
struct target_sched_attr {
    abi_uint size;
    abi_uint sched_policy;
    abi_ullong sched_flags;
    abi_int sched_nice;
    abi_uint sched_priority;
    abi_ullong sched_runtime;
    abi_ullong sched_deadline;
    abi_ullong sched_period;
    abi_uint sched_util_min;
    abi_uint sched_util_max;
};

struct target_sched_param {
    abi_int sched_priority;
};

#endif
