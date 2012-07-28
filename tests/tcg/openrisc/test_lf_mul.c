#include <stdio.h>

int main(void)
{
    float a, b, c;
    float result;

    b = 1.5;
    c = 4.0;
    result = 6.0;
    __asm
    ("lf.mul.s   %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("lf.mul.s error\n");
        return -1;
    }

    return 0;
}
