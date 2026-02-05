#include <stdio.h>

int main(void)
{
    int a, result;
    float b, c;

    a = 0;
    b = 122.5;
    c = 123.5;
    result = 0x1;
    __asm
    ("lfgts:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfgt.s %1, %2\n\t"
     "l.bf      lfgts\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfgt.s error\n");
        return -1;
    }

    b = 133.5;
    c = 13.5;
    result = 0x1;
    __asm
    ("lf.sfgt.s %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "1:\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfgt.s error\n");
        return -1;
    }

/*    int a, result;
    double b, c;

    a = 0;
    b = 122.5;
    c = 123.5;
    result = 0x1;
    __asm
    ("lfgtd:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfgt.d %1, %2\n\t"
     "l.bf      lfgtd\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfgt.d error\n");
        return -1;
    }

    b = 133.5;
    c = 13.5;
    result = 0x3;
    __asm
    ("l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfgt.d %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "1:\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfgt.d error, %x\n", a);
        return -1;
    }*/

    return 0;
}
