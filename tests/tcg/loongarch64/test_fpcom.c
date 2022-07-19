#include <assert.h>

#define TEST_COMP(N)                              \
void test_##N(float fj, float fk)                 \
{                                                 \
    int rd = 0;                                   \
                                                  \
    asm volatile("fcmp."#N".s $fcc6,%1,%2\n"      \
                 "movcf2gr %0, $fcc6\n"           \
                 : "=r"(rd)                       \
                 : "f"(fj), "f"(fk)               \
                 : );                             \
    assert(rd == 1);                              \
}

TEST_COMP(ceq)
TEST_COMP(clt)
TEST_COMP(cle)
TEST_COMP(cne)
TEST_COMP(seq)
TEST_COMP(slt)
TEST_COMP(sle)
TEST_COMP(sne)

int main()
{
    test_ceq(0xff700102, 0xff700102);
    test_clt(0x00730007, 0xff730007);
    test_cle(0xff70130a, 0xff70130b);
    test_cne(0x1238acde, 0xff71111f);
    test_seq(0xff766618, 0xff766619);
    test_slt(0xff78881c, 0xff78901d);
    test_sle(0xff780b22, 0xff790b22);
    test_sne(0xff7bcd25, 0xff7a26cf);

    return 0;
}
