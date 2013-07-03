#include"io.h"
int main(void)
{
    long long rt, rs;
    long long achi, acli;
    long long acho, aclo;
    long long resh, resl;

    achi = 0x1;
    acli = 0x1;
    rs = 0x0001000100010001;
    rt = 0x0002000200020002;
    resh = 0x1;
    resl = 0x11;

    __asm
        ("mthi %2, $ac1\t\n"
         "mtlo %3, $ac1\t\n"
         "dpaq_s.w.qh $ac1, %4, %5\t\n"
         "mfhi %0, $ac1\t\n"
         "mflo %1, $ac1\t\n"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    if ((acho != resh) || (aclo != resl)) {
        printf("1 dpaq_s.w.qh error\n");

        return -1;
    }

    achi = 0xffffffff;
    acli = 0xaaaaaaaa;

    rs = 0x1111222233334444;
    rt = 0xffffeeeeddddcccc;

    resh = 0x00;
    resl = 0xffffffffd27ad82e;

    __asm
        ("mthi %2, $ac1\t\n"
         "mtlo %3, $ac1\t\n"
         "dpaq_s.w.qh $ac1, %4, %5\t\n"
         "mfhi %0, $ac1\t\n"
         "mflo %1, $ac1\t\n"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );

    if ((acho != resh) || (aclo != resl)) {
        printf("2 dpaq_s.w.qh error\n");

        return -1;
    }

    return 0;
}
