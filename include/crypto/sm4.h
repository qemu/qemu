#ifndef QEMU_SM4_H
#define QEMU_SM4_H

extern const uint8_t sm4_sbox[256];
extern const uint32_t sm4_ck[32];

static inline uint32_t sm4_subword(uint32_t word)
{
    return sm4_sbox[word & 0xff] |
           sm4_sbox[(word >> 8) & 0xff] << 8 |
           sm4_sbox[(word >> 16) & 0xff] << 16 |
           sm4_sbox[(word >> 24) & 0xff] << 24;
}

#endif
