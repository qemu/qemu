/*
 * processor control and status functions
 *
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <libcflat.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/thread_info.h>

static const char *processor_modes[] = {
	"USER_26", "FIQ_26" , "IRQ_26" , "SVC_26" ,
	"UK4_26" , "UK5_26" , "UK6_26" , "UK7_26" ,
	"UK8_26" , "UK9_26" , "UK10_26", "UK11_26",
	"UK12_26", "UK13_26", "UK14_26", "UK15_26",
	"USER_32", "FIQ_32" , "IRQ_32" , "SVC_32" ,
	"UK4_32" , "UK5_32" , "UK6_32" , "ABT_32" ,
	"UK8_32" , "UK9_32" , "UK10_32", "UND_32" ,
	"UK12_32", "UK13_32", "UK14_32", "SYS_32"
};

static const char *vector_names[] = {
	"rst", "und", "svc", "pabt", "dabt", "addrexcptn", "irq", "fiq"
};

void show_regs(struct pt_regs *regs)
{
	unsigned long flags;
	char buf[64];

	printf("pc : [<%08lx>]    lr : [<%08lx>]    psr: %08lx\n"
	       "sp : %08lx  ip : %08lx  fp : %08lx\n",
		regs->ARM_pc, regs->ARM_lr, regs->ARM_cpsr,
		regs->ARM_sp, regs->ARM_ip, regs->ARM_fp);
	printf("r10: %08lx  r9 : %08lx  r8 : %08lx\n",
		regs->ARM_r10, regs->ARM_r9, regs->ARM_r8);
	printf("r7 : %08lx  r6 : %08lx  r5 : %08lx  r4 : %08lx\n",
		regs->ARM_r7, regs->ARM_r6, regs->ARM_r5, regs->ARM_r4);
	printf("r3 : %08lx  r2 : %08lx  r1 : %08lx  r0 : %08lx\n",
		regs->ARM_r3, regs->ARM_r2, regs->ARM_r1, regs->ARM_r0);

	flags = regs->ARM_cpsr;
	buf[0] = flags & PSR_N_BIT ? 'N' : 'n';
	buf[1] = flags & PSR_Z_BIT ? 'Z' : 'z';
	buf[2] = flags & PSR_C_BIT ? 'C' : 'c';
	buf[3] = flags & PSR_V_BIT ? 'V' : 'v';
	buf[4] = '\0';

	printf("Flags: %s  IRQs o%s  FIQs o%s  Mode %s\n",
		buf, interrupts_enabled(regs) ? "n" : "ff",
		fast_interrupts_enabled(regs) ? "n" : "ff",
		processor_modes[processor_mode(regs)]);

	if (!user_mode(regs)) {
		unsigned int ctrl, transbase, dac;
		asm volatile(
			"mrc p15, 0, %0, c1, c0\n"
			"mrc p15, 0, %1, c2, c0\n"
			"mrc p15, 0, %2, c3, c0\n"
		: "=r" (ctrl), "=r" (transbase), "=r" (dac));
		printf("Control: %08x  Table: %08x  DAC: %08x\n",
			ctrl, transbase, dac);
	}
}

void install_exception_handler(enum vector v, exception_fn fn)
{
	struct thread_info *ti = current_thread_info();

	if (v < EXCPTN_MAX)
		ti->exception_handlers[v] = fn;
}

void do_handle_exception(enum vector v, struct pt_regs *regs)
{
	struct thread_info *ti = thread_info_sp(regs->ARM_sp);

	if (ti->flags & TIF_USER_MODE) {
		if (v < EXCPTN_MAX && ti->exception_handlers[v]) {
			ti->exception_handlers[v](regs);
			return;
		}
		ti = current_thread_info();
	}

	if (v < EXCPTN_MAX && ti->exception_handlers[v]) {
		ti->exception_handlers[v](regs);
		return;
	}

	if (v < EXCPTN_MAX)
		printf("Unhandled exception %d (%s)\n", v, vector_names[v]);
	else
		printf("%s called with vector=%d\n", __func__, v);

	printf("Exception frame registers:\n");
	show_regs(regs);
	if (v == EXCPTN_DABT) {
		unsigned long far, fsr;
		asm volatile("mrc p15, 0, %0, c6, c0, 0": "=r" (far));
		asm volatile("mrc p15, 0, %0, c5, c0, 0": "=r" (fsr));
		printf("DFAR: %08lx    DFSR: %08lx\n", far, fsr);
	} else if (v == EXCPTN_PABT) {
		unsigned long far, fsr;
		asm volatile("mrc p15, 0, %0, c6, c0, 2": "=r" (far));
		asm volatile("mrc p15, 0, %0, c5, c0, 1": "=r" (fsr));
		printf("IFAR: %08lx    IFSR: %08lx\n", far, fsr);
	}
	abort();
}

void thread_info_init(struct thread_info *ti, unsigned int flags)
{
	memset(ti, 0, sizeof(struct thread_info));
	ti->cpu = mpidr_to_cpu(get_mpidr());
	ti->flags = flags;
}

void start_usr(void (*func)(void *arg), void *arg, unsigned long sp_usr)
{
	sp_usr &= (~7UL); /* stack ptr needs 8-byte alignment */

	thread_info_init(thread_info_sp(sp_usr), TIF_USER_MODE);

	asm volatile(
		"mrs	r0, cpsr\n"
		"bic	r0, #" xstr(MODE_MASK) "\n"
		"orr	r0, #" xstr(USR_MODE) "\n"
		"msr	cpsr_c, r0\n"
		"isb\n"
		"mov	r0, %0\n"
		"mov	sp, %1\n"
		"mov	pc, %2\n"
	:: "r" (arg), "r" (sp_usr), "r" (func) : "r0");
}

bool is_user(void)
{
	return current_thread_info()->flags & TIF_USER_MODE;
}
