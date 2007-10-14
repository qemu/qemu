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

static int cris_mmu_translate_page(struct cris_mmu_result_t *res,
				   CPUState *env, uint32_t vaddr,
				   int rw, int usermode)
{
	unsigned int vpage;
	unsigned int idx;
	uint32_t lo, hi;
	uint32_t vpn, pfn = 0, pid, fg, fv, fk, fw, fx;
	int i, match = 0;

	vpage = vaddr >> 13;
	idx = vpage & 31;
	vpage >>= 4;

	/* We know the index which to check on each set.
	   Scan both I and D.  */
	for (i = 0; i < 4; i++)
	{
		lo = env->tlbsets[0][i][idx].lo;
		hi = env->tlbsets[0][i][idx].hi;

		vpn = EXTRACT_FIELD(hi, 13, 31);
		pid = EXTRACT_FIELD(hi, 0, 7);

		if (vpn == vpage
		    && pid == env->pregs[SR_PID]) {
			match = 1;
			break;
		}
	}

	if (match) {
		pfn = EXTRACT_FIELD(lo, 13, 31);
		fg = EXTRACT_FIELD(lo, 4, 4);
		fv = EXTRACT_FIELD(lo, 3, 3);
		fk = EXTRACT_FIELD(lo, 2, 2);
		fw = EXTRACT_FIELD(lo, 1, 1);
		fx = EXTRACT_FIELD(lo, 0, 0);
	}
	printf ("%s match=%d vaddr=%x vpage=%x vpn=%x pfn=%x pid=%x %x\n",
		__func__, match,
		vaddr, vpage,
		vpn, pfn, pid, env->pregs[SR_PID]);
	res->pfn = pfn;
	return !match;
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
//	printf ("miss=%d v=%x -> p=%x\n", miss, vaddr, phy);
	return miss;
}
#endif
