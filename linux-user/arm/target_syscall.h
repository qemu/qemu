#ifndef ARM_TARGET_SYSCALL_H
#define ARM_TARGET_SYSCALL_H

#define ARM_SYSCALL_BASE	0x900000
#define ARM_THUMB_SYSCALL	0

#define ARM_NR_BASE	  0xf0000
#define ARM_NR_breakpoint (ARM_NR_BASE + 1)
#define ARM_NR_cacheflush (ARM_NR_BASE + 2)
#define ARM_NR_set_tls	  (ARM_NR_BASE + 5)
#define ARM_NR_get_tls    (ARM_NR_BASE + 6)

#if TARGET_BIG_ENDIAN
#define UNAME_MACHINE "armv5teb"
#else
#define UNAME_MACHINE "armv5tel"
#endif
#define UNAME_MINIMUM_RELEASE "2.6.32"

#define TARGET_CLONE_BACKWARDS

#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4

#define TARGET_WANT_OLD_SYS_SELECT

#define TARGET_FORCE_SHMLBA

static inline abi_ulong target_shmlba(CPUARMState *env)
{
    return 4 * 4096;
}

#endif /* ARM_TARGET_SYSCALL_H */
