/*
 * Test modifying code using the MVC instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <minilib.h>

#define PAGE_SIZE 4096
#define BR_14_SIZE 2
#define RWX_OFFSET 2

static unsigned char rw[PAGE_SIZE + BR_14_SIZE];
static unsigned char rwx[RWX_OFFSET + sizeof(rw)]
    __attribute__((aligned(PAGE_SIZE)));

typedef unsigned long (*function_t)(unsigned long);

static int emit_function(unsigned char *p, int n)
{
    int i = 0, val = 0;

    while (i < n - 2) {
        /* aghi %r2,1 */
        p[i++] = 0xa7;
        p[i++] = 0x2b;
        p[i++] = 0x00;
        p[i++] = 0x01;
        val++;
    }

    /* br %r14 */
    p[i++] = 0x07;
    p[i++] = 0xfe;

    return val;
}

static void memcpy_mvc(void *dest, void *src, unsigned long n)
{
    while (n >= 256) {
        asm("mvc 0(256,%[dest]),0(%[src])"
            :
            : [dest] "a" (dest)
            , [src] "a" (src)
            : "memory");
        dest += 256;
        src += 256;
        n -= 256;
    }
    asm("exrl %[n],0f\n"
        "j 1f\n"
        "0: mvc 0(1,%[dest]),0(%[src])\n"
        "1:"
        :
        : [dest] "a" (dest)
        , [src] "a" (src)
        , [n] "a" (n)
        : "memory");
}

int main(void)
{
    int expected, size;

    /* Create a TB. */
    size = sizeof(rwx) - RWX_OFFSET - 4;
    expected = emit_function(rwx + RWX_OFFSET, size);
    if (((function_t)(rwx + RWX_OFFSET))(0) != expected) {
        return 1;
    }

    /* Overwrite the TB. */
    size += 4;
    expected = emit_function(rw, size);
    memcpy_mvc(rwx + RWX_OFFSET, rw, size);
    if (((function_t)(rwx + RWX_OFFSET))(0) != expected) {
        return 2;
    }

    return 0;
}
