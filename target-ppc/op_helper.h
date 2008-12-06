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

void do_print_mem_EA (target_ulong EA);

/* Registers load and stores */
#if defined(TARGET_PPC64)
void do_store_pri (int prio);
#endif
target_ulong ppc_load_dump_spr (int sprn);
void ppc_store_dump_spr (int sprn, target_ulong val);

/* Misc */
/* POWER / PowerPC 601 specific helpers */
#if !defined(CONFIG_USER_ONLY)
void do_POWER_rac (void);
void do_store_hid0_601 (void);
#endif

/* PowerPC 403 specific helpers */
#if !defined(CONFIG_USER_ONLY)
void do_load_403_pb (int num);
void do_store_403_pb (int num);
#endif
