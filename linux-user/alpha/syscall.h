/* default linux values for the selectors */
#define __USER_DS	(1)

struct target_pt_regs {
	abi_ulong r0;
	abi_ulong r1;
	abi_ulong r2;
	abi_ulong r3;
	abi_ulong r4;
	abi_ulong r5;
	abi_ulong r6;
	abi_ulong r7;
	abi_ulong r8;
	abi_ulong r19;
	abi_ulong r20;
	abi_ulong r21;
	abi_ulong r22;
	abi_ulong r23;
	abi_ulong r24;
	abi_ulong r25;
	abi_ulong r26;
	abi_ulong r27;
	abi_ulong r28;
	abi_ulong hae;
/* JRP - These are the values provided to a0-a2 by PALcode */
	abi_ulong trap_a0;
	abi_ulong trap_a1;
	abi_ulong trap_a2;
/* These are saved by PAL-code: */
	abi_ulong ps;
	abi_ulong pc;
	abi_ulong gp;
	abi_ulong r16;
	abi_ulong r17;
	abi_ulong r18;
/* Those is needed by qemu to temporary store the user stack pointer */
        abi_ulong usp;
        abi_ulong unique;
};

#define UNAME_MACHINE "alpha"
