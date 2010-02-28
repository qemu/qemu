#ifndef TARGET_SIGNAL_H
#define TARGET_SIGNAL_H

#include "cpu.h"

/* this struct defines a stack used during syscall handling */

typedef struct target_sigaltstack {
	abi_ulong ss_sp;
	abi_long ss_flags;
	abi_ulong ss_size;
} target_stack_t;


/*
 * sigaltstack controls
 */
#define TARGET_SS_ONSTACK	1
#define TARGET_SS_DISABLE	2

#define TARGET_MINSIGSTKSZ	4096
#define TARGET_SIGSTKSZ		16384

static inline abi_ulong get_sp_from_cpustate(CPUAlphaState *state)
{
    return state->ir[IR_SP];
}

/* From <asm/gentrap.h>.  */
#define TARGET_GEN_INTOVF      -1      /* integer overflow */
#define TARGET_GEN_INTDIV      -2      /* integer division by zero */
#define TARGET_GEN_FLTOVF      -3      /* fp overflow */
#define TARGET_GEN_FLTDIV      -4      /* fp division by zero */
#define TARGET_GEN_FLTUND      -5      /* fp underflow */
#define TARGET_GEN_FLTINV      -6      /* invalid fp operand */
#define TARGET_GEN_FLTINE      -7      /* inexact fp operand */
#define TARGET_GEN_DECOVF      -8      /* decimal overflow (for COBOL??) */
#define TARGET_GEN_DECDIV      -9      /* decimal division by zero */
#define TARGET_GEN_DECINV      -10     /* invalid decimal operand */
#define TARGET_GEN_ROPRAND     -11     /* reserved operand */
#define TARGET_GEN_ASSERTERR   -12     /* assertion error */
#define TARGET_GEN_NULPTRERR   -13     /* null pointer error */
#define TARGET_GEN_STKOVF      -14     /* stack overflow */
#define TARGET_GEN_STRLENERR   -15     /* string length error */
#define TARGET_GEN_SUBSTRERR   -16     /* substring error */
#define TARGET_GEN_RANGERR     -17     /* range error */
#define TARGET_GEN_SUBRNG      -18
#define TARGET_GEN_SUBRNG1     -19
#define TARGET_GEN_SUBRNG2     -20
#define TARGET_GEN_SUBRNG3     -21
#define TARGET_GEN_SUBRNG4     -22
#define TARGET_GEN_SUBRNG5     -23
#define TARGET_GEN_SUBRNG6     -24
#define TARGET_GEN_SUBRNG7     -25

#endif /* TARGET_SIGNAL_H */
