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

#endif /* LOONGARCH_VEC_H */
