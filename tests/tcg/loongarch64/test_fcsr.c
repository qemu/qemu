#include <assert.h>

int main()
{
    unsigned fcsr;

    asm("movgr2fcsr $r0,$r0\n\t"
        "movgr2fr.d $f0,$r0\n\t"
        "fdiv.d     $f0,$f0,$f0\n\t"
        "movfcsr2gr %0,$r0"
        : "=r"(fcsr) : : "f0");

    assert(fcsr & (16 << 16)); /* Invalid */
    return 0;
}
