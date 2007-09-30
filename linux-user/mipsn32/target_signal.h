#ifndef TARGET_SIGNAL_H
#define TARGET_SIGNAL_H

#include "cpu.h"

/* this struct defines a stack used during syscall handling */

typedef struct target_sigaltstack {
	int32_t ss_sp;
	uint32_t ss_size;
	int32_t ss_flags;
} target_stack_t;


/*
 * sigaltstack controls
 */
#define TARGET_SS_ONSTACK     1
#define TARGET_SS_DISABLE     2

#define TARGET_MINSIGSTKSZ    2048
#define TARGET_SIGSTKSZ       8192

static inline target_ulong get_sp_from_cpustate(CPUMIPSState *state)
{
    return state->gpr[29][state->current_tc];
}

#endif /* TARGET_SIGNAL_H */
