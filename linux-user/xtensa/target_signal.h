#ifndef XTENSA_TARGET_SIGNAL_H
#define XTENSA_TARGET_SIGNAL_H

/* this struct defines a stack used during syscall handling */

typedef struct target_sigaltstack {
    abi_ulong ss_sp;
    abi_int ss_flags;
    abi_ulong ss_size;
} target_stack_t;

/*
 * sigaltstack controls
 */
#define TARGET_SS_ONSTACK     1
#define TARGET_SS_DISABLE     2

#define TARGET_MINSIGSTKSZ    2048
#define TARGET_SIGSTKSZ       8192

#include "../generic/signal.h"

#endif
