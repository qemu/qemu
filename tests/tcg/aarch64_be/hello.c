/*
 * Non-libc syscall hello world for Aarch64 BE
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define __NR_write 64
#define __NR_exit 93

int write(int fd, char *buf, int len)
{
    register int x0 __asm__("x0") = fd;
    register char *x1 __asm__("x1") = buf;
    register int x2 __asm__("x2") = len;
    register int x8 __asm__("x8") = __NR_write;

    asm volatile("svc #0" : : "r"(x0), "r"(x1), "r"(x2), "r"(x8));

    return len;
}

void exit(int ret)
{
    register int x0 __asm__("x0") = ret;
    register int x8 __asm__("x8") = __NR_exit;

    asm volatile("svc #0" : : "r"(x0), "r"(x8));
    __builtin_unreachable();
}

void _start(void)
{
    write(1, "Hello World\n", 12);
    exit(0);
}
