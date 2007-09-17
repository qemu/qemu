#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <math.h>

int main (void)
{
    double d;
    uint8_t n;
    int i;

    printf("static const uint8_t mfrom_ROM_table[602] =\n{\n    ");
    for (i = 0; i < 602; i++) {
        /* Extremly decomposed:
         *                    -T0 / 256
         * T0 = 256 * log10(10          + 1.0) + 0.5
         */
        d = -i;
        d /= 256.0;
        d = exp10(d);
        d += 1.0;
        d = log10(d);
        d *= 256;
        d += 0.5;
        n = d;
        printf("%3d, ", n);
        if ((i & 7) == 7)
            printf("\n    ");
    }
    printf("\n};\n");

    return 0;
}
