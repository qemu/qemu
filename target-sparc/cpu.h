#ifndef CPU_SPARC_H
#define CPU_SPARC_H

#include "config.h"

#if !defined(TARGET_SPARC64)
#define TARGET_LONG_BITS 32
#define TARGET_FPREGS 32
#define TARGET_FPREG_T float
#else
#define TARGET_LONG_BITS 64
#define TARGET_FPREGS 64
#define TARGET_FPREG_T double
#endif

#include "cpu-defs.h"

/*#define EXCP_INTERRUPT 0x100*/

/* trap definitions */
#define TT_TFAULT   0x01
#define TT_ILL_INSN 0x02
#define TT_PRIV_INSN 0x03
#define TT_NFPU_INSN 0x04
#define TT_WIN_OVF  0x05
#define TT_WIN_UNF  0x06 
#define TT_FP_EXCP  0x08
#define TT_DFAULT   0x09
#define TT_EXTINT   0x10
#define TT_DIV_ZERO 0x2a
#define TT_TRAP     0x80

#define PSR_NEG   (1<<23)
#define PSR_ZERO  (1<<22)
#define PSR_OVF   (1<<21)
#define PSR_CARRY (1<<20)
#define PSR_ICC   (PSR_NEG|PSR_ZERO|PSR_OVF|PSR_CARRY)
#define PSR_EF    (1<<12)
#define PSR_PIL   0xf00
#define PSR_S     (1<<7)
#define PSR_PS    (1<<6)
#define PSR_ET    (1<<5)
#define PSR_CWP   0x1f

/* Trap base register */
#define TBR_BASE_MASK 0xfffff000

/* Fcc */
#define FSR_RD1        (1<<31)
#define FSR_RD0        (1<<30)
#define FSR_RD_MASK    (FSR_RD1 | FSR_RD0)
#define FSR_RD_NEAREST 0
#define FSR_RD_ZERO    FSR_RD0
#define FSR_RD_POS     FSR_RD1
#define FSR_RD_NEG     (FSR_RD1 | FSR_RD0)

#define FSR_NVM   (1<<27)
#define FSR_OFM   (1<<26)
#define FSR_UFM   (1<<25)
#define FSR_DZM   (1<<24)
#define FSR_NXM   (1<<23)
#define FSR_TEM_MASK (FSR_NVM | FSR_OFM | FSR_UFM | FSR_DZM | FSR_NXM)

#define FSR_NVA   (1<<9)
#define FSR_OFA   (1<<8)
#define FSR_UFA   (1<<7)
#define FSR_DZA   (1<<6)
#define FSR_NXA   (1<<5)
#define FSR_AEXC_MASK (FSR_NVA | FSR_OFA | FSR_UFA | FSR_DZA | FSR_NXA)

#define FSR_NVC   (1<<4)
#define FSR_OFC   (1<<3)
#define FSR_UFC   (1<<2)
#define FSR_DZC   (1<<1)
#define FSR_NXC   (1<<0)
#define FSR_CEXC_MASK (FSR_NVC | FSR_OFC | FSR_UFC | FSR_DZC | FSR_NXC)

#define FSR_FTT2   (1<<16)
#define FSR_FTT1   (1<<15)
#define FSR_FTT0   (1<<14)
#define FSR_FTT_MASK (FSR_FTT2 | FSR_FTT1 | FSR_FTT0)
#define FSR_FTT_IEEE_EXCP (1 << 14)
#define FSR_FTT_UNIMPFPOP (3 << 14)
#define FSR_FTT_INVAL_FPR (6 << 14)

#define FSR_FCC1  (1<<11)
#define FSR_FCC0  (1<<10)

/* MMU */
#define MMU_E	  (1<<0)
#define MMU_NF	  (1<<1)

#define PTE_ENTRYTYPE_MASK 3
#define PTE_ACCESS_MASK    0x1c
#define PTE_ACCESS_SHIFT   2
#define PTE_PPN_SHIFT      7
#define PTE_ADDR_MASK      0xffffff00

#define PG_ACCESSED_BIT	5
#define PG_MODIFIED_BIT	6
#define PG_CACHE_BIT    7

#define PG_ACCESSED_MASK (1 << PG_ACCESSED_BIT)
#define PG_MODIFIED_MASK (1 << PG_MODIFIED_BIT)
#define PG_CACHE_MASK    (1 << PG_CACHE_BIT)

/* 2 <= NWINDOWS <= 32. In QEMU it must also be a power of two. */
#define NWINDOWS  8

typedef struct CPUSPARCState {
    target_ulong gregs[8]; /* general registers */
    target_ulong *regwptr; /* pointer to current register window */
    TARGET_FPREG_T    fpr[TARGET_FPREGS];  /* floating point registers */
    target_ulong pc;       /* program counter */
    target_ulong npc;      /* next program counter */
    target_ulong y;        /* multiply/divide register */
    uint32_t psr;      /* processor state register */
    uint32_t fsr;      /* FPU state register */
    uint32_t cwp;      /* index of current register window (extracted
                          from PSR) */
    uint32_t wim;      /* window invalid mask */
    uint32_t tbr;      /* trap base register */
    int      psrs;     /* supervisor mode (extracted from PSR) */
    int      psrps;    /* previous supervisor mode */
    int      psret;    /* enable traps */
    int      psrpil;   /* interrupt level */
    int      psref;    /* enable fpu */
    jmp_buf  jmp_env;
    int user_mode_only;
    int exception_index;
    int interrupt_index;
    int interrupt_request;
    struct TranslationBlock *current_tb;
    void *opaque;
    /* NOTE: we allow 8 more registers to handle wrapping */
    target_ulong regbase[NWINDOWS * 16 + 8];

    /* in order to avoid passing too many arguments to the memory
       write helpers, we store some rarely used information in the CPU
       context) */
    unsigned long mem_write_pc; /* host pc at which the memory was
                                   written */
    unsigned long mem_write_vaddr; /* target virtual addr at which the
                                      memory was written */
    /* 0 = kernel, 1 = user (may have 2 = kernel code, 3 = user code ?) */
    CPUTLBEntry tlb_read[2][CPU_TLB_SIZE];
    CPUTLBEntry tlb_write[2][CPU_TLB_SIZE];
    /* MMU regs */
    uint32_t mmuregs[16];
    /* temporary float registers */
    float ft0, ft1, ft2;
    double dt0, dt1, dt2;
#if defined(TARGET_SPARC64)
    target_ulong t0, t1, t2;
#endif

    /* ice debug support */
    target_ulong breakpoints[MAX_BREAKPOINTS];
    int nb_breakpoints;
    int singlestep_enabled; /* XXX: should use CPU single step mode instead */

} CPUSPARCState;

CPUSPARCState *cpu_sparc_init(void);
int cpu_sparc_exec(CPUSPARCState *s);
int cpu_sparc_close(CPUSPARCState *s);
void cpu_get_fp64(uint64_t *pmant, uint16_t *pexp, double f);
double cpu_put_fp64(uint64_t mant, uint16_t exp);

/* Fake impl 0, version 4 */
#define GET_PSR(env) ((0 << 28) | (4 << 24) | (env->psr & PSR_ICC) |	\
		      (env->psref? PSR_EF : 0) |			\
		      (env->psrpil << 8) |				\
		      (env->psrs? PSR_S : 0) |				\
		      (env->psrps? PSR_PS : 0) |			\
		      (env->psret? PSR_ET : 0) | env->cwp)

#ifndef NO_CPU_IO_DEFS
void cpu_set_cwp(CPUSPARCState *env1, int new_cwp);
#endif

#define PUT_PSR(env, val) do { int _tmp = val;				\
	env->psr = _tmp & PSR_ICC;					\
	env->psref = (_tmp & PSR_EF)? 1 : 0;				\
	env->psrpil = (_tmp & PSR_PIL) >> 8;				\
	env->psrs = (_tmp & PSR_S)? 1 : 0;				\
	env->psrps = (_tmp & PSR_PS)? 1 : 0;				\
	env->psret = (_tmp & PSR_ET)? 1 : 0;				\
	cpu_set_cwp(env, _tmp & PSR_CWP & (NWINDOWS - 1));		\
    } while (0)

struct siginfo;
int cpu_sparc_signal_handler(int hostsignum, struct siginfo *info, void *puc);

#define TARGET_PAGE_BITS 12 /* 4k */
#include "cpu-all.h"

#endif
