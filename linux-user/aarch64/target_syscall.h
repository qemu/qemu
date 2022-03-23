#ifndef AARCH64_TARGET_SYSCALL_H
#define AARCH64_TARGET_SYSCALL_H

struct target_pt_regs {
    uint64_t        regs[31];
    uint64_t        sp;
    uint64_t        pc;
    uint64_t        pstate;
};

#if TARGET_BIG_ENDIAN
#define UNAME_MACHINE "aarch64_be"
#else
#define UNAME_MACHINE "aarch64"
#endif
#define UNAME_MINIMUM_RELEASE "3.8.0"
#define TARGET_CLONE_BACKWARDS
#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4

#endif /* AARCH64_TARGET_SYSCALL_H */
