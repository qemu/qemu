/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Defines and inline functions around testing CPACF instructions
 *
 */

#ifndef _S390_CPACF_H_
#define _S390_CPACF_H_

#include "../../../target/s390x/tcg/cpacf-arch.h"

union register_pair {
    unsigned __int128 pair;
    struct {
        unsigned long even;
        unsigned long odd;
    };
};

/*
 * Instruction opcodes for the CPACF instructions
 */
#define CPACF_KMAC              0xb91e          /* MSA  */
#define CPACF_KM                0xb92e          /* MSA  */
#define CPACF_KMC               0xb92f          /* MSA  */
#define CPACF_KIMD              0xb93e          /* MSA  */
#define CPACF_KLMD              0xb93f          /* MSA  */
#define CPACF_PCKMO             0xb928          /* MSA3 */
#define CPACF_KMF               0xb92a          /* MSA4 */
#define CPACF_KMO               0xb92b          /* MSA4 */
#define CPACF_PCC               0xb92c          /* MSA4 */
#define CPACF_KMCTR             0xb92d          /* MSA4 */
#define CPACF_PRNO              0xb93c          /* MSA5 */
#define CPACF_KMA               0xb929          /* MSA8 */
#define CPACF_KDSA              0xb93a          /* MSA9 */

/*
 * 'encrypt' the clear key value into a protected key
 * by xor-ing the protkey_xor_pattern onto it.
 */
static inline void encrypt_clrkey(uint8_t *key, int keysize)
{
    const uint8_t protkey_xor_pattern[32] = PROTKEY_XOR_PATTERN;

    for (int i = 0; i < keysize; i++) {
        key[i] ^= protkey_xor_pattern[i];
    }
}

/**
 * cpacf_km() - executes the KM instruction
 * @func: the function code passed to KM; see CPACF_KM_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @dest: address of destination memory area
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Returns 0 for the query func, number of processed bytes for
 * encryption/decryption funcs
 */
static inline int cpacf_km(unsigned long func, void *param,
                           uint8_t *dest, const uint8_t *src, long src_len,
                           unsigned long *cc)
{
    union register_pair d, s;

    *cc = 0;
    d.even = (unsigned long)dest;
    s.even = (unsigned long)src;
    s.odd  = (unsigned long)src_len;
    asm volatile(
                 "      lgr     0,%[fc]\n"
                 "      lgr     1,%[pba]\n"
                 "0:    .insn   rre,%[opc] << 16,%[dst],%[src]\n"
                 "      brc     1,0b\n" /* handle partial completion */
                 "      brc     8,2f\n"
                 "      brc     4,1f\n"
                 "      agsi    %[__cc],1\n"
                 "1:    agsi    %[__cc],1\n"
                 "2:\n"
                 : [src] "+&d" (s.pair), [dst] "+&d" (d.pair), [__cc] "+Q" (*cc)
                 : [fc] "d" (func), [pba] "d" ((unsigned long)param),
                   [opc] "i" (CPACF_KM)
                 : "cc", "memory", "0", "1");

    return src_len - s.odd;
}

/**
 * cpacf_kmc() - executes the KMC instruction
 * @func: the function code passed to KM; see CPACF_KMC_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @dest: address of destination memory area
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Returns 0 for the query func, number of processed bytes for
 * encryption/decryption funcs
 */
static inline int cpacf_kmc(unsigned long func, void *param,
                            uint8_t *dest, const uint8_t *src, long src_len,
                            unsigned long *cc)
{
    union register_pair d, s;

    *cc = 0;
    d.even = (unsigned long)dest;
    s.even = (unsigned long)src;
    s.odd  = (unsigned long)src_len;
    asm volatile(
                 "      lgr     0,%[fc]\n"
                 "      lgr     1,%[pba]\n"
                 "0:    .insn   rre,%[opc] << 16,%[dst],%[src]\n"
                 "      brc     1,0b\n" /* handle partial completion */
                 "      brc     8,2f\n"
                 "      brc     4,1f\n"
                 "      agsi    %[__cc],1\n"
                 "1:    agsi    %[__cc],1\n"
                 "2:\n"
                 : [src] "+&d" (s.pair), [dst] "+&d" (d.pair), [__cc] "+Q" (*cc)
                 : [fc] "d" (func), [pba] "d" ((unsigned long)param),
                   [opc] "i" (CPACF_KMC)
                 : "cc", "memory", "0", "1");

    return src_len - s.odd;
}

/**
 * cpacf_kimd() - executes the KIMD instruction
 * @func: the function code passed to KM; see CPACF_KIMD_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 */
static inline void cpacf_kimd(unsigned long func, void *param,
                              const uint8_t *src, long src_len,
                              unsigned long *cc)
{
    union register_pair s;

    *cc = 0;
    s.even = (unsigned long)src;
    s.odd  = (unsigned long)src_len;
    asm volatile(
                 "      lgr     0,%[fc]\n"
                 "      lgr     1,%[pba]\n"
                 "0:    .insn   rre,%[opc] << 16,0,%[src]\n"
                 "      brc     1,0b\n" /* handle partial completion */
                 "      brc     8,2f\n"
                 "      brc     4,1f\n"
                 "      agsi    %[__cc],1\n"
                 "1:    agsi    %[__cc],1\n"
                 "2:\n"
                 : [src] "+&d" (s.pair), [__cc] "+Q" (*cc)
                 : [fc] "d" (func), [pba] "d" ((unsigned long)(param)),
                   [opc] "i" (CPACF_KIMD)
                 : "cc", "memory", "0", "1");
}

/**
 * cpacf_klmd() - executes the KLMD instruction
 * @func: the function code passed to KM; see CPACF_KLMD_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 */
static inline void cpacf_klmd(unsigned long func, void *param,
                              const uint8_t *src, long src_len,
                              uint8_t *dest, long dest_len,
                              unsigned long *cc)
{
    union register_pair s, d;

    *cc = 0;
    s.even = (unsigned long)src;
    s.odd  = (unsigned long)src_len;
    d.even = (unsigned long)dest;
    d.odd  = (unsigned long)dest_len;
    asm volatile(
                 "      lgr     0,%[fc]\n"
                 "      lgr     1,%[pba]\n"
                 "0:    .insn   rre,%[opc] << 16,%[dst],%[src]\n"
                 "      brc     1,0b\n" /* handle partial completion */
                 "      brc     8,2f\n"
                 "      brc     4,1f\n"
                 "      agsi    %[__cc],1\n"
                 "1:    agsi    %[__cc],1\n"
                 "2:\n"
                 : [src] "+&d" (s.pair), [dst] "+&d" (d.pair), [__cc] "+Q" (*cc)
                 : [fc] "d" (func), [pba] "d" ((unsigned long)param),
                   [opc] "i" (CPACF_KLMD)
                 : "cc", "memory", "0", "1");
}

/**
 * cpacf_kmac() - executes the KMAC instruction
 * @func: the function code passed to KM; see CPACF_KMAC_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Returns 0 for the query func, number of processed bytes for digest funcs
 */
static inline int cpacf_kmac(unsigned long func, void *param,
                             const uint8_t *src, long src_len,
                             unsigned long *cc)
{
    union register_pair s;

    *cc = 0;
    s.even = (unsigned long)src;
    s.odd  = (unsigned long)src_len;
    asm volatile(
                 "      lgr     0,%[fc]\n"
                 "      lgr     1,%[pba]\n"
                 "0:    .insn   rre,%[opc] << 16,0,%[src]\n"
                 "      brc     1,0b\n" /* handle partial completion */
                 "      brc     8,2f\n"
                 "      brc     4,1f\n"
                 "      agsi    %[__cc],1\n"
                 "1:    agsi    %[__cc],1\n"
                 "2:\n"
                 : [src] "+&d" (s.pair), [__cc] "+Q" (*cc)
                 : [fc] "d" (func), [pba] "d" ((unsigned long)param),
                   [opc] "i" (CPACF_KMAC)
                 : "cc", "memory", "0", "1");

    return src_len - s.odd;
}

static inline int cpacf_kmac_x(unsigned long *func, void *param,
                               const uint8_t *src, long src_len,
                               unsigned long *cc)
{
    union register_pair s;
    unsigned long fc = *func;

    *cc = 0;
    s.even = (unsigned long)src;
    s.odd  = (unsigned long)src_len;
    asm volatile(
                 "      lgr     0,%[fc]\n"
                 "      lgr     1,%[pba]\n"
                 "0:    .insn   rre,%[opc] << 16,0,%[src]\n"
                 "      brc     1,0b\n" /* handle partial completion */
                 "      brc     8,2f\n"
                 "      brc     4,1f\n"
                 "      agsi    %[__cc],1\n"
                 "1:    agsi    %[__cc],1\n"
                 "2:    lgr     %[fc],0\n"
                 : [fc] "+d" (fc), [src] "+&d" (s.pair), [__cc] "+Q" (*cc)
                 : [pba] "d" ((unsigned long)param),
                   [opc] "i" (CPACF_KMAC)
                 : "cc", "memory", "0", "1");

    *func = fc;

    return src_len - s.odd;
}

/**
 * cpacf_kmctr() - executes the KMCTR instruction
 * @func: the function code passed to KMCTR; see CPACF_KMCTR_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @dest: address of destination memory area
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 * @counter: address of counter value
 *
 * Returns 0 for the query func, number of processed bytes for
 * encryption/decryption funcs
 */
static inline int cpacf_kmctr(unsigned long func, void *param, uint8_t *dest,
                              const uint8_t *src, long src_len,
                              uint8_t *counter, unsigned long *cc)
{
    union register_pair d, s, c;

    *cc = 0;
    d.even = (unsigned long)dest;
    s.even = (unsigned long)src;
    s.odd  = (unsigned long)src_len;
    c.even = (unsigned long)counter;
    asm volatile(
                 "      lgr     0,%[fc]\n"
                 "      lgr     1,%[pba]\n"
                 "0:    .insn   rrf,%[opc] << 16,%[dst],%[src],%[ctr],0\n"
                 "      brc     1,0b\n" /* handle partial completion */
                 "      brc     8,2f\n"
                 "      brc     4,1f\n"
                 "      agsi    %[__cc],1\n"
                 "1:    agsi    %[__cc],1\n"
                 "2:\n"
                 : [src] "+&d" (s.pair), [dst] "+&d" (d.pair),
                   [ctr] "+&d" (c.pair), [__cc] "+Q" (*cc)
                 : [fc] "d" (func), [pba] "d" ((unsigned long)param),
                   [opc] "i" (CPACF_KMCTR)
                 : "cc", "memory", "0", "1");

    return src_len - s.odd;
}

/**
 * cpacf_prno() - executes the PRNO instruction
 * @func: the function code passed to PRNO; see CPACF_PRNO_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @dest: address of destination memory area
 * @dest_len: size of destination memory area in bytes
 * @seed: address of seed data
 * @seed_len: size of seed data in bytes
 */
static inline void cpacf_prno(unsigned long func, void *param,
                              uint8_t *dest, unsigned long dest_len,
                              const uint8_t *seed, unsigned long seed_len,
                              unsigned long *cc)
{
    union register_pair d, s;

    *cc = 0;
    d.even = (unsigned long)dest;
    d.odd  = (unsigned long)dest_len;
    s.even = (unsigned long)seed;
    s.odd  = (unsigned long)seed_len;
    asm volatile (
                  "     lgr     0,%[fc]\n"
                  "     lgr     1,%[pba]\n"
                  "0:   .insn   rre,%[opc] << 16,%[dst],%[seed]\n"
                  "     brc     1,0b\n"          /* handle partial completion */
                  "     brc     8,2f\n"
                  "     brc     4,1f\n"
                  "     agsi    %[__cc],1\n"
                  "1:   agsi    %[__cc],1\n"
                  "2:\n"
                  : [dst] "+&d" (d.pair), [__cc] "+Q" (*cc)
                  : [fc] "d" (func), [pba] "d" ((unsigned long)param),
                    [seed] "d" (s.pair), [opc] "i" (CPACF_PRNO)
                  : "cc", "memory", "0", "1");
}

/**
 * cpacf_trng() - executes the TRNG subfunction of the PRNO instruction
 * @ucbuf: buffer for unconditioned data
 * @ucbuf_len: amount of unconditioned data to fetch in bytes
 * @cbuf: buffer for conditioned data
 * @cbuf_len: amount of conditioned data to fetch in bytes
 */
static inline void cpacf_trng(uint8_t *ucbuf, unsigned long ucbuf_len,
                              uint8_t *cbuf, unsigned long cbuf_len,
                              unsigned long *cc)
{
    union register_pair u, c;

    *cc = 0;
    u.even = (unsigned long)ucbuf;
    u.odd  = (unsigned long)ucbuf_len;
    c.even = (unsigned long)cbuf;
    c.odd  = (unsigned long)cbuf_len;
    asm volatile (
                  "     lghi    0,%[fc]\n"
                  "0:   .insn   rre,%[opc] << 16,%[ucbuf],%[cbuf]\n"
                  "     brc     1,0b\n"          /* handle partial completion */
                  "     brc     8,2f\n"
                  "     brc     4,1f\n"
                  "     agsi    %[__cc],1\n"
                  "1:   agsi    %[__cc],1\n"
                  "2:\n"
                  : [ucbuf] "+&d" (u.pair), [cbuf] "+&d" (c.pair),
                    [__cc] "+Q" (*cc)
                  : [fc] "K" (CPACF_PRNO_TRNG), [opc] "i" (CPACF_PRNO)
                  : "cc", "memory", "0");
}

/**
 * cpacf_pcc() - executes the PCC instruction
 * @func: the function code passed to PCC; see CPACF_KM_xxx defines
 * @param: address of parameter block; see POP for details on each func
 */
static inline void cpacf_pcc(unsigned long func, void *param, unsigned long *cc)
{
    *cc = 0;
    asm volatile(
                 "      lgr     0,%[fc]\n"
                 "      lgr     1,%[pba]\n"
                 "0:    .insn   rre,%[opc] << 16,0,0\n" /* PCC opcode */
                 "      brc     1,0b\n" /* handle partial completion */
                 "      brc     8,2f\n"
                 "      brc     4,1f\n"
                 "      agsi    %[__cc],1\n"
                 "1:    agsi    %[__cc],1\n"
                 "2:\n"
                 : [__cc] "+Q" (*cc)
                 : [fc] "d" (func), [pba] "d" ((unsigned long)param),
                   [opc] "i" (CPACF_PCC)
                 : "cc", "memory", "0", "1");
}

/**
 * cpacf_pckmo() - executes the PCKMO instruction
 * @func: the function code passed to PCKMO; see CPACF_PCKMO_xxx defines
 * @param: address of parameter block; see POP for details on each func
 */
static inline void cpacf_pckmo(long func, void *param)
{
    asm volatile(
                 "      lgr     0,%[fc]\n"
                 "      lgr     1,%[pba]\n"
                 "      .insn   rre,%[opc] << 16,0,0\n" /* PCKMO opcode */
                 :
                 : [fc] "d" (func), [pba] "d" ((unsigned long)param),
                   [opc] "i" (CPACF_PCKMO)
                 : "cc", "memory", "0", "1");
}

/**
 * cpacf_kma() - executes the KMA instruction
 * @func: the function code passed to KMA; see CPACF_KMA_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @dest: address of destination memory area
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 * @aad: address of additional authenticated data memory area
 * @aad_len: length of aad operand in bytes
 */
static inline void cpacf_kma(unsigned long func, void *param, uint8_t *dest,
                             const uint8_t *src, unsigned long src_len,
                             const uint8_t *aad, unsigned long aad_len,
                             unsigned long *cc)
{
    union register_pair d, s, a;

    *cc = 0;
    d.even = (unsigned long)dest;
    s.even = (unsigned long)src;
    s.odd  = (unsigned long)src_len;
    a.even = (unsigned long)aad;
    a.odd  = (unsigned long)aad_len;
    asm volatile(
                 "      lgr     0,%[fc]\n"
                 "      lgr     1,%[pba]\n"
                 "0:    .insn   rrf,%[opc] << 16,%[dst],%[src],%[aad],0\n"
                 "      brc     1,0b\n"         /* handle partial completion */
                 "      brc     8,2f\n"
                 "      brc     4,1f\n"
                 "      agsi    %[__cc],1\n"
                 "1:    agsi    %[__cc],1\n"
                 "2:\n"
                 : [dst] "+&d" (d.pair), [src] "+&d" (s.pair),
                   [aad] "+&d" (a.pair), [__cc] "+Q" (*cc)
                 : [fc] "d" (func), [pba] "d" ((unsigned long)param),
                   [opc] "i" (CPACF_KMA)
                 : "cc", "memory", "0", "1");
}

/**
 * cpacf_kmf() - executes the KMF instruction
 * @func: the function code passed to KMF; see CPACF_KMF_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @dest: address of destination memory area
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Returns 0 for the query func, number of processed bytes for
 * encryption/decryption funcs
 */
static inline int cpacf_kmf(unsigned long func, void *param,
                            uint8_t *dest, const uint8_t *src, long src_len,
                            unsigned long *cc)
{
    union register_pair d, s;

    *cc = 0;
    d.even = (unsigned long)dest;
    s.even = (unsigned long)src;
    s.odd  = (unsigned long)src_len;
    asm volatile(
                 "      lgr     0,%[fc]\n"
                 "      lgr     1,%[pba]\n"
                 "0:    .insn   rre,%[opc] << 16,%[dst],%[src]\n"
                 "      brc     1,0b\n" /* handle partial completion */
                 "      brc     8,2f\n"
                 "      brc     4,1f\n"
                 "      agsi    %[__cc],1\n"
                 "1:    agsi    %[__cc],1\n"
                 "2:\n"
                 : [src] "+&d" (s.pair), [dst] "+&d" (d.pair), [__cc] "+Q" (*cc)
                 : [fc] "d" (func), [pba] "d" ((unsigned long)param),
                   [opc] "i" (CPACF_KMF)
                 : "cc", "memory", "0", "1");

    return src_len - s.odd;
}

/**
 * cpacf_kmo() - executes the KMO instruction
 * @func: the function code passed to KMO; see CPACF_KMO_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @dest: address of destination memory area
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Returns 0 for the query func, number of processed bytes for
 * encryption/decryption funcs
 */
static inline int cpacf_kmo(unsigned long func, void *param,
                            uint8_t *dest, const uint8_t *src, long src_len,
                            unsigned long *cc)
{
    union register_pair d, s;

    *cc = 0;
    d.even = (unsigned long)dest;
    s.even = (unsigned long)src;
    s.odd  = (unsigned long)src_len;
    asm volatile(
                 "      lgr     0,%[fc]\n"
                 "      lgr     1,%[pba]\n"
                 "0:    .insn   rre,%[opc] << 16,%[dst],%[src]\n"
                 "      brc     1,0b\n" /* handle partial completion */
                 "      brc     8,2f\n"
                 "      brc     4,1f\n"
                 "      agsi    %[__cc],1\n"
                 "1:    agsi    %[__cc],1\n"
                 "2:\n"
                 : [src] "+&d" (s.pair), [dst] "+&d" (d.pair), [__cc] "+Q" (*cc)
                 : [fc] "d" (func), [pba] "d" ((unsigned long)param),
                   [opc] "i" (CPACF_KMO)
                 : "cc", "memory", "0", "1");

    return src_len - s.odd;
}

/**
 * cpacf_kdsa() - executes the KDSA instruction
 * @func: the function code passed to KDSA; see CPACF_KDSA_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Returns 0 for the query func, otherwise the condition code is checked
 * and 0 returned on cc 0, otherwise a value != 0 to indicate failure.
 */
static inline int cpacf_kdsa(unsigned long func, void *param,
                             const uint8_t *src, long src_len,
                             unsigned long *cc)
{
    union register_pair s;

    *cc = 0;
    s.even = (unsigned long)src;
    s.odd  = (unsigned long)src_len;
    asm volatile(
                 "      lgr     0,%[fc]\n"
                 "      lgr     1,%[pba]\n"
                 "0:    .insn   rre,%[opc] << 16,0,%[src]\n"
                 "      brc     1,0b\n" /* handle partial completion */
                 "      brc     8,2f\n"
                 "      brc     4,1f\n"
                 "      agsi    %[__cc],1\n"
                 "1:    agsi    %[__cc],1\n"
                 "2:\n"
                 : [src] "+&d" (s.pair), [__cc] "+Q" (*cc)
                 : [fc] "d" (func), [pba] "d" ((unsigned long)param),
                   [opc] "i" (CPACF_KDSA)
                 : "cc", "memory", "0", "1");

    return (int)(*cc != 0);
}

#endif  /* _S390_CPACF_H_ */
