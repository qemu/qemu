#include <stdio.h>

int main(void)
{
    int a;
    int result;

    a = 0;
    result = 3;
    __asm
    ("1:\n\t"
     "l.addi   %0, %0, 3\n\t"
     "l.sfnei  %0, 0x3\n\t"
     "l.bf 1b\n\t"
     "l.nop\n\t"
     : "+r"(a)
    );
    if (a != result) {
        printf("sfnei error\n");
        return -1;
    }

    a = 0;
    result = 3;
    __asm
    ("1:\n\t"
     "l.addi   %0, %0, 1\n\t"
     "l.sfnei  %0, 0x3\n\t"
     "l.bf 1b\n\t"
     "l.nop\n\t"
     : "+r"(a)
    );
    if (a != result) {
        printf("sfnei error\n");
        return -1;
    }

    return 0;
}
