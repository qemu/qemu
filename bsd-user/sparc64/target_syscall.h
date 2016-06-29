#ifndef TARGET_SYSCALL_H
#define TARGET_SYSCALL_H

struct target_pt_regs {
	abi_ulong u_regs[16];
	abi_ulong tstate;
	abi_ulong pc;
	abi_ulong npc;
	abi_ulong y;
	abi_ulong fprs;
};

#define UNAME_MACHINE "sun4u"

#endif /* TARGET_SYSCALL_H */
