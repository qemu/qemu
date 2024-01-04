#ifndef _ASMARM_PTRACE_H_
#define _ASMARM_PTRACE_H_
/*
 * Adapted from Linux kernel headers
 *   arch/arm/include/asm/ptrace.h
 *   arch/arm/include/uapi/asm/ptrace.h
 */

/*
 * PSR bits
 */
#define USR_MODE	0x00000010
#define SVC_MODE	0x00000013
#define FIQ_MODE	0x00000011
#define IRQ_MODE	0x00000012
#define ABT_MODE	0x00000017
#define HYP_MODE	0x0000001a
#define UND_MODE	0x0000001b
#define SYSTEM_MODE	0x0000001f
#define MODE32_BIT	0x00000010
#define MODE_MASK	0x0000001f

#define PSR_T_BIT	0x00000020	/* >= V4T, but not V7M */
#define PSR_F_BIT	0x00000040	/* >= V4, but not V7M */
#define PSR_I_BIT	0x00000080	/* >= V4, but not V7M */
#define PSR_A_BIT	0x00000100	/* >= V6, but not V7M */
#define PSR_E_BIT	0x00000200	/* >= V6, but not V7M */
#define PSR_J_BIT	0x01000000	/* >= V5J, but not V7M */
#define PSR_Q_BIT	0x08000000	/* >= V5E, including V7M */
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

/*
 * ARMv7 groups of PSR bits
 */
#define APSR_MASK	0xf80f0000	/* N, Z, C, V, Q and GE flags */
#define PSR_ISET_MASK	0x01000010	/* ISA state (J, T) mask */
#define PSR_IT_MASK	0x0600fc00	/* If-Then execution state mask */
#define PSR_ENDIAN_MASK	0x00000200	/* Endianness state mask */

#ifndef __ASSEMBLY__
#include <libcflat.h>

struct pt_regs {
	unsigned long uregs[18];
};

#define ARM_cpsr	uregs[16]
#define ARM_pc		uregs[15]
#define ARM_lr		uregs[14]
#define ARM_sp		uregs[13]
#define ARM_ip		uregs[12]
#define ARM_fp		uregs[11]
#define ARM_r10		uregs[10]
#define ARM_r9		uregs[9]
#define ARM_r8		uregs[8]
#define ARM_r7		uregs[7]
#define ARM_r6		uregs[6]
#define ARM_r5		uregs[5]
#define ARM_r4		uregs[4]
#define ARM_r3		uregs[3]
#define ARM_r2		uregs[2]
#define ARM_r1		uregs[1]
#define ARM_r0		uregs[0]
#define ARM_ORIG_r0	uregs[17]

#define user_mode(regs) \
	(((regs)->ARM_cpsr & 0xf) == 0)

#define processor_mode(regs) \
	((regs)->ARM_cpsr & MODE_MASK)

#define interrupts_enabled(regs) \
	(!((regs)->ARM_cpsr & PSR_I_BIT))

#define fast_interrupts_enabled(regs) \
	(!((regs)->ARM_cpsr & PSR_F_BIT))

#define MAX_REG_OFFSET (offsetof(struct pt_regs, ARM_ORIG_r0))

static inline unsigned long regs_get_register(struct pt_regs *regs,
					      unsigned int offset)
{
	if (offset > MAX_REG_OFFSET)
		return 0;
	return *(unsigned long *)((unsigned long)regs + offset);
}

#endif /* !__ASSEMBLY__ */
#endif /* _ASMARM_PTRACE_H_ */
