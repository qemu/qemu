/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * See https://gitlab.com/qemu-project/qemu/-/issues/3522
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FXSAVE_FOP_OFFSET      6
#define FXSAVE_X87_OFFSET      32
#define FXSAVE_SLOT_SIZE       16
#define FXSAVE_RESERVED_START  10
#define FXSAVE_NUM_SLOTS       8

struct fxsave_area {
    uint8_t raw[512];
} __attribute__((aligned(16)));

_Static_assert(sizeof(struct fxsave_area) == 512,
                "FXSAVE area must be exactly 512 bytes");

static uint16_t u16_le(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

int main(void)
{
    struct fxsave_area area;
    uint16_t fop;

    memset(&area, 0xcc, sizeof(area));

    __asm__ volatile(
        "fninit\n\t"
        "fxsave64 %0"
        : "+m" (area)
        :
        : "memory"
    );

    fop = u16_le(&area.raw[FXSAVE_FOP_OFFSET]);
    if (fop != 0) {
        fprintf(stderr, "FOP: expected 0, got 0x%04x\n", (unsigned)fop);
        return 1;
    }

    for (int slot = 0; slot < FXSAVE_NUM_SLOTS; slot++) {
        int base = FXSAVE_X87_OFFSET + slot * FXSAVE_SLOT_SIZE;
        for (int b = FXSAVE_RESERVED_START; b < FXSAVE_SLOT_SIZE; b++) {
            uint8_t val = area.raw[base + b];
            if (val != 0) {
                fprintf(stderr,
                        "Slot %d byte %d (offset %d): expected 0x00, "
                        "got 0x%02x\n",
                        slot, b, base + b, (unsigned)val);
                return 1;
            }
        }
    }

    return 0;
}
