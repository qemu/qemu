#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    a = 1;
    result = 0x1;
    __asm
    ("l.addic %0, %0, 0xffff\n\t"
     : "+r"(a)
    );
    if (a != result) {
        printf("first addic error\n");
        return -1;
   }

    a = 0x1;
    result = 0x201;
    __asm
    ("l.addic %0, %0, 0xffff\n\t"
     "l.ori   %0, r0, 0x100\n\t"
     "l.addic %0, %0, 0x100\n\t"
     : "+r"(a)
    );
    if (a != result) {
        printf("second addic error\n");
        return -1;
    }

    return 0;
}
