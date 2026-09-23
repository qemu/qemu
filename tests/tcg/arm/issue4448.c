/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <assert.h>
#include <stdint.h>

/* Signed 64-bit product, constant operand first then second. */
#define SMULL_CONST_LHS(k, x, lo, hi) \
    __asm__("mov r0, #" #k "\n\tsmull %0, %1, r0, %2" \
            : "=&r"(lo), "=&r"(hi) : "r"(x) : "r0", "cc")

#define SMULL_CONST_RHS(k, x, lo, hi) \
    __asm__("mov r0, #" #k "\n\tsmull %0, %1, %2, r0" \
            : "=&r"(lo), "=&r"(hi) : "r"(x) : "r0", "cc")

/* Unsigned 64-bit product, for contrast: this one is folded correctly. */
#define UMULL_CONST_LHS(k, x, lo, hi) \
    __asm__("mov r0, #" #k "\n\tumull %0, %1, r0, %2" \
            : "=&r"(lo), "=&r"(hi) : "r"(x) : "r0", "cc")

static int64_t s64(int32_t hi, int32_t lo)
{
    return ((int64_t)hi << 32) | (uint32_t)lo;
}

static uint64_t u64(int32_t hi, int32_t lo)
{
    return ((uint64_t)(uint32_t)hi << 32) | (uint32_t)lo;
}

int main(void)
{
    static const int32_t values[] = { 1961612, -1479053948, -7, 0 };

    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        int32_t x = values[i];
        int32_t lo, hi;

        SMULL_CONST_LHS(1, x, lo, hi);
        assert(x == s64(hi, lo));

        SMULL_CONST_RHS(1, x, lo, hi);
        assert(x == s64(hi, lo));

        UMULL_CONST_LHS(1, x, lo, hi);
        assert((uint32_t)x == u64(hi, lo));

        SMULL_CONST_LHS(0, x, lo, hi);
        assert(0 == u64(hi, lo));
    }
    return 0;
}
