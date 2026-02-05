#include <stdio.h>

int main(void)
{
    int a;
    int result;

    a = 0;
    result = 2;
    __asm
    ("l.addi %0, %0, 1\n\t"
     "l.j j\n\t"
     "l.nop\n\t"
     "l.addi %0, %0, 1\n\t"
     "l.nop\n\t"
     "j:\n\t"
     "l.addi %0, %0, 1\n\t"
     : "+r"(a)
    );
    if (a != result) {
        printf("j error\n");
        return -1;
    }

    return 0;
}
