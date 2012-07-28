#include <stdio.h>
int main(void)
{
    int a, b, c;
    int result;

    a = 0;
    b = 3;
    result = 1;
    __asm
    ("1:\n\t"
     "l.addi %0, %0, 1\n\t"
     "l.sfgtu %0, %1\n\t"
     "l.bf 1b\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("sfgtu error\n");
        return -1;
    }

    a = 0xff;
    b = 3;
    c = 1;
    result = 3;
    __asm
    ("1:\n\t"
     "l.sub    %0, %0, %2\n\t"
     "l.sfgtu  %0, %1\n\t"
     "l.bf 1b\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("sfgtu error\n");
        return -1;
    }

    return 0;
}
