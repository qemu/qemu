#ifndef SPARC_TARGET_SYSCALL_H
#define SPARC_TARGET_SYSCALL_H

#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
struct target_pt_regs {
    abi_ulong u_regs[16];
    abi_ulong tstate;
    abi_ulong pc;
    abi_ulong npc;
    uint32_t y;
    uint32_t magic;
};
#else
struct target_pt_regs {
    abi_ulong psr;
    abi_ulong pc;
    abi_ulong npc;
    abi_ulong y;
    abi_ulong u_regs[16];
};
#endif

#ifdef TARGET_SPARC64
# define UNAME_MACHINE "sparc64"
#else
# define UNAME_MACHINE "sparc"
#endif
#define UNAME_MINIMUM_RELEASE "2.6.32"

/*
 * SPARC kernels don't define this in their Kconfig, but they have the
 * same ABI as if they did, implemented by sparc-specific code which fishes
 * directly in the u_regs() struct for half the parameters in sparc_do_fork()
 * and copy_thread().
 */
#define TARGET_CLONE_BACKWARDS
#define TARGET_MINSIGSTKSZ      4096
#define TARGET_MCL_CURRENT 0x2000
#define TARGET_MCL_FUTURE  0x4000
#define TARGET_MCL_ONFAULT 0x8000

/*
 * For SPARC SHMLBA is determined at runtime in the kernel, and
 * libc has to runtime-detect it using the hwcaps.
 * See glibc sysdeps/unix/sysv/linux/sparc/getshmlba.
 */
#define TARGET_FORCE_SHMLBA

static inline abi_ulong target_shmlba(CPUSPARCState *env)
{
#ifdef TARGET_SPARC64
    return MAX(TARGET_PAGE_SIZE, 16 * 1024);
#else
    if (!(env->def.features & CPU_FEATURE_FLUSH)) {
        return 64 * 1024;
    } else {
        return 256 * 1024;
    }
#endif
}

#endif /* SPARC_TARGET_SYSCALL_H */
