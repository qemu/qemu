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

void dump_regs(struct ucontext *uc)
{
    printf("EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n"
           "ESI=%08x EDI=%08x EBP=%08x ESP=%08x\n"
           "EFL=%08x EIP=%08x\n",
           uc->uc_mcontext.gregs[EAX],
           uc->uc_mcontext.gregs[EBX],
           uc->uc_mcontext.gregs[ECX],
           uc->uc_mcontext.gregs[EDX],
           uc->uc_mcontext.gregs[ESI],
           uc->uc_mcontext.gregs[EDI],
           uc->uc_mcontext.gregs[EBP],
           uc->uc_mcontext.gregs[ESP],
           uc->uc_mcontext.gregs[EFL],
           uc->uc_mcontext.gregs[EIP]);
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

int main(int argc, char **argv)
{
    struct sigaction act;
    int i;
    
    /* test division by zero reporting */
    if (setjmp(jmp_env) == 0) {
        act.sa_sigaction = sig_handler;
        sigemptyset(&act.sa_mask);
        act.sa_flags = SA_SIGINFO | SA_ONESHOT;
        sigaction(SIGFPE, &act, NULL);
        
        /* now divide by zero */
        v1 = 0;
        v1 = 2 / v1;
    }

    /* test illegal instruction reporting */
    if (setjmp(jmp_env) == 0) {
        act.sa_sigaction = sig_handler;
        sigemptyset(&act.sa_mask);
        act.sa_flags = SA_SIGINFO | SA_ONESHOT;
        sigaction(SIGILL, &act, NULL);
        
        /* now execute an invalid instruction */
        asm volatile("ud2");
    }
    
    /* test SEGV reporting */
    if (setjmp(jmp_env) == 0) {
        act.sa_sigaction = sig_handler;
        sigemptyset(&act.sa_mask);
        act.sa_flags = SA_SIGINFO | SA_ONESHOT;
        sigaction(SIGSEGV, &act, NULL);
        
        /* now store in an invalid address */
        *(char *)0x1234 = 1;
    }
    
    act.sa_handler = alarm_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGALRM, &act, NULL);
    alarm(1);
    for(i = 0;i < 2; i++) {
        sleep(1);
    }
    return 0;
}
