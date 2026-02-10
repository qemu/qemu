#include <stdio.h>

int main(void)
{
    int a;
    float b, c;
    int result;

    a = 0;
    b = 1234.2;
    c = 12.4;
    result = 0x1;
    __asm
    ("lfles:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfle.s %1, %2\n\t"
     "l.bf      lfles\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfle.s error\n");
        return -1;
    }

    b = 1.1;
    c = 19.4;
    result = 0x3;
    __asm
    ("l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfle.s %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "1:\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfle.s error\n");
        return -1;
    }

/*    int a;
    double b, c;
    int result;

    a = 0;
    b = 1212.5;
    c = 123.5;
    result = 0x1;
    __asm
    ("lfled:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "lf.sfle.d %1, %2\n\t"
     "l.bf      lfled\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfle.d error\n");
        return -1;
    }

    b = 13.5;
    c = 113.5;
    result = 0x2;
    __asm
    ("l.addi    %0, %0, 0x1\n\t"
     "lf.sfle.d %1, %2\n\t"
     "l.bf      1f\n\t"
     "l.nop\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "1:\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.sfle.d error\n");
        return -1;
    }*/

    return 0;
}
