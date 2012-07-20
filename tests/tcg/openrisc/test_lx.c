#include <stdio.h>

int main(void)
{
    int a;
    int p[50];
    int result;

    result = 0x23;
    __asm
    ("l.ori r8, r0, 0x123\n\t"
     "l.sb  0x4 + %1, r8\n\t"
     "\n\t"
     "l.lbz %0, 0x4 + %1\n\t"
     : "=r"(a), "+m"(*p)
    );
    if (a != result) {
        printf("lbz error, %x\n", a);
        return -1;
    }

    result = 0x23;
    __asm
    ("l.lbs %0, 0x4 + %1\n\t"
     : "=r"(a)
     : "m"(*p)
    );
    if (a != result) {
        printf("lbs error\n");
        return -1;
    }

    result = 0x1111;
    __asm
    ("l.ori r8, r0, 0x1111\n\t"
     "l.sh  0x20 + %1, r8\n\t"
     "\n\t"
     "l.lhs %0, 0x20 + %1\n\t"
     : "=r"(a), "=m"(*p)
    );
    if (a != result) {
        printf("lhs error, %x\n", a);
        return -1;
    }

    result = 0x1111;
    __asm
    ("l.lhz %0, 0x20 + %1\n\t"
     : "=r"(a)
     : "m"(*p)
    );
    if (a != result) {
        printf("lhz error\n");
        return -1;
    }

    result = 0x1111233;
    __asm
    ("l.ori r8, r0, 0x1233\n\t"
     "l.movhi r1, 0x111\n\t"
     "l.or  r8, r8, r1\n\t"
     "l.sw  0x123 + %1, r8\n\t"
     "\n\t"
     "l.lws %0, 0x123 + %1\n\t"
     : "=r"(a), "+m"(*p)
    );
    if (a != result) {
        printf("lws error, %x\n", a);
        return -1;
    }

    result = 0x1111233;
    __asm
    ("l.lwz %0, 0x123 + %1\n\t"
     : "=r"(a)
     : "m"(*p)
    );
    if (a != result) {
        printf("lwz error\n");
        return -1;
    }

    return 0;
}
