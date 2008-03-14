/*
 *  CRIS mmu emulation.
 *
 *  Copyright (c) 2007 AXIS Communications AB
 *  Written by Edgar E. Iglesias.
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

#ifndef CONFIG_USER_ONLY

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "cpu.h"
#include "mmu.h"
#include "exec-all.h"

#define D(x)

static int cris_mmu_enabled(uint32_t rw_gc_cfg)
{
	return (rw_gc_cfg & 12) != 0;
}

static int cris_mmu_segmented_addr(int seg, uint32_t rw_mm_cfg)
{
	return (1 << seg) & rw_mm_cfg;
}

static uint32_t cris_mmu_translate_seg(CPUState *env, int seg)
{
	uint32_t base;
	int i;

	if (seg < 8)
		base = env->sregs[SFR_RW_MM_KBASE_LO];
	else
		base = env->sregs[SFR_RW_MM_KBASE_HI];

	i = seg & 7;
	base >>= i * 4;
	base &= 15;

	base <<= 28;
	return base;
}
/* Used by the tlb decoder.  */
#define EXTRACT_FIELD(src, start, end) \
	    (((src) >> start) & ((1 << (end - start + 1)) - 1))

static inline void set_field(uint32_t *dst, unsigned int val, 
			     unsigned int offset, unsigned int width)
{
	uint32_t mask;

	mask = (1 << width) - 1;
	mask <<= offset;
	val <<= offset;

	val &= mask;
	D(printf ("val=%x mask=%x dst=%x\n", val, mask, *dst));
	*dst &= ~(mask);
	*dst |= val;
}

static int cris_mmu_translate_page(struct cris_mmu_result_t *res,
				   CPUState *env, uint32_t vaddr,
				   int rw, int usermode)
{
	unsigned int vpage;
	unsigned int idx;
	uint32_t lo, hi;
	uint32_t tlb_vpn, tlb_pfn = 0;
	int tlb_pid, tlb_g, tlb_v, tlb_k, tlb_w, tlb_x;
	int cfg_v, cfg_k, cfg_w, cfg_x;	
	int i, match = 0;
	uint32_t r_cause;
	uint32_t r_cfg;
	int rwcause;
	int update_sel = 0;

	r_cause = env->sregs[SFR_R_MM_CAUSE];
	r_cfg = env->sregs[SFR_RW_MM_CFG];
	rwcause = rw ? CRIS_MMU_ERR_WRITE : CRIS_MMU_ERR_READ;

	vpage = vaddr >> 13;
	idx = vpage & 15;

	/* We know the index which to check on each set.
	   Scan both I and D.  */
#if 0
	for (i = 0; i < 4; i++) {
		int j;
		for (j = 0; j < 16; j++) {
			lo = env->tlbsets[1][i][j].lo;
			hi = env->tlbsets[1][i][j].hi;
			tlb_vpn = EXTRACT_FIELD(hi, 13, 31);
			tlb_pfn = EXTRACT_FIELD(lo, 13, 31);

			printf ("TLB: [%d][%d] hi=%x lo=%x v=%x p=%x\n", 
					i, j, hi, lo, tlb_vpn, tlb_pfn);
		}
	}
#endif
	for (i = 0; i < 4; i++)
	{
		lo = env->tlbsets[1][i][idx].lo;
		hi = env->tlbsets[1][i][idx].hi;

		tlb_vpn = EXTRACT_FIELD(hi, 13, 31);
		tlb_pfn = EXTRACT_FIELD(lo, 13, 31);

		D(printf ("TLB[%d][%d] tlbv=%x vpage=%x -> pfn=%x\n", 
				i, idx, tlb_vpn, vpage, tlb_pfn));
		if (tlb_vpn == vpage) {
			match = 1;
			break;
		}
	}

	if (match) {

		cfg_w  = EXTRACT_FIELD(r_cfg, 19, 19);
		cfg_k  = EXTRACT_FIELD(r_cfg, 18, 18);
		cfg_x  = EXTRACT_FIELD(r_cfg, 17, 17);
		cfg_v  = EXTRACT_FIELD(r_cfg, 16, 16);

		tlb_pid = EXTRACT_FIELD(hi, 0, 7);
		tlb_pfn = EXTRACT_FIELD(lo, 13, 31);
		tlb_g  = EXTRACT_FIELD(lo, 4, 4);
		tlb_v = EXTRACT_FIELD(lo, 3, 3);
		tlb_k = EXTRACT_FIELD(lo, 2, 2);
		tlb_w = EXTRACT_FIELD(lo, 1, 1);
		tlb_x = EXTRACT_FIELD(lo, 0, 0);

		/*
		set_exception_vector(0x04, i_mmu_refill);
		set_exception_vector(0x05, i_mmu_invalid);
		set_exception_vector(0x06, i_mmu_access);
		set_exception_vector(0x07, i_mmu_execute);
		set_exception_vector(0x08, d_mmu_refill);
		set_exception_vector(0x09, d_mmu_invalid);
		set_exception_vector(0x0a, d_mmu_access);
		set_exception_vector(0x0b, d_mmu_write);
		*/
		if (cfg_v && !tlb_v) {
			printf ("tlb: invalid\n");
			set_field(&r_cause, rwcause, 8, 9);
			match = 0;
			res->bf_vec = 0x9;
			update_sel = 1;
		}
		else if (!tlb_g 
			 && tlb_pid != 0xff
			 && tlb_pid != env->pregs[PR_PID]
			 && cfg_w && !tlb_w) {
			printf ("tlb: wrong pid\n");
			match = 0;
			res->bf_vec = 0xa;
		}
		else if (rw && cfg_w && !tlb_w) {
			printf ("tlb: write protected\n");
			match = 0;
			res->bf_vec = 0xb;
		}
	} else
		update_sel = 1;

	if (update_sel) {
		/* miss.  */
		env->sregs[SFR_RW_MM_TLB_SEL] = 0;
		D(printf ("tlb: miss %x vp=%x\n", 
			env->sregs[SFR_RW_MM_TLB_SEL], vpage & 15));
		set_field(&env->sregs[SFR_RW_MM_TLB_SEL], vpage & 15, 0, 4);
		set_field(&env->sregs[SFR_RW_MM_TLB_SEL], 0, 4, 5);
		res->bf_vec = 0x8;
	}

	if (!match) {
		set_field(&r_cause, rwcause, 8, 9);
		set_field(&r_cause, vpage, 13, 19);
		set_field(&r_cause, env->pregs[PR_PID], 0, 8);
		env->sregs[SFR_R_MM_CAUSE] = r_cause;
	}
	D(printf ("%s mtch=%d pc=%x va=%x vpn=%x tlbvpn=%x pfn=%x pid=%x"
		  " %x cause=%x sel=%x r13=%x\n",
		  __func__, match, env->pc,
		  vaddr, vpage,
		  tlb_vpn, tlb_pfn, tlb_pid, 
		  env->pregs[PR_PID],
		  r_cause,
		  env->sregs[SFR_RW_MM_TLB_SEL],
		  env->regs[13]));

	res->pfn = tlb_pfn;
	return !match;
}

/* Give us the vaddr corresponding to the latest TLB update.  */
target_ulong cris_mmu_tlb_latest_update(CPUState *env, uint32_t new_lo)
{
	uint32_t sel = env->sregs[SFR_RW_MM_TLB_SEL];
	uint32_t vaddr;
	uint32_t hi;
	int set;
	int idx;

	idx = EXTRACT_FIELD(sel, 0, 4);
	set = EXTRACT_FIELD(sel, 4, 5);

	hi = env->tlbsets[1][set][idx].hi;
	vaddr = EXTRACT_FIELD(hi, 13, 31);
	return vaddr << TARGET_PAGE_BITS;
}

int cris_mmu_translate(struct cris_mmu_result_t *res,
		       CPUState *env, uint32_t vaddr,
		       int rw, int mmu_idx)
{
	uint32_t phy = vaddr;
	int seg;
	int miss = 0;
	int is_user = mmu_idx == MMU_USER_IDX;

	if (!cris_mmu_enabled(env->sregs[SFR_RW_GC_CFG])) {
		res->phy = vaddr;
		return 0;
	}

	seg = vaddr >> 28;
	if (cris_mmu_segmented_addr(seg, env->sregs[SFR_RW_MM_CFG]))
	{
		uint32_t base;

		miss = 0;
		base = cris_mmu_translate_seg(env, seg);
		phy = base | (0x0fffffff & vaddr);
		res->phy = phy;
	}
	else
	{
		miss = cris_mmu_translate_page(res, env, vaddr, rw, is_user);
		if (!miss) {
			phy &= 8191;
			phy |= (res->pfn << 13);
			res->phy = phy;
		}
	}
	D(printf ("miss=%d v=%x -> p=%x\n", miss, vaddr, phy));
	return miss;
}
#endif
