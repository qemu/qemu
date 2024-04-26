/*
 * QEMU abi_ptr type definitions
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef EXEC_ABI_PTR_H
#define EXEC_ABI_PTR_H

#include "cpu-param.h"

#if defined(CONFIG_USER_ONLY)
/*
 * sparc32plus has 64bit long but 32bit space address
 * this can make bad result with g2h() and h2g()
 */
#if TARGET_VIRT_ADDR_SPACE_BITS <= 32
typedef uint32_t abi_ptr;
#define TARGET_ABI_FMT_ptr "%x"
#else
typedef uint64_t abi_ptr;
#define TARGET_ABI_FMT_ptr "%"PRIx64
#endif

#else /* !CONFIG_USER_ONLY */

#include "exec/target_long.h"

typedef target_ulong abi_ptr;
#define TARGET_ABI_FMT_ptr TARGET_FMT_lx

#endif /* !CONFIG_USER_ONLY */

#endif
