/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This test target increments a value 100 times. The patcher converts the
 * inc instruction to a nop, so it only increments the value once.
 *
 */
#include <minilib.h>

int main(void)
{
    ml_printf("Running test...\n");
    unsigned int x = 0;
    for (int i = 0; i < 100; i++) {
        asm volatile (
            "inc %[x]"
            : [x] "+a" (x)
        );
    }
    ml_printf("Value: %d\n", x);
    return 0;
}
