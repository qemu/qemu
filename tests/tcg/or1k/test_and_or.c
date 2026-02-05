#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    b = 0x2;
    c = 0x1;
    result = 0;
    __asm
    ("l.and  %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("and error\n");
        return -1;
    }

    result = 0x2;
    __asm
    ("l.andi  %0, %1, 0x3\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("andi error %x\n", a);
        return -1;
    }

    result = 0x3;
    __asm
    ("l.or   %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("or error\n");
        return -1;
    }

    result = 0x3;
    __asm
    ("l.xor  %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("xor error\n");
        return -1;
    }

    __asm
    ("l.xori  %0, %1, 0x1\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("xori error\n");
        return -1;
    }

    return 0;
}
