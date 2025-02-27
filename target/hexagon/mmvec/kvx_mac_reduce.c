/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#include "qemu/osdep.h"
#include "kvx_ieee.h"

#define DF_MANTBITS() 52
#define SF_MANTBITS() 23
#define HF_MANTBITS() 10

#define DF_INF_EXP 0x7ff
#define DF_BIAS 1023

#define SF_INF_EXP 0xff
#define SF_BIAS 127

#define HF_INF_EXP 0x1f
#define HF_BIAS 15

#define WAY_BIG_EXP 4096

#define isz(X) (fabs(X) == 0.0f)


typedef union {
	double f;
	size8u_t i;
#ifndef SLOWLARIS
	struct {
		size8u_t mant:52;
		size8u_t exp:11;
		size8u_t sign:1;
	} x;
#else
	struct {
		size8u_t sign:1;
		size8u_t exp:11;
		size8u_t mant:52;
	} x;
#endif
} df_t;

typedef union {
	float f;
	size4u_t i;
#ifndef SLOWLARIS
	struct {
		size4u_t mant:23;
		size4u_t exp:8;
		size4u_t sign:1;
	} x;
#else
	struct {
		size4u_t sign:1;
		size4u_t exp:8;
		size4u_t mant:23;
	} x;
#endif
} sf_t;

typedef struct {
	union {
		size8u_t low;
		struct {
#ifndef SLOWLARIS
			size4u_t w0;
			size4u_t w1;
#else
			size4u_t w1;
			size4u_t w0;
#endif
		};
	};
	union {
		size8u_t high;
		struct {
#ifndef SLOWLARIS
			size4u_t w2;
			size4u_t w3;
#else
			size4u_t w3;
			size4u_t w2;
#endif
		};
	};
} int128_t;

typedef struct {
	int128_t mant;
	size4s_t exp;
	size1u_t sign;
	size1u_t guard;
	size1u_t round;
	size1u_t sticky;
} xf_t;

static inline void xf_init(xf_t * p)
{
	p->mant.low = 0;
	p->mant.high = 0;
	p->exp = 0;
	p->sign = 0;
	p->guard = 0;
	p->round = 0;
	p->sticky = 0;
}

size8u_t df_getmant_kvx(df_t a);
size8u_t df_getmant_kvx(df_t a)
{
	//int class = fpclassify(a.f);
	//switch (class) {
	//case FP_NORMAL:
		return (a.x.mant | 1ULL << 52);
	//case FP_ZERO:
	//	return 0;
	//case FP_SUBNORMAL:
	//	return a.x.mant;
	//default:
	//	return -1;
	//};
}

size4s_t df_getexp_kvx(df_t a);
size4s_t df_getexp_kvx(df_t a)
{
	//int class = fpclassify(a.f);
	//switch (class) {
	//case FP_NORMAL:
		return a.x.exp;
	//case FP_SUBNORMAL:
	//	return a.x.exp + 1;
	//default:
	//	return -1;
	//};
}

size8u_t sf_getmant_kvx(sf_t a);
size8u_t sf_getmant_kvx(sf_t a)
{
	//case FP_ZERO:
	if((a.x.mant == 0) && (a.x.exp == 0))
		return 0;
	//case FP_SUBNORMAL:
        else if((a.x.mant != 0) && (a.x.exp == 0))
		return a.x.mant;
	//case FP_NORMAL:
        else if((a.x.exp != 0xFF) && (a.x.exp != 0))
		return (a.x.mant | 1ULL << 23);
	//default:
        else
		return -1;
}

size4s_t sf_getexp_kvx(sf_t a);
size4s_t sf_getexp_kvx(sf_t a)
{
	//case FP_SUBNORMAL:
        if((a.x.mant != 0) && (a.x.exp == 0))
		return a.x.exp + 1;
	//case FP_NORMAL:
        else if((a.x.exp != 0xFF) && (a.x.exp != 0))
		return a.x.exp;
	//default:
	else
		return -1;
}

static inline void xf_debug(const char *msg, xf_t a)
{
#ifdef DEBUG
	printf("%s %c0x%016llx_%016llx /%d/%d/%d p%d\n", msg,
				  a.sign ? '-' : '+', a.mant.high, a.mant.low, a.guard,
				  a.round, a.sticky, a.exp);
#endif
}

static inline int128_t int128_shl(int128_t a, size4u_t amt)
{
	int128_t ret;
	if (amt == 0)
		return a;
	if (amt > 128) {
		ret.high = 0;
		ret.low = 0;
		return ret;
	}
	if (amt >= 64) {
		amt -= 64;
		a.high = a.low;
		a.low = 0;
	}
	ret.high = a.high << amt;
	ret.high |= (a.low >> (64 - amt));
	ret.low = a.low << amt;
	return ret;
}

static inline int128_t int128_shr(int128_t a, size4u_t amt)
{
	int128_t ret;
	if (amt == 0)
		return a;
	if (amt > 128) {
		ret.high = 0;
		ret.low = 0;
		return ret;
	}
	if (amt >= 64) {
		amt -= 64;
		a.low = a.high;
		a.high = 0;
	}
	ret.low = a.low >> amt;
	ret.low |= (a.high << (64 - amt));
	ret.high = a.high >> amt;
	return ret;
}


#define int128_gt kvx_int128_gt
static inline int kvx_int128_gt(int128_t a, int128_t b)
{
	if (a.high == b.high)
		return (a.low > b.low);
	return (a.high > b.high);
}

static inline xf_t xf_norm_left(xf_t a)
{
	a.exp--;
	a.mant = int128_shl(a.mant, 1);
	a.mant.low |= a.guard;
	a.guard = a.round;
	a.round = a.sticky;
	return a;
}

static inline xf_t xf_norm_right(xf_t a, int amt)
{
	if (amt > 130) {
		a.sticky |=
			a.round | a.guard | (a.mant.low != 0) | (a.mant.high != 0);
		a.guard = a.round = a.mant.high = a.mant.low = 0;
		a.exp += amt;
		return a;

	}
	while (amt >= 64) {
		a.sticky |= a.round | a.guard | (a.mant.low != 0);
		a.guard = (a.mant.low >> 63) & 1;
		a.round = (a.mant.low >> 62) & 1;
		a.mant.low = a.mant.high;
		a.mant.high = 0;
		a.exp += 64;
		amt -= 64;
	}
	while (amt > 0) {
		a.exp++;
		a.sticky |= a.round;
		a.round = a.guard;
		a.guard = a.mant.low & 1;
		a.mant = int128_shr(a.mant, 1);
		amt--;
	}
	return a;
}

#define int128_add  kvx_int128_add
static inline int128_t kvx_int128_add(int128_t a, int128_t b)
{
	int128_t ret;
	ret.low = a.low + b.low;
	if ((ret.low < a.low) || (ret.low < b.low)) {
		/* carry into high part */
		a.high += 1;
	}
	ret.high = a.high + b.high;
	return ret;
}

#define int128_sub kvx_int128_sub
static inline int128_t kvx_int128_sub(int128_t a, int128_t b, int borrow)
{
	int128_t ret;
	ret.low = a.low - b.low;
	if (ret.low > a.low) {
		/* borrow into high part */
		a.high -= 1;
	}
	ret.high = a.high - b.high;
	if (borrow == 0) {
		return ret;
	} else {
		a.high = 0;
		a.low = 1;
		return int128_sub(ret, a, 0);
	}
}

/* Return an infinity with the same sign as a */
static inline df_t infinite_df_t(xf_t a)
{
	df_t ret;
	ret.x.sign = a.sign;
	ret.x.exp = DF_INF_EXP;
	ret.x.mant = 0ULL;
	return ret;
}

/* Return a maximum finite value with the same sign as a */
static inline df_t maxfinite_df_t(xf_t a)
{
	df_t ret;
	ret.x.sign = a.sign;
	ret.x.exp = DF_INF_EXP - 1;
	ret.x.mant = 0x000fffffffffffffULL;
	return ret;
}

static inline df_t f2df_t(double in)
{
	df_t ret;
	ret.f = in;
	return ret;
}

/* Return an infinity with the same sign as a */
static inline sf_t infinite_sf_t(xf_t a)
{
	sf_t ret;
	ret.x.sign = a.sign;
	ret.x.exp = SF_INF_EXP;
	ret.x.mant = 0ULL;
	return ret;
}

/* Return a maximum finite value with the same sign as a */
static inline sf_t maxfinite_sf_t(xf_t a)
{
	sf_t ret;
	ret.x.sign = a.sign;
	ret.x.exp = SF_INF_EXP - 1;
	ret.x.mant = 0x007fffffUL;
	return ret;
}

static inline sf_t f2sf_t(float in)
{
	sf_t ret;
	ret.f = in;
	return ret;
}

#define GEN_XF_ROUND(TYPE,MANTBITS,INF_EXP) \
TYPE xf_round_kvx_##TYPE(xf_t a); \
TYPE xf_round_kvx_##TYPE(xf_t a) \
{ \
	TYPE ret; \
	ret.i = 0; \
	ret.x.sign = a.sign; \
	if ((a.mant.high == 0) && (a.mant.low == 0) \
		&& ((a.guard | a.round | a.sticky) == 0)) { \
		/* result zero */ \
		/*switch (fegetround()) { */\
		/*case FE_DOWNWARD: */\
		/*	return f2##TYPE(-0.0); */\
		/*default: */\
		if(a.sign) return f2##TYPE(-0.0); \
		else return f2##TYPE(0.0); \
		/*} */\
	} \
	/* Normalize right */ \
	/* We want MANTBITS bits of mantissa plus the leading one. */ \
	/* That means that we want MANTBITS+1 bits, or 0x000000000000FF_FFFF */ \
	/* So we need to normalize right while the high word is non-zero and \
	 * while the low word is nonzero when masked with 0xffe0_0000_0000_0000 */ \
	xf_debug("input: ", a); \
	while ((a.mant.high != 0) || ((a.mant.low >> (MANTBITS+1)) != 0)) { \
		a = xf_norm_right(a, 1); \
	} \
	xf_debug("norm_right: ", a); \
	/* OK, now normalize left */ \
	/* We want to normalize left until we have a leading one in bit 24 */ \
	/* Theoretically, we only need to shift a maximum of one to the left if we \
	 * shifted out lots of bits from B, or if we had no shift / 1 shift sticky shoudl be 0  \
	 */ \
	while ((a.mant.low & (1ULL << MANTBITS)) == 0) { \
		a = xf_norm_left(a); \
	} \
	xf_debug("norm_left: ", a); \
	/* OK, now we might need to denormalize because of potential underflow.  We need \
	 * to do this before rounding, and rounding might make us normal again */ \
	while (a.exp <= 0) { \
		a = xf_norm_right(a, 1 - a.exp); \
		/* Do we have underflow?  That's when we get an inexact answer because we \
		 * ran out of bits in a denormal. */ \
		if (a.guard || a.round || a.sticky) { \
			/*feraiseexcept(FE_UNDERFLOW);*/ \
		} \
	} \
	xf_debug("norm_denorm: ", a); \
	/* OK, we're relatively canonical... now we need to round */ \
	if (a.guard || a.round || a.sticky) { \
		/*feraiseexcept(FE_INEXACT);*/ \
		/*switch (fegetround()) { */\
		/*case FE_TOWARDZERO: */\
		/*	 Chop and we're done */ \
		/*	break; */\
		/*case FE_UPWARD: */\
		/*	if (a.sign == 0) a.mant.low += 1; */\
		/*	break; */\
		/*case FE_DOWNWARD: */\
		/*	if (a.sign != 0) a.mant.low += 1; */\
		/*	break; */\
		/*default: */\
			if (a.round || a.sticky) { \
				/* round up if guard is 1, down if guard is zero */ \
				a.mant.low += a.guard; \
			} else if (a.guard) { \
				/* exactly .5, round up if odd */ \
				a.mant.low += (a.mant.low & 1); \
			} \
			/*break; */\
		/*}*/ \
	} \
	xf_debug("post_round: ", a); \
	/* OK, now we might have carried all the way up.  So we might need to shr once */ \
	/* at least we know that the lsb should be zero if we rounded and got a carry out... */ \
	if ((a.mant.low >> (MANTBITS+1)) != 0) { \
		a = xf_norm_right(a, 1); \
	} \
	xf_debug("once_norm_right: ", a); \
	/* Overflow? */ \
	if (a.exp >= INF_EXP) { \
		/* Yep, inf result */ \
		xf_debug("inf: ", a); \
		/*feraiseexcept(FE_OVERFLOW);*/ \
		/*feraiseexcept(FE_INEXACT);*/ \
		/*switch (fegetround()) { */\
		/*case FE_TOWARDZERO: */\
		/*	return maxfinite_##TYPE(a); */\
		/*case FE_UPWARD: */\
		/*	if (a.sign == 0) */\
		/*		return infinite_##TYPE(a); */\
		/*	else */\
		/*		return maxfinite_##TYPE(a); */\
		/*case FE_DOWNWARD: */\
		/*	if (a.sign != 0) */\
		/*		return infinite_##TYPE(a); */\
		/*	else */\
		/*		return maxfinite_##TYPE(a); */\
		/*default: */\
			return infinite_##TYPE(a); \
		/*} */\
	} \
	/* Underflow? */ \
	if (a.mant.low & (1ULL << MANTBITS)) { \
		/* Leading one means: No, we're normal. So, we should be done... */ \
		xf_debug("norm: ", a); \
		ret.x.exp = a.exp; \
		ret.x.mant = a.mant.low; \
		return ret; \
	} \
	xf_debug("denorm: ", a); \
	if (a.exp != 1) \
		/*printf("a.exp == %d\n", a.exp);*/ \
	assert(a.exp == 1); \
	ret.x.exp = 0; \
	ret.x.mant = a.mant.low; \
	return ret; \
}

#define GEN_HF_ROUND(TYPE,MANTBITS,INF_EXP) \
TYPE hf_round_##TYPE(xf_t a); \
TYPE hf_round_##TYPE(xf_t a) \
{ \
	TYPE ret; \
	ret.i = 0; \
	ret.x.sign = a.sign; \
	if ((a.mant.high == 0) && (a.mant.low == 0) \
		&& ((a.guard | a.round | a.sticky) == 0)) { \
		/* result zero */ \
		/*switch (fegetround()) { */\
		/*case FE_DOWNWARD: */\
		/*	return f2##TYPE(-0.0); */\
		/*default: */\
		if(a.sign) return f2##TYPE(-0.0); \
		else return f2##TYPE(0.0); \
		/*} */\
	} \
	/* Normalize right */ \
	/* We want MANTBITS bits of mantissa plus the leading one. */ \
	/* That means that we want MANTBITS+1 bits, or 0x000000000000FF_FFFF */ \
	/* So we need to normalize right while the high word is non-zero and \
	 * while the low word is nonzero when masked with 0xffe0_0000_0000_0000 */ \
	xf_debug("input: ", a); \
	while ((a.mant.high != 0) || ((a.mant.low >> (MANTBITS+1)) != 0)) { \
		a = xf_norm_right(a, 1); \
	} \
	xf_debug("norm_right: ", a); \
	/* OK, now normalize left */ \
	/* We want to normalize left until we have a leading one in bit 24 */ \
	/* Theoretically, we only need to shift a maximum of one to the left if we \
	 * shifted out lots of bits from B, or if we had no shift / 1 shift sticky shoudl be 0  \
	 */ \
	while ((a.mant.low & (1ULL << MANTBITS)) == 0) { \
		a = xf_norm_left(a); \
	} \
	xf_debug("norm_left: ", a); \
	/* OK, now we might need to denormalize because of potential underflow.  We need \
	 * to do this before rounding, and rounding might make us normal again */ \
	while (a.exp <= 0) { \
		a = xf_norm_right(a, 1 - a.exp); \
		/* Do we have underflow?  That's when we get an inexact answer because we \
		 * ran out of bits in a denormal. */ \
		if (a.guard || a.round || a.sticky) { \
			/*feraiseexcept(FE_UNDERFLOW);*/ \
		} \
	} \
	xf_debug("norm_denorm: ", a); \
	/* OK, we're relatively canonical... now we need to round */ \
	/*if (a.guard || a.round || a.sticky) { */\
		/*feraiseexcept(FE_INEXACT);*/ \
		/*switch (fegetround()) { */\
		/*case FE_TOWARDZERO: */\
		/*	   Chop and we're done */ \
		/*	break; */\
		/*case FE_UPWARD: */\
		/*	if (a.sign == 0) a.mant.low += 1; */\
		/*	break; */\
		/*case FE_DOWNWARD: */\
		/*	if (a.sign != 0) a.mant.low += 1; */\
		/*	break; */\
		/*default: */\
			if (a.round || a.sticky || a.guard) { \
				/* round up if guard is 1, down if guard is zero */ \
				if ((a.mant.low & 0xFFF) == 0) a.mant.low += 1;  \
	/*		} else if (a.guard) {*/ \
				/* exactly .5, round up if odd */ \
	/*			a.mant.low += (a.mant.low & 1); */\
			} \
			/*break; */\
		/*}*/ \
	/*} */\
	xf_debug("post_round: ", a); \
	/* OK, now we might have carried all the way up.  So we might need to shr once */ \
	/* at least we know that the lsb should be zero if we rounded and got a carry out... */ \
	if ((a.mant.low >> (MANTBITS+1)) != 0) { \
		a = xf_norm_right(a, 1); \
	} \
	xf_debug("once_norm_right: ", a); \
	/* Overflow? */ \
	if (a.exp >= INF_EXP) { \
		/* Yep, inf result */ \
		xf_debug("inf: ", a); \
		/*feraiseexcept(FE_OVERFLOW);*/ \
		/*feraiseexcept(FE_INEXACT);*/ \
		/*switch (fegetround()) { */\
		/*case FE_TOWARDZERO: */\
		/*	return maxfinite_##TYPE(a); */\
		/*case FE_UPWARD: */\
		/*	if (a.sign == 0) */\
		/*		return infinite_##TYPE(a); */\
		/*	else */\
		/*		return maxfinite_##TYPE(a); */\
		/*case FE_DOWNWARD: */\
		/*	if (a.sign != 0) */\
		/*		return infinite_##TYPE(a); */\
		/*	else */\
		/*		return maxfinite_##TYPE(a); */\
		/*default: */\
			return infinite_##TYPE(a); \
		/*} */\
	} \
	/* Underflow? */ \
	if (a.mant.low & (1ULL << MANTBITS)) { \
		/* Leading one means: No, we're normal. So, we should be done... */ \
		xf_debug("norm: ", a); \
		ret.x.exp = a.exp; \
		ret.x.mant = a.mant.low; \
		return ret; \
	} \
	xf_debug("denorm: ", a); \
	if (a.exp != 1) \
		/*printf("a.exp == %d\n", a.exp);*/ \
	assert(a.exp == 1); \
	ret.x.exp = 0; \
	ret.x.mant = a.mant.low; \
	return ret; \
}


GEN_XF_ROUND(df_t,DF_MANTBITS(),DF_INF_EXP)
GEN_XF_ROUND(sf_t,SF_MANTBITS(),SF_INF_EXP)
GEN_HF_ROUND(sf_t,SF_MANTBITS(),SF_INF_EXP)

#define int128_mult_6464 kvx_int128_mult_6464
static inline int128_t kvx_int128_mult_6464(size8u_t ai, size8u_t bi)
{
	int128_t ret;
	int128_t a, b;
	size8u_t pp0, pp1a, pp1b, pp1s, pp2;

#ifdef DEBUG
	printf("ai/bi: 0x%016llx/0x%016llx\n", ai, bi);
#endif
	a.high = b.high = 0;
	a.low = ai;
	b.low = bi;
	pp0 = (size8u_t) a.w0 * (size8u_t) b.w0;
	pp1a = (size8u_t) a.w1 * (size8u_t) b.w0;
	pp1b = (size8u_t) b.w1 * (size8u_t) a.w0;
	pp2 = (size8u_t) a.w1 * (size8u_t) b.w1;
#ifdef DEBUG
	printf("pp2/1b/1a/0: 0x%016llx/0x%016llx/0x%016llx/0x%016llx\n",
				  pp2, pp1b, pp1a, pp0);
#endif
	pp1s = pp1a + pp1b;
	if ((pp1s < pp1a) || (pp1s < pp1b)) {
		pp2 += (1ULL << 32);
	}
	ret.low = pp0 + (pp1s << 32);
	if ((ret.low < pp0) || (ret.low < (pp1s << 32)))
		pp2 += 1;
	ret.high = pp2 + (pp1s >> 32);
#ifdef DEBUG
	printf("pp1s/rethi/retlo: 0x%016llx/0x%016llx/0x%016llx\n",
				  pp1s, ret.high, ret.low);
#endif
	return ret;
}

xf_t xf_add_kvx(xf_t a, xf_t b);

xf_t xf_sub_kvx(xf_t a, xf_t b, int negate);
xf_t xf_sub_kvx(xf_t a, xf_t b, int negate)
{
	xf_t ret;
	xf_init(&ret);
	int borrow;
	xf_debug("-->Sub/a: ", a);
	xf_debug("-->Sub/b: ", b);
	if (a.sign != b.sign) {
		b.sign = !b.sign;
		return xf_add_kvx(a, b);
	}
	if (b.exp > a.exp) {
		/* small - big == - (big - small) */
		return xf_sub_kvx(b, a, !negate);
	}
	if ((b.exp == a.exp) && (int128_gt(b.mant, a.mant))) {
		/* small - big == - (big - small) */
		return xf_sub_kvx(b, a, !negate);
	}
	xf_debug("OK: Sub/a: ", a);
	xf_debug("OK: Sub/b: ", b);
	while (a.exp > b.exp) {
		/* Try to normalize exponents: shrink a exponent and grow mantissa */
		if (a.mant.high & (1ULL << 62)) {
			/* Can't grow a any more */
			break;
		} else {
			a = xf_norm_left(a);
		}
	}
	xf_debug("norm_l: Sub/a: ", a);
	xf_debug("norm_l: Sub/b: ", b);
	while (a.exp > b.exp) {
		/* Try to normalize exponents: grow b exponent and shrink mantissa */
		/* Keep around shifted out bits... we might need those later */
		b = xf_norm_right(b, a.exp - b.exp);
	}
	xf_debug("norm_r: Sub/a: ", a);
	xf_debug("norm_r: Sub/b: ", b);
	if ((int128_gt(b.mant, a.mant))) {
		xf_debug("retry: Sub/a: ", a);
		xf_debug("retry: Sub/b: ", b);
		return xf_sub_kvx(b, a, !negate);
	}
	/* OK, now things should be normalized! */
	ret.sign = a.sign;
	ret.exp = a.exp;
	assert(!int128_gt(b.mant, a.mant));
	borrow = (b.round << 2) | (b.guard << 1) | b.sticky;
	ret.mant = int128_sub(a.mant, b.mant, (borrow != 0));
	borrow = 0 - borrow;
	ret.guard = (borrow >> 2) & 1;
	ret.round = (borrow >> 1) & 1;
	ret.sticky = (borrow >> 0) & 1;
	if (negate)
		ret.sign = !ret.sign;
        //According to the IEEE standard, Zero result in a subtraction should always be positive
	if ((ret.sign) && ((ret.mant.high == 0) && (ret.mant.low == 0) && ((ret.guard | ret.round | ret.sticky) == 0)))
		ret.sign = !ret.sign;
        xf_debug("ret: Sub ", ret);
	return ret;
}


xf_t xf_add_kvx(xf_t a, xf_t b)
{
	xf_t ret;
	xf_init(&ret);
	xf_debug("-->Add/a: ", a);
	xf_debug("-->Add/b: ", b);
	if (a.sign != b.sign) {
		b.sign = !b.sign;
		return xf_sub_kvx(a, b, 0);
	}
	if (b.exp > a.exp) {
		/* small + big ==  (big + small) */
		return xf_add_kvx(b, a);
	}
	if ((b.exp == a.exp) && int128_gt(b.mant, a.mant)) {
		/* small + big ==  (big + small) */
		return xf_add_kvx(b, a);
	}
	xf_debug("OK? Add/a: ", a);
	xf_debug("OK? Add/b: ", b);
	while (a.exp > b.exp) {
		/* Try to normalize exponents: shrink a exponent and grow mantissa */
		if (a.mant.high & (1ULL << 62)) {
			/* Can't grow a any more */
			break;
		} else {
			a = xf_norm_left(a);
		}
	}
	xf_debug("norm_l: Add/a: ", a);
	xf_debug("norm_l: Add/b: ", b);
	while (a.exp > b.exp) {
		/* Try to normalize exponents: grow b exponent and shrink mantissa */
		/* Keep around shifted out bits... we might need those later */
		b = xf_norm_right(b, a.exp - b.exp);
	}
	xf_debug("norm_r: Add/a: ", a);
	xf_debug("norm_r: Add/b: ", b);
	/* OK, now things should be normalized! */
	if (int128_gt(b.mant, a.mant)) {
		xf_debug("retry: Add/a: ", a);
		xf_debug("retry: Add/b: ", b);
		return xf_add_kvx(b, a);
	};
	ret.sign = a.sign;
	ret.exp = a.exp;
	assert(!int128_gt(b.mant, a.mant));
	ret.mant = int128_add(a.mant, b.mant);
	ret.guard = b.guard;
	ret.round = b.round;
	ret.sticky = b.sticky;
        xf_debug("ret: Add ", ret);
	return ret;
}


float internal_fma_kvx(float a_in, float b_in, float c_in, int scale);
float internal_fma_kvx(float a_in, float b_in, float c_in, int scale)
{
	sf_t a, b, c;
	xf_t prod;
	xf_t acc;
	xf_t result;
#if 0
	df_t t;
	fexcept_t flags_tmp;
#endif
	xf_init(&prod);
	xf_init(&acc);
	xf_init(&result);
	a.f = a_in;
	b.f = b_in;
	c.f = c_in;
//	printf("internal_fma_kvxx: 0x%016x * 0x%016x + 0x%016x sc: %d\n",
//				  fUNFLOAT(a_in), fUNFLOAT(b_in), fUNFLOAT(c_in), scale);
//	if (isinf(a.f) || isinf(b.f) || isinf(c.f))
//		return special_fmaf(a, b, c);
//	if (isnan(a.f) || isnan(b.f) || isnan(c.f))
//		return special_fmaf(a, b, c);
	if ((scale == 0) && (isz(a.f) || isz(b.f)))
		return (a.f * b.f + c.f);
	/* Is a*b exact?  If so, we don't have to go the slow way */
	/* EJP: axe this for simplicity? */
#if 0
	fegetexceptflag(&flags_tmp, FE_ALL_EXCEPT);
	feclearexcept(FE_ALL_EXCEPT);
	t.f = a.f * b.f;
	if (0 && (scale == 0) && isfinite(t.f)
		&& fetestexcept(FE_ALL_EXCEPT) == 0) {
		/* It's exactly correct, we can just do the add and return */
		fesetexceptflag(&flags_tmp, FE_ALL_EXCEPT);
		asm volatile ("");
		t.f = (t.f + c.f);
		return t.f;
	}
	fesetexceptflag(&flags_tmp, FE_ALL_EXCEPT);
#endif
	/* (a * 2**b) * (c * 2**d) == a*c * 2**(b+d) */
	prod.mant = int128_mult_6464(sf_getmant_kvx(a), sf_getmant_kvx(b));
	/* Note: extracting the mantissa into an int is multiplying by 2**23, so adjust here: */
	prod.exp = sf_getexp_kvx(a) + sf_getexp_kvx(b) - SF_BIAS - 23;
	prod.sign = a.x.sign ^ b.x.sign;
	if (isz(a.f) || isz(b.f)) prod.exp = -2*WAY_BIG_EXP;
	xf_debug("prod: ", prod);
	if ((scale > 0) /*&& (fpclassify(c.f) == FP_SUBNORMAL)*/) {
		acc.mant = int128_mult_6464(0,0);
		acc.exp = -WAY_BIG_EXP;
		acc.sign = c.x.sign;
		acc.sticky = 1;
		xf_debug("special denorm acc: ",acc);
		result = xf_add_kvx(prod,acc);
	} else if (!isz(c.f)) {
		acc.mant = int128_mult_6464(sf_getmant_kvx(c), 1);
		acc.exp = sf_getexp_kvx(c);
		acc.sign = c.x.sign;
		xf_debug("acc: ", acc);
		result = xf_add_kvx(prod, acc);
	} else {
		result = prod;
	}
	xf_debug("sum: ", result);
#ifdef DEBUG
	printf("Scaling: %d\n", scale);
#endif
	result.exp += scale;
	xf_debug("post-scale: ", result);
	return hf_round_sf_t(result).f;
}

// result = (a*c) + (b*d) + acc
float internal_vdmpy_acc(float a_in, float b_in, float c_in, float d_in, float acc_in, int scale);
float internal_vdmpy_acc(float a_in, float b_in, float c_in, float d_in, float acc_in, int scale)
{
	sf_t a, b, c, d, accm;
	xf_t prod1; //a*c
	xf_t prod2; //b*d
	xf_t acc;
	xf_t result_temp;
	xf_t result;

        xf_init(&prod1);
        xf_init(&prod2);
	xf_init(&acc);
	xf_init(&result_temp);
	xf_init(&result);

	a.f = a_in;
	b.f = b_in;
	c.f = c_in;
	d.f = d_in;
        accm.f = acc_in;

        /* (a * 2**b) * (c * 2**d) == a*c * 2**(b+d) */
	prod1.mant = int128_mult_6464(sf_getmant_kvx(a), sf_getmant_kvx(c));
	/* Note: extracting the mantissa into an int is multiplying by 2**23, so adjust here: */
	prod1.exp = sf_getexp_kvx(a) + sf_getexp_kvx(c) - SF_BIAS - 23;
	prod1.sign = a.x.sign ^ c.x.sign;

        /* (a * 2**b) * (c * 2**d) == a*c * 2**(b+d) */
	prod2.mant = int128_mult_6464(sf_getmant_kvx(b), sf_getmant_kvx(d));
	/* Note: extracting the mantissa into an int is multiplying by 2**23, so adjust here: */
	prod2.exp = sf_getexp_kvx(b) + sf_getexp_kvx(d) - SF_BIAS - 23;
	prod2.sign = b.x.sign ^ d.x.sign;


	if (isz(a.f) || isz(c.f)) prod1.exp = -2*WAY_BIG_EXP;
	if (isz(b.f) || isz(d.f)) prod2.exp = -2*WAY_BIG_EXP;

	xf_debug("prod1: ", prod1);
	xf_debug("prod2: ", prod2);

	if ((scale > 0) /*&& (fpclassify(c.f) == FP_SUBNORMAL)*/) {
		acc.mant = int128_mult_6464(0,0);
		acc.exp = -WAY_BIG_EXP;
		acc.sign = c.x.sign;
		acc.sticky = 1;
		xf_debug("special denorm acc: ",acc);
		//result = xf_add_kvx(prod,acc);
	} else /*if (!isz(accm.f)) */{
		acc.mant = int128_mult_6464(sf_getmant_kvx(accm), 1);
		acc.exp  = sf_getexp_kvx(accm);
		acc.sign = accm.x.sign;
		xf_debug("acc: ", acc);
		//result = xf_add_kvx(prod, acc);
	} /*else {
		result = xf_add_kvx(prod1, prod2);
	}*/

        //Add the 3 numbers: prod1 prod2 acc
        //result_temp = xf_add_kvx(prod1,prod2);
        //result = xf_add_kvx(result_temp,acc);
        result_temp = xf_add_kvx(prod1,prod2);
        result = xf_add_kvx(result_temp,acc);

	xf_debug("sum: ", result);
#ifdef DEBUG
	printf("Scaling: %d\n", scale);
#endif
	result.exp += scale;
	xf_debug("post-scale: ", result);
	return xf_round_kvx_sf_t(result).f;
}


uint32_t fp_vdmpy_acc  (uint32_t acc,uint16_t op1_u,uint16_t op1_l,uint16_t op2_u,uint16_t op2_l)
{
    union ui32_f32 u_op;
    union ui32_f32 u_acc;
    union ui32_f32 u_rslt;

    uint32_t op1_u_f32, op1_l_f32, op2_u_f32, op2_l_f32;
    float f_op1_u, f_op1_l, f_op2_u, f_op2_l, f_acc;
    float f_prod_l = 0, f_prod_u = 0, rslt;
    uint32_t result;

#ifdef DEBUG
    printf("Debug : op1_u =0x%04x\n",op1_u);
    printf("Debug : op1_l =0x%04x\n",op1_l);
    printf("Debug : op2_u =0x%04x\n",op2_u);
    printf("Debug : op2_l =0x%04x\n",op2_l);
    printf("Debug : acc   =0x%08x\n",acc);
#endif

    if(isNaNF16UI(op1_u) || isNaNF16UI(op1_l) || isNaNF16UI(op2_u) || isNaNF16UI(op2_l) || isNaNF32UI(acc))
       return FP32_DEF_NAN;

    op1_u_f32 = f16_to_f32(op1_u);
    op1_l_f32 = f16_to_f32(op1_l);
    op2_u_f32 = f16_to_f32(op2_u);
    op2_l_f32 = f16_to_f32(op2_l);

#ifdef DEBUG
    printf("Debug : op1_u_f32 =0x%08x\n",op1_u_f32);
    printf("Debug : op1_l_f32 =0x%08x\n",op1_l_f32);
    printf("Debug : op2_u_f32 =0x%08x\n",op2_u_f32);
    printf("Debug : op2_l_f32 =0x%08x\n",op2_l_f32);
#endif

    u_op.ui = op1_u_f32;
    f_op1_u = u_op.f;

    u_op.ui = op1_l_f32;
    f_op1_l = u_op.f;

    u_op.ui = op2_l_f32;
    f_op2_l = u_op.f;

    u_op.ui = op2_u_f32;
    f_op2_u = u_op.f;

    u_acc.ui = acc;
    f_acc   = u_acc.f;

#ifdef DEBUG
    printf("Debug_0 : f_op1_u = %f\n",f_op1_u);
    printf("Debug_0 : f_op1_l   = %f\n",f_op1_l);
    printf("Debug_0 : f_op2_u = %f\n",f_op2_u);
    printf("Debug_0 : f_op2_l   = %f\n",f_op2_l);
    printf("Debug_0 : f_acc   = %f\n",f_acc);
#endif

    f_prod_l =  (f_op1_l * f_op2_l);
    f_prod_u =  (f_op1_u * f_op2_u);

    if(isInfF16UI(op1_u) || isInfF16UI(op1_l) || isInfF16UI(op2_u) || isInfF16UI(op2_l) || isInfF32UI(acc))
    {
       rslt     =  (f_prod_u + f_prod_l + f_acc);
#ifdef DEBUG
       printf("Debug_inf : rslt = %f\n",rslt);
#endif
       u_rslt.f = rslt;
       result = u_rslt.ui;
#ifdef DEBUG
       printf("Debug_inf : result =0x%08x\n",result);
#endif
       result = isNaNF32UI(result) ? FP32_DEF_NAN : result;
#ifdef DEBUG
       printf("Debug_inf : result final =0x%08x\n",result);
#endif
       return result;
    }

    //If any of the below is a zero, we can use easy approach
    if(isz(f_prod_l) || isz(f_prod_u) || isz(f_acc))
    {
       rslt     =  (f_prod_u + f_prod_l + f_acc);
#ifdef DEBUG
       printf("Debug_inf : rslt = %f\n",rslt);
#endif
       u_rslt.f = rslt;
       result = u_rslt.ui;
#ifdef DEBUG
       printf("Debug_inf : result =0x%08x\n",result);
#endif
       result = isNaNF32UI(result) ? FP32_DEF_NAN : result;
#ifdef DEBUG
       printf("Debug_inf : result final =0x%08x\n",result);
#endif
       return result;
    }


////----------------------------------------------------------------------------------------------------
//    f_prod_l =  (f_op1_l * f_op2_l);
//    f_prod_u =  (f_op1_u * f_op2_u);
//
//    printf("Debug_1 : f_prod_l = %f\n",f_prod_l);
//    printf("Debug_1 : f_prod_u = %f\n",f_prod_u);
//
//    rslt     =  (f_prod_u + f_prod_l + f_acc);
//    printf("Debug_1 : rslt = %f\n",rslt);
//    u_rslt.f = rslt;
//    result = u_rslt.ui;
//    printf("Debug_1 : result =0x%08x\n",result);
////----------------------------------------------------------------------------------------------------

    rslt     =  internal_vdmpy_acc(f_op1_u, f_op1_l,f_op2_u,f_op2_l,f_acc,0);
    u_rslt.f = rslt;
    result = u_rslt.ui;
#ifdef DEBUG
    printf("Debug_2 : rslt = %f\n",rslt);
    printf("Debug_2 : result =0x%08x\n",result);
#endif

    result = isNaNF32UI(result) ? FP32_DEF_NAN : result;

#ifdef DEBUG
    printf("Debug : f_op1_u = %f\n",f_op1_u);
    printf("Debug : f_op1_l = %f\n",f_op1_l);
    printf("Debug : f_op2_u = %f\n",f_op2_u);
    printf("Debug : f_op2_l = %f\n",f_op2_l);
    printf("Debug : f_acc   = %f\n",f_acc);
    printf("Debug : f_prod_l = %f\n",f_prod_l);
    printf("Debug : f_prod_u = %f\n",f_prod_u);
    printf("Debug : rslt = %f\n",rslt);
    printf("Debug : result =0x%08x\n",result);
#endif

    return result;
}


uint16_t fp_mult_hf_hf_acc (uint16_t op1, uint16_t op2, uint16_t acc)
{
    union ui32_f32 u_op1;
    union ui32_f32 u_op2;
    union ui32_f32 u_acc;
    union ui32_f32 u_rslt;

    uint32_t op1_f32;
    uint32_t op2_f32;
    uint32_t acc_f32;

    float a,b,facc,rslt;
    uint32_t result_f32;
    uint16_t result;

#ifdef DEBUG
    printf("Debug : op1 =0x%04x\n",op1);
    printf("Debug : op2 =0x%04x\n",op2);
    printf("Debug : acc =0x%04x\n",acc);
#endif

    if(isNaNF16UI(op1) || isNaNF16UI(op2) || isNaNF16UI(acc))
       return FP16_DEF_NAN;

    op1_f32 = f16_to_f32(op1);
    op2_f32 = f16_to_f32(op2);
    acc_f32 = f16_to_f32(acc);

#ifdef DEBUG
    printf("Debug : op1_f32 = 0x%08x\n",op1_f32);
    printf("Debug : op2_f32 = 0x%08x\n",op2_f32);
    printf("Debug : acc_f32 = 0x%08x\n",acc_f32);
#endif

    u_op1.ui = op1_f32;
    u_op2.ui = op2_f32;
    u_acc.ui = acc_f32;
    a = u_op1.f;
    b = u_op2.f;
    facc = u_acc.f;

#ifdef DEBUG
    printf("Debug_1 : a = %f\n",a);
    printf("Debug_1 : b = %f\n",b);
    printf("Debug_1 : facc = %f\n",facc);
#endif

    if(isInfF16UI(op1) || isInfF16UI(op2) || isInfF16UI(acc))
    {
       rslt = (a * b) + facc;
#ifdef DEBUG
       printf("Debug_inf : rslt = %f\n",rslt);
#endif
       u_rslt.f = rslt;
       result_f32 = u_rslt.ui;
       result = f32_to_f16(result_f32);
#ifdef DEBUG
       printf("Debug_inf : result_f32 =0x%08x\n",result_f32);
       printf("Debug_inf : result =0x%04x\n",result);
#endif
       result = isNaNF16UI(result) ? FP16_DEF_NAN : result;
#ifdef DEBUG
       printf("Debug_inf : result final =0x%04x\n",result);
#endif
       return result;
    }

//    //----------------------------------------------------------------------------------------------------
//    rslt = (a * b) + facc;
//    u_rslt.f = rslt;
//    result_f32 = u_rslt.ui;
//    printf("Debug_3 : result_f32 =0x%08x\n",result_f32);
//    result = f32_to_f16(result_f32);
//    printf("Debug_3 : result =0x%04x\n",result);
//    //----------------------------------------------------------------------------------------------------

    //rslt = fma(a,b,facc);
    rslt = internal_fma_kvx(a, b, facc, 0);
    u_rslt.f = rslt;
    result_f32 = u_rslt.ui;
#ifdef DEBUG
    printf("Debug_2 : rslt = %f\n",rslt);
    printf("Debug_2 : result_f32 =0x%08x\n",result_f32);
#endif

    result = f32_to_f16(result_f32);

#ifdef DEBUG
    printf("Debug_2 : result =0x%04x\n",result);
#endif

    result = isNaNF16UI(result) ? FP16_DEF_NAN : result;

#ifdef DEBUG
    printf("Debug_2 : result final =0x%04x\n",result);
#endif

    return result;
}

