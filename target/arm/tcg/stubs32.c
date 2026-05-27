/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "target/arm/internals.h"
#include "target/arm/tcg/translate.h"


void gen_a64_update_pc(DisasContext *s, int64_t diff)
{
    g_assert_not_reached();
}

void a64_translate_init(void)
{
    /* Don't initialize for 32 bits. Call site will be fixed later. */
}

void aarch64_translate_code(CPUState *cs, TranslationBlock *tb,
                            int *max_insns, vaddr pc, void *host_pc)
{
    g_assert_not_reached();
}

void aarch64_host_initfn(Object *obj)
{
    g_assert_not_reached();
}

void aarch64_max_tcg_initfn(Object *obj)
{
    g_assert_not_reached();
}
