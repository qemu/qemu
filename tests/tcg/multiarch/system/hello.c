/*
 * Hello World, system test version
 *
 * We don't have the benefit of libc, just builtin C primitives and
 * whatever is in minilib.
 */

#include <minilib.h>

int main(void)
{
    ml_printf("Hello World\n");
    return 0;
}
