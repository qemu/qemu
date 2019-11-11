/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#ifndef ARCH_H
#define ARCH_H

#include "global_types.h"
#include "arch_types.h"
#include "cpu.h"

extern size1u_t rLPS_table_64x4[64][4];
extern size1u_t AC_next_state_MPS_64[64];
extern size1u_t AC_next_state_LPS_64[64];

extern size4u_t fbrevaddr(size4u_t pointer);
extern size4u_t count_ones_2(size2u_t src);
extern size4u_t count_ones_4(size4u_t src);
extern size4u_t count_ones_8(size8u_t src);
extern size4u_t count_leading_ones_8(size8u_t src);
extern size4u_t count_leading_ones_4(size4u_t src);
extern size4u_t count_leading_ones_2(size2u_t src);
extern size4u_t count_leading_ones_1(size1u_t src);
extern size8u_t reverse_bits_8(size8u_t src);
extern size4u_t reverse_bits_4(size4u_t src);
extern size4u_t reverse_bits_2(size2u_t src);
extern size4u_t reverse_bits_1(size1u_t src);
extern size8u_t exchange(size8u_t bits, size4u_t cntrl);
extern size8u_t interleave(size4u_t odd, size4u_t even);
extern size8u_t deinterleave(size8u_t src);
extern size4u_t carry_from_add64(size8u_t a, size8u_t b, size4u_t c);
extern size4s_t conv_round(size4s_t a, int n);
extern size16s_t cast8s_to_16s(size8s_t a);
extern size8s_t cast16s_to_8s(size16s_t a);
extern size4s_t cast16s_to_4s(size16s_t a);
extern size16s_t add128(size16s_t a, size16s_t b);
extern size16s_t sub128(size16s_t a, size16s_t b);
extern size16s_t shiftr128(size16s_t a, size4u_t n);
extern size16s_t shiftl128(size16s_t a, size4u_t n);
extern size16s_t and128(size16s_t a, size16s_t b);
extern size8s_t conv_round64(size8s_t a, size4u_t n);
extern void arch_fpop_start(CPUHexagonState *env);
extern void arch_fpop_end(CPUHexagonState *env);
extern void arch_raise_fpflag(unsigned int flags);
extern int arch_df_recip_common(size8s_t * Rs, size8s_t * Rt, size8s_t * Rd, int *adjust);
extern int arch_sf_recip_common(reg_t * Rs, reg_t * Rt, reg_t * Rd, int *adjust);
extern int arch_sf_invsqrt_common(reg_t * Rs, reg_t * Rd, int *adjust);
extern int arch_df_invsqrt_common(size8s_t * Rs, size8s_t * Rd, int *adjust);
extern int arch_recip_lookup(int index);
extern int arch_invsqrt_lookup(int index);
extern void arch_test_sf_recip(float n, float d, float *inv_out, float *n_out,
						float *d_out, int *adj_out);
extern void arch_test_df_recip(double n, double d, double *inv_out, double *n_out,
						double *d_out, int *adj_out);
extern void arch_test_sf_invsqrt(float r, float *invsqrt_out, float *r_out,
						  int *adj_out);
extern void arch_test_df_invsqrt(double r, double *invsqrt_out, double *r_out,
						  int *adj_out);
extern float arch_test_div(float n, float d);
extern double arch_test_divd(double n, double d);
extern float arch_test_sqrt(float r);
extern double arch_test_sqrtd(double r);

#endif
