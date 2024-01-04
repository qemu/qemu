/*
 * Adapted from u-boot's arch/arm/lib/eabi_compat.c
 */
#include <libcflat.h>

int raise(int signum __unused)
{
	printf("Divide by zero!\n");
	abort();
	return 0;
}

/* Dummy functions to avoid linker complaints */
void __aeabi_unwind_cpp_pr0(void)
{
}

void __aeabi_unwind_cpp_pr1(void)
{
}
