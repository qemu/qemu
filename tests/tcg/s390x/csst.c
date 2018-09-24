#include <stdint.h>
#include <unistd.h>

int main(void)
{
    uint64_t parmlist[] = {
        0xfedcba9876543210ull,
        0,
        0x7777777777777777ull,
        0,
    };
    uint64_t op1 = 0x0123456789abcdefull;
    uint64_t op2 = 0;
    uint64_t op3 = op1;
    uint64_t cc;

    asm volatile(
        "    lghi %%r0,%[flags]\n"
        "    la %%r1,%[parmlist]\n"
        "    csst %[op1],%[op2],%[op3]\n"
        "    ipm %[cc]\n"
        : [op1] "+m" (op1),
          [op2] "+m" (op2),
          [op3] "+r" (op3),
          [cc] "=r" (cc)
        : [flags] "K" (0x0301),
          [parmlist] "m" (parmlist)
        : "r0", "r1", "cc", "memory");
    cc = (cc >> 28) & 3;
    if (cc) {
        write(1, "bad cc\n", 7);
        return 1;
    }
    if (op1 != parmlist[0]) {
        write(1, "bad op1\n", 8);
        return 1;
    }
    if (op2 != parmlist[2]) {
        write(1, "bad op2\n", 8);
        return 1;
    }
    return 0;
}
