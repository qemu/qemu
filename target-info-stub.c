/*
 * QEMU target info stubs (target specific)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/target-info.h"
#include "qemu/target-info-impl.h"
#include "hw/core/boards.h"
#include "cpu.h"
#include "exec/page-vary.h"

/* Validate correct placement of CPUArchState. */
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, parent_obj) != 0);
QEMU_BUILD_BUG_ON(offsetof(ArchCPU, env) != sizeof(CPUState));

/* Validate target page size, if invariant. */
#ifndef TARGET_PAGE_BITS_VARY
QEMU_BUILD_BUG_ON(TARGET_PAGE_BITS < TARGET_PAGE_BITS_MIN);
#endif

static const TargetInfo target_info_stub = {
    .target_name = TARGET_NAME,
    .target_arch = glue(SYS_EMU_TARGET_, TARGET_ARCH),
    .long_bits = TARGET_LONG_BITS,
    .cpu_type = CPU_RESOLVING_TYPE,
    .machine_typename = TYPE_MACHINE,
    .endianness = TARGET_BIG_ENDIAN ? ENDIAN_MODE_BIG : ENDIAN_MODE_LITTLE,
#ifdef TARGET_PAGE_BITS_VARY
    .page_bits_vary = true,
# ifdef TARGET_PAGE_BITS_LEGACY
    .page_bits_init = TARGET_PAGE_BITS_LEGACY,
# endif
#else
    .page_bits_vary = false,
    .page_bits_init = TARGET_PAGE_BITS,
#endif
};

const TargetInfo *target_info(void)
{
    return &target_info_stub;
}
