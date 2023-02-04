#include <assert.h>
#include <stdint.h>

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

int main(void)
{
    test_dr();
    test_dlr();
    test_dsgr();
    test_dlgr();
    return 0;
}
