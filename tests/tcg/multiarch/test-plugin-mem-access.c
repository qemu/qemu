/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Check if we detect all memory accesses expected using plugin API.
 * Used in conjunction with ./check-plugin-mem-access.sh check script.
 * Output of this program is the list of patterns expected in plugin output.
 *
 * 8,16,32 load/store are tested for all arch.
 * 64,128 load/store are tested for aarch64/x64.
 * atomic operations (8,16,32,64) are tested for x64 only.
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__x86_64__)
#include <emmintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif /* __x86_64__ */

static void *data;

/* ,store_u8,.*,8,store,0xf1 */
#define PRINT_EXPECTED(function, type, value, action)                 \
do {                                                                  \
    printf(",%s,.*,%d,%s,%s\n",                                       \
           #function, (int) sizeof(type) * 8, action, value);         \
}                                                                     \
while (0)

#define DEFINE_STORE(name, type, value)                  \
                                                         \
static void print_expected_store_##name(void)            \
{                                                        \
    PRINT_EXPECTED(store_##name, type, #value, "store"); \
}                                                        \
                                                         \
static void store_##name(void)                           \
{                                                        \
    *((type *)data) = value;                             \
    print_expected_store_##name();                       \
}

#define DEFINE_ATOMIC_OP(name, type, value)                    \
                                                               \
static void print_expected_atomic_op_##name(void)              \
{                                                              \
    PRINT_EXPECTED(atomic_op_##name, type, "0x0*42", "load");  \
    PRINT_EXPECTED(atomic_op_##name, type, #value, "store");   \
}                                                              \
                                                               \
static void atomic_op_##name(void)                             \
{                                                              \
    *((type *)data) = 0x42;                                    \
    __sync_val_compare_and_swap((type *)data, 0x42, value);    \
    print_expected_atomic_op_##name();                         \
}

#define DEFINE_LOAD(name, type, value)                  \
                                                        \
static void print_expected_load_##name(void)            \
{                                                       \
    PRINT_EXPECTED(load_##name, type, #value, "load");  \
}                                                       \
                                                        \
static void load_##name(void)                           \
{                                                       \
                                                        \
    /* volatile forces load to be generated. */         \
    volatile type src = *((type *) data);               \
    volatile type dest = src;                           \
    (void)src, (void)dest;                              \
    print_expected_load_##name();                       \
}

DEFINE_STORE(u8, uint8_t, 0xf1)
DEFINE_LOAD(u8, uint8_t, 0xf1)
DEFINE_STORE(u16, uint16_t, 0xf123)
DEFINE_LOAD(u16, uint16_t, 0xf123)
DEFINE_STORE(u32, uint32_t, 0xff112233)
DEFINE_LOAD(u32, uint32_t, 0xff112233)

#if defined(__x86_64__) || defined(__aarch64__)
DEFINE_STORE(u64, uint64_t, 0xf123456789abcdef)
DEFINE_LOAD(u64, uint64_t, 0xf123456789abcdef)

static void print_expected_store_u128(void)
{
    PRINT_EXPECTED(store_u128, __int128,
                   "0xf122334455667788f123456789abcdef", "store");
}

static void store_u128(void)
{
#ifdef __x86_64__
    _mm_store_si128(data, _mm_set_epi32(0xf1223344, 0x55667788,
                                        0xf1234567, 0x89abcdef));
#else
    const uint32_t init[4] = {0x89abcdef, 0xf1234567, 0x55667788, 0xf1223344};
    uint32x4_t vec = vld1q_u32(init);
    vst1q_u32(data, vec);
#endif /* __x86_64__ */
    print_expected_store_u128();
}

static void print_expected_load_u128(void)
{
    PRINT_EXPECTED(load_u128, __int128,
                   "0xf122334455667788f123456789abcdef", "load");
}

static void load_u128(void)
{
#ifdef __x86_64__
    __m128i var = _mm_load_si128(data);
#else
    uint32x4_t var = vld1q_u32(data);
#endif
    (void) var;
    print_expected_load_u128();
}
#endif /* __x86_64__ || __aarch64__ */

#if defined(__x86_64__)
DEFINE_ATOMIC_OP(u8, uint8_t, 0xf1)
DEFINE_ATOMIC_OP(u16, uint16_t, 0xf123)
DEFINE_ATOMIC_OP(u32, uint32_t, 0xff112233)
DEFINE_ATOMIC_OP(u64, uint64_t, 0xf123456789abcdef)
#endif /* __x86_64__ */

static void *f(void *p)
{
    return NULL;
}

int main(void)
{
    /*
     * We force creation of a second thread to enable cpu flag CF_PARALLEL.
     * This will generate atomic operations when needed.
     */
    pthread_t thread;
    pthread_create(&thread, NULL, &f, NULL);
    pthread_join(thread, NULL);

    /* allocate storage up to 128 bits */
    data = malloc(16);

    store_u8();
    load_u8();

    store_u16();
    load_u16();

    store_u32();
    load_u32();

#if defined(__x86_64__) || defined(__aarch64__)
    store_u64();
    load_u64();

    store_u128();
    load_u128();
#endif /* __x86_64__ || __aarch64__ */

#if defined(__x86_64__)
    atomic_op_u8();
    atomic_op_u16();
    atomic_op_u32();
    atomic_op_u64();
#endif /* __x86_64__ */

    free(data);
}
