#ifndef NIOS2_TARGET_SYSCALL_H
#define NIOS2_TARGET_SYSCALL_H

#define UNAME_MACHINE "nios2"
#define UNAME_MINIMUM_RELEASE "3.19.0"

struct target_pt_regs {
    unsigned long  r8;    /* r8-r15 Caller-saved GP registers */
    unsigned long  r9;
    unsigned long  r10;
    unsigned long  r11;
    unsigned long  r12;
    unsigned long  r13;
    unsigned long  r14;
    unsigned long  r15;
    unsigned long  r1;    /* Assembler temporary */
    unsigned long  r2;    /* Retval LS 32bits */
    unsigned long  r3;    /* Retval MS 32bits */
    unsigned long  r4;    /* r4-r7 Register arguments */
    unsigned long  r5;
    unsigned long  r6;
    unsigned long  r7;
    unsigned long  orig_r2;    /* Copy of r2 ?? */
    unsigned long  ra;    /* Return address */
    unsigned long  fp;    /* Frame pointer */
    unsigned long  sp;    /* Stack pointer */
    unsigned long  gp;    /* Global pointer */
    unsigned long  estatus;
    unsigned long  ea;    /* Exception return address (pc) */
    unsigned long  orig_r7;
};

#define TARGET_MINSIGSTKSZ 2048
#define TARGET_MLOCKALL_MCL_CURRENT 1
#define TARGET_MLOCKALL_MCL_FUTURE  2

#endif /* NIOS2_TARGET_SYSCALL_H */
