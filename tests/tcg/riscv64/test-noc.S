#include <asm/unistd.h>

	.text
	.globl _start
_start:
	.option	norvc
	li	a0, 4		/* SIGILL */
	la	a1, sa
	li	a2, 0
	li	a3, 8
	li	a7, __NR_rt_sigaction
	scall

	.option	rvc
	li	a0, 1
	j	exit
	.option	norvc

pass:
	li	a0, 0
exit:
	li	a7, __NR_exit
	scall

	.data
	/* struct kernel_sigaction sa = { .sa_handler = pass }; */
	.type	sa, @object
	.size	sa, 32
sa:
	.dword	pass
	.zero	24

