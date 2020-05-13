/* Test fxam instruction.  */

#include <stdint.h>
#include <stdio.h>

union u {
    struct { uint64_t sig; uint16_t sign_exp; } s;
    long double ld;
};

volatile union u ld_pseudo_m16382 = { .s = { UINT64_C(1) << 63, 0 } };
volatile union u ld_pseudo_nm16382 = { .s = { UINT64_C(1) << 63, 0x8000 } };
volatile union u ld_invalid_1 = { .s = { 1, 1234 } };
volatile union u ld_invalid_2 = { .s = { 0, 1234 } };
volatile union u ld_invalid_3 = { .s = { 0, 0x7fff } };
volatile union u ld_invalid_4 = { .s = { (UINT64_C(1) << 63) - 1, 0x7fff } };
volatile union u ld_invalid_n1 = { .s = { 1, 0x8123 } };
volatile union u ld_invalid_n2 = { .s = { 0, 0x8123 } };
volatile union u ld_invalid_n3 = { .s = { 0, 0xffff } };
volatile union u ld_invalid_n4 = { .s = { (UINT64_C(1) << 63) - 1, 0xffff } };

#define C0 (1 << 8)
#define C1 (1 << 9)
#define C2 (1 << 10)
#define C3 (1 << 14)
#define FLAGS (C0 | C1 | C2 | C3)

int main(void)
{
    short sw;
    int ret = 0;
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (0.0L));
    if ((sw & FLAGS) != C3) {
        printf("FAIL: +0\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (-0.0L));
    if ((sw & FLAGS) != (C3 | C1)) {
        printf("FAIL: -0\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (1.0L));
    if ((sw & FLAGS) != C2) {
        printf("FAIL: +normal\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (-1.0L));
    if ((sw & FLAGS) != (C2 | C1)) {
        printf("FAIL: -normal\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (__builtin_infl()));
    if ((sw & FLAGS) != (C2 | C0)) {
        printf("FAIL: +inf\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (-__builtin_infl()));
    if ((sw & FLAGS) != (C2 | C1 | C0)) {
        printf("FAIL: -inf\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (__builtin_nanl("")));
    if ((sw & FLAGS) != C0) {
        printf("FAIL: +nan\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (-__builtin_nanl("")));
    if ((sw & FLAGS) != (C1 | C0)) {
        printf("FAIL: -nan\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (__builtin_nansl("")));
    if ((sw & FLAGS) != C0) {
        printf("FAIL: +snan\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (-__builtin_nansl("")));
    if ((sw & FLAGS) != (C1 | C0)) {
        printf("FAIL: -snan\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (0x1p-16445L));
    if ((sw & FLAGS) != (C3 | C2)) {
        printf("FAIL: +denormal\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (-0x1p-16445L));
    if ((sw & FLAGS) != (C3 | C2 | C1)) {
        printf("FAIL: -denormal\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (ld_pseudo_m16382.ld));
    if ((sw & FLAGS) != (C3 | C2)) {
        printf("FAIL: +pseudo-denormal\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (ld_pseudo_nm16382.ld));
    if ((sw & FLAGS) != (C3 | C2 | C1)) {
        printf("FAIL: -pseudo-denormal\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (ld_invalid_1.ld));
    if ((sw & FLAGS) != 0) {
        printf("FAIL: +invalid 1\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (ld_invalid_n1.ld));
    if ((sw & FLAGS) != C1) {
        printf("FAIL: -invalid 1\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (ld_invalid_2.ld));
    if ((sw & FLAGS) != 0) {
        printf("FAIL: +invalid 2\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (ld_invalid_n2.ld));
    if ((sw & FLAGS) != C1) {
        printf("FAIL: -invalid 2\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (ld_invalid_3.ld));
    if ((sw & FLAGS) != 0) {
        printf("FAIL: +invalid 3\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (ld_invalid_n3.ld));
    if ((sw & FLAGS) != C1) {
        printf("FAIL: -invalid 3\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (ld_invalid_4.ld));
    if ((sw & FLAGS) != 0) {
        printf("FAIL: +invalid 4\n");
        ret = 1;
    }
    __asm__ volatile ("fxam\nfnstsw" : "=a" (sw) : "t" (ld_invalid_n4.ld));
    if ((sw & FLAGS) != C1) {
        printf("FAIL: -invalid 4\n");
        ret = 1;
    }
    return ret;
}
