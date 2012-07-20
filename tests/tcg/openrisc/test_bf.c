#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    a = 0;
    b = 10;
    c = 11;
    result = 0x2;
    __asm
    ("1:\n\t"
     "l.addi %1, %1, 0x01\n\t"
     "l.addi %0, %0, 0x01\n\t"
     "l.sfeq %1, %2\n\t"
     "l.bf   1b\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("sfeq error\n");
        return -1;
    }

    a = 0x00;
    b = 0x11;
    c = 0x11;
    result = 0x01;
    __asm
    ("1:\n\t"
     "l.addi %1, %1, 0x01\n\t"
     "l.addi %0, %0, 0x01\n\t"
     "l.sfeq %1, %2\n\t"
     "l.bf   1b\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("sfeq error\n");
        return -1;
    }

    return 0;
}
