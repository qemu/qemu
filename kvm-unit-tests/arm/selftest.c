/*
 * Test the framework itself. These tests confirm that setup works.
 *
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <libcflat.h>
#include <alloc.h>
#include <devicetree.h>
#include <asm/setup.h>
#include <asm/ptrace.h>
#include <asm/asm-offsets.h>
#include <asm/processor.h>
#include <asm/thread_info.h>
#include <asm/psci.h>
#include <asm/smp.h>
#include <asm/cpumask.h>
#include <asm/barrier.h>

static void assert_args(int num_args, int needed_args)
{
	if (num_args < needed_args) {
		printf("selftest: not enough arguments\n");
		abort();
	}
}

static char *split_var(char *s, long *val)
{
	char *p;

	p = strchr(s, '=');
	if (!p)
		return NULL;

	*val = atol(p+1);
	*p = '\0';

	return s;
}

static void check_setup(int argc, char **argv)
{
	int nr_tests = 0, i;
	char *var;
	long val;

	for (i = 0; i < argc; ++i) {

		var = split_var(argv[i], &val);
		if (!var)
			continue;

		report_prefix_push(var);

		if (strcmp(var, "mem") == 0) {

			phys_addr_t memsize = PHYS_END - PHYS_OFFSET;
			phys_addr_t expected = ((phys_addr_t)val)*1024*1024;

			report("size = %d MB", memsize == expected,
							memsize/1024/1024);
			++nr_tests;

		} else if (strcmp(var, "smp") == 0) {

			report("nr_cpus = %d", nr_cpus == (int)val, nr_cpus);
			++nr_tests;
		}

		report_prefix_pop();
	}

	assert_args(nr_tests, 2);
}

static struct pt_regs expected_regs;
static bool und_works;
static bool svc_works;
#if defined(__arm__)
/*
 * Capture the current register state and execute an instruction
 * that causes an exception. The test handler will check that its
 * capture of the current register state matches the capture done
 * here.
 *
 * NOTE: update clobber list if passed insns needs more than r0,r1
 */
#define test_exception(pre_insns, excptn_insn, post_insns)	\
	asm volatile(						\
		pre_insns "\n"					\
		"mov	r0, %0\n"				\
		"stmia	r0, { r0-lr }\n"			\
		"mrs	r1, cpsr\n"				\
		"str	r1, [r0, #" xstr(S_PSR) "]\n"		\
		"mov	r1, #-1\n"				\
		"str	r1, [r0, #" xstr(S_OLD_R0) "]\n"	\
		"add	r1, pc, #8\n"				\
		"str	r1, [r0, #" xstr(S_R1) "]\n"		\
		"str	r1, [r0, #" xstr(S_PC) "]\n"		\
		excptn_insn "\n"				\
		post_insns "\n"					\
	:: "r" (&expected_regs) : "r0", "r1")

static bool check_regs(struct pt_regs *regs)
{
	unsigned i;

	/* exception handlers should always run in svc mode */
	if (current_mode() != SVC_MODE)
		return false;

	for (i = 0; i < ARRAY_SIZE(regs->uregs); ++i) {
		if (regs->uregs[i] != expected_regs.uregs[i])
			return false;
	}

	return true;
}

static void und_handler(struct pt_regs *regs)
{
	und_works = check_regs(regs);
}

static bool check_und(void)
{
	install_exception_handler(EXCPTN_UND, und_handler);

	/* issue an instruction to a coprocessor we don't have */
	test_exception("", "mcr p2, 0, r0, c0, c0", "");

	install_exception_handler(EXCPTN_UND, NULL);

	return und_works;
}

static void svc_handler(struct pt_regs *regs)
{
	u32 svc = *(u32 *)(regs->ARM_pc - 4) & 0xffffff;

	if (processor_mode(regs) == SVC_MODE) {
		/*
		 * When issuing an svc from supervisor mode lr_svc will
		 * get corrupted. So before issuing the svc, callers must
		 * always push it on the stack. We pushed it to offset 4.
		 */
		regs->ARM_lr = *(unsigned long *)(regs->ARM_sp + 4);
	}

	svc_works = check_regs(regs) && svc == 123;
}

static bool check_svc(void)
{
	install_exception_handler(EXCPTN_SVC, svc_handler);

	if (current_mode() == SVC_MODE) {
		/*
		 * An svc from supervisor mode will corrupt lr_svc and
		 * spsr_svc. We need to save/restore them separately.
		 */
		test_exception(
			"mrs	r0, spsr\n"
			"push	{ r0,lr }\n",
			"svc	#123\n",
			"pop	{ r0,lr }\n"
			"msr	spsr_cxsf, r0\n"
		);
	} else {
		test_exception("", "svc #123", "");
	}

	install_exception_handler(EXCPTN_SVC, NULL);

	return svc_works;
}
#elif defined(__aarch64__)
#include <asm/esr.h>

/*
 * Capture the current register state and execute an instruction
 * that causes an exception. The test handler will check that its
 * capture of the current register state matches the capture done
 * here.
 *
 * NOTE: update clobber list if passed insns needs more than x0,x1
 */
#define test_exception(pre_insns, excptn_insn, post_insns)	\
	asm volatile(						\
		pre_insns "\n"					\
		"mov	x1, %0\n"				\
		"ldr	x0, [x1, #" xstr(S_PSTATE) "]\n"	\
		"mrs	x1, nzcv\n"				\
		"orr	w0, w0, w1\n"				\
		"mov	x1, %0\n"				\
		"str	w0, [x1, #" xstr(S_PSTATE) "]\n"	\
		"mov	x0, sp\n"				\
		"str	x0, [x1, #" xstr(S_SP) "]\n"		\
		"adr	x0, 1f\n"				\
		"str	x0, [x1, #" xstr(S_PC) "]\n"		\
		"stp	 x2,  x3, [x1,  #16]\n"			\
		"stp	 x4,  x5, [x1,  #32]\n"			\
		"stp	 x6,  x7, [x1,  #48]\n"			\
		"stp	 x8,  x9, [x1,  #64]\n"			\
		"stp	x10, x11, [x1,  #80]\n"			\
		"stp	x12, x13, [x1,  #96]\n"			\
		"stp	x14, x15, [x1, #112]\n"			\
		"stp	x16, x17, [x1, #128]\n"			\
		"stp	x18, x19, [x1, #144]\n"			\
		"stp	x20, x21, [x1, #160]\n"			\
		"stp	x22, x23, [x1, #176]\n"			\
		"stp	x24, x25, [x1, #192]\n"			\
		"stp	x26, x27, [x1, #208]\n"			\
		"stp	x28, x29, [x1, #224]\n"			\
		"str	x30, [x1, #" xstr(S_LR) "]\n"		\
		"stp	 x0,  x1, [x1]\n"			\
	"1:"	excptn_insn "\n"				\
		post_insns "\n"					\
	:: "r" (&expected_regs) : "x0", "x1")

static bool check_regs(struct pt_regs *regs)
{
	unsigned i;

	/* exception handlers should always run in EL1 */
	if (current_level() != CurrentEL_EL1)
		return false;

	for (i = 0; i < ARRAY_SIZE(regs->regs); ++i) {
		if (regs->regs[i] != expected_regs.regs[i])
			return false;
	}

	regs->pstate &= 0xf0000000 /* NZCV */ | 0x3c0 /* DAIF */
			| PSR_MODE_MASK;

	return regs->sp == expected_regs.sp
		&& regs->pc == expected_regs.pc
		&& regs->pstate == expected_regs.pstate;
}

static enum vector check_vector_prep(void)
{
	unsigned long daif;

	if (is_user())
		return EL0_SYNC_64;

	asm volatile("mrs %0, daif" : "=r" (daif) ::);
	expected_regs.pstate = daif | PSR_MODE_EL1h;
	return EL1H_SYNC;
}

static void unknown_handler(struct pt_regs *regs, unsigned int esr __unused)
{
	und_works = check_regs(regs);
	regs->pc += 4;
}

static bool check_und(void)
{
	enum vector v = check_vector_prep();

	install_exception_handler(v, ESR_EL1_EC_UNKNOWN, unknown_handler);

	/* try to read an el2 sysreg from el0/1 */
	test_exception("", "mrs x0, sctlr_el2", "");

	install_exception_handler(v, ESR_EL1_EC_UNKNOWN, NULL);

	return und_works;
}

static void svc_handler(struct pt_regs *regs, unsigned int esr)
{
	u16 svc = esr & 0xffff;

	expected_regs.pc += 4;
	svc_works = check_regs(regs) && svc == 123;
}

static bool check_svc(void)
{
	enum vector v = check_vector_prep();

	install_exception_handler(v, ESR_EL1_EC_SVC64, svc_handler);

	test_exception("", "svc #123", "");

	install_exception_handler(v, ESR_EL1_EC_SVC64, NULL);

	return svc_works;
}
#endif

static void check_vectors(void *arg __unused)
{
	report("und", check_und());
	report("svc", check_svc());
	exit(report_summary());
}

static bool psci_check(void)
{
	const struct fdt_property *method;
	int node, len, ver;

	node = fdt_node_offset_by_compatible(dt_fdt(), -1, "arm,psci-0.2");
	if (node < 0) {
		printf("PSCI v0.2 compatibility required\n");
		return false;
	}

	method = fdt_get_property(dt_fdt(), node, "method", &len);
	if (method == NULL) {
		printf("bad psci device tree node\n");
		return false;
	}

	if (len < 4 || strcmp(method->data, "hvc") != 0) {
		printf("psci method must be hvc\n");
		return false;
	}

	ver = psci_invoke(PSCI_0_2_FN_PSCI_VERSION, 0, 0, 0);
	printf("PSCI version %d.%d\n", PSCI_VERSION_MAJOR(ver),
				       PSCI_VERSION_MINOR(ver));

	return true;
}

static cpumask_t smp_reported;
static void cpu_report(void)
{
	int cpu = smp_processor_id();

	report("CPU%d online", true, cpu);
	cpumask_set_cpu(cpu, &smp_reported);
	halt();
}

int main(int argc, char **argv)
{
	report_prefix_push("selftest");
	assert_args(argc, 1);
	report_prefix_push(argv[0]);

	if (strcmp(argv[0], "setup") == 0) {

		check_setup(argc-1, &argv[1]);

	} else if (strcmp(argv[0], "vectors-kernel") == 0) {

		check_vectors(NULL);

	} else if (strcmp(argv[0], "vectors-user") == 0) {

		void *sp = memalign(THREAD_SIZE, THREAD_SIZE);
		start_usr(check_vectors, NULL,
				(unsigned long)sp + THREAD_START_SP);

	} else if (strcmp(argv[0], "smp") == 0) {

		int cpu;

		report("PSCI version", psci_check());

		for_each_present_cpu(cpu) {
			if (cpu == 0)
				continue;
			smp_boot_secondary(cpu, cpu_report);
		}

		cpumask_set_cpu(0, &smp_reported);
		while (!cpumask_full(&smp_reported))
			cpu_relax();
	}

	return report_summary();
}
