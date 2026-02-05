#include <stdio.h>

int main(void)
{
    int a;
    int result;

    a = 1;
    result = 2;
    __asm
    ("1:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "l.sfeqi   %0, 0x80\n\t"
     "l.bf      1b\n\t"
     "l.nop\n\t"
     : "+r"(a)
    );
    if (a != result) {
        printf("sfeqi error\n");
        return -1;
    }

    a = 0x7f;
    result = 0x81;
    __asm
    ("2:\n\t"
     "l.addi    %0, %0, 0x1\n\t"
     "l.sfeqi   %0, 0x80\n\t"
     "l.bf      2b\n\t"
     "l.nop\n\t"
     : "+r"(a)
    );
    if (a != result) {
        printf("sfeqi error\n");
        return -1;
    }

    return 0;
}
