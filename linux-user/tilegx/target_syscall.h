#ifndef TILEGX_TARGET_SYSCALL_H
#define TILEGX_TARGET_SYSCALL_H

#define UNAME_MACHINE "tilegx"
#define UNAME_MINIMUM_RELEASE "3.19"

#define MMAP_SHIFT TARGET_PAGE_BITS

#define TILEGX_IS_ERRNO(ret) \
                       ((ret) > 0xfffffffffffff000ULL) /* errno is 0 -- 4096 */

typedef uint64_t tilegx_reg_t;

struct target_pt_regs {

    union {
        /* Saved main processor registers; 56..63 are special. */
        tilegx_reg_t regs[56];
        struct {
            tilegx_reg_t __regs[53];
            tilegx_reg_t tp;    /* aliases regs[TREG_TP] */
            tilegx_reg_t sp;    /* aliases regs[TREG_SP] */
            tilegx_reg_t lr;    /* aliases regs[TREG_LR] */
        };
    };

    /* Saved special registers. */
    tilegx_reg_t pc;            /* stored in EX_CONTEXT_K_0 */
    tilegx_reg_t ex1;           /* stored in EX_CONTEXT_K_1 (PL and ICS bit) */
    tilegx_reg_t faultnum;      /* fault number (INT_SWINT_1 for syscall) */
    tilegx_reg_t orig_r0;       /* r0 at syscall entry, else zero */
    tilegx_reg_t flags;         /* flags (see below) */
    tilegx_reg_t cmpexch;       /* value of CMPEXCH_VALUE SPR at interrupt */
    tilegx_reg_t pad[2];
};

#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4

/* For faultnum */
#define TARGET_INT_SWINT_1            14

#endif
