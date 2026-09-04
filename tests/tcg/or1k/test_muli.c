#include <stdio.h>

int main(void)
{
    int a, b;
    int result;

    b = 0x4;
    result = 0x4;
    __asm
    ("l.muli    %0, %1, 0x1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("muli error\n");
        return -1;
    }

    b = 0x1;
    result = 0x0;
    __asm
    ("l.muli    %0, %1, 0x0\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("muli error\n");
        return -1;
    }

    b = 0x1;
    result = 0xff;
    __asm
    ("l.muli    %0, %1, 0xff\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("muli error\n");
        return -1;
    }

    return 0;
}
