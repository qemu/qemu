/*
 *  PowerPC emulation helpers header for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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

#if defined(MEMSUFFIX)

/* Memory load/store helpers */
void glue(do_lsw, MEMSUFFIX) (int dst);
void glue(do_stsw, MEMSUFFIX) (int src);
void glue(do_lmw, MEMSUFFIX) (int dst);
void glue(do_lmw_le, MEMSUFFIX) (int dst);
void glue(do_stmw, MEMSUFFIX) (int src);
void glue(do_stmw_le, MEMSUFFIX) (int src);
void glue(do_icbi, MEMSUFFIX) (void);
void glue(do_dcbz, MEMSUFFIX) (void);
void glue(do_POWER_lscbx, MEMSUFFIX) (int dest, int ra, int rb);
void glue(do_POWER2_lfq, MEMSUFFIX) (void);
void glue(do_POWER2_lfq_le, MEMSUFFIX) (void);
void glue(do_POWER2_stfq, MEMSUFFIX) (void);
void glue(do_POWER2_stfq_le, MEMSUFFIX) (void);

#if defined(TARGET_PPC64)
void glue(do_lsw_64, MEMSUFFIX) (int dst);
void glue(do_stsw_64, MEMSUFFIX) (int src);
void glue(do_lmw_64, MEMSUFFIX) (int dst);
void glue(do_lmw_le_64, MEMSUFFIX) (int dst);
void glue(do_stmw_64, MEMSUFFIX) (int src);
void glue(do_stmw_le_64, MEMSUFFIX) (int src);
void glue(do_icbi_64, MEMSUFFIX) (void);
void glue(do_dcbz_64, MEMSUFFIX) (void);
#endif

#else

void do_print_mem_EA (target_ulong EA);

/* Registers load and stores */
void do_load_cr (void);
void do_store_cr (uint32_t mask);
#if defined(TARGET_PPC64)
void do_store_pri (int prio);
#endif
target_ulong ppc_load_dump_spr (int sprn);
void ppc_store_dump_spr (int sprn, target_ulong val);

/* Misc */
#if !defined(CONFIG_USER_ONLY)
void do_store_msr (void);
void do_rfi (void);
#if defined(TARGET_PPC64)
void do_rfid (void);
void do_hrfid (void);
#endif
void do_load_6xx_tlb (int is_code);
void do_load_74xx_tlb (int is_code);
#endif

/* POWER / PowerPC 601 specific helpers */
void do_POWER_abso (void);
void do_POWER_clcs (void);
void do_POWER_div (void);
void do_POWER_divo (void);
void do_POWER_divs (void);
void do_POWER_divso (void);
void do_POWER_dozo (void);
void do_POWER_maskg (void);
void do_POWER_mulo (void);
#if !defined(CONFIG_USER_ONLY)
void do_POWER_rac (void);
void do_POWER_rfsvc (void);
void do_store_hid0_601 (void);
#endif

/* PowerPC 602 specific helper */
#if !defined(CONFIG_USER_ONLY)
void do_op_602_mfrom (void);
#endif

/* PowerPC 440 specific helpers */
#if !defined(CONFIG_USER_ONLY)
void do_440_tlbre (int word);
void do_440_tlbwe (int word);
#endif

/* PowerPC 4xx specific helpers */
void do_load_dcr (void);
void do_store_dcr (void);
#if !defined(CONFIG_USER_ONLY)
void do_40x_rfci (void);
void do_rfci (void);
void do_rfdi (void);
void do_rfmci (void);
void do_4xx_tlbre_lo (void);
void do_4xx_tlbre_hi (void);
void do_4xx_tlbwe_lo (void);
void do_4xx_tlbwe_hi (void);
#endif

/* PowerPC 440 specific helpers */
void do_440_dlmzb (void);

/* PowerPC 403 specific helpers */
#if !defined(CONFIG_USER_ONLY)
void do_load_403_pb (int num);
void do_store_403_pb (int num);
#endif

#endif
