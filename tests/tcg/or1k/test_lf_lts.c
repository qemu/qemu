#include <stdio.h>

int main(void)
{
    int a;
    float b, c, d;
    int result;

    a = 0;
    b = 124.5;
    c = 1.4;
    result = 1;
    __asm
    ("lfltd:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sflt.s %1, %2\n\t"
     "l.bf      lfltd\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sflt.s error\n");
        return -1;
    }

    a = 0;
    b = 11.1;
    c = 13.1;
    d = 1.0;
    result = 2;
    __asm
    ("1:\n\t"
     "lf.add.s  %1, %1, %3\n\t"
     "l.addi    %0, %0, 1\n\t"
     "lf.sflt.s %1, %2\n\t"
     "l.bf      1b\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c), "r"(d)
    );
    if (a != result) {
        printf("lf.sflt.s error\n");
        return -1;
    }

/*    int a;
    double b, c;
    int result;

    a = 0;
    b = 1432.1;
    c = 2.4;
    result = 0x1;
    __asm
    ("lfltd:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sflt.d %1, %2\n\t"
     "l.bf      lfltd\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sflt.d error\n");
        return -1;
    }

    a = 0;
    b = 1.1;
    c = 19.7;
    result = 2;
    __asm
    ("lf.sflt.d %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi %0, %0, 1\n\t"
     "l.addi %0, %0, 1\n\t"
     "l.addi %0, %0, 1\n\t"
     "1:\n\t"
     "l.addi %0, %0, 1\n\t"
     "l.addi %0, %0, 1\n\t"
     : "+r"(a), "+r"(b)
     : "r"(c)
    );
    if (a != result) {
        printf("lf.sflt.d error\n");
        return -1;
    }*/

    return 0;
}
