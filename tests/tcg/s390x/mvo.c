#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint8_t dest[6] = {0xff, 0x77, 0x88, 0x99, 0x0c, 0xff};
    uint8_t src[5] = {0xee, 0x12, 0x34, 0x56, 0xee};
    uint8_t expected[6] = {0xff, 0x01, 0x23, 0x45, 0x6c, 0xff};
    int i;

    asm volatile (
        "    mvo 0(4,%[dest]),0(3,%[src])\n"
        :
        : [dest] "a" (dest + 1),
          [src] "a" (src + 1)
        : "memory");

    for (i = 0; i < sizeof(expected); i++) {
        if (dest[i] != expected[i]) {
            fprintf(stderr, "bad data\n");
            return 1;
        }
    }
    return 0;
}
