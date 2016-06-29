#ifndef QEMU_BCD_H
#define QEMU_BCD_H

/* Convert a byte between binary and BCD.  */
static inline uint8_t to_bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

static inline uint8_t from_bcd(uint8_t val)
{
    return ((val >> 4) * 10) + (val & 0x0f);
}

#endif
