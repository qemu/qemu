#include <stdio.h>

int main(void)
{
    int a, b, c;
    int result;

    b = 0x9743;
    c = 0x2;
    result = 0x25d0c;
    __asm
    ("l.sll    %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("sll error\n");
        return -1;
    }

    b = 0x9743;
    result = 0x25d0c;
    __asm
    ("l.slli   %0, %1, 0x2\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("slli error\n");
        return -1;
    }

    b = 0x7654;
    c = 0x03;
    result = 0xeca;
    __asm
    ("l.srl    %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );

    b = 0x7654;
    result = 0xeca;
    __asm
    ("l.srli   %0, %1, 0x3\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("srli error\n");
        return -1;
    }

    b = 0x80000001;
    c = 0x4;
    result = 0x18000000;
    __asm
    ("l.ror    %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("ror error\n");
        return -1;
    }

    b = 0x80000001;
    result = 0x18000000;
    __asm
    ("l.rori   %0, %1, 0x4\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("rori error\n");
        return -1;
    }

    b = 0x80000001;
    c = 0x03;
    result = 0xf0000000;
    __asm
    ("l.sra    %0, %1, %2\n\t"
     : "=r"(a)
     : "r"(b), "r"(c)
    );
    if (a != result) {
        printf("sra error\n");
        return -1;
    }

    b = 0x80000001;
    result = 0xf0000000;
    __asm
    ("l.srai   %0, %1, 0x3\n\t"
     : "=r"(a)
     : "r"(b)
    );
    if (a != result) {
        printf("srai error\n");
        return -1;
    }

    return 0;
}
