/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#include <stdio.h>
#include "qemu/osdep.h"
#include "tcg-op.h"
#include "fma_emu.h"
#include "arch.h"
#include "regs.h"
#include "macros.h"
#include "myfenv.h"

#define thread_t CPUHexagonState
#define env      thread

size1u_t rLPS_table_64x4[64][4] = {
	{128, 176, 208, 240}
	,
	{128, 167, 197, 227}
	,
	{128, 158, 187, 216}
	,
	{123, 150, 178, 205}
	,
	{116, 142, 169, 195}
	,
	{111, 135, 160, 185}
	,
	{105, 128, 152, 175}
	,
	{100, 122, 144, 166}
	,
	{95, 116, 137, 158}
	,
	{90, 110, 130, 150}
	,
	{85, 104, 123, 142}
	,
	{81, 99, 117, 135}
	,
	{77, 94, 111, 128}
	,
	{73, 89, 105, 122}
	,
	{69, 85, 100, 116}
	,
	{66, 80, 95, 110}
	,
	{62, 76, 90, 104}
	,
	{59, 72, 86, 99}
	,
	{56, 69, 81, 94}
	,
	{53, 65, 77, 89}
	,
	{51, 62, 73, 85}
	,
	{48, 59, 69, 80}
	,
	{46, 56, 66, 76}
	,
	{43, 53, 63, 72}
	,
	{41, 50, 59, 69}
	,
	{39, 48, 56, 65}
	,
	{37, 45, 54, 62}
	,
	{35, 43, 51, 59}
	,
	{33, 41, 48, 56}
	,
	{32, 39, 46, 53}
	,
	{30, 37, 43, 50}
	,
	{29, 35, 41, 48}
	,
	{27, 33, 39, 45}
	,
	{26, 31, 37, 43}
	,
	{24, 30, 35, 41}
	,
	{23, 28, 33, 39}
	,
	{22, 27, 32, 37}
	,
	{21, 26, 30, 35}
	,
	{20, 24, 29, 33}
	,
	{19, 23, 27, 31}
	,
	{18, 22, 26, 30}
	,
	{17, 21, 25, 28}
	,
	{16, 20, 23, 27}
	,
	{15, 19, 22, 25}
	,
	{14, 18, 21, 24}
	,
	{14, 17, 20, 23}
	,
	{13, 16, 19, 22}
	,
	{12, 15, 18, 21}
	,
	{12, 14, 17, 20}
	,
	{11, 14, 16, 19}
	,
	{11, 13, 15, 18}
	,
	{10, 12, 15, 17}
	,
	{10, 12, 14, 16}
	,
	{9, 11, 13, 15}
	,
	{9, 11, 12, 14}
	,
	{8, 10, 12, 14}
	,
	{8, 9, 11, 13}
	,
	{7, 9, 11, 12}
	,
	{7, 9, 10, 12}
	,
	{7, 8, 10, 11}
	,
	{6, 8, 9, 11}
	,
	{6, 7, 9, 10}
	,
	{6, 7, 8, 9}
	,
	{2, 2, 2, 2}
};

size1u_t AC_next_state_MPS_64[64] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
	11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
	31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
	51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
	61, 62, 62, 63
};


size1u_t AC_next_state_LPS_64[64] = {
	0, 0, 1, 2, 2, 4, 4, 5, 6, 7,
	8, 9, 9, 11, 11, 12, 13, 13, 15, 15,
	16, 16, 18, 18, 19, 19, 21, 21, 22, 22,
	23, 24, 24, 25, 26, 26, 27, 27, 28, 29,
	29, 30, 30, 30, 31, 32, 32, 33, 33, 33,
	34, 34, 35, 35, 35, 36, 36, 36, 37, 37,
	37, 38, 38, 63
};

size4u_t fbrevaddr(size4u_t pointer)
{
	size4u_t offset = pointer & 0xffff;
	size4u_t brevoffset = 0;
	int i;

	for (i = 0; i < 16; i++) {
		fSETBIT(i, brevoffset, (fGETBIT((15 - i), offset)));
	}
	return ((pointer & 0xffff0000) | brevoffset);
}

/* Counting bits set, Brian Kernighan's way */
/* 
 * Brian Kernighan's method goes through as many iterations as there are set bits. 
 * So if we have a 32-bit word with only the high bit set, 
 * then it will only go once through the loop. 
 */
size4u_t count_ones_2(size2u_t src)
{
	int ret;
	for (ret = 0; src; ret++) {
		src &= src - 1;			/* clear the least significant bit set */
	}
	return ret;
}

 size4u_t count_ones_4(size4u_t src)
{
	int ret;
	for (ret = 0; src; ret++) {
		src &= src - 1;			/* clear the least significant bit set */
	}
	return ret;
}

size4u_t count_ones_8(size8u_t src)
{
	int ret;
	for (ret = 0; src; ret++) {
		src &= src - 1;			/* clear the least significant bit set */
	}
	return ret;
}

size4u_t count_leading_ones_8(size8u_t src)
{
	int ret;
	for (ret = 0; src & 0x8000000000000000LL; src <<= 1) {
		ret++;
	}
	return ret;
}


size4u_t count_leading_ones_4(size4u_t src)
{
	int ret;
	for (ret = 0; src & 0x80000000; src <<= 1) {
		ret++;
	}
	return ret;
}

size4u_t count_leading_ones_2(size2u_t src)
{
	int ret;
	for (ret = 0; src & 0x8000; src <<= 1) {
		ret++;
	}
	return ret;
}

size4u_t count_leading_ones_1(size1u_t src)
{
	int ret;
	for (ret = 0; src & 0x80; src <<= 1) {
		ret++;
	}
	return ret;
}

#define BITS_MASK_8 0x5555555555555555ULL
#define PAIR_MASK_8 0x3333333333333333ULL
#define NYBL_MASK_8 0x0f0f0f0f0f0f0f0fULL
#define BYTE_MASK_8 0x00ff00ff00ff00ffULL
#define HALF_MASK_8 0x0000ffff0000ffffULL
#define WORD_MASK_8 0x00000000ffffffffULL

size8u_t reverse_bits_8(size8u_t src)
{
	src = ((src >> 1) & BITS_MASK_8) | ((src & BITS_MASK_8) << 1);
	src = ((src >> 2) & PAIR_MASK_8) | ((src & PAIR_MASK_8) << 2);
	src = ((src >> 4) & NYBL_MASK_8) | ((src & NYBL_MASK_8) << 4);
	src = ((src >> 8) & BYTE_MASK_8) | ((src & BYTE_MASK_8) << 8);
	src = ((src >> 16) & HALF_MASK_8) | ((src & HALF_MASK_8) << 16);
	src = ((src >> 32) & WORD_MASK_8) | ((src & WORD_MASK_8) << 32);
	return src;
}


#define BITS_MASK_4 0x55555555ULL
#define PAIR_MASK_4 0x33333333ULL
#define NYBL_MASK_4 0x0f0f0f0fULL
#define BYTE_MASK_4 0x00ff00ffULL
#define HALF_MASK_4 0x0000ffffULL

size4u_t reverse_bits_4(size4u_t src)
{
	src = ((src >> 1) & BITS_MASK_4) | ((src & BITS_MASK_4) << 1);
	src = ((src >> 2) & PAIR_MASK_4) | ((src & PAIR_MASK_4) << 2);
	src = ((src >> 4) & NYBL_MASK_4) | ((src & NYBL_MASK_4) << 4);
	src = ((src >> 8) & BYTE_MASK_4) | ((src & BYTE_MASK_4) << 8);
	src = ((src >> 16) & HALF_MASK_4) | ((src & HALF_MASK_4) << 16);
	return src;
}

#define BITS_MASK_2 0x5555ULL
#define PAIR_MASK_2 0x3333ULL
#define NYBL_MASK_2 0x0f0fULL
#define BYTE_MASK_2 0x00ffULL
#define HALF_MASK_2 0x0000ULL

size4u_t reverse_bits_2(size2u_t src)
{
	src = ((src >> 1) & BITS_MASK_2) | ((src & BITS_MASK_2) << 1);
	src = ((src >> 2) & PAIR_MASK_2) | ((src & PAIR_MASK_2) << 2);
	src = ((src >> 4) & NYBL_MASK_2) | ((src & NYBL_MASK_2) << 4);
	src = ((src >> 8) & BYTE_MASK_2) | ((src & BYTE_MASK_2) << 8);
	return src;
}

#define BITS_MASK_1 0x55ULL
#define PAIR_MASK_1 0x33ULL
#define NYBL_MASK_1 0x0fULL
#define BYTE_MASK_1 0x00ULL
#define HALF_MASK_1 0x00ULL

size4u_t reverse_bits_1(size1u_t src)
{
	src = ((src >> 1) & BITS_MASK_1) | ((src & BITS_MASK_1) << 1);
	src = ((src >> 2) & PAIR_MASK_1) | ((src & PAIR_MASK_1) << 2);
	src = ((src >> 4) & NYBL_MASK_1) | ((src & NYBL_MASK_1) << 4);
	return src;
}

size8u_t exchange(size8u_t bits, size4u_t cntrl)
{
   int i;
   size4u_t mask = 1<<31;
   size8u_t mask0 = 0x1LL<<62;
   size8u_t mask1 = 0x2LL<<62;
   size8u_t outbits = 0;
   size8u_t b0, b1;

   for(i=0; i < 32; i++) {
       b0 = (bits & mask0)>>(2*i);
       b1 = (bits & mask1)>>(2*i);
       if(cntrl & mask) {
         outbits |= (b1>>1)|(b0<<1);
       } else {
         outbits |= (b1|b0);
       }
       outbits <<= 2;
       mask >>= 1;
       mask0 >>=2;
       mask1 >>=2;
   }
   return(outbits);
}

size8u_t interleave(size4u_t odd, size4u_t even)
{
	/* Convert to long long */
	size8u_t myodd = odd;
	size8u_t myeven = even;
	/* First, spread bits out */
	myodd = (myodd | (myodd << 16)) & HALF_MASK_8;
	myeven = (myeven | (myeven << 16)) & HALF_MASK_8;
	myodd = (myodd | (myodd << 8)) & BYTE_MASK_8;
	myeven = (myeven | (myeven << 8)) & BYTE_MASK_8;
	myodd = (myodd | (myodd << 4)) & NYBL_MASK_8;
	myeven = (myeven | (myeven << 4)) & NYBL_MASK_8;
	myodd = (myodd | (myodd << 2)) & PAIR_MASK_8;
	myeven = (myeven | (myeven << 2)) & PAIR_MASK_8;
	myodd = (myodd | (myodd << 1)) & BITS_MASK_8;
	myeven = (myeven | (myeven << 1)) & BITS_MASK_8;
	/* Now OR together */
	return (myeven | (myodd << 1));
}

size8u_t deinterleave(size8u_t src)
{
	/* Get odd and even bits */
//      thread_t *thread = NULL;
	size8u_t myodd = ((src >> 1) & BITS_MASK_8);
	size8u_t myeven = (src & BITS_MASK_8);

	/* Unspread bits */
	myeven = (myeven | (myeven >> 1)) & PAIR_MASK_8;
	myodd = (myodd | (myodd >> 1)) & PAIR_MASK_8;
	myeven = (myeven | (myeven >> 2)) & NYBL_MASK_8;
	myodd = (myodd | (myodd >> 2)) & NYBL_MASK_8;
	myeven = (myeven | (myeven >> 4)) & BYTE_MASK_8;
	myodd = (myodd | (myodd >> 4)) & BYTE_MASK_8;
	myeven = (myeven | (myeven >> 8)) & HALF_MASK_8;
	myodd = (myodd | (myodd >> 8)) & HALF_MASK_8;
	myeven = (myeven | (myeven >> 16)) & WORD_MASK_8;
	myodd = (myodd | (myodd >> 16)) & WORD_MASK_8;

	/* Return odd bits in upper half */
	return (myeven | (myodd << 32));
}

size4u_t carry_from_add64(size8u_t a, size8u_t b, size4u_t c)
{
	size8u_t tmpa, tmpb, tmpc;
	tmpa = fGETUWORD(0, a);
	tmpb = fGETUWORD(0, b);
	tmpc = tmpa + tmpb + c;
	tmpa = fGETUWORD(1, a);
	tmpb = fGETUWORD(1, b);
	tmpc = tmpa + tmpb + fGETUWORD(1, tmpc);
	tmpc = fGETUWORD(1, tmpc);
	return (tmpc);
}


size4s_t conv_round(size4s_t a, int n)
{
	size8s_t val;

	if (n == 0) {
		val = a;
	} else if ((a & ((1 << (n - 1)) - 1)) == 0) {	/* N-1..0 all zero? */
		val = ((fSE32_64(a)) + (size8s_t) (((size4u_t) ((1 << n) & a)) >> 1));	/* Add LSB from int part */
	} else {
		val = ((fSE32_64(a)) + (1 << (n - 1)));
	}

	val = val >> n;
	return ((size4s_t) val);
}

size16s_t cast8s_to_16s(size8s_t a)
{
	size16s_t result = {.hi = 0, .lo = 0};
	result.lo = a;
	if (a < 0)
		result.hi = -1;
	return result;
}
size8s_t cast16s_to_8s(size16s_t a)
{
	return a.lo;
}

size4s_t cast16s_to_4s(size16s_t a)
{
	return (size4s_t)a.lo;
}
 
size16s_t add128(size16s_t a, size16s_t b)
{
	size16s_t result = {.hi = 0, .lo = 0};
	result.lo = a.lo+b.lo;
	result.hi = a.hi+b.hi;	
	
	if (result.lo < b.lo)
		result.hi++;

	return result;
}
size16s_t sub128(size16s_t a, size16s_t b)
{
	size16s_t result = {.hi = 0, .lo = 0};
	result.lo = a.lo-b.lo;
	result.hi = a.hi-b.hi;	
	if (result.lo > a.lo)
		result.hi--;

	return result;
}
size16s_t shiftr128(size16s_t a, size4u_t n)
{
	size16s_t result;
	result.lo = (a.lo >> n) | (a.hi << (64-n));
	result.hi = a.hi >> n;
	return result;
}
size16s_t shiftl128(size16s_t a, size4u_t n)
{
	size16s_t result;
	result.lo = a.lo << n;
	result.hi = (a.hi << n) | (a.lo>>(64-n));
	return result;
}
size16s_t and128(size16s_t a, size16s_t b)
{
	size16s_t result;
	result.lo = a.lo & b.lo;
	result.hi = a.hi & b.hi;
	return result;
}





size8s_t conv_round64(size8s_t a, size4u_t n)
{
	size8s_t val=a;
#if 0
	if (n == 0) {
		val = a;
		val = val >> n;
	} else if ((a & (size8s_t)((1LL << (n - 1)) - 1LL)) == 0) { /* N-1..0 all zero? */
		  // val = (a + (size8s_t) (((size8s_t) ((1LL << n) & a)) >> 1));  /* Add LSB from int part */
		size8s_t b = (size8s_t) ((1LL << n) & a) >> 1;
		size16s_t r = add128(a, b);
		val =  shift128(r, n);
	} else {
		  // val = (a + (1LL << (n - 1)));
		size8s_t b = (size8s_t) (1LL << (n - 1));
		size16s_t r = add128(a, b);
		val =  shift128(r, n);
	}
	#endif
	return val;
}

/* Floating Point Stuff */

static const int roundingmodes[] = {
	FE_TONEAREST,
	FE_TOWARDZERO,
	FE_DOWNWARD,
	FE_UPWARD
};

void arch_fpop_start(thread_t * thread)
{
	fegetenv(&thread->fenv);
	feclearexcept(FE_ALL_EXCEPT);
	fesetround(roundingmodes[fREAD_REG_FIELD(USR, USR_FPRND)]);
}

#define TEST_FLAG(LIBCF,MYF,MYE) \
	if (fetestexcept(LIBCF)) { \
		if (GET_USR_FIELD(USR_##MYF) == 0) { \
			SET_USR_FIELD(USR_##MYF,1); \
			if (GET_USR_FIELD(USR_##MYE)) { \
				/* It's a trap! */ \
			} \
		} \
	}

void arch_fpop_end(thread_t * thread)
{
	if (fetestexcept(FE_ALL_EXCEPT)) {
		TEST_FLAG(FE_INEXACT, FPINPF, FPINPE)
			TEST_FLAG(FE_DIVBYZERO, FPDBZF, FPDBZE)
			TEST_FLAG(FE_INVALID, FPINVF, FPINVE)
			TEST_FLAG(FE_OVERFLOW, FPOVFF, FPOVFE)
			TEST_FLAG(FE_UNDERFLOW, FPUNFF, FPUNFE)
	}
	fesetenv(&thread->fenv);
}

#undef TEST_FLAG


#define TEST_FLAG(LIBF,MYF,MYE) \
	if (flags & LIBF) { \
		if (GET_USR_FIELD(USR_##MYF) == 0) { \
			SET_USR_FIELD(USR_##MYF,1); \
			if (GET_USR_FIELD(USR_##MYE)) { \
				/* It's a trap! */ \
			} \
		} \
	}

void arch_raise_fpflag(unsigned int flags)
{
#if 0
	TEST_FLAG(FE_INEXACT, FPINPF, FPINPE)
		TEST_FLAG(FE_DIVBYZERO, FPDBZF, FPDBZE)
		TEST_FLAG(FE_INVALID, FPINVF, FPINVE)
		TEST_FLAG(FE_OVERFLOW, FPOVFF, FPOVFE)
		TEST_FLAG(FE_UNDERFLOW, FPUNFF, FPUNFE)
#else
	feraiseexcept(flags);
#endif
}

#undef TEST_FLAG

int
arch_df_recip_common(size8s_t * Rs, size8s_t * Rt, size8s_t * Rd,
					 int *adjust)
{
	int n_class;
	int d_class;
	int n_exp;
	int d_exp;
	int ret = 0;
	size8s_t RssV, RttV, RddV;
	int PeV = 0;
	RssV = *Rs;
	RttV = *Rt;
	n_class = fpclassify(fDOUBLE(RssV));
	d_class = fpclassify(fDOUBLE(RttV));
	//fprintf(stdout,"Rs: 0x%08x/%a, Rt: 0x%08x/%a, n_class: %d, d_class: %d\n",RsV,fFLOAT(RsV),RtV,fFLOAT(RtV),n_class,d_class);
	if ((n_class == FP_NAN) && (d_class == FP_NAN)) {
		if (fGETBIT(51, RssV & RttV) == 0) {
			fRAISEFLAGS(FE_INVALID);
		}
		RddV = RssV = RttV = fDFNANVAL();
	} else if (n_class == FP_NAN) {
		if (fGETBIT(51, RssV) == 0) {
			fRAISEFLAGS(FE_INVALID);
		}
		RddV = RssV = RttV = fDFNANVAL();
	} else if (d_class == FP_NAN) {
		if (fGETBIT(51, RttV) == 0) {
			fRAISEFLAGS(FE_INVALID);
		}
		RddV = RssV = RttV = fDFNANVAL();
	} else if ((n_class == FP_INFINITE) && (d_class == FP_INFINITE)) {
		/* EJP: or put Inf in num fixup? */
		RddV = RssV = RttV = fDFNANVAL();
		fRAISEFLAGS(FE_INVALID);
	} else if ((n_class == FP_ZERO) && (d_class == FP_ZERO)) {
		/* EJP: or put zero in num fixup? */
		RddV = RssV = RttV = fDFNANVAL();
		fRAISEFLAGS(FE_INVALID);
	} else if (d_class == FP_ZERO) {
		/* EJP: or put Inf in num fixup? */
		RssV = fDFINFVAL(RssV ^ RttV);
		RttV = fDFONEVAL(0);
		RddV = fDFONEVAL(0);
		if (n_class != FP_INFINITE)
			fRAISEFLAGS(FE_DIVBYZERO);
	} else if (d_class == FP_INFINITE) {
		RssV = 0x8000000000000000ULL & (RssV ^ RttV);
		RttV = fDFONEVAL(0);
		RddV = fDFONEVAL(0);
	} else if (n_class == FP_ZERO) {
		/* EJP: Does this just work itself out? */
		/* EJP: No, 0/inf */
		RssV = 0x8000000000000000ULL & (RssV ^ RttV);
		RttV = fDFONEVAL(0);
		RddV = fDFONEVAL(0);
	} else if (n_class == FP_INFINITE) {
		/* EJP: Does this just work itself out? */
		RssV = fDFINFVAL(RssV ^ RttV);
		RttV = fDFONEVAL(0);
		RddV = fDFONEVAL(0);
	} else {
		PeV = 0x00;
		/* Basic checks passed */
		n_exp = fDF_GETEXP(RssV);
		d_exp = fDF_GETEXP(RttV);
		if ((n_exp - d_exp + fDF_BIAS()) <= fDF_MANTBITS()) {
			/* Near quotient underflow / inexact Q */
			//fprintf(stdout,"Avoiding inexact Q\n");
			PeV = 0x80;
			RttV = fDF_MUL_POW2(RttV, -128);
			RssV = fDF_MUL_POW2(RssV, 128);
		} else if ((n_exp - d_exp + fDF_BIAS()) > (fDF_MAXEXP() - 54)) {
			/* Near quotient overflow */
			//fprintf(stdout,"Avoiding quotient overflow\n");
			PeV = 0x40;
			RttV = fDF_MUL_POW2(RttV, 64);
			RssV = fDF_MUL_POW2(RssV, -64);
		} else if (n_exp <= fDF_MANTBITS() + 2) {
			RttV = fDF_MUL_POW2(RttV, 128);
			RssV = fDF_MUL_POW2(RssV, 128);
		} else if (d_exp <= 1) {
			RttV = fDF_MUL_POW2(RttV, 64);
			RssV = fDF_MUL_POW2(RssV, 64);
		} else if (d_exp > fDF_MAXEXP() - 2) {
			RttV = fDF_MUL_POW2(RttV, -64);
			RssV = fDF_MUL_POW2(RssV, -64);
		}
		RddV = 0;
		ret = 1;
	}
	*Rs = RssV;
	*Rt = RttV;
	*Rd = RddV;
	*adjust = PeV;
	return ret;
}

int arch_sf_recip_common(reg_t * Rs, reg_t * Rt, reg_t * Rd, int *adjust)
{
	int n_class;
	int d_class;
	int n_exp;
	int d_exp;
	int ret = 0;
	reg_t RsV, RtV, RdV;
	int PeV = 0;
	RsV = *Rs;
	RtV = *Rt;
	n_class = fpclassify(fFLOAT(RsV));
	d_class = fpclassify(fFLOAT(RtV));
	//fprintf(stdout,"Rs: 0x%08x/%a, Rt: 0x%08x/%a, n_class: %d, d_class: %d\n",RsV,fFLOAT(RsV),RtV,fFLOAT(RtV),n_class,d_class);
	if ((n_class == FP_NAN) && (d_class == FP_NAN)) {
		if (fGETBIT(22, RsV & RtV) == 0) {
			fRAISEFLAGS(FE_INVALID);
		}
		RdV = RsV = RtV = fSFNANVAL();
	} else if (n_class == FP_NAN) {
		if (fGETBIT(22, RsV) == 0) {
			fRAISEFLAGS(FE_INVALID);
		}
		RdV = RsV = RtV = fSFNANVAL();
	} else if (d_class == FP_NAN) {
		/* EJP: or put NaN in num/den fixup? */
		if (fGETBIT(22, RtV) == 0) {
			fRAISEFLAGS(FE_INVALID);
		}
		RdV = RsV = RtV = fSFNANVAL();
	} else if ((n_class == FP_INFINITE) && (d_class == FP_INFINITE)) {
		/* EJP: or put Inf in num fixup? */
		RdV = RsV = RtV = fSFNANVAL();
		fRAISEFLAGS(FE_INVALID);
	} else if ((n_class == FP_ZERO) && (d_class == FP_ZERO)) {
		/* EJP: or put zero in num fixup? */
		RdV = RsV = RtV = fSFNANVAL();
		fRAISEFLAGS(FE_INVALID);
	} else if (d_class == FP_ZERO) {
		/* EJP: or put Inf in num fixup? */
		RsV = fSFINFVAL(RsV ^ RtV);
		RtV = fSFONEVAL(0);
		RdV = fSFONEVAL(0);
		if (n_class != FP_INFINITE)
			fRAISEFLAGS(FE_DIVBYZERO);
	} else if (d_class == FP_INFINITE) {
		RsV = 0x80000000 & (RsV ^ RtV);
		RtV = fSFONEVAL(0);
		RdV = fSFONEVAL(0);
	} else if (n_class == FP_ZERO) {
		/* EJP: Does this just work itself out? */
		/* EJP: No, 0/Inf causes problems. */
		RsV = 0x80000000 & (RsV ^ RtV);
		RtV = fSFONEVAL(0);
		RdV = fSFONEVAL(0);
	} else if (n_class == FP_INFINITE) {
		/* EJP: Does this just work itself out? */
		RsV = fSFINFVAL(RsV ^ RtV);
		RtV = fSFONEVAL(0);
		RdV = fSFONEVAL(0);
	} else {
		PeV = 0x00;
		/* Basic checks passed */
		n_exp = fSF_GETEXP(RsV);
		d_exp = fSF_GETEXP(RtV);
		if ((n_exp - d_exp + fSF_BIAS()) <= fSF_MANTBITS()) {
			/* Near quotient underflow / inexact Q */
			//fprintf(stdout,"Avoiding inexact Q\n");
			PeV = 0x80;
			RtV = fSF_MUL_POW2(RtV, -64);
			RsV = fSF_MUL_POW2(RsV, 64);
		} else if ((n_exp - d_exp + fSF_BIAS()) > (fSF_MAXEXP() - 24)) {
			/* Near quotient overflow */
			//fprintf(stdout,"Avoiding quotient overflow\n");
			PeV = 0x40;
			RtV = fSF_MUL_POW2(RtV, 32);
			RsV = fSF_MUL_POW2(RsV, -32);
		} else if (n_exp <= fSF_MANTBITS() + 2) {
			RtV = fSF_MUL_POW2(RtV, 64);
			RsV = fSF_MUL_POW2(RsV, 64);
		} else if (d_exp <= 1) {
			RtV = fSF_MUL_POW2(RtV, 32);
			RsV = fSF_MUL_POW2(RsV, 32);
		} else if (d_exp > 252) {
			RtV = fSF_MUL_POW2(RtV, -32);
			RsV = fSF_MUL_POW2(RsV, -32);
		}
		RdV = 0;
		ret = 1;
	}
	*Rs = RsV;
	*Rt = RtV;
	*Rd = RdV;
	*adjust = PeV;
	return ret;
}

int arch_sf_invsqrt_common(reg_t * Rs, reg_t * Rd, int *adjust)
{
	int r_class;
	reg_t RsV, RdV;
	int PeV = 0;
	int r_exp;
	int ret = 0;
	RsV = *Rs;
	r_class = fpclassify(fFLOAT(RsV));
	if (r_class == FP_NAN) {
		if (fGETBIT(22, RsV) == 0) {
			fRAISEFLAGS(FE_INVALID);
		}
		RdV = RsV = fSFNANVAL();
	} else if (fFLOAT(RsV) < 0.0) {
		/* Negative nonzero values are NaN */
		fRAISEFLAGS(FE_INVALID);
		RsV = fSFNANVAL();
		RdV = fSFNANVAL();
	} else if (r_class == FP_INFINITE) {
		/* EJP: or put Inf in num fixup? */
		RsV = fSFINFVAL(-1);
		RdV = fSFINFVAL(-1);
	} else if (r_class == FP_ZERO) {
		/* EJP: or put zero in num fixup? */
		RsV = RsV;
		RdV = fSFONEVAL(0);
	} else {
		PeV = 0x00;
		/* Basic checks passed */
		r_exp = fSF_GETEXP(RsV);
		if (r_exp <= 24) {
			RsV = fSF_MUL_POW2(RsV, 64);
			PeV = 0xe0;
		}
		RdV = 0;
		ret = 1;
	}
	*Rs = RsV;
	*Rd = RdV;
	*adjust = PeV;
	return ret;
}

int arch_df_invsqrt_common(size8s_t * Rs, size8s_t * Rd, int *adjust)
{
	int r_class;
	size8s_t RssV, RddV;
	int PeV = 0;
	int r_exp;
	int ret = 0;
	RssV = *Rs;
	r_class = fpclassify(fDOUBLE(RssV));
	if (r_class == FP_NAN) {
		if (fGETBIT(51, RssV) == 0) {
			fRAISEFLAGS(FE_INVALID);
		}
		RddV = RssV = fDFNANVAL();
	} else if (fDOUBLE(RssV) < 0.0) {
		/* Negative nonzero values are NaN */
		fRAISEFLAGS(FE_INVALID);
		RssV = fDFNANVAL();
		RddV = fDFNANVAL();
	} else if (r_class == FP_INFINITE) {
		/* EJP: or put Inf in num fixup? */
		RssV = fDFINFVAL(-1);
		RddV = fDFINFVAL(-1);
	} else if (r_class == FP_ZERO) {
		/* EJP: or put zero in num fixup? */
		RssV = RssV;
		RddV = fDFONEVAL(0);
	} else {
		PeV = 0x00;
		/* Basic checks passed */
		r_exp = fDF_GETEXP(RssV);
		if (r_exp <= 54) {
			RssV = fDF_MUL_POW2(RssV, 128);
			PeV = 0xe0;
		}
		RddV = 0;
		ret = 1;
	}
	*Rs = RssV;
	*Rd = RddV;
	*adjust = PeV;
	return ret;
}

int arch_recip_lookup(int index)
{
	index &= 0x7f;
	unsigned const int roundrom[128] = {
		0x0fe, 0x0fa, 0x0f6, 0x0f2, 0x0ef, 0x0eb, 0x0e7, 0x0e4,
		0x0e0, 0x0dd, 0x0d9, 0x0d6, 0x0d2, 0x0cf, 0x0cc, 0x0c9,
		0x0c6, 0x0c2, 0x0bf, 0x0bc, 0x0b9, 0x0b6, 0x0b3, 0x0b1,
		0x0ae, 0x0ab, 0x0a8, 0x0a5, 0x0a3, 0x0a0, 0x09d, 0x09b,
		0x098, 0x096, 0x093, 0x091, 0x08e, 0x08c, 0x08a, 0x087,
		0x085, 0x083, 0x080, 0x07e, 0x07c, 0x07a, 0x078, 0x075,
		0x073, 0x071, 0x06f, 0x06d, 0x06b, 0x069, 0x067, 0x065,
		0x063, 0x061, 0x05f, 0x05e, 0x05c, 0x05a, 0x058, 0x056,
		0x054, 0x053, 0x051, 0x04f, 0x04e, 0x04c, 0x04a, 0x049,
		0x047, 0x045, 0x044, 0x042, 0x040, 0x03f, 0x03d, 0x03c,
		0x03a, 0x039, 0x037, 0x036, 0x034, 0x033, 0x032, 0x030,
		0x02f, 0x02d, 0x02c, 0x02b, 0x029, 0x028, 0x027, 0x025,
		0x024, 0x023, 0x021, 0x020, 0x01f, 0x01e, 0x01c, 0x01b,
		0x01a, 0x019, 0x017, 0x016, 0x015, 0x014, 0x013, 0x012,
		0x011, 0x00f, 0x00e, 0x00d, 0x00c, 0x00b, 0x00a, 0x009,
		0x008, 0x007, 0x006, 0x005, 0x004, 0x003, 0x002, 0x000,
	};
	return roundrom[index];
};

int arch_invsqrt_lookup(int index)
{
	index &= 0x7f;
	unsigned const int roundrom[128] = {
		0x069, 0x066, 0x063, 0x061, 0x05e, 0x05b, 0x059, 0x057,
		0x054, 0x052, 0x050, 0x04d, 0x04b, 0x049, 0x047, 0x045,
		0x043, 0x041, 0x03f, 0x03d, 0x03b, 0x039, 0x037, 0x036,
		0x034, 0x032, 0x030, 0x02f, 0x02d, 0x02c, 0x02a, 0x028,
		0x027, 0x025, 0x024, 0x022, 0x021, 0x01f, 0x01e, 0x01d,
		0x01b, 0x01a, 0x019, 0x017, 0x016, 0x015, 0x014, 0x012,
		0x011, 0x010, 0x00f, 0x00d, 0x00c, 0x00b, 0x00a, 0x009,
		0x008, 0x007, 0x006, 0x005, 0x004, 0x003, 0x002, 0x001,
		0x0fe, 0x0fa, 0x0f6, 0x0f3, 0x0ef, 0x0eb, 0x0e8, 0x0e4,
		0x0e1, 0x0de, 0x0db, 0x0d7, 0x0d4, 0x0d1, 0x0ce, 0x0cb,
		0x0c9, 0x0c6, 0x0c3, 0x0c0, 0x0be, 0x0bb, 0x0b8, 0x0b6,
		0x0b3, 0x0b1, 0x0af, 0x0ac, 0x0aa, 0x0a8, 0x0a5, 0x0a3,
		0x0a1, 0x09f, 0x09d, 0x09b, 0x099, 0x097, 0x095, 0x093,
		0x091, 0x08f, 0x08d, 0x08b, 0x089, 0x087, 0x086, 0x084,
		0x082, 0x080, 0x07f, 0x07d, 0x07b, 0x07a, 0x078, 0x077,
		0x075, 0x074, 0x072, 0x071, 0x06f, 0x06e, 0x06c, 0x06b,
	};
	return roundrom[index];
};


void arch_test_sf_recip(float n, float d, float *inv_out, float *n_out,
						float *d_out, int *adj_out)
{
	reg_t RsV, RtV, RtV_rnd, RdV;
	int adj;
	int mant, exp;
	int idx;
	RsV = fUNFLOAT(n);
	RtV = fUNFLOAT(d);
	RdV = 0;
	if (arch_sf_recip_common(&RsV, &RtV, &RdV, &adj)) {
		RtV_rnd = (RtV + 0xf7fc);	/* Round */
		idx = (RtV_rnd >> 17) & 0x3f;
		mant = fSF_RECIP_LOOKUP(idx) << 15;
		exp =
			fSF_BIAS() - (fSF_GETEXP(RtV_rnd) - fSF_BIAS()) + (idx ==
															   0) - 1;
		RdV = fMAKESF(fGETBIT(31, RtV), exp, mant | 1);
	}
	*inv_out = fFLOAT(RdV);
	*n_out = fFLOAT(RsV);
	*d_out = fFLOAT(RtV);
	*adj_out = adj;
}

void arch_test_df_recip(double n, double d, double *inv_out, double *n_out,
						double *d_out, int *adj_out)
{
	size8s_t RssV, RttV, RttV_rnd, RddV;
	int adj;
	size8s_t mant, exp;
	int idx;
	RssV = fUNDOUBLE(n);
	RttV = fUNDOUBLE(d);
	RddV = 0;
	if (arch_df_recip_common(&RssV, &RttV, &RddV, &adj)) {
		RttV_rnd = (RttV + 0x00001fffffffffffULL);	/* Round */
		idx = (RttV_rnd >> 46) & 0x3f;
		mant = fDF_RECIP_LOOKUP(idx) << 44;
		exp =
			fDF_BIAS() - (fDF_GETEXP(RttV_rnd) - fDF_BIAS()) + (idx ==
																0) - 1;
		RddV = fMAKEDF(fGETBIT(63, RttV), exp, mant | 1);
		printf
			("RttV: %a RttV_rnd: %016llx idx: %08x exp: %016llx mant: %016llx RddV: %016llx adj: %d\n",
			 fDOUBLE(RttV), RttV_rnd, idx, exp, mant, RddV, adj);
	}
	*inv_out = fDOUBLE(RddV);
	*n_out = fDOUBLE(RssV);
	*d_out = fDOUBLE(RttV);
	*adj_out = adj;
}

void arch_test_sf_invsqrt(float r, float *invsqrt_out, float *r_out,
						  int *adj_out)
{
	reg_t RsV, RsV_rnd, RdV;
	int adj;
	int mant, exp;
	int idx;
	RsV = fUNFLOAT(r);
	RdV = 0;
	if (arch_sf_invsqrt_common(&RsV, &RdV, &adj)) {
		RsV_rnd = (RsV + 0xf7fc);	/* Round */
		idx = (RsV_rnd >> 18) & 0x3f;
		mant = fSF_INVSQRT_LOOKUP(idx) << 15;
		exp =
			fSF_BIAS() - ((fSF_GETEXP(RsV_rnd) - fSF_BIAS()) >> 1) - 1 +
			(idx == 0x20);
		RdV = fMAKESF(fGETBIT(31, RsV), exp, mant | 1);
	}
	*invsqrt_out = fFLOAT(RdV);
	*r_out = fFLOAT(RsV);
	*adj_out = adj;
}

void arch_test_df_invsqrt(double r, double *invsqrt_out, double *r_out,
						  int *adj_out)
{
	size8s_t RssV, RssV_rnd, RddV;
	int adj;
	size8s_t mant, exp;
	int idx;
	RssV = fUNDOUBLE(r);
	RddV = 0;
	if (arch_df_invsqrt_common(&RssV, &RddV, &adj)) {
		RssV_rnd = (RssV + 0x0000400000000000ULL);	/* Round */
		idx = (RssV_rnd >> 47) & 0x3f;
		mant = fDF_INVSQRT_LOOKUP(idx) << 44;
		exp =
			fDF_BIAS() - ((fDF_GETEXP(RssV_rnd) - fDF_BIAS()) >> 1) - 1 +
			(idx == 0x20);
		RddV = fMAKEDF(fGETBIT(63, RssV), exp, mant | 1);
	}
	*invsqrt_out = fDOUBLE(RddV);
	*r_out = fDOUBLE(RssV);
	*adj_out = adj;
}

float arch_test_div(float n, float d)
{
	int adj;
	float y0, e0;
	float y1, e1;
	float y2, e2;
	float y3;
	float q0, r0;
	float q1;
	arch_test_sf_recip(n, d, &y0, &n, &d, &adj);
	e0 = fFMAF(-d, y0, 1.0);
	fprintf(stdout, "n: %a/%x, d: %a/%x, y0: %a/%x, e0: %a/%x\n", n,
			fUNFLOAT(n), d, fUNFLOAT(d), y0, fUNFLOAT(y0), e0,
			fUNFLOAT(e0));
	y1 = fFMAF(e0, y0, y0);
	//e1 = fFMAF(e0,e0,0.0);
	e1 = fFMAF(-d, y1, 1.0);
	fprintf(stdout, "n: %a, d: %a, y1: %a, e1: %a\n", n, d, y1, e1);
	y2 = fFMAF(e1, y1, y1);
	//e2 = fFMAF(e1,e1,0.0);
	//e2 = fFMAF(-d,y2,1.0);
	e2 = 0.0;
	fprintf(stdout, "n: %a, d: %a, y2: %a, e2: %a\n", n, d, y2, e2);
	//y3 = fFMAF(e2,y2,y2);
	y3 = 0.0;
	q0 = fFMAF(n, y2, 0.0);
	r0 = fFMAF(-d, q0, n);
	fprintf(stdout, "n: %a, d: %a, y3: %a, q0: %a, r0: %a\n", n, d, y3, q0,
			r0);
	adj = fSXTN(8, 32, adj);
	q1 = fFMAFX(r0, y2, q0, adj);
	fprintf(stdout,
			"n: %a, d: %a, y3: %a, q0: %a, r0: %a, q1: %a/%08x, adj=%d\n",
			n, d, y3, q0, r0, q1, fUNFLOAT(q1), adj);
	return q1;
}

double arch_test_divd(double n, double d)
{
	int adj;
	double y0, e0;
	double y1, e1;
	double y2, e2;
	double y3, e3;
	double y4;
	double q0, r0;
	double q1;
	arch_test_df_recip(n, d, &y0, &n, &d, &adj);
	e0 = fFMA(-d, y0, 1.0);
	fprintf(stdout, "n: %a, d: %a, y0: %a, e0: %a\n", n, d, y0, e0);
	y1 = fFMA(e0, y0, y0);
	e1 = fFMA(e0, e0, 0.0);
	//e1 = fFMA(-d,y1,1.0);
	fprintf(stdout, "n: %a, d: %a, y1: %a, e1: %a\n", n, d, y1, e1);
	y2 = fFMA(e1, y1, y1);
	e2 = fFMA(e1, e1, 0.0);
	//e2 = fFMA(-d,y2,1.0);
	//e2 = 0.0;
	fprintf(stdout, "n: %a, d: %a, y2: %a, e2: %a\n", n, d, y2, e2);
	y3 = fFMA(e2, y2, y2);
	e3 = fFMA(-d, y3, 1.0);
	fprintf(stdout, "n: %a, d: %a, y3: %a, e3: %a\n", n, d, y3, e3);
	y4 = fFMA(e3, y3, y3);
	//y3 = 0.0;
	q0 = fFMA(n, y4, 0.0);
	r0 = fFMA(-d, q0, n);
	fprintf(stdout, "n: %a, d: %a, y4: %a, q0: %a, r0: %a\n", n, d, y4, q0,
			r0);
	adj = fSXTN(8, 32, adj);
	q1 = fFMAX(r0, y4, q0, adj);
	fprintf(stdout,
			"n: %a, d: %a, y4: %a, q0: %a, r0: %a, q1: %a/%016llx, adj=%d\n",
			n, d, y4, q0, r0, q1, fUNDOUBLE(q1), adj);
	return q1;
}

float arch_test_sqrt(float r)
{
	int adj;
	float y0;
	float S0, H0;
	float d0, e0;
	float S1, H1;
	float d1, e1;
	float S2, H2;
	float d2, e2;
	float S3;
	float answer = sqrtf(r);
	arch_test_sf_invsqrt(r, &y0, &r, &adj);
	adj = fSXTN(8, 32, adj);
	S0 = fFMAF(r, y0, 0.0);
	H0 = fFMAF(0.5, y0, 0.0);
	d0 = fFMAF(-S0, H0, 0.5);
	e0 = fFMAF(-S0, S0, r);
	fprintf(stdout,
			"r: %a, y0: %a, S0: %a, H0: %a, d0: %a, e0: %a, adj=%d\n", r,
			y0, S0, H0, d0, e0, adj);
	S1 = fFMAF(S0, d0, S0);
	H1 = fFMAF(H0, d0, H0);
	d1 = fFMAF(-S1, H1, 0.5);
	e1 = fFMAF(-S1, S1, r);
	fprintf(stdout, "r: %a, S1: %a, H1: %a, d1: %a, e1: %a\n", r, S1, H1,
			d1, e1);
	S2 = fFMAF(H1, e1, S1);
	H2 = fFMAF(H1, d1, H1);
	d2 = fFMAF(-S2, H2, 0.5);
	e2 = fFMAF(-S2, S2, r);
	fprintf(stdout, "r: %a, S2: %a, H2: %a, d2: %a, e2: %a\n", r, S2, H2,
			d2, e2);
	S3 = fFMAFX(H2, e2, S2, adj);
	fprintf(stdout, "r: %a, S3: %a should be: %a\n", r, S3, answer);
	return S3;
}

double arch_test_sqrtd(double r)
{
	int adj;
	double y0;
	double S0, H0;
	double d0, e0;
	double S1, H1;
	double d1, e1;
	double S2, H2;
	double d2, e2;
//	double S3, H3;
	double S3;
//	double d3, e3;
	double S4;
	double answer = sqrt(r);
	arch_test_df_invsqrt(r, &y0, &r, &adj);
	adj = fSXTN(8, 32, adj);
	S0 = fFMA(r, y0, 0.0);
	H0 = fFMA(0.5, y0, 0.0);
	d0 = fFMA(-S0, H0, 0.5);
	e0 = fFMA(-S0, S0, r);
	fprintf(stdout,
			"r: %a, y0: %a, S0: %a, H0: %a, d0: %a, e0: %a, adj=%d\n", r,
			y0, S0, H0, d0, e0, adj);
	S1 = fFMA(S0, d0, S0);
	H1 = fFMA(H0, d0, H0);
	d1 = fFMA(-S1, H1, 0.5);
	e1 = fFMA(-S1, S1, r);
	fprintf(stdout, "r: %a, S1: %a, H1: %a, d1: %a, e1: %a\n", r, S1, H1,
			d1, e1);
	S2 = fFMA(H1, e1, S1);
	H2 = fFMA(H1, d1, H1);
	d2 = fFMA(-S2, H2, 0.5);
	e2 = fFMA(-S2, S2, r);
	fprintf(stdout, "r: %a, S2: %a, H2: %a, d2: %a, e2: %a\n", r, S2, H2,
			d2, e2);
	S3 = fFMA(H2, e2, S2);
//	H3 = fFMA(H2, d2, H2);

	// FIXME: Variable 'd3' is assigned a value that is never used
	// FIXME: Variable 'e3' is assigned a value that is never used
//	d3 = fFMA(-S3, H3, 0.5);
//	e3 = fFMA(-S3, S3, r);
	fprintf(stdout, "r: %a, S2: %a, H2: %a, d2: %a, e2: %a\n", r, S2, H2,
			d2, e2);
	S4 = fFMAX(H2, e2, S2, adj);
	fprintf(stdout, "r: %a, S3: %a should be: %a\n", r, S3, answer);
	return S4;
}
