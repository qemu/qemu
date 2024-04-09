/* SPDX-License-Identifier: GPL-2.0-or-later */
/* See https://gitlab.com/qemu-project/qemu/-/issues/1648 */

#include <signal.h>

__attribute__((noinline))
void bar(void)
{
    /* Success! Continue through sigreturn. */
}

/*
 * Because of the change of ABI between foo and bar, the compiler is
 * required to save XMM6-XMM15.  The compiler will use MOVAPS or MOVDQA,
 * which will trap if the stack frame is not 16 byte aligned.
 */
__attribute__((noinline, ms_abi))
void foo(void)
{
    bar();
}

void sighandler(int num)
{
    foo();
}

int main(void)
{
    signal(SIGUSR1, sighandler);
    raise(SIGUSR1);
    return 0;
}
