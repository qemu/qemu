/*
 * FEAT_XS Test
 *
 * Copyright (c) 2024 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <minilib.h>
#include <stdint.h>

int main(void)
{
    uint64_t isar1;

    asm volatile ("mrs %0, id_aa64isar1_el1" : "=r"(isar1));
    if (((isar1 >> 56) & 0xf) < 1) {
        ml_printf("FEAT_XS not supported by CPU");
        return 1;
    }
    /* VMALLE1NXS */
    asm volatile (".inst 0xd508971f");
    /* VMALLE1OSNXS */
    asm volatile (".inst 0xd508911f");

    return 0;
}
