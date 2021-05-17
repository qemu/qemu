/*
 * fp-bench.c - A collection of simple floating point microbenchmarks.
 *
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#ifndef HW_POISON_H
#error Must define HW_POISON_H to work around TARGET_* poisoning
#endif

#include "qemu/osdep.h"
#include <math.h>
#include <fenv.h>
#include "qemu/timer.h"
#include "qemu/int128.h"
#include "fpu/softfloat.h"

/* amortize the computation of random inputs */
#define OPS_PER_ITER     50000

#define MAX_OPERANDS 3

#define SEED_A 0xdeadfacedeadface
#define SEED_B 0xbadc0feebadc0fee
#define SEED_C 0xbeefdeadbeefdead

enum op {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_FMA,
    OP_SQRT,
    OP_CMP,
    OP_MAX_NR,
};

static const char * const op_names[] = {
    [OP_ADD] = "add",
    [OP_SUB] = "sub",
    [OP_MUL] = "mul",
    [OP_DIV] = "div",
    [OP_FMA] = "mulAdd",
    [OP_SQRT] = "sqrt",
    [OP_CMP] = "cmp",
    [OP_MAX_NR] = NULL,
};

enum precision {
    PREC_SINGLE,
    PREC_DOUBLE,
    PREC_QUAD,
    PREC_FLOAT32,
    PREC_FLOAT64,
    PREC_FLOAT128,
    PREC_MAX_NR,
};

enum rounding {
    ROUND_EVEN,
    ROUND_ZERO,
    ROUND_DOWN,
    ROUND_UP,
    ROUND_TIEAWAY,
    N_ROUND_MODES,
};

static const char * const round_names[] = {
    [ROUND_EVEN] = "even",
    [ROUND_ZERO] = "zero",
    [ROUND_DOWN] = "down",
    [ROUND_UP] = "up",
    [ROUND_TIEAWAY] = "tieaway",
};

enum tester {
    TESTER_SOFT,
    TESTER_HOST,
    TESTER_MAX_NR,
};

static const char * const tester_names[] = {
    [TESTER_SOFT] = "soft",
    [TESTER_HOST] = "host",
    [TESTER_MAX_NR] = NULL,
};

union fp {
    float f;
    double d;
    float32 f32;
    float64 f64;
    float128 f128;
    uint64_t u64;
};

struct op_state;

typedef float (*float_func_t)(const struct op_state *s);
typedef double (*double_func_t)(const struct op_state *s);

union fp_func {
    float_func_t float_func;
    double_func_t double_func;
};

typedef void (*bench_func_t)(void);

struct op_desc {
    const char * const name;
};

#define DEFAULT_DURATION_SECS 1

static uint64_t random_ops[MAX_OPERANDS] = {
    SEED_A, SEED_B, SEED_C,
};

static float128 random_quad_ops[MAX_OPERANDS] = {
    {SEED_A, SEED_B}, {SEED_B, SEED_C}, {SEED_C, SEED_A},
};
static float_status soft_status;
static enum precision precision;
static enum op operation;
static enum tester tester;
static uint64_t n_completed_ops;
static unsigned int duration = DEFAULT_DURATION_SECS;
static int64_t ns_elapsed;
/* disable optimizations with volatile */
static volatile union fp res;

/*
 * From: https://en.wikipedia.org/wiki/Xorshift
 * This is faster than rand_r(), and gives us a wider range (RAND_MAX is only
 * guaranteed to be >= INT_MAX).
 */
static uint64_t xorshift64star(uint64_t x)
{
    x ^= x >> 12; /* a */
    x ^= x << 25; /* b */
    x ^= x >> 27; /* c */
    return x * UINT64_C(2685821657736338717);
}

static void update_random_ops(int n_ops, enum precision prec)
{
    int i;

    for (i = 0; i < n_ops; i++) {

        switch (prec) {
        case PREC_SINGLE:
        case PREC_FLOAT32:
        {
            uint64_t r = random_ops[i];
            do {
                r = xorshift64star(r);
            } while (!float32_is_normal(r));
            random_ops[i] = r;
            break;
        }
        case PREC_DOUBLE:
        case PREC_FLOAT64:
        {
            uint64_t r = random_ops[i];
            do {
                r = xorshift64star(r);
            } while (!float64_is_normal(r));
            random_ops[i] = r;
            break;
        }
        case PREC_QUAD:
        case PREC_FLOAT128:
        {
            float128 r = random_quad_ops[i];
            uint64_t hi = r.high;
            uint64_t lo = r.low;
            do {
                hi = xorshift64star(hi);
                lo = xorshift64star(lo);
                r = make_float128(hi, lo);
            } while (!float128_is_normal(r));
            random_quad_ops[i] = r;
            break;
        }
        default:
            g_assert_not_reached();
        }
    }
}

static void fill_random(union fp *ops, int n_ops, enum precision prec,
                        bool no_neg)
{
    int i;

    for (i = 0; i < n_ops; i++) {
        switch (prec) {
        case PREC_SINGLE:
        case PREC_FLOAT32:
            ops[i].f32 = make_float32(random_ops[i]);
            if (no_neg && float32_is_neg(ops[i].f32)) {
                ops[i].f32 = float32_chs(ops[i].f32);
            }
            break;
        case PREC_DOUBLE:
        case PREC_FLOAT64:
            ops[i].f64 = make_float64(random_ops[i]);
            if (no_neg && float64_is_neg(ops[i].f64)) {
                ops[i].f64 = float64_chs(ops[i].f64);
            }
            break;
        case PREC_QUAD:
        case PREC_FLOAT128:
            ops[i].f128 = random_quad_ops[i];
            if (no_neg && float128_is_neg(ops[i].f128)) {
                ops[i].f128 = float128_chs(ops[i].f128);
            }
            break;
        default:
            g_assert_not_reached();
        }
    }
}

/*
 * The main benchmark function. Instead of (ab)using macros, we rely
 * on the compiler to unfold this at compile-time.
 */
static void bench(enum precision prec, enum op op, int n_ops, bool no_neg)
{
    int64_t tf = get_clock() + duration * 1000000000LL;

    while (get_clock() < tf) {
        union fp ops[MAX_OPERANDS];
        int64_t t0;
        int i;

        update_random_ops(n_ops, prec);
        switch (prec) {
        case PREC_SINGLE:
            fill_random(ops, n_ops, prec, no_neg);
            t0 = get_clock();
            for (i = 0; i < OPS_PER_ITER; i++) {
                float a = ops[0].f;
                float b = ops[1].f;
                float c = ops[2].f;

                switch (op) {
                case OP_ADD:
                    res.f = a + b;
                    break;
                case OP_SUB:
                    res.f = a - b;
                    break;
                case OP_MUL:
                    res.f = a * b;
                    break;
                case OP_DIV:
                    res.f = a / b;
                    break;
                case OP_FMA:
                    res.f = fmaf(a, b, c);
                    break;
                case OP_SQRT:
                    res.f = sqrtf(a);
                    break;
                case OP_CMP:
                    res.u64 = isgreater(a, b);
                    break;
                default:
                    g_assert_not_reached();
                }
            }
            break;
        case PREC_DOUBLE:
            fill_random(ops, n_ops, prec, no_neg);
            t0 = get_clock();
            for (i = 0; i < OPS_PER_ITER; i++) {
                double a = ops[0].d;
                double b = ops[1].d;
                double c = ops[2].d;

                switch (op) {
                case OP_ADD:
                    res.d = a + b;
                    break;
                case OP_SUB:
                    res.d = a - b;
                    break;
                case OP_MUL:
                    res.d = a * b;
                    break;
                case OP_DIV:
                    res.d = a / b;
                    break;
                case OP_FMA:
                    res.d = fma(a, b, c);
                    break;
                case OP_SQRT:
                    res.d = sqrt(a);
                    break;
                case OP_CMP:
                    res.u64 = isgreater(a, b);
                    break;
                default:
                    g_assert_not_reached();
                }
            }
            break;
        case PREC_FLOAT32:
            fill_random(ops, n_ops, prec, no_neg);
            t0 = get_clock();
            for (i = 0; i < OPS_PER_ITER; i++) {
                float32 a = ops[0].f32;
                float32 b = ops[1].f32;
                float32 c = ops[2].f32;

                switch (op) {
                case OP_ADD:
                    res.f32 = float32_add(a, b, &soft_status);
                    break;
                case OP_SUB:
                    res.f32 = float32_sub(a, b, &soft_status);
                    break;
                case OP_MUL:
                    res.f = float32_mul(a, b, &soft_status);
                    break;
                case OP_DIV:
                    res.f32 = float32_div(a, b, &soft_status);
                    break;
                case OP_FMA:
                    res.f32 = float32_muladd(a, b, c, 0, &soft_status);
                    break;
                case OP_SQRT:
                    res.f32 = float32_sqrt(a, &soft_status);
                    break;
                case OP_CMP:
                    res.u64 = float32_compare_quiet(a, b, &soft_status);
                    break;
                default:
                    g_assert_not_reached();
                }
            }
            break;
        case PREC_FLOAT64:
            fill_random(ops, n_ops, prec, no_neg);
            t0 = get_clock();
            for (i = 0; i < OPS_PER_ITER; i++) {
                float64 a = ops[0].f64;
                float64 b = ops[1].f64;
                float64 c = ops[2].f64;

                switch (op) {
                case OP_ADD:
                    res.f64 = float64_add(a, b, &soft_status);
                    break;
                case OP_SUB:
                    res.f64 = float64_sub(a, b, &soft_status);
                    break;
                case OP_MUL:
                    res.f = float64_mul(a, b, &soft_status);
                    break;
                case OP_DIV:
                    res.f64 = float64_div(a, b, &soft_status);
                    break;
                case OP_FMA:
                    res.f64 = float64_muladd(a, b, c, 0, &soft_status);
                    break;
                case OP_SQRT:
                    res.f64 = float64_sqrt(a, &soft_status);
                    break;
                case OP_CMP:
                    res.u64 = float64_compare_quiet(a, b, &soft_status);
                    break;
                default:
                    g_assert_not_reached();
                }
            }
            break;
        case PREC_FLOAT128:
            fill_random(ops, n_ops, prec, no_neg);
            t0 = get_clock();
            for (i = 0; i < OPS_PER_ITER; i++) {
                float128 a = ops[0].f128;
                float128 b = ops[1].f128;
                float128 c = ops[2].f128;

                switch (op) {
                case OP_ADD:
                    res.f128 = float128_add(a, b, &soft_status);
                    break;
                case OP_SUB:
                    res.f128 = float128_sub(a, b, &soft_status);
                    break;
                case OP_MUL:
                    res.f128 = float128_mul(a, b, &soft_status);
                    break;
                case OP_DIV:
                    res.f128 = float128_div(a, b, &soft_status);
                    break;
                case OP_FMA:
                    res.f128 = float128_muladd(a, b, c, 0, &soft_status);
                    break;
                case OP_SQRT:
                    res.f128 = float128_sqrt(a, &soft_status);
                    break;
                case OP_CMP:
                    res.u64 = float128_compare_quiet(a, b, &soft_status);
                    break;
                default:
                    g_assert_not_reached();
                }
            }
            break;
        default:
            g_assert_not_reached();
        }
        ns_elapsed += get_clock() - t0;
        n_completed_ops += OPS_PER_ITER;
    }
}

#define GEN_BENCH(name, type, prec, op, n_ops)          \
    static void __attribute__((flatten)) name(void)     \
    {                                                   \
        bench(prec, op, n_ops, false);                  \
    }

#define GEN_BENCH_NO_NEG(name, type, prec, op, n_ops)   \
    static void __attribute__((flatten)) name(void)     \
    {                                                   \
        bench(prec, op, n_ops, true);                   \
    }

#define GEN_BENCH_ALL_TYPES(opname, op, n_ops)                          \
    GEN_BENCH(bench_ ## opname ## _float, float, PREC_SINGLE, op, n_ops) \
    GEN_BENCH(bench_ ## opname ## _double, double, PREC_DOUBLE, op, n_ops) \
    GEN_BENCH(bench_ ## opname ## _float32, float32, PREC_FLOAT32, op, n_ops) \
    GEN_BENCH(bench_ ## opname ## _float64, float64, PREC_FLOAT64, op, n_ops) \
    GEN_BENCH(bench_ ## opname ## _float128, float128, PREC_FLOAT128, op, n_ops)

GEN_BENCH_ALL_TYPES(add, OP_ADD, 2)
GEN_BENCH_ALL_TYPES(sub, OP_SUB, 2)
GEN_BENCH_ALL_TYPES(mul, OP_MUL, 2)
GEN_BENCH_ALL_TYPES(div, OP_DIV, 2)
GEN_BENCH_ALL_TYPES(fma, OP_FMA, 3)
GEN_BENCH_ALL_TYPES(cmp, OP_CMP, 2)
#undef GEN_BENCH_ALL_TYPES

#define GEN_BENCH_ALL_TYPES_NO_NEG(name, op, n)                         \
    GEN_BENCH_NO_NEG(bench_ ## name ## _float, float, PREC_SINGLE, op, n) \
    GEN_BENCH_NO_NEG(bench_ ## name ## _double, double, PREC_DOUBLE, op, n) \
    GEN_BENCH_NO_NEG(bench_ ## name ## _float32, float32, PREC_FLOAT32, op, n) \
    GEN_BENCH_NO_NEG(bench_ ## name ## _float64, float64, PREC_FLOAT64, op, n) \
    GEN_BENCH_NO_NEG(bench_ ## name ## _float128, float128, PREC_FLOAT128, op, n)

GEN_BENCH_ALL_TYPES_NO_NEG(sqrt, OP_SQRT, 1)
#undef GEN_BENCH_ALL_TYPES_NO_NEG

#undef GEN_BENCH_NO_NEG
#undef GEN_BENCH

#define GEN_BENCH_FUNCS(opname, op)                             \
    [op] = {                                                    \
        [PREC_SINGLE]    = bench_ ## opname ## _float,          \
        [PREC_DOUBLE]    = bench_ ## opname ## _double,         \
        [PREC_FLOAT32]   = bench_ ## opname ## _float32,        \
        [PREC_FLOAT64]   = bench_ ## opname ## _float64,        \
        [PREC_FLOAT128]   = bench_ ## opname ## _float128,      \
    }

static const bench_func_t bench_funcs[OP_MAX_NR][PREC_MAX_NR] = {
    GEN_BENCH_FUNCS(add, OP_ADD),
    GEN_BENCH_FUNCS(sub, OP_SUB),
    GEN_BENCH_FUNCS(mul, OP_MUL),
    GEN_BENCH_FUNCS(div, OP_DIV),
    GEN_BENCH_FUNCS(fma, OP_FMA),
    GEN_BENCH_FUNCS(sqrt, OP_SQRT),
    GEN_BENCH_FUNCS(cmp, OP_CMP),
};

#undef GEN_BENCH_FUNCS

static void run_bench(void)
{
    bench_func_t f;

    f = bench_funcs[operation][precision];
    g_assert(f);
    f();
}

/* @arr must be NULL-terminated */
static int find_name(const char * const *arr, const char *name)
{
    int i;

    for (i = 0; arr[i] != NULL; i++) {
        if (strcmp(name, arr[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static void usage_complete(int argc, char *argv[])
{
    gchar *op_list = g_strjoinv(", ", (gchar **)op_names);
    gchar *tester_list = g_strjoinv(", ", (gchar **)tester_names);

    fprintf(stderr, "Usage: %s [options]\n", argv[0]);
    fprintf(stderr, "options:\n");
    fprintf(stderr, " -d = duration, in seconds. Default: %d\n",
            DEFAULT_DURATION_SECS);
    fprintf(stderr, " -h = show this help message.\n");
    fprintf(stderr, " -o = floating point operation (%s). Default: %s\n",
            op_list, op_names[0]);
    fprintf(stderr, " -p = floating point precision (single, double, quad[soft only]). "
            "Default: single\n");
    fprintf(stderr, " -r = rounding mode (even, zero, down, up, tieaway). "
            "Default: even\n");
    fprintf(stderr, " -t = tester (%s). Default: %s\n",
            tester_list, tester_names[0]);
    fprintf(stderr, " -z = flush inputs to zero (soft tester only). "
            "Default: disabled\n");
    fprintf(stderr, " -Z = flush output to zero (soft tester only). "
            "Default: disabled\n");

    g_free(tester_list);
    g_free(op_list);
}

static int round_name_to_mode(const char *name)
{
    int i;

    for (i = 0; i < N_ROUND_MODES; i++) {
        if (!strcmp(round_names[i], name)) {
            return i;
        }
    }
    return -1;
}

static void QEMU_NORETURN die_host_rounding(enum rounding rounding)
{
    fprintf(stderr, "fatal: '%s' rounding not supported on this host\n",
            round_names[rounding]);
    exit(EXIT_FAILURE);
}

static void set_host_precision(enum rounding rounding)
{
    int rhost;

    switch (rounding) {
    case ROUND_EVEN:
        rhost = FE_TONEAREST;
        break;
    case ROUND_ZERO:
        rhost = FE_TOWARDZERO;
        break;
    case ROUND_DOWN:
        rhost = FE_DOWNWARD;
        break;
    case ROUND_UP:
        rhost = FE_UPWARD;
        break;
    case ROUND_TIEAWAY:
        die_host_rounding(rounding);
        return;
    default:
        g_assert_not_reached();
    }

    if (fesetround(rhost)) {
        die_host_rounding(rounding);
    }
}

static void set_soft_precision(enum rounding rounding)
{
    signed char mode;

    switch (rounding) {
    case ROUND_EVEN:
        mode = float_round_nearest_even;
        break;
    case ROUND_ZERO:
        mode = float_round_to_zero;
        break;
    case ROUND_DOWN:
        mode = float_round_down;
        break;
    case ROUND_UP:
        mode = float_round_up;
        break;
    case ROUND_TIEAWAY:
        mode = float_round_ties_away;
        break;
    default:
        g_assert_not_reached();
    }
    soft_status.float_rounding_mode = mode;
}

static void parse_args(int argc, char *argv[])
{
    int c;
    int val;
    int rounding = ROUND_EVEN;

    for (;;) {
        c = getopt(argc, argv, "d:ho:p:r:t:zZ");
        if (c < 0) {
            break;
        }
        switch (c) {
        case 'd':
            duration = atoi(optarg);
            break;
        case 'h':
            usage_complete(argc, argv);
            exit(EXIT_SUCCESS);
        case 'o':
            val = find_name(op_names, optarg);
            if (val < 0) {
                fprintf(stderr, "Unsupported op '%s'\n", optarg);
                exit(EXIT_FAILURE);
            }
            operation = val;
            break;
        case 'p':
            if (!strcmp(optarg, "single")) {
                precision = PREC_SINGLE;
            } else if (!strcmp(optarg, "double")) {
                precision = PREC_DOUBLE;
            } else if (!strcmp(optarg, "quad")) {
                precision = PREC_QUAD;
            } else {
                fprintf(stderr, "Unsupported precision '%s'\n", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 'r':
            rounding = round_name_to_mode(optarg);
            if (rounding < 0) {
                fprintf(stderr, "fatal: invalid rounding mode '%s'\n", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 't':
            val = find_name(tester_names, optarg);
            if (val < 0) {
                fprintf(stderr, "Unsupported tester '%s'\n", optarg);
                exit(EXIT_FAILURE);
            }
            tester = val;
            break;
        case 'z':
            soft_status.flush_inputs_to_zero = 1;
            break;
        case 'Z':
            soft_status.flush_to_zero = 1;
            break;
        }
    }

    /* set precision and rounding mode based on the tester */
    switch (tester) {
    case TESTER_HOST:
        set_host_precision(rounding);
        break;
    case TESTER_SOFT:
        set_soft_precision(rounding);
        switch (precision) {
        case PREC_SINGLE:
            precision = PREC_FLOAT32;
            break;
        case PREC_DOUBLE:
            precision = PREC_FLOAT64;
            break;
        case PREC_QUAD:
            precision = PREC_FLOAT128;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void pr_stats(void)
{
    printf("%.2f MFlops\n", (double)n_completed_ops / ns_elapsed * 1e3);
}

int main(int argc, char *argv[])
{
    parse_args(argc, argv);
    run_bench();
    pr_stats();
    return 0;
}
