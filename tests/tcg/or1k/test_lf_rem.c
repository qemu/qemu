#include <stdio.h>

int main(void)
{
    float a, b, c;
    float result;

    b = 101.5;
    c = 10;
    result = 1.5;
/*    __asm
    ("lf.rem.d      %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.rem.d error\n");
        return -1;
    }*/

    __asm
    ("lf.rem.s      %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.rem.s error\n");
        return -1;
    }

    return 0;
}
