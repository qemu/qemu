#include <stdio.h>
#include "qemu/compiler.h"

typedef vector float vsx_float32_vec_t;
typedef vector double vsx_float64_vec_t;
typedef vector signed int vsx_int32_vec_t;
typedef vector unsigned int vsx_uint32_vec_t;
typedef vector signed long long vsx_int64_vec_t;
typedef vector unsigned long long vsx_uint64_vec_t;

#define DEFINE_VSX_F2I_FUNC(SRC_T, DEST_T, INSN)                       \
static inline vsx_##DEST_T##_vec_t                                     \
    vsx_convert_##SRC_T##_vec_to_##DEST_T##_vec(vsx_##SRC_T##_vec_t v) \
{                                                                      \
    vsx_##DEST_T##_vec_t result;                                       \
    asm(#INSN " %x0, %x1" : "=wa" (result) : "wa" (v));                \
    return result;                                                     \
}

DEFINE_VSX_F2I_FUNC(float32, int32, xvcvspsxws)
DEFINE_VSX_F2I_FUNC(float32, uint32, xvcvspuxws)
DEFINE_VSX_F2I_FUNC(float32, int64, xvcvspsxds)
DEFINE_VSX_F2I_FUNC(float32, uint64, xvcvspuxds)
DEFINE_VSX_F2I_FUNC(float64, int32, xvcvdpsxws)
DEFINE_VSX_F2I_FUNC(float64, uint32, xvcvdpuxws)
DEFINE_VSX_F2I_FUNC(float64, int64, xvcvdpsxds)
DEFINE_VSX_F2I_FUNC(float64, uint64, xvcvdpuxds)

static inline vsx_float32_vec_t vsx_float32_is_nan(vsx_float32_vec_t v)
{
    vsx_float32_vec_t abs_v;
    vsx_float32_vec_t result_mask;
    const vsx_uint32_vec_t f32_pos_inf_bits = {0x7F800000U, 0x7F800000U,
                                               0x7F800000U, 0x7F800000U};

    asm("xvabssp %x0, %x1" : "=wa" (abs_v) : "wa" (v));
    asm("vcmpgtuw %0, %1, %2"
        : "=v" (result_mask)
        : "v" (abs_v), "v" (f32_pos_inf_bits));
    return result_mask;
}

static inline vsx_float64_vec_t vsx_float64_is_nan(vsx_float64_vec_t v)
{
    vsx_float64_vec_t abs_v;
    vsx_float64_vec_t result_mask;
    const vsx_uint64_vec_t f64_pos_inf_bits = {0x7FF0000000000000ULL,
                                               0x7FF0000000000000ULL};

    asm("xvabsdp %x0, %x1" : "=wa" (abs_v) : "wa" (v));
    asm("vcmpgtud %0, %1, %2"
        : "=v" (result_mask)
        : "v" (abs_v), "v" (f64_pos_inf_bits));
    return result_mask;
}

#define DEFINE_VSX_BINARY_LOGICAL_OP_INSN(LANE_TYPE, OP_NAME, OP_INSN)    \
static inline vsx_##LANE_TYPE##_vec_t vsx_##LANE_TYPE##_##OP_NAME(        \
    vsx_##LANE_TYPE##_vec_t a, vsx_##LANE_TYPE##_vec_t b)                 \
{                                                                         \
    vsx_##LANE_TYPE##_vec_t result;                                       \
    asm(#OP_INSN " %x0, %x1, %x2" : "=wa" (result) : "wa" (a), "wa" (b)); \
    return result;                                                        \
}

DEFINE_VSX_BINARY_LOGICAL_OP_INSN(float32, logical_and, xxland)
DEFINE_VSX_BINARY_LOGICAL_OP_INSN(float64, logical_and, xxland)
DEFINE_VSX_BINARY_LOGICAL_OP_INSN(int32, logical_and, xxland)
DEFINE_VSX_BINARY_LOGICAL_OP_INSN(uint32, logical_and, xxland)
DEFINE_VSX_BINARY_LOGICAL_OP_INSN(int64, logical_and, xxland)
DEFINE_VSX_BINARY_LOGICAL_OP_INSN(uint64, logical_and, xxland)

DEFINE_VSX_BINARY_LOGICAL_OP_INSN(float32, logical_andc, xxlandc)
DEFINE_VSX_BINARY_LOGICAL_OP_INSN(float64, logical_andc, xxlandc)

DEFINE_VSX_BINARY_LOGICAL_OP_INSN(float32, logical_or, xxlor)
DEFINE_VSX_BINARY_LOGICAL_OP_INSN(float64, logical_or, xxlor)
DEFINE_VSX_BINARY_LOGICAL_OP_INSN(int32, logical_or, xxlor)
DEFINE_VSX_BINARY_LOGICAL_OP_INSN(uint32, logical_or, xxlor)
DEFINE_VSX_BINARY_LOGICAL_OP_INSN(int64, logical_or, xxlor)
DEFINE_VSX_BINARY_LOGICAL_OP_INSN(uint64, logical_or, xxlor)

static inline vsx_int32_vec_t vsx_mask_out_float32_vec_to_int32_vec(
    vsx_int32_vec_t v)
{
    return v;
}
static inline vsx_uint32_vec_t vsx_mask_out_float32_vec_to_uint32_vec(
    vsx_uint32_vec_t v)
{
    return v;
}
static inline vsx_int64_vec_t vsx_mask_out_float32_vec_to_int64_vec(
    vsx_int64_vec_t v)
{
    return v;
}
static inline vsx_uint64_vec_t vsx_mask_out_float32_vec_to_uint64_vec(
    vsx_uint64_vec_t v)
{
    return v;
}

static inline vsx_int32_vec_t vsx_mask_out_float64_vec_to_int32_vec(
    vsx_int32_vec_t v)
{
#if HOST_BIG_ENDIAN
    const vsx_int32_vec_t valid_lanes_mask = {-1, 0, -1, 0};
#else
    const vsx_int32_vec_t valid_lanes_mask = {0, -1, 0, -1};
#endif

    return vsx_int32_logical_and(v, valid_lanes_mask);
}

static inline vsx_uint32_vec_t vsx_mask_out_float64_vec_to_uint32_vec(
    vsx_uint32_vec_t v)
{
    return (vsx_uint32_vec_t)vsx_mask_out_float64_vec_to_int32_vec(
        (vsx_int32_vec_t)v);
}

static inline vsx_int64_vec_t vsx_mask_out_float64_vec_to_int64_vec(
    vsx_int64_vec_t v)
{
    return v;
}
static inline vsx_uint64_vec_t vsx_mask_out_float64_vec_to_uint64_vec(
    vsx_uint64_vec_t v)
{
    return v;
}

static inline void print_vsx_float32_vec_elements(FILE *stream,
                                                  vsx_float32_vec_t vec)
{
    fprintf(stream, "%g, %g, %g, %g", (double)vec[0], (double)vec[1],
            (double)vec[2], (double)vec[3]);
}

static inline void print_vsx_float64_vec_elements(FILE *stream,
                                                  vsx_float64_vec_t vec)
{
    fprintf(stream, "%.17g, %.17g", vec[0], vec[1]);
}

static inline void print_vsx_int32_vec_elements(FILE *stream,
                                                vsx_int32_vec_t vec)
{
    fprintf(stream, "%d, %d, %d, %d", vec[0], vec[1], vec[2], vec[3]);
}

static inline void print_vsx_uint32_vec_elements(FILE *stream,
                                                 vsx_uint32_vec_t vec)
{
    fprintf(stream, "%u, %u, %u, %u", vec[0], vec[1], vec[2], vec[3]);
}

static inline void print_vsx_int64_vec_elements(FILE *stream,
                                                vsx_int64_vec_t vec)
{
    fprintf(stream, "%lld, %lld", vec[0], vec[1]);
}

static inline void print_vsx_uint64_vec_elements(FILE *stream,
                                                 vsx_uint64_vec_t vec)
{
    fprintf(stream, "%llu, %llu", vec[0], vec[1]);
}

#define DEFINE_VSX_ALL_EQ_FUNC(LANE_TYPE, CMP_INSN)                   \
static inline int vsx_##LANE_TYPE##_all_eq(vsx_##LANE_TYPE##_vec_t a, \
                                           vsx_##LANE_TYPE##_vec_t b) \
{                                                                     \
    unsigned result;                                                  \
    vsx_##LANE_TYPE##_vec_t is_eq_mask_vec;                           \
    asm(#CMP_INSN ". %0, %2, %3\n\t"                                  \
        "mfocrf %1, 2"                                                \
        : "=v" (is_eq_mask_vec), "=r" (result)                        \
        : "v" (a), "v" (b)                                            \
        : "cr6");                                                     \
    return (int)((result >> 7) & 1u);                                 \
}

DEFINE_VSX_ALL_EQ_FUNC(int32, vcmpequw)
DEFINE_VSX_ALL_EQ_FUNC(uint32, vcmpequw)
DEFINE_VSX_ALL_EQ_FUNC(int64, vcmpequd)
DEFINE_VSX_ALL_EQ_FUNC(uint64, vcmpequd)

#define DEFINE_VSX_F2I_TEST_FUNC(SRC_T, DEST_T)                          \
static inline int test_vsx_conv_##SRC_T##_vec_to_##DEST_T##_vec(         \
    vsx_##SRC_T##_vec_t src_v)                                           \
{                                                                        \
    const vsx_##SRC_T##_vec_t is_nan_mask = vsx_##SRC_T##_is_nan(src_v); \
    const vsx_##SRC_T##_vec_t nan_src_v =                                \
        vsx_##SRC_T##_logical_and(src_v, is_nan_mask);                   \
    const vsx_##SRC_T##_vec_t non_nan_src_v =                            \
        vsx_##SRC_T##_logical_andc(src_v, is_nan_mask);                  \
                                                                         \
    const vsx_##DEST_T##_vec_t expected_result =                         \
        vsx_mask_out_##SRC_T##_vec_to_##DEST_T##_vec(                    \
            vsx_##DEST_T##_logical_or(                                   \
                vsx_convert_##SRC_T##_vec_to_##DEST_T##_vec(nan_src_v),  \
                vsx_convert_##SRC_T##_vec_to_##DEST_T##_vec(             \
                    non_nan_src_v)));                                    \
    const vsx_##DEST_T##_vec_t actual_result =                           \
        vsx_mask_out_##SRC_T##_vec_to_##DEST_T##_vec(                    \
            vsx_convert_##SRC_T##_vec_to_##DEST_T##_vec(src_v));         \
    const int test_result =                                              \
        vsx_##DEST_T##_all_eq(expected_result, actual_result);           \
                                                                         \
    if (unlikely(test_result == 0)) {                                    \
        fputs("FAIL: Conversion of " #SRC_T " vector to " #DEST_T        \
              " vector failed\n", stdout);                               \
        fputs("Source values: ", stdout);                                \
        print_vsx_##SRC_T##_vec_elements(stdout, src_v);                 \
        fputs("\nExpected result: ", stdout);                            \
        print_vsx_##DEST_T##_vec_elements(stdout, expected_result);      \
        fputs("\nActual result: ", stdout);                              \
        print_vsx_##DEST_T##_vec_elements(stdout, actual_result);        \
        fputs("\n\n", stdout);                                           \
    }                                                                    \
                                                                         \
    return test_result;                                                  \
}


DEFINE_VSX_F2I_TEST_FUNC(float32, int32)
DEFINE_VSX_F2I_TEST_FUNC(float32, uint32)
DEFINE_VSX_F2I_TEST_FUNC(float32, int64)
DEFINE_VSX_F2I_TEST_FUNC(float32, uint64)
DEFINE_VSX_F2I_TEST_FUNC(float64, int32)
DEFINE_VSX_F2I_TEST_FUNC(float64, uint32)
DEFINE_VSX_F2I_TEST_FUNC(float64, int64)
DEFINE_VSX_F2I_TEST_FUNC(float64, uint64)

static inline vsx_int32_vec_t vsx_int32_vec_from_mask(int mask)
{
    const vsx_int32_vec_t bits_to_test = {1, 2, 4, 8};
    const vsx_int32_vec_t vec_mask = {mask, mask, mask, mask};
    vsx_int32_vec_t result;

    asm("vcmpequw %0, %1, %2"
        : "=v" (result)
        : "v" (vsx_int32_logical_and(vec_mask, bits_to_test)),
          "v" (bits_to_test));
    return result;
}

static inline vsx_int64_vec_t vsx_int64_vec_from_mask(int mask)
{
    const vsx_int64_vec_t bits_to_test = {1, 2};
    const vsx_int64_vec_t vec_mask = {mask, mask};
    vsx_int64_vec_t result;

    asm("vcmpequd %0, %1, %2"
        : "=v" (result)
        : "v" (vsx_int64_logical_and(vec_mask, bits_to_test)),
          "v" (bits_to_test));
    return result;
}

int main(void)
{
    const vsx_float32_vec_t f32_iota1 = {1.0f, 2.0f, 3.0f, 4.0f};
    const vsx_float64_vec_t f64_iota1 = {1.0, 2.0};

    int num_of_tests_failed = 0;

    for (int i = 0; i < 16; i++) {
        const vsx_int32_vec_t nan_mask = vsx_int32_vec_from_mask(i);
        const vsx_float32_vec_t f32_v =
            vsx_float32_logical_or(f32_iota1, (vsx_float32_vec_t)nan_mask);
        num_of_tests_failed +=
            (int)(!test_vsx_conv_float32_vec_to_int32_vec(f32_v));
        num_of_tests_failed +=
            (int)(!test_vsx_conv_float32_vec_to_int64_vec(f32_v));
        num_of_tests_failed +=
            (int)(!test_vsx_conv_float32_vec_to_uint32_vec(f32_v));
        num_of_tests_failed +=
            (int)(!test_vsx_conv_float32_vec_to_uint64_vec(f32_v));
    }

    for (int i = 0; i < 4; i++) {
        const vsx_int64_vec_t nan_mask = vsx_int64_vec_from_mask(i);
        const vsx_float64_vec_t f64_v =
            vsx_float64_logical_or(f64_iota1, (vsx_float64_vec_t)nan_mask);
        num_of_tests_failed +=
            (int)(!test_vsx_conv_float64_vec_to_int32_vec(f64_v));
        num_of_tests_failed +=
            (int)(!test_vsx_conv_float64_vec_to_int64_vec(f64_v));
        num_of_tests_failed +=
            (int)(!test_vsx_conv_float64_vec_to_uint32_vec(f64_v));
        num_of_tests_failed +=
            (int)(!test_vsx_conv_float64_vec_to_uint64_vec(f64_v));
    }

    printf("%d tests failed\n", num_of_tests_failed);
    return (int)(num_of_tests_failed != 0);
}
