/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 *
 * Based on include/sbi/{fw_dynamic.h,sbi_scratch.h} from the OpenSBI project.
 */

#ifndef RISCV_BOOT_OPENSBI_H
#define RISCV_BOOT_OPENSBI_H

#include "exec/cpu-defs.h"

/** Expected value of info magic ('OSBI' ascii string in hex) */
#define FW_DYNAMIC_INFO_MAGIC_VALUE     0x4942534f

/** Maximum supported info version */
#define FW_DYNAMIC_INFO_VERSION         0x2

/** Possible next mode values */
#define FW_DYNAMIC_INFO_NEXT_MODE_U     0x0
#define FW_DYNAMIC_INFO_NEXT_MODE_S     0x1
#define FW_DYNAMIC_INFO_NEXT_MODE_M     0x3

enum sbi_scratch_options {
    /** Disable prints during boot */
    SBI_SCRATCH_NO_BOOT_PRINTS = (1 << 0),
    /** Enable runtime debug prints */
    SBI_SCRATCH_DEBUG_PRINTS = (1 << 1),
};

/** Representation dynamic info passed by previous booting stage */
struct fw_dynamic_info64 {
    /** Info magic */
    int64_t magic;
    /** Info version */
    int64_t version;
    /** Next booting stage address */
    int64_t next_addr;
    /** Next booting stage mode */
    int64_t next_mode;
    /** Options for OpenSBI library */
    int64_t options;
    /**
     * Preferred boot HART id
     *
     * It is possible that the previous booting stage uses same link
     * address as the FW_DYNAMIC firmware. In this case, the relocation
     * lottery mechanism can potentially overwrite the previous booting
     * stage while other HARTs are still running in the previous booting
     * stage leading to boot-time crash. To avoid this boot-time crash,
     * the previous booting stage can specify last HART that will jump
     * to the FW_DYNAMIC firmware as the preferred boot HART.
     *
     * To avoid specifying a preferred boot HART, the previous booting
     * stage can set it to -1UL which will force the FW_DYNAMIC firmware
     * to use the relocation lottery mechanism.
     */
    int64_t boot_hart;
};

/** Representation dynamic info passed by previous booting stage */
struct fw_dynamic_info32 {
    /** Info magic */
    int32_t magic;
    /** Info version */
    int32_t version;
    /** Next booting stage address */
    int32_t next_addr;
    /** Next booting stage mode */
    int32_t next_mode;
    /** Options for OpenSBI library */
    int32_t options;
    /**
     * Preferred boot HART id
     *
     * It is possible that the previous booting stage uses same link
     * address as the FW_DYNAMIC firmware. In this case, the relocation
     * lottery mechanism can potentially overwrite the previous booting
     * stage while other HARTs are still running in the previous booting
     * stage leading to boot-time crash. To avoid this boot-time crash,
     * the previous booting stage can specify last HART that will jump
     * to the FW_DYNAMIC firmware as the preferred boot HART.
     *
     * To avoid specifying a preferred boot HART, the previous booting
     * stage can set it to -1UL which will force the FW_DYNAMIC firmware
     * to use the relocation lottery mechanism.
     */
    int32_t boot_hart;
};
#endif
