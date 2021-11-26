#ifndef SPARC_TARGET_SIGNAL_H
#define SPARC_TARGET_SIGNAL_H

#define TARGET_SIGHUP            1
#define TARGET_SIGINT            2
#define TARGET_SIGQUIT           3
#define TARGET_SIGILL            4
#define TARGET_SIGTRAP           5
#define TARGET_SIGABRT           6
#define TARGET_SIGIOT            6
#define TARGET_SIGSTKFLT         7 /* actually EMT */
#define TARGET_SIGFPE            8
#define TARGET_SIGKILL           9
#define TARGET_SIGBUS           10
#define TARGET_SIGSEGV          11
#define TARGET_SIGSYS           12
#define TARGET_SIGPIPE          13
#define TARGET_SIGALRM          14
#define TARGET_SIGTERM          15
#define TARGET_SIGURG           16
#define TARGET_SIGSTOP          17
#define TARGET_SIGTSTP          18
#define TARGET_SIGCONT          19
#define TARGET_SIGCHLD          20
#define TARGET_SIGTTIN          21
#define TARGET_SIGTTOU          22
#define TARGET_SIGIO            23
#define TARGET_SIGXCPU          24
#define TARGET_SIGXFSZ          25
#define TARGET_SIGVTALRM        26
#define TARGET_SIGPROF          27
#define TARGET_SIGWINCH         28
#define TARGET_SIGPWR           29
#define TARGET_SIGUSR1          30
#define TARGET_SIGUSR2          31
#define TARGET_SIGRTMIN         32

#define TARGET_SIG_BLOCK          0x01 /* for blocking signals */
#define TARGET_SIG_UNBLOCK        0x02 /* for unblocking signals */
#define TARGET_SIG_SETMASK        0x04 /* for setting the signal mask */

/* this struct defines a stack used during syscall handling */

typedef struct target_sigaltstack {
    abi_ulong ss_sp;
    abi_int ss_flags;
    abi_ulong ss_size;
} target_stack_t;


/*
 * sigaltstack controls
 */
#define TARGET_SS_ONSTACK	1
#define TARGET_SS_DISABLE	2

#define TARGET_SA_NOCLDSTOP    8u
#define TARGET_SA_NOCLDWAIT    0x100u
#define TARGET_SA_SIGINFO      0x200u
#define TARGET_SA_ONSTACK      1u
#define TARGET_SA_RESTART      2u
#define TARGET_SA_NODEFER      0x20u
#define TARGET_SA_RESETHAND    4u
#define TARGET_ARCH_HAS_SA_RESTORER 1
#define TARGET_ARCH_HAS_KA_RESTORER 1

#define TARGET_MINSIGSTKSZ	4096

#ifdef TARGET_ABI32
#define TARGET_ARCH_HAS_SETUP_FRAME
#define TARGET_ARCH_HAS_SIGTRAMP_PAGE 1
#else
/* For sparc64, use of KA_RESTORER is mandatory. */
#define TARGET_ARCH_HAS_SIGTRAMP_PAGE 0
#endif

/* bit-flags */
#define TARGET_SS_AUTODISARM (1U << 31) /* disable sas during sighandling */
/* mask for all SS_xxx flags */
#define TARGET_SS_FLAG_BITS  TARGET_SS_AUTODISARM

#endif /* SPARC_TARGET_SIGNAL_H */
