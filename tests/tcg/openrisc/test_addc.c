#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    b = 0x01;
    c = 0xffffffff;
    result = 1;
    __asm
    ("l.addc   %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("first addc error\n");
        return -1;
    }

    b = 0x01;
    c = 0xffffffff;
    result = 0x80000001;
    __asm
    ("l.addc   %0, %1, %2\n\t"
     "l.movhi  %2, 0x7fff\n\t"
     "l.ori    %2, %2, 0xffff\n\t"
     "l.addc   %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("addc error\n");
        return -1;
    }

    return 0;
}
