#include "io.h"

int main(void)
{
    long long rd, rs, rt, result;

    rd = 0;
    rs = 0x45BCFFFF12345678;
    rt = 0x98529AD287654321;
    result = 0x52fbec7035a2ca5c;

    __asm
        ("muleq_s.pw.qhl %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    if (result != rd) {
        printf("1 muleq_s.pw.qhl error\n");

        return -1;
    }

    rd = 0;
    rs = 0x45BC800012345678;
    rt = 0x9852800087654321;
    result = 0x52fbec707FFFFFFF;

    __asm
        ("muleq_s.pw.qhl %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    if (result != rd) {
        printf("2 muleq_s.pw.qhl error\n");

        return -1;
    }

    rd = 0;
    __asm
        ("rddsp %0\n\t"
         : "=r"(rd)
        );
    rd = rd >> 21;
    rd = rd & 0x1;

    if (rd != 1) {
        printf("3 muleq_s.pw.qhl error\n");

        return -1;
    }

    return 0;
}
