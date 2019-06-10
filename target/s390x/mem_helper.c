/*
 *  S/390 memory access helper routines
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2009 Alexander Graf
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internal.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "qemu/int128.h"
#include "qemu/atomic128.h"

#if !defined(CONFIG_USER_ONLY)
#include "hw/s390x/storage-keys.h"
#endif

/*****************************************************************************/
/* Softmmu support */

/* #define DEBUG_HELPER */
#ifdef DEBUG_HELPER
#define HELPER_LOG(x...) qemu_log(x)
#else
#define HELPER_LOG(x...)
#endif

static inline bool psw_key_valid(CPUS390XState *env, uint8_t psw_key)
{
    uint16_t pkm = env->cregs[3] >> 16;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        /* PSW key has range 0..15, it is valid if the bit is 1 in the PKM */
        return pkm & (0x80 >> psw_key);
    }
    return true;
}

/* Reduce the length so that addr + len doesn't cross a page boundary.  */
static inline uint32_t adj_len_to_page(uint32_t len, uint64_t addr)
{
#ifndef CONFIG_USER_ONLY
    if ((addr & ~TARGET_PAGE_MASK) + len - 1 >= TARGET_PAGE_SIZE) {
        return -(addr | TARGET_PAGE_MASK);
    }
#endif
    return len;
}

/* Trigger a SPECIFICATION exception if an address or a length is not
   naturally aligned.  */
static inline void check_alignment(CPUS390XState *env, uint64_t v,
                                   int wordsize, uintptr_t ra)
{
    if (v % wordsize) {
        s390_program_interrupt(env, PGM_SPECIFICATION, 6, ra);
    }
}

/* Load a value from memory according to its size.  */
static inline uint64_t cpu_ldusize_data_ra(CPUS390XState *env, uint64_t addr,
                                           int wordsize, uintptr_t ra)
{
    switch (wordsize) {
    case 1:
        return cpu_ldub_data_ra(env, addr, ra);
    case 2:
        return cpu_lduw_data_ra(env, addr, ra);
    default:
        abort();
    }
}

/* Store a to memory according to its size.  */
static inline void cpu_stsize_data_ra(CPUS390XState *env, uint64_t addr,
                                      uint64_t value, int wordsize,
                                      uintptr_t ra)
{
    switch (wordsize) {
    case 1:
        cpu_stb_data_ra(env, addr, value, ra);
        break;
    case 2:
        cpu_stw_data_ra(env, addr, value, ra);
        break;
    default:
        abort();
    }
}

static void fast_memset(CPUS390XState *env, uint64_t dest, uint8_t byte,
                        uint32_t l, uintptr_t ra)
{
    int mmu_idx = cpu_mmu_index(env, false);

    while (l > 0) {
        void *p = tlb_vaddr_to_host(env, dest, MMU_DATA_STORE, mmu_idx);
        if (p) {
            /* Access to the whole page in write mode granted.  */
            uint32_t l_adj = adj_len_to_page(l, dest);
            memset(p, byte, l_adj);
            dest += l_adj;
            l -= l_adj;
        } else {
            /* We failed to get access to the whole page. The next write
               access will likely fill the QEMU TLB for the next iteration.  */
            cpu_stb_data_ra(env, dest, byte, ra);
            dest++;
            l--;
        }
    }
}

#ifndef CONFIG_USER_ONLY
static void fast_memmove_idx(CPUS390XState *env, uint64_t dest, uint64_t src,
                             uint32_t len, int dest_idx, int src_idx,
                             uintptr_t ra)
{
    TCGMemOpIdx oi_dest = make_memop_idx(MO_UB, dest_idx);
    TCGMemOpIdx oi_src = make_memop_idx(MO_UB, src_idx);
    uint32_t len_adj;
    void *src_p;
    void *dest_p;
    uint8_t x;

    while (len > 0) {
        src = wrap_address(env, src);
        dest = wrap_address(env, dest);
        src_p = tlb_vaddr_to_host(env, src, MMU_DATA_LOAD, src_idx);
        dest_p = tlb_vaddr_to_host(env, dest, MMU_DATA_STORE, dest_idx);

        if (src_p && dest_p) {
            /* Access to both whole pages granted.  */
            len_adj = adj_len_to_page(adj_len_to_page(len, src), dest);
            memmove(dest_p, src_p, len_adj);
        } else {
            /* We failed to get access to one or both whole pages. The next
               read or write access will likely fill the QEMU TLB for the
               next iteration.  */
            len_adj = 1;
            x = helper_ret_ldub_mmu(env, src, oi_src, ra);
            helper_ret_stb_mmu(env, dest, x, oi_dest, ra);
        }
        src += len_adj;
        dest += len_adj;
        len -= len_adj;
    }
}

static int mmu_idx_from_as(uint8_t as)
{
    switch (as) {
    case AS_PRIMARY:
        return MMU_PRIMARY_IDX;
    case AS_SECONDARY:
        return MMU_SECONDARY_IDX;
    case AS_HOME:
        return MMU_HOME_IDX;
    default:
        /* FIXME AS_ACCREG */
        g_assert_not_reached();
    }
}

static void fast_memmove_as(CPUS390XState *env, uint64_t dest, uint64_t src,
                            uint32_t len, uint8_t dest_as, uint8_t src_as,
                            uintptr_t ra)
{
    int src_idx = mmu_idx_from_as(src_as);
    int dest_idx = mmu_idx_from_as(dest_as);

    fast_memmove_idx(env, dest, src, len, dest_idx, src_idx, ra);
}
#endif

static void fast_memmove(CPUS390XState *env, uint64_t dest, uint64_t src,
                         uint32_t l, uintptr_t ra)
{
    int mmu_idx = cpu_mmu_index(env, false);

    while (l > 0) {
        void *src_p = tlb_vaddr_to_host(env, src, MMU_DATA_LOAD, mmu_idx);
        void *dest_p = tlb_vaddr_to_host(env, dest, MMU_DATA_STORE, mmu_idx);
        if (src_p && dest_p) {
            /* Access to both whole pages granted.  */
            uint32_t l_adj = adj_len_to_page(l, src);
            l_adj = adj_len_to_page(l_adj, dest);
            memmove(dest_p, src_p, l_adj);
            src += l_adj;
            dest += l_adj;
            l -= l_adj;
        } else {
            /* We failed to get access to one or both whole pages. The next
               read or write access will likely fill the QEMU TLB for the
               next iteration.  */
            cpu_stb_data_ra(env, dest, cpu_ldub_data_ra(env, src, ra), ra);
            src++;
            dest++;
            l--;
        }
    }
}

/* and on array */
static uint32_t do_helper_nc(CPUS390XState *env, uint32_t l, uint64_t dest,
                             uint64_t src, uintptr_t ra)
{
    uint32_t i;
    uint8_t c = 0;

    HELPER_LOG("%s l %d dest %" PRIx64 " src %" PRIx64 "\n",
               __func__, l, dest, src);

    for (i = 0; i <= l; i++) {
        uint8_t x = cpu_ldub_data_ra(env, src + i, ra);
        x &= cpu_ldub_data_ra(env, dest + i, ra);
        c |= x;
        cpu_stb_data_ra(env, dest + i, x, ra);
    }
    return c != 0;
}

uint32_t HELPER(nc)(CPUS390XState *env, uint32_t l, uint64_t dest,
                    uint64_t src)
{
    return do_helper_nc(env, l, dest, src, GETPC());
}

/* xor on array */
static uint32_t do_helper_xc(CPUS390XState *env, uint32_t l, uint64_t dest,
                             uint64_t src, uintptr_t ra)
{
    uint32_t i;
    uint8_t c = 0;

    HELPER_LOG("%s l %d dest %" PRIx64 " src %" PRIx64 "\n",
               __func__, l, dest, src);

    /* xor with itself is the same as memset(0) */
    if (src == dest) {
        fast_memset(env, dest, 0, l + 1, ra);
        return 0;
    }

    for (i = 0; i <= l; i++) {
        uint8_t x = cpu_ldub_data_ra(env, src + i, ra);
        x ^= cpu_ldub_data_ra(env, dest + i, ra);
        c |= x;
        cpu_stb_data_ra(env, dest + i, x, ra);
    }
    return c != 0;
}

uint32_t HELPER(xc)(CPUS390XState *env, uint32_t l, uint64_t dest,
                    uint64_t src)
{
    return do_helper_xc(env, l, dest, src, GETPC());
}

/* or on array */
static uint32_t do_helper_oc(CPUS390XState *env, uint32_t l, uint64_t dest,
                             uint64_t src, uintptr_t ra)
{
    uint32_t i;
    uint8_t c = 0;

    HELPER_LOG("%s l %d dest %" PRIx64 " src %" PRIx64 "\n",
               __func__, l, dest, src);

    for (i = 0; i <= l; i++) {
        uint8_t x = cpu_ldub_data_ra(env, src + i, ra);
        x |= cpu_ldub_data_ra(env, dest + i, ra);
        c |= x;
        cpu_stb_data_ra(env, dest + i, x, ra);
    }
    return c != 0;
}

uint32_t HELPER(oc)(CPUS390XState *env, uint32_t l, uint64_t dest,
                    uint64_t src)
{
    return do_helper_oc(env, l, dest, src, GETPC());
}

/* memmove */
static uint32_t do_helper_mvc(CPUS390XState *env, uint32_t l, uint64_t dest,
                              uint64_t src, uintptr_t ra)
{
    uint32_t i;

    HELPER_LOG("%s l %d dest %" PRIx64 " src %" PRIx64 "\n",
               __func__, l, dest, src);

    /* mvc and memmove do not behave the same when areas overlap! */
    /* mvc with source pointing to the byte after the destination is the
       same as memset with the first source byte */
    if (dest == src + 1) {
        fast_memset(env, dest, cpu_ldub_data_ra(env, src, ra), l + 1, ra);
    } else if (dest < src || src + l < dest) {
        fast_memmove(env, dest, src, l + 1, ra);
    } else {
        /* slow version with byte accesses which always work */
        for (i = 0; i <= l; i++) {
            uint8_t x = cpu_ldub_data_ra(env, src + i, ra);
            cpu_stb_data_ra(env, dest + i, x, ra);
        }
    }

    return env->cc_op;
}

void HELPER(mvc)(CPUS390XState *env, uint32_t l, uint64_t dest, uint64_t src)
{
    do_helper_mvc(env, l, dest, src, GETPC());
}

/* move inverse  */
void HELPER(mvcin)(CPUS390XState *env, uint32_t l, uint64_t dest, uint64_t src)
{
    uintptr_t ra = GETPC();
    int i;

    for (i = 0; i <= l; i++) {
        uint8_t v = cpu_ldub_data_ra(env, src - i, ra);
        cpu_stb_data_ra(env, dest + i, v, ra);
    }
}

/* move numerics  */
void HELPER(mvn)(CPUS390XState *env, uint32_t l, uint64_t dest, uint64_t src)
{
    uintptr_t ra = GETPC();
    int i;

    for (i = 0; i <= l; i++) {
        uint8_t v = cpu_ldub_data_ra(env, dest + i, ra) & 0xf0;
        v |= cpu_ldub_data_ra(env, src + i, ra) & 0x0f;
        cpu_stb_data_ra(env, dest + i, v, ra);
    }
}

/* move with offset  */
void HELPER(mvo)(CPUS390XState *env, uint32_t l, uint64_t dest, uint64_t src)
{
    uintptr_t ra = GETPC();
    int len_dest = l >> 4;
    int len_src = l & 0xf;
    uint8_t byte_dest, byte_src;
    int i;

    src += len_src;
    dest += len_dest;

    /* Handle rightmost byte */
    byte_src = cpu_ldub_data_ra(env, src, ra);
    byte_dest = cpu_ldub_data_ra(env, dest, ra);
    byte_dest = (byte_dest & 0x0f) | (byte_src << 4);
    cpu_stb_data_ra(env, dest, byte_dest, ra);

    /* Process remaining bytes from right to left */
    for (i = 1; i <= len_dest; i++) {
        byte_dest = byte_src >> 4;
        if (len_src - i >= 0) {
            byte_src = cpu_ldub_data_ra(env, src - i, ra);
        } else {
            byte_src = 0;
        }
        byte_dest |= byte_src << 4;
        cpu_stb_data_ra(env, dest - i, byte_dest, ra);
    }
}

/* move zones  */
void HELPER(mvz)(CPUS390XState *env, uint32_t l, uint64_t dest, uint64_t src)
{
    uintptr_t ra = GETPC();
    int i;

    for (i = 0; i <= l; i++) {
        uint8_t b = cpu_ldub_data_ra(env, dest + i, ra) & 0x0f;
        b |= cpu_ldub_data_ra(env, src + i, ra) & 0xf0;
        cpu_stb_data_ra(env, dest + i, b, ra);
    }
}

/* compare unsigned byte arrays */
static uint32_t do_helper_clc(CPUS390XState *env, uint32_t l, uint64_t s1,
                              uint64_t s2, uintptr_t ra)
{
    uint32_t i;
    uint32_t cc = 0;

    HELPER_LOG("%s l %d s1 %" PRIx64 " s2 %" PRIx64 "\n",
               __func__, l, s1, s2);

    for (i = 0; i <= l; i++) {
        uint8_t x = cpu_ldub_data_ra(env, s1 + i, ra);
        uint8_t y = cpu_ldub_data_ra(env, s2 + i, ra);
        HELPER_LOG("%02x (%c)/%02x (%c) ", x, x, y, y);
        if (x < y) {
            cc = 1;
            break;
        } else if (x > y) {
            cc = 2;
            break;
        }
    }

    HELPER_LOG("\n");
    return cc;
}

uint32_t HELPER(clc)(CPUS390XState *env, uint32_t l, uint64_t s1, uint64_t s2)
{
    return do_helper_clc(env, l, s1, s2, GETPC());
}

/* compare logical under mask */
uint32_t HELPER(clm)(CPUS390XState *env, uint32_t r1, uint32_t mask,
                     uint64_t addr)
{
    uintptr_t ra = GETPC();
    uint32_t cc = 0;

    HELPER_LOG("%s: r1 0x%x mask 0x%x addr 0x%" PRIx64 "\n", __func__, r1,
               mask, addr);

    while (mask) {
        if (mask & 8) {
            uint8_t d = cpu_ldub_data_ra(env, addr, ra);
            uint8_t r = extract32(r1, 24, 8);
            HELPER_LOG("mask 0x%x %02x/%02x (0x%" PRIx64 ") ", mask, r, d,
                       addr);
            if (r < d) {
                cc = 1;
                break;
            } else if (r > d) {
                cc = 2;
                break;
            }
            addr++;
        }
        mask = (mask << 1) & 0xf;
        r1 <<= 8;
    }

    HELPER_LOG("\n");
    return cc;
}

static inline uint64_t get_address(CPUS390XState *env, int reg)
{
    return wrap_address(env, env->regs[reg]);
}

static inline void set_address(CPUS390XState *env, int reg, uint64_t address)
{
    if (env->psw.mask & PSW_MASK_64) {
        /* 64-Bit mode */
        env->regs[reg] = address;
    } else {
        if (!(env->psw.mask & PSW_MASK_32)) {
            /* 24-Bit mode. According to the PoO it is implementation
            dependent if bits 32-39 remain unchanged or are set to
            zeros.  Choose the former so that the function can also be
            used for TRT.  */
            env->regs[reg] = deposit64(env->regs[reg], 0, 24, address);
        } else {
            /* 31-Bit mode. According to the PoO it is implementation
            dependent if bit 32 remains unchanged or is set to zero.
            Choose the latter so that the function can also be used for
            TRT.  */
            address &= 0x7fffffff;
            env->regs[reg] = deposit64(env->regs[reg], 0, 32, address);
        }
    }
}

static inline uint64_t wrap_length(CPUS390XState *env, uint64_t length)
{
    if (!(env->psw.mask & PSW_MASK_64)) {
        /* 24-Bit and 31-Bit mode */
        length &= 0x7fffffff;
    }
    return length;
}

static inline uint64_t get_length(CPUS390XState *env, int reg)
{
    return wrap_length(env, env->regs[reg]);
}

static inline void set_length(CPUS390XState *env, int reg, uint64_t length)
{
    if (env->psw.mask & PSW_MASK_64) {
        /* 64-Bit mode */
        env->regs[reg] = length;
    } else {
        /* 24-Bit and 31-Bit mode */
        env->regs[reg] = deposit64(env->regs[reg], 0, 32, length);
    }
}

/* search string (c is byte to search, r2 is string, r1 end of string) */
void HELPER(srst)(CPUS390XState *env, uint32_t r1, uint32_t r2)
{
    uintptr_t ra = GETPC();
    uint64_t end, str;
    uint32_t len;
    uint8_t v, c = env->regs[0];

    /* Bits 32-55 must contain all 0.  */
    if (env->regs[0] & 0xffffff00u) {
        s390_program_interrupt(env, PGM_SPECIFICATION, 6, ra);
    }

    str = get_address(env, r2);
    end = get_address(env, r1);

    /* Lest we fail to service interrupts in a timely manner, limit the
       amount of work we're willing to do.  For now, let's cap at 8k.  */
    for (len = 0; len < 0x2000; ++len) {
        if (str + len == end) {
            /* Character not found.  R1 & R2 are unmodified.  */
            env->cc_op = 2;
            return;
        }
        v = cpu_ldub_data_ra(env, str + len, ra);
        if (v == c) {
            /* Character found.  Set R1 to the location; R2 is unmodified.  */
            env->cc_op = 1;
            set_address(env, r1, str + len);
            return;
        }
    }

    /* CPU-determined bytes processed.  Advance R2 to next byte to process.  */
    env->cc_op = 3;
    set_address(env, r2, str + len);
}

void HELPER(srstu)(CPUS390XState *env, uint32_t r1, uint32_t r2)
{
    uintptr_t ra = GETPC();
    uint32_t len;
    uint16_t v, c = env->regs[0];
    uint64_t end, str, adj_end;

    /* Bits 32-47 of R0 must be zero.  */
    if (env->regs[0] & 0xffff0000u) {
        s390_program_interrupt(env, PGM_SPECIFICATION, 6, ra);
    }

    str = get_address(env, r2);
    end = get_address(env, r1);

    /* If the LSB of the two addresses differ, use one extra byte.  */
    adj_end = end + ((str ^ end) & 1);

    /* Lest we fail to service interrupts in a timely manner, limit the
       amount of work we're willing to do.  For now, let's cap at 8k.  */
    for (len = 0; len < 0x2000; len += 2) {
        if (str + len == adj_end) {
            /* End of input found.  */
            env->cc_op = 2;
            return;
        }
        v = cpu_lduw_data_ra(env, str + len, ra);
        if (v == c) {
            /* Character found.  Set R1 to the location; R2 is unmodified.  */
            env->cc_op = 1;
            set_address(env, r1, str + len);
            return;
        }
    }

    /* CPU-determined bytes processed.  Advance R2 to next byte to process.  */
    env->cc_op = 3;
    set_address(env, r2, str + len);
}

/* unsigned string compare (c is string terminator) */
uint64_t HELPER(clst)(CPUS390XState *env, uint64_t c, uint64_t s1, uint64_t s2)
{
    uintptr_t ra = GETPC();
    uint32_t len;

    c = c & 0xff;
    s1 = wrap_address(env, s1);
    s2 = wrap_address(env, s2);

    /* Lest we fail to service interrupts in a timely manner, limit the
       amount of work we're willing to do.  For now, let's cap at 8k.  */
    for (len = 0; len < 0x2000; ++len) {
        uint8_t v1 = cpu_ldub_data_ra(env, s1 + len, ra);
        uint8_t v2 = cpu_ldub_data_ra(env, s2 + len, ra);
        if (v1 == v2) {
            if (v1 == c) {
                /* Equal.  CC=0, and don't advance the registers.  */
                env->cc_op = 0;
                env->retxl = s2;
                return s1;
            }
        } else {
            /* Unequal.  CC={1,2}, and advance the registers.  Note that
               the terminator need not be zero, but the string that contains
               the terminator is by definition "low".  */
            env->cc_op = (v1 == c ? 1 : v2 == c ? 2 : v1 < v2 ? 1 : 2);
            env->retxl = s2 + len;
            return s1 + len;
        }
    }

    /* CPU-determined bytes equal; advance the registers.  */
    env->cc_op = 3;
    env->retxl = s2 + len;
    return s1 + len;
}

/* move page */
uint32_t HELPER(mvpg)(CPUS390XState *env, uint64_t r0, uint64_t r1, uint64_t r2)
{
    /* ??? missing r0 handling, which includes access keys, but more
       importantly optional suppression of the exception!  */
    fast_memmove(env, r1, r2, TARGET_PAGE_SIZE, GETPC());
    return 0; /* data moved */
}

/* string copy (c is string terminator) */
uint64_t HELPER(mvst)(CPUS390XState *env, uint64_t c, uint64_t d, uint64_t s)
{
    uintptr_t ra = GETPC();
    uint32_t len;

    c = c & 0xff;
    d = wrap_address(env, d);
    s = wrap_address(env, s);

    /* Lest we fail to service interrupts in a timely manner, limit the
       amount of work we're willing to do.  For now, let's cap at 8k.  */
    for (len = 0; len < 0x2000; ++len) {
        uint8_t v = cpu_ldub_data_ra(env, s + len, ra);
        cpu_stb_data_ra(env, d + len, v, ra);
        if (v == c) {
            /* Complete.  Set CC=1 and advance R1.  */
            env->cc_op = 1;
            env->retxl = s;
            return d + len;
        }
    }

    /* Incomplete.  Set CC=3 and signal to advance R1 and R2.  */
    env->cc_op = 3;
    env->retxl = s + len;
    return d + len;
}

/* load access registers r1 to r3 from memory at a2 */
void HELPER(lam)(CPUS390XState *env, uint32_t r1, uint64_t a2, uint32_t r3)
{
    uintptr_t ra = GETPC();
    int i;

    if (a2 & 0x3) {
        /* we either came here by lam or lamy, which have different lengths */
        s390_program_interrupt(env, PGM_SPECIFICATION, ILEN_AUTO, ra);
    }

    for (i = r1;; i = (i + 1) % 16) {
        env->aregs[i] = cpu_ldl_data_ra(env, a2, ra);
        a2 += 4;

        if (i == r3) {
            break;
        }
    }
}

/* store access registers r1 to r3 in memory at a2 */
void HELPER(stam)(CPUS390XState *env, uint32_t r1, uint64_t a2, uint32_t r3)
{
    uintptr_t ra = GETPC();
    int i;

    if (a2 & 0x3) {
        s390_program_interrupt(env, PGM_SPECIFICATION, 4, ra);
    }

    for (i = r1;; i = (i + 1) % 16) {
        cpu_stl_data_ra(env, a2, env->aregs[i], ra);
        a2 += 4;

        if (i == r3) {
            break;
        }
    }
}

/* move long helper */
static inline uint32_t do_mvcl(CPUS390XState *env,
                               uint64_t *dest, uint64_t *destlen,
                               uint64_t *src, uint64_t *srclen,
                               uint16_t pad, int wordsize, uintptr_t ra)
{
    uint64_t len = MIN(*srclen, *destlen);
    uint32_t cc;

    if (*destlen == *srclen) {
        cc = 0;
    } else if (*destlen < *srclen) {
        cc = 1;
    } else {
        cc = 2;
    }

    /* Copy the src array */
    fast_memmove(env, *dest, *src, len, ra);
    *src += len;
    *srclen -= len;
    *dest += len;
    *destlen -= len;

    /* Pad the remaining area */
    if (wordsize == 1) {
        fast_memset(env, *dest, pad, *destlen, ra);
        *dest += *destlen;
        *destlen = 0;
    } else {
        /* If remaining length is odd, pad with odd byte first.  */
        if (*destlen & 1) {
            cpu_stb_data_ra(env, *dest, pad & 0xff, ra);
            *dest += 1;
            *destlen -= 1;
        }
        /* The remaining length is even, pad using words.  */
        for (; *destlen; *dest += 2, *destlen -= 2) {
            cpu_stw_data_ra(env, *dest, pad, ra);
        }
    }

    return cc;
}

/* move long */
uint32_t HELPER(mvcl)(CPUS390XState *env, uint32_t r1, uint32_t r2)
{
    uintptr_t ra = GETPC();
    uint64_t destlen = env->regs[r1 + 1] & 0xffffff;
    uint64_t dest = get_address(env, r1);
    uint64_t srclen = env->regs[r2 + 1] & 0xffffff;
    uint64_t src = get_address(env, r2);
    uint8_t pad = env->regs[r2 + 1] >> 24;
    uint32_t cc;

    cc = do_mvcl(env, &dest, &destlen, &src, &srclen, pad, 1, ra);

    env->regs[r1 + 1] = deposit64(env->regs[r1 + 1], 0, 24, destlen);
    env->regs[r2 + 1] = deposit64(env->regs[r2 + 1], 0, 24, srclen);
    set_address(env, r1, dest);
    set_address(env, r2, src);

    return cc;
}

/* move long extended */
uint32_t HELPER(mvcle)(CPUS390XState *env, uint32_t r1, uint64_t a2,
                       uint32_t r3)
{
    uintptr_t ra = GETPC();
    uint64_t destlen = get_length(env, r1 + 1);
    uint64_t dest = get_address(env, r1);
    uint64_t srclen = get_length(env, r3 + 1);
    uint64_t src = get_address(env, r3);
    uint8_t pad = a2;
    uint32_t cc;

    cc = do_mvcl(env, &dest, &destlen, &src, &srclen, pad, 1, ra);

    set_length(env, r1 + 1, destlen);
    set_length(env, r3 + 1, srclen);
    set_address(env, r1, dest);
    set_address(env, r3, src);

    return cc;
}

/* move long unicode */
uint32_t HELPER(mvclu)(CPUS390XState *env, uint32_t r1, uint64_t a2,
                       uint32_t r3)
{
    uintptr_t ra = GETPC();
    uint64_t destlen = get_length(env, r1 + 1);
    uint64_t dest = get_address(env, r1);
    uint64_t srclen = get_length(env, r3 + 1);
    uint64_t src = get_address(env, r3);
    uint16_t pad = a2;
    uint32_t cc;

    cc = do_mvcl(env, &dest, &destlen, &src, &srclen, pad, 2, ra);

    set_length(env, r1 + 1, destlen);
    set_length(env, r3 + 1, srclen);
    set_address(env, r1, dest);
    set_address(env, r3, src);

    return cc;
}

/* compare logical long helper */
static inline uint32_t do_clcl(CPUS390XState *env,
                               uint64_t *src1, uint64_t *src1len,
                               uint64_t *src3, uint64_t *src3len,
                               uint16_t pad, uint64_t limit,
                               int wordsize, uintptr_t ra)
{
    uint64_t len = MAX(*src1len, *src3len);
    uint32_t cc = 0;

    check_alignment(env, *src1len | *src3len, wordsize, ra);

    if (!len) {
        return cc;
    }

    /* Lest we fail to service interrupts in a timely manner, limit the
       amount of work we're willing to do.  */
    if (len > limit) {
        len = limit;
        cc = 3;
    }

    for (; len; len -= wordsize) {
        uint16_t v1 = pad;
        uint16_t v3 = pad;

        if (*src1len) {
            v1 = cpu_ldusize_data_ra(env, *src1, wordsize, ra);
        }
        if (*src3len) {
            v3 = cpu_ldusize_data_ra(env, *src3, wordsize, ra);
        }

        if (v1 != v3) {
            cc = (v1 < v3) ? 1 : 2;
            break;
        }

        if (*src1len) {
            *src1 += wordsize;
            *src1len -= wordsize;
        }
        if (*src3len) {
            *src3 += wordsize;
            *src3len -= wordsize;
        }
    }

    return cc;
}


/* compare logical long */
uint32_t HELPER(clcl)(CPUS390XState *env, uint32_t r1, uint32_t r2)
{
    uintptr_t ra = GETPC();
    uint64_t src1len = extract64(env->regs[r1 + 1], 0, 24);
    uint64_t src1 = get_address(env, r1);
    uint64_t src3len = extract64(env->regs[r2 + 1], 0, 24);
    uint64_t src3 = get_address(env, r2);
    uint8_t pad = env->regs[r2 + 1] >> 24;
    uint32_t cc;

    cc = do_clcl(env, &src1, &src1len, &src3, &src3len, pad, -1, 1, ra);

    env->regs[r1 + 1] = deposit64(env->regs[r1 + 1], 0, 24, src1len);
    env->regs[r2 + 1] = deposit64(env->regs[r2 + 1], 0, 24, src3len);
    set_address(env, r1, src1);
    set_address(env, r2, src3);

    return cc;
}

/* compare logical long extended memcompare insn with padding */
uint32_t HELPER(clcle)(CPUS390XState *env, uint32_t r1, uint64_t a2,
                       uint32_t r3)
{
    uintptr_t ra = GETPC();
    uint64_t src1len = get_length(env, r1 + 1);
    uint64_t src1 = get_address(env, r1);
    uint64_t src3len = get_length(env, r3 + 1);
    uint64_t src3 = get_address(env, r3);
    uint8_t pad = a2;
    uint32_t cc;

    cc = do_clcl(env, &src1, &src1len, &src3, &src3len, pad, 0x2000, 1, ra);

    set_length(env, r1 + 1, src1len);
    set_length(env, r3 + 1, src3len);
    set_address(env, r1, src1);
    set_address(env, r3, src3);

    return cc;
}

/* compare logical long unicode memcompare insn with padding */
uint32_t HELPER(clclu)(CPUS390XState *env, uint32_t r1, uint64_t a2,
                       uint32_t r3)
{
    uintptr_t ra = GETPC();
    uint64_t src1len = get_length(env, r1 + 1);
    uint64_t src1 = get_address(env, r1);
    uint64_t src3len = get_length(env, r3 + 1);
    uint64_t src3 = get_address(env, r3);
    uint16_t pad = a2;
    uint32_t cc = 0;

    cc = do_clcl(env, &src1, &src1len, &src3, &src3len, pad, 0x1000, 2, ra);

    set_length(env, r1 + 1, src1len);
    set_length(env, r3 + 1, src3len);
    set_address(env, r1, src1);
    set_address(env, r3, src3);

    return cc;
}

/* checksum */
uint64_t HELPER(cksm)(CPUS390XState *env, uint64_t r1,
                      uint64_t src, uint64_t src_len)
{
    uintptr_t ra = GETPC();
    uint64_t max_len, len;
    uint64_t cksm = (uint32_t)r1;

    /* Lest we fail to service interrupts in a timely manner, limit the
       amount of work we're willing to do.  For now, let's cap at 8k.  */
    max_len = (src_len > 0x2000 ? 0x2000 : src_len);

    /* Process full words as available.  */
    for (len = 0; len + 4 <= max_len; len += 4, src += 4) {
        cksm += (uint32_t)cpu_ldl_data_ra(env, src, ra);
    }

    switch (max_len - len) {
    case 1:
        cksm += cpu_ldub_data_ra(env, src, ra) << 24;
        len += 1;
        break;
    case 2:
        cksm += cpu_lduw_data_ra(env, src, ra) << 16;
        len += 2;
        break;
    case 3:
        cksm += cpu_lduw_data_ra(env, src, ra) << 16;
        cksm += cpu_ldub_data_ra(env, src + 2, ra) << 8;
        len += 3;
        break;
    }

    /* Fold the carry from the checksum.  Note that we can see carry-out
       during folding more than once (but probably not more than twice).  */
    while (cksm > 0xffffffffull) {
        cksm = (uint32_t)cksm + (cksm >> 32);
    }

    /* Indicate whether or not we've processed everything.  */
    env->cc_op = (len == src_len ? 0 : 3);

    /* Return both cksm and processed length.  */
    env->retxl = cksm;
    return len;
}

void HELPER(pack)(CPUS390XState *env, uint32_t len, uint64_t dest, uint64_t src)
{
    uintptr_t ra = GETPC();
    int len_dest = len >> 4;
    int len_src = len & 0xf;
    uint8_t b;

    dest += len_dest;
    src += len_src;

    /* last byte is special, it only flips the nibbles */
    b = cpu_ldub_data_ra(env, src, ra);
    cpu_stb_data_ra(env, dest, (b << 4) | (b >> 4), ra);
    src--;
    len_src--;

    /* now pack every value */
    while (len_dest > 0) {
        b = 0;

        if (len_src >= 0) {
            b = cpu_ldub_data_ra(env, src, ra) & 0x0f;
            src--;
            len_src--;
        }
        if (len_src >= 0) {
            b |= cpu_ldub_data_ra(env, src, ra) << 4;
            src--;
            len_src--;
        }

        len_dest--;
        dest--;
        cpu_stb_data_ra(env, dest, b, ra);
    }
}

static inline void do_pkau(CPUS390XState *env, uint64_t dest, uint64_t src,
                           uint32_t srclen, int ssize, uintptr_t ra)
{
    int i;
    /* The destination operand is always 16 bytes long.  */
    const int destlen = 16;

    /* The operands are processed from right to left.  */
    src += srclen - 1;
    dest += destlen - 1;

    for (i = 0; i < destlen; i++) {
        uint8_t b = 0;

        /* Start with a positive sign */
        if (i == 0) {
            b = 0xc;
        } else if (srclen > ssize) {
            b = cpu_ldub_data_ra(env, src, ra) & 0x0f;
            src -= ssize;
            srclen -= ssize;
        }

        if (srclen > ssize) {
            b |= cpu_ldub_data_ra(env, src, ra) << 4;
            src -= ssize;
            srclen -= ssize;
        }

        cpu_stb_data_ra(env, dest, b, ra);
        dest--;
    }
}


void HELPER(pka)(CPUS390XState *env, uint64_t dest, uint64_t src,
                 uint32_t srclen)
{
    do_pkau(env, dest, src, srclen, 1, GETPC());
}

void HELPER(pku)(CPUS390XState *env, uint64_t dest, uint64_t src,
                 uint32_t srclen)
{
    do_pkau(env, dest, src, srclen, 2, GETPC());
}

void HELPER(unpk)(CPUS390XState *env, uint32_t len, uint64_t dest,
                  uint64_t src)
{
    uintptr_t ra = GETPC();
    int len_dest = len >> 4;
    int len_src = len & 0xf;
    uint8_t b;
    int second_nibble = 0;

    dest += len_dest;
    src += len_src;

    /* last byte is special, it only flips the nibbles */
    b = cpu_ldub_data_ra(env, src, ra);
    cpu_stb_data_ra(env, dest, (b << 4) | (b >> 4), ra);
    src--;
    len_src--;

    /* now pad every nibble with 0xf0 */

    while (len_dest > 0) {
        uint8_t cur_byte = 0;

        if (len_src > 0) {
            cur_byte = cpu_ldub_data_ra(env, src, ra);
        }

        len_dest--;
        dest--;

        /* only advance one nibble at a time */
        if (second_nibble) {
            cur_byte >>= 4;
            len_src--;
            src--;
        }
        second_nibble = !second_nibble;

        /* digit */
        cur_byte = (cur_byte & 0xf);
        /* zone bits */
        cur_byte |= 0xf0;

        cpu_stb_data_ra(env, dest, cur_byte, ra);
    }
}

static inline uint32_t do_unpkau(CPUS390XState *env, uint64_t dest,
                                 uint32_t destlen, int dsize, uint64_t src,
                                 uintptr_t ra)
{
    int i;
    uint32_t cc;
    uint8_t b;
    /* The source operand is always 16 bytes long.  */
    const int srclen = 16;

    /* The operands are processed from right to left.  */
    src += srclen - 1;
    dest += destlen - dsize;

    /* Check for the sign.  */
    b = cpu_ldub_data_ra(env, src, ra);
    src--;
    switch (b & 0xf) {
    case 0xa:
    case 0xc:
    case 0xe ... 0xf:
        cc = 0;  /* plus */
        break;
    case 0xb:
    case 0xd:
        cc = 1;  /* minus */
        break;
    default:
    case 0x0 ... 0x9:
        cc = 3;  /* invalid */
        break;
    }

    /* Now pad every nibble with 0x30, advancing one nibble at a time. */
    for (i = 0; i < destlen; i += dsize) {
        if (i == (31 * dsize)) {
            /* If length is 32/64 bytes, the leftmost byte is 0. */
            b = 0;
        } else if (i % (2 * dsize)) {
            b = cpu_ldub_data_ra(env, src, ra);
            src--;
        } else {
            b >>= 4;
        }
        cpu_stsize_data_ra(env, dest, 0x30 + (b & 0xf), dsize, ra);
        dest -= dsize;
    }

    return cc;
}

uint32_t HELPER(unpka)(CPUS390XState *env, uint64_t dest, uint32_t destlen,
                       uint64_t src)
{
    return do_unpkau(env, dest, destlen, 1, src, GETPC());
}

uint32_t HELPER(unpku)(CPUS390XState *env, uint64_t dest, uint32_t destlen,
                       uint64_t src)
{
    return do_unpkau(env, dest, destlen, 2, src, GETPC());
}

uint32_t HELPER(tp)(CPUS390XState *env, uint64_t dest, uint32_t destlen)
{
    uintptr_t ra = GETPC();
    uint32_t cc = 0;
    int i;

    for (i = 0; i < destlen; i++) {
        uint8_t b = cpu_ldub_data_ra(env, dest + i, ra);
        /* digit */
        cc |= (b & 0xf0) > 0x90 ? 2 : 0;

        if (i == (destlen - 1)) {
            /* sign */
            cc |= (b & 0xf) < 0xa ? 1 : 0;
        } else {
            /* digit */
            cc |= (b & 0xf) > 0x9 ? 2 : 0;
        }
    }

    return cc;
}

static uint32_t do_helper_tr(CPUS390XState *env, uint32_t len, uint64_t array,
                             uint64_t trans, uintptr_t ra)
{
    uint32_t i;

    for (i = 0; i <= len; i++) {
        uint8_t byte = cpu_ldub_data_ra(env, array + i, ra);
        uint8_t new_byte = cpu_ldub_data_ra(env, trans + byte, ra);
        cpu_stb_data_ra(env, array + i, new_byte, ra);
    }

    return env->cc_op;
}

void HELPER(tr)(CPUS390XState *env, uint32_t len, uint64_t array,
                uint64_t trans)
{
    do_helper_tr(env, len, array, trans, GETPC());
}

uint64_t HELPER(tre)(CPUS390XState *env, uint64_t array,
                     uint64_t len, uint64_t trans)
{
    uintptr_t ra = GETPC();
    uint8_t end = env->regs[0] & 0xff;
    uint64_t l = len;
    uint64_t i;
    uint32_t cc = 0;

    if (!(env->psw.mask & PSW_MASK_64)) {
        array &= 0x7fffffff;
        l = (uint32_t)l;
    }

    /* Lest we fail to service interrupts in a timely manner, limit the
       amount of work we're willing to do.  For now, let's cap at 8k.  */
    if (l > 0x2000) {
        l = 0x2000;
        cc = 3;
    }

    for (i = 0; i < l; i++) {
        uint8_t byte, new_byte;

        byte = cpu_ldub_data_ra(env, array + i, ra);

        if (byte == end) {
            cc = 1;
            break;
        }

        new_byte = cpu_ldub_data_ra(env, trans + byte, ra);
        cpu_stb_data_ra(env, array + i, new_byte, ra);
    }

    env->cc_op = cc;
    env->retxl = len - i;
    return array + i;
}

static inline uint32_t do_helper_trt(CPUS390XState *env, int len,
                                     uint64_t array, uint64_t trans,
                                     int inc, uintptr_t ra)
{
    int i;

    for (i = 0; i <= len; i++) {
        uint8_t byte = cpu_ldub_data_ra(env, array + i * inc, ra);
        uint8_t sbyte = cpu_ldub_data_ra(env, trans + byte, ra);

        if (sbyte != 0) {
            set_address(env, 1, array + i * inc);
            env->regs[2] = deposit64(env->regs[2], 0, 8, sbyte);
            return (i == len) ? 2 : 1;
        }
    }

    return 0;
}

static uint32_t do_helper_trt_fwd(CPUS390XState *env, uint32_t len,
                                  uint64_t array, uint64_t trans,
                                  uintptr_t ra)
{
    return do_helper_trt(env, len, array, trans, 1, ra);
}

uint32_t HELPER(trt)(CPUS390XState *env, uint32_t len, uint64_t array,
                     uint64_t trans)
{
    return do_helper_trt(env, len, array, trans, 1, GETPC());
}

static uint32_t do_helper_trt_bkwd(CPUS390XState *env, uint32_t len,
                                   uint64_t array, uint64_t trans,
                                   uintptr_t ra)
{
    return do_helper_trt(env, len, array, trans, -1, ra);
}

uint32_t HELPER(trtr)(CPUS390XState *env, uint32_t len, uint64_t array,
                      uint64_t trans)
{
    return do_helper_trt(env, len, array, trans, -1, GETPC());
}

/* Translate one/two to one/two */
uint32_t HELPER(trXX)(CPUS390XState *env, uint32_t r1, uint32_t r2,
                      uint32_t tst, uint32_t sizes)
{
    uintptr_t ra = GETPC();
    int dsize = (sizes & 1) ? 1 : 2;
    int ssize = (sizes & 2) ? 1 : 2;
    uint64_t tbl = get_address(env, 1);
    uint64_t dst = get_address(env, r1);
    uint64_t len = get_length(env, r1 + 1);
    uint64_t src = get_address(env, r2);
    uint32_t cc = 3;
    int i;

    /* The lower address bits of TBL are ignored.  For TROO, TROT, it's
       the low 3 bits (double-word aligned).  For TRTO, TRTT, it's either
       the low 12 bits (4K, without ETF2-ENH) or 3 bits (with ETF2-ENH).  */
    if (ssize == 2 && !s390_has_feat(S390_FEAT_ETF2_ENH)) {
        tbl &= -4096;
    } else {
        tbl &= -8;
    }

    check_alignment(env, len, ssize, ra);

    /* Lest we fail to service interrupts in a timely manner, */
    /* limit the amount of work we're willing to do.   */
    for (i = 0; i < 0x2000; i++) {
        uint16_t sval = cpu_ldusize_data_ra(env, src, ssize, ra);
        uint64_t tble = tbl + (sval * dsize);
        uint16_t dval = cpu_ldusize_data_ra(env, tble, dsize, ra);
        if (dval == tst) {
            cc = 1;
            break;
        }
        cpu_stsize_data_ra(env, dst, dval, dsize, ra);

        len -= ssize;
        src += ssize;
        dst += dsize;

        if (len == 0) {
            cc = 0;
            break;
        }
    }

    set_address(env, r1, dst);
    set_length(env, r1 + 1, len);
    set_address(env, r2, src);

    return cc;
}

void HELPER(cdsg)(CPUS390XState *env, uint64_t addr,
                  uint32_t r1, uint32_t r3)
{
    uintptr_t ra = GETPC();
    Int128 cmpv = int128_make128(env->regs[r1 + 1], env->regs[r1]);
    Int128 newv = int128_make128(env->regs[r3 + 1], env->regs[r3]);
    Int128 oldv;
    uint64_t oldh, oldl;
    bool fail;

    check_alignment(env, addr, 16, ra);

    oldh = cpu_ldq_data_ra(env, addr + 0, ra);
    oldl = cpu_ldq_data_ra(env, addr + 8, ra);

    oldv = int128_make128(oldl, oldh);
    fail = !int128_eq(oldv, cmpv);
    if (fail) {
        newv = oldv;
    }

    cpu_stq_data_ra(env, addr + 0, int128_gethi(newv), ra);
    cpu_stq_data_ra(env, addr + 8, int128_getlo(newv), ra);

    env->cc_op = fail;
    env->regs[r1] = int128_gethi(oldv);
    env->regs[r1 + 1] = int128_getlo(oldv);
}

void HELPER(cdsg_parallel)(CPUS390XState *env, uint64_t addr,
                           uint32_t r1, uint32_t r3)
{
    uintptr_t ra = GETPC();
    Int128 cmpv = int128_make128(env->regs[r1 + 1], env->regs[r1]);
    Int128 newv = int128_make128(env->regs[r3 + 1], env->regs[r3]);
    int mem_idx;
    TCGMemOpIdx oi;
    Int128 oldv;
    bool fail;

    assert(HAVE_CMPXCHG128);

    mem_idx = cpu_mmu_index(env, false);
    oi = make_memop_idx(MO_TEQ | MO_ALIGN_16, mem_idx);
    oldv = helper_atomic_cmpxchgo_be_mmu(env, addr, cmpv, newv, oi, ra);
    fail = !int128_eq(oldv, cmpv);

    env->cc_op = fail;
    env->regs[r1] = int128_gethi(oldv);
    env->regs[r1 + 1] = int128_getlo(oldv);
}

static uint32_t do_csst(CPUS390XState *env, uint32_t r3, uint64_t a1,
                        uint64_t a2, bool parallel)
{
    uint32_t mem_idx = cpu_mmu_index(env, false);
    uintptr_t ra = GETPC();
    uint32_t fc = extract32(env->regs[0], 0, 8);
    uint32_t sc = extract32(env->regs[0], 8, 8);
    uint64_t pl = get_address(env, 1) & -16;
    uint64_t svh, svl;
    uint32_t cc;

    /* Sanity check the function code and storage characteristic.  */
    if (fc > 1 || sc > 3) {
        if (!s390_has_feat(S390_FEAT_COMPARE_AND_SWAP_AND_STORE_2)) {
            goto spec_exception;
        }
        if (fc > 2 || sc > 4 || (fc == 2 && (r3 & 1))) {
            goto spec_exception;
        }
    }

    /* Sanity check the alignments.  */
    if (extract32(a1, 0, fc + 2) || extract32(a2, 0, sc)) {
        goto spec_exception;
    }

    /* Sanity check writability of the store address.  */
#ifndef CONFIG_USER_ONLY
    probe_write(env, a2, 0, mem_idx, ra);
#endif

    /*
     * Note that the compare-and-swap is atomic, and the store is atomic,
     * but the complete operation is not.  Therefore we do not need to
     * assert serial context in order to implement this.  That said,
     * restart early if we can't support either operation that is supposed
     * to be atomic.
     */
    if (parallel) {
        uint32_t max = 2;
#ifdef CONFIG_ATOMIC64
        max = 3;
#endif
        if ((HAVE_CMPXCHG128 ? 0 : fc + 2 > max) ||
            (HAVE_ATOMIC128  ? 0 : sc > max)) {
            cpu_loop_exit_atomic(env_cpu(env), ra);
        }
    }

    /* All loads happen before all stores.  For simplicity, load the entire
       store value area from the parameter list.  */
    svh = cpu_ldq_data_ra(env, pl + 16, ra);
    svl = cpu_ldq_data_ra(env, pl + 24, ra);

    switch (fc) {
    case 0:
        {
            uint32_t nv = cpu_ldl_data_ra(env, pl, ra);
            uint32_t cv = env->regs[r3];
            uint32_t ov;

            if (parallel) {
#ifdef CONFIG_USER_ONLY
                uint32_t *haddr = g2h(a1);
                ov = atomic_cmpxchg__nocheck(haddr, cv, nv);
#else
                TCGMemOpIdx oi = make_memop_idx(MO_TEUL | MO_ALIGN, mem_idx);
                ov = helper_atomic_cmpxchgl_be_mmu(env, a1, cv, nv, oi, ra);
#endif
            } else {
                ov = cpu_ldl_data_ra(env, a1, ra);
                cpu_stl_data_ra(env, a1, (ov == cv ? nv : ov), ra);
            }
            cc = (ov != cv);
            env->regs[r3] = deposit64(env->regs[r3], 32, 32, ov);
        }
        break;

    case 1:
        {
            uint64_t nv = cpu_ldq_data_ra(env, pl, ra);
            uint64_t cv = env->regs[r3];
            uint64_t ov;

            if (parallel) {
#ifdef CONFIG_ATOMIC64
# ifdef CONFIG_USER_ONLY
                uint64_t *haddr = g2h(a1);
                ov = atomic_cmpxchg__nocheck(haddr, cv, nv);
# else
                TCGMemOpIdx oi = make_memop_idx(MO_TEQ | MO_ALIGN, mem_idx);
                ov = helper_atomic_cmpxchgq_be_mmu(env, a1, cv, nv, oi, ra);
# endif
#else
                /* Note that we asserted !parallel above.  */
                g_assert_not_reached();
#endif
            } else {
                ov = cpu_ldq_data_ra(env, a1, ra);
                cpu_stq_data_ra(env, a1, (ov == cv ? nv : ov), ra);
            }
            cc = (ov != cv);
            env->regs[r3] = ov;
        }
        break;

    case 2:
        {
            uint64_t nvh = cpu_ldq_data_ra(env, pl, ra);
            uint64_t nvl = cpu_ldq_data_ra(env, pl + 8, ra);
            Int128 nv = int128_make128(nvl, nvh);
            Int128 cv = int128_make128(env->regs[r3 + 1], env->regs[r3]);
            Int128 ov;

            if (!parallel) {
                uint64_t oh = cpu_ldq_data_ra(env, a1 + 0, ra);
                uint64_t ol = cpu_ldq_data_ra(env, a1 + 8, ra);

                ov = int128_make128(ol, oh);
                cc = !int128_eq(ov, cv);
                if (cc) {
                    nv = ov;
                }

                cpu_stq_data_ra(env, a1 + 0, int128_gethi(nv), ra);
                cpu_stq_data_ra(env, a1 + 8, int128_getlo(nv), ra);
            } else if (HAVE_CMPXCHG128) {
                TCGMemOpIdx oi = make_memop_idx(MO_TEQ | MO_ALIGN_16, mem_idx);
                ov = helper_atomic_cmpxchgo_be_mmu(env, a1, cv, nv, oi, ra);
                cc = !int128_eq(ov, cv);
            } else {
                /* Note that we asserted !parallel above.  */
                g_assert_not_reached();
            }

            env->regs[r3 + 0] = int128_gethi(ov);
            env->regs[r3 + 1] = int128_getlo(ov);
        }
        break;

    default:
        g_assert_not_reached();
    }

    /* Store only if the comparison succeeded.  Note that above we use a pair
       of 64-bit big-endian loads, so for sc < 3 we must extract the value
       from the most-significant bits of svh.  */
    if (cc == 0) {
        switch (sc) {
        case 0:
            cpu_stb_data_ra(env, a2, svh >> 56, ra);
            break;
        case 1:
            cpu_stw_data_ra(env, a2, svh >> 48, ra);
            break;
        case 2:
            cpu_stl_data_ra(env, a2, svh >> 32, ra);
            break;
        case 3:
            cpu_stq_data_ra(env, a2, svh, ra);
            break;
        case 4:
            if (!parallel) {
                cpu_stq_data_ra(env, a2 + 0, svh, ra);
                cpu_stq_data_ra(env, a2 + 8, svl, ra);
            } else if (HAVE_ATOMIC128) {
                TCGMemOpIdx oi = make_memop_idx(MO_TEQ | MO_ALIGN_16, mem_idx);
                Int128 sv = int128_make128(svl, svh);
                helper_atomic_sto_be_mmu(env, a2, sv, oi, ra);
            } else {
                /* Note that we asserted !parallel above.  */
                g_assert_not_reached();
            }
            break;
        default:
            g_assert_not_reached();
        }
    }

    return cc;

 spec_exception:
    s390_program_interrupt(env, PGM_SPECIFICATION, 6, ra);
    g_assert_not_reached();
}

uint32_t HELPER(csst)(CPUS390XState *env, uint32_t r3, uint64_t a1, uint64_t a2)
{
    return do_csst(env, r3, a1, a2, false);
}

uint32_t HELPER(csst_parallel)(CPUS390XState *env, uint32_t r3, uint64_t a1,
                               uint64_t a2)
{
    return do_csst(env, r3, a1, a2, true);
}

#if !defined(CONFIG_USER_ONLY)
void HELPER(lctlg)(CPUS390XState *env, uint32_t r1, uint64_t a2, uint32_t r3)
{
    uintptr_t ra = GETPC();
    bool PERchanged = false;
    uint64_t src = a2;
    uint32_t i;

    if (src & 0x7) {
        s390_program_interrupt(env, PGM_SPECIFICATION, 6, ra);
    }

    for (i = r1;; i = (i + 1) % 16) {
        uint64_t val = cpu_ldq_data_ra(env, src, ra);
        if (env->cregs[i] != val && i >= 9 && i <= 11) {
            PERchanged = true;
        }
        env->cregs[i] = val;
        HELPER_LOG("load ctl %d from 0x%" PRIx64 " == 0x%" PRIx64 "\n",
                   i, src, val);
        src += sizeof(uint64_t);

        if (i == r3) {
            break;
        }
    }

    if (PERchanged && env->psw.mask & PSW_MASK_PER) {
        s390_cpu_recompute_watchpoints(env_cpu(env));
    }

    tlb_flush(env_cpu(env));
}

void HELPER(lctl)(CPUS390XState *env, uint32_t r1, uint64_t a2, uint32_t r3)
{
    uintptr_t ra = GETPC();
    bool PERchanged = false;
    uint64_t src = a2;
    uint32_t i;

    if (src & 0x3) {
        s390_program_interrupt(env, PGM_SPECIFICATION, 4, ra);
    }

    for (i = r1;; i = (i + 1) % 16) {
        uint32_t val = cpu_ldl_data_ra(env, src, ra);
        if ((uint32_t)env->cregs[i] != val && i >= 9 && i <= 11) {
            PERchanged = true;
        }
        env->cregs[i] = deposit64(env->cregs[i], 0, 32, val);
        HELPER_LOG("load ctl %d from 0x%" PRIx64 " == 0x%x\n", i, src, val);
        src += sizeof(uint32_t);

        if (i == r3) {
            break;
        }
    }

    if (PERchanged && env->psw.mask & PSW_MASK_PER) {
        s390_cpu_recompute_watchpoints(env_cpu(env));
    }

    tlb_flush(env_cpu(env));
}

void HELPER(stctg)(CPUS390XState *env, uint32_t r1, uint64_t a2, uint32_t r3)
{
    uintptr_t ra = GETPC();
    uint64_t dest = a2;
    uint32_t i;

    if (dest & 0x7) {
        s390_program_interrupt(env, PGM_SPECIFICATION, 6, ra);
    }

    for (i = r1;; i = (i + 1) % 16) {
        cpu_stq_data_ra(env, dest, env->cregs[i], ra);
        dest += sizeof(uint64_t);

        if (i == r3) {
            break;
        }
    }
}

void HELPER(stctl)(CPUS390XState *env, uint32_t r1, uint64_t a2, uint32_t r3)
{
    uintptr_t ra = GETPC();
    uint64_t dest = a2;
    uint32_t i;

    if (dest & 0x3) {
        s390_program_interrupt(env, PGM_SPECIFICATION, 4, ra);
    }

    for (i = r1;; i = (i + 1) % 16) {
        cpu_stl_data_ra(env, dest, env->cregs[i], ra);
        dest += sizeof(uint32_t);

        if (i == r3) {
            break;
        }
    }
}

uint32_t HELPER(testblock)(CPUS390XState *env, uint64_t real_addr)
{
    uintptr_t ra = GETPC();
    int i;

    real_addr = wrap_address(env, real_addr) & TARGET_PAGE_MASK;

    for (i = 0; i < TARGET_PAGE_SIZE; i += 8) {
        cpu_stq_real_ra(env, real_addr + i, 0, ra);
    }

    return 0;
}

uint32_t HELPER(tprot)(CPUS390XState *env, uint64_t a1, uint64_t a2)
{
    S390CPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);

    /*
     * TODO: we currently don't handle all access protection types
     * (including access-list and key-controlled) as well as AR mode.
     */
    if (!s390_cpu_virt_mem_check_write(cpu, a1, 0, 1)) {
        /* Fetching permitted; storing permitted */
        return 0;
    }

    if (env->int_pgm_code == PGM_PROTECTION) {
        /* retry if reading is possible */
        cs->exception_index = 0;
        if (!s390_cpu_virt_mem_check_read(cpu, a1, 0, 1)) {
            /* Fetching permitted; storing not permitted */
            return 1;
        }
    }

    switch (env->int_pgm_code) {
    case PGM_PROTECTION:
        /* Fetching not permitted; storing not permitted */
        cs->exception_index = 0;
        return 2;
    case PGM_ADDRESSING:
    case PGM_TRANS_SPEC:
        /* exceptions forwarded to the guest */
        s390_cpu_virt_mem_handle_exc(cpu, GETPC());
        return 0;
    }

    /* Translation not available */
    cs->exception_index = 0;
    return 3;
}

/* insert storage key extended */
uint64_t HELPER(iske)(CPUS390XState *env, uint64_t r2)
{
    static S390SKeysState *ss;
    static S390SKeysClass *skeyclass;
    uint64_t addr = wrap_address(env, r2);
    uint8_t key;

    if (addr > ram_size) {
        return 0;
    }

    if (unlikely(!ss)) {
        ss = s390_get_skeys_device();
        skeyclass = S390_SKEYS_GET_CLASS(ss);
    }

    if (skeyclass->get_skeys(ss, addr / TARGET_PAGE_SIZE, 1, &key)) {
        return 0;
    }
    return key;
}

/* set storage key extended */
void HELPER(sske)(CPUS390XState *env, uint64_t r1, uint64_t r2)
{
    static S390SKeysState *ss;
    static S390SKeysClass *skeyclass;
    uint64_t addr = wrap_address(env, r2);
    uint8_t key;

    if (addr > ram_size) {
        return;
    }

    if (unlikely(!ss)) {
        ss = s390_get_skeys_device();
        skeyclass = S390_SKEYS_GET_CLASS(ss);
    }

    key = (uint8_t) r1;
    skeyclass->set_skeys(ss, addr / TARGET_PAGE_SIZE, 1, &key);
}

/* reset reference bit extended */
uint32_t HELPER(rrbe)(CPUS390XState *env, uint64_t r2)
{
    static S390SKeysState *ss;
    static S390SKeysClass *skeyclass;
    uint8_t re, key;

    if (r2 > ram_size) {
        return 0;
    }

    if (unlikely(!ss)) {
        ss = s390_get_skeys_device();
        skeyclass = S390_SKEYS_GET_CLASS(ss);
    }

    if (skeyclass->get_skeys(ss, r2 / TARGET_PAGE_SIZE, 1, &key)) {
        return 0;
    }

    re = key & (SK_R | SK_C);
    key &= ~SK_R;

    if (skeyclass->set_skeys(ss, r2 / TARGET_PAGE_SIZE, 1, &key)) {
        return 0;
    }

    /*
     * cc
     *
     * 0  Reference bit zero; change bit zero
     * 1  Reference bit zero; change bit one
     * 2  Reference bit one; change bit zero
     * 3  Reference bit one; change bit one
     */

    return re >> 1;
}

uint32_t HELPER(mvcs)(CPUS390XState *env, uint64_t l, uint64_t a1, uint64_t a2)
{
    uintptr_t ra = GETPC();
    int cc = 0, i;

    HELPER_LOG("%s: %16" PRIx64 " %16" PRIx64 " %16" PRIx64 "\n",
               __func__, l, a1, a2);

    if (l > 256) {
        /* max 256 */
        l = 256;
        cc = 3;
    }

    /* XXX replace w/ memcpy */
    for (i = 0; i < l; i++) {
        uint8_t x = cpu_ldub_primary_ra(env, a2 + i, ra);
        cpu_stb_secondary_ra(env, a1 + i, x, ra);
    }

    return cc;
}

uint32_t HELPER(mvcp)(CPUS390XState *env, uint64_t l, uint64_t a1, uint64_t a2)
{
    uintptr_t ra = GETPC();
    int cc = 0, i;

    HELPER_LOG("%s: %16" PRIx64 " %16" PRIx64 " %16" PRIx64 "\n",
               __func__, l, a1, a2);

    if (l > 256) {
        /* max 256 */
        l = 256;
        cc = 3;
    }

    /* XXX replace w/ memcpy */
    for (i = 0; i < l; i++) {
        uint8_t x = cpu_ldub_secondary_ra(env, a2 + i, ra);
        cpu_stb_primary_ra(env, a1 + i, x, ra);
    }

    return cc;
}

void HELPER(idte)(CPUS390XState *env, uint64_t r1, uint64_t r2, uint32_t m4)
{
    CPUState *cs = env_cpu(env);
    const uintptr_t ra = GETPC();
    uint64_t table, entry, raddr;
    uint16_t entries, i, index = 0;

    if (r2 & 0xff000) {
        s390_program_interrupt(env, PGM_SPECIFICATION, 4, ra);
    }

    if (!(r2 & 0x800)) {
        /* invalidation-and-clearing operation */
        table = r1 & ASCE_ORIGIN;
        entries = (r2 & 0x7ff) + 1;

        switch (r1 & ASCE_TYPE_MASK) {
        case ASCE_TYPE_REGION1:
            index = (r2 >> 53) & 0x7ff;
            break;
        case ASCE_TYPE_REGION2:
            index = (r2 >> 42) & 0x7ff;
            break;
        case ASCE_TYPE_REGION3:
            index = (r2 >> 31) & 0x7ff;
            break;
        case ASCE_TYPE_SEGMENT:
            index = (r2 >> 20) & 0x7ff;
            break;
        }
        for (i = 0; i < entries; i++) {
            /* addresses are not wrapped in 24/31bit mode but table index is */
            raddr = table + ((index + i) & 0x7ff) * sizeof(entry);
            entry = cpu_ldq_real_ra(env, raddr, ra);
            if (!(entry & REGION_ENTRY_INV)) {
                /* we are allowed to not store if already invalid */
                entry |= REGION_ENTRY_INV;
                cpu_stq_real_ra(env, raddr, entry, ra);
            }
        }
    }

    /* We simply flush the complete tlb, therefore we can ignore r3. */
    if (m4 & 1) {
        tlb_flush(cs);
    } else {
        tlb_flush_all_cpus_synced(cs);
    }
}

/* invalidate pte */
void HELPER(ipte)(CPUS390XState *env, uint64_t pto, uint64_t vaddr,
                  uint32_t m4)
{
    CPUState *cs = env_cpu(env);
    const uintptr_t ra = GETPC();
    uint64_t page = vaddr & TARGET_PAGE_MASK;
    uint64_t pte_addr, pte;

    /* Compute the page table entry address */
    pte_addr = (pto & SEGMENT_ENTRY_ORIGIN);
    pte_addr += (vaddr & VADDR_PX) >> 9;

    /* Mark the page table entry as invalid */
    pte = cpu_ldq_real_ra(env, pte_addr, ra);
    pte |= PAGE_INVALID;
    cpu_stq_real_ra(env, pte_addr, pte, ra);

    /* XXX we exploit the fact that Linux passes the exact virtual
       address here - it's not obliged to! */
    if (m4 & 1) {
        if (vaddr & ~VADDR_PX) {
            tlb_flush_page(cs, page);
            /* XXX 31-bit hack */
            tlb_flush_page(cs, page ^ 0x80000000);
        } else {
            /* looks like we don't have a valid virtual address */
            tlb_flush(cs);
        }
    } else {
        if (vaddr & ~VADDR_PX) {
            tlb_flush_page_all_cpus_synced(cs, page);
            /* XXX 31-bit hack */
            tlb_flush_page_all_cpus_synced(cs, page ^ 0x80000000);
        } else {
            /* looks like we don't have a valid virtual address */
            tlb_flush_all_cpus_synced(cs);
        }
    }
}

/* flush local tlb */
void HELPER(ptlb)(CPUS390XState *env)
{
    tlb_flush(env_cpu(env));
}

/* flush global tlb */
void HELPER(purge)(CPUS390XState *env)
{
    tlb_flush_all_cpus_synced(env_cpu(env));
}

/* load using real address */
uint64_t HELPER(lura)(CPUS390XState *env, uint64_t addr)
{
    return cpu_ldl_real_ra(env, wrap_address(env, addr), GETPC());
}

uint64_t HELPER(lurag)(CPUS390XState *env, uint64_t addr)
{
    return cpu_ldq_real_ra(env, wrap_address(env, addr), GETPC());
}

/* store using real address */
void HELPER(stura)(CPUS390XState *env, uint64_t addr, uint64_t v1)
{
    cpu_stl_real_ra(env, wrap_address(env, addr), (uint32_t)v1, GETPC());

    if ((env->psw.mask & PSW_MASK_PER) &&
        (env->cregs[9] & PER_CR9_EVENT_STORE) &&
        (env->cregs[9] & PER_CR9_EVENT_STORE_REAL)) {
        /* PSW is saved just before calling the helper.  */
        env->per_address = env->psw.addr;
        env->per_perc_atmid = PER_CODE_EVENT_STORE_REAL | get_per_atmid(env);
    }
}

void HELPER(sturg)(CPUS390XState *env, uint64_t addr, uint64_t v1)
{
    cpu_stq_real_ra(env, wrap_address(env, addr), v1, GETPC());

    if ((env->psw.mask & PSW_MASK_PER) &&
        (env->cregs[9] & PER_CR9_EVENT_STORE) &&
        (env->cregs[9] & PER_CR9_EVENT_STORE_REAL)) {
        /* PSW is saved just before calling the helper.  */
        env->per_address = env->psw.addr;
        env->per_perc_atmid = PER_CODE_EVENT_STORE_REAL | get_per_atmid(env);
    }
}

/* load real address */
uint64_t HELPER(lra)(CPUS390XState *env, uint64_t addr)
{
    CPUState *cs = env_cpu(env);
    uint32_t cc = 0;
    uint64_t asc = env->psw.mask & PSW_MASK_ASC;
    uint64_t ret;
    int old_exc, flags;

    /* XXX incomplete - has more corner cases */
    if (!(env->psw.mask & PSW_MASK_64) && (addr >> 32)) {
        s390_program_interrupt(env, PGM_SPECIAL_OP, 2, GETPC());
    }

    old_exc = cs->exception_index;
    if (mmu_translate(env, addr, 0, asc, &ret, &flags, true)) {
        cc = 3;
    }
    if (cs->exception_index == EXCP_PGM) {
        ret = env->int_pgm_code | 0x80000000;
    } else {
        ret |= addr & ~TARGET_PAGE_MASK;
    }
    cs->exception_index = old_exc;

    env->cc_op = cc;
    return ret;
}
#endif

/* load pair from quadword */
uint64_t HELPER(lpq)(CPUS390XState *env, uint64_t addr)
{
    uintptr_t ra = GETPC();
    uint64_t hi, lo;

    check_alignment(env, addr, 16, ra);
    hi = cpu_ldq_data_ra(env, addr + 0, ra);
    lo = cpu_ldq_data_ra(env, addr + 8, ra);

    env->retxl = lo;
    return hi;
}

uint64_t HELPER(lpq_parallel)(CPUS390XState *env, uint64_t addr)
{
    uintptr_t ra = GETPC();
    uint64_t hi, lo;
    int mem_idx;
    TCGMemOpIdx oi;
    Int128 v;

    assert(HAVE_ATOMIC128);

    mem_idx = cpu_mmu_index(env, false);
    oi = make_memop_idx(MO_TEQ | MO_ALIGN_16, mem_idx);
    v = helper_atomic_ldo_be_mmu(env, addr, oi, ra);
    hi = int128_gethi(v);
    lo = int128_getlo(v);

    env->retxl = lo;
    return hi;
}

/* store pair to quadword */
void HELPER(stpq)(CPUS390XState *env, uint64_t addr,
                  uint64_t low, uint64_t high)
{
    uintptr_t ra = GETPC();

    check_alignment(env, addr, 16, ra);
    cpu_stq_data_ra(env, addr + 0, high, ra);
    cpu_stq_data_ra(env, addr + 8, low, ra);
}

void HELPER(stpq_parallel)(CPUS390XState *env, uint64_t addr,
                           uint64_t low, uint64_t high)
{
    uintptr_t ra = GETPC();
    int mem_idx;
    TCGMemOpIdx oi;
    Int128 v;

    assert(HAVE_ATOMIC128);

    mem_idx = cpu_mmu_index(env, false);
    oi = make_memop_idx(MO_TEQ | MO_ALIGN_16, mem_idx);
    v = int128_make128(low, high);
    helper_atomic_sto_be_mmu(env, addr, v, oi, ra);
}

/* Execute instruction.  This instruction executes an insn modified with
   the contents of r1.  It does not change the executed instruction in memory;
   it does not change the program counter.

   Perform this by recording the modified instruction in env->ex_value.
   This will be noticed by cpu_get_tb_cpu_state and thus tb translation.
*/
void HELPER(ex)(CPUS390XState *env, uint32_t ilen, uint64_t r1, uint64_t addr)
{
    uint64_t insn = cpu_lduw_code(env, addr);
    uint8_t opc = insn >> 8;

    /* Or in the contents of R1[56:63].  */
    insn |= r1 & 0xff;

    /* Load the rest of the instruction.  */
    insn <<= 48;
    switch (get_ilen(opc)) {
    case 2:
        break;
    case 4:
        insn |= (uint64_t)cpu_lduw_code(env, addr + 2) << 32;
        break;
    case 6:
        insn |= (uint64_t)(uint32_t)cpu_ldl_code(env, addr + 2) << 16;
        break;
    default:
        g_assert_not_reached();
    }

    /* The very most common cases can be sped up by avoiding a new TB.  */
    if ((opc & 0xf0) == 0xd0) {
        typedef uint32_t (*dx_helper)(CPUS390XState *, uint32_t, uint64_t,
                                      uint64_t, uintptr_t);
        static const dx_helper dx[16] = {
            [0x0] = do_helper_trt_bkwd,
            [0x2] = do_helper_mvc,
            [0x4] = do_helper_nc,
            [0x5] = do_helper_clc,
            [0x6] = do_helper_oc,
            [0x7] = do_helper_xc,
            [0xc] = do_helper_tr,
            [0xd] = do_helper_trt_fwd,
        };
        dx_helper helper = dx[opc & 0xf];

        if (helper) {
            uint32_t l = extract64(insn, 48, 8);
            uint32_t b1 = extract64(insn, 44, 4);
            uint32_t d1 = extract64(insn, 32, 12);
            uint32_t b2 = extract64(insn, 28, 4);
            uint32_t d2 = extract64(insn, 16, 12);
            uint64_t a1 = wrap_address(env, env->regs[b1] + d1);
            uint64_t a2 = wrap_address(env, env->regs[b2] + d2);

            env->cc_op = helper(env, l, a1, a2, 0);
            env->psw.addr += ilen;
            return;
        }
    } else if (opc == 0x0a) {
        env->int_svc_code = extract64(insn, 48, 8);
        env->int_svc_ilen = ilen;
        helper_exception(env, EXCP_SVC);
        g_assert_not_reached();
    }

    /* Record the insn we want to execute as well as the ilen to use
       during the execution of the target insn.  This will also ensure
       that ex_value is non-zero, which flags that we are in a state
       that requires such execution.  */
    env->ex_value = insn | ilen;
}

uint32_t HELPER(mvcos)(CPUS390XState *env, uint64_t dest, uint64_t src,
                       uint64_t len)
{
    const uint8_t psw_key = (env->psw.mask & PSW_MASK_KEY) >> PSW_SHIFT_KEY;
    const uint8_t psw_as = (env->psw.mask & PSW_MASK_ASC) >> PSW_SHIFT_ASC;
    const uint64_t r0 = env->regs[0];
    const uintptr_t ra = GETPC();
    uint8_t dest_key, dest_as, dest_k, dest_a;
    uint8_t src_key, src_as, src_k, src_a;
    uint64_t val;
    int cc = 0;

    HELPER_LOG("%s dest %" PRIx64 ", src %" PRIx64 ", len %" PRIx64 "\n",
               __func__, dest, src, len);

    if (!(env->psw.mask & PSW_MASK_DAT)) {
        s390_program_interrupt(env, PGM_SPECIAL_OP, 6, ra);
    }

    /* OAC (operand access control) for the first operand -> dest */
    val = (r0 & 0xffff0000ULL) >> 16;
    dest_key = (val >> 12) & 0xf;
    dest_as = (val >> 6) & 0x3;
    dest_k = (val >> 1) & 0x1;
    dest_a = val & 0x1;

    /* OAC (operand access control) for the second operand -> src */
    val = (r0 & 0x0000ffffULL);
    src_key = (val >> 12) & 0xf;
    src_as = (val >> 6) & 0x3;
    src_k = (val >> 1) & 0x1;
    src_a = val & 0x1;

    if (!dest_k) {
        dest_key = psw_key;
    }
    if (!src_k) {
        src_key = psw_key;
    }
    if (!dest_a) {
        dest_as = psw_as;
    }
    if (!src_a) {
        src_as = psw_as;
    }

    if (dest_a && dest_as == AS_HOME && (env->psw.mask & PSW_MASK_PSTATE)) {
        s390_program_interrupt(env, PGM_SPECIAL_OP, 6, ra);
    }
    if (!(env->cregs[0] & CR0_SECONDARY) &&
        (dest_as == AS_SECONDARY || src_as == AS_SECONDARY)) {
        s390_program_interrupt(env, PGM_SPECIAL_OP, 6, ra);
    }
    if (!psw_key_valid(env, dest_key) || !psw_key_valid(env, src_key)) {
        s390_program_interrupt(env, PGM_PRIVILEGED, 6, ra);
    }

    len = wrap_length(env, len);
    if (len > 4096) {
        cc = 3;
        len = 4096;
    }

    /* FIXME: AR-mode and proper problem state mode (using PSW keys) missing */
    if (src_as == AS_ACCREG || dest_as == AS_ACCREG ||
        (env->psw.mask & PSW_MASK_PSTATE)) {
        qemu_log_mask(LOG_UNIMP, "%s: AR-mode and PSTATE support missing\n",
                      __func__);
        s390_program_interrupt(env, PGM_ADDRESSING, 6, ra);
    }

    /* FIXME: a) LAP
     *        b) Access using correct keys
     *        c) AR-mode
     */
#ifdef CONFIG_USER_ONLY
    /* psw keys are never valid in user mode, we will never reach this */
    g_assert_not_reached();
#else
    fast_memmove_as(env, dest, src, len, dest_as, src_as, ra);
#endif

    return cc;
}

/* Decode a Unicode character.  A return value < 0 indicates success, storing
   the UTF-32 result into OCHAR and the input length into OLEN.  A return
   value >= 0 indicates failure, and the CC value to be returned.  */
typedef int (*decode_unicode_fn)(CPUS390XState *env, uint64_t addr,
                                 uint64_t ilen, bool enh_check, uintptr_t ra,
                                 uint32_t *ochar, uint32_t *olen);

/* Encode a Unicode character.  A return value < 0 indicates success, storing
   the bytes into ADDR and the output length into OLEN.  A return value >= 0
   indicates failure, and the CC value to be returned.  */
typedef int (*encode_unicode_fn)(CPUS390XState *env, uint64_t addr,
                                 uint64_t ilen, uintptr_t ra, uint32_t c,
                                 uint32_t *olen);

static int decode_utf8(CPUS390XState *env, uint64_t addr, uint64_t ilen,
                       bool enh_check, uintptr_t ra,
                       uint32_t *ochar, uint32_t *olen)
{
    uint8_t s0, s1, s2, s3;
    uint32_t c, l;

    if (ilen < 1) {
        return 0;
    }
    s0 = cpu_ldub_data_ra(env, addr, ra);
    if (s0 <= 0x7f) {
        /* one byte character */
        l = 1;
        c = s0;
    } else if (s0 <= (enh_check ? 0xc1 : 0xbf)) {
        /* invalid character */
        return 2;
    } else if (s0 <= 0xdf) {
        /* two byte character */
        l = 2;
        if (ilen < 2) {
            return 0;
        }
        s1 = cpu_ldub_data_ra(env, addr + 1, ra);
        c = s0 & 0x1f;
        c = (c << 6) | (s1 & 0x3f);
        if (enh_check && (s1 & 0xc0) != 0x80) {
            return 2;
        }
    } else if (s0 <= 0xef) {
        /* three byte character */
        l = 3;
        if (ilen < 3) {
            return 0;
        }
        s1 = cpu_ldub_data_ra(env, addr + 1, ra);
        s2 = cpu_ldub_data_ra(env, addr + 2, ra);
        c = s0 & 0x0f;
        c = (c << 6) | (s1 & 0x3f);
        c = (c << 6) | (s2 & 0x3f);
        /* Fold the byte-by-byte range descriptions in the PoO into
           tests against the complete value.  It disallows encodings
           that could be smaller, and the UTF-16 surrogates.  */
        if (enh_check
            && ((s1 & 0xc0) != 0x80
                || (s2 & 0xc0) != 0x80
                || c < 0x1000
                || (c >= 0xd800 && c <= 0xdfff))) {
            return 2;
        }
    } else if (s0 <= (enh_check ? 0xf4 : 0xf7)) {
        /* four byte character */
        l = 4;
        if (ilen < 4) {
            return 0;
        }
        s1 = cpu_ldub_data_ra(env, addr + 1, ra);
        s2 = cpu_ldub_data_ra(env, addr + 2, ra);
        s3 = cpu_ldub_data_ra(env, addr + 3, ra);
        c = s0 & 0x07;
        c = (c << 6) | (s1 & 0x3f);
        c = (c << 6) | (s2 & 0x3f);
        c = (c << 6) | (s3 & 0x3f);
        /* See above.  */
        if (enh_check
            && ((s1 & 0xc0) != 0x80
                || (s2 & 0xc0) != 0x80
                || (s3 & 0xc0) != 0x80
                || c < 0x010000
                || c > 0x10ffff)) {
            return 2;
        }
    } else {
        /* invalid character */
        return 2;
    }

    *ochar = c;
    *olen = l;
    return -1;
}

static int decode_utf16(CPUS390XState *env, uint64_t addr, uint64_t ilen,
                        bool enh_check, uintptr_t ra,
                        uint32_t *ochar, uint32_t *olen)
{
    uint16_t s0, s1;
    uint32_t c, l;

    if (ilen < 2) {
        return 0;
    }
    s0 = cpu_lduw_data_ra(env, addr, ra);
    if ((s0 & 0xfc00) != 0xd800) {
        /* one word character */
        l = 2;
        c = s0;
    } else {
        /* two word character */
        l = 4;
        if (ilen < 4) {
            return 0;
        }
        s1 = cpu_lduw_data_ra(env, addr + 2, ra);
        c = extract32(s0, 6, 4) + 1;
        c = (c << 6) | (s0 & 0x3f);
        c = (c << 10) | (s1 & 0x3ff);
        if (enh_check && (s1 & 0xfc00) != 0xdc00) {
            /* invalid surrogate character */
            return 2;
        }
    }

    *ochar = c;
    *olen = l;
    return -1;
}

static int decode_utf32(CPUS390XState *env, uint64_t addr, uint64_t ilen,
                        bool enh_check, uintptr_t ra,
                        uint32_t *ochar, uint32_t *olen)
{
    uint32_t c;

    if (ilen < 4) {
        return 0;
    }
    c = cpu_ldl_data_ra(env, addr, ra);
    if ((c >= 0xd800 && c <= 0xdbff) || c > 0x10ffff) {
        /* invalid unicode character */
        return 2;
    }

    *ochar = c;
    *olen = 4;
    return -1;
}

static int encode_utf8(CPUS390XState *env, uint64_t addr, uint64_t ilen,
                       uintptr_t ra, uint32_t c, uint32_t *olen)
{
    uint8_t d[4];
    uint32_t l, i;

    if (c <= 0x7f) {
        /* one byte character */
        l = 1;
        d[0] = c;
    } else if (c <= 0x7ff) {
        /* two byte character */
        l = 2;
        d[1] = 0x80 | extract32(c, 0, 6);
        d[0] = 0xc0 | extract32(c, 6, 5);
    } else if (c <= 0xffff) {
        /* three byte character */
        l = 3;
        d[2] = 0x80 | extract32(c, 0, 6);
        d[1] = 0x80 | extract32(c, 6, 6);
        d[0] = 0xe0 | extract32(c, 12, 4);
    } else {
        /* four byte character */
        l = 4;
        d[3] = 0x80 | extract32(c, 0, 6);
        d[2] = 0x80 | extract32(c, 6, 6);
        d[1] = 0x80 | extract32(c, 12, 6);
        d[0] = 0xf0 | extract32(c, 18, 3);
    }

    if (ilen < l) {
        return 1;
    }
    for (i = 0; i < l; ++i) {
        cpu_stb_data_ra(env, addr + i, d[i], ra);
    }

    *olen = l;
    return -1;
}

static int encode_utf16(CPUS390XState *env, uint64_t addr, uint64_t ilen,
                        uintptr_t ra, uint32_t c, uint32_t *olen)
{
    uint16_t d0, d1;

    if (c <= 0xffff) {
        /* one word character */
        if (ilen < 2) {
            return 1;
        }
        cpu_stw_data_ra(env, addr, c, ra);
        *olen = 2;
    } else {
        /* two word character */
        if (ilen < 4) {
            return 1;
        }
        d1 = 0xdc00 | extract32(c, 0, 10);
        d0 = 0xd800 | extract32(c, 10, 6);
        d0 = deposit32(d0, 6, 4, extract32(c, 16, 5) - 1);
        cpu_stw_data_ra(env, addr + 0, d0, ra);
        cpu_stw_data_ra(env, addr + 2, d1, ra);
        *olen = 4;
    }

    return -1;
}

static int encode_utf32(CPUS390XState *env, uint64_t addr, uint64_t ilen,
                        uintptr_t ra, uint32_t c, uint32_t *olen)
{
    if (ilen < 4) {
        return 1;
    }
    cpu_stl_data_ra(env, addr, c, ra);
    *olen = 4;
    return -1;
}

static inline uint32_t convert_unicode(CPUS390XState *env, uint32_t r1,
                                       uint32_t r2, uint32_t m3, uintptr_t ra,
                                       decode_unicode_fn decode,
                                       encode_unicode_fn encode)
{
    uint64_t dst = get_address(env, r1);
    uint64_t dlen = get_length(env, r1 + 1);
    uint64_t src = get_address(env, r2);
    uint64_t slen = get_length(env, r2 + 1);
    bool enh_check = m3 & 1;
    int cc, i;

    /* Lest we fail to service interrupts in a timely manner, limit the
       amount of work we're willing to do.  For now, let's cap at 256.  */
    for (i = 0; i < 256; ++i) {
        uint32_t c, ilen, olen;

        cc = decode(env, src, slen, enh_check, ra, &c, &ilen);
        if (unlikely(cc >= 0)) {
            break;
        }
        cc = encode(env, dst, dlen, ra, c, &olen);
        if (unlikely(cc >= 0)) {
            break;
        }

        src += ilen;
        slen -= ilen;
        dst += olen;
        dlen -= olen;
        cc = 3;
    }

    set_address(env, r1, dst);
    set_length(env, r1 + 1, dlen);
    set_address(env, r2, src);
    set_length(env, r2 + 1, slen);

    return cc;
}

uint32_t HELPER(cu12)(CPUS390XState *env, uint32_t r1, uint32_t r2, uint32_t m3)
{
    return convert_unicode(env, r1, r2, m3, GETPC(),
                           decode_utf8, encode_utf16);
}

uint32_t HELPER(cu14)(CPUS390XState *env, uint32_t r1, uint32_t r2, uint32_t m3)
{
    return convert_unicode(env, r1, r2, m3, GETPC(),
                           decode_utf8, encode_utf32);
}

uint32_t HELPER(cu21)(CPUS390XState *env, uint32_t r1, uint32_t r2, uint32_t m3)
{
    return convert_unicode(env, r1, r2, m3, GETPC(),
                           decode_utf16, encode_utf8);
}

uint32_t HELPER(cu24)(CPUS390XState *env, uint32_t r1, uint32_t r2, uint32_t m3)
{
    return convert_unicode(env, r1, r2, m3, GETPC(),
                           decode_utf16, encode_utf32);
}

uint32_t HELPER(cu41)(CPUS390XState *env, uint32_t r1, uint32_t r2, uint32_t m3)
{
    return convert_unicode(env, r1, r2, m3, GETPC(),
                           decode_utf32, encode_utf8);
}

uint32_t HELPER(cu42)(CPUS390XState *env, uint32_t r1, uint32_t r2, uint32_t m3)
{
    return convert_unicode(env, r1, r2, m3, GETPC(),
                           decode_utf32, encode_utf16);
}

void probe_write_access(CPUS390XState *env, uint64_t addr, uint64_t len,
                        uintptr_t ra)
{
#ifdef CONFIG_USER_ONLY
    if (!h2g_valid(addr) || !h2g_valid(addr + len - 1) ||
        page_check_range(addr, len, PAGE_WRITE) < 0) {
        s390_program_interrupt(env, PGM_ADDRESSING, ILEN_AUTO, ra);
    }
#else
    /* test the actual access, not just any access to the page due to LAP */
    while (len) {
        const uint64_t pagelen = -(addr | -TARGET_PAGE_MASK);
        const uint64_t curlen = MIN(pagelen, len);

        probe_write(env, addr, curlen, cpu_mmu_index(env, false), ra);
        addr = wrap_address(env, addr + curlen);
        len -= curlen;
    }
#endif
}

void HELPER(probe_write_access)(CPUS390XState *env, uint64_t addr, uint64_t len)
{
    probe_write_access(env, addr, len, GETPC());
}
