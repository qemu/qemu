/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch vector utilitites
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_VEC_H
#define LOONGARCH_VEC_H

#if HOST_BIG_ENDIAN
#define B(x)  B[(x) ^ 15]
#define H(x)  H[(x) ^ 7]
#define W(x)  W[(x) ^ 3]
#define D(x)  D[(x) ^ 1]
#define UB(x) UB[(x) ^ 15]
#define UH(x) UH[(x) ^ 7]
#define UW(x) UW[(x) ^ 3]
#define UD(x) UD[(x) ^ 1]
#define Q(x)  Q[x]
#else
#define B(x)  B[x]
#define H(x)  H[x]
#define W(x)  W[x]
#define D(x)  D[x]
#define UB(x) UB[x]
#define UH(x) UH[x]
#define UW(x) UW[x]
#define UD(x) UD[x]
#define Q(x)  Q[x]
#endif /* HOST_BIG_ENDIAN */

#define DO_ADD(a, b)  (a + b)
#define DO_SUB(a, b)  (a - b)
#define DO_VAVG(a, b)  ((a >> 1) + (b >> 1) + (a & b & 1))
#define DO_VAVGR(a, b) ((a >> 1) + (b >> 1) + ((a | b) & 1))
#define DO_VABSD(a, b)  ((a > b) ? (a -b) : (b-a))
#define DO_VABS(a)  ((a < 0) ? (-a) : (a))
#define DO_MIN(a, b) (a < b ? a : b)
#define DO_MAX(a, b) (a > b ? a : b)
#define DO_MUL(a, b) (a * b)
#define DO_MADD(a, b, c)  (a + b * c)
#define DO_MSUB(a, b, c)  (a - b * c)

#define DO_DIVU(N, M) (unlikely(M == 0) ? 0 : N / M)
#define DO_REMU(N, M) (unlikely(M == 0) ? 0 : N % M)
#define DO_DIV(N, M)  (unlikely(M == 0) ? 0 :\
        unlikely((N == -N) && (M == (__typeof(N))(-1))) ? N : N / M)
#define DO_REM(N, M)  (unlikely(M == 0) ? 0 :\
        unlikely((N == -N) && (M == (__typeof(N))(-1))) ? 0 : N % M)

#define DO_SIGNCOV(a, b)  (a == 0 ? 0 : a < 0 ? -b : b)

#define R_SHIFT(a, b) (a >> b)

#define DO_CLO_B(N)  (clz32(~N & 0xff) - 24)
#define DO_CLO_H(N)  (clz32(~N & 0xffff) - 16)
#define DO_CLO_W(N)  (clz32(~N))
#define DO_CLO_D(N)  (clz64(~N))
#define DO_CLZ_B(N)  (clz32(N) - 24)
#define DO_CLZ_H(N)  (clz32(N) - 16)
#define DO_CLZ_W(N)  (clz32(N))
#define DO_CLZ_D(N)  (clz64(N))

#define DO_BITCLR(a, bit) (a & ~(1ull << bit))
#define DO_BITSET(a, bit) (a | 1ull << bit)
#define DO_BITREV(a, bit) (a ^ (1ull << bit))

#define VSEQ(a, b) (a == b ? -1 : 0)
#define VSLE(a, b) (a <= b ? -1 : 0)
#define VSLT(a, b) (a < b ? -1 : 0)

#define SHF_POS(i, imm) (((i) & 0xfc) + (((imm) >> (2 * ((i) & 0x03))) & 0x03))

#endif /* LOONGARCH_VEC_H */
