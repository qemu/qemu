#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    b = 0x120;
    c = 0x4;
    result = 0x48;
    __asm
    ("l.div  %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("div error\n");
        return -1;
    }

    result = 0x4;
    __asm
    ("l.div %0, %1, %0\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("div error\n");
        return -1;
    }

    b = 0xffffffff;
    c = 0x80000000;
    result = 0;
    __asm
    ("l.div %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("div error\n");
        return -1;
    }

    b = 0x80000000;
    c = 0xffffffff;
    __asm
    ("l.div %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );

    return 0;
}
