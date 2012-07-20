#include <stdio.h>

int main(void)
{
    int a, b;
    int result;

    b = 0x01;
    result = 0x00;
    __asm
    ("l.addi  %0, %1, 0xffff\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("addi error\n\t");
        return -1;
    }

    b = 0x010000;
    result = 0xffff;
    __asm
    ("l.addi  %0, %1, 0xffff\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("addi error\n");
        return -1;
    }

    return 0;
}
