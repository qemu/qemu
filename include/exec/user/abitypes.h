#ifndef QEMU_TYPES_H
#define QEMU_TYPES_H
#include "cpu.h"

#ifdef TARGET_ABI32
#define TARGET_ABI_BITS 32
#else
#define TARGET_ABI_BITS TARGET_LONG_BITS
#endif

#ifdef TARGET_M68K
#define ABI_INT_ALIGNMENT 2
#define ABI_LONG_ALIGNMENT 2
#define ABI_LLONG_ALIGNMENT 2
#endif

#ifndef ABI_SHORT_ALIGNMENT
#define ABI_SHORT_ALIGNMENT 2
#endif
#ifndef ABI_INT_ALIGNMENT
#define ABI_INT_ALIGNMENT 4
#endif
#ifndef ABI_LONG_ALIGNMENT
#define ABI_LONG_ALIGNMENT (TARGET_ABI_BITS / 8)
#endif
#ifndef ABI_LLONG_ALIGNMENT
#define ABI_LLONG_ALIGNMENT 8
#endif

typedef int16_t abi_short __attribute__ ((aligned(ABI_SHORT_ALIGNMENT)));
typedef uint16_t abi_ushort __attribute__((aligned(ABI_SHORT_ALIGNMENT)));
typedef int32_t abi_int __attribute__((aligned(ABI_INT_ALIGNMENT)));
typedef uint32_t abi_uint __attribute__((aligned(ABI_INT_ALIGNMENT)));
typedef int64_t abi_llong __attribute__((aligned(ABI_LLONG_ALIGNMENT)));
typedef uint64_t abi_ullong __attribute__((aligned(ABI_LLONG_ALIGNMENT)));

#ifdef TARGET_ABI32
typedef uint32_t abi_ulong __attribute__((aligned(ABI_LONG_ALIGNMENT)));
typedef int32_t abi_long __attribute__((aligned(ABI_LONG_ALIGNMENT)));
#define TARGET_ABI_FMT_lx "%08x"
#define TARGET_ABI_FMT_ld "%d"
#define TARGET_ABI_FMT_lu "%u"

static inline abi_ulong tswapal(abi_ulong v)
{
    return tswap32(v);
}

#else
typedef target_ulong abi_ulong __attribute__((aligned(ABI_LONG_ALIGNMENT)));
typedef target_long abi_long __attribute__((aligned(ABI_LONG_ALIGNMENT)));
#define TARGET_ABI_FMT_lx TARGET_FMT_lx
#define TARGET_ABI_FMT_ld TARGET_FMT_ld
#define TARGET_ABI_FMT_lu TARGET_FMT_lu
/* for consistency, define ABI32 too */
#if TARGET_ABI_BITS == 32
#define TARGET_ABI32 1
#endif

static inline abi_ulong tswapal(abi_ulong v)
{
    return tswapl(v);
}

#endif
#endif
