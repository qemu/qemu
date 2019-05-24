#ifndef M68K_TARGET_SYSCALL_H
#define M68K_TARGET_SYSCALL_H

/* this struct defines the way the registers are stored on the
   stack during a system call. */

struct target_pt_regs {
    abi_long d1, d2, d3, d4, d5, d6, d7;
    abi_long a0, a1, a2, a3, a4, a5, a6;
    abi_ulong d0;
    abi_ulong usp;
    abi_ulong orig_d0;
    int16_t stkadj;
    uint16_t sr;
    abi_ulong pc;
    uint16_t fntvex;
    uint16_t __fill;
};

#define UNAME_MACHINE "m68k"
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_MINSIGSTKSZ 2048
#define TARGET_MLOCKALL_MCL_CURRENT 1
#define TARGET_MLOCKALL_MCL_FUTURE  2

#define TARGET_WANT_OLD_SYS_SELECT

#endif /* M68K_TARGET_SYSCALL_H */
