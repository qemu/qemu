#include <stdio.h>

#define FPCR_SUM                (1UL << 63)
#define FPCR_INED               (1UL << 62)
#define FPCR_UNFD               (1UL << 61)
#define FPCR_UNDZ               (1UL << 60)
#define FPCR_DYN_SHIFT          58
#define FPCR_DYN_CHOPPED        (0UL << FPCR_DYN_SHIFT)
#define FPCR_DYN_MINUS          (1UL << FPCR_DYN_SHIFT)
#define FPCR_DYN_NORMAL         (2UL << FPCR_DYN_SHIFT)
#define FPCR_DYN_PLUS           (3UL << FPCR_DYN_SHIFT)
#define FPCR_DYN_MASK           (3UL << FPCR_DYN_SHIFT)
#define FPCR_IOV                (1UL << 57)
#define FPCR_INE                (1UL << 56)
#define FPCR_UNF                (1UL << 55)
#define FPCR_OVF                (1UL << 54)
#define FPCR_DZE                (1UL << 53)
#define FPCR_INV                (1UL << 52)
#define FPCR_OVFD               (1UL << 51)
#define FPCR_DZED               (1UL << 50)
#define FPCR_INVD               (1UL << 49)
#define FPCR_DNZ                (1UL << 48)
#define FPCR_DNOD               (1UL << 47)
#define FPCR_STATUS_MASK        (FPCR_IOV | FPCR_INE | FPCR_UNF \
                                 | FPCR_OVF | FPCR_DZE | FPCR_INV)

static long test_cvttq(long *ret_e, double d)
{
    unsigned long reset = (FPCR_INED | FPCR_UNFD | FPCR_OVFD | FPCR_DZED |
                           FPCR_INVD | FPCR_DYN_NORMAL);
    long r, e;

    asm("excb\n\t"
        "mt_fpcr %3\n\t"
        "excb\n\t"
        "cvttq/svic %2, %0\n\t"
        "excb\n\t"
        "mf_fpcr %1\n\t"
        "excb\n\t"
        : "=f"(r), "=f"(e)
        : "f"(d), "f"(reset));

    *ret_e = e & FPCR_STATUS_MASK;
    return r;
}

int main (void)
{
    static const struct {
        double d;
        long r;
        long e;
    } T[] = {
        {  1.0,  1, 0 },
        { -1.0, -1, 0 },
        {  1.5,  1, FPCR_INE },
        {  0x1.0p32,   0x0000000100000000ul, 0 },
        { -0x1.0p63,   0x8000000000000000ul, 0 },
        {  0x1.0p63,   0x8000000000000000ul, FPCR_IOV | FPCR_INE },
        {  0x1.0p64,   0x0000000000000000ul, FPCR_IOV | FPCR_INE },
        {  0x1.cccp64, 0xccc0000000000000ul, FPCR_IOV | FPCR_INE },
        { __builtin_inf(), 0, FPCR_INV },
        { __builtin_nan(""), 0, FPCR_INV },
    };

    int i, err = 0;

    for (i = 0; i < sizeof(T)/sizeof(T[0]); i++) {
        long e, r = test_cvttq(&e, T[i].d);

        if (r != T[i].r || e != T[i].e) {
            printf("Fail %a: expect (%016lx : %04lx) got (%016lx : %04lx)\n",
                   T[i].d, T[i].r, T[i].e >> 48, r, e >> 48);
            err = 1;
        }
    }
    return err;
}
