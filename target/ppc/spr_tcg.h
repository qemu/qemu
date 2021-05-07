/*
 *  PowerPC emulation for qemu: read/write callbacks for SPRs
 *
 *  Copyright (C) 2021 Instituto de Pesquisas Eldorado
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
#ifndef SPR_TCG_H
#define SPR_TCG_H

#define SPR_NOACCESS (&spr_noaccess)

/* prototypes for readers and writers for SPRs */
void spr_noaccess(DisasContext *ctx, int gprn, int sprn);
void spr_read_generic(DisasContext *ctx, int gprn, int sprn);
void spr_write_generic(DisasContext *ctx, int sprn, int gprn);
void spr_read_xer(DisasContext *ctx, int gprn, int sprn);
void spr_write_xer(DisasContext *ctx, int sprn, int gprn);
void spr_read_lr(DisasContext *ctx, int gprn, int sprn);
void spr_write_lr(DisasContext *ctx, int sprn, int gprn);
void spr_read_ctr(DisasContext *ctx, int gprn, int sprn);
void spr_write_ctr(DisasContext *ctx, int sprn, int gprn);
void spr_read_ureg(DisasContext *ctx, int gprn, int sprn);
void spr_read_tbl(DisasContext *ctx, int gprn, int sprn);
void spr_read_tbu(DisasContext *ctx, int gprn, int sprn);
void spr_read_atbl(DisasContext *ctx, int gprn, int sprn);
void spr_read_atbu(DisasContext *ctx, int gprn, int sprn);
void spr_read_601_rtcl(DisasContext *ctx, int gprn, int sprn);
void spr_read_601_rtcu(DisasContext *ctx, int gprn, int sprn);
void spr_read_spefscr(DisasContext *ctx, int gprn, int sprn);
void spr_write_spefscr(DisasContext *ctx, int sprn, int gprn);

#ifndef CONFIG_USER_ONLY
void spr_write_generic32(DisasContext *ctx, int sprn, int gprn);
void spr_write_clear(DisasContext *ctx, int sprn, int gprn);
void spr_access_nop(DisasContext *ctx, int sprn, int gprn);
void spr_read_decr(DisasContext *ctx, int gprn, int sprn);
void spr_write_decr(DisasContext *ctx, int sprn, int gprn);
void spr_write_tbl(DisasContext *ctx, int sprn, int gprn);
void spr_write_tbu(DisasContext *ctx, int sprn, int gprn);
void spr_write_atbl(DisasContext *ctx, int sprn, int gprn);
void spr_write_atbu(DisasContext *ctx, int sprn, int gprn);
void spr_read_ibat(DisasContext *ctx, int gprn, int sprn);
void spr_read_ibat_h(DisasContext *ctx, int gprn, int sprn);
void spr_write_ibatu(DisasContext *ctx, int sprn, int gprn);
void spr_write_ibatu_h(DisasContext *ctx, int sprn, int gprn);
void spr_write_ibatl(DisasContext *ctx, int sprn, int gprn);
void spr_write_ibatl_h(DisasContext *ctx, int sprn, int gprn);
void spr_read_dbat(DisasContext *ctx, int gprn, int sprn);
void spr_read_dbat_h(DisasContext *ctx, int gprn, int sprn);
void spr_write_dbatu(DisasContext *ctx, int sprn, int gprn);
void spr_write_dbatu_h(DisasContext *ctx, int sprn, int gprn);
void spr_write_dbatl(DisasContext *ctx, int sprn, int gprn);
void spr_write_dbatl_h(DisasContext *ctx, int sprn, int gprn);
void spr_write_sdr1(DisasContext *ctx, int sprn, int gprn);
void spr_write_601_rtcu(DisasContext *ctx, int sprn, int gprn);
void spr_write_601_rtcl(DisasContext *ctx, int sprn, int gprn);
void spr_write_hid0_601(DisasContext *ctx, int sprn, int gprn);
void spr_read_601_ubat(DisasContext *ctx, int gprn, int sprn);
void spr_write_601_ubatu(DisasContext *ctx, int sprn, int gprn);
void spr_write_601_ubatl(DisasContext *ctx, int sprn, int gprn);
void spr_read_40x_pit(DisasContext *ctx, int gprn, int sprn);
void spr_write_40x_pit(DisasContext *ctx, int sprn, int gprn);
void spr_write_40x_dbcr0(DisasContext *ctx, int sprn, int gprn);
void spr_write_40x_sler(DisasContext *ctx, int sprn, int gprn);
void spr_write_booke_tcr(DisasContext *ctx, int sprn, int gprn);
void spr_write_booke_tsr(DisasContext *ctx, int sprn, int gprn);
void spr_read_403_pbr(DisasContext *ctx, int gprn, int sprn);
void spr_write_403_pbr(DisasContext *ctx, int sprn, int gprn);
void spr_write_pir(DisasContext *ctx, int sprn, int gprn);
void spr_write_excp_prefix(DisasContext *ctx, int sprn, int gprn);
void spr_write_excp_vector(DisasContext *ctx, int sprn, int gprn);
void spr_read_thrm(DisasContext *ctx, int gprn, int sprn);
void spr_write_e500_l1csr0(DisasContext *ctx, int sprn, int gprn);
void spr_write_e500_l1csr1(DisasContext *ctx, int sprn, int gprn);
void spr_write_e500_l2csr0(DisasContext *ctx, int sprn, int gprn);
void spr_write_booke206_mmucsr0(DisasContext *ctx, int sprn, int gprn);
void spr_write_booke_pid(DisasContext *ctx, int sprn, int gprn);
void spr_write_eplc(DisasContext *ctx, int sprn, int gprn);
void spr_write_epsc(DisasContext *ctx, int sprn, int gprn);
void spr_write_mas73(DisasContext *ctx, int sprn, int gprn);
void spr_read_mas73(DisasContext *ctx, int gprn, int sprn);
#ifdef TARGET_PPC64
void spr_read_cfar(DisasContext *ctx, int gprn, int sprn);
void spr_write_cfar(DisasContext *ctx, int sprn, int gprn);
void spr_write_ureg(DisasContext *ctx, int sprn, int gprn);
void spr_read_purr(DisasContext *ctx, int gprn, int sprn);
void spr_write_purr(DisasContext *ctx, int sprn, int gprn);
void spr_read_hdecr(DisasContext *ctx, int gprn, int sprn);
void spr_write_hdecr(DisasContext *ctx, int sprn, int gprn);
void spr_read_vtb(DisasContext *ctx, int gprn, int sprn);
void spr_write_vtb(DisasContext *ctx, int sprn, int gprn);
void spr_write_tbu40(DisasContext *ctx, int sprn, int gprn);
void spr_write_pidr(DisasContext *ctx, int sprn, int gprn);
void spr_write_lpidr(DisasContext *ctx, int sprn, int gprn);
void spr_read_hior(DisasContext *ctx, int gprn, int sprn);
void spr_write_hior(DisasContext *ctx, int sprn, int gprn);
void spr_write_ptcr(DisasContext *ctx, int sprn, int gprn);
void spr_write_pcr(DisasContext *ctx, int sprn, int gprn);
void spr_read_dpdes(DisasContext *ctx, int gprn, int sprn);
void spr_write_dpdes(DisasContext *ctx, int sprn, int gprn);
void spr_write_amr(DisasContext *ctx, int sprn, int gprn);
void spr_write_uamor(DisasContext *ctx, int sprn, int gprn);
void spr_write_iamr(DisasContext *ctx, int sprn, int gprn);
#endif
#endif

#ifdef TARGET_PPC64
void spr_read_prev_upper32(DisasContext *ctx, int gprn, int sprn);
void spr_write_prev_upper32(DisasContext *ctx, int sprn, int gprn);
void spr_read_tar(DisasContext *ctx, int gprn, int sprn);
void spr_write_tar(DisasContext *ctx, int sprn, int gprn);
void spr_read_tm(DisasContext *ctx, int gprn, int sprn);
void spr_write_tm(DisasContext *ctx, int sprn, int gprn);
void spr_read_tm_upper32(DisasContext *ctx, int gprn, int sprn);
void spr_write_tm_upper32(DisasContext *ctx, int sprn, int gprn);
void spr_read_ebb(DisasContext *ctx, int gprn, int sprn);
void spr_write_ebb(DisasContext *ctx, int sprn, int gprn);
void spr_read_ebb_upper32(DisasContext *ctx, int gprn, int sprn);
void spr_write_ebb_upper32(DisasContext *ctx, int sprn, int gprn);
void spr_write_hmer(DisasContext *ctx, int sprn, int gprn);
void spr_write_lpcr(DisasContext *ctx, int sprn, int gprn);
#endif

#endif
