struct target_pt_regs {
	abi_ulong psr;
	abi_ulong pc;
	abi_ulong npc;
	abi_ulong y;
	abi_ulong u_regs[16];
};

#define UNAME_MACHINE "sun4"
