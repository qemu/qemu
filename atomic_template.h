/*
 * Atomic helper templates
 * Included from tcg-runtime.c and cputlb.c.
 *
 * Copyright (c) 2016 Red Hat, Inc
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

#if DATA_SIZE == 16
# define SUFFIX     o
# define DATA_TYPE  Int128
# define BSWAP      bswap128
#elif DATA_SIZE == 8
# define SUFFIX     q
# define DATA_TYPE  uint64_t
# define BSWAP      bswap64
#elif DATA_SIZE == 4
# define SUFFIX     l
# define DATA_TYPE  uint32_t
# define BSWAP      bswap32
#elif DATA_SIZE == 2
# define SUFFIX     w
# define DATA_TYPE  uint16_t
# define BSWAP      bswap16
#elif DATA_SIZE == 1
# define SUFFIX     b
# define DATA_TYPE  uint8_t
# define BSWAP
#else
# error unsupported data size
#endif

#if DATA_SIZE >= 4
# define ABI_TYPE  DATA_TYPE
#else
# define ABI_TYPE  uint32_t
#endif

/* Define host-endian atomic operations.  Note that END is used within
   the ATOMIC_NAME macro, and redefined below.  */
#if DATA_SIZE == 1
# define END
#elif defined(HOST_WORDS_BIGENDIAN)
# define END  _be
#else
# define END  _le
#endif

ABI_TYPE ATOMIC_NAME(cmpxchg)(CPUArchState *env, target_ulong addr,
                              ABI_TYPE cmpv, ABI_TYPE newv EXTRA_ARGS)
{
    DATA_TYPE *haddr = ATOMIC_MMU_LOOKUP;
    return atomic_cmpxchg__nocheck(haddr, cmpv, newv);
}

#if DATA_SIZE >= 16
ABI_TYPE ATOMIC_NAME(ld)(CPUArchState *env, target_ulong addr EXTRA_ARGS)
{
    DATA_TYPE val, *haddr = ATOMIC_MMU_LOOKUP;
    __atomic_load(haddr, &val, __ATOMIC_RELAXED);
    return val;
}

void ATOMIC_NAME(st)(CPUArchState *env, target_ulong addr,
                     ABI_TYPE val EXTRA_ARGS)
{
    DATA_TYPE *haddr = ATOMIC_MMU_LOOKUP;
    __atomic_store(haddr, &val, __ATOMIC_RELAXED);
}
#else
ABI_TYPE ATOMIC_NAME(xchg)(CPUArchState *env, target_ulong addr,
                           ABI_TYPE val EXTRA_ARGS)
{
    DATA_TYPE *haddr = ATOMIC_MMU_LOOKUP;
    return atomic_xchg__nocheck(haddr, val);
}

#define GEN_ATOMIC_HELPER(X)                                        \
ABI_TYPE ATOMIC_NAME(X)(CPUArchState *env, target_ulong addr,       \
                 ABI_TYPE val EXTRA_ARGS)                           \
{                                                                   \
    DATA_TYPE *haddr = ATOMIC_MMU_LOOKUP;                           \
    return atomic_##X(haddr, val);                                  \
}                                                                   \

GEN_ATOMIC_HELPER(fetch_add)
GEN_ATOMIC_HELPER(fetch_and)
GEN_ATOMIC_HELPER(fetch_or)
GEN_ATOMIC_HELPER(fetch_xor)
GEN_ATOMIC_HELPER(add_fetch)
GEN_ATOMIC_HELPER(and_fetch)
GEN_ATOMIC_HELPER(or_fetch)
GEN_ATOMIC_HELPER(xor_fetch)

#undef GEN_ATOMIC_HELPER
#endif /* DATA SIZE >= 16 */

#undef END

#if DATA_SIZE > 1

/* Define reverse-host-endian atomic operations.  Note that END is used
   within the ATOMIC_NAME macro.  */
#ifdef HOST_WORDS_BIGENDIAN
# define END  _le
#else
# define END  _be
#endif

ABI_TYPE ATOMIC_NAME(cmpxchg)(CPUArchState *env, target_ulong addr,
                              ABI_TYPE cmpv, ABI_TYPE newv EXTRA_ARGS)
{
    DATA_TYPE *haddr = ATOMIC_MMU_LOOKUP;
    return BSWAP(atomic_cmpxchg__nocheck(haddr, BSWAP(cmpv), BSWAP(newv)));
}

#if DATA_SIZE >= 16
ABI_TYPE ATOMIC_NAME(ld)(CPUArchState *env, target_ulong addr EXTRA_ARGS)
{
    DATA_TYPE val, *haddr = ATOMIC_MMU_LOOKUP;
    __atomic_load(haddr, &val, __ATOMIC_RELAXED);
    return BSWAP(val);
}

void ATOMIC_NAME(st)(CPUArchState *env, target_ulong addr,
                     ABI_TYPE val EXTRA_ARGS)
{
    DATA_TYPE *haddr = ATOMIC_MMU_LOOKUP;
    val = BSWAP(val);
    __atomic_store(haddr, &val, __ATOMIC_RELAXED);
}
#else
ABI_TYPE ATOMIC_NAME(xchg)(CPUArchState *env, target_ulong addr,
                           ABI_TYPE val EXTRA_ARGS)
{
    DATA_TYPE *haddr = ATOMIC_MMU_LOOKUP;
    return BSWAP(atomic_xchg__nocheck(haddr, BSWAP(val)));
}

#define GEN_ATOMIC_HELPER(X)                                        \
ABI_TYPE ATOMIC_NAME(X)(CPUArchState *env, target_ulong addr,       \
                 ABI_TYPE val EXTRA_ARGS)                           \
{                                                                   \
    DATA_TYPE *haddr = ATOMIC_MMU_LOOKUP;                           \
    return BSWAP(atomic_##X(haddr, BSWAP(val)));                    \
}

GEN_ATOMIC_HELPER(fetch_and)
GEN_ATOMIC_HELPER(fetch_or)
GEN_ATOMIC_HELPER(fetch_xor)
GEN_ATOMIC_HELPER(and_fetch)
GEN_ATOMIC_HELPER(or_fetch)
GEN_ATOMIC_HELPER(xor_fetch)

#undef GEN_ATOMIC_HELPER

/* Note that for addition, we need to use a separate cmpxchg loop instead
   of bswaps for the reverse-host-endian helpers.  */
ABI_TYPE ATOMIC_NAME(fetch_add)(CPUArchState *env, target_ulong addr,
                         ABI_TYPE val EXTRA_ARGS)
{
    DATA_TYPE *haddr = ATOMIC_MMU_LOOKUP;
    DATA_TYPE ldo, ldn, ret, sto;

    ldo = atomic_read__nocheck(haddr);
    while (1) {
        ret = BSWAP(ldo);
        sto = BSWAP(ret + val);
        ldn = atomic_cmpxchg__nocheck(haddr, ldo, sto);
        if (ldn == ldo) {
            return ret;
        }
        ldo = ldn;
    }
}

ABI_TYPE ATOMIC_NAME(add_fetch)(CPUArchState *env, target_ulong addr,
                         ABI_TYPE val EXTRA_ARGS)
{
    DATA_TYPE *haddr = ATOMIC_MMU_LOOKUP;
    DATA_TYPE ldo, ldn, ret, sto;

    ldo = atomic_read__nocheck(haddr);
    while (1) {
        ret = BSWAP(ldo) + val;
        sto = BSWAP(ret);
        ldn = atomic_cmpxchg__nocheck(haddr, ldo, sto);
        if (ldn == ldo) {
            return ret;
        }
        ldo = ldn;
    }
}
#endif /* DATA_SIZE >= 16 */

#undef END
#endif /* DATA_SIZE > 1 */

#undef BSWAP
#undef ABI_TYPE
#undef DATA_TYPE
#undef SUFFIX
#undef DATA_SIZE
