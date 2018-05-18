/*
 * AArch64 SVE translation
 *
 * Copyright (c) 2018 Linaro, Ltd
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg-op.h"
#include "tcg-op-gvec.h"
#include "qemu/log.h"
#include "arm_ldst.h"
#include "translate.h"
#include "internals.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"
#include "trace-tcg.h"
#include "translate-a64.h"

/*
 * Include the generated decoder.
 */

#include "decode-sve.inc.c"

/*
 * Implement all of the translator functions referenced by the decoder.
 */

static bool trans_AND_zzz(DisasContext *s, arg_AND_zzz *a, uint32_t insn)
{
    return false;
}

static bool trans_ORR_zzz(DisasContext *s, arg_ORR_zzz *a, uint32_t insn)
{
    return false;
}

static bool trans_EOR_zzz(DisasContext *s, arg_EOR_zzz *a, uint32_t insn)
{
    return false;
}

static bool trans_BIC_zzz(DisasContext *s, arg_BIC_zzz *a, uint32_t insn)
{
    return false;
}
