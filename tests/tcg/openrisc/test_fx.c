#include <stdio.h>

int main(void)
{
    int a, b;
    int result;

    b = 0x123;
    result = 1;
    __asm
    ("l.ff1 %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("ff1 error\n");
        return -1;
    }

    b = 0x0;
    result = 0;
    __asm
    ("l.ff1 %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("ff1 error\n");
        return -1;
    }

    b = 0x123;
    result = 9;
    __asm
    ("l.fl1 %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("fl1 error\n");
        return -1;
    }

    b = 0x0;
    result = 0;
    __asm
    ("l.fl1 %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("fl1 error\n");
        return -1;
    }

    return 0;
}
