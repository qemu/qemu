#ifndef XTENSA_TARGET_SYSCALL_H
#define XTENSA_TARGET_SYSCALL_H

#define UNAME_MACHINE "xtensa"

#define UNAME_MINIMUM_RELEASE "3.19"
#define TARGET_CLONE_BACKWARDS

#define MMAP_SHIFT TARGET_PAGE_BITS

typedef uint32_t xtensa_reg_t;
typedef struct {
} xtregs_opt_t; /* TODO */

struct target_pt_regs {
    xtensa_reg_t pc;            /*   4 */
    xtensa_reg_t ps;            /*   8 */
    xtensa_reg_t depc;          /*  12 */
    xtensa_reg_t exccause;      /*  16 */
    xtensa_reg_t excvaddr;      /*  20 */
    xtensa_reg_t debugcause;    /*  24 */
    xtensa_reg_t wmask;         /*  28 */
    xtensa_reg_t lbeg;          /*  32 */
    xtensa_reg_t lend;          /*  36 */
    xtensa_reg_t lcount;        /*  40 */
    xtensa_reg_t sar;           /*  44 */
    xtensa_reg_t windowbase;    /*  48 */
    xtensa_reg_t windowstart;   /*  52 */
    xtensa_reg_t syscall;       /*  56 */
    xtensa_reg_t icountlevel;   /*  60 */
    xtensa_reg_t scompare1;     /*  64 */
    xtensa_reg_t threadptr;     /*  68 */

    /* Additional configurable registers that are used by the compiler. */
    xtregs_opt_t xtregs_opt;

    /* Make sure the areg field is 16 bytes aligned. */
    int align[0] __attribute__ ((aligned(16)));

    /* current register frame.
     * Note: The ESF for kernel exceptions ends after 16 registers!
     */
    xtensa_reg_t areg[16];
};

#define TARGET_MLOCKALL_MCL_CURRENT 1
#define TARGET_MLOCKALL_MCL_FUTURE  2

#endif
