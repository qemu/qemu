#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

static void test_chrl(void)
{
    uint32_t program_mask, cc;

    asm volatile (
        ".pushsection .rodata\n"
        "0:\n\t"
        ".short 1, 0x8000\n\t"
        ".popsection\n\t"

        "chrl %[r], 0b\n\t"
        "ipm %[program_mask]\n"
        : [program_mask] "=r" (program_mask)
        : [r] "r" (1)
    );

    cc = program_mask >> 28;
    assert(!cc);

    asm volatile (
        ".pushsection .rodata\n"
        "0:\n\t"
        ".short -1, 0x8000\n\t"
        ".popsection\n\t"

        "chrl %[r], 0b\n\t"
        "ipm %[program_mask]\n"
        : [program_mask] "=r" (program_mask)
        : [r] "r" (-1)
    );

    cc = program_mask >> 28;
    assert(!cc);
}

static void test_cghrl(void)
{
    uint32_t program_mask, cc;

    asm volatile (
        ".pushsection .rodata\n"
        "0:\n\t"
        ".short 1, 0x8000, 0, 0\n\t"
        ".popsection\n\t"

        "cghrl %[r], 0b\n\t"
        "ipm %[program_mask]\n"
        : [program_mask] "=r" (program_mask)
        : [r] "r" (1L)
    );

    cc = program_mask >> 28;
    assert(!cc);

    asm volatile (
        ".pushsection .rodata\n"
        "0:\n\t"
        ".short -1, 0x8000, 0, 0\n\t"
        ".popsection\n\t"

        "cghrl %[r], 0b\n\t"
        "ipm %[program_mask]\n"
        : [program_mask] "=r" (program_mask)
        : [r] "r" (-1L)
    );

    cc = program_mask >> 28;
    assert(!cc);
}

int main(void)
{
    test_chrl();
    test_cghrl();
    return EXIT_SUCCESS;
}
