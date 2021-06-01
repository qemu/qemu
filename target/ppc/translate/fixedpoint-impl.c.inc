/*
 * Power ISA decode for Fixed-Point Facility instructions
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

/*
 * Incorporate CIA into the constant when R=1.
 * Validate that when R=1, RA=0.
 */
static bool resolve_PLS_D(DisasContext *ctx, arg_D *d, arg_PLS_D *a)
{
    d->rt = a->rt;
    d->ra = a->ra;
    d->si = a->si;
    if (a->r) {
        if (unlikely(a->ra != 0)) {
            gen_invalid(ctx);
            return false;
        }
        d->si += ctx->cia;
    }
    return true;
}

static bool trans_ADDI(DisasContext *ctx, arg_D *a)
{
    if (a->ra) {
        tcg_gen_addi_tl(cpu_gpr[a->rt], cpu_gpr[a->ra], a->si);
    } else {
        tcg_gen_movi_tl(cpu_gpr[a->rt], a->si);
    }
    return true;
}

static bool trans_PADDI(DisasContext *ctx, arg_PLS_D *a)
{
    arg_D d;
    if (!resolve_PLS_D(ctx, &d, a)) {
        return true;
    }
    return trans_ADDI(ctx, &d);
}

static bool trans_ADDIS(DisasContext *ctx, arg_D *a)
{
    a->si <<= 16;
    return trans_ADDI(ctx, a);
}

static bool trans_INVALID(DisasContext *ctx, arg_INVALID *a)
{
    gen_invalid(ctx);
    return true;
}

static bool trans_PNOP(DisasContext *ctx, arg_PNOP *a)
{
    return true;
}
