#include <stdint.h>
#include <assert.h>

asm(".arch armv8.4-a");

void do_test(uint64_t value)
{
    uint64_t salt1, salt2;
    uint64_t encode, decode;

    /*
     * With TBI enabled and a 48-bit VA, there are 7 bits of auth,
     * and so a 1/128 chance of encode = pac(value,key,salt) producing
     * an auth for which leaves value unchanged.
     * Iterate until we find a salt for which encode != value.
     */
    for (salt1 = 1; ; salt1++) {
        asm volatile("pacda %0, %2" : "=r"(encode) : "0"(value), "r"(salt1));
        if (encode != value) {
            break;
        }
    }

    /* A valid salt must produce a valid authorization.  */
    asm volatile("autda %0, %2" : "=r"(decode) : "0"(encode), "r"(salt1));
    assert(decode == value);

    /*
     * An invalid salt usually fails authorization, but again there
     * is a chance of choosing another salt that works.
     * Iterate until we find another salt which does fail.
     */
    for (salt2 = salt1 + 1; ; salt2++) {
        asm volatile("autda %0, %2" : "=r"(decode) : "0"(encode), "r"(salt2));
        if (decode != value) {
            break;
        }
    }

    /* The VA bits, bit 55, and the TBI bits, should be unchanged.  */
    assert(((decode ^ value) & 0xff80ffffffffffffull) == 0);

    /*
     * Bits [54:53] are an error indicator based on the key used;
     * the DA key above is keynumber 0, so error == 0b01.  Otherwise
     * bit 55 of the original is sign-extended into the rest of the auth.
     */
    if ((value >> 55) & 1) {
        assert(((decode >> 48) & 0xff) == 0b10111111);
    } else {
        assert(((decode >> 48) & 0xff) == 0b00100000);
    }
}

int main()
{
    do_test(0);
    do_test(-1);
    do_test(0xda004acedeadbeefull);
    return 0;
}
