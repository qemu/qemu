#ifndef TARGET_OS_SIGNAL_H
#define TARGET_OS_SIGNAL_H

#include "target_os_siginfo.h"
#include "target_arch_signal.h"

abi_long setup_sigframe_arch(CPUArchState *env, abi_ulong frame_addr,
                             struct target_sigframe *frame, int flags);

/* Compare to sys/signal.h */
#define TARGET_SIGHUP  1       /* hangup */
#define TARGET_SIGINT  2       /* interrupt */
#define TARGET_SIGQUIT 3       /* quit */
#define TARGET_SIGILL  4       /* illegal instruction (not reset when caught) */
#define TARGET_SIGTRAP 5       /* trace trap (not reset when caught) */
#define TARGET_SIGABRT 6       /* abort() */
#define TARGET_SIGIOT  SIGABRT /* compatibility */
#define TARGET_SIGEMT  7       /* EMT instruction */
#define TARGET_SIGFPE  8       /* floating point exception */
#define TARGET_SIGKILL 9       /* kill (cannot be caught or ignored) */
#define TARGET_SIGBUS  10      /* bus error */
#define TARGET_SIGSEGV 11      /* segmentation violation */
#define TARGET_SIGSYS  12      /* bad argument to system call */
#define TARGET_SIGPIPE 13      /* write on a pipe with no one to read it */
#define TARGET_SIGALRM 14      /* alarm clock */
#define TARGET_SIGTERM 15      /* software termination signal from kill */
#define TARGET_SIGURG  16      /* urgent condition on IO channel */
#define TARGET_SIGSTOP 17      /* sendable stop signal not from tty */
#define TARGET_SIGTSTP 18      /* stop signal from tty */
#define TARGET_SIGCONT 19      /* continue a stopped process */
#define TARGET_SIGCHLD 20      /* to parent on child stop or exit */
#define TARGET_SIGTTIN 21      /* to readers pgrp upon background tty read */
#define TARGET_SIGTTOU 22      /* like TTIN for output if(tp->t_local&LTOSTOP)*/
#define TARGET_SIGIO   23      /* input/output possible signal */
#define TARGET_SIGXCPU 24      /* exceeded CPU time limit */
#define TARGET_SIGXFSZ 25      /* exceeded file size limit */
#define TARGET_SIGVTALRM 26    /* virtual time alarm */
#define TARGET_SIGPROF 27      /* profiling time alarm */
#define TARGET_SIGWINCH 28     /* window size changes */
#define TARGET_SIGINFO  29     /* information request */
#define TARGET_SIGUSR1 30      /* user defined signal 1 */
#define TARGET_SIGUSR2 31      /* user defined signal 2 */
#define TARGET_SIGTHR 32       /* reserved by thread library */
#define TARGET_SIGLWP SIGTHR   /* compatibility */
#define TARGET_SIGLIBRT 33     /* reserved by the real-time library */
#define TARGET_SIGRTMIN 65
#define TARGET_SIGRTMAX 126

/*
 * Language spec says we must list exactly one parameter, even though we
 * actually supply three.  Ugh!
 */
#define TARGET_SIG_DFL      ((abi_long)0)   /* default signal handling */
#define TARGET_SIG_IGN      ((abi_long)1)   /* ignore signal */
#define TARGET_SIG_ERR      ((abi_long)-1)  /* error return from signal */

#define TARGET_SA_ONSTACK   0x0001  /* take signal on signal stack */
#define TARGET_SA_RESTART   0x0002  /* restart system on signal return */
#define TARGET_SA_RESETHAND 0x0004  /* reset to SIG_DFL when taking signal */
#define TARGET_SA_NODEFER   0x0010  /* don't mask the signal we're delivering */
#define TARGET_SA_NOCLDWAIT 0x0020  /* don't create zombies (assign to pid 1) */
#define TARGET_SA_USERTRAMP 0x0100  /* do not bounce off kernel's sigtramp */
#define TARGET_SA_NOCLDSTOP 0x0008  /* do not generate SIGCHLD on child stop */
#define TARGET_SA_SIGINFO   0x0040  /* generate siginfo_t */

/*
 * Flags for sigprocmask:
 */
#define TARGET_SIG_BLOCK        1   /* block specified signal set */
#define TARGET_SIG_UNBLOCK      2   /* unblock specified signal set */
#define TARGET_SIG_SETMASK      3   /* set specified signal set */

#define TARGET_BADSIG           SIG_ERR

/*
 * sigaltstack control
 */
#define TARGET_SS_ONSTACK 0x0001  /* take signals on alternate stack */
#define TARGET_SS_DISABLE 0x0004  /* disable taking signals on alternate stack*/

#endif /* TARGET_OS_SIGNAL_H */
