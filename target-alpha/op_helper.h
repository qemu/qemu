/*
 *  Alpha emulation cpu micro-operations helpers definitions for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

void helper_call_pal (uint32_t palcode);
void helper_excp (uint32_t excp, uint32_t error);
void helper_load_pcc (void);
void helper_load_implver (void);
void helper_load_fpcr (void);
void helper_store_fpcr (void);
void helper_load_irf (void);
void helper_set_irf (void);
void helper_clear_irf (void);
void helper_addqv (void);
void helper_addlv (void);
void helper_subqv (void);
void helper_sublv (void);
void helper_mullv (void);
void helper_mulqv (void);
void helper_mskbl (void);
void helper_extbl (void);
void helper_insbl (void);
void helper_mskwl (void);
void helper_extwl (void);
void helper_inswl (void);
void helper_mskll (void);
void helper_extll (void);
void helper_insll (void);
void helper_zap (void);
void helper_zapnot (void);
void helper_mskql (void);
void helper_extql (void);
void helper_insql (void);
void helper_mskwh (void);
void helper_inswh (void);
void helper_extwh (void);
void helper_msklh (void);
void helper_inslh (void);
void helper_extlh (void);
void helper_mskqh (void);
void helper_insqh (void);
void helper_extqh (void);
void helper_cmpbge (void);
void helper_cmov_fir (int freg);

double helper_ldff_raw (target_ulong ea);
void helper_stff_raw (target_ulong ea, double op);
double helper_ldfg_raw (target_ulong ea);
void helper_stfg_raw (target_ulong ea, double op);
#if !defined(CONFIG_USER_ONLY)
double helper_ldff_user (target_ulong ea);
void helper_stff_user (target_ulong ea, double op);
double helper_ldff_kernel (target_ulong ea);
void helper_stff_kernel (target_ulong ea, double op);
double helper_ldff_data (target_ulong ea);
void helper_stff_data (target_ulong ea, double op);
double helper_ldfg_user (target_ulong ea);
void helper_stfg_user (target_ulong ea, double op);
double helper_ldfg_kernel (target_ulong ea);
void helper_stfg_kernel (target_ulong ea, double op);
double helper_ldfg_data (target_ulong ea);
void helper_stfg_data (target_ulong ea, double op);
#endif

void helper_sqrts (void);
void helper_cpys (void);
void helper_cpysn (void);
void helper_cpyse (void);
void helper_itofs (void);
void helper_ftois (void);

void helper_sqrtt (void);
void helper_cmptun (void);
void helper_cmpteq (void);
void helper_cmptle (void);
void helper_cmptlt (void);
void helper_itoft (void);
void helper_ftoit (void);

void helper_addf (void);
void helper_subf (void);
void helper_mulf (void);
void helper_divf (void);
void helper_sqrtf (void);
void helper_cmpfeq (void);
void helper_cmpfne (void);
void helper_cmpflt (void);
void helper_cmpfle (void);
void helper_cmpfgt (void);
void helper_cmpfge (void);
void helper_itoff (void);

void helper_addg (void);
void helper_subg (void);
void helper_mulg (void);
void helper_divg (void);
void helper_sqrtg (void);
void helper_cmpgeq (void);
void helper_cmpglt (void);
void helper_cmpgle (void);

void helper_cvtqs (void);
void helper_cvttq (void);
void helper_cvtqt (void);
void helper_cvtqf (void);
void helper_cvtgf (void);
void helper_cvtgd (void);
void helper_cvtgq (void);
void helper_cvtqg (void);
void helper_cvtdg (void);
void helper_cvtlq (void);
void helper_cvtql (void);
void helper_cvtqlv (void);
void helper_cvtqlsv (void);

void helper_mfpr (int iprn);
void helper_mtpr (int iprn);
void helper_ld_phys_to_virt (void);
void helper_st_phys_to_virt (void);
void helper_tb_flush (void);

#if defined(HOST_SPARC) || defined(HOST_SPARC64)
void helper_reset_FT0 (void);
void helper_reset_FT1 (void);
void helper_reset_FT2 (void);
#endif
