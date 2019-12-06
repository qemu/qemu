/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 *
 * This file contains architecture parameters
 *
 * 
 *
 */

#ifndef _MAX_H
#define _MAX_H


#define INSTRUCTIONS_MAX 7		/* 2 pairs + loopend */
#define LOOPNEST_MAX 2
#define IMMEDS_MAX 2


#define MAX_CORES 4				/* Cores per shared L2 */
#define MAX_CLUSTERS 2
#define MAX_THREADS_PER_CLUSTER 4
#define THREADS_PER_CORE (MAX_CLUSTERS * MAX_THREADS_PER_CLUSTER)		/* HW threads in a core */
#define THREADS_MAX (MAX_CORES * THREADS_PER_CORE)
#define MAX_EXT_CONTEXTS 4
#define MAX_L2_INTERLEAVES 2
#define MAX_VFIFO_COUNT 4

#define SLOTS_MAX 4

/* #define PROCESSORS_MAX 1 */
#define REG_WRITES_MAX 32		/*?? */
#define PRED_WRITES_MAX 5		/* 4 insns + endloop */
#define STORES_MAX 2
#define LOADS_MAX 2
#define MAX_PRED 4

#define PACKET_BYTES_MAX 16
#define MAX_TLB_ENTRIES 320
#define DTLB_ENTRIES 16
#define ITLB_ENTRIES 16
#define MAX_N_GRANULES 4		/* Max of number of granules of all caches */

/* FMC */
#define FAST_MEM_CACHE_SIZE (256)

/* IIC stuff */
#define IIC_TAGS_MAX (1024*128)	/* 1MB = 2^20 bytes */
#define IIC_DATA_IN_BYTES_MAX (1024*1024*4)

#define MAX_TLB_GUESS_ENTRIES (1024)	// power of 2

/* IICTLB */
#define IICTLB_PAGE_BITS 12
#define IICTLB_ENTRIES 64
#define IICTLB_PAGE_SIZE (1024*4)
#define NUM_MEM_UTLB_ENTRIES 8

#define MAX_L1S_SIZE (1024*1024)

#define REG_OPERANDS_MAX 5
#define SUB_IMMEDS_MAX 4		/* number of immediate fragments within insn */

#define MAX_SUPPORTED_BUSES 4

#define MAX_HISTO_BUCKETS 256

/* Trace input format */
//#define QTRACE_HEADER_SIZE_MAX 50 /* at most 50 chars in header */

#define MAX_PMU_REGISTERS 8
#define EXEC_PIPE_DEPTH 3
#define MAX_L2_INTERLEAVE 2
#define MAX_BP_QUEUE_SIZE 16
#define MAX_CMT_WINDOW 10
#define MAX_BRANCHES_PER_PACKET 2
#endif							/* _MAX_H */
