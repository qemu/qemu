/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

int64_t mac_w(int64_t mac, const int16_t *a, const int16_t *b)
{
    register uint32_t macl __asm__("macl") = mac;
    register uint32_t mach __asm__("mach") = mac >> 32;

    asm volatile("mac.w @%0+,@%1+"
                 : "+r"(a), "+r"(b), "+x"(macl), "+x"(mach));

    return ((uint64_t)mach << 32) | macl;
}

typedef struct {
    int64_t mac;
    int16_t a, b;
    int64_t res[2];
} Test;

__attribute__((noinline))
void test(const Test *t, int sat)
{
    int64_t res;

    if (sat) {
        asm volatile("sets");
    } else {
        asm volatile("clrs");
    }
    res = mac_w(t->mac, &t->a, &t->b);

    if (res != t->res[sat]) {
        fprintf(stderr, "%#llx + (%#x * %#x) = %#llx -- got %#llx\n",
                t->mac, t->a, t->b, t->res[sat], res);
        abort();
    }
}

int main()
{
    static const Test tests[] = {
        { 0, 2, 3, { 6, 6 } },
        { 0x123456787ffffffell, 2, -3,
          { 0x123456787ffffff8ll, 0x123456787ffffff8ll } },
        { 0xabcdef127ffffffall, 2, 3,
          { 0xabcdef1280000000ll, 0x000000017fffffffll } },
        { 0xfffffffffll, INT16_MAX, INT16_MAX,
          { 0x103fff0000ll, 0xf3fff0000ll } },
    };

    for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        for (int j = 0; j < 2; ++j) {
            test(&tests[i], j);
        }
    }
    return 0;
}
