/*
 * Branch target identification, basic notskip cases.
 */

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#ifndef PROT_BTI
#define PROT_BTI  0x10
#endif

static void skip2_sigill(int sig, siginfo_t *info, void *vuc)
{
    ucontext_t *uc = vuc;
    uc->uc_mcontext.pc += 8;
    uc->uc_mcontext.pstate = 1;
}

#define NOP       "nop"
#define BTI_N     "hint #32"
#define BTI_C     "hint #34"
#define BTI_J     "hint #36"
#define BTI_JC    "hint #38"

#define BTYPE_1(DEST)    \
    "mov x1, #1\n\t"     \
    "adr x16, 1f\n\t"    \
    "br x16\n"           \
"1: " DEST "\n\t"        \
    "mov x1, #0"

#define BTYPE_2(DEST)    \
    "mov x1, #1\n\t"     \
    "adr x16, 1f\n\t"    \
    "blr x16\n"          \
"1: " DEST "\n\t"        \
    "mov x1, #0"

#define BTYPE_3(DEST)    \
    "mov x1, #1\n\t"     \
    "adr x15, 1f\n\t"    \
    "br x15\n"           \
"1: " DEST "\n\t"        \
    "mov x1, #0"

#define TEST(WHICH, DEST, EXPECT) \
    WHICH(DEST) "\n"              \
    ".if " #EXPECT "\n\t"         \
    "eor x1, x1," #EXPECT "\n"    \
    ".endif\n\t"                  \
    "add x0, x0, x1\n\t"

asm("\n"
"test_begin:\n\t"
    BTI_C "\n\t"
    "mov x2, x30\n\t"
    "mov x0, #0\n\t"

    TEST(BTYPE_1, NOP, 1)
    TEST(BTYPE_1, BTI_N, 1)
    TEST(BTYPE_1, BTI_C, 0)
    TEST(BTYPE_1, BTI_J, 0)
    TEST(BTYPE_1, BTI_JC, 0)

    TEST(BTYPE_2, NOP, 1)
    TEST(BTYPE_2, BTI_N, 1)
    TEST(BTYPE_2, BTI_C, 0)
    TEST(BTYPE_2, BTI_J, 1)
    TEST(BTYPE_2, BTI_JC, 0)

    TEST(BTYPE_3, NOP, 1)
    TEST(BTYPE_3, BTI_N, 1)
    TEST(BTYPE_3, BTI_C, 1)
    TEST(BTYPE_3, BTI_J, 0)
    TEST(BTYPE_3, BTI_JC, 0)

    "ret x2\n"
"test_end:"
);

int main()
{
    struct sigaction sa;
    void *tb, *te;

    void *p = mmap(0, getpagesize(),
                   PROT_EXEC | PROT_READ | PROT_WRITE | PROT_BTI,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = skip2_sigill;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGILL, &sa, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    /*
     * ??? With "extern char test_begin[]", some compiler versions
     * will use :got references, and some linker versions will
     * resolve this reference to a static symbol incorrectly.
     * Bypass this error by using a pc-relative reference directly.
     */
    asm("adr %0, test_begin; adr %1, test_end" : "=r"(tb), "=r"(te));

    memcpy(p, tb, te - tb);

    return ((int (*)(void))p)();
}
