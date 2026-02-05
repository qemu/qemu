#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    b = 0x120;
    c = 0x4;
    result = 0x48;

    __asm
    ("l.divu  %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("divu error\n");
        return -1;
    }

    result = 0x4;
    __asm
    ("l.divu %0, %1, %0\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("divu error\n");
        return -1;
    }

    return 0;
}
