struct target_pt_regs {
	abi_ulong u_regs[16];
	abi_ulong tstate;
	abi_ulong pc;
	abi_ulong npc;
	abi_ulong y;
	abi_ulong fprs;
};

#define UNAME_MACHINE "sun4u"

/* SPARC kernels don't define this in their Kconfig, but they have the
 * same ABI as if they did, implemented by sparc-specific code which fishes
 * directly in the u_regs() struct for half the parameters in sparc_do_fork()
 * and copy_thread().
 */
#define TARGET_CLONE_BACKWARDS
