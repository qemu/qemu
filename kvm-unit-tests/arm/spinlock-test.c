#include <libcflat.h>
#include <asm/smp.h>
#include <asm/cpumask.h>
#include <asm/barrier.h>

#define LOOP_SIZE 10000000

struct lock_ops {
	void (*lock)(int *v);
	void (*unlock)(int *v);
};
static struct lock_ops lock_ops;

static void gcc_builtin_lock(int *lock_var)
{
	while (__sync_lock_test_and_set(lock_var, 1));
}
static void gcc_builtin_unlock(int *lock_var)
{
	__sync_lock_release(lock_var);
}
static void none_lock(int *lock_var)
{
	while (*lock_var != 0);
	*lock_var = 1;
}
static void none_unlock(int *lock_var)
{
	*lock_var = 0;
}

static int global_a, global_b;
static int global_lock;

static cpumask_t smp_test_complete;

static void test_spinlock(void)
{
	int i, errors = 0;
	int cpu = smp_processor_id();

	printf("CPU%d online\n", cpu);

	for (i = 0; i < LOOP_SIZE; i++) {

		lock_ops.lock(&global_lock);

		if (global_a == (cpu + 1) % 2) {
			global_a = 1;
			global_b = 0;
		} else {
			global_a = 0;
			global_b = 1;
		}

		if (global_a == global_b)
			errors++;

		lock_ops.unlock(&global_lock);
	}
	report("CPU%d: Done - Errors: %d\n", errors == 0, cpu, errors);

	cpumask_set_cpu(cpu, &smp_test_complete);
	if (cpu != 0)
		halt();
}

int main(int argc, char **argv)
{
	int cpu;

	if (argc && strcmp(argv[0], "bad") != 0) {
		lock_ops.lock = gcc_builtin_lock;
		lock_ops.unlock = gcc_builtin_unlock;
	} else {
		lock_ops.lock = none_lock;
		lock_ops.unlock = none_unlock;
	}

	for_each_present_cpu(cpu) {
		if (cpu == 0)
			continue;
		smp_boot_secondary(cpu, test_spinlock);
	}

	test_spinlock();

	while (!cpumask_full(&smp_test_complete))
		cpu_relax();

	return report_summary();
}
