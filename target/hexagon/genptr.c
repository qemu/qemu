/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#define QEMU_GENERATE
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "internal.h"
#include "tcg/tcg-op.h"
#include "insn.h"
#include "opcodes.h"
#include "translate.h"
#include "macros.h"
#include "mmvec/macros.h"
#include "genptr_helpers.h"
#include "helper_overrides.h"

#include "qemu_wrap_generated.h"

#define DEF_QEMU(TAG, SHORTCODE, HELPER, GENFN, HELPFN) \
static void generate_##TAG(CPUHexagonState *env, DisasContext *ctx, \
                           insn_t *insn) \
{ \
    GENFN \
}
#include "qemu_def_generated.h"
#undef DEF_QEMU


/* Fill in the table with NULLs because not all the opcodes have DEF_QEMU */
semantic_insn_t opcode_genptr[] = {
#define OPCODE(X)                              NULL
#include "opcodes_def_generated.h"
    NULL
#undef OPCODE
};

/* This function overwrites the NULL entries where we have a DEF_QEMU */
void init_genptr(void)
{
#define DEF_QEMU(TAG, SHORTCODE, HELPER, GENFN, HELPFN) \
    opcode_genptr[TAG] = generate_##TAG;
#include "qemu_def_generated.h"
#undef DEF_QEMU
}


