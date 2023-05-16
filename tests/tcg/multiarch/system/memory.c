/*
 * Memory Test
 *
 * This is intended to test the softmmu code and ensure we properly
 * behave across normal and unaligned accesses across several pages.
 * We are not replicating memory tests for stuck bits and other
 * hardware level failures but looking for issues with different size
 * accesses when access is:
 *
 *   - unaligned at various sizes (if -DCHECK_UNALIGNED set)
 *   - spanning a (softmmu) page
 *   - sign extension when loading
 */

#include <stdint.h>
#include <stdbool.h>
#include <minilib.h>

#ifndef CHECK_UNALIGNED
# error "Target does not specify CHECK_UNALIGNED"
#endif

#define MEM_PAGE_SIZE 4096             /* nominal 4k "pages" */
#define TEST_SIZE (MEM_PAGE_SIZE * 4)  /* 4 pages */

#define ARRAY_SIZE(x) ((sizeof(x) / sizeof((x)[0])))

__attribute__((aligned(MEM_PAGE_SIZE)))
static uint8_t test_data[TEST_SIZE];

typedef void (*init_ufn) (int offset);
typedef bool (*read_ufn) (int offset);
typedef bool (*read_sfn) (int offset, bool nf);

static void pdot(int count)
{
    if (count % 128 == 0) {
        ml_printf(".");
    }
}

/*
 * Helper macros for endian handling.
 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define BYTE_SHIFT(b, pos) (b << (pos * 8))
#define BYTE_NEXT(b) ((b)++)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BYTE_SHIFT(b, pos) (b << ((sizeof(b) - 1 - (pos)) * 8))
#define BYTE_NEXT(b) (--(b))
#else
#error Unsupported __BYTE_ORDER__
#endif

/*
 * Fill the data with ascending (for little-endian) or descending (for
 * big-endian) value bytes.
 */

static void init_test_data_u8(int unused_offset)
{
    uint8_t count = 0, *ptr = &test_data[0];
    int i;
    (void)(unused_offset);

    ml_printf("Filling test area with u8:");
    for (i = 0; i < TEST_SIZE; i++) {
        *ptr++ = BYTE_NEXT(count);
        pdot(i);
    }
    ml_printf("done\n");
}

/*
 * Fill the data with alternating positive and negative bytes. This
 * should mean for reads larger than a byte all subsequent reads will
 * stay either negative or positive. We never write 0.
 */

static inline uint8_t get_byte(int index, bool neg)
{
    return neg ? (0xff << (index % 7)) : (0xff >> ((index % 6) + 1));
}

static void init_test_data_s8(bool neg_first)
{
    uint8_t top, bottom, *ptr = &test_data[0];
    int i;

    ml_printf("Filling test area with s8 pairs (%s):",
              neg_first ? "neg first" : "pos first");
    for (i = 0; i < TEST_SIZE / 2; i++) {
        *ptr++ = get_byte(i, neg_first);
        *ptr++ = get_byte(i, !neg_first);
        pdot(i);
    }
    ml_printf("done\n");
}

/*
 * Zero the first few bytes of the test data in preparation for
 * new offset values.
 */
static void reset_start_data(int offset)
{
    uint32_t *ptr = (uint32_t *) &test_data[0];
    int i;
    for (i = 0; i < offset; i++) {
        *ptr++ = 0;
    }
}

static void init_test_data_u16(int offset)
{
    uint8_t count = 0;
    uint16_t word, *ptr = (uint16_t *) &test_data[offset];
    const int max = (TEST_SIZE - offset) / sizeof(word);
    int i;

    ml_printf("Filling test area with u16 (offset %d, %p):", offset, ptr);

    reset_start_data(offset);

    for (i = 0; i < max; i++) {
        uint16_t low = BYTE_NEXT(count), high = BYTE_NEXT(count);
        word = BYTE_SHIFT(high, 1) | BYTE_SHIFT(low, 0);
        *ptr++ = word;
        pdot(i);
    }
    ml_printf("done @ %p\n", ptr);
}

static void init_test_data_u32(int offset)
{
    uint8_t count = 0;
    uint32_t word, *ptr = (uint32_t *) &test_data[offset];
    const int max = (TEST_SIZE - offset) / sizeof(word);
    int i;

    ml_printf("Filling test area with u32 (offset %d, %p):", offset, ptr);

    reset_start_data(offset);

    for (i = 0; i < max; i++) {
        uint32_t b4 = BYTE_NEXT(count), b3 = BYTE_NEXT(count);
        uint32_t b2 = BYTE_NEXT(count), b1 = BYTE_NEXT(count);
        word = BYTE_SHIFT(b1, 3) | BYTE_SHIFT(b2, 2) | BYTE_SHIFT(b3, 1) |
               BYTE_SHIFT(b4, 0);
        *ptr++ = word;
        pdot(i);
    }
    ml_printf("done @ %p\n", ptr);
}

static void init_test_data_u64(int offset)
{
    uint8_t count = 0;
    uint64_t word, *ptr = (uint64_t *) &test_data[offset];
    const int max = (TEST_SIZE - offset) / sizeof(word);
    int i;

    ml_printf("Filling test area with u64 (offset %d, %p):", offset, ptr);

    reset_start_data(offset);

    for (i = 0; i < max; i++) {
        uint64_t b8 = BYTE_NEXT(count), b7 = BYTE_NEXT(count);
        uint64_t b6 = BYTE_NEXT(count), b5 = BYTE_NEXT(count);
        uint64_t b4 = BYTE_NEXT(count), b3 = BYTE_NEXT(count);
        uint64_t b2 = BYTE_NEXT(count), b1 = BYTE_NEXT(count);
        word = BYTE_SHIFT(b1, 7) | BYTE_SHIFT(b2, 6) | BYTE_SHIFT(b3, 5) |
               BYTE_SHIFT(b4, 4) | BYTE_SHIFT(b5, 3) | BYTE_SHIFT(b6, 2) |
               BYTE_SHIFT(b7, 1) | BYTE_SHIFT(b8, 0);
        *ptr++ = word;
        pdot(i);
    }
    ml_printf("done @ %p\n", ptr);
}

static bool read_test_data_u16(int offset)
{
    uint16_t word, *ptr = (uint16_t *)&test_data[offset];
    int i;
    const int max = (TEST_SIZE - offset) / sizeof(word);

    ml_printf("Reading u16 from %#lx (offset %d):", ptr, offset);

    for (i = 0; i < max; i++) {
        uint8_t high, low;
        word = *ptr++;
        high = (word >> 8) & 0xff;
        low = word & 0xff;
        if (high < low && high != 0) {
            ml_printf("Error %d < %d\n", high, low);
            return false;
        } else {
            pdot(i);
        }

    }
    ml_printf("done @ %p\n", ptr);
    return true;
}

static bool read_test_data_u32(int offset)
{
    uint32_t word, *ptr = (uint32_t *)&test_data[offset];
    int i;
    const int max = (TEST_SIZE - offset) / sizeof(word);

    ml_printf("Reading u32 from %#lx (offset %d):", ptr, offset);

    for (i = 0; i < max; i++) {
        uint8_t b1, b2, b3, b4;
        int zeros = 0;
        word = *ptr++;

        b1 = word >> 24 & 0xff;
        b2 = word >> 16 & 0xff;
        b3 = word >> 8 & 0xff;
        b4 = word & 0xff;

        zeros += (b1 == 0 ? 1 : 0);
        zeros += (b2 == 0 ? 1 : 0);
        zeros += (b3 == 0 ? 1 : 0);
        zeros += (b4 == 0 ? 1 : 0);
        if (zeros > 1) {
            ml_printf("Error @ %p, more zeros than expected: %d, %d, %d, %d",
                      ptr - 1, b1, b2, b3, b4);
            return false;
        }

        if ((b1 < b2 && b1 != 0) ||
            (b2 < b3 && b2 != 0) ||
            (b3 < b4 && b3 != 0)) {
            ml_printf("Error %d, %d, %d, %d", b1, b2, b3, b4);
            return false;
        } else {
            pdot(i);
        }
    }
    ml_printf("done @ %p\n", ptr);
    return true;
}

static bool read_test_data_u64(int offset)
{
    uint64_t word, *ptr = (uint64_t *)&test_data[offset];
    int i;
    const int max = (TEST_SIZE - offset) / sizeof(word);

    ml_printf("Reading u64 from %#lx (offset %d):", ptr, offset);

    for (i = 0; i < max; i++) {
        uint8_t b1, b2, b3, b4, b5, b6, b7, b8;
        int zeros = 0;
        word = *ptr++;

        b1 = ((uint64_t) (word >> 56)) & 0xff;
        b2 = ((uint64_t) (word >> 48)) & 0xff;
        b3 = ((uint64_t) (word >> 40)) & 0xff;
        b4 = (word >> 32) & 0xff;
        b5 = (word >> 24) & 0xff;
        b6 = (word >> 16) & 0xff;
        b7 = (word >> 8)  & 0xff;
        b8 = (word >> 0)  & 0xff;

        zeros += (b1 == 0 ? 1 : 0);
        zeros += (b2 == 0 ? 1 : 0);
        zeros += (b3 == 0 ? 1 : 0);
        zeros += (b4 == 0 ? 1 : 0);
        zeros += (b5 == 0 ? 1 : 0);
        zeros += (b6 == 0 ? 1 : 0);
        zeros += (b7 == 0 ? 1 : 0);
        zeros += (b8 == 0 ? 1 : 0);
        if (zeros > 1) {
            ml_printf("Error @ %p, more zeros than expected: %d, %d, %d, %d, %d, %d, %d, %d",
                      ptr - 1, b1, b2, b3, b4, b5, b6, b7, b8);
            return false;
        }

        if ((b1 < b2 && b1 != 0) ||
            (b2 < b3 && b2 != 0) ||
            (b3 < b4 && b3 != 0) ||
            (b4 < b5 && b4 != 0) ||
            (b5 < b6 && b5 != 0) ||
            (b6 < b7 && b6 != 0) ||
            (b7 < b8 && b7 != 0)) {
            ml_printf("Error %d, %d, %d, %d, %d, %d, %d, %d",
                      b1, b2, b3, b4, b5, b6, b7, b8);
            return false;
        } else {
            pdot(i);
        }
    }
    ml_printf("done @ %p\n", ptr);
    return true;
}

/* Read the test data and verify at various offsets */
read_ufn read_ufns[] = { read_test_data_u16,
                         read_test_data_u32,
                         read_test_data_u64 };

bool do_unsigned_reads(int start_off)
{
    int i;
    bool ok = true;

    for (i = 0; i < ARRAY_SIZE(read_ufns) && ok; i++) {
#if CHECK_UNALIGNED
        int off;
        for (off = start_off; off < 8 && ok; off++) {
            ok = read_ufns[i](off);
        }
#else
        ok = read_ufns[i](start_off);
#endif
    }

    return ok;
}

static bool do_unsigned_test(init_ufn fn)
{
#if CHECK_UNALIGNED
    bool ok = true;
    int i;
    for (i = 0; i < 8 && ok; i++) {
        fn(i);
        ok = do_unsigned_reads(i);
    }
    return ok;
#else
    fn(0);
    return do_unsigned_reads(0);
#endif
}

/*
 * We need to ensure signed data is read into a larger data type to
 * ensure that sign extension is working properly.
 */

static bool read_test_data_s8(int offset, bool neg_first)
{
    int8_t *ptr = (int8_t *)&test_data[offset];
    int i;
    const int max = (TEST_SIZE - offset) / 2;

    ml_printf("Reading s8 pairs from %#lx (offset %d):", ptr, offset);

    for (i = 0; i < max; i++) {
        int16_t first, second;
        bool ok;
        first = *ptr++;
        second = *ptr++;

        if (neg_first && first < 0 && second > 0) {
            pdot(i);
        } else if (!neg_first && first > 0 && second < 0) {
            pdot(i);
        } else {
            ml_printf("Error %d %c %d\n", first, neg_first ? '<' : '>', second);
            return false;
        }
    }
    ml_printf("done @ %p\n", ptr);
    return true;
}

static bool read_test_data_s16(int offset, bool neg_first)
{
    int16_t *ptr = (int16_t *)&test_data[offset];
    int i;
    const int max = (TEST_SIZE - offset) / (sizeof(*ptr));

    ml_printf("Reading s16 from %#lx (offset %d, %s):", ptr,
              offset, neg_first ? "neg" : "pos");

    /*
     * If the first byte is negative, then the last byte is positive.
     * Therefore the logic below must be flipped for big-endian.
     */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    neg_first = !neg_first;
#endif

    for (i = 0; i < max; i++) {
        int32_t data = *ptr++;

        if (neg_first && data < 0) {
            pdot(i);
        } else if (!neg_first && data > 0) {
            pdot(i);
        } else {
            ml_printf("Error %d %c 0\n", data, neg_first ? '<' : '>');
            return false;
        }
    }
    ml_printf("done @ %p\n", ptr);
    return true;
}

static bool read_test_data_s32(int offset, bool neg_first)
{
    int32_t *ptr = (int32_t *)&test_data[offset];
    int i;
    const int max = (TEST_SIZE - offset) / (sizeof(int32_t));

    ml_printf("Reading s32 from %#lx (offset %d, %s):",
              ptr, offset, neg_first ? "neg" : "pos");

    /*
     * If the first byte is negative, then the last byte is positive.
     * Therefore the logic below must be flipped for big-endian.
     */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    neg_first = !neg_first;
#endif

    for (i = 0; i < max; i++) {
        int64_t data = *ptr++;

        if (neg_first && data < 0) {
            pdot(i);
        } else if (!neg_first && data > 0) {
            pdot(i);
        } else {
            ml_printf("Error %d %c 0\n", data, neg_first ? '<' : '>');
            return false;
        }
    }
    ml_printf("done @ %p\n", ptr);
    return true;
}

/*
 * Read the test data and verify at various offsets
 *
 * For everything except bytes all our reads should be either positive
 * or negative depending on what offset we are reading from.
 */
read_sfn read_sfns[] = { read_test_data_s8,
                         read_test_data_s16,
                         read_test_data_s32 };

bool do_signed_reads(bool neg_first)
{
    int i;
    bool ok = true;

    for (i = 0; i < ARRAY_SIZE(read_sfns) && ok; i++) {
#if CHECK_UNALIGNED
        int off;
        for (off = 0; off < 8 && ok; off++) {
            bool nf = i == 0 ? neg_first ^ (off & 1) : !(neg_first ^ (off & 1));
            ok = read_sfns[i](off, nf);
        }
#else
        ok = read_sfns[i](0, i == 0 ? neg_first : !neg_first);
#endif
    }

    return ok;
}

init_ufn init_ufns[] = { init_test_data_u8,
                         init_test_data_u16,
                         init_test_data_u32,
                         init_test_data_u64 };

int main(void)
{
    int i;
    bool ok = true;

    /* Run through the unsigned tests first */
    for (i = 0; i < ARRAY_SIZE(init_ufns) && ok; i++) {
        ok = do_unsigned_test(init_ufns[i]);
    }

    if (ok) {
        init_test_data_s8(false);
        ok = do_signed_reads(false);
    }

    if (ok) {
        init_test_data_s8(true);
        ok = do_signed_reads(true);
    }

    ml_printf("Test complete: %s\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : -1;
}
