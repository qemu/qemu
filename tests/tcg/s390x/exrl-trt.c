#include <stdint.h>
#include <unistd.h>

int main(void)
{
    char op1[] = "hello";
    char op2[256];
    register uint64_t r1 asm("r1") = 0xffffffffffffffffull;
    register uint64_t r2 asm("r2") = 0xffffffffffffffffull;
    uint64_t cc;
    int i;

    for (i = 0; i < 256; i++) {
        if (i == 0) {
            op2[i] = 0xaa;
        } else {
            op2[i] = 0;
        }
    }
    asm volatile(
        "    j 2f\n"
        "1:  trt 0(1,%[op1]),%[op2]\n"
        "2:  exrl %[op1_len],1b\n"
        "    ipm %[cc]\n"
        : [r1] "+r" (r1),
          [r2] "+r" (r2),
          [cc] "=r" (cc)
        : [op1] "a" (&op1),
          [op1_len] "a" (5),
          [op2] "Q" (op2)
        : "cc");
    cc = (cc >> 28) & 3;
    if (cc != 2) {
        write(1, "bad cc\n", 7);
        return 1;
    }
    if ((char *)r1 != &op1[5]) {
        write(1, "bad r1\n", 7);
        return 1;
    }
    if (r2 != 0xffffffffffffffaaull) {
        write(1, "bad r2\n", 7);
        return 1;
    }
    return 0;
}
