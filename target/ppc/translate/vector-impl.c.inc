/*
 * Power ISA decode for Vector Facility instructions
 *
 * Copyright (c) 2021 Instituto de Pesquisas Eldorado (eldorado.org.br)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#define REQUIRE_ALTIVEC(CTX) \
    do {                                                \
        if (unlikely(!(CTX)->altivec_enabled)) {        \
            gen_exception((CTX), POWERPC_EXCP_VPU);     \
            return true;                                \
        }                                               \
    } while (0)

static bool trans_VCFUGED(DisasContext *ctx, arg_VX *a)
{
    TCGv_i64 tgt, src, mask;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_ALTIVEC(ctx);

    tgt = tcg_temp_new_i64();
    src = tcg_temp_new_i64();
    mask = tcg_temp_new_i64();

    /* centrifuge lower double word */
    get_cpu_vsrl(src, a->vra + 32);
    get_cpu_vsrl(mask, a->vrb + 32);
    gen_helper_cfuged(tgt, src, mask);
    set_cpu_vsrl(a->vrt + 32, tgt);

    /* centrifuge higher double word */
    get_cpu_vsrh(src, a->vra + 32);
    get_cpu_vsrh(mask, a->vrb + 32);
    gen_helper_cfuged(tgt, src, mask);
    set_cpu_vsrh(a->vrt + 32, tgt);

    tcg_temp_free_i64(tgt);
    tcg_temp_free_i64(src);
    tcg_temp_free_i64(mask);

    return true;
}
