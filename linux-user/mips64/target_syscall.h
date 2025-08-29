#ifndef MIPS64_TARGET_SYSCALL_H
#define MIPS64_TARGET_SYSCALL_H

#define UNAME_MACHINE "mips64"
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_CLONE_BACKWARDS
#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4

#define TARGET_FORCE_SHMLBA

static inline abi_ulong target_shmlba(CPUMIPSState *env)
{
    return 0x40000;
}

#endif /* MIPS64_TARGET_SYSCALL_H */
