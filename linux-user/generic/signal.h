/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */

#ifndef GENERIC_SIGNAL_H
#define GENERIC_SIGNAL_H

#define TARGET_SA_NOCLDSTOP     0x00000001
#define TARGET_SA_NOCLDWAIT     0x00000002 /* not supported yet */
#define TARGET_SA_SIGINFO       0x00000004
#define TARGET_SA_ONSTACK       0x08000000
#define TARGET_SA_RESTART       0x10000000
#define TARGET_SA_NODEFER       0x40000000
#define TARGET_SA_RESETHAND     0x80000000
#define TARGET_SA_RESTORER      0x04000000

#define TARGET_SIGHUP            1
#define TARGET_SIGINT            2
#define TARGET_SIGQUIT           3
#define TARGET_SIGILL            4
#define TARGET_SIGTRAP           5
#define TARGET_SIGABRT           6
#define TARGET_SIGIOT            6
#define TARGET_SIGBUS            7
#define TARGET_SIGFPE            8
#define TARGET_SIGKILL           9
#define TARGET_SIGUSR1          10
#define TARGET_SIGSEGV          11
#define TARGET_SIGUSR2          12
#define TARGET_SIGPIPE          13
#define TARGET_SIGALRM          14
#define TARGET_SIGTERM          15
#define TARGET_SIGSTKFLT        16
#define TARGET_SIGCHLD          17
#define TARGET_SIGCONT          18
#define TARGET_SIGSTOP          19
#define TARGET_SIGTSTP          20
#define TARGET_SIGTTIN          21
#define TARGET_SIGTTOU          22
#define TARGET_SIGURG           23
#define TARGET_SIGXCPU          24
#define TARGET_SIGXFSZ          25
#define TARGET_SIGVTALRM        26
#define TARGET_SIGPROF          27
#define TARGET_SIGWINCH         28
#define TARGET_SIGIO            29
#define TARGET_SIGPWR           30
#define TARGET_SIGSYS           31
#define TARGET_SIGRTMIN         32

#define TARGET_SIG_BLOCK          0    /* for blocking signals */
#define TARGET_SIG_UNBLOCK        1    /* for unblocking signals */
#define TARGET_SIG_SETMASK        2    /* for setting the signal mask */

/* this struct defines a stack used during syscall handling */
typedef struct target_sigaltstack {
    abi_ulong ss_sp;
    abi_int ss_flags;
    abi_ulong ss_size;
} target_stack_t;

/*
 * sigaltstack controls
 */
#define TARGET_SS_ONSTACK 1
#define TARGET_SS_DISABLE 2

#define TARGET_MINSIGSTKSZ     2048

/* bit-flags */
#define TARGET_SS_AUTODISARM (1U << 31) /* disable sas during sighandling */
/* mask for all SS_xxx flags */
#define TARGET_SS_FLAG_BITS  TARGET_SS_AUTODISARM

#endif
