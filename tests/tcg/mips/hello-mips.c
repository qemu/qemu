/*
* MIPS o32 Linux syscall example
*
* http://www.linux-mips.org/wiki/RISC/os
* http://www.linux-mips.org/wiki/MIPSABIHistory
* http://www.linux.com/howtos/Assembly-HOWTO/mips.shtml
*
* mipsel-linux-gcc -nostdlib -mno-abicalls -fno-PIC -fno-stack-protector \
*                  -mabi=32 -O2 -static -o hello-mips hello-mips.c
*
*/
#define __NR_SYSCALL_BASE	4000
#define __NR_exit			(__NR_SYSCALL_BASE+  1)
#define __NR_write			(__NR_SYSCALL_BASE+  4)

static inline void exit1(int status)
{
    register unsigned long __a0 asm("$4") = (unsigned long) status;

    __asm__ __volatile__ (
        "	.set push	\n"
        "	.set noreorder	\n"
        "	li	$2, %0	\n"
        "	syscall		\n"
        "	.set pop	"
        :
        : "i" (__NR_exit), "r" (__a0)
        : "$2", "$8", "$9", "$10", "$11", "$12", "$13", "$14", "$15", "$24",
          "memory");
}

static inline int write(int fd, const char *buf, int len)
{
    register unsigned long __a0 asm("$4") = (unsigned long) fd;
    register unsigned long __a1 asm("$5") = (unsigned long) buf;
    register unsigned long __a2 asm("$6") = (unsigned long) len;
    register unsigned long __a3 asm("$7");
    unsigned long __v0;

    __asm__ __volatile__ (
        "	.set push	\n"
        "	.set noreorder	\n"
        "	li	$2, %2	\n"
        "	syscall		\n"
        "	move	%0, $2	\n"
        "	.set pop	"
        : "=r" (__v0), "=r" (__a3)
        : "i" (__NR_write), "r" (__a0), "r" (__a1), "r" (__a2)
        : "$2", "$8", "$9", "$10", "$11", "$12", "$13", "$14", "$15", "$24",
          "memory");

/*    if (__a3 == 0) */
        return (int) __v0;
/*
    errno = __v0;
    return -1;
 */
}

void __start(void)
{
    write (1, "Hello, World!\n", 14);
    exit1(0);
}
