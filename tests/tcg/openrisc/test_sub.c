#include <stdio.h>

int main(void)
{
    int a, b;
    int result;

    a = 0x100;
    b = 0x100;
    result = 0x0;
    __asm
    ("l.sub %0, %0, %1\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("sub error\n");
        return -1;
    }

    a = 0xffff;
    b = 0x1;
    result = 0xfffe;
    __asm
    ("l.sub %0, %0, %1\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("sub error\n");
        return -1;
    }

    return 0;
}
