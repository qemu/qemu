#include <stdio.h>

int main(void)
{
    int a, result;
    float b, c;

    a = 0x1;
    b = 122.5;
    c = 123.5;
    result = 0x3;
    __asm
    ("lfeqd:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfeq.s %1, %2\n\t"
     "l.bf      lfeqd\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfeq.s error\n");
        return -1;
    }

    b = 13.5;
    c = 13.5;
    result = 0x3;
    __asm
    ("lf.sfeq.s %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    r4, r4, 0x1\n\t"
     "1:\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfeq.s error\n");
        return -1;
    }

/*    double b, c;
    double result;
    int a;

    a = 0x1;
    b = 122.5;
    c = 133.5;
    result = 0x3;

    __asm
    ("lfeqd:\n\t"
     "l.addi %0, %0, 0x1\n\t"
     "lf.sfeq.d %1, %2\n\t"
     "l.bf      lfeqd\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfeq.d error\n");
        return -1;
    }

    double c, d, res;
    int e = 0;
    c = 11.5;
    d = 11.5;
    res = 1;
    __asm
    ("lf.sfeq.d %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "1:\n\t"
     : "+r"(e)
     : "r"(c), "r"(d)
    );
    if (e != res) {
        printf("lf.sfeq.d error\n");
        return -1;
    }*/

    return 0;
}
