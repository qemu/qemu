#ifndef OPENRISC_TARGET_SIGNAL_H
#define OPENRISC_TARGET_SIGNAL_H

/* this struct defines a stack used during syscall handling */

typedef struct target_sigaltstack {
    abi_long ss_sp;
    abi_int ss_flags;
    abi_ulong ss_size;
} target_stack_t;

/* sigaltstack controls  */
#define TARGET_SS_ONSTACK     1
#define TARGET_SS_DISABLE     2

#define TARGET_SA_NOCLDSTOP    0x00000001
#define TARGET_SA_NOCLDWAIT    0x00000002
#define TARGET_SA_SIGINFO      0x00000004
#define TARGET_SA_ONSTACK      0x08000000
#define TARGET_SA_RESTART      0x10000000
#define TARGET_SA_NODEFER      0x40000000
#define TARGET_SA_RESETHAND    0x80000000

#define TARGET_MINSIGSTKSZ    2048
#define TARGET_SIGSTKSZ       8192

#include "../generic/signal.h"

#define TARGET_ARCH_HAS_SIGTRAMP_PAGE 1

#endif /* OPENRISC_TARGET_SIGNAL_H */
