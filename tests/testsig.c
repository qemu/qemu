#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <setjmp.h>
#include <sys/ucontext.h>

jmp_buf jmp_env;

void alarm_handler(int sig)
{
    printf("alarm signal=%d\n", sig);
    alarm(1);
}

#ifndef REG_EAX
#define REG_EAX EAX
#define REG_EBX EBX
#define REG_ECX ECX
#define REG_EDX EDX
#define REG_ESI ESI
#define REG_EDI EDI
#define REG_EBP EBP
#define REG_ESP ESP
#define REG_EIP EIP
#define REG_EFL EFL
#define REG_TRAPNO TRAPNO
#define REG_ERR ERR
#endif

void dump_regs(struct ucontext *uc)
{
    printf("EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n"
           "ESI=%08x EDI=%08x EBP=%08x ESP=%08x\n"
           "EFL=%08x EIP=%08x trapno=%02x err=%08x\n",
           uc->uc_mcontext.gregs[REG_EAX],
           uc->uc_mcontext.gregs[REG_EBX],
           uc->uc_mcontext.gregs[REG_ECX],
           uc->uc_mcontext.gregs[REG_EDX],
           uc->uc_mcontext.gregs[REG_ESI],
           uc->uc_mcontext.gregs[REG_EDI],
           uc->uc_mcontext.gregs[REG_EBP],
           uc->uc_mcontext.gregs[REG_ESP],
           uc->uc_mcontext.gregs[REG_EFL],
           uc->uc_mcontext.gregs[REG_EIP],
           uc->uc_mcontext.gregs[REG_TRAPNO],
           uc->uc_mcontext.gregs[REG_ERR]);
}

void sig_handler(int sig, siginfo_t *info, void *puc)
{
    struct ucontext *uc = puc;

    printf("%s: si_signo=%d si_errno=%d si_code=%d si_addr=0x%08lx\n",
           strsignal(info->si_signo),
           info->si_signo, info->si_errno, info->si_code, 
           (unsigned long)info->si_addr);
    dump_regs(uc);
    longjmp(jmp_env, 1);
}

int v1;
int tab[2];

int main(int argc, char **argv)
{
    struct sigaction act;
    volatile int val;
    
    act.sa_sigaction = sig_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGFPE, &act, NULL);
    sigaction(SIGILL, &act, NULL);
    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGTRAP, &act, NULL);

    /* test division by zero reporting */
    if (setjmp(jmp_env) == 0) {
        /* now divide by zero */
        v1 = 0;
        v1 = 2 / v1;
    }

    /* test illegal instruction reporting */
    if (setjmp(jmp_env) == 0) {
        /* now execute an invalid instruction */
        asm volatile("ud2");
    }
    
    /* test SEGV reporting */
    if (setjmp(jmp_env) == 0) {
        /* now store in an invalid address */
        *(char *)0x1234 = 1;
    }

    /* test SEGV reporting */
    if (setjmp(jmp_env) == 0) {
        /* read from an invalid address */
        v1 = *(char *)0x1234;
    }
    
    printf("segment GPF exception:\n");
    if (setjmp(jmp_env) == 0) {
        /* load an invalid segment */
        asm volatile ("movl %0, %%fs" : : "r" ((0x1234 << 3) | 0));
    }

    printf("INT exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("int $0xfd");
    }

    printf("INT3 exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("int3");
    }

    printf("CLI exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("cli");
    }

    printf("STI exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("cli");
    }

    printf("INTO exception:\n");
    if (setjmp(jmp_env) == 0) {
        /* overflow exception */
        asm volatile ("addl $1, %0 ; into" : : "r" (0x7fffffff));
    }

    printf("BOUND exception:\n");
    if (setjmp(jmp_env) == 0) {
        /* bound exception */
        tab[0] = 1;
        tab[1] = 10;
        asm volatile ("bound %0, %1" : : "r" (11), "m" (tab));
    }

    printf("OUTB exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("outb %%al, %%dx" : : "d" (0x4321), "a" (0));
    }

    printf("INB exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("inb %%dx, %%al" : "=a" (val) : "d" (0x4321));
    }

    printf("REP OUTSB exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("rep outsb" : : "d" (0x4321), "S" (tab), "c" (1));
    }

    printf("REP INSB exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("rep insb" : : "d" (0x4321), "D" (tab), "c" (1));
    }

    printf("HLT exception:\n");
    if (setjmp(jmp_env) == 0) {
        asm volatile ("hlt");
    }

    printf("single step exception:\n");
    val = 0;
    if (setjmp(jmp_env) == 0) {
        asm volatile ("pushf\n"
                      "orl $0x00100, (%%esp)\n"
                      "popf\n"
                      "movl $0xabcd, %0\n" : "=m" (val) : : "cc", "memory");
    }
    printf("val=0x%x\n", val);
    
#if 1
    {
        int i;
        act.sa_handler = alarm_handler;
        sigemptyset(&act.sa_mask);
        act.sa_flags = 0;
        sigaction(SIGALRM, &act, NULL);
        alarm(1);
        for(i = 0;i < 2; i++) {
            sleep(1);
        }
    }
#endif
    return 0;
}
