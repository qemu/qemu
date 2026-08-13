#include <assert.h>
#include <signal.h>
#include <stdint.h>

/* Set asynchronously by the signal handler. */
static volatile int signum;

static void signal_handler(int n)
{
    signum = n;
}

static void test_dr(void)
{
    register int32_t r0 asm("r0") = -1;
    register int32_t r1 asm("r1") = -4241;
    int32_t b = 101, q, r;

    asm("dr %[r0],%[b]"
        : [r0] "+r" (r0), [r1] "+r" (r1)
        : [b] "r" (b)
        : "cc");
    q = r1;
    r = r0;
    assert(q == -41);
    assert(r == -100);
}

static void test_dlr(void)
{
    register uint32_t r0 asm("r0") = 0;
    register uint32_t r1 asm("r1") = 4243;
    uint32_t b = 101, q, r;

    asm("dlr %[r0],%[b]"
        : [r0] "+r" (r0), [r1] "+r" (r1)
        : [b] "r" (b)
        : "cc");
    q = r1;
    r = r0;
    assert(q == 42);
    assert(r == 1);
}

static void test_dsgr(void)
{
    register int64_t r0 asm("r0") = -1;
    register int64_t r1 asm("r1") = -4241;
    int64_t b = 101, q, r;

    asm("dsgr %[r0],%[b]"
        : [r0] "+r" (r0), [r1] "+r" (r1)
        : [b] "r" (b)
        : "cc");
    q = r1;
    r = r0;
    assert(q == -41);
    assert(r == -100);
}

static void test_dlgr(void)
{
    register uint64_t r0 asm("r0") = 0;
    register uint64_t r1 asm("r1") = 4243;
    uint64_t b = 101, q, r;

    asm("dlgr %[r0],%[b]"
        : [r0] "+r" (r0), [r1] "+r" (r1)
        : [b] "r" (b)
        : "cc");
    q = r1;
    r = r0;
    assert(q == 42);
    assert(r == 1);
}

/*
 * The most negative dividend divided by -1 yields a quotient that does not
 * fit into 32 bits, so DR must raise a fixed-point-divide exception.
 */
static void test_dr_overflow(void)
{
    struct sigaction act = { .sa_handler = signal_handler };
    register int32_t r0 asm("r0");
    register int32_t r1 asm("r1");
    int32_t b = -1;
    int err;

    err = sigaction(SIGFPE, &act, NULL);
    assert(err == 0);
    signum = -1;

    r0 = 0x80000000;
    r1 = 0;
    asm volatile("dr %[r0],%[b]"
                 : [r0] "+r" (r0), [r1] "+r" (r1)
                 : [b] "r" (b)
                 : "cc");
    assert(signum == SIGFPE);

    signal(SIGFPE, SIG_DFL);
}

int main(void)
{
    test_dr();
    test_dlr();
    test_dsgr();
    test_dlgr();
    test_dr_overflow();
    return 0;
}
