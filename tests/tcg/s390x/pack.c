#include <unistd.h>

int main(void)
{
    char data[] = {0xaa, 0xaa, 0xf1, 0xf2, 0xf3, 0xc4, 0xaa, 0xaa};
    char exp[] = {0xaa, 0xaa, 0x00, 0x01, 0x23, 0x4c, 0xaa, 0xaa};
    int i;

    asm volatile(
        "    pack 2(4,%[data]),2(4,%[data])\n"
        :
        : [data] "r" (&data[0])
        : "memory");
    for (i = 0; i < 8; i++) {
        if (data[i] != exp[i]) {
            write(1, "bad data\n", 9);
            return 1;
        }
    }
    return 0;
}
