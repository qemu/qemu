#include <stdio.h>

int main(void)
{
    int a, b;
    int result;

    a = 0;
    b = 0;
    result = 0x3;
    __asm
    ("l.sfeqi %1, 0x0\n\t"
     "l.bnf 1f\n\t"
     "l.nop\n\t"
     "\n\t"
     "l.addi %0, %0, 0x1\n\t"
     "l.addi %0, %0, 0x1\n\t"
     "\n\t"
     "1:\n\t"
     "l.addi %0, %0, 0x1\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("l.bnf error\n");
        return -1;
    }

    a = 0;
    b = 0;
    result = 1;
    __asm
    ("l.sfeqi %1, 0x1\n\t"
     "l.bnf 1f\n\t"
     "l.nop\n\t"
     "\n\t"
     "l.addi %0, %0, 0x1\n\t"
     "l.addi %0, %0, 0x1\n\t"
     "\n\t"
     "1:\n\t"
     "l.addi %0, %0, 0x1\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("l.bnf error\n");
        return -1;
    }

    return 0;
}
