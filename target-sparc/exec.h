#ifndef EXEC_SPARC_H
#define EXEC_SPARC_H 1
#include "dyngen-exec.h"

register struct CPUSPARCState *env asm(AREG0);
register uint32_t T0 asm(AREG1);
register uint32_t T1 asm(AREG2);
register uint32_t T2 asm(AREG3);

#include "cpu.h"
#include "exec-all.h"

void cpu_lock(void);
void cpu_unlock(void);
void cpu_loop_exit(void);
#endif
