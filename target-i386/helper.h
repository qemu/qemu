#define TCG_HELPER_PROTO

void TCG_HELPER_PROTO helper_divl_EAX_T0(target_ulong t0);
void TCG_HELPER_PROTO helper_idivl_EAX_T0(target_ulong t0);

/* x86 FPU */

void helper_flds_FT0(uint32_t val);
void helper_fldl_FT0(uint64_t val);
void helper_fildl_FT0(int32_t val);
void helper_flds_ST0(uint32_t val);
void helper_fldl_ST0(uint64_t val);
void helper_fildl_ST0(int32_t val);
void helper_fildll_ST0(int64_t val);
uint32_t helper_fsts_ST0(void);
uint64_t helper_fstl_ST0(void);
int32_t helper_fist_ST0(void);
int32_t helper_fistl_ST0(void);
int64_t helper_fistll_ST0(void);
int32_t helper_fistt_ST0(void);
int32_t helper_fisttl_ST0(void);
int64_t helper_fisttll_ST0(void);
void helper_fldt_ST0(target_ulong ptr);
void helper_fstt_ST0(target_ulong ptr);
void helper_fpush(void);
void helper_fpop(void);
void helper_fdecstp(void);
void helper_fincstp(void);
void helper_ffree_STN(int st_index);
void helper_fmov_ST0_FT0(void);
void helper_fmov_FT0_STN(int st_index);
void helper_fmov_ST0_STN(int st_index);
void helper_fmov_STN_ST0(int st_index);
void helper_fxchg_ST0_STN(int st_index);
void helper_fcom_ST0_FT0(void);
void helper_fucom_ST0_FT0(void);
void helper_fcomi_ST0_FT0(void);
void helper_fucomi_ST0_FT0(void);
void helper_fadd_ST0_FT0(void);
void helper_fmul_ST0_FT0(void);
void helper_fsub_ST0_FT0(void);
void helper_fsubr_ST0_FT0(void);
void helper_fdiv_ST0_FT0(void);
void helper_fdivr_ST0_FT0(void);
void helper_fadd_STN_ST0(int st_index);
void helper_fmul_STN_ST0(int st_index);
void helper_fsub_STN_ST0(int st_index);
void helper_fsubr_STN_ST0(int st_index);
void helper_fdiv_STN_ST0(int st_index);
void helper_fdivr_STN_ST0(int st_index);
void helper_fchs_ST0(void);
void helper_fabs_ST0(void);
void helper_fxam_ST0(void);
void helper_fld1_ST0(void);
void helper_fldl2t_ST0(void);
void helper_fldl2e_ST0(void);
void helper_fldpi_ST0(void);
void helper_fldlg2_ST0(void);
void helper_fldln2_ST0(void);
void helper_fldz_ST0(void);
void helper_fldz_FT0(void);
uint32_t helper_fnstsw(void);
uint32_t helper_fnstcw(void);
void helper_fldcw(uint32_t val);
void helper_fclex(void);
void helper_fwait(void);
void helper_fninit(void);
void helper_fbld_ST0(target_ulong ptr);
void helper_fbst_ST0(target_ulong ptr);
void helper_f2xm1(void);
void helper_fyl2x(void);
void helper_fptan(void);
void helper_fpatan(void);
void helper_fxtract(void);
void helper_fprem1(void);
void helper_fprem(void);
void helper_fyl2xp1(void);
void helper_fsqrt(void);
void helper_fsincos(void);
void helper_frndint(void);
void helper_fscale(void);
void helper_fsin(void);
void helper_fcos(void);
void helper_fxam_ST0(void);
void helper_fstenv(target_ulong ptr, int data32);
void helper_fldenv(target_ulong ptr, int data32);
void helper_fsave(target_ulong ptr, int data32);
void helper_frstor(target_ulong ptr, int data32);
void helper_fxsave(target_ulong ptr, int data64);
void helper_fxrstor(target_ulong ptr, int data64);

/* MMX/SSE */

void TCG_HELPER_PROTO helper_enter_mmx(void);
void TCG_HELPER_PROTO helper_emms(void);
void TCG_HELPER_PROTO helper_movq(uint64_t *d, uint64_t *s);

#define SHIFT 0
#include "ops_sse_header.h"
#define SHIFT 1
#include "ops_sse_header.h"

