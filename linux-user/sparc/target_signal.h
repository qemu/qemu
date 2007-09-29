#ifndef TARGET_SIGNAL_H
#define TARGET_SIGNAL_H

#include "cpu.h"

/* this struct defines a stack used during syscall handling */

typedef struct target_sigaltstack {
	target_ulong ss_sp;
	target_long ss_flags;
	target_ulong ss_size;
} target_stack_t;


/*
 * sigaltstack controls
 */
#define TARGET_SS_ONSTACK	1
#define TARGET_SS_DISABLE	2

#define TARGET_MINSIGSTKSZ	4096
#define TARGET_SIGSTKSZ		16384

#ifndef UREG_I6
#define UREG_I6        6
#endif
#ifndef UREG_FP
#define UREG_FP        UREG_I6
#endif

static inline target_ulong get_sp_from_cpustate(CPUSPARCState *state)
{
    return state->regwptr[UREG_FP];
}

#endif /* TARGET_SIGNAL_H */
