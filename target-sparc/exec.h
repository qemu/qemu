#ifndef EXEC_SPARC_H
#define EXEC_SPARC_H 1
#include "config.h"
#include "dyngen-exec.h"

register struct CPUSPARCState *env asm(AREG0);

#include "cpu.h"
#include "exec-all.h"

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

#endif
