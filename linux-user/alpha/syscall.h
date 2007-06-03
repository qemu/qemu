/* default linux values for the selectors */
#define __USER_DS	(1)

struct target_pt_regs {
	target_ulong r0;
	target_ulong r1;
	target_ulong r2;
	target_ulong r3;
	target_ulong r4;
	target_ulong r5;
	target_ulong r6;
	target_ulong r7;
	target_ulong r8;
	target_ulong r19;
	target_ulong r20;
	target_ulong r21;
	target_ulong r22;
	target_ulong r23;
	target_ulong r24;
	target_ulong r25;
	target_ulong r26;
	target_ulong r27;
	target_ulong r28;
	target_ulong hae;
/* JRP - These are the values provided to a0-a2 by PALcode */
	target_ulong trap_a0;
	target_ulong trap_a1;
	target_ulong trap_a2;
/* These are saved by PAL-code: */
	target_ulong ps;
	target_ulong pc;
	target_ulong gp;
	target_ulong r16;
	target_ulong r17;
	target_ulong r18;
/* Those is needed by qemu to temporary store the user stack pointer */
        target_ulong usp;
        target_ulong unique;
};

#define UNAME_MACHINE "alpha"
