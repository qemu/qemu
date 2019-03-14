/*
 * Memory Test
 *
 * This is intended to test the softmmu code and ensure we properly
 * behave across normal and unaligned accesses across several pages.
 * We are not replicating memory tests for stuck bits and other
 * hardware level failures but looking for issues with different size
 * accesses when:

 *
 */

#include <inttypes.h>
#include <minilib.h>

#define TEST_SIZE (4096 * 4)  /* 4 pages */

static uint8_t test_data[TEST_SIZE];

static void pdot(int count)
{
    if (count % 128 == 0) {
        ml_printf(".");
    }
}


/*
 * Fill the data with ascending value bytes. As x86 is a LE machine we
 * write in ascending order and then read and high byte should either
 * be zero or higher than the lower bytes.
 */

static void init_test_data_u8(void)
{
    uint8_t count = 0, *ptr = &test_data[0];
    int i;

    ml_printf("Filling test area with u8:");
    for (i = 0; i < TEST_SIZE; i++) {
        *ptr++ = count++;
        pdot(i);
    }
    ml_printf("done\n");
}

static void init_test_data_u16(int offset)
{
    uint8_t count = 0;
    uint16_t word, *ptr = (uint16_t *) &test_data[0];
    const int max = (TEST_SIZE - offset) / sizeof(word);
    int i;

    ml_printf("Filling test area with u16 (offset %d):", offset);

    /* Leading zeros */
    for (i = 0; i < offset; i++) {
        *ptr = 0;
    }

    ptr = (uint16_t *) &test_data[offset];
    for (i = 0; i < max; i++) {
        uint8_t high, low;
        low = count++;
        high = count++;
        word = (high << 8) | low;
        *ptr++ = word;
        pdot(i);
    }
    ml_printf("done\n");
}

static void init_test_data_u32(int offset)
{
    uint8_t count = 0;
    uint32_t word, *ptr = (uint32_t *) &test_data[0];
    const int max = (TEST_SIZE - offset) / sizeof(word);
    int i;

    ml_printf("Filling test area with u32 (offset %d):", offset);

    /* Leading zeros */
    for (i = 0; i < offset; i++) {
        *ptr = 0;
    }

    ptr = (uint32_t *) &test_data[offset];
    for (i = 0; i < max; i++) {
        uint8_t b1, b2, b3, b4;
        b4 = count++;
        b3 = count++;
        b2 = count++;
        b1 = count++;
        word = (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
        *ptr++ = word;
        pdot(i);
    }
    ml_printf("done\n");
}


static int read_test_data_u16(int offset)
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
            return 1;
        } else {
            pdot(i);
        }

    }
    ml_printf("done\n");
    return 0;
}

static int read_test_data_u32(int offset)
{
    uint32_t word, *ptr = (uint32_t *)&test_data[offset];
    int i;
    const int max = (TEST_SIZE - offset) / sizeof(word);

    ml_printf("Reading u32 from %#lx (offset %d):", ptr, offset);

    for (i = 0; i < max; i++) {
        uint8_t b1, b2, b3, b4;
        word = *ptr++;

        b1 = word >> 24 & 0xff;
        b2 = word >> 16 & 0xff;
        b3 = word >> 8 & 0xff;
        b4 = word & 0xff;

        if ((b1 < b2 && b1 != 0) ||
            (b2 < b3 && b2 != 0) ||
            (b3 < b4 && b3 != 0)) {
            ml_printf("Error %d, %d, %d, %d", b1, b2, b3, b4);
            return 2;
        } else {
            pdot(i);
        }
    }
    ml_printf("done\n");
    return 0;
}

static int read_test_data_u64(int offset)
{
    uint64_t word, *ptr = (uint64_t *)&test_data[offset];
    int i;
    const int max = (TEST_SIZE - offset) / sizeof(word);

    ml_printf("Reading u64 from %#lx (offset %d):", ptr, offset);

    for (i = 0; i < max; i++) {
        uint8_t b1, b2, b3, b4, b5, b6, b7, b8;
        word = *ptr++;

        b1 = ((uint64_t) (word >> 56)) & 0xff;
        b2 = ((uint64_t) (word >> 48)) & 0xff;
        b3 = ((uint64_t) (word >> 40)) & 0xff;
        b4 = (word >> 32) & 0xff;
        b5 = (word >> 24) & 0xff;
        b6 = (word >> 16) & 0xff;
        b7 = (word >> 8)  & 0xff;
        b8 = (word >> 0)  & 0xff;

        if ((b1 < b2 && b1 != 0) ||
            (b2 < b3 && b2 != 0) ||
            (b3 < b4 && b3 != 0) ||
            (b4 < b5 && b4 != 0) ||
            (b5 < b6 && b5 != 0) ||
            (b6 < b7 && b6 != 0) ||
            (b7 < b8 && b7 != 0)) {
            ml_printf("Error %d, %d, %d, %d, %d, %d, %d, %d",
                      b1, b2, b3, b4, b5, b6, b7, b8);
            return 2;
        } else {
            pdot(i);
        }
    }
    ml_printf("done\n");
    return 0;
}

/* Read the test data and verify at various offsets */
int do_reads(void)
{
    int r = 0;
    int off = 0;

    while (r == 0 && off < 8) {
        r = read_test_data_u16(off);
        r |= read_test_data_u32(off);
        r |= read_test_data_u64(off);
        off++;
    }

    return r;
}

int main(void)
{
    int i, r = 0;


    init_test_data_u8();
    r = do_reads();
    if (r) {
        return r;
    }

    for (i = 0; i < 8; i++) {
        init_test_data_u16(i);

        r = do_reads();
        if (r) {
            return r;
        }
    }

    for (i = 0; i < 8; i++) {
        init_test_data_u32(i);

        r = do_reads();
        if (r) {
            return r;
        }
    }

    ml_printf("Test complete: %s\n", r == 0 ? "PASSED" : "FAILED");
    return r;
}
