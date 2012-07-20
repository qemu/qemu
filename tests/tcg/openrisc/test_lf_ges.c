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
    ("lfges:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfge.s %1, %2\n\t"
     "l.bf      lfges\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfge.s error\n");
        return -1;
    }

    b = 133.5;
    c = 13.5;
    result = 0x3;
    __asm
    ("l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfge.s %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "1:\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfge.s error\n");
        return -1;
    }

/*    int a, result;
    double b, c;

    a = 0x1;
    b = 122.5;
    c = 123.5;
    result = 0x2;
    __asm
    ("lfged:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfge.d %1, %2\n\t"
     "l.bf      lfged\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfge.d error\n");
        return -1;
    }

    b = 133.5;
    c = 13.5;
    result = 0x4;
    __asm
    ("lf.sfge.d %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "1:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfge.d error\n");
        return -1;
    }*/

    return 0;
}
