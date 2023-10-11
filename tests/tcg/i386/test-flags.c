#define _GNU_SOURCE
#include <sys/mman.h>
#include <signal.h>
#include <stdio.h>
#include <assert.h>

volatile unsigned long flags;
volatile unsigned long flags_after;
int *addr;

void sigsegv(int sig, siginfo_t *info, ucontext_t *uc)
{
    flags = uc->uc_mcontext.gregs[REG_EFL];
    mprotect(addr, 4096, PROT_READ|PROT_WRITE);
}

int main()
{
    struct sigaction sa = { .sa_handler = (void *)sigsegv, .sa_flags = SA_SIGINFO };
    sigaction(SIGSEGV, &sa, NULL);

    /* fault in the page then protect it */
    addr = mmap (NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    *addr = 0x1234;
    mprotect(addr, 4096, PROT_READ);

    asm("# set flags to all ones    \n"
        "mov $-1, %%eax             \n"
        "movq addr, %%rdi           \n"
        "sahf                       \n"
        "sub %%eax, (%%rdi)         \n"
        "pushf                      \n"
        "pop  flags_after(%%rip)    \n" : : : "eax", "edi", "memory");

    /* OF can have any value before the SUB instruction.  */
    assert((flags & 0xff) == 0xd7 && (flags_after & 0x8ff) == 0x17);
}
