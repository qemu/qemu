#ifndef MIPS_TARGET_SYSCALL_H
#define MIPS_TARGET_SYSCALL_H

/* this struct defines the way the registers are stored on the
   stack during a system call. */

struct target_pt_regs {
	/* Pad bytes for argument save space on the stack. */
	abi_ulong pad0[6];

	/* Saved main processor registers. */
	abi_ulong regs[32];

	/* Saved special registers. */
	abi_ulong cp0_status;
	abi_ulong lo;
	abi_ulong hi;
	abi_ulong cp0_badvaddr;
	abi_ulong cp0_cause;
	abi_ulong cp0_epc;
};

#define UNAME_MACHINE "mips"
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

#endif /* MIPS_TARGET_SYSCALL_H */
