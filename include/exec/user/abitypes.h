#ifndef QEMU_TYPES_H
#define QEMU_TYPES_H
#include "cpu.h"

#ifdef TARGET_M68K
#define ABI_INT_ALIGNMENT 2
#endif

#ifndef ABI_SHORT_ALIGNMENT
#define ABI_SHORT_ALIGNMENT 2
#endif
#ifndef ABI_INT_ALIGNMENT
#define ABI_INT_ALIGNMENT 4
#endif

typedef int16_t abi_short __attribute__ ((aligned(ABI_SHORT_ALIGNMENT)));
typedef uint16_t abi_ushort __attribute__((aligned(ABI_SHORT_ALIGNMENT)));
typedef int32_t abi_int __attribute__((aligned(ABI_INT_ALIGNMENT)));
typedef uint32_t abi_uint __attribute__((aligned(ABI_INT_ALIGNMENT)));

#ifdef TARGET_ABI32
typedef uint32_t abi_ulong;
typedef int32_t abi_long;
#define TARGET_ABI_FMT_lx "%08x"
#define TARGET_ABI_FMT_ld "%d"
#define TARGET_ABI_FMT_lu "%u"
#define TARGET_ABI_BITS 32

static inline abi_ulong tswapal(abi_ulong v)
{
    return tswap32(v);
}

#else
typedef target_ulong abi_ulong;
typedef target_long abi_long;
#define TARGET_ABI_FMT_lx TARGET_FMT_lx
#define TARGET_ABI_FMT_ld TARGET_FMT_ld
#define TARGET_ABI_FMT_lu TARGET_FMT_lu
#define TARGET_ABI_BITS TARGET_LONG_BITS
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
