#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <stdlib.h>

/* will be changed in signal handler */
volatile sig_atomic_t completed_tests;
static jmp_buf after_test;
static int nr_tests;

void __attribute__((naked)) test_return(void)
{
    asm volatile(
        "allocframe(#0x8)\n"
        "r0 = #0xffffffff\n"
        "framekey = r0\n"
        "dealloc_return\n"
        :
        :
        : "r0", "r29", "r30", "r31", "framekey");
}

void test_endloop(void)
{
    asm volatile(
        "loop0(1f, #2)\n"
        "1: r0 = #0x3\n"
        "sa0 = r0\n"
        "{ nop }:endloop0\n"
        :
        :
        : "r0", "sa0", "lc0", "usr");
}

asm(
    ".pushsection .text.unaligned\n"
    ".org 0x3\n"
    ".global test_multi_cof_unaligned\n"
    "test_multi_cof_unaligned:\n"
    "   jumpr r31\n"
    ".popsection\n"
);

#define SYS_EXIT 94

void test_multi_cof(void)
{
    asm volatile(
        "p0 = cmp.eq(r0, r0)\n"
        "{\n"
        "    if (p0) jump test_multi_cof_unaligned\n"
        "    if (!p0) jump 1f\n"
        "}\n"
        "1:"
        "  r0 = #1\n"
        "  r6 = #%0\n"
        "  trap0(#1)\n"
        :
        : "i"(SYS_EXIT)
        : "p0", "r0", "r6");
}

void sigbus_handler(int signum)
{
    /* retore framekey after test_return */
    asm volatile(
        "r0 = #0\n"
        "framekey = r0\n"
        :
        :
        : "r0", "framekey");
    printf("Test %d complete\n", completed_tests);
    completed_tests++;
    siglongjmp(after_test, 1);
}

void test_done(void)
{
    int err = (completed_tests != nr_tests);
    puts(err ? "FAIL" : "PASS");
    exit(err);
}

typedef void (*test_fn)(void);

int main()
{
    test_fn tests[] = { test_return, test_endloop, test_multi_cof, test_done };
    nr_tests = (sizeof(tests) / sizeof(tests[0])) - 1;

    struct sigaction sa = {
        .sa_sigaction = sigbus_handler,
        .sa_flags = SA_SIGINFO
    };

    if (sigaction(SIGBUS, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    sigsetjmp(after_test, 1);
    tests[completed_tests]();

    /* should never get here */
    puts("FAIL");
    return 1;
}
