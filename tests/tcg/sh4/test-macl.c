/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define MACL_S_MIN  (-(1ll << 47))
#define MACL_S_MAX  ((1ll << 47) - 1)

int64_t mac_l(int64_t mac, const int32_t *a, const int32_t *b)
{
    register uint32_t macl __asm__("macl") = mac;
    register uint32_t mach __asm__("mach") = mac >> 32;

    asm volatile("mac.l @%0+,@%1+"
                 : "+r"(a), "+r"(b), "+x"(macl), "+x"(mach));

    return ((uint64_t)mach << 32) | macl;
}

typedef struct {
    int64_t mac;
    int32_t a, b;
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
    res = mac_l(t->mac, &t->a, &t->b);

    if (res != t->res[sat]) {
        fprintf(stderr, "%#llx + (%#x * %#x) = %#llx -- got %#llx\n",
                t->mac, t->a, t->b, t->res[sat], res);
        abort();
    }
}

int main()
{
    static const Test tests[] = {
        { 0x00007fff12345678ll, INT32_MAX, INT32_MAX,
          { 0x40007ffe12345679ll, MACL_S_MAX } },
        { MACL_S_MIN, -1, 1,
          { 0xffff7fffffffffffll, MACL_S_MIN } },
        { INT64_MIN, -1, 1,
          { INT64_MAX, MACL_S_MIN } },
        { 0x00007fff00000000ll, INT32_MAX, INT32_MAX,
          { 0x40007ffe00000001ll, MACL_S_MAX } },
        { 4, 1, 2, { 6, 6 } },
        { -4, -1, -2, { -2, -2 } },
    };

    for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        for (int j = 0; j < 2; ++j) {
            test(&tests[i], j);
        }
    }
    return 0;
}
