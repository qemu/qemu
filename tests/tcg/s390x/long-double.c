/*
 * Perform some basic arithmetic with long double, as a sanity check.
 * With small integral numbers, we can cross-check with integers.
 */

#include <assert.h>

int main()
{
    int i, j;

    for (i = 1; i < 5; i++) {
        for (j = 1; j < 5; j++) {
            long double la = (long double)i + j;
            long double lm = (long double)i * j;
            long double ls = (long double)i - j;

            assert(la == i + j);
            assert(lm == i * j);
            assert(ls == i - j);
        }
    }
    return 0;
}
