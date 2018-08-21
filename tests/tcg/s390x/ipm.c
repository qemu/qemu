#include <stdint.h>
#include <unistd.h>

int main(void)
{
    uint32_t op1 = 0x55555555;
    uint32_t op2 = 0x44444444;
    uint64_t cc = 0xffffffffffffffffull;

    asm volatile(
        "    clc 0(4,%[op1]),0(%[op2])\n"
        "    ipm %[cc]\n"
        : [cc] "+r" (cc)
        : [op1] "r" (&op1),
          [op2] "r" (&op2)
        : "cc");
    if (cc != 0xffffffff20ffffffull) {
        write(1, "bad cc\n", 7);
        return 1;
    }
    return 0;
}
