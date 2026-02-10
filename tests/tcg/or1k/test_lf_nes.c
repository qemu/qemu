#include <stdio.h>

int main(void)
{
    int a;
    float b, c;
    int result;

    a = 0;
    b = 23.1;
    c = 23.1;
    result = 0x1;
    __asm
    ("lfnes:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfne.s %1, %2\n\t"
     "l.bf      lfnes\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfne.s error");
        return -1;
    }

    b = 12.4;
    c = 7.8;
    result = 0x3;
    __asm
    ("l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfne.s %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "1:\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfne.s error\n");
        return -1;
    }
/*    int a;
    double b, c;
    int result;

    a = 0;
    b = 124.3;
    c = 124.3;
    result = 0x1;
    __asm
    ("lfned:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfne.d %1, %2\n\t"
     "l.bf      lfned\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfne.d error\n");
        return -1;
    }

    b = 11.5;
    c = 16.7;
    result = 0x3;
    __asm
    ("l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfne.d %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    r4, r4, 0x1\n\t"
     "l.addi    r4, r4, 0x1\n\t"
     "1:\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfne.d error\n");
        return -1;
    }*/

    return 0;
}
