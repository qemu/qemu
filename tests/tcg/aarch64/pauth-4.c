#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define TESTS 1000

int main()
{
    int i, count = 0;
    float perc;
    void *base = malloc(TESTS);

    for (i = 0; i < TESTS; i++) {
        uintptr_t in, x, y;

        in = i + (uintptr_t) base;

        asm("mov %0, %[in]\n\t"
            "pacia %0, sp\n\t"        /* sigill if pauth not supported */
            "eor %0, %0, #4\n\t"      /* corrupt single bit */
            "mov %1, %0\n\t"
            "autia %1, sp\n\t"        /* validate corrupted pointer */
            "xpaci %0\n\t"            /* strip pac from corrupted pointer */
            : /* out */ "=r"(x), "=r"(y)
            : /* in */ [in] "r" (in)
            : /* clobbers */);

        /*
         * Once stripped, the corrupted pointer is of the form 0x0000...wxyz.
         * We expect the autia to indicate failure, producing a pointer of the
         * form 0x000e....wxyz.  Use xpaci and != for the test, rather than
         * extracting explicit bits from the top, because the location of the
         * error code "e" depends on the configuration of virtual memory.
         */
        if (x != y) {
            count++;
        }

    }
    perc = (float) count / (float) TESTS;
    printf("Checks Passed: %0.2f%%", perc * 100.0);
    assert(perc > 0.95);
    return 0;
}
