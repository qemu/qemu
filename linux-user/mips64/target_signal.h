#ifndef MIPS64_TARGET_SIGNAL_H
#define MIPS64_TARGET_SIGNAL_H

#define TARGET_SIGHUP            1      /* Hangup (POSIX).  */
#define TARGET_SIGINT            2      /* Interrupt (ANSI).  */
#define TARGET_SIGQUIT           3      /* Quit (POSIX).  */
#define TARGET_SIGILL            4      /* Illegal instruction (ANSI).  */
#define TARGET_SIGTRAP           5      /* Trace trap (POSIX).  */
#define TARGET_SIGIOT            6      /* IOT trap (4.2 BSD).  */
#define TARGET_SIGABRT           TARGET_SIGIOT  /* Abort (ANSI).  */
#define TARGET_SIGEMT            7
#define TARGET_SIGSTKFLT         7 /* XXX: incorrect */
#define TARGET_SIGFPE            8      /* Floating-point exception (ANSI).  */
#define TARGET_SIGKILL           9      /* Kill, unblockable (POSIX).  */
#define TARGET_SIGBUS           10      /* BUS error (4.2 BSD).  */
#define TARGET_SIGSEGV          11      /* Segmentation violation (ANSI).  */
#define TARGET_SIGSYS           12
#define TARGET_SIGPIPE          13      /* Broken pipe (POSIX).  */
#define TARGET_SIGALRM          14      /* Alarm clock (POSIX).  */
#define TARGET_SIGTERM          15      /* Termination (ANSI).  */
#define TARGET_SIGUSR1          16      /* User-defined signal 1 (POSIX).  */
#define TARGET_SIGUSR2          17      /* User-defined signal 2 (POSIX).  */
#define TARGET_SIGCHLD          18      /* Child status has changed (POSIX).  */
#define TARGET_SIGCLD           TARGET_SIGCHLD  /* Same as TARGET_SIGCHLD (System V).  */
#define TARGET_SIGPWR           19      /* Power failure restart (System V).  */
#define TARGET_SIGWINCH 20      /* Window size change (4.3 BSD, Sun).  */
#define TARGET_SIGURG           21      /* Urgent condition on socket (4.2 BSD).  */
#define TARGET_SIGIO            22      /* I/O now possible (4.2 BSD).  */
#define TARGET_SIGPOLL          TARGET_SIGIO    /* Pollable event occurred (System V).  */
#define TARGET_SIGSTOP          23      /* Stop, unblockable (POSIX).  */
#define TARGET_SIGTSTP          24      /* Keyboard stop (POSIX).  */
#define TARGET_SIGCONT          25      /* Continue (POSIX).  */
#define TARGET_SIGTTIN          26      /* Background read from tty (POSIX).  */
#define TARGET_SIGTTOU          27      /* Background write to tty (POSIX).  */
#define TARGET_SIGVTALRM        28      /* Virtual alarm clock (4.2 BSD).  */
#define TARGET_SIGPROF          29      /* Profiling alarm clock (4.2 BSD).  */
#define TARGET_SIGXCPU          30      /* CPU limit exceeded (4.2 BSD).  */
#define TARGET_SIGXFSZ          31      /* File size limit exceeded (4.2 BSD).  */
#define TARGET_SIGRTMIN         32

#define TARGET_SIG_BLOCK        1       /* for blocking signals */
#define TARGET_SIG_UNBLOCK      2       /* for unblocking signals */
#define TARGET_SIG_SETMASK      3       /* for setting the signal mask */

/* this struct defines a stack used during syscall handling */

typedef struct target_sigaltstack {
	abi_long ss_sp;
	abi_ulong ss_size;
	abi_int ss_flags;
} target_stack_t;


/*
 * sigaltstack controls
 */
#define TARGET_SS_ONSTACK     1
#define TARGET_SS_DISABLE     2

#define TARGET_SA_NOCLDSTOP     0x00000001
#define TARGET_SA_NOCLDWAIT     0x00010000
#define TARGET_SA_SIGINFO       0x00000008
#define TARGET_SA_ONSTACK       0x08000000
#define TARGET_SA_NODEFER       0x40000000
#define TARGET_SA_RESTART       0x10000000
#define TARGET_SA_RESETHAND     0x80000000

#define TARGET_MINSIGSTKSZ    2048
#define TARGET_SIGSTKSZ       8192

#endif /* MIPS64_TARGET_SIGNAL_H */
