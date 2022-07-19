#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#define TEST_DIV(N, M)                               \
static void test_div_ ##N(uint ## M ## _t rj,        \
                          uint ## M ## _t rk,        \
                          uint64_t rm)               \
{                                                    \
    uint64_t rd = 0;                                 \
                                                     \
    asm volatile("div."#N" %0,%1,%2\n\t"             \
                 : "=r"(rd)                          \
                 : "r"(rj), "r"(rk)                  \
                 : );                                \
    assert(rd == rm);                                \
}

#define TEST_MOD(N, M)                               \
static void test_mod_ ##N(uint ## M ## _t rj,        \
                          uint ## M ## _t rk,        \
                          uint64_t rm)               \
{                                                    \
    uint64_t rd = 0;                                 \
                                                     \
    asm volatile("mod."#N" %0,%1,%2\n\t"             \
                 : "=r"(rd)                          \
                 : "r"(rj), "r"(rk)                  \
                 : );                                \
    assert(rd == rm);                                \
}

TEST_DIV(w, 32)
TEST_DIV(wu, 32)
TEST_DIV(d, 64)
TEST_DIV(du, 64)
TEST_MOD(w, 32)
TEST_MOD(wu, 32)
TEST_MOD(d, 64)
TEST_MOD(du, 64)

int main(void)
{
    test_div_w(0xffaced97, 0xc36abcde, 0x0);
    test_div_wu(0xffaced97, 0xc36abcde, 0x1);
    test_div_d(0xffaced973582005f, 0xef56832a358b, 0xffffffffffffffa8);
    test_div_du(0xffaced973582005f, 0xef56832a358b, 0x11179);
    test_mod_w(0x7cf18c32, 0xa04da650, 0x1d3f3282);
    test_mod_wu(0x7cf18c32, 0xc04da650, 0x7cf18c32);
    test_mod_d(0x7cf18c3200000000, 0xa04da65000000000, 0x1d3f328200000000);
    test_mod_du(0x7cf18c3200000000, 0xc04da65000000000, 0x7cf18c3200000000);

    return 0;
}
