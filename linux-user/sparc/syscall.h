struct target_pt_regs {
	target_ulong psr;
	target_ulong pc;
	target_ulong npc;
	target_ulong y;
	target_ulong u_regs[16];
};

#define UNAME_MACHINE "sun4"
