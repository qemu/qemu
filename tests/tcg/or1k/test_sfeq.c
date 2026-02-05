#include <stdio.h>

int main(void)
{
    int a, b;
    int result;

    a = 0x1;
    b = 0x80;
    result = 0x2;
    __asm
    ("1:\n\t"
     "l.addi   %0, %0, 0x1\n\t"
     "l.sfeq   %0, %1\n\t"
     "l.bf     1b\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("sfeq error\n");
        return -1;
    }

    a = 0x7f;
    b = 0x80;
    result = 0x81;
    __asm
    ("2:\n\t"
     "l.addi   %0, %0, 0x1\n\t"
     "l.sfeq   %0, %1\n\t"
     "l.bf     2b\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("sfeq error\n");
        return -1;
    }

    return 0;
}
