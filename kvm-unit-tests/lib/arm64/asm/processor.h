#ifndef _ASMARM64_PROCESSOR_H_
#define _ASMARM64_PROCESSOR_H_
/*
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */

/* System Control Register (SCTLR_EL1) bits */
#define SCTLR_EL1_EE	(1 << 25)
#define SCTLR_EL1_WXN	(1 << 19)
#define SCTLR_EL1_I	(1 << 12)
#define SCTLR_EL1_SA0	(1 << 4)
#define SCTLR_EL1_SA	(1 << 3)
#define SCTLR_EL1_C	(1 << 2)
#define SCTLR_EL1_A	(1 << 1)
#define SCTLR_EL1_M	(1 << 0)

#ifndef __ASSEMBLY__
#include <asm/ptrace.h>

enum vector {
	EL1T_SYNC,
	EL1T_IRQ,
	EL1T_FIQ,
	EL1T_ERROR,
	EL1H_SYNC,
	EL1H_IRQ,
	EL1H_FIQ,
	EL1H_ERROR,
	EL0_SYNC_64,
	EL0_IRQ_64,
	EL0_FIQ_64,
	EL0_ERROR_64,
	EL0_SYNC_32,
	EL0_IRQ_32,
	EL0_FIQ_32,
	EL0_ERROR_32,
	VECTOR_MAX,
};

#define EC_MAX 64

typedef void (*vector_fn)(enum vector v, struct pt_regs *regs,
			  unsigned int esr);
typedef void (*exception_fn)(struct pt_regs *regs, unsigned int esr);
extern void install_vector_handler(enum vector v, vector_fn fn);
extern void install_exception_handler(enum vector v, unsigned int ec,
				      exception_fn fn);
extern void default_vector_handler(enum vector v, struct pt_regs *regs,
				   unsigned int esr);
extern void vector_handlers_default_init(vector_fn *handlers);

extern void show_regs(struct pt_regs *regs);
extern bool get_far(unsigned int esr, unsigned long *far);

static inline unsigned long current_level(void)
{
	unsigned long el;
	asm volatile("mrs %0, CurrentEL" : "=r" (el));
	return el & 0xc;
}

#define DEFINE_GET_SYSREG32(reg)				\
static inline unsigned int get_##reg(void)			\
{								\
	unsigned int reg;					\
	asm volatile("mrs %0, " #reg "_el1" : "=r" (reg));	\
	return reg;						\
}
DEFINE_GET_SYSREG32(mpidr)

/* Only support Aff0 for now, gicv2 only */
#define mpidr_to_cpu(mpidr) ((int)((mpidr) & 0xff))

extern void start_usr(void (*func)(void *arg), void *arg, unsigned long sp_usr);
extern bool is_user(void);

#endif /* !__ASSEMBLY__ */
#endif /* _ASMARM64_PROCESSOR_H_ */
