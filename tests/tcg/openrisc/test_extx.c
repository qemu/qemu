#include <stdio.h>

int main(void)
{
    int a, b;
    int result;

    b = 0x83;
    result = 0xffffff83;
    __asm
    ("l.extbs  %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("extbs error\n");
        return -1;
    }

    result = 0x83;
    __asm
    ("l.extbz  %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("extbz error\n");
        return -1;
    }

    b = 0x8083;
    result = 0xffff8083;
    __asm
    ("l.exths  %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("exths error\n");
        return -1;
    }

    result = 0x8083;
    __asm
    ("l.exthz  %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("exthz error\n");
        return -1;
    }

    b = 0x11;
    result = 0x11;
    __asm
    ("l.extws  %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );

    if (a != result) {
        printf("extws error\n");
        return -1;
    }

    __asm
    ("l.extwz  %0, %1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("extwz error\n");
        return -1;
    }

    return 0;
}
