#ifndef CPU_SPARC_H
#define CPU_SPARC_H

#include <setjmp.h>
#include "config.h"
#include "cpu-defs.h"

/*#define EXCP_INTERRUPT 0x100*/


#define PSR_NEG   (1<<23)
#define PSR_ZERO  (1<<22)
#define PSR_OVF   (1<<21)
#define PSR_CARRY (1<<20)

typedef struct CPUSPARCState {
	uint32_t gregs[8]; /* general registers */
	uint32_t *regwptr; /* pointer to current register window */
	double   *regfptr; /* floating point registers */
	uint32_t pc;       /* program counter */
	uint32_t npc;      /* next program counter */
	uint32_t sp;       /* stack pointer */
	uint32_t y;        /* multiply/divide register */
	uint32_t psr;      /* processor state register */
	uint32_t T2;
	jmp_buf  jmp_env;
	int user_mode_only;
	int exception_index;
	int interrupt_index;
	int interrupt_request;
	struct TranslationBlock *current_tb;
	void *opaque;
} CPUSPARCState;

CPUSPARCState *cpu_sparc_init(void);
int cpu_sparc_exec(CPUSPARCState *s);
int cpu_sparc_close(CPUSPARCState *s);

struct siginfo;
int cpu_sparc_signal_handler(int hostsignum, struct siginfo *info, void *puc);
void cpu_sparc_dump_state(CPUSPARCState *env, FILE *f, int flags);

#define TARGET_PAGE_BITS 13
#include "cpu-all.h"

#endif
