#ifndef HPPA_TARGET_SIGNAL_H
#define HPPA_TARGET_SIGNAL_H

#define TARGET_SIGHUP           1
#define TARGET_SIGINT           2
#define TARGET_SIGQUIT          3
#define TARGET_SIGILL           4
#define TARGET_SIGTRAP          5
#define TARGET_SIGABRT          6
#define TARGET_SIGIOT           6
#define TARGET_SIGSTKFLT        7
#define TARGET_SIGFPE           8
#define TARGET_SIGKILL          9
#define TARGET_SIGBUS          10
#define TARGET_SIGSEGV         11
#define TARGET_SIGXCPU         12
#define TARGET_SIGPIPE         13
#define TARGET_SIGALRM         14
#define TARGET_SIGTERM         15
#define TARGET_SIGUSR1         16
#define TARGET_SIGUSR2         17
#define TARGET_SIGCHLD         18
#define TARGET_SIGPWR          19
#define TARGET_SIGVTALRM       20
#define TARGET_SIGPROF         21
#define TARGET_SIGIO           22
#define TARGET_SIGPOLL         TARGET_SIGIO
#define TARGET_SIGWINCH        23
#define TARGET_SIGSTOP         24
#define TARGET_SIGTSTP         25
#define TARGET_SIGCONT         26
#define TARGET_SIGTTIN         27
#define TARGET_SIGTTOU         28
#define TARGET_SIGURG          29
#define TARGET_SIGXFSZ         30
#define TARGET_SIGSYS          31

#define TARGET_SIG_BLOCK       0
#define TARGET_SIG_UNBLOCK     1
#define TARGET_SIG_SETMASK     2

/* this struct defines a stack used during syscall handling */

typedef struct target_sigaltstack {
    abi_ulong ss_sp;
    int32_t ss_flags;
    abi_ulong ss_size;
} target_stack_t;


/*
 * sigaltstack controls
 */
#define TARGET_SS_ONSTACK	1
#define TARGET_SS_DISABLE	2

#define TARGET_SA_ONSTACK       0x00000001
#define TARGET_SA_RESETHAND     0x00000004
#define TARGET_SA_NOCLDSTOP     0x00000008
#define TARGET_SA_SIGINFO       0x00000010
#define TARGET_SA_NODEFER       0x00000020
#define TARGET_SA_RESTART       0x00000040
#define TARGET_SA_NOCLDWAIT     0x00000080

#define TARGET_MINSIGSTKSZ	2048
#define TARGET_SIGSTKSZ		8192

#endif /* HPPA_TARGET_SIGNAL_H */
