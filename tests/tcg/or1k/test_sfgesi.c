#include <stdio.h>
int main(void)
{
    int a, b;
    int result;

    a = 0;
    result = 1;
    __asm
    ("1:\n\t"
     "l.addi   %0, %0, 1\n\t"
     "l.sfgesi %0, 0x3\n\t"
     "l.bf 1b\n\t"
     "l.nop\n\t"
     : "+r"(a)
    );
    if (a != result) {
        printf("sfgesi error\n");
        return -1;
    }

    a = 0xff;
    b = 1;
    result = 2;
    __asm
    ("1:\n\t"
     "l.sub    %0, %0, %1\n\t"
     "l.sfgesi %0, 0x3\n\t"
     "l.bf 1b\n\t"
     "l.nop\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("sfgesi error\n");
        return -1;
    }

    return 0;
}
