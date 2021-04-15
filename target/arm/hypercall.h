#ifndef HYPERCALL_H
#define HYPERCALL_H

#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "qemu/log.h"
#include "arm_ldst.h"
#include "translate.h"
#include "internals.h"
#include "qemu/host-utils.h"

#include "semihosting/semihost.h"
#include "exec/gen-icount.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"

#include "trace-tcg.h"
#include "translate-a64.h"
#include "qemu/atomic128.h"

#define FUZZER_MAGIC_HVC_IMM 0x1337

/*
 * intercept_hypercall
 * Intercepts a HVC call regardless of whether we came from EL0 or not.
 *
 * Inputs:
 *  cpu_env - Current CPU environment variable (global state to translate-a64)
 *
 * Outputs:
 *  None
 *
 * Side Effects:
 *  May log to the qemu logfile (Specified with -D argument)
 */
void intercept_hypercall(CPUARMState *cpu_env);

#endif // HYPERCALL_H
