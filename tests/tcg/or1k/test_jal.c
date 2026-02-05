#include <stdio.h>

int main(void)
{
    int a;
    int result;

    a = 0;
    result = 2;
    __asm
    ("l.addi %0, %0, 1\n\t"
     "l.jal jal\n\t"
     "l.nop\n\t"
     "l.addi %0, %0, 1\n\t"
     "l.nop\n\t"
     "jal:\n\t"
     "l.addi %0, %0, 1\n\t"
     : "+r"(a)
    );
    if (a != result) {
        printf("jal error\n");
        return -1;
    }

    return 0;
}
