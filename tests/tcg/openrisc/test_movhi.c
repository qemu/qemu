#include <stdio.h>

int main(void)
{
    int a;
    int result;

    result = 0x1222;
    __asm
    ("l.movhi r3, 0x1222\n\t"
     "l.srli   %0, r3, 16\n\t"
     : "=r"(a)
    );
    if (a != result) {
        printf("movhi error\n");
        return -1;
    }

    result = 0x1111;
    __asm
    ("l.movhi r8, 0x1111\n\t"
     "l.srli   %0, r8, 16\n\t"
     : "=r"(a)
    );
    if (a != result) {
        printf("movhi error\n");
        return -1;
    }

    return 0;
}
