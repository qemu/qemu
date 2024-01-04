#ifndef _ASMARM_THREAD_INFO_H_
#define _ASMARM_THREAD_INFO_H_
/*
 * Adapted from arch/arm64/include/asm/thread_info.h
 *
 * Copyright (C) 2015, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <asm/page.h>

#define MIN_THREAD_SHIFT	14	/* THREAD_SIZE == 16K */
#if PAGE_SHIFT > MIN_THREAD_SHIFT
#define THREAD_SHIFT		PAGE_SHIFT
#define THREAD_SIZE		PAGE_SIZE
#define THREAD_MASK		PAGE_MASK
#else
#define THREAD_SHIFT		MIN_THREAD_SHIFT
#define THREAD_SIZE		(_AC(1,UL) << THREAD_SHIFT)
#define THREAD_MASK		(~(THREAD_SIZE-1))
#endif

#ifndef __ASSEMBLY__
#include <asm/processor.h>

#ifdef __arm__
#include <asm/ptrace.h>
/*
 * arm needs room left at the top for the exception stacks,
 * and the stack needs to be 8-byte aligned
 */
#define THREAD_START_SP \
	((THREAD_SIZE - (sizeof(struct pt_regs) * 8)) & ~7)
#else
#define THREAD_START_SP		(THREAD_SIZE - 16)
#endif

#define TIF_USER_MODE		(1U << 0)

struct thread_info {
	int cpu;
	unsigned int flags;
#ifdef __arm__
	exception_fn exception_handlers[EXCPTN_MAX];
#else
	vector_fn vector_handlers[VECTOR_MAX];
	exception_fn exception_handlers[VECTOR_MAX][EC_MAX];
#endif
	char ext[0];		/* allow unit tests to add extended info */
};

static inline struct thread_info *thread_info_sp(unsigned long sp)
{
	return (struct thread_info *)(sp & ~(THREAD_SIZE - 1));
}

register unsigned long current_stack_pointer asm("sp");

static inline struct thread_info *current_thread_info(void)
{
	return thread_info_sp(current_stack_pointer);
}

extern void thread_info_init(struct thread_info *ti, unsigned int flags);

#endif /* !__ASSEMBLY__ */
#endif /* _ASMARM_THREAD_INFO_H_ */
