#ifndef CPU_SPARC_H
#define CPU_SPARC_H

#define TARGET_LONG_BITS 32

#include "cpu-defs.h"

/*#define EXCP_INTERRUPT 0x100*/

/* trap definitions */
#define TT_ILL_INSN 0x02
#define TT_WIN_OVF  0x05
#define TT_WIN_UNF  0x06 
#define TT_DIV_ZERO 0x2a
#define TT_TRAP     0x80

#define PSR_NEG   (1<<23)
#define PSR_ZERO  (1<<22)
#define PSR_OVF   (1<<21)
#define PSR_CARRY (1<<20)

#define NWINDOWS  32

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
    uint32_t cwp;      /* index of current register window (extracted
                          from PSR) */
    uint32_t wim;      /* window invalid mask */
    jmp_buf  jmp_env;
    int user_mode_only;
    int exception_index;
    int interrupt_index;
    int interrupt_request;
    struct TranslationBlock *current_tb;
    void *opaque;
    /* NOTE: we allow 8 more registers to handle wrapping */
    uint32_t regbase[NWINDOWS * 16 + 8];

    /* in order to avoid passing too many arguments to the memory
       write helpers, we store some rarely used information in the CPU
       context) */
    unsigned long mem_write_pc; /* host pc at which the memory was
                                   written */
    unsigned long mem_write_vaddr; /* target virtual addr at which the
                                      memory was written */
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
