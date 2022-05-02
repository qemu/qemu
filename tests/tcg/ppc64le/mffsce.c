#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#define MTFSF(FLM, FRB) asm volatile ("mtfsf %0, %1" :: "i" (FLM), "f" (FRB))
#define MFFS(FRT) asm("mffs %0" : "=f" (FRT))
#define MFFSCE(FRT) asm("mffsce %0" : "=f" (FRT))

#define PPC_BIT_NR(nr) (63 - (nr))

#define FP_VE (1ull << PPC_BIT_NR(56))
#define FP_OE (1ull << PPC_BIT_NR(57))
#define FP_UE (1ull << PPC_BIT_NR(58))
#define FP_ZE (1ull << PPC_BIT_NR(59))
#define FP_XE (1ull << PPC_BIT_NR(60))
#define FP_NI (1ull << PPC_BIT_NR(61))
#define FP_RN0 (1ull << PPC_BIT_NR(62))
#define FP_RN1 (1ull << PPC_BIT_NR(63))

int main(void)
{
    uint64_t frt, fpscr;
    uint64_t last_8_bits = FP_VE | FP_UE | FP_ZE |
                           FP_XE | FP_NI | FP_RN1;
    MTFSF(0b11111111, last_8_bits); // set test value to cpu fpscr
    MFFSCE(frt);
    MFFS(fpscr);

    // in the returned value
    // should be as the cpu fpscr was before
    assert((frt & FP_VE) != 0);
    assert((frt & FP_OE) == 0);
    assert((frt & FP_UE) != 0);
    assert((frt & FP_ZE) != 0);
    assert((frt & FP_XE) != 0);
    assert((frt & FP_NI) != 0);
    assert((frt & FP_RN0) == 0);
    assert((frt & FP_RN1) != 0);

    // in the cpu fpscr
    // last 3 bits should be unchanged
    assert((fpscr & FP_NI) != 0);
    assert((fpscr & FP_RN0) == 0);
    assert((fpscr & FP_RN1) != 0);

    // enable bits should be unset
    assert((fpscr & FP_VE) == 0);
    assert((fpscr & FP_OE) == 0);
    assert((fpscr & FP_UE) == 0);
    assert((fpscr & FP_ZE) == 0);
    assert((fpscr & FP_XE) == 0);

    return 0;
}
