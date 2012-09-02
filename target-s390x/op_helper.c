/*
 *  S/390 helper routines
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2009 Alexander Graf
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "memory.h"
#include "cputlb.h"
#include "dyngen-exec.h"
#include "host-utils.h"
#include "helper.h"
#include <string.h>
#include "kvm.h"
#include "qemu-timer.h"
#ifdef CONFIG_KVM
#include <linux/kvm.h>
#endif

#if !defined(CONFIG_USER_ONLY)
#include "sysemu.h"
#endif

/*****************************************************************************/
/* Softmmu support */
#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"

#define MMUSUFFIX _mmu

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(CPUS390XState *env1, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    TranslationBlock *tb;
    CPUS390XState *saved_env;
    int ret;

    saved_env = env;
    env = env1;
    ret = cpu_s390x_handle_mmu_fault(env, addr, is_write, mmu_idx);
    if (unlikely(ret != 0)) {
        if (likely(retaddr)) {
            /* now we have a real cpu fault */
            tb = tb_find_pc(retaddr);
            if (likely(tb)) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, retaddr);
            }
        }
        cpu_loop_exit(env);
    }
    env = saved_env;
}

#endif

/* #define DEBUG_HELPER */
#ifdef DEBUG_HELPER
#define HELPER_LOG(x...) qemu_log(x)
#else
#define HELPER_LOG(x...)
#endif

/* raise an exception */
void HELPER(exception)(uint32_t excp)
{
    HELPER_LOG("%s: exception %d\n", __func__, excp);
    env->exception_index = excp;
    cpu_loop_exit(env);
}

#ifndef CONFIG_USER_ONLY
static void mvc_fast_memset(CPUS390XState *env, uint32_t l, uint64_t dest,
                            uint8_t byte)
{
    target_phys_addr_t dest_phys;
    target_phys_addr_t len = l;
    void *dest_p;
    uint64_t asc = env->psw.mask & PSW_MASK_ASC;
    int flags;

    if (mmu_translate(env, dest, 1, asc, &dest_phys, &flags)) {
        stb(dest, byte);
        cpu_abort(env, "should never reach here");
    }
    dest_phys |= dest & ~TARGET_PAGE_MASK;

    dest_p = cpu_physical_memory_map(dest_phys, &len, 1);

    memset(dest_p, byte, len);

    cpu_physical_memory_unmap(dest_p, 1, len, len);
}

static void mvc_fast_memmove(CPUS390XState *env, uint32_t l, uint64_t dest,
                             uint64_t src)
{
    target_phys_addr_t dest_phys;
    target_phys_addr_t src_phys;
    target_phys_addr_t len = l;
    void *dest_p;
    void *src_p;
    uint64_t asc = env->psw.mask & PSW_MASK_ASC;
    int flags;

    if (mmu_translate(env, dest, 1, asc, &dest_phys, &flags)) {
        stb(dest, 0);
        cpu_abort(env, "should never reach here");
    }
    dest_phys |= dest & ~TARGET_PAGE_MASK;

    if (mmu_translate(env, src, 0, asc, &src_phys, &flags)) {
        ldub(src);
        cpu_abort(env, "should never reach here");
    }
    src_phys |= src & ~TARGET_PAGE_MASK;

    dest_p = cpu_physical_memory_map(dest_phys, &len, 1);
    src_p = cpu_physical_memory_map(src_phys, &len, 0);

    memmove(dest_p, src_p, len);

    cpu_physical_memory_unmap(dest_p, 1, len, len);
    cpu_physical_memory_unmap(src_p, 0, len, len);
}
#endif

/* and on array */
uint32_t HELPER(nc)(uint32_t l, uint64_t dest, uint64_t src)
{
    int i;
    unsigned char x;
    uint32_t cc = 0;

    HELPER_LOG("%s l %d dest %" PRIx64 " src %" PRIx64 "\n",
               __func__, l, dest, src);
    for (i = 0; i <= l; i++) {
        x = ldub(dest + i) & ldub(src + i);
        if (x) {
            cc = 1;
        }
        stb(dest + i, x);
    }
    return cc;
}

/* xor on array */
uint32_t HELPER(xc)(uint32_t l, uint64_t dest, uint64_t src)
{
    int i;
    unsigned char x;
    uint32_t cc = 0;

    HELPER_LOG("%s l %d dest %" PRIx64 " src %" PRIx64 "\n",
               __func__, l, dest, src);

#ifndef CONFIG_USER_ONLY
    /* xor with itself is the same as memset(0) */
    if ((l > 32) && (src == dest) &&
        (src & TARGET_PAGE_MASK) == ((src + l) & TARGET_PAGE_MASK)) {
        mvc_fast_memset(env, l + 1, dest, 0);
        return 0;
    }
#else
    if (src == dest) {
        memset(g2h(dest), 0, l + 1);
        return 0;
    }
#endif

    for (i = 0; i <= l; i++) {
        x = ldub(dest + i) ^ ldub(src + i);
        if (x) {
            cc = 1;
        }
        stb(dest + i, x);
    }
    return cc;
}

/* or on array */
uint32_t HELPER(oc)(uint32_t l, uint64_t dest, uint64_t src)
{
    int i;
    unsigned char x;
    uint32_t cc = 0;

    HELPER_LOG("%s l %d dest %" PRIx64 " src %" PRIx64 "\n",
               __func__, l, dest, src);
    for (i = 0; i <= l; i++) {
        x = ldub(dest + i) | ldub(src + i);
        if (x) {
            cc = 1;
        }
        stb(dest + i, x);
    }
    return cc;
}

/* memmove */
void HELPER(mvc)(uint32_t l, uint64_t dest, uint64_t src)
{
    int i = 0;
    int x = 0;
    uint32_t l_64 = (l + 1) / 8;

    HELPER_LOG("%s l %d dest %" PRIx64 " src %" PRIx64 "\n",
               __func__, l, dest, src);

#ifndef CONFIG_USER_ONLY
    if ((l > 32) &&
        (src & TARGET_PAGE_MASK) == ((src + l) & TARGET_PAGE_MASK) &&
        (dest & TARGET_PAGE_MASK) == ((dest + l) & TARGET_PAGE_MASK)) {
        if (dest == (src + 1)) {
            mvc_fast_memset(env, l + 1, dest, ldub(src));
            return;
        } else if ((src & TARGET_PAGE_MASK) != (dest & TARGET_PAGE_MASK)) {
            mvc_fast_memmove(env, l + 1, dest, src);
            return;
        }
    }
#else
    if (dest == (src + 1)) {
        memset(g2h(dest), ldub(src), l + 1);
        return;
    } else {
        memmove(g2h(dest), g2h(src), l + 1);
        return;
    }
#endif

    /* handle the parts that fit into 8-byte loads/stores */
    if (dest != (src + 1)) {
        for (i = 0; i < l_64; i++) {
            stq(dest + x, ldq(src + x));
            x += 8;
        }
    }

    /* slow version crossing pages with byte accesses */
    for (i = x; i <= l; i++) {
        stb(dest + i, ldub(src + i));
    }
}

/* compare unsigned byte arrays */
uint32_t HELPER(clc)(uint32_t l, uint64_t s1, uint64_t s2)
{
    int i;
    unsigned char x, y;
    uint32_t cc;

    HELPER_LOG("%s l %d s1 %" PRIx64 " s2 %" PRIx64 "\n",
               __func__, l, s1, s2);
    for (i = 0; i <= l; i++) {
        x = ldub(s1 + i);
        y = ldub(s2 + i);
        HELPER_LOG("%02x (%c)/%02x (%c) ", x, x, y, y);
        if (x < y) {
            cc = 1;
            goto done;
        } else if (x > y) {
            cc = 2;
            goto done;
        }
    }
    cc = 0;
 done:
    HELPER_LOG("\n");
    return cc;
}

/* compare logical under mask */
uint32_t HELPER(clm)(uint32_t r1, uint32_t mask, uint64_t addr)
{
    uint8_t r, d;
    uint32_t cc;

    HELPER_LOG("%s: r1 0x%x mask 0x%x addr 0x%" PRIx64 "\n", __func__, r1,
               mask, addr);
    cc = 0;
    while (mask) {
        if (mask & 8) {
            d = ldub(addr);
            r = (r1 & 0xff000000UL) >> 24;
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

/* store character under mask */
void HELPER(stcm)(uint32_t r1, uint32_t mask, uint64_t addr)
{
    uint8_t r;

    HELPER_LOG("%s: r1 0x%x mask 0x%x addr 0x%lx\n", __func__, r1, mask,
               addr);
    while (mask) {
        if (mask & 8) {
            r = (r1 & 0xff000000UL) >> 24;
            stb(addr, r);
            HELPER_LOG("mask 0x%x %02x (0x%lx) ", mask, r, addr);
            addr++;
        }
        mask = (mask << 1) & 0xf;
        r1 <<= 8;
    }
    HELPER_LOG("\n");
}

static inline uint64_t get_address(int x2, int b2, int d2)
{
    uint64_t r = d2;

    if (x2) {
        r += env->regs[x2];
    }

    if (b2) {
        r += env->regs[b2];
    }

    /* 31-Bit mode */
    if (!(env->psw.mask & PSW_MASK_64)) {
        r &= 0x7fffffff;
    }

    return r;
}

static inline uint64_t get_address_31fix(int reg)
{
    uint64_t r = env->regs[reg];

    /* 31-Bit mode */
    if (!(env->psw.mask & PSW_MASK_64)) {
        r &= 0x7fffffff;
    }

    return r;
}

/* search string (c is byte to search, r2 is string, r1 end of string) */
uint32_t HELPER(srst)(uint32_t c, uint32_t r1, uint32_t r2)
{
    uint64_t i;
    uint32_t cc = 2;
    uint64_t str = get_address_31fix(r2);
    uint64_t end = get_address_31fix(r1);

    HELPER_LOG("%s: c %d *r1 0x%" PRIx64 " *r2 0x%" PRIx64 "\n", __func__,
               c, env->regs[r1], env->regs[r2]);

    for (i = str; i != end; i++) {
        if (ldub(i) == c) {
            env->regs[r1] = i;
            cc = 1;
            break;
        }
    }

    return cc;
}

/* unsigned string compare (c is string terminator) */
uint32_t HELPER(clst)(uint32_t c, uint32_t r1, uint32_t r2)
{
    uint64_t s1 = get_address_31fix(r1);
    uint64_t s2 = get_address_31fix(r2);
    uint8_t v1, v2;
    uint32_t cc;

    c = c & 0xff;
#ifdef CONFIG_USER_ONLY
    if (!c) {
        HELPER_LOG("%s: comparing '%s' and '%s'\n",
                   __func__, (char *)g2h(s1), (char *)g2h(s2));
    }
#endif
    for (;;) {
        v1 = ldub(s1);
        v2 = ldub(s2);
        if ((v1 == c || v2 == c) || (v1 != v2)) {
            break;
        }
        s1++;
        s2++;
    }

    if (v1 == v2) {
        cc = 0;
    } else {
        cc = (v1 < v2) ? 1 : 2;
        /* FIXME: 31-bit mode! */
        env->regs[r1] = s1;
        env->regs[r2] = s2;
    }
    return cc;
}

/* move page */
void HELPER(mvpg)(uint64_t r0, uint64_t r1, uint64_t r2)
{
    /* XXX missing r0 handling */
#ifdef CONFIG_USER_ONLY
    int i;

    for (i = 0; i < TARGET_PAGE_SIZE; i++) {
        stb(r1 + i, ldub(r2 + i));
    }
#else
    mvc_fast_memmove(env, TARGET_PAGE_SIZE, r1, r2);
#endif
}

/* string copy (c is string terminator) */
void HELPER(mvst)(uint32_t c, uint32_t r1, uint32_t r2)
{
    uint64_t dest = get_address_31fix(r1);
    uint64_t src = get_address_31fix(r2);
    uint8_t v;

    c = c & 0xff;
#ifdef CONFIG_USER_ONLY
    if (!c) {
        HELPER_LOG("%s: copy '%s' to 0x%lx\n", __func__, (char *)g2h(src),
                   dest);
    }
#endif
    for (;;) {
        v = ldub(src);
        stb(dest, v);
        if (v == c) {
            break;
        }
        src++;
        dest++;
    }
    env->regs[r1] = dest; /* FIXME: 31-bit mode! */
}

/* compare and swap 64-bit */
uint32_t HELPER(csg)(uint32_t r1, uint64_t a2, uint32_t r3)
{
    /* FIXME: locking? */
    uint32_t cc;
    uint64_t v2 = ldq(a2);

    if (env->regs[r1] == v2) {
        cc = 0;
        stq(a2, env->regs[r3]);
    } else {
        cc = 1;
        env->regs[r1] = v2;
    }
    return cc;
}

/* compare double and swap 64-bit */
uint32_t HELPER(cdsg)(uint32_t r1, uint64_t a2, uint32_t r3)
{
    /* FIXME: locking? */
    uint32_t cc;
    uint64_t v2_hi = ldq(a2);
    uint64_t v2_lo = ldq(a2 + 8);
    uint64_t v1_hi = env->regs[r1];
    uint64_t v1_lo = env->regs[r1 + 1];

    if ((v1_hi == v2_hi) && (v1_lo == v2_lo)) {
        cc = 0;
        stq(a2, env->regs[r3]);
        stq(a2 + 8, env->regs[r3 + 1]);
    } else {
        cc = 1;
        env->regs[r1] = v2_hi;
        env->regs[r1 + 1] = v2_lo;
    }

    return cc;
}

/* compare and swap 32-bit */
uint32_t HELPER(cs)(uint32_t r1, uint64_t a2, uint32_t r3)
{
    /* FIXME: locking? */
    uint32_t cc;
    uint32_t v2 = ldl(a2);

    HELPER_LOG("%s: r1 %d a2 0x%lx r3 %d\n", __func__, r1, a2, r3);
    if (((uint32_t)env->regs[r1]) == v2) {
        cc = 0;
        stl(a2, (uint32_t)env->regs[r3]);
    } else {
        cc = 1;
        env->regs[r1] = (env->regs[r1] & 0xffffffff00000000ULL) | v2;
    }
    return cc;
}

static uint32_t helper_icm(uint32_t r1, uint64_t address, uint32_t mask)
{
    int pos = 24; /* top of the lower half of r1 */
    uint64_t rmask = 0xff000000ULL;
    uint8_t val = 0;
    int ccd = 0;
    uint32_t cc = 0;

    while (mask) {
        if (mask & 8) {
            env->regs[r1] &= ~rmask;
            val = ldub(address);
            if ((val & 0x80) && !ccd) {
                cc = 1;
            }
            ccd = 1;
            if (val && cc == 0) {
                cc = 2;
            }
            env->regs[r1] |= (uint64_t)val << pos;
            address++;
        }
        mask = (mask << 1) & 0xf;
        pos -= 8;
        rmask >>= 8;
    }

    return cc;
}

/* execute instruction
   this instruction executes an insn modified with the contents of r1
   it does not change the executed instruction in memory
   it does not change the program counter
   in other words: tricky...
   currently implemented by interpreting the cases it is most commonly used in
*/
uint32_t HELPER(ex)(uint32_t cc, uint64_t v1, uint64_t addr, uint64_t ret)
{
    uint16_t insn = lduw_code(addr);

    HELPER_LOG("%s: v1 0x%lx addr 0x%lx insn 0x%x\n", __func__, v1, addr,
               insn);
    if ((insn & 0xf0ff) == 0xd000) {
        uint32_t l, insn2, b1, b2, d1, d2;

        l = v1 & 0xff;
        insn2 = ldl_code(addr + 2);
        b1 = (insn2 >> 28) & 0xf;
        b2 = (insn2 >> 12) & 0xf;
        d1 = (insn2 >> 16) & 0xfff;
        d2 = insn2 & 0xfff;
        switch (insn & 0xf00) {
        case 0x200:
            helper_mvc(l, get_address(0, b1, d1), get_address(0, b2, d2));
            break;
        case 0x500:
            cc = helper_clc(l, get_address(0, b1, d1), get_address(0, b2, d2));
            break;
        case 0x700:
            cc = helper_xc(l, get_address(0, b1, d1), get_address(0, b2, d2));
            break;
        case 0xc00:
            helper_tr(l, get_address(0, b1, d1), get_address(0, b2, d2));
            break;
        default:
            goto abort;
            break;
        }
    } else if ((insn & 0xff00) == 0x0a00) {
        /* supervisor call */
        HELPER_LOG("%s: svc %ld via execute\n", __func__, (insn | v1) & 0xff);
        env->psw.addr = ret - 4;
        env->int_svc_code = (insn | v1) & 0xff;
        env->int_svc_ilc = 4;
        helper_exception(EXCP_SVC);
    } else if ((insn & 0xff00) == 0xbf00) {
        uint32_t insn2, r1, r3, b2, d2;

        insn2 = ldl_code(addr + 2);
        r1 = (insn2 >> 20) & 0xf;
        r3 = (insn2 >> 16) & 0xf;
        b2 = (insn2 >> 12) & 0xf;
        d2 = insn2 & 0xfff;
        cc = helper_icm(r1, get_address(0, b2, d2), r3);
    } else {
    abort:
        cpu_abort(env, "EXECUTE on instruction prefix 0x%x not implemented\n",
                  insn);
    }
    return cc;
}

/* store character under mask high operates on the upper half of r1 */
void HELPER(stcmh)(uint32_t r1, uint64_t address, uint32_t mask)
{
    int pos = 56; /* top of the upper half of r1 */

    while (mask) {
        if (mask & 8) {
            stb(address, (env->regs[r1] >> pos) & 0xff);
            address++;
        }
        mask = (mask << 1) & 0xf;
        pos -= 8;
    }
}

/* insert character under mask high; same as icm, but operates on the
   upper half of r1 */
uint32_t HELPER(icmh)(uint32_t r1, uint64_t address, uint32_t mask)
{
    int pos = 56; /* top of the upper half of r1 */
    uint64_t rmask = 0xff00000000000000ULL;
    uint8_t val = 0;
    int ccd = 0;
    uint32_t cc = 0;

    while (mask) {
        if (mask & 8) {
            env->regs[r1] &= ~rmask;
            val = ldub(address);
            if ((val & 0x80) && !ccd) {
                cc = 1;
            }
            ccd = 1;
            if (val && cc == 0) {
                cc = 2;
            }
            env->regs[r1] |= (uint64_t)val << pos;
            address++;
        }
        mask = (mask << 1) & 0xf;
        pos -= 8;
        rmask >>= 8;
    }

    return cc;
}

/* load access registers r1 to r3 from memory at a2 */
void HELPER(lam)(uint32_t r1, uint64_t a2, uint32_t r3)
{
    int i;

    for (i = r1;; i = (i + 1) % 16) {
        env->aregs[i] = ldl(a2);
        a2 += 4;

        if (i == r3) {
            break;
        }
    }
}

/* store access registers r1 to r3 in memory at a2 */
void HELPER(stam)(uint32_t r1, uint64_t a2, uint32_t r3)
{
    int i;

    for (i = r1;; i = (i + 1) % 16) {
        stl(a2, env->aregs[i]);
        a2 += 4;

        if (i == r3) {
            break;
        }
    }
}

/* move long */
uint32_t HELPER(mvcl)(uint32_t r1, uint32_t r2)
{
    uint64_t destlen = env->regs[r1 + 1] & 0xffffff;
    uint64_t dest = get_address_31fix(r1);
    uint64_t srclen = env->regs[r2 + 1] & 0xffffff;
    uint64_t src = get_address_31fix(r2);
    uint8_t pad = src >> 24;
    uint8_t v;
    uint32_t cc;

    if (destlen == srclen) {
        cc = 0;
    } else if (destlen < srclen) {
        cc = 1;
    } else {
        cc = 2;
    }

    if (srclen > destlen) {
        srclen = destlen;
    }

    for (; destlen && srclen; src++, dest++, destlen--, srclen--) {
        v = ldub(src);
        stb(dest, v);
    }

    for (; destlen; dest++, destlen--) {
        stb(dest, pad);
    }

    env->regs[r1 + 1] = destlen;
    /* can't use srclen here, we trunc'ed it */
    env->regs[r2 + 1] -= src - env->regs[r2];
    env->regs[r1] = dest;
    env->regs[r2] = src;

    return cc;
}

/* move long extended another memcopy insn with more bells and whistles */
uint32_t HELPER(mvcle)(uint32_t r1, uint64_t a2, uint32_t r3)
{
    uint64_t destlen = env->regs[r1 + 1];
    uint64_t dest = env->regs[r1];
    uint64_t srclen = env->regs[r3 + 1];
    uint64_t src = env->regs[r3];
    uint8_t pad = a2 & 0xff;
    uint8_t v;
    uint32_t cc;

    if (!(env->psw.mask & PSW_MASK_64)) {
        destlen = (uint32_t)destlen;
        srclen = (uint32_t)srclen;
        dest &= 0x7fffffff;
        src &= 0x7fffffff;
    }

    if (destlen == srclen) {
        cc = 0;
    } else if (destlen < srclen) {
        cc = 1;
    } else {
        cc = 2;
    }

    if (srclen > destlen) {
        srclen = destlen;
    }

    for (; destlen && srclen; src++, dest++, destlen--, srclen--) {
        v = ldub(src);
        stb(dest, v);
    }

    for (; destlen; dest++, destlen--) {
        stb(dest, pad);
    }

    env->regs[r1 + 1] = destlen;
    /* can't use srclen here, we trunc'ed it */
    /* FIXME: 31-bit mode! */
    env->regs[r3 + 1] -= src - env->regs[r3];
    env->regs[r1] = dest;
    env->regs[r3] = src;

    return cc;
}

/* compare logical long extended memcompare insn with padding */
uint32_t HELPER(clcle)(uint32_t r1, uint64_t a2, uint32_t r3)
{
    uint64_t destlen = env->regs[r1 + 1];
    uint64_t dest = get_address_31fix(r1);
    uint64_t srclen = env->regs[r3 + 1];
    uint64_t src = get_address_31fix(r3);
    uint8_t pad = a2 & 0xff;
    uint8_t v1 = 0, v2 = 0;
    uint32_t cc = 0;

    if (!(destlen || srclen)) {
        return cc;
    }

    if (srclen > destlen) {
        srclen = destlen;
    }

    for (; destlen || srclen; src++, dest++, destlen--, srclen--) {
        v1 = srclen ? ldub(src) : pad;
        v2 = destlen ? ldub(dest) : pad;
        if (v1 != v2) {
            cc = (v1 < v2) ? 1 : 2;
            break;
        }
    }

    env->regs[r1 + 1] = destlen;
    /* can't use srclen here, we trunc'ed it */
    env->regs[r3 + 1] -= src - env->regs[r3];
    env->regs[r1] = dest;
    env->regs[r3] = src;

    return cc;
}

/* checksum */
void HELPER(cksm)(uint32_t r1, uint32_t r2)
{
    uint64_t src = get_address_31fix(r2);
    uint64_t src_len = env->regs[(r2 + 1) & 15];
    uint64_t cksm = (uint32_t)env->regs[r1];

    while (src_len >= 4) {
        cksm += ldl(src);

        /* move to next word */
        src_len -= 4;
        src += 4;
    }

    switch (src_len) {
    case 0:
        break;
    case 1:
        cksm += ldub(src) << 24;
        break;
    case 2:
        cksm += lduw(src) << 16;
        break;
    case 3:
        cksm += lduw(src) << 16;
        cksm += ldub(src + 2) << 8;
        break;
    }

    /* indicate we've processed everything */
    env->regs[r2] = src + src_len;
    env->regs[(r2 + 1) & 15] = 0;

    /* store result */
    env->regs[r1] = (env->regs[r1] & 0xffffffff00000000ULL) |
        ((uint32_t)cksm + (cksm >> 32));
}

void HELPER(unpk)(uint32_t len, uint64_t dest, uint64_t src)
{
    int len_dest = len >> 4;
    int len_src = len & 0xf;
    uint8_t b;
    int second_nibble = 0;

    dest += len_dest;
    src += len_src;

    /* last byte is special, it only flips the nibbles */
    b = ldub(src);
    stb(dest, (b << 4) | (b >> 4));
    src--;
    len_src--;

    /* now pad every nibble with 0xf0 */

    while (len_dest > 0) {
        uint8_t cur_byte = 0;

        if (len_src > 0) {
            cur_byte = ldub(src);
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

        stb(dest, cur_byte);
    }
}

void HELPER(tr)(uint32_t len, uint64_t array, uint64_t trans)
{
    int i;

    for (i = 0; i <= len; i++) {
        uint8_t byte = ldub(array + i);
        uint8_t new_byte = ldub(trans + byte);

        stb(array + i, new_byte);
    }
}

#ifndef CONFIG_USER_ONLY
void program_interrupt(CPUS390XState *env, uint32_t code, int ilc)
{
    qemu_log("program interrupt at %#" PRIx64 "\n", env->psw.addr);

    if (kvm_enabled()) {
#ifdef CONFIG_KVM
        kvm_s390_interrupt(env, KVM_S390_PROGRAM_INT, code);
#endif
    } else {
        env->int_pgm_code = code;
        env->int_pgm_ilc = ilc;
        env->exception_index = EXCP_PGM;
        cpu_loop_exit(env);
    }
}

/*
 * ret < 0 indicates program check, ret = 0, 1, 2, 3 -> cc
 */
int sclp_service_call(CPUS390XState *env, uint32_t sccb, uint64_t code)
{
    int r = 0;
    int shift = 0;

#ifdef DEBUG_HELPER
    printf("sclp(0x%x, 0x%" PRIx64 ")\n", sccb, code);
#endif

    /* basic checks */
    if (!memory_region_is_ram(phys_page_find(sccb >> TARGET_PAGE_BITS)->mr)) {
        return -PGM_ADDRESSING;
    }
    if (sccb & ~0x7ffffff8ul) {
        return -PGM_SPECIFICATION;
    }

    switch (code) {
    case SCLP_CMDW_READ_SCP_INFO:
    case SCLP_CMDW_READ_SCP_INFO_FORCED:
        while ((ram_size >> (20 + shift)) > 65535) {
            shift++;
        }
        stw_phys(sccb + SCP_MEM_CODE, ram_size >> (20 + shift));
        stb_phys(sccb + SCP_INCREMENT, 1 << shift);
        stw_phys(sccb + SCP_RESPONSE_CODE, 0x10);

        s390_sclp_extint(sccb & ~3);
        break;
    default:
#ifdef DEBUG_HELPER
        printf("KVM: invalid sclp call 0x%x / 0x%" PRIx64 "x\n", sccb, code);
#endif
        r = 3;
        break;
    }

    return r;
}

/* SCLP service call */
uint32_t HELPER(servc)(uint32_t r1, uint64_t r2)
{
    int r;

    r = sclp_service_call(env, r1, r2);
    if (r < 0) {
        program_interrupt(env, -r, 4);
        return 0;
    }
    return r;
}

/* DIAG */
uint64_t HELPER(diag)(uint32_t num, uint64_t mem, uint64_t code)
{
    uint64_t r;

    switch (num) {
    case 0x500:
        /* KVM hypercall */
        r = s390_virtio_hypercall(env, mem, code);
        break;
    case 0x44:
        /* yield */
        r = 0;
        break;
    case 0x308:
        /* ipl */
        r = 0;
        break;
    default:
        r = -1;
        break;
    }

    if (r) {
        program_interrupt(env, PGM_OPERATION, ILC_LATER_INC);
    }

    return r;
}

/* Store CPU ID */
void HELPER(stidp)(uint64_t a1)
{
    stq(a1, env->cpu_num);
}

/* Set Prefix */
void HELPER(spx)(uint64_t a1)
{
    uint32_t prefix;

    prefix = ldl(a1);
    env->psa = prefix & 0xfffff000;
    qemu_log("prefix: %#x\n", prefix);
    tlb_flush_page(env, 0);
    tlb_flush_page(env, TARGET_PAGE_SIZE);
}

/* Set Clock */
uint32_t HELPER(sck)(uint64_t a1)
{
    /* XXX not implemented - is it necessary? */

    return 0;
}

static inline uint64_t clock_value(CPUS390XState *env)
{
    uint64_t time;

    time = env->tod_offset +
        time2tod(qemu_get_clock_ns(vm_clock) - env->tod_basetime);

    return time;
}

/* Store Clock */
uint32_t HELPER(stck)(uint64_t a1)
{
    stq(a1, clock_value(env));

    return 0;
}

/* Store Clock Extended */
uint32_t HELPER(stcke)(uint64_t a1)
{
    stb(a1, 0);
    /* basically the same value as stck */
    stq(a1 + 1, clock_value(env) | env->cpu_num);
    /* more fine grained than stck */
    stq(a1 + 9, 0);
    /* XXX programmable fields */
    stw(a1 + 17, 0);

    return 0;
}

/* Set Clock Comparator */
void HELPER(sckc)(uint64_t a1)
{
    uint64_t time = ldq(a1);

    if (time == -1ULL) {
        return;
    }

    /* difference between now and then */
    time -= clock_value(env);
    /* nanoseconds */
    time = (time * 125) >> 9;

    qemu_mod_timer(env->tod_timer, qemu_get_clock_ns(vm_clock) + time);
}

/* Store Clock Comparator */
void HELPER(stckc)(uint64_t a1)
{
    /* XXX implement */
    stq(a1, 0);
}

/* Set CPU Timer */
void HELPER(spt)(uint64_t a1)
{
    uint64_t time = ldq(a1);

    if (time == -1ULL) {
        return;
    }

    /* nanoseconds */
    time = (time * 125) >> 9;

    qemu_mod_timer(env->cpu_timer, qemu_get_clock_ns(vm_clock) + time);
}

/* Store CPU Timer */
void HELPER(stpt)(uint64_t a1)
{
    /* XXX implement */
    stq(a1, 0);
}

/* Store System Information */
uint32_t HELPER(stsi)(uint64_t a0, uint32_t r0, uint32_t r1)
{
    int cc = 0;
    int sel1, sel2;

    if ((r0 & STSI_LEVEL_MASK) <= STSI_LEVEL_3 &&
        ((r0 & STSI_R0_RESERVED_MASK) || (r1 & STSI_R1_RESERVED_MASK))) {
        /* valid function code, invalid reserved bits */
        program_interrupt(env, PGM_SPECIFICATION, 2);
    }

    sel1 = r0 & STSI_R0_SEL1_MASK;
    sel2 = r1 & STSI_R1_SEL2_MASK;

    /* XXX: spec exception if sysib is not 4k-aligned */

    switch (r0 & STSI_LEVEL_MASK) {
    case STSI_LEVEL_1:
        if ((sel1 == 1) && (sel2 == 1)) {
            /* Basic Machine Configuration */
            struct sysib_111 sysib;

            memset(&sysib, 0, sizeof(sysib));
            ebcdic_put(sysib.manuf, "QEMU            ", 16);
            /* same as machine type number in STORE CPU ID */
            ebcdic_put(sysib.type, "QEMU", 4);
            /* same as model number in STORE CPU ID */
            ebcdic_put(sysib.model, "QEMU            ", 16);
            ebcdic_put(sysib.sequence, "QEMU            ", 16);
            ebcdic_put(sysib.plant, "QEMU", 4);
            cpu_physical_memory_rw(a0, (uint8_t *)&sysib, sizeof(sysib), 1);
        } else if ((sel1 == 2) && (sel2 == 1)) {
            /* Basic Machine CPU */
            struct sysib_121 sysib;

            memset(&sysib, 0, sizeof(sysib));
            /* XXX make different for different CPUs? */
            ebcdic_put(sysib.sequence, "QEMUQEMUQEMUQEMU", 16);
            ebcdic_put(sysib.plant, "QEMU", 4);
            stw_p(&sysib.cpu_addr, env->cpu_num);
            cpu_physical_memory_rw(a0, (uint8_t *)&sysib, sizeof(sysib), 1);
        } else if ((sel1 == 2) && (sel2 == 2)) {
            /* Basic Machine CPUs */
            struct sysib_122 sysib;

            memset(&sysib, 0, sizeof(sysib));
            stl_p(&sysib.capability, 0x443afc29);
            /* XXX change when SMP comes */
            stw_p(&sysib.total_cpus, 1);
            stw_p(&sysib.active_cpus, 1);
            stw_p(&sysib.standby_cpus, 0);
            stw_p(&sysib.reserved_cpus, 0);
            cpu_physical_memory_rw(a0, (uint8_t *)&sysib, sizeof(sysib), 1);
        } else {
            cc = 3;
        }
        break;
    case STSI_LEVEL_2:
        {
            if ((sel1 == 2) && (sel2 == 1)) {
                /* LPAR CPU */
                struct sysib_221 sysib;

                memset(&sysib, 0, sizeof(sysib));
                /* XXX make different for different CPUs? */
                ebcdic_put(sysib.sequence, "QEMUQEMUQEMUQEMU", 16);
                ebcdic_put(sysib.plant, "QEMU", 4);
                stw_p(&sysib.cpu_addr, env->cpu_num);
                stw_p(&sysib.cpu_id, 0);
                cpu_physical_memory_rw(a0, (uint8_t *)&sysib, sizeof(sysib), 1);
            } else if ((sel1 == 2) && (sel2 == 2)) {
                /* LPAR CPUs */
                struct sysib_222 sysib;

                memset(&sysib, 0, sizeof(sysib));
                stw_p(&sysib.lpar_num, 0);
                sysib.lcpuc = 0;
                /* XXX change when SMP comes */
                stw_p(&sysib.total_cpus, 1);
                stw_p(&sysib.conf_cpus, 1);
                stw_p(&sysib.standby_cpus, 0);
                stw_p(&sysib.reserved_cpus, 0);
                ebcdic_put(sysib.name, "QEMU    ", 8);
                stl_p(&sysib.caf, 1000);
                stw_p(&sysib.dedicated_cpus, 0);
                stw_p(&sysib.shared_cpus, 0);
                cpu_physical_memory_rw(a0, (uint8_t *)&sysib, sizeof(sysib), 1);
            } else {
                cc = 3;
            }
            break;
        }
    case STSI_LEVEL_3:
        {
            if ((sel1 == 2) && (sel2 == 2)) {
                /* VM CPUs */
                struct sysib_322 sysib;

                memset(&sysib, 0, sizeof(sysib));
                sysib.count = 1;
                /* XXX change when SMP comes */
                stw_p(&sysib.vm[0].total_cpus, 1);
                stw_p(&sysib.vm[0].conf_cpus, 1);
                stw_p(&sysib.vm[0].standby_cpus, 0);
                stw_p(&sysib.vm[0].reserved_cpus, 0);
                ebcdic_put(sysib.vm[0].name, "KVMguest", 8);
                stl_p(&sysib.vm[0].caf, 1000);
                ebcdic_put(sysib.vm[0].cpi, "KVM/Linux       ", 16);
                cpu_physical_memory_rw(a0, (uint8_t *)&sysib, sizeof(sysib), 1);
            } else {
                cc = 3;
            }
            break;
        }
    case STSI_LEVEL_CURRENT:
        env->regs[0] = STSI_LEVEL_3;
        break;
    default:
        cc = 3;
        break;
    }

    return cc;
}

void HELPER(lctlg)(uint32_t r1, uint64_t a2, uint32_t r3)
{
    int i;
    uint64_t src = a2;

    for (i = r1;; i = (i + 1) % 16) {
        env->cregs[i] = ldq(src);
        HELPER_LOG("load ctl %d from 0x%" PRIx64 " == 0x%" PRIx64 "\n",
                   i, src, env->cregs[i]);
        src += sizeof(uint64_t);

        if (i == r3) {
            break;
        }
    }

    tlb_flush(env, 1);
}

void HELPER(lctl)(uint32_t r1, uint64_t a2, uint32_t r3)
{
    int i;
    uint64_t src = a2;

    for (i = r1;; i = (i + 1) % 16) {
        env->cregs[i] = (env->cregs[i] & 0xFFFFFFFF00000000ULL) | ldl(src);
        src += sizeof(uint32_t);

        if (i == r3) {
            break;
        }
    }

    tlb_flush(env, 1);
}

void HELPER(stctg)(uint32_t r1, uint64_t a2, uint32_t r3)
{
    int i;
    uint64_t dest = a2;

    for (i = r1;; i = (i + 1) % 16) {
        stq(dest, env->cregs[i]);
        dest += sizeof(uint64_t);

        if (i == r3) {
            break;
        }
    }
}

void HELPER(stctl)(uint32_t r1, uint64_t a2, uint32_t r3)
{
    int i;
    uint64_t dest = a2;

    for (i = r1;; i = (i + 1) % 16) {
        stl(dest, env->cregs[i]);
        dest += sizeof(uint32_t);

        if (i == r3) {
            break;
        }
    }
}

uint32_t HELPER(tprot)(uint64_t a1, uint64_t a2)
{
    /* XXX implement */

    return 0;
}

/* insert storage key extended */
uint64_t HELPER(iske)(uint64_t r2)
{
    uint64_t addr = get_address(0, 0, r2);

    if (addr > ram_size) {
        return 0;
    }

    return env->storage_keys[addr / TARGET_PAGE_SIZE];
}

/* set storage key extended */
void HELPER(sske)(uint32_t r1, uint64_t r2)
{
    uint64_t addr = get_address(0, 0, r2);

    if (addr > ram_size) {
        return;
    }

    env->storage_keys[addr / TARGET_PAGE_SIZE] = r1;
}

/* reset reference bit extended */
uint32_t HELPER(rrbe)(uint32_t r1, uint64_t r2)
{
    uint8_t re;
    uint8_t key;

    if (r2 > ram_size) {
        return 0;
    }

    key = env->storage_keys[r2 / TARGET_PAGE_SIZE];
    re = key & (SK_R | SK_C);
    env->storage_keys[r2 / TARGET_PAGE_SIZE] = (key & ~SK_R);

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

/* compare and swap and purge */
uint32_t HELPER(csp)(uint32_t r1, uint32_t r2)
{
    uint32_t cc;
    uint32_t o1 = env->regs[r1];
    uint64_t a2 = get_address_31fix(r2) & ~3ULL;
    uint32_t o2 = ldl(a2);

    if (o1 == o2) {
        stl(a2, env->regs[(r1 + 1) & 15]);
        if (env->regs[r2] & 0x3) {
            /* flush TLB / ALB */
            tlb_flush(env, 1);
        }
        cc = 0;
    } else {
        env->regs[r1] = (env->regs[r1] & 0xffffffff00000000ULL) | o2;
        cc = 1;
    }

    return cc;
}

static uint32_t mvc_asc(int64_t l, uint64_t a1, uint64_t mode1, uint64_t a2,
                        uint64_t mode2)
{
    target_ulong src, dest;
    int flags, cc = 0, i;

    if (!l) {
        return 0;
    } else if (l > 256) {
        /* max 256 */
        l = 256;
        cc = 3;
    }

    if (mmu_translate(env, a1 & TARGET_PAGE_MASK, 1, mode1, &dest, &flags)) {
        cpu_loop_exit(env);
    }
    dest |= a1 & ~TARGET_PAGE_MASK;

    if (mmu_translate(env, a2 & TARGET_PAGE_MASK, 0, mode2, &src, &flags)) {
        cpu_loop_exit(env);
    }
    src |= a2 & ~TARGET_PAGE_MASK;

    /* XXX replace w/ memcpy */
    for (i = 0; i < l; i++) {
        /* XXX be more clever */
        if ((((dest + i) & TARGET_PAGE_MASK) != (dest & TARGET_PAGE_MASK)) ||
            (((src + i) & TARGET_PAGE_MASK) != (src & TARGET_PAGE_MASK))) {
            mvc_asc(l - i, a1 + i, mode1, a2 + i, mode2);
            break;
        }
        stb_phys(dest + i, ldub_phys(src + i));
    }

    return cc;
}

uint32_t HELPER(mvcs)(uint64_t l, uint64_t a1, uint64_t a2)
{
    HELPER_LOG("%s: %16" PRIx64 " %16" PRIx64 " %16" PRIx64 "\n",
               __func__, l, a1, a2);

    return mvc_asc(l, a1, PSW_ASC_SECONDARY, a2, PSW_ASC_PRIMARY);
}

uint32_t HELPER(mvcp)(uint64_t l, uint64_t a1, uint64_t a2)
{
    HELPER_LOG("%s: %16" PRIx64 " %16" PRIx64 " %16" PRIx64 "\n",
               __func__, l, a1, a2);

    return mvc_asc(l, a1, PSW_ASC_PRIMARY, a2, PSW_ASC_SECONDARY);
}

uint32_t HELPER(sigp)(uint64_t order_code, uint32_t r1, uint64_t cpu_addr)
{
    int cc = 0;

    HELPER_LOG("%s: %016" PRIx64 " %08x %016" PRIx64 "\n",
               __func__, order_code, r1, cpu_addr);

    /* Remember: Use "R1 or R1 + 1, whichever is the odd-numbered register"
       as parameter (input). Status (output) is always R1. */

    switch (order_code) {
    case SIGP_SET_ARCH:
        /* switch arch */
        break;
    case SIGP_SENSE:
        /* enumerate CPU status */
        if (cpu_addr) {
            /* XXX implement when SMP comes */
            return 3;
        }
        env->regs[r1] &= 0xffffffff00000000ULL;
        cc = 1;
        break;
#if !defined(CONFIG_USER_ONLY)
    case SIGP_RESTART:
        qemu_system_reset_request();
        cpu_loop_exit(env);
        break;
    case SIGP_STOP:
        qemu_system_shutdown_request();
        cpu_loop_exit(env);
        break;
#endif
    default:
        /* unknown sigp */
        fprintf(stderr, "XXX unknown sigp: 0x%" PRIx64 "\n", order_code);
        cc = 3;
    }

    return cc;
}

/* invalidate pte */
void HELPER(ipte)(uint64_t pte_addr, uint64_t vaddr)
{
    uint64_t page = vaddr & TARGET_PAGE_MASK;
    uint64_t pte = 0;

    /* XXX broadcast to other CPUs */

    /* XXX Linux is nice enough to give us the exact pte address.
       According to spec we'd have to find it out ourselves */
    /* XXX Linux is fine with overwriting the pte, the spec requires
       us to only set the invalid bit */
    stq_phys(pte_addr, pte | _PAGE_INVALID);

    /* XXX we exploit the fact that Linux passes the exact virtual
       address here - it's not obliged to! */
    tlb_flush_page(env, page);

    /* XXX 31-bit hack */
    if (page & 0x80000000) {
        tlb_flush_page(env, page & ~0x80000000);
    } else {
        tlb_flush_page(env, page | 0x80000000);
    }
}

/* flush local tlb */
void HELPER(ptlb)(void)
{
    tlb_flush(env, 1);
}

/* store using real address */
void HELPER(stura)(uint64_t addr, uint32_t v1)
{
    stw_phys(get_address(0, 0, addr), v1);
}

/* load real address */
uint32_t HELPER(lra)(uint64_t addr, uint32_t r1)
{
    uint32_t cc = 0;
    int old_exc = env->exception_index;
    uint64_t asc = env->psw.mask & PSW_MASK_ASC;
    uint64_t ret;
    int flags;

    /* XXX incomplete - has more corner cases */
    if (!(env->psw.mask & PSW_MASK_64) && (addr >> 32)) {
        program_interrupt(env, PGM_SPECIAL_OP, 2);
    }

    env->exception_index = old_exc;
    if (mmu_translate(env, addr, 0, asc, &ret, &flags)) {
        cc = 3;
    }
    if (env->exception_index == EXCP_PGM) {
        ret = env->int_pgm_code | 0x80000000;
    } else {
        ret |= addr & ~TARGET_PAGE_MASK;
    }
    env->exception_index = old_exc;

    if (!(env->psw.mask & PSW_MASK_64)) {
        env->regs[r1] = (env->regs[r1] & 0xffffffff00000000ULL) |
            (ret & 0xffffffffULL);
    } else {
        env->regs[r1] = ret;
    }

    return cc;
}

#endif
