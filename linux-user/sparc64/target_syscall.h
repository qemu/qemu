#ifndef SPARC64_TARGET_SYSCALL_H
#define SPARC64_TARGET_SYSCALL_H

#include "../sparc/target_errno.h"

struct target_pt_regs {
	abi_ulong u_regs[16];
	abi_ulong tstate;
	abi_ulong pc;
	abi_ulong npc;
	abi_ulong y;
	abi_ulong fprs;
};

#define UNAME_MACHINE "sparc64"
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

#define TARGET_FORCE_SHMLBA

static inline abi_ulong target_shmlba(CPUSPARCState *env)
{
    return MAX(TARGET_PAGE_SIZE, 16 * 1024);
}
#endif /* SPARC64_TARGET_SYSCALL_H */
