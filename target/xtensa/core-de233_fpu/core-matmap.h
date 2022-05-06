/* 
 * xtensa/config/core-matmap.h -- Memory access and translation mapping
 *	parameters (CHAL) of the Xtensa processor core configuration.
 *
 *  If you are using Xtensa Tools, see <xtensa/config/core.h> (which includes
 *  this file) for more details.
 *
 *  In the Xtensa processor products released to date, all parameters
 *  defined in this file are derivable (at least in theory) from
 *  information contained in the core-isa.h header file.
 *  In particular, the following core configuration parameters are relevant:
 *	XCHAL_HAVE_CACHEATTR
 *	XCHAL_HAVE_MIMIC_CACHEATTR
 *	XCHAL_HAVE_XLT_CACHEATTR
 *	XCHAL_HAVE_PTP_MMU
 *	XCHAL_ITLB_ARF_ENTRIES_LOG2
 *	XCHAL_DTLB_ARF_ENTRIES_LOG2
 *	XCHAL_DCACHE_IS_WRITEBACK
 *	XCHAL_ICACHE_SIZE		(presence of I-cache)
 *	XCHAL_DCACHE_SIZE		(presence of D-cache)
 *	XCHAL_HW_VERSION_MAJOR
 *	XCHAL_HW_VERSION_MINOR
 */

/* Copyright (c) 1999-2020 Tensilica Inc.

   Permission is hereby granted, free of charge, to any person obtaining
   a copy of this software and associated documentation files (the
   "Software"), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

#ifndef XTENSA_CONFIG_CORE_MATMAP_H
#define XTENSA_CONFIG_CORE_MATMAP_H

/*----------------------------------------------------------------------
			CACHE (MEMORY ACCESS) ATTRIBUTES
  ----------------------------------------------------------------------*/



/*  Cache Attribute encodings -- lists of access modes for each cache attribute:  */
#define XCHAL_FCA_LIST		XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_BYPASS	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_BYPASS	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_CACHED	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_CACHED	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_CACHED	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_CACHED	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION	XCHAL_SEP \
				XTHAL_FAM_EXCEPTION
#define XCHAL_LCA_LIST		XTHAL_LAM_BYPASSG	XCHAL_SEP \
				XTHAL_LAM_BYPASSG	XCHAL_SEP \
				XTHAL_LAM_BYPASSG	XCHAL_SEP \
				XTHAL_LAM_BYPASSG	XCHAL_SEP \
				XTHAL_LAM_CACHED	XCHAL_SEP \
				XTHAL_LAM_CACHED	XCHAL_SEP \
				XTHAL_LAM_CACHED	XCHAL_SEP \
				XTHAL_LAM_CACHED	XCHAL_SEP \
				XTHAL_LAM_CACHED	XCHAL_SEP \
				XTHAL_LAM_CACHED	XCHAL_SEP \
				XTHAL_LAM_CACHED	XCHAL_SEP \
				XTHAL_LAM_CACHED	XCHAL_SEP \
				XTHAL_LAM_EXCEPTION	XCHAL_SEP \
				XTHAL_LAM_EXCEPTION	XCHAL_SEP \
				XTHAL_LAM_EXCEPTION	XCHAL_SEP \
				XTHAL_LAM_EXCEPTION
#define XCHAL_SCA_LIST		XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_BYPASS	XCHAL_SEP \
				XTHAL_SAM_BYPASS	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_WRITEBACK	XCHAL_SEP \
				XTHAL_SAM_WRITEBACK	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_WRITETHRU	XCHAL_SEP \
				XTHAL_SAM_WRITETHRU	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION	XCHAL_SEP \
				XTHAL_SAM_EXCEPTION

#define XCHAL_CA_R   (0xC0 | 0x40000000)
#define XCHAL_CA_RX  (0xD0 | 0x40000000)
#define XCHAL_CA_RW  (0xE0 | 0x40000000)
#define XCHAL_CA_RWX (0xF0 | 0x40000000)

/*
 *  Specific encoded cache attribute values of general interest.
 *  If a specific cache mode is not available, the closest available
 *  one is returned instead (eg. writethru instead of writeback,
 *  bypass instead of writethru).
 */
#define XCHAL_CA_BYPASS  		3	/* cache disabled (bypassed) mode */
#define XCHAL_CA_WRITETHRU		11	/* cache enabled (write-through) mode */
#define XCHAL_CA_WRITEBACK		7	/* cache enabled (write-back) mode */
#define XCHAL_HAVE_CA_WRITEBACK_NOALLOC	0	/* write-back no-allocate availability */
#define XCHAL_CA_WRITEBACK_NOALLOC	7	/* cache enabled (write-back no-allocate) mode */
#define XCHAL_CA_BYPASS_RX  		1	/* cache disabled (bypassed) mode (no write) */
#define XCHAL_CA_WRITETHRU_RX		9	/* cache enabled (write-through) mode (no write) */
#define XCHAL_CA_WRITEBACK_RX		5	/* cache enabled (write-back) mode (no write) */
#define XCHAL_CA_WRITEBACK_NOALLOC_RX	5	/* cache enabled (write-back no-allocate) mode (no write) */
#define XCHAL_CA_BYPASS_RW  		2	/* cache disabled (bypassed) mode (no exec) */
#define XCHAL_CA_WRITETHRU_RW		10	/* cache enabled (write-through) mode (no exec) */
#define XCHAL_CA_WRITEBACK_RW		6	/* cache enabled (write-back) mode (no exec) */
#define XCHAL_CA_WRITEBACK_NOALLOC_RW	6	/* cache enabled (write-back no-allocate) mode (no exec) */
#define XCHAL_CA_BYPASS_R  		0	/* cache disabled (bypassed) mode (no exec, no write) */
#define XCHAL_CA_WRITETHRU_R		8	/* cache enabled (write-through) mode (no exec, no write) */
#define XCHAL_CA_WRITEBACK_R		4	/* cache enabled (write-back) mode (no exec, no write) */
#define XCHAL_CA_WRITEBACK_NOALLOC_R	4	/* cache enabled (write-back no-allocate) mode (no exec, no write) */
#define XCHAL_CA_ILLEGAL		12	/* no access allowed (all cause exceptions) mode */

/*----------------------------------------------------------------------
				MMU
  ----------------------------------------------------------------------*/

/*
 *  General notes on MMU parameters.
 *
 *  Terminology:
 *	ASID = address-space ID (acts as an "extension" of virtual addresses)
 *	VPN  = virtual page number
 *	PPN  = physical page number
 *	CA   = encoded cache attribute (access modes)
 *	TLB  = translation look-aside buffer (term is stretched somewhat here)
 *	I    = instruction (fetch accesses)
 *	D    = data (load and store accesses)
 *	way  = each TLB (ITLB and DTLB) consists of a number of "ways"
 *		that simultaneously match the virtual address of an access;
 *		a TLB successfully translates a virtual address if exactly
 *		one way matches the vaddr; if none match, it is a miss;
 *		if multiple match, one gets a "multihit" exception;
 *		each way can be independently configured in terms of number of
 *		entries, page sizes, which fields are writable or constant, etc.
 *	set  = group of contiguous ways with exactly identical parameters
 *	ARF  = auto-refill; hardware services a 1st-level miss by loading a PTE
 *		from the page table and storing it in one of the auto-refill ways;
 *		if this PTE load also misses, a miss exception is posted for s/w.
 *	min-wired = a "min-wired" way can be used to map a single (minimum-sized)
 * 		page arbitrarily under program control; it has a single entry,
 *		is non-auto-refill (some other way(s) must be auto-refill),
 *		all its fields (VPN, PPN, ASID, CA) are all writable, and it
 *		supports the XCHAL_MMU_MIN_PTE_PAGE_SIZE page size (a current
 *		restriction is that this be the only page size it supports).
 *
 *  TLB way entries are virtually indexed.
 *  TLB ways that support multiple page sizes:
 *	- must have all writable VPN and PPN fields;
 *	- can only use one page size at any given time (eg. setup at startup),
 *	  selected by the respective ITLBCFG or DTLBCFG special register,
 *	  whose bits n*4+3 .. n*4 index the list of page sizes for way n
 *	  (XCHAL_xTLB_SETm_PAGESZ_LOG2_LIST for set m corresponding to way n);
 *	  this list may be sparse for auto-refill ways because auto-refill
 *	  ways have independent lists of supported page sizes sharing a
 *	  common encoding with PTE entries; the encoding is the index into
 *	  this list; unsupported sizes for a given way are zero in the list;
 *	  selecting unsupported sizes results in undefine hardware behaviour;
 *	- is only possible for ways 0 thru 7 (due to ITLBCFG/DTLBCFG definition).
 */

#define XCHAL_MMU_ASID_INVALID		0	/* ASID value indicating invalid address space */
#define XCHAL_MMU_ASID_KERNEL		1	/* ASID value indicating kernel (ring 0) address space */
#define XCHAL_MMU_SR_BITS		0	/* number of size-restriction bits supported */
#define XCHAL_MMU_CA_BITS		4	 /* number of bits needed to hold cache attribute encoding */
#define XCHAL_MMU_MAX_PTE_PAGE_SIZE	12	/* max page size in a PTE structure (log2) */
#define XCHAL_MMU_MIN_PTE_PAGE_SIZE	12	/* min page size in a PTE structure (log2) */


/***  Instruction TLB:  ***/

#define XCHAL_ITLB_WAY_BITS		3	/* number of bits holding the ways */
#define XCHAL_ITLB_WAYS			7	/* number of ways (n-way set-associative TLB) */
#define XCHAL_ITLB_ARF_WAYS		4	/* number of auto-refill ways */
#define XCHAL_ITLB_SETS			7	/* number of sets (groups of ways with identical settings) */

/*  Way set to which each way belongs:  */
#define XCHAL_ITLB_WAY0_SET		0
#define XCHAL_ITLB_WAY1_SET		1
#define XCHAL_ITLB_WAY2_SET		2
#define XCHAL_ITLB_WAY3_SET		3
#define XCHAL_ITLB_WAY4_SET		4
#define XCHAL_ITLB_WAY5_SET		5
#define XCHAL_ITLB_WAY6_SET		6

/*  Ways sets that are used by hardware auto-refill (ARF):  */
#define XCHAL_ITLB_ARF_SETS		4	/* number of auto-refill sets */
#define XCHAL_ITLB_ARF_SET0		0	/* index of n'th auto-refill set */
#define XCHAL_ITLB_ARF_SET1		1	/* index of n'th auto-refill set */
#define XCHAL_ITLB_ARF_SET2		2	/* index of n'th auto-refill set */
#define XCHAL_ITLB_ARF_SET3		3	/* index of n'th auto-refill set */

/*  Way sets that are "min-wired" (see terminology comment above):  */
#define XCHAL_ITLB_MINWIRED_SETS	0	/* number of "min-wired" sets */


/*  ITLB way set 0 (group of ways 0 thru 0):  */
#define XCHAL_ITLB_SET0_WAY			0	/* index of first way in this way set */
#define XCHAL_ITLB_SET0_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_ITLB_SET0_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_ITLB_SET0_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_ITLB_SET0_ARF			1	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_ITLB_SET0_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_ITLB_SET0_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_ITLB_SET0_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_ITLB_SET0_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_ITLB_SET0_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_ITLB_SET0_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_ITLB_SET0_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET0_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET0_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_ITLB_SET0_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET0_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET0_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET0_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  ITLB way set 1 (group of ways 1 thru 1):  */
#define XCHAL_ITLB_SET1_WAY			1	/* index of first way in this way set */
#define XCHAL_ITLB_SET1_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_ITLB_SET1_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_ITLB_SET1_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_ITLB_SET1_ARF			1	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_ITLB_SET1_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_ITLB_SET1_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_ITLB_SET1_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_ITLB_SET1_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_ITLB_SET1_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_ITLB_SET1_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_ITLB_SET1_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET1_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET1_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_ITLB_SET1_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET1_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET1_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET1_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  ITLB way set 2 (group of ways 2 thru 2):  */
#define XCHAL_ITLB_SET2_WAY			2	/* index of first way in this way set */
#define XCHAL_ITLB_SET2_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_ITLB_SET2_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_ITLB_SET2_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_ITLB_SET2_ARF			1	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_ITLB_SET2_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_ITLB_SET2_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_ITLB_SET2_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_ITLB_SET2_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_ITLB_SET2_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_ITLB_SET2_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_ITLB_SET2_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET2_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET2_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_ITLB_SET2_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET2_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET2_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET2_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  ITLB way set 3 (group of ways 3 thru 3):  */
#define XCHAL_ITLB_SET3_WAY			3	/* index of first way in this way set */
#define XCHAL_ITLB_SET3_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_ITLB_SET3_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_ITLB_SET3_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_ITLB_SET3_ARF			1	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_ITLB_SET3_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_ITLB_SET3_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_ITLB_SET3_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_ITLB_SET3_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_ITLB_SET3_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_ITLB_SET3_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_ITLB_SET3_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET3_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET3_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_ITLB_SET3_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET3_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET3_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET3_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  ITLB way set 4 (group of ways 4 thru 4):  */
#define XCHAL_ITLB_SET4_WAY			4	/* index of first way in this way set */
#define XCHAL_ITLB_SET4_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_ITLB_SET4_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_ITLB_SET4_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_ITLB_SET4_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_ITLB_SET4_PAGESIZES		4	/* number of supported page sizes in this way */
#define XCHAL_ITLB_SET4_PAGESZ_BITS		2	/* number of bits to encode the page size */
#define XCHAL_ITLB_SET4_PAGESZ_LOG2_MIN		20	/* log2(minimum supported page size) */
#define XCHAL_ITLB_SET4_PAGESZ_LOG2_MAX		26	/* log2(maximum supported page size) */
#define XCHAL_ITLB_SET4_PAGESZ_LOG2_LIST	20 XCHAL_SEP 22 XCHAL_SEP 24 XCHAL_SEP 26	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_ITLB_SET4_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_ITLB_SET4_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET4_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET4_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_ITLB_SET4_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET4_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET4_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET4_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  ITLB way set 5 (group of ways 5 thru 5):  */
#define XCHAL_ITLB_SET5_WAY			5	/* index of first way in this way set */
#define XCHAL_ITLB_SET5_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_ITLB_SET5_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_ITLB_SET5_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_ITLB_SET5_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_ITLB_SET5_PAGESIZES		2	/* number of supported page sizes in this way */
#define XCHAL_ITLB_SET5_PAGESZ_BITS		1	/* number of bits to encode the page size */
#define XCHAL_ITLB_SET5_PAGESZ_LOG2_MIN		27	/* log2(minimum supported page size) */
#define XCHAL_ITLB_SET5_PAGESZ_LOG2_MAX		28	/* log2(maximum supported page size) */
#define XCHAL_ITLB_SET5_PAGESZ_LOG2_LIST	27 XCHAL_SEP 28	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_ITLB_SET5_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_ITLB_SET5_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET5_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET5_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_ITLB_SET5_ASID_RESET		1	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET5_VPN_RESET		1	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET5_PPN_RESET		1	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET5_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */
/*  Reset ASID values for each entry of ITLB way set 5 (because SET5_ASID_RESET is non-zero):  */
#define XCHAL_ITLB_SET5_E0_ASID_RESET		0x00
#define XCHAL_ITLB_SET5_E1_ASID_RESET		0x00
#define XCHAL_ITLB_SET5_E2_ASID_RESET		0x00
#define XCHAL_ITLB_SET5_E3_ASID_RESET		0x00
/*  Reset VPN values for each entry of ITLB way set 5 (because SET5_VPN_RESET is non-zero):  */
#define XCHAL_ITLB_SET5_E0_VPN_RESET		0x00000000
#define XCHAL_ITLB_SET5_E1_VPN_RESET		0x00000000
#define XCHAL_ITLB_SET5_E2_VPN_RESET		0x00000000
#define XCHAL_ITLB_SET5_E3_VPN_RESET		0x00000000
/*  Reset PPN values for each entry of ITLB way set 5 (because SET5_PPN_RESET is non-zero):  */
#define XCHAL_ITLB_SET5_E0_PPN_RESET		0x00000000
#define XCHAL_ITLB_SET5_E1_PPN_RESET		0x00000000
#define XCHAL_ITLB_SET5_E2_PPN_RESET		0x00000000
#define XCHAL_ITLB_SET5_E3_PPN_RESET		0x00000000

/*  ITLB way set 6 (group of ways 6 thru 6):  */
#define XCHAL_ITLB_SET6_WAY			6	/* index of first way in this way set */
#define XCHAL_ITLB_SET6_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_ITLB_SET6_ENTRIES_LOG2		3	/* log2(number of entries in this way) */
#define XCHAL_ITLB_SET6_ENTRIES			8	/* number of entries in this way (always a power of 2) */
#define XCHAL_ITLB_SET6_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_ITLB_SET6_PAGESIZES		2	/* number of supported page sizes in this way */
#define XCHAL_ITLB_SET6_PAGESZ_BITS		1	/* number of bits to encode the page size */
#define XCHAL_ITLB_SET6_PAGESZ_LOG2_MIN		28	/* log2(minimum supported page size) */
#define XCHAL_ITLB_SET6_PAGESZ_LOG2_MAX		29	/* log2(maximum supported page size) */
#define XCHAL_ITLB_SET6_PAGESZ_LOG2_LIST	29 XCHAL_SEP 28	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_ITLB_SET6_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_ITLB_SET6_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET6_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_ITLB_SET6_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_ITLB_SET6_ASID_RESET		1	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET6_VPN_RESET		1	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET6_PPN_RESET		1	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_ITLB_SET6_CA_RESET		1	/* 1 if CA reset values defined (and all writable); 0 otherwise */
/*  Reset ASID values for each entry of ITLB way set 6 (because SET6_ASID_RESET is non-zero):  */
#define XCHAL_ITLB_SET6_E0_ASID_RESET		0x01
#define XCHAL_ITLB_SET6_E1_ASID_RESET		0x01
#define XCHAL_ITLB_SET6_E2_ASID_RESET		0x01
#define XCHAL_ITLB_SET6_E3_ASID_RESET		0x01
#define XCHAL_ITLB_SET6_E4_ASID_RESET		0x01
#define XCHAL_ITLB_SET6_E5_ASID_RESET		0x01
#define XCHAL_ITLB_SET6_E6_ASID_RESET		0x01
#define XCHAL_ITLB_SET6_E7_ASID_RESET		0x01
/*  Reset VPN values for each entry of ITLB way set 6 (because SET6_VPN_RESET is non-zero):  */
#define XCHAL_ITLB_SET6_E0_VPN_RESET		0x00000000
#define XCHAL_ITLB_SET6_E1_VPN_RESET		0x20000000
#define XCHAL_ITLB_SET6_E2_VPN_RESET		0x40000000
#define XCHAL_ITLB_SET6_E3_VPN_RESET		0x60000000
#define XCHAL_ITLB_SET6_E4_VPN_RESET		0x80000000
#define XCHAL_ITLB_SET6_E5_VPN_RESET		0xA0000000
#define XCHAL_ITLB_SET6_E6_VPN_RESET		0xC0000000
#define XCHAL_ITLB_SET6_E7_VPN_RESET		0xE0000000
/*  Reset PPN values for each entry of ITLB way set 6 (because SET6_PPN_RESET is non-zero):  */
#define XCHAL_ITLB_SET6_E0_PPN_RESET		0x00000000
#define XCHAL_ITLB_SET6_E1_PPN_RESET		0x20000000
#define XCHAL_ITLB_SET6_E2_PPN_RESET		0x40000000
#define XCHAL_ITLB_SET6_E3_PPN_RESET		0x60000000
#define XCHAL_ITLB_SET6_E4_PPN_RESET		0x80000000
#define XCHAL_ITLB_SET6_E5_PPN_RESET		0xA0000000
#define XCHAL_ITLB_SET6_E6_PPN_RESET		0xC0000000
#define XCHAL_ITLB_SET6_E7_PPN_RESET		0xE0000000
/*  Reset CA values for each entry of ITLB way set 6 (because SET6_CA_RESET is non-zero):  */
#define XCHAL_ITLB_SET6_E0_CA_RESET		0x03
#define XCHAL_ITLB_SET6_E1_CA_RESET		0x03
#define XCHAL_ITLB_SET6_E2_CA_RESET		0x03
#define XCHAL_ITLB_SET6_E3_CA_RESET		0x03
#define XCHAL_ITLB_SET6_E4_CA_RESET		0x03
#define XCHAL_ITLB_SET6_E5_CA_RESET		0x03
#define XCHAL_ITLB_SET6_E6_CA_RESET		0x03
#define XCHAL_ITLB_SET6_E7_CA_RESET		0x03


/***  Data TLB:  ***/

#define XCHAL_DTLB_WAY_BITS		4	/* number of bits holding the ways */
#define XCHAL_DTLB_WAYS			10	/* number of ways (n-way set-associative TLB) */
#define XCHAL_DTLB_ARF_WAYS		4	/* number of auto-refill ways */
#define XCHAL_DTLB_SETS			10	/* number of sets (groups of ways with identical settings) */

/*  Way set to which each way belongs:  */
#define XCHAL_DTLB_WAY0_SET		0
#define XCHAL_DTLB_WAY1_SET		1
#define XCHAL_DTLB_WAY2_SET		2
#define XCHAL_DTLB_WAY3_SET		3
#define XCHAL_DTLB_WAY4_SET		4
#define XCHAL_DTLB_WAY5_SET		5
#define XCHAL_DTLB_WAY6_SET		6
#define XCHAL_DTLB_WAY7_SET		7
#define XCHAL_DTLB_WAY8_SET		8
#define XCHAL_DTLB_WAY9_SET		9

/*  Ways sets that are used by hardware auto-refill (ARF):  */
#define XCHAL_DTLB_ARF_SETS		4	/* number of auto-refill sets */
#define XCHAL_DTLB_ARF_SET0		0	/* index of n'th auto-refill set */
#define XCHAL_DTLB_ARF_SET1		1	/* index of n'th auto-refill set */
#define XCHAL_DTLB_ARF_SET2		2	/* index of n'th auto-refill set */
#define XCHAL_DTLB_ARF_SET3		3	/* index of n'th auto-refill set */

/*  Way sets that are "min-wired" (see terminology comment above):  */
#define XCHAL_DTLB_MINWIRED_SETS	3	/* number of "min-wired" sets */
#define XCHAL_DTLB_MINWIRED_SET0	7	/* index of n'th "min-wired" set */
#define XCHAL_DTLB_MINWIRED_SET1	8	/* index of n'th "min-wired" set */
#define XCHAL_DTLB_MINWIRED_SET2	9	/* index of n'th "min-wired" set */


/*  DTLB way set 0 (group of ways 0 thru 0):  */
#define XCHAL_DTLB_SET0_WAY			0	/* index of first way in this way set */
#define XCHAL_DTLB_SET0_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET0_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET0_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET0_ARF			1	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET0_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET0_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET0_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET0_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET0_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET0_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET0_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET0_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET0_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET0_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET0_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET0_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET0_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  DTLB way set 1 (group of ways 1 thru 1):  */
#define XCHAL_DTLB_SET1_WAY			1	/* index of first way in this way set */
#define XCHAL_DTLB_SET1_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET1_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET1_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET1_ARF			1	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET1_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET1_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET1_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET1_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET1_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET1_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET1_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET1_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET1_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET1_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET1_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET1_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET1_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  DTLB way set 2 (group of ways 2 thru 2):  */
#define XCHAL_DTLB_SET2_WAY			2	/* index of first way in this way set */
#define XCHAL_DTLB_SET2_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET2_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET2_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET2_ARF			1	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET2_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET2_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET2_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET2_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET2_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET2_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET2_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET2_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET2_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET2_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET2_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET2_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET2_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  DTLB way set 3 (group of ways 3 thru 3):  */
#define XCHAL_DTLB_SET3_WAY			3	/* index of first way in this way set */
#define XCHAL_DTLB_SET3_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET3_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET3_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET3_ARF			1	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET3_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET3_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET3_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET3_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET3_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET3_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET3_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET3_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET3_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET3_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET3_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET3_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET3_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  DTLB way set 4 (group of ways 4 thru 4):  */
#define XCHAL_DTLB_SET4_WAY			4	/* index of first way in this way set */
#define XCHAL_DTLB_SET4_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET4_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET4_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET4_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET4_PAGESIZES		4	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET4_PAGESZ_BITS		2	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET4_PAGESZ_LOG2_MIN		20	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET4_PAGESZ_LOG2_MAX		26	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET4_PAGESZ_LOG2_LIST	20 XCHAL_SEP 22 XCHAL_SEP 24 XCHAL_SEP 26	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET4_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET4_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET4_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET4_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET4_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET4_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET4_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET4_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  DTLB way set 5 (group of ways 5 thru 5):  */
#define XCHAL_DTLB_SET5_WAY			5	/* index of first way in this way set */
#define XCHAL_DTLB_SET5_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET5_ENTRIES_LOG2		2	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET5_ENTRIES			4	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET5_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET5_PAGESIZES		2	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET5_PAGESZ_BITS		1	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET5_PAGESZ_LOG2_MIN		27	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET5_PAGESZ_LOG2_MAX		28	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET5_PAGESZ_LOG2_LIST	27 XCHAL_SEP 28	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET5_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET5_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET5_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET5_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET5_ASID_RESET		1	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET5_VPN_RESET		1	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET5_PPN_RESET		1	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET5_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */
/*  Reset ASID values for each entry of DTLB way set 5 (because SET5_ASID_RESET is non-zero):  */
#define XCHAL_DTLB_SET5_E0_ASID_RESET		0x00
#define XCHAL_DTLB_SET5_E1_ASID_RESET		0x00
#define XCHAL_DTLB_SET5_E2_ASID_RESET		0x00
#define XCHAL_DTLB_SET5_E3_ASID_RESET		0x00
/*  Reset VPN values for each entry of DTLB way set 5 (because SET5_VPN_RESET is non-zero):  */
#define XCHAL_DTLB_SET5_E0_VPN_RESET		0x00000000
#define XCHAL_DTLB_SET5_E1_VPN_RESET		0x00000000
#define XCHAL_DTLB_SET5_E2_VPN_RESET		0x00000000
#define XCHAL_DTLB_SET5_E3_VPN_RESET		0x00000000
/*  Reset PPN values for each entry of DTLB way set 5 (because SET5_PPN_RESET is non-zero):  */
#define XCHAL_DTLB_SET5_E0_PPN_RESET		0x00000000
#define XCHAL_DTLB_SET5_E1_PPN_RESET		0x00000000
#define XCHAL_DTLB_SET5_E2_PPN_RESET		0x00000000
#define XCHAL_DTLB_SET5_E3_PPN_RESET		0x00000000

/*  DTLB way set 6 (group of ways 6 thru 6):  */
#define XCHAL_DTLB_SET6_WAY			6	/* index of first way in this way set */
#define XCHAL_DTLB_SET6_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET6_ENTRIES_LOG2		3	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET6_ENTRIES			8	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET6_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET6_PAGESIZES		2	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET6_PAGESZ_BITS		1	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET6_PAGESZ_LOG2_MIN		28	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET6_PAGESZ_LOG2_MAX		29	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET6_PAGESZ_LOG2_LIST	29 XCHAL_SEP 28	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET6_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET6_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET6_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET6_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET6_ASID_RESET		1	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET6_VPN_RESET		1	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET6_PPN_RESET		1	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET6_CA_RESET		1	/* 1 if CA reset values defined (and all writable); 0 otherwise */
/*  Reset ASID values for each entry of DTLB way set 6 (because SET6_ASID_RESET is non-zero):  */
#define XCHAL_DTLB_SET6_E0_ASID_RESET		0x01
#define XCHAL_DTLB_SET6_E1_ASID_RESET		0x01
#define XCHAL_DTLB_SET6_E2_ASID_RESET		0x01
#define XCHAL_DTLB_SET6_E3_ASID_RESET		0x01
#define XCHAL_DTLB_SET6_E4_ASID_RESET		0x01
#define XCHAL_DTLB_SET6_E5_ASID_RESET		0x01
#define XCHAL_DTLB_SET6_E6_ASID_RESET		0x01
#define XCHAL_DTLB_SET6_E7_ASID_RESET		0x01
/*  Reset VPN values for each entry of DTLB way set 6 (because SET6_VPN_RESET is non-zero):  */
#define XCHAL_DTLB_SET6_E0_VPN_RESET		0x00000000
#define XCHAL_DTLB_SET6_E1_VPN_RESET		0x20000000
#define XCHAL_DTLB_SET6_E2_VPN_RESET		0x40000000
#define XCHAL_DTLB_SET6_E3_VPN_RESET		0x60000000
#define XCHAL_DTLB_SET6_E4_VPN_RESET		0x80000000
#define XCHAL_DTLB_SET6_E5_VPN_RESET		0xA0000000
#define XCHAL_DTLB_SET6_E6_VPN_RESET		0xC0000000
#define XCHAL_DTLB_SET6_E7_VPN_RESET		0xE0000000
/*  Reset PPN values for each entry of DTLB way set 6 (because SET6_PPN_RESET is non-zero):  */
#define XCHAL_DTLB_SET6_E0_PPN_RESET		0x00000000
#define XCHAL_DTLB_SET6_E1_PPN_RESET		0x20000000
#define XCHAL_DTLB_SET6_E2_PPN_RESET		0x40000000
#define XCHAL_DTLB_SET6_E3_PPN_RESET		0x60000000
#define XCHAL_DTLB_SET6_E4_PPN_RESET		0x80000000
#define XCHAL_DTLB_SET6_E5_PPN_RESET		0xA0000000
#define XCHAL_DTLB_SET6_E6_PPN_RESET		0xC0000000
#define XCHAL_DTLB_SET6_E7_PPN_RESET		0xE0000000
/*  Reset CA values for each entry of DTLB way set 6 (because SET6_CA_RESET is non-zero):  */
#define XCHAL_DTLB_SET6_E0_CA_RESET		0x03
#define XCHAL_DTLB_SET6_E1_CA_RESET		0x03
#define XCHAL_DTLB_SET6_E2_CA_RESET		0x03
#define XCHAL_DTLB_SET6_E3_CA_RESET		0x03
#define XCHAL_DTLB_SET6_E4_CA_RESET		0x03
#define XCHAL_DTLB_SET6_E5_CA_RESET		0x03
#define XCHAL_DTLB_SET6_E6_CA_RESET		0x03
#define XCHAL_DTLB_SET6_E7_CA_RESET		0x03

/*  DTLB way set 7 (group of ways 7 thru 7):  */
#define XCHAL_DTLB_SET7_WAY			7	/* index of first way in this way set */
#define XCHAL_DTLB_SET7_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET7_ENTRIES_LOG2		0	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET7_ENTRIES			1	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET7_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET7_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET7_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET7_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET7_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET7_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET7_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET7_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET7_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET7_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET7_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET7_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET7_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET7_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  DTLB way set 8 (group of ways 8 thru 8):  */
#define XCHAL_DTLB_SET8_WAY			8	/* index of first way in this way set */
#define XCHAL_DTLB_SET8_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET8_ENTRIES_LOG2		0	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET8_ENTRIES			1	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET8_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET8_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET8_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET8_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET8_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET8_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET8_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET8_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET8_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET8_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET8_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET8_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET8_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET8_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */

/*  DTLB way set 9 (group of ways 9 thru 9):  */
#define XCHAL_DTLB_SET9_WAY			9	/* index of first way in this way set */
#define XCHAL_DTLB_SET9_WAYS			1	/* number of (contiguous) ways in this way set */
#define XCHAL_DTLB_SET9_ENTRIES_LOG2		0	/* log2(number of entries in this way) */
#define XCHAL_DTLB_SET9_ENTRIES			1	/* number of entries in this way (always a power of 2) */
#define XCHAL_DTLB_SET9_ARF			0	/* 1=autorefill by h/w, 0=non-autorefill (wired/constant/static) */
#define XCHAL_DTLB_SET9_PAGESIZES		1	/* number of supported page sizes in this way */
#define XCHAL_DTLB_SET9_PAGESZ_BITS		0	/* number of bits to encode the page size */
#define XCHAL_DTLB_SET9_PAGESZ_LOG2_MIN		12	/* log2(minimum supported page size) */
#define XCHAL_DTLB_SET9_PAGESZ_LOG2_MAX		12	/* log2(maximum supported page size) */
#define XCHAL_DTLB_SET9_PAGESZ_LOG2_LIST	12	/* list of log2(page size)s, separated by XCHAL_SEP;
							   2^PAGESZ_BITS entries in list, unsupported entries are zero */
#define XCHAL_DTLB_SET9_ASID_CONSTMASK		0	/* constant ASID bits; 0 if all writable */
#define XCHAL_DTLB_SET9_VPN_CONSTMASK		0	/* constant VPN bits, not including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET9_PPN_CONSTMASK		0	/* constant PPN bits, including entry index bits; 0 if all writable */
#define XCHAL_DTLB_SET9_CA_CONSTMASK		0	/* constant CA bits; 0 if all writable */
#define XCHAL_DTLB_SET9_ASID_RESET		0	/* 1 if ASID reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET9_VPN_RESET		0	/* 1 if VPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET9_PPN_RESET		0	/* 1 if PPN reset values defined (and all writable); 0 otherwise */
#define XCHAL_DTLB_SET9_CA_RESET		0	/* 1 if CA reset values defined (and all writable); 0 otherwise */




#endif /* XTENSA_CONFIG_CORE_MATMAP_H */

