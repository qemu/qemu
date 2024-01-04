#ifndef _ASMARM64_PTRACE_H_
#define _ASMARM64_PTRACE_H_
/*
 * Adapted from Linux kernel headers
 * arch/arm64/include/asm/ptrace.h
 * arch/arm64/include/uapi/asm/ptrace.h
 */

/* Current Exception Level values, as contained in CurrentEL */
#define CurrentEL_EL1	(1 << 2)
#define CurrentEL_EL2	(2 << 2)

/*
 * PSR bits
 */
#define PSR_MODE_EL0t	0x00000000
#define PSR_MODE_EL1t	0x00000004
#define PSR_MODE_EL1h	0x00000005
#define PSR_MODE_EL2t	0x00000008
#define PSR_MODE_EL2h	0x00000009
#define PSR_MODE_EL3t	0x0000000c
#define PSR_MODE_EL3h	0x0000000d
#define PSR_MODE_MASK	0x0000000f

/* AArch32 CPSR bits */
#define PSR_MODE32_BIT	0x00000010

/* AArch64 SPSR bits */
#define PSR_F_BIT	0x00000040
#define PSR_I_BIT	0x00000080
#define PSR_A_BIT	0x00000100
#define PSR_D_BIT	0x00000200
#define PSR_Q_BIT	0x08000000
#define PSR_V_BIT	0x10000000
#define PSR_C_BIT	0x20000000
#define PSR_Z_BIT	0x40000000
#define PSR_N_BIT	0x80000000

/*
 * Groups of PSR bits
 */
#define PSR_f		0xff000000	/* Flags                */
#define PSR_s		0x00ff0000	/* Status               */
#define PSR_x		0x0000ff00	/* Extension            */
#define PSR_c		0x000000ff	/* Control              */

#ifndef __ASSEMBLY__
#include <libcflat.h>

struct user_pt_regs {
	u64		regs[31];
	u64		sp;
	u64		pc;
	u64		pstate;
};

struct user_fpsimd_state {
	__uint128_t	vregs[32];
	u32		fpsr;
	u32		fpcr;
};

/*
 * This struct defines the way the registers are stored on the stack during an
 * exception. Note that sizeof(struct pt_regs) has to be a multiple of 16 (for
 * stack alignment). struct user_pt_regs must form a prefix of struct pt_regs.
 */
struct pt_regs {
	union {
		struct user_pt_regs user_regs;
		struct {
			u64 regs[31];
			u64 sp;
			u64 pc;
			u64 pstate;
		};
	};
	u64 orig_x0;
	u64 syscallno;
};

#define user_mode(regs) \
	(((regs)->pstate & PSR_MODE_MASK) == PSR_MODE_EL0t)

#define processor_mode(regs) \
	((regs)->pstate & PSR_MODE_MASK)

#define interrupts_enabled(regs) \
	(!((regs)->pstate & PSR_I_BIT))

#define fast_interrupts_enabled(regs) \
	(!((regs)->pstate & PSR_F_BIT))

#endif /* !__ASSEMBLY__ */
#endif /* _ASMARM64_PTRACE_H_ */
