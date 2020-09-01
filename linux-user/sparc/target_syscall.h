#ifndef SPARC_TARGET_SYSCALL_H
#define SPARC_TARGET_SYSCALL_H

#include "target_errno.h"

struct target_pt_regs {
	abi_ulong psr;
	abi_ulong pc;
	abi_ulong npc;
	abi_ulong y;
	abi_ulong u_regs[16];
};

#define UNAME_MACHINE "sparc"
#define UNAME_MINIMUM_RELEASE "2.6.32"

/* SPARC kernels don't define this in their Kconfig, but they have the
 * same ABI as if they did, implemented by sparc-specific code which fishes
 * directly in the u_regs() struct for half the parameters in sparc_do_fork()
 * and copy_thread().
 */
#define TARGET_CLONE_BACKWARDS
#define TARGET_MINSIGSTKSZ      4096
#define TARGET_MCL_CURRENT 0x2000
#define TARGET_MCL_FUTURE  0x4000
#define TARGET_MCL_ONFAULT 0x8000

/* For SPARC SHMLBA is determined at runtime in the kernel, and
 * libc has to runtime-detect it using the hwcaps (see glibc
 * sysdeps/unix/sysv/linux/sparc/getshmlba; we follow the same
 * logic here, though we know we're not the sparc v9 64-bit case).
 */
#define TARGET_FORCE_SHMLBA

static inline abi_ulong target_shmlba(CPUSPARCState *env)
{
    if (!(env->def.features & CPU_FEATURE_FLUSH)) {
        return 64 * 1024;
    } else {
        return 256 * 1024;
    }
}

#endif /* SPARC_TARGET_SYSCALL_H */
