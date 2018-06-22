/*
 *  x86 FPREM test - executes the FPREM and FPREM1 instructions with corner case
 *  operands and prints the operands, result and FPU status word.
 *
 *  Run this on real hardware, then under QEMU, and diff the outputs, to compare
 *  QEMU's implementation to your hardware. The 'run-test-i386-fprem' make
 *  target does this.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2012 Catalin Patulea
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdint.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/*
 * Inspired by <ieee754.h>'s union ieee854_long_double, but with single
 * long long mantissa fields and assuming little-endianness for simplicity.
 */
union float80u {
    long double d;

    /* This is the IEEE 854 double-extended-precision format.  */
    struct {
        unsigned long long mantissa:63;
        unsigned int one:1;
        unsigned int exponent:15;
        unsigned int negative:1;
        unsigned int empty:16;
    } __attribute__((packed)) ieee;

    /* This is for NaNs in the IEEE 854 double-extended-precision format.  */
    struct {
        unsigned long long mantissa:62;
        unsigned int quiet_nan:1;
        unsigned int one:1;
        unsigned int exponent:15;
        unsigned int negative:1;
        unsigned int empty:16;
    } __attribute__((packed)) ieee_nan;
};

#define IEEE854_LONG_DOUBLE_BIAS 0x3fff

static const union float80u q_nan = {
    .ieee_nan.negative = 0,  /* X */
    .ieee_nan.exponent = 0x7fff,
    .ieee_nan.one = 1,
    .ieee_nan.quiet_nan = 1,
    .ieee_nan.mantissa = 0,
};

static const union float80u s_nan = {
    .ieee_nan.negative = 0,  /* X */
    .ieee_nan.exponent = 0x7fff,
    .ieee_nan.one = 1,
    .ieee_nan.quiet_nan = 0,
    .ieee_nan.mantissa = 1,  /* nonzero */
};

static const union float80u pos_inf = {
    .ieee.negative = 0,
    .ieee.exponent = 0x7fff,
    .ieee.one = 1,
    .ieee.mantissa = 0,
};

static const union float80u pseudo_pos_inf = {  /* "unsupported" */
    .ieee.negative = 0,
    .ieee.exponent = 0x7fff,
    .ieee.one = 0,
    .ieee.mantissa = 0,
};

static const union float80u pos_denorm = {
    .ieee.negative = 0,
    .ieee.exponent = 0,
    .ieee.one = 0,
    .ieee.mantissa = 1,
};

static const union float80u smallest_positive_norm = {
    .ieee.negative = 0,
    .ieee.exponent = 1,
    .ieee.one = 1,
    .ieee.mantissa = 0,
};

static void fninit()
{
    asm volatile ("fninit\n");
}

static long double fprem(long double a, long double b, uint16_t *sw)
{
    long double result;
    asm volatile ("fprem\n"
                  "fnstsw %1\n"
                  : "=t" (result), "=m" (*sw)
                  : "0" (a), "u" (b)
                  : "st(1)");
    return result;
}

static long double fprem1(long double a, long double b, uint16_t *sw)
{
    long double result;
    asm volatile ("fprem1\n"
                  "fnstsw %1\n"
                  : "=t" (result), "=m" (*sw)
                  : "0" (a), "u" (b)
                  : "st(1)");
    return result;
}

#define FPUS_IE (1 << 0)
#define FPUS_DE (1 << 1)
#define FPUS_ZE (1 << 2)
#define FPUS_OE (1 << 3)
#define FPUS_UE (1 << 4)
#define FPUS_PE (1 << 5)
#define FPUS_SF (1 << 6)
#define FPUS_SE (1 << 7)
#define FPUS_C0 (1 << 8)
#define FPUS_C1 (1 << 9)
#define FPUS_C2 (1 << 10)
#define FPUS_TOP 0x3800
#define FPUS_C3 (1 << 14)
#define FPUS_B  (1 << 15)

#define FPUS_EMASK 0x007f

#define FPUC_EM 0x3f

static void psw(uint16_t sw)
{
    printf("SW:  C3 TopC2C1C0\n");
    printf("SW: %c %d %3d %d %d %d %c %c %c %c %c %c %c %c\n",
           sw & FPUS_B ? 'B' : 'b',
           !!(sw & FPUS_C3),
           (sw & FPUS_TOP) >> 11,
           !!(sw & FPUS_C2),
           !!(sw & FPUS_C1),
           !!(sw & FPUS_C0),
           (sw & FPUS_SE) ? 'S' : 's',
           (sw & FPUS_SF) ? 'F' : 'f',
           (sw & FPUS_PE) ? 'P' : 'p',
           (sw & FPUS_UE) ? 'U' : 'u',
           (sw & FPUS_OE) ? 'O' : 'o',
           (sw & FPUS_ZE) ? 'Z' : 'z',
           (sw & FPUS_DE) ? 'D' : 'd',
           (sw & FPUS_IE) ? 'I' : 'i');
}

static void do_fprem(long double a, long double b)
{
    const union float80u au = {.d = a};
    const union float80u bu = {.d = b};
    union float80u ru;
    uint16_t sw;

    printf("A: S=%d Exp=%04x Int=%d (QNaN=%d) Sig=%016llx (%.06Le)\n",
           au.ieee.negative, au.ieee.exponent, au.ieee.one,
           au.ieee_nan.quiet_nan, (unsigned long long)au.ieee.mantissa,
           a);
    printf("B: S=%d Exp=%04x Int=%d (QNaN=%d) Sig=%016llx (%.06Le)\n",
           bu.ieee.negative, bu.ieee.exponent, bu.ieee.one,
           bu.ieee_nan.quiet_nan, (unsigned long long)bu.ieee.mantissa,
           b);
    fflush(stdout);

    fninit();
    ru.d = fprem(a, b, &sw);
    psw(sw);

    printf("R : S=%d Exp=%04x Int=%d (QNaN=%d) Sig=%016llx (%.06Le)\n",
           ru.ieee.negative, ru.ieee.exponent, ru.ieee.one,
           ru.ieee_nan.quiet_nan, (unsigned long long)ru.ieee.mantissa,
           ru.d);

    fninit();
    ru.d = fprem1(a, b, &sw);
    psw(sw);

    printf("R1: S=%d Exp=%04x Int=%d (QNaN=%d) Sig=%016llx (%.06Le)\n",
           ru.ieee.negative, ru.ieee.exponent, ru.ieee.one,
           ru.ieee_nan.quiet_nan, (unsigned long long)ru.ieee.mantissa,
           ru.d);

    printf("\n");
}

static void do_fprem_stack_underflow(void)
{
    const long double a = 1.0;
    union float80u ru;
    uint16_t sw;

    fninit();
    asm volatile ("fprem\n"
                  "fnstsw %1\n"
                  : "=t" (ru.d), "=m" (sw)
                  : "0" (a)
                  : "st(1)");
    psw(sw);

    printf("R: S=%d Exp=%04x Int=%d (QNaN=%d) Sig=%016llx (%.06Le)\n",
           ru.ieee.negative, ru.ieee.exponent, ru.ieee.one,
           ru.ieee_nan.quiet_nan, (unsigned long long)ru.ieee.mantissa,
           ru.d);
    printf("\n");
}

static void test_fprem_cases(void)
{
    printf("= stack underflow =\n");
    do_fprem_stack_underflow();

    printf("= invalid operation =\n");
    do_fprem(q_nan.d, 1.0);
    do_fprem(s_nan.d, 1.0);
    do_fprem(1.0, 0.0);
    do_fprem(pos_inf.d, 1.0);
    do_fprem(pseudo_pos_inf.d, 1.0);

    printf("= denormal =\n");
    do_fprem(pos_denorm.d, 1.0);
    do_fprem(1.0, pos_denorm.d);

    do_fprem(smallest_positive_norm.d, smallest_positive_norm.d);

    /* printf("= underflow =\n"); */
    /* TODO: Is there a case where FPREM raises underflow? */
}

static void test_fprem_pairs(void)
{
    unsigned long long count;

    unsigned int negative_index_a = 0;
    unsigned int negative_index_b = 0;
    static const unsigned int negative_values[] = {
        0,
        1,
    };

    unsigned int exponent_index_a = 0;
    unsigned int exponent_index_b = 0;
    static const unsigned int exponent_values[] = {
        0,
        1,
        2,
        IEEE854_LONG_DOUBLE_BIAS - 1,
        IEEE854_LONG_DOUBLE_BIAS,
        IEEE854_LONG_DOUBLE_BIAS + 1,
        0x7ffd,
        0x7ffe,
        0x7fff,
    };

    unsigned int one_index_a = 0;
    unsigned int one_index_b = 0;
    static const unsigned int one_values[] = {
        0,
        1,
    };

    unsigned int quiet_nan_index_a = 0;
    unsigned int quiet_nan_index_b = 0;
    static const unsigned int quiet_nan_values[] = {
        0,
        1,
    };

    unsigned int mantissa_index_a = 0;
    unsigned int mantissa_index_b = 0;
    static const unsigned long long mantissa_values[] = {
        0,
        1,
        2,
        0x3ffffffffffffffdULL,
        0x3ffffffffffffffeULL,
        0x3fffffffffffffffULL,
    };

    for (count = 0; ; ++count) {
#define INIT_FIELD(var, field) \
            .ieee_nan.field = field##_values[field##_index_##var]
        const union float80u a = {
            INIT_FIELD(a, negative),
            INIT_FIELD(a, exponent),
            INIT_FIELD(a, one),
            INIT_FIELD(a, quiet_nan),
            INIT_FIELD(a, mantissa),
        };
        const union float80u b = {
            INIT_FIELD(b, negative),
            INIT_FIELD(b, exponent),
            INIT_FIELD(b, one),
            INIT_FIELD(b, quiet_nan),
            INIT_FIELD(b, mantissa),
        };
#undef INIT_FIELD

        do_fprem(a.d, b.d);

        int carry = 1;
#define CARRY_INTO(var, field) do { \
            if (carry) { \
                if (++field##_index_##var == ARRAY_SIZE(field##_values)) { \
                    field##_index_##var = 0; \
                } else { \
                    carry = 0; \
                } \
            } \
        } while (0)
        CARRY_INTO(b, mantissa);
        CARRY_INTO(b, quiet_nan);
        CARRY_INTO(b, one);
        CARRY_INTO(b, exponent);
        CARRY_INTO(b, negative);
        CARRY_INTO(a, mantissa);
        CARRY_INTO(a, quiet_nan);
        CARRY_INTO(a, one);
        CARRY_INTO(a, exponent);
        CARRY_INTO(a, negative);
#undef CARRY_INTO

        if (carry) {
            break;
        }
    }

    fprintf(stderr, "test-i386-fprem: tested %llu cases\n", count);
}

int main(int argc, char **argv)
{
    test_fprem_cases();
    test_fprem_pairs();
    return 0;
}
