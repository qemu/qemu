/*
 * External interruption test. This test is structured in such a way that it
 * passes the cases that require it to exit, but we can make it enter an
 * infinite loop from GDB.
 *
 * We don't have the benefit of libc, just builtin C primitives and
 * whatever is in minilib.
 */

#include <minilib.h>

void loop(void)
{
    do {
        /*
         * Loop forever. Just make sure the condition is always a constant
         * expression, so that this loop is not UB, as per the C
         * standard.
         */
    } while (1);
}

int main(void)
{
    return 0;
}


