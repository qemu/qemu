#ifndef QEMU_TESTS_S390X_VX_H
#define QEMU_TESTS_S390X_VX_H

typedef union S390Vector {
    uint64_t d[2];  /* doubleword */
    uint32_t w[4];  /* word */
    uint16_t h[8];  /* halfword */
    uint8_t  b[16]; /* byte */
    float    f[4];  /* float32 */
    double   fd[2]; /* float64 */
    __uint128_t v;
} S390Vector;

#define ES8  0
#define ES16 1
#define ES32 2
#define ES64 3

#endif /* QEMU_TESTS_S390X_VX_H */
