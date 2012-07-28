#include <stdio.h>

int main(void)
{
    int a, b;
    int result;

    a = 0;
    b = 3;
    result = 4;
    __asm
    ("1:\n\t"
     "l.addi   %0, %0, 4\n\t"
     "l.sfles  %0, %1\n\t"
     "l.bf 1b\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("sfles error\n");
        return -1;
    }

    return 0;
}
