#include <stdint.h>
#include <unistd.h>

int main(void)
{
    char op1[] = {0, 1, 2, 3};
    char op2[256];
    uint64_t r1 = 0xffffffffffffffffull;
    uint64_t r2 = 0xffffffffffffffffull;
    uint64_t cc;
    int i;

    for (i = 0; i < 256; i++) {
        if (i == 1) {
            op2[i] = 0xbb;
        } else {
            op2[i] = 0;
        }
    }
    asm volatile(
        "    j 2f\n"
        "1:  trtr 3(1,%[op1]),0(%[op2])\n"
        "2:  exrl %[op1_len],1b\n"
        "    lgr %[r1],%%r1\n"
        "    lgr %[r2],%%r2\n"
        "    ipm %[cc]\n"
        : [r1] "+r" (r1),
          [r2] "+r" (r2),
          [cc] "=r" (cc)
        : [op1] "r" (&op1),
          [op1_len] "r" (3),
          [op2] "r" (&op2)
        : "r1", "r2", "cc");
    cc = (cc >> 28) & 3;
    if (cc != 1) {
        write(1, "bad cc\n", 7);
        return 1;
    }
    if ((char *)r1 != &op1[1]) {
        write(1, "bad r1\n", 7);
        return 1;
    }
    if (r2 != 0xffffffffffffffbbull) {
        write(1, "bad r2\n", 7);
        return 1;
    }
    return 0;
}
