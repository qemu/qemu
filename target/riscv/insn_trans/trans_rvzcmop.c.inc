/*
 * RISC-V translation routines for compressed May-Be-Operation(zcmop).
 *
 * Copyright (c) 2024 Alibaba Group.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define REQUIRE_ZCMOP(ctx) do {           \
    if (!ctx->cfg_ptr->ext_zcmop) {       \
        return false;                     \
    }                                     \
} while (0)

static bool trans_c_mop_n(DisasContext *ctx, arg_c_mop_n *a)
{
    REQUIRE_ZCMOP(ctx);
    return true;
}
