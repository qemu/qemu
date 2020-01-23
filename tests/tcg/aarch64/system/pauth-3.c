#include <inttypes.h>
#include <minilib.h>

int main()
{
    /*
     * Test vector from QARMA paper (https://eprint.iacr.org/2016/444.pdf)
     * to verify one computation of the pauth_computepac() function,
     * which uses sbox2.
     *
     * Use PACGA, because it returns the most bits from ComputePAC.
     * We still only get the most significant 32-bits of the result.
     */

    static const uint64_t d[5] = {
        0xfb623599da6e8127ull,
        0x477d469dec0b8762ull,
        0x84be85ce9804e94bull,
        0xec2802d4e0a488e9ull,
        0xc003b93999b33765ull & 0xffffffff00000000ull
    };
    uint64_t r;

    asm("msr apgakeyhi_el1, %[w0]\n\t"
        "msr apgakeylo_el1, %[k0]\n\t"
        "pacga %[r], %[P], %[T]"
        : [r] "=r"(r)
        : [P] "r" (d[0]),
          [T] "r" (d[1]),
          [w0] "r" (d[2]),
          [k0] "r" (d[3]));

    if (r == d[4]) {
        ml_printf("OK\n");
        return 0;
    } else {
        ml_printf("FAIL: %lx != %lx\n", r, d[4]);
        return 1;
    }
}
