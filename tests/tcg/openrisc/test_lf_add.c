#include <stdio.h>

int main(void)
{
    float a, b;
    float res2;

    a = 1.5;
    b = 2.5;
    res2 = 4.0;
    __asm
    ("lf.add.s  %0, %0, %1\n\t"
     : "+r"(a)
     : "r"(b)
    );
    if (a != res2) {
        printf("lf.add.s error, %f\n", a);
        return -1;
    }

/*    double c, d;
    double res1;

    c = 1.5;
    d = 1.5;
    res1 = 3.00;
    __asm
    ("lf.add.d  %0, %1, %2\n\t"
     : "+r"(c)
     : "r"(d)
    );

    if ((e - res1) > 0.002) {
        printf("lf.add.d error, %f\n", e - res1);
        return -1;
    }*/

    return 0;
}
