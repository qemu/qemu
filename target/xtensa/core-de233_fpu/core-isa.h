/* 
 * xtensa/config/core-isa.h -- HAL definitions that are dependent on Xtensa
 *				processor CORE configuration
 *
 *  See <xtensa/config/core.h>, which includes this file, for more details.
 */

/* Xtensa processor core configuration information.

   Copyright (c) 1999-2020 Tensilica Inc.

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

#ifndef XTENSA_CORE_CONFIGURATION_H_
#define XTENSA_CORE_CONFIGURATION_H_

//depot/dev/Homewood/Xtensa/SWConfig/hal/core-common.h.tph#24 - edit change 444323 (text+ko)

/****************************************************************************
	    Parameters Useful for Any Code, USER or PRIVILEGED
 ****************************************************************************/

/*
 *  Note:  Macros of the form XCHAL_HAVE_*** have a value of 1 if the option is
 *  configured, and a value of 0 otherwise.  These macros are always defined.
 */


/*----------------------------------------------------------------------
				ISA
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_BE			0	/* big-endian byte ordering */
#define XCHAL_HAVE_WINDOWED		1	/* windowed registers option */
#define XCHAL_NUM_AREGS			32	/* num of physical addr regs */
#define XCHAL_NUM_AREGS_LOG2		5	/* log2(XCHAL_NUM_AREGS) */
#define XCHAL_MAX_INSTRUCTION_SIZE	3	/* max instr bytes (3..8) */
#define XCHAL_HAVE_DEBUG		1	/* debug option */
#define XCHAL_HAVE_DENSITY		1	/* 16-bit instructions */
#define XCHAL_HAVE_LOOPS		1	/* zero-overhead loops */
#define XCHAL_LOOP_BUFFER_SIZE		0	/* zero-ov. loop instr buffer size */
#define XCHAL_HAVE_NSA			1	/* NSA/NSAU instructions */
#define XCHAL_HAVE_MINMAX		1	/* MIN/MAX instructions */
#define XCHAL_HAVE_SEXT			1	/* SEXT instruction */
#define XCHAL_HAVE_DEPBITS		0	/* DEPBITS instruction */
#define XCHAL_HAVE_CLAMPS		1	/* CLAMPS instruction */
#define XCHAL_HAVE_MUL16		1	/* MUL16S/MUL16U instructions */
#define XCHAL_HAVE_MUL32		1	/* MULL instruction */
#define XCHAL_HAVE_MUL32_HIGH		0	/* MULUH/MULSH instructions */
#define XCHAL_HAVE_DIV32		1	/* QUOS/QUOU/REMS/REMU instructions */
#define XCHAL_HAVE_L32R			1	/* L32R instruction */
#define XCHAL_HAVE_ABSOLUTE_LITERALS	0	/* non-PC-rel (extended) L32R */
#define XCHAL_HAVE_CONST16		0	/* CONST16 instruction */
#define XCHAL_HAVE_ADDX			1	/* ADDX#/SUBX# instructions */
#define XCHAL_HAVE_EXCLUSIVE            0	/* L32EX/S32EX instructions */
#define XCHAL_HAVE_WIDE_BRANCHES	0	/* B*.W18 or B*.W15 instr's */
#define XCHAL_HAVE_PREDICTED_BRANCHES	0	/* B[EQ/EQZ/NE/NEZ]T instr's */
#define XCHAL_HAVE_CALL4AND12		1	/* (obsolete option) */
#define XCHAL_HAVE_ABS			1	/* ABS instruction */
#define XCHAL_HAVE_RELEASE_SYNC		1	/* L32AI/S32RI instructions */
#define XCHAL_HAVE_S32C1I		1	/* S32C1I instruction */
#define XCHAL_HAVE_SPECULATION		0	/* speculation */
#define XCHAL_HAVE_FULL_RESET		1	/* all regs/state reset */
#define XCHAL_NUM_CONTEXTS		1	/* */
#define XCHAL_NUM_MISC_REGS		2	/* num of scratch regs (0..4) */
#define XCHAL_HAVE_TAP_MASTER		0	/* JTAG TAP control instr's */
#define XCHAL_HAVE_PRID			1	/* processor ID register */
#define XCHAL_HAVE_EXTERN_REGS		1	/* WER/RER instructions */
#define XCHAL_HAVE_MX			0	/* MX core (Tensilica internal) */
#define XCHAL_HAVE_MP_INTERRUPTS	0	/* interrupt distributor port */
#define XCHAL_HAVE_MP_RUNSTALL		0	/* core RunStall control port */
#define XCHAL_HAVE_PSO			0	/* Power Shut-Off */
#define XCHAL_HAVE_PSO_CDM		0	/* core/debug/mem pwr domains */
#define XCHAL_HAVE_PSO_FULL_RETENTION	0	/* all regs preserved on PSO */
#define XCHAL_HAVE_THREADPTR		1	/* THREADPTR register */
#define XCHAL_HAVE_BOOLEANS		1	/* boolean registers */
#define XCHAL_HAVE_CP			1	/* CPENABLE reg (coprocessor) */
#define XCHAL_CP_MAXCFG			8	/* max allowed cp id plus one */
#define XCHAL_HAVE_MAC16		1	/* MAC16 package */
#define XCHAL_HAVE_LX			1	/* LX core */
#define XCHAL_HAVE_NX			0	/* NX core (starting RH) */

#define XCHAL_HAVE_SUPERGATHER		0	/* SuperGather */

#define XCHAL_HAVE_FUSION		0	/* Fusion*/
#define XCHAL_HAVE_FUSION_FP		0	/* Fusion FP option */
#define XCHAL_HAVE_FUSION_LOW_POWER	0	/* Fusion Low Power option */
#define XCHAL_HAVE_FUSION_AES		0	/* Fusion BLE/Wifi AES-128 CCM option */
#define XCHAL_HAVE_FUSION_CONVENC	0	/* Fusion Conv Encode option */
#define XCHAL_HAVE_FUSION_LFSR_CRC	0	/* Fusion LFSR-CRC option */
#define XCHAL_HAVE_FUSION_BITOPS	0	/* Fusion Bit Operations Support option */
#define XCHAL_HAVE_FUSION_AVS		0	/* Fusion AVS option */
#define XCHAL_HAVE_FUSION_16BIT_BASEBAND 0	/* Fusion 16-bit Baseband option */
#define XCHAL_HAVE_FUSION_VITERBI	0	/* Fusion Viterbi option */
#define XCHAL_HAVE_FUSION_SOFTDEMAP	0	/* Fusion Soft Bit Demap option */
#define XCHAL_HAVE_HIFIPRO		0	/* HiFiPro Audio Engine pkg */
#define XCHAL_HAVE_HIFI5		0	/* HiFi5 Audio Engine pkg */
#define XCHAL_HAVE_HIFI5_NN_MAC		0	/* HiFi5 Audio Engine NN-MAC option */
#define XCHAL_HAVE_HIFI5_VFPU		0	/* HiFi5 Audio Engine Single-Precision VFPU option */
#define XCHAL_HAVE_HIFI5_HP_VFPU	0	/* HiFi5 Audio Engine Half-Precision VFPU option */
#define XCHAL_HAVE_HIFI4		0	/* HiFi4 Audio Engine pkg */
#define XCHAL_HAVE_HIFI4_VFPU		0	/* HiFi4 Audio Engine VFPU option */
#define XCHAL_HAVE_HIFI3		0	/* HiFi3 Audio Engine pkg */
#define XCHAL_HAVE_HIFI3_VFPU		0	/* HiFi3 Audio Engine VFPU option */
#define XCHAL_HAVE_HIFI3Z		0	/* HiFi3Z Audio Engine pkg */
#define XCHAL_HAVE_HIFI3Z_VFPU	0	/* HiFi3Z Audio Engine VFPU option */
#define XCHAL_HAVE_HIFI2		0	/* HiFi2 Audio Engine pkg */
#define XCHAL_HAVE_HIFI2EP		0	/* HiFi2EP */
#define XCHAL_HAVE_HIFI_MINI		0	



#define XCHAL_HAVE_VECTORFPU2005	0	/* vector floating-point pkg */
#define XCHAL_HAVE_USER_DPFPU		0       /* user DP floating-point pkg */
#define XCHAL_HAVE_USER_SPFPU		0       /* user SP floating-point pkg */
#define XCHAL_HAVE_FP			1	/* single prec floating point */
#define XCHAL_HAVE_FP_DIV		1	/* FP with DIV instructions */
#define XCHAL_HAVE_FP_RECIP		1	/* FP with RECIP instructions */
#define XCHAL_HAVE_FP_SQRT		1	/* FP with SQRT instructions */
#define XCHAL_HAVE_FP_RSQRT		1	/* FP with RSQRT instructions */
#define XCHAL_HAVE_DFP			1	/* double precision FP pkg */
#define XCHAL_HAVE_DFP_DIV		1	/* DFP with DIV instructions */
#define XCHAL_HAVE_DFP_RECIP		1	/* DFP with RECIP instructions*/
#define XCHAL_HAVE_DFP_SQRT		1	/* DFP with SQRT instructions */
#define XCHAL_HAVE_DFP_RSQRT		1	/* DFP with RSQRT instructions*/
#define XCHAL_HAVE_DFP_ACCEL		0	/* double precision FP acceleration pkg */
#define XCHAL_HAVE_DFP_accel		XCHAL_HAVE_DFP_ACCEL	/* for backward compatibility */

#define XCHAL_HAVE_DFPU_SINGLE_ONLY	0	/* DFPU Coprocessor, single precision only */
#define XCHAL_HAVE_DFPU_SINGLE_DOUBLE	1	/* DFPU Coprocessor, single and double precision */
#define XCHAL_HAVE_VECTRA1		0	/* Vectra I  pkg */
#define XCHAL_HAVE_VECTRALX		0	/* Vectra LX pkg */

#define XCHAL_HAVE_FUSIONG		0    /* FusionG */
#define XCHAL_HAVE_FUSIONG3		0    /* FusionG3 */
#define XCHAL_HAVE_FUSIONG6		0    /* FusionG6 */
#define XCHAL_HAVE_FUSIONG_SP_VFPU	0    /* sp_vfpu option on FusionG */
#define XCHAL_HAVE_FUSIONG_DP_VFPU	0    /* dp_vfpu option on FusionG */
#define XCHAL_FUSIONG_SIMD32		0    /* simd32 for FusionG */

#define XCHAL_HAVE_FUSIONJ		0    /* FusionJ */
#define XCHAL_HAVE_FUSIONJ6		0    /* FusionJ6 */
#define XCHAL_HAVE_FUSIONJ_SP_VFPU	0    /* sp_vfpu option on FusionJ */
#define XCHAL_HAVE_FUSIONJ_DP_VFPU	0    /* dp_vfpu option on FusionJ */
#define XCHAL_FUSIONJ_SIMD32		0    /* simd32 for FusionJ */

#define XCHAL_HAVE_PDX			0    /* PDX-LX */
#define XCHAL_PDX_SIMD32		0    /* simd32 for PDX */
#define XCHAL_HAVE_PDX4			0    /* PDX4-LX */
#define XCHAL_HAVE_PDX8			0    /* PDX8-LX */
#define XCHAL_HAVE_PDX16		0    /* PDX16-LX */
#define XCHAL_HAVE_PDXNX 		0    /* PDX-NX */

#define XCHAL_HAVE_CONNXD2		0	/* ConnX D2 pkg */
#define XCHAL_HAVE_CONNXD2_DUALLSFLIX   0	/* ConnX D2 & Dual LoadStore Flix */
#define XCHAL_HAVE_BALL			0
#define XCHAL_HAVE_BALLAP		0
#define XCHAL_HAVE_BBE16		0	/* ConnX BBE16 pkg */
#define XCHAL_HAVE_BBE16_RSQRT		0	/* BBE16 & vector recip sqrt */
#define XCHAL_HAVE_BBE16_VECDIV		0	/* BBE16 & vector divide */
#define XCHAL_HAVE_BBE16_DESPREAD	0	/* BBE16 & despread */
#define XCHAL_HAVE_CONNX_B10            0    /* ConnX B10 pkg*/
#define XCHAL_HAVE_CONNX_B20            0    /* ConnX B20 pkg*/
#define XCHAL_HAVE_CONNX_B_SP_VFPU      0    /* Single-precision Vector Floating-point option on ConnX B10 & B20 */
#define XCHAL_HAVE_CONNX_B_SPX_VFPU     0    /* Single-precision Extended Vector Floating-point option on ConnX B10 & B20 */
#define XCHAL_HAVE_CONNX_B_HPX_VFPU     0    /* Half-precision Extended Vector Floating-point option on ConnX B10 & B20 */
#define XCHAL_HAVE_CONNX_B_32B_MAC      0    /* 32-bit vector MAC (real and complex), FIR & FFT option on ConnX B10 & B20 */
#define XCHAL_HAVE_CONNX_B_VITERBI      0    /* Viterbi option on ConnX B10 & B20 */
#define XCHAL_HAVE_CONNX_B_TURBO        0    /* Turbo option on ConnX B10 & B20 */
#define XCHAL_HAVE_CONNX_B_LDPC         0    /* LDPC option on ConnX B10 & B20 */
#define XCHAL_HAVE_BBENEP		0	/* ConnX BBENEP pkgs */
#define XCHAL_HAVE_BBENEP_SP_VFPU	0       /* sp_vfpu option on BBE-EP */
#define XCHAL_HAVE_BSP3			0	/* ConnX BSP3 pkg */
#define XCHAL_HAVE_BSP3_TRANSPOSE	0	/* BSP3 & transpose32x32 */
#define XCHAL_HAVE_SSP16		0	/* ConnX SSP16 pkg */
#define XCHAL_HAVE_SSP16_VITERBI	0	/* SSP16 & viterbi */
#define XCHAL_HAVE_TURBO16		0	/* ConnX Turbo16 pkg */
#define XCHAL_HAVE_BBP16		0	/* ConnX BBP16 pkg */
#define XCHAL_HAVE_FLIX3		0	/* basic 3-way FLIX option */
#define XCHAL_HAVE_GRIVPEP		0	/* General Release of IVPEP */
#define XCHAL_HAVE_GRIVPEP_HISTOGRAM	0       /* Histogram option on GRIVPEP */

#define XCHAL_HAVE_VISION	        0     /* Vision P5/P6 */
#define XCHAL_VISION_SIMD16             0     /* simd16 for Vision P5/P6 */
#define XCHAL_VISION_TYPE               0     /* Vision P5, P6, Q6 or Q7 */
#define XCHAL_VISION_QUAD_MAC_TYPE      0     /* quad_mac option on Vision P6 */
#define XCHAL_HAVE_VISION_HISTOGRAM     0     /* histogram option on Vision P5/P6 */
#define XCHAL_HAVE_VISION_SP_VFPU       0     /* sp_vfpu option on Vision P5/P6/Q6/Q7 */
#define XCHAL_HAVE_VISION_SP_VFPU_2XFMAC 0     /* sp_vfpu_2xfma option on Vision Q7 */
#define XCHAL_HAVE_VISION_HP_VFPU       0     /* hp_vfpu option on Vision P6/Q6 */
#define XCHAL_HAVE_VISION_HP_VFPU_2XFMAC 0     /* hp_vfpu_2xfma option on Vision Q7 */

#define XCHAL_HAVE_VISIONC	        0     /* Vision C */

#define XCHAL_HAVE_XNNE			0		/* XNNE */

/*----------------------------------------------------------------------
				MISC
  ----------------------------------------------------------------------*/

#define XCHAL_NUM_LOADSTORE_UNITS	1	/* load/store units */
#define XCHAL_NUM_WRITEBUFFER_ENTRIES	8	/* size of write buffer */
#define XCHAL_INST_FETCH_WIDTH		4	/* instr-fetch width in bytes */
#define XCHAL_DATA_WIDTH		8	/* data width in bytes */
#define XCHAL_DATA_PIPE_DELAY		1	/* d-side pipeline delay
						   (1 = 5-stage, 2 = 7-stage) */
#define XCHAL_CLOCK_GATING_GLOBAL	1	/* global clock gating */
#define XCHAL_CLOCK_GATING_FUNCUNIT	1	/* funct. unit clock gating */
/*  In T1050, applies to selected core load and store instructions (see ISA): */
#define XCHAL_UNALIGNED_LOAD_EXCEPTION	1	/* unaligned loads cause exc. */
#define XCHAL_UNALIGNED_STORE_EXCEPTION	1	/* unaligned stores cause exc.*/
#define XCHAL_UNALIGNED_LOAD_HW		0	/* unaligned loads work in hw */
#define XCHAL_UNALIGNED_STORE_HW	0	/* unaligned stores work in hw*/

#define XCHAL_UNIFIED_LOADSTORE         0

#define XCHAL_SW_VERSION		1403000	/* sw version of this header */
#define XCHAL_SW_VERSION_MAJOR		14000	/* major ver# of sw          */
#define XCHAL_SW_VERSION_MINOR		3	/* minor ver# of sw          */
#define XCHAL_SW_VERSION_MICRO		0	/* micro ver# of sw          */
#define XCHAL_SW_MINOR_VERSION		1403000	/* with zeroed micro */
#define XCHAL_SW_MICRO_VERSION		1403000

#define XCHAL_CORE_ID			"DE_233L_FPU"	/* alphanum core name
						   (CoreID) set in the Xtensa
						   Processor Generator */

#define XCHAL_BUILD_UNIQUE_ID		0x000872E0	/* 22-bit sw build ID */

/*
 *  These definitions describe the hardware targeted by this software.
 */
#define XCHAL_HW_CONFIGID0		0xC1039286	/* ConfigID hi 32 bits*/
#define XCHAL_HW_CONFIGID1		0x28C872E0	/* ConfigID lo 32 bits*/
#define XCHAL_HW_VERSION_NAME		"LX7.1.3"	/* full version name */
#define XCHAL_HW_VERSION_MAJOR		2810	/* major ver# of targeted hw */
#define XCHAL_HW_VERSION_MINOR		3	/* minor ver# of targeted hw */
#define XCHAL_HW_VERSION_MICRO		0	/* subdot ver# of targeted hw */
#define XCHAL_HW_VERSION		281030	/* major*100+(major<2810 ? minor : minor*10+micro) */
#define XCHAL_HW_REL_LX7		1
#define XCHAL_HW_REL_LX7_1		1
#define XCHAL_HW_REL_LX7_1_3		1
#define XCHAL_HW_CONFIGID_RELIABLE	1
/*  If software targets a *range* of hardware versions, these are the bounds: */
#define XCHAL_HW_MIN_VERSION_MAJOR	2810	/* major v of earliest tgt hw */
#define XCHAL_HW_MIN_VERSION_MINOR	3	/* minor v of earliest tgt hw */
#define XCHAL_HW_MIN_VERSION_MICRO	0	/* micro v of earliest tgt hw */
#define XCHAL_HW_MIN_VERSION		281030	/* earliest targeted hw */
#define XCHAL_HW_MAX_VERSION_MAJOR	2810	/* major v of latest tgt hw */
#define XCHAL_HW_MAX_VERSION_MINOR	3	/* minor v of latest tgt hw */
#define XCHAL_HW_MAX_VERSION_MICRO	0	/* micro v of latest tgt hw */
#define XCHAL_HW_MAX_VERSION		281030	/* latest targeted hw */

/*  Config is enabled for functional safety: */
#define XCHAL_HAVE_FUNC_SAFETY		0

#define XCHAL_HAVE_APB			0 

/*----------------------------------------------------------------------
				CACHE
  ----------------------------------------------------------------------*/

#define XCHAL_ICACHE_LINESIZE		32	/* I-cache line size in bytes */
#define XCHAL_DCACHE_LINESIZE		32	/* D-cache line size in bytes */
#define XCHAL_ICACHE_LINEWIDTH		5	/* log2(I line size in bytes) */
#define XCHAL_DCACHE_LINEWIDTH		5	/* log2(D line size in bytes) */

#define XCHAL_ICACHE_SIZE		16384	/* I-cache size in bytes or 0 */
#define XCHAL_ICACHE_SIZE_LOG2		14
#define XCHAL_DCACHE_SIZE		16384	/* D-cache size in bytes or 0 */
#define XCHAL_DCACHE_SIZE_LOG2		14

#define XCHAL_DCACHE_IS_WRITEBACK	1	/* writeback feature */
#define XCHAL_DCACHE_IS_COHERENT	0	/* MP coherence feature */

#define XCHAL_HAVE_PREFETCH		0	/* PREFCTL register */
#define XCHAL_HAVE_PREFETCH_L1		0	/* prefetch to L1 cache */
#define XCHAL_PREFETCH_CASTOUT_LINES	0	/* dcache pref. castout bufsz */
#define XCHAL_PREFETCH_ENTRIES		0	/* cache prefetch entries */
#define XCHAL_PREFETCH_BLOCK_ENTRIES	0	/* prefetch block streams */
#define XCHAL_HAVE_CACHE_BLOCKOPS	0	/* block prefetch for caches */
#define XCHAL_HAVE_CME_DOWNGRADES	0
#define XCHAL_HAVE_ICACHE_TEST		1	/* Icache test instructions */
#define XCHAL_HAVE_DCACHE_TEST		1	/* Dcache test instructions */
#define XCHAL_HAVE_ICACHE_DYN_WAYS	0	/* Icache dynamic way support */
#define XCHAL_HAVE_DCACHE_DYN_WAYS	0	/* Dcache dynamic way support */
#define XCHAL_HAVE_ICACHE_DYN_ENABLE	0	/* Icache enabled via MEMCTL */
#define XCHAL_HAVE_DCACHE_DYN_ENABLE	0	/* Dcache enabled via MEMCTL */

#define XCHAL_L1SCACHE_SIZE		         0
#define XCHAL_L1SCACHE_SIZE_LOG2		 0
#define XCHAL_L1SCACHE_WAYS              1
#define XCHAL_L1SCACHE_WAYS_LOG2         0
#define XCHAL_L1SCACHE_ACCESS_SIZE       0
#define XCHAL_L1SCACHE_BANKS             1

#define XCHAL_HAVE_L2			0	/* NX L2 cache controller */

/*  Number of cores in cluster */
#if XCHAL_HAVE_L2
#define XCHAL_NUM_CORES_IN_CLUSTER	XCHAL_L2CC_NUM_CORES_LOG2
#else
#define XCHAL_NUM_CORES_IN_CLUSTER	0
#endif

/* PRID_ID macros are for internal use only ... subject to removal */
#define PRID_ID_SHIFT           0
#define PRID_ID_BITS            4
#define PRID_ID_MASK            0x0000000F

/*  This one is a form of caching, though not architecturally visible:  */
#define XCHAL_HAVE_BRANCH_PREDICTION	0	/* branch [target] prediction */




/****************************************************************************
    Parameters Useful for PRIVILEGED (Supervisory or Non-Virtualized) Code
 ****************************************************************************/


#ifndef XTENSA_HAL_NON_PRIVILEGED_ONLY

/*----------------------------------------------------------------------
				CACHE
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_PIF			1	/* any outbound bus present */

#define XCHAL_HAVE_AXI			0	/* AXI bus */
#define XCHAL_HAVE_AXI_ECC		0	/* ECC on AXI bus */
#define XCHAL_HAVE_ACELITE		0	/* ACELite bus */

#define XCHAL_HAVE_PIF_WR_RESP			0	/* pif write response */
#define XCHAL_HAVE_PIF_REQ_ATTR			0	/* pif attribute */

/*  If present, cache size in bytes == (ways * 2^(linewidth + setwidth)).  */

/*  Number of cache sets in log2(lines per way):  */
#define XCHAL_ICACHE_SETWIDTH		7
#define XCHAL_DCACHE_SETWIDTH		7

/*  Cache set associativity (number of ways):  */
#define XCHAL_ICACHE_WAYS		4
#define XCHAL_ICACHE_WAYS_LOG2		2
#define XCHAL_DCACHE_WAYS		4
#define XCHAL_DCACHE_WAYS_LOG2		2

/*  Cache features:  */
#define XCHAL_ICACHE_LINE_LOCKABLE	1
#define XCHAL_DCACHE_LINE_LOCKABLE	1
#define XCHAL_ICACHE_ECC_PARITY		0
#define XCHAL_DCACHE_ECC_PARITY		0
#define XCHAL_ICACHE_ECC_WIDTH		4
#define XCHAL_DCACHE_ECC_WIDTH		1

/*  Cache access size in bytes (affects operation of SICW instruction):  */
#define XCHAL_ICACHE_ACCESS_SIZE	4
#define XCHAL_DCACHE_ACCESS_SIZE	8

#define XCHAL_DCACHE_BANKS		1	/* number of banks */

/* The number of Cache lines associated with a single cache tag */
#define XCHAL_DCACHE_LINES_PER_TAG_LOG2	0

/*  Number of encoded cache attr bits (see <xtensa/hal.h> for decoded bits):  */
#define XCHAL_CA_BITS			4


/*----------------------------------------------------------------------
			INTERNAL I/D RAM/ROMs and XLMI
  ----------------------------------------------------------------------*/
#define XCHAL_NUM_INSTROM		0	/* number of core instr. ROMs */
#define XCHAL_NUM_INSTRAM		0	/* number of core instr. RAMs */
#define XCHAL_NUM_DATAROM		0	/* number of core data ROMs */
#define XCHAL_NUM_DATARAM		0	/* number of core data RAMs */
#define XCHAL_NUM_URAM			0	/* number of core unified RAMs*/
#define XCHAL_NUM_XLMI			0	/* number of core XLMI ports */
#define	XCHAL_HAVE_IRAMCFG		0	/* IRAMxCFG register present */
#define	XCHAL_HAVE_DRAMCFG		0	/* DRAMxCFG register present */


#define XCHAL_HAVE_IDMA			0


#define XCHAL_HAVE_IMEM_LOADSTORE	1	/* can load/store to IROM/IRAM*/

/*----------------------------------------------------------------------
			INTERRUPTS and TIMERS
  ----------------------------------------------------------------------*/

#define XCHAL_HAVE_INTERRUPTS		1	/* interrupt option */
#define XCHAL_HAVE_NMI			1	/* non-maskable interrupt */
#define XCHAL_HAVE_CCOUNT		1	/* CCOUNT reg. (timer option) */
#define XCHAL_NUM_TIMERS		3	/* number of CCOMPAREn regs */
#define XCHAL_NUM_INTERRUPTS		22	/* number of interrupts */
#define XCHAL_NUM_INTERRUPTS_LOG2	5	/* ceil(log2(NUM_INTERRUPTS)) */
#define XCHAL_NUM_EXTINTERRUPTS		17	/* num of external interrupts */
#define XCHAL_NUM_INTLEVELS		6	/* number of interrupt levels
						   (not including level zero) */


#define XCHAL_HAVE_HIGHPRI_INTERRUPTS	1   /* med/high-pri. interrupts */
#define XCHAL_EXCM_LEVEL		3	/* level masked by PS.EXCM */
	/* (always 1 in XEA1; levels 2 .. EXCM_LEVEL are "medium priority") */

/*  Masks of interrupts at each interrupt level:  */
#define XCHAL_INTLEVEL1_MASK		0x001F80FF
#define XCHAL_INTLEVEL2_MASK		0x00000100
#define XCHAL_INTLEVEL3_MASK		0x00200E00
#define XCHAL_INTLEVEL4_MASK		0x00001000
#define XCHAL_INTLEVEL5_MASK		0x00002000
#define XCHAL_INTLEVEL6_MASK		0x00000000
#define XCHAL_INTLEVEL7_MASK		0x00004000

/*  Masks of interrupts at each range 1..n of interrupt levels:  */
#define XCHAL_INTLEVEL1_ANDBELOW_MASK	0x001F80FF
#define XCHAL_INTLEVEL2_ANDBELOW_MASK	0x001F81FF
#define XCHAL_INTLEVEL3_ANDBELOW_MASK	0x003F8FFF
#define XCHAL_INTLEVEL4_ANDBELOW_MASK	0x003F9FFF
#define XCHAL_INTLEVEL5_ANDBELOW_MASK	0x003FBFFF
#define XCHAL_INTLEVEL6_ANDBELOW_MASK	0x003FBFFF
#define XCHAL_INTLEVEL7_ANDBELOW_MASK	0x003FFFFF

/*  Level of each interrupt:  */
#define XCHAL_INT0_LEVEL		1
#define XCHAL_INT1_LEVEL		1
#define XCHAL_INT2_LEVEL		1
#define XCHAL_INT3_LEVEL		1
#define XCHAL_INT4_LEVEL		1
#define XCHAL_INT5_LEVEL		1
#define XCHAL_INT6_LEVEL		1
#define XCHAL_INT7_LEVEL		1
#define XCHAL_INT8_LEVEL		2
#define XCHAL_INT9_LEVEL		3
#define XCHAL_INT10_LEVEL		3
#define XCHAL_INT11_LEVEL		3
#define XCHAL_INT12_LEVEL		4
#define XCHAL_INT13_LEVEL		5
#define XCHAL_INT14_LEVEL		7
#define XCHAL_INT15_LEVEL		1
#define XCHAL_INT16_LEVEL		1
#define XCHAL_INT17_LEVEL		1
#define XCHAL_INT18_LEVEL		1
#define XCHAL_INT19_LEVEL		1
#define XCHAL_INT20_LEVEL		1
#define XCHAL_INT21_LEVEL		3
#define XCHAL_DEBUGLEVEL		6	/* debug interrupt level */
#define XCHAL_HAVE_DEBUG_EXTERN_INT	1	/* OCD external db interrupt */
#define XCHAL_NMILEVEL			7	/* NMI "level" (for use with
						   EXCSAVE/EPS/EPC_n, RFI n) */

/*  Type of each interrupt:  */
#define XCHAL_INT0_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT1_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT2_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT3_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT4_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT5_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT6_TYPE 	XTHAL_INTTYPE_TIMER
#define XCHAL_INT7_TYPE 	XTHAL_INTTYPE_SOFTWARE
#define XCHAL_INT8_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT9_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT10_TYPE 	XTHAL_INTTYPE_TIMER
#define XCHAL_INT11_TYPE 	XTHAL_INTTYPE_SOFTWARE
#define XCHAL_INT12_TYPE 	XTHAL_INTTYPE_EXTERN_LEVEL
#define XCHAL_INT13_TYPE 	XTHAL_INTTYPE_TIMER
#define XCHAL_INT14_TYPE 	XTHAL_INTTYPE_NMI
#define XCHAL_INT15_TYPE 	XTHAL_INTTYPE_EXTERN_EDGE
#define XCHAL_INT16_TYPE 	XTHAL_INTTYPE_EXTERN_EDGE
#define XCHAL_INT17_TYPE 	XTHAL_INTTYPE_EXTERN_EDGE
#define XCHAL_INT18_TYPE 	XTHAL_INTTYPE_EXTERN_EDGE
#define XCHAL_INT19_TYPE 	XTHAL_INTTYPE_EXTERN_EDGE
#define XCHAL_INT20_TYPE 	XTHAL_INTTYPE_EXTERN_EDGE
#define XCHAL_INT21_TYPE 	XTHAL_INTTYPE_EXTERN_EDGE

/*  Masks of interrupts for each type of interrupt:  */
#define XCHAL_INTTYPE_MASK_UNCONFIGURED	0xFFC00000
#define XCHAL_INTTYPE_MASK_EXTERN_LEVEL	0x0000133F
#define XCHAL_INTTYPE_MASK_EXTERN_EDGE	0x003F8000
#define XCHAL_INTTYPE_MASK_NMI		0x00004000
#define XCHAL_INTTYPE_MASK_SOFTWARE	0x00000880
#define XCHAL_INTTYPE_MASK_TIMER	0x00002440
#define XCHAL_INTTYPE_MASK_WRITE_ERROR	0x00000000
#define XCHAL_INTTYPE_MASK_DBG_REQUEST	0x00000000
#define XCHAL_INTTYPE_MASK_BREAKIN	0x00000000
#define XCHAL_INTTYPE_MASK_TRAX		0x00000000
#define XCHAL_INTTYPE_MASK_PROFILING	0x00000000
#define XCHAL_INTTYPE_MASK_IDMA_DONE	0x00000000
#define XCHAL_INTTYPE_MASK_IDMA_ERR	0x00000000
#define XCHAL_INTTYPE_MASK_GS_ERR	0x00000000
#define XCHAL_INTTYPE_MASK_L2_ERR	0x00000000
#define XCHAL_INTTYPE_MASK_L2_STATUS	0x00000000
#define XCHAL_INTTYPE_MASK_COR_ECC_ERR	0x00000000

/*  Interrupt numbers assigned to specific interrupt sources:  */
#define XCHAL_TIMER0_INTERRUPT		6	/* CCOMPARE0 */
#define XCHAL_TIMER1_INTERRUPT		10	/* CCOMPARE1 */
#define XCHAL_TIMER2_INTERRUPT		13	/* CCOMPARE2 */
#define XCHAL_TIMER3_INTERRUPT		XTHAL_TIMER_UNCONFIGURED
#define XCHAL_NMI_INTERRUPT		14	/* non-maskable interrupt */

/*  Interrupt numbers for levels at which only one interrupt is configured:  */
#define XCHAL_INTLEVEL2_NUM		8
#define XCHAL_INTLEVEL4_NUM		12
#define XCHAL_INTLEVEL5_NUM		13
#define XCHAL_INTLEVEL7_NUM		14
/*  (There are many interrupts each at level(s) 1, 3.)  */


/*
 *  External interrupt mapping.
 *  These macros describe how Xtensa processor interrupt numbers
 *  (as numbered internally, eg. in INTERRUPT and INTENABLE registers)
 *  map to external BInterrupt<n> pins, for those interrupts
 *  configured as external (level-triggered, edge-triggered, or NMI).
 *  See the Xtensa processor databook for more details.
 */

/*  Core interrupt numbers mapped to each EXTERNAL BInterrupt pin number:  */
#define XCHAL_EXTINT0_NUM		0	/* (intlevel 1) */
#define XCHAL_EXTINT1_NUM		1	/* (intlevel 1) */
#define XCHAL_EXTINT2_NUM		2	/* (intlevel 1) */
#define XCHAL_EXTINT3_NUM		3	/* (intlevel 1) */
#define XCHAL_EXTINT4_NUM		4	/* (intlevel 1) */
#define XCHAL_EXTINT5_NUM		5	/* (intlevel 1) */
#define XCHAL_EXTINT6_NUM		8	/* (intlevel 2) */
#define XCHAL_EXTINT7_NUM		9	/* (intlevel 3) */
#define XCHAL_EXTINT8_NUM		12	/* (intlevel 4) */
#define XCHAL_EXTINT9_NUM		14	/* (intlevel 7) */
#define XCHAL_EXTINT10_NUM		15	/* (intlevel 1) */
#define XCHAL_EXTINT11_NUM		16	/* (intlevel 1) */
#define XCHAL_EXTINT12_NUM		17	/* (intlevel 1) */
#define XCHAL_EXTINT13_NUM		18	/* (intlevel 1) */
#define XCHAL_EXTINT14_NUM		19	/* (intlevel 1) */
#define XCHAL_EXTINT15_NUM		20	/* (intlevel 1) */
#define XCHAL_EXTINT16_NUM		21	/* (intlevel 3) */
/*  EXTERNAL BInterrupt pin numbers mapped to each core interrupt number:  */
#define XCHAL_INT0_EXTNUM		0	/* (intlevel 1) */
#define XCHAL_INT1_EXTNUM		1	/* (intlevel 1) */
#define XCHAL_INT2_EXTNUM		2	/* (intlevel 1) */
#define XCHAL_INT3_EXTNUM		3	/* (intlevel 1) */
#define XCHAL_INT4_EXTNUM		4	/* (intlevel 1) */
#define XCHAL_INT5_EXTNUM		5	/* (intlevel 1) */
#define XCHAL_INT8_EXTNUM		6	/* (intlevel 2) */
#define XCHAL_INT9_EXTNUM		7	/* (intlevel 3) */
#define XCHAL_INT12_EXTNUM		8	/* (intlevel 4) */
#define XCHAL_INT14_EXTNUM		9	/* (intlevel 7) */
#define XCHAL_INT15_EXTNUM		10	/* (intlevel 1) */
#define XCHAL_INT16_EXTNUM		11	/* (intlevel 1) */
#define XCHAL_INT17_EXTNUM		12	/* (intlevel 1) */
#define XCHAL_INT18_EXTNUM		13	/* (intlevel 1) */
#define XCHAL_INT19_EXTNUM		14	/* (intlevel 1) */
#define XCHAL_INT20_EXTNUM		15	/* (intlevel 1) */
#define XCHAL_INT21_EXTNUM		16	/* (intlevel 3) */

#define	XCHAL_HAVE_ISB			0	/* No ISB */
#define	XCHAL_ISB_VADDR			0	/* N/A    */
#define	XCHAL_HAVE_ITB			0	/* No ITB */
#define	XCHAL_ITB_VADDR			0	/* N/A    */

#define	XCHAL_HAVE_KSL			0	/* Kernel Stack Limit */
#define	XCHAL_HAVE_ISL			0	/* Interrupt Stack Limit */
#define	XCHAL_HAVE_PSL			0	/* Pageable Stack Limit */


/*----------------------------------------------------------------------
			EXCEPTIONS and VECTORS
  ----------------------------------------------------------------------*/

#define XCHAL_XEA_VERSION		2	/* Xtensa Exception Architecture
						   number: 1 == XEA1 (until T1050)
							   2 == XEA2 (T1040 onwards)
							   3 == XEA3 (LX8/NX/SX onwards)
							   0 == XEAX (extern) or TX */
#define XCHAL_HAVE_XEA1			0	/* Exception Architecture 1 */
#define XCHAL_HAVE_XEA2			1	/* Exception Architecture 2 */
#define XCHAL_HAVE_XEA3			0	/* Exception Architecture 3 */
#define XCHAL_HAVE_XEAX			0	/* External Exception Arch. */
#define XCHAL_HAVE_EXCEPTIONS		1	/* exception option */
#define XCHAL_HAVE_IMPRECISE_EXCEPTIONS	0	/* imprecise exception option */
#define	XCHAL_EXCCAUSE_NUM		64	/* Number of exceptions */
#define XCHAL_HAVE_HALT			0	/* halt architecture option */
#define XCHAL_HAVE_BOOTLOADER		0	/* boot loader (for TX) */
#define XCHAL_HAVE_MEM_ECC_PARITY	0	/* local memory ECC/parity */
#define XCHAL_HAVE_VECTOR_SELECT	1	/* relocatable vectors */
#define XCHAL_HAVE_VECBASE		1	/* relocatable vectors */
#define XCHAL_VECBASE_RESET_VADDR	0x00002000  /* VECBASE reset value */
#define XCHAL_VECBASE_RESET_PADDR	0x00002000
#define XCHAL_RESET_VECBASE_OVERLAP	0	/* UNUSED */

#define XCHAL_RESET_VECTOR0_VADDR	0xFE000000
#define XCHAL_RESET_VECTOR0_PADDR	0xFE000000
#define XCHAL_RESET_VECTOR1_VADDR	0x00001000
#define XCHAL_RESET_VECTOR1_PADDR	0x00001000
#define XCHAL_RESET_VECTOR_VADDR	XCHAL_RESET_VECTOR0_VADDR
#define XCHAL_RESET_VECTOR_PADDR	XCHAL_RESET_VECTOR0_PADDR
#define XCHAL_USER_VECOFS		0x00000340
#define XCHAL_USER_VECTOR_VADDR		0x00002340
#define XCHAL_USER_VECTOR_PADDR		0x00002340
#define XCHAL_KERNEL_VECOFS		0x00000300
#define XCHAL_KERNEL_VECTOR_VADDR	0x00002300
#define XCHAL_KERNEL_VECTOR_PADDR	0x00002300
#define XCHAL_DOUBLEEXC_VECOFS		0x000003C0
#define XCHAL_DOUBLEEXC_VECTOR_VADDR	0x000023C0
#define XCHAL_DOUBLEEXC_VECTOR_PADDR	0x000023C0
#define XCHAL_WINDOW_OF4_VECOFS		0x00000000
#define XCHAL_WINDOW_UF4_VECOFS		0x00000040
#define XCHAL_WINDOW_OF8_VECOFS		0x00000080
#define XCHAL_WINDOW_UF8_VECOFS		0x000000C0
#define XCHAL_WINDOW_OF12_VECOFS	0x00000100
#define XCHAL_WINDOW_UF12_VECOFS	0x00000140
#define XCHAL_WINDOW_VECTORS_VADDR	0x00002000
#define XCHAL_WINDOW_VECTORS_PADDR	0x00002000
#define XCHAL_INTLEVEL2_VECOFS		0x00000180
#define XCHAL_INTLEVEL2_VECTOR_VADDR	0x00002180
#define XCHAL_INTLEVEL2_VECTOR_PADDR	0x00002180
#define XCHAL_INTLEVEL3_VECOFS		0x000001C0
#define XCHAL_INTLEVEL3_VECTOR_VADDR	0x000021C0
#define XCHAL_INTLEVEL3_VECTOR_PADDR	0x000021C0
#define XCHAL_INTLEVEL4_VECOFS		0x00000200
#define XCHAL_INTLEVEL4_VECTOR_VADDR	0x00002200
#define XCHAL_INTLEVEL4_VECTOR_PADDR	0x00002200
#define XCHAL_INTLEVEL5_VECOFS		0x00000240
#define XCHAL_INTLEVEL5_VECTOR_VADDR	0x00002240
#define XCHAL_INTLEVEL5_VECTOR_PADDR	0x00002240
#define XCHAL_INTLEVEL6_VECOFS		0x00000280
#define XCHAL_INTLEVEL6_VECTOR_VADDR	0x00002280
#define XCHAL_INTLEVEL6_VECTOR_PADDR	0x00002280
#define XCHAL_DEBUG_VECOFS		XCHAL_INTLEVEL6_VECOFS
#define XCHAL_DEBUG_VECTOR_VADDR	XCHAL_INTLEVEL6_VECTOR_VADDR
#define XCHAL_DEBUG_VECTOR_PADDR	XCHAL_INTLEVEL6_VECTOR_PADDR
#define XCHAL_NMI_VECOFS		0x000002C0
#define XCHAL_NMI_VECTOR_VADDR		0x000022C0
#define XCHAL_NMI_VECTOR_PADDR		0x000022C0
#define XCHAL_INTLEVEL7_VECOFS		XCHAL_NMI_VECOFS
#define XCHAL_INTLEVEL7_VECTOR_VADDR	XCHAL_NMI_VECTOR_VADDR
#define XCHAL_INTLEVEL7_VECTOR_PADDR	XCHAL_NMI_VECTOR_PADDR


/*----------------------------------------------------------------------
				DEBUG MODULE
  ----------------------------------------------------------------------*/

/*  Misc  */
#define XCHAL_HAVE_DEBUG_ERI		0	/* ERI to debug module */
#define XCHAL_HAVE_DEBUG_APB		0	/* APB to debug module */
#define XCHAL_HAVE_DEBUG_JTAG		1	/* JTAG to debug module */

/*  On-Chip Debug (OCD)  */
#define XCHAL_HAVE_OCD			1	/* OnChipDebug option */
#define XCHAL_NUM_IBREAK		2	/* number of IBREAKn regs */
#define XCHAL_NUM_DBREAK		2	/* number of DBREAKn regs */
#define XCHAL_HAVE_OCD_DIR_ARRAY	0	/* faster OCD option (to LX4) */
#define XCHAL_HAVE_OCD_LS32DDR		1	/* L32DDR/S32DDR (faster OCD) */

/*  TRAX (in core)  */
#define XCHAL_HAVE_TRAX			0	/* TRAX in debug module */
#define XCHAL_TRAX_MEM_SIZE		0	/* TRAX memory size in bytes */
#define XCHAL_TRAX_MEM_SHAREABLE	0	/* start/end regs; ready sig. */
#define XCHAL_TRAX_ATB_WIDTH		0	/* ATB width (bits), 0=no ATB */
#define XCHAL_TRAX_TIME_WIDTH		0	/* timestamp bitwidth, 0=none */

/*  Perf counters  */
#define XCHAL_NUM_PERF_COUNTERS		0	/* performance counters */


/*----------------------------------------------------------------------
				MMU
  ----------------------------------------------------------------------*/

/*  See core-matmap.h header file for more details.  */

#define XCHAL_HAVE_TLBS			1	/* inverse of HAVE_CACHEATTR */
#define XCHAL_HAVE_SPANNING_WAY		1	/* one way maps I+D 4GB vaddr */
#define XCHAL_SPANNING_WAY		6	/* TLB spanning way number */
#define XCHAL_HAVE_IDENTITY_MAP		0	/* vaddr == paddr always */
#define XCHAL_HAVE_CACHEATTR		0	/* CACHEATTR register present */
#define XCHAL_HAVE_MIMIC_CACHEATTR	0	/* region protection */
#define XCHAL_HAVE_XLT_CACHEATTR	0	/* region prot. w/translation */
#define XCHAL_HAVE_PTP_MMU		1	/* full MMU (with page table
						   [autorefill] and protection)
						   usable for an MMU-based OS */

/*  If none of the above last 5 are set, it's a custom TLB configuration.  */
#define XCHAL_ITLB_ARF_ENTRIES_LOG2	2	/* log2(autorefill way size) */
#define XCHAL_DTLB_ARF_ENTRIES_LOG2	2	/* log2(autorefill way size) */

#define XCHAL_MMU_ASID_BITS		8	/* number of bits in ASIDs */
#define XCHAL_MMU_RINGS			4	/* number of rings (1..4) */
#define XCHAL_MMU_RING_BITS		2	/* num of bits in RING field */

/*----------------------------------------------------------------------
				MPU
  ----------------------------------------------------------------------*/
#define XCHAL_HAVE_MPU			0 
#define XCHAL_MPU_ENTRIES		0

#define XCHAL_MPU_ALIGN_REQ		1	/* MPU requires alignment of entries to background map */
#define XCHAL_MPU_BACKGROUND_ENTRIES	0	/* number of entries in bg map*/
#define XCHAL_MPU_BG_CACHEADRDIS	0	/* default CACHEADRDIS for bg */
 
#define XCHAL_MPU_ALIGN_BITS		0
#define XCHAL_MPU_ALIGN			0

#endif /* !XTENSA_HAL_NON_PRIVILEGED_ONLY */


#endif /* XTENSA_CORE_CONFIGURATION_H_ */

