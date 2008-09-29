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

void helper_mfpr (int iprn);
void helper_mtpr (int iprn);
void helper_ld_phys_to_virt (void);
void helper_st_phys_to_virt (void);

