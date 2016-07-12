#ifndef AARCH64_TARGET_SYSCALL_H
#define AARCH64_TARGET_SYSCALL_H

struct target_pt_regs {
    uint64_t        regs[31];
    uint64_t        sp;
    uint64_t        pc;
    uint64_t        pstate;
};

#define UNAME_MACHINE "aarch64"
#define UNAME_MINIMUM_RELEASE "3.8.0"
#define TARGET_CLONE_BACKWARDS
#define TARGET_MINSIGSTKSZ       2048
#define TARGET_MLOCKALL_MCL_CURRENT 1
#define TARGET_MLOCKALL_MCL_FUTURE  2

#endif /* AARCH64_TARGET_SYSCALL_H */
