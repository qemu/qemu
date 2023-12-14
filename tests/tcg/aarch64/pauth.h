/*
 * Helper for pauth test case
 *
 * Copyright (c) 2023 Linaro Ltd
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <sys/auxv.h>

static int get_pac_feature(void)
{
    unsigned long isar1, isar2;

    assert(getauxval(AT_HWCAP) & HWCAP_CPUID);

    asm("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
    asm("mrs %0, S3_0_C0_C6_2" : "=r"(isar2));     /* id_aa64isar2_el1 */

    return ((isar1 >> 4) & 0xf)   /* APA */
         | ((isar1 >> 8) & 0xf)   /* API */
         | ((isar2 >> 12) & 0xf); /* APA3 */
}
