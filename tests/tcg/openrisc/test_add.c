#include <stdio.h>

int main(void)
{
    int a, b, d;
    int result;

    a = 0x100;
    b = 0x100;
    result = 0x200;
    __asm
    ("l.add %0, %0, %1\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("add error\n");
        return -1;
    }

    a = 0xffff;
    b = 0x1;
    result = 0x10000;
    __asm
    ("l.add %0, %0, %1\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("add error\n");
        return -1;
    }

    a = 0x7fffffff;
    b = 0x1;
    __asm
    ("l.add %0, %1, %2\n\t"
     : "=r"(d)
     : "r"(b), "r"(a)
    );

    return 0;
}
