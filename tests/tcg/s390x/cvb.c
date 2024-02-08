/*
 * Test the CONVERT TO BINARY instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int signum;

static void signal_handler(int n)
{
    signum = n;
}

#define FAIL 0x1234567887654321
#define OK32(x) (0x1234567800000000 | (uint32_t)(x))

static int64_t cvb(uint64_t x)
{
    int64_t ret = FAIL;

    signum = -1;
    asm("cvb %[ret],%[x]" : [ret] "+r" (ret) : [x] "R" (x));

    return ret;
}

static int64_t cvby(uint64_t x)
{
    int64_t ret = FAIL;

    signum = -1;
    asm("cvby %[ret],%[x]" : [ret] "+r" (ret) : [x] "T" (x));

    return ret;
}

static int64_t cvbg(__uint128_t x)
{
    int64_t ret = FAIL;

    signum = -1;
    asm("cvbg %[ret],%[x]" : [ret] "+r" (ret) : [x] "T" (x));

    return ret;
}

int main(void)
{
    __uint128_t m = (((__uint128_t)0x9223372036854775) << 16) | 0x8070;
    struct sigaction act;
    int err;

    memset(&act, 0, sizeof(act));
    act.sa_handler = signal_handler;
    err = sigaction(SIGFPE, &act, NULL);
    assert(err == 0);
    err = sigaction(SIGILL, &act, NULL);
    assert(err == 0);

    assert(cvb(0xc) == OK32(0) && signum == -1);
    assert(cvb(0x1c) == OK32(1) && signum == -1);
    assert(cvb(0x25594c) == OK32(25594) && signum == -1);
    assert(cvb(0x1d) == OK32(-1) && signum == -1);
    assert(cvb(0x2147483647c) == OK32(0x7fffffff) && signum == -1);
    assert(cvb(0x2147483648d) == OK32(-0x80000000) && signum == -1);
    assert(cvb(0x7) == FAIL && signum == SIGILL);
    assert(cvb(0x2147483648c) == OK32(0x80000000) && signum == SIGFPE);
    assert(cvb(0x3000000000c) == OK32(0xb2d05e00) && signum == SIGFPE);
    assert(cvb(0x2147483649d) == OK32(0x7fffffff) && signum == SIGFPE);
    assert(cvb(0x3000000000d) == OK32(0x4d2fa200) && signum == SIGFPE);

    assert(cvby(0xc) == OK32(0));
    assert(cvby(0x1c) == OK32(1));
    assert(cvby(0x25594c) == OK32(25594));
    assert(cvby(0x1d) == OK32(-1));
    assert(cvby(0x2147483647c) == OK32(0x7fffffff));
    assert(cvby(0x2147483648d) == OK32(-0x80000000));
    assert(cvby(0x7) == FAIL && signum == SIGILL);
    assert(cvby(0x2147483648c) == OK32(0x80000000) && signum == SIGFPE);
    assert(cvby(0x3000000000c) == OK32(0xb2d05e00) && signum == SIGFPE);
    assert(cvby(0x2147483649d) == OK32(0x7fffffff) && signum == SIGFPE);
    assert(cvby(0x3000000000d) == OK32(0x4d2fa200) && signum == SIGFPE);

    assert(cvbg(0xc) == 0);
    assert(cvbg(0x1c) == 1);
    assert(cvbg(0x25594c) == 25594);
    assert(cvbg(0x1d) == -1);
    assert(cvbg(m + 0xc) == 0x7fffffffffffffff);
    assert(cvbg(m + 0x1d) == -0x8000000000000000);
    assert(cvbg(0x7) == FAIL && signum == SIGILL);
    assert(cvbg(m + 0x1c) == FAIL && signum == SIGFPE);
    assert(cvbg(m + 0x2d) == FAIL && signum == SIGFPE);
    assert(cvbg(((__uint128_t)1 << 80) + 0xc) == FAIL && signum == SIGFPE);
    assert(cvbg(((__uint128_t)1 << 80) + 0xd) == FAIL && signum == SIGFPE);

    return EXIT_SUCCESS;
}
