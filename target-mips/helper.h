#include "def-helper.h"

DEF_HELPER_3(raise_exception_err, noreturn, env, i32, int)
DEF_HELPER_2(raise_exception, noreturn, env, i32)

#ifdef TARGET_MIPS64
DEF_HELPER_4(ldl, tl, env, tl, tl, int)
DEF_HELPER_4(ldr, tl, env, tl, tl, int)
DEF_HELPER_4(sdl, void, env, tl, tl, int)
DEF_HELPER_4(sdr, void, env, tl, tl, int)
#endif
DEF_HELPER_4(lwl, tl, env, tl, tl, int)
DEF_HELPER_4(lwr, tl, env, tl, tl, int)
DEF_HELPER_4(swl, void, env, tl, tl, int)
DEF_HELPER_4(swr, void, env, tl, tl, int)

#ifndef CONFIG_USER_ONLY
DEF_HELPER_3(ll, tl, env, tl, int)
DEF_HELPER_4(sc, tl, env, tl, tl, int)
#ifdef TARGET_MIPS64
DEF_HELPER_3(lld, tl, env, tl, int)
DEF_HELPER_4(scd, tl, env, tl, tl, int)
#endif
#endif

DEF_HELPER_FLAGS_1(clo, TCG_CALL_CONST | TCG_CALL_PURE, tl, tl)
DEF_HELPER_FLAGS_1(clz, TCG_CALL_CONST | TCG_CALL_PURE, tl, tl)
#ifdef TARGET_MIPS64
DEF_HELPER_FLAGS_1(dclo, TCG_CALL_CONST | TCG_CALL_PURE, tl, tl)
DEF_HELPER_FLAGS_1(dclz, TCG_CALL_CONST | TCG_CALL_PURE, tl, tl)
DEF_HELPER_3(dmult, void, env, tl, tl)
DEF_HELPER_3(dmultu, void, env, tl, tl)
#endif

DEF_HELPER_3(muls, tl, env, tl, tl)
DEF_HELPER_3(mulsu, tl, env, tl, tl)
DEF_HELPER_3(macc, tl, env, tl, tl)
DEF_HELPER_3(maccu, tl, env, tl, tl)
DEF_HELPER_3(msac, tl, env, tl, tl)
DEF_HELPER_3(msacu, tl, env, tl, tl)
DEF_HELPER_3(mulhi, tl, env, tl, tl)
DEF_HELPER_3(mulhiu, tl, env, tl, tl)
DEF_HELPER_3(mulshi, tl, env, tl, tl)
DEF_HELPER_3(mulshiu, tl, env, tl, tl)
DEF_HELPER_3(macchi, tl, env, tl, tl)
DEF_HELPER_3(macchiu, tl, env, tl, tl)
DEF_HELPER_3(msachi, tl, env, tl, tl)
DEF_HELPER_3(msachiu, tl, env, tl, tl)

#ifndef CONFIG_USER_ONLY
/* CP0 helpers */
DEF_HELPER_1(mfc0_mvpcontrol, tl, env)
DEF_HELPER_1(mfc0_mvpconf0, tl, env)
DEF_HELPER_1(mfc0_mvpconf1, tl, env)
DEF_HELPER_1(mftc0_vpecontrol, tl, env)
DEF_HELPER_1(mftc0_vpeconf0, tl, env)
DEF_HELPER_1(mfc0_random, tl, env)
DEF_HELPER_1(mfc0_tcstatus, tl, env)
DEF_HELPER_1(mftc0_tcstatus, tl, env)
DEF_HELPER_1(mfc0_tcbind, tl, env)
DEF_HELPER_1(mftc0_tcbind, tl, env)
DEF_HELPER_1(mfc0_tcrestart, tl, env)
DEF_HELPER_1(mftc0_tcrestart, tl, env)
DEF_HELPER_1(mfc0_tchalt, tl, env)
DEF_HELPER_1(mftc0_tchalt, tl, env)
DEF_HELPER_1(mfc0_tccontext, tl, env)
DEF_HELPER_1(mftc0_tccontext, tl, env)
DEF_HELPER_1(mfc0_tcschedule, tl, env)
DEF_HELPER_1(mftc0_tcschedule, tl, env)
DEF_HELPER_1(mfc0_tcschefback, tl, env)
DEF_HELPER_1(mftc0_tcschefback, tl, env)
DEF_HELPER_1(mfc0_count, tl, env)
DEF_HELPER_1(mftc0_entryhi, tl, env)
DEF_HELPER_1(mftc0_status, tl, env)
DEF_HELPER_1(mftc0_cause, tl, env)
DEF_HELPER_1(mftc0_epc, tl, env)
DEF_HELPER_1(mftc0_ebase, tl, env)
DEF_HELPER_2(mftc0_configx, tl, env, tl)
DEF_HELPER_1(mfc0_lladdr, tl, env)
DEF_HELPER_2(mfc0_watchlo, tl, env, i32)
DEF_HELPER_2(mfc0_watchhi, tl, env, i32)
DEF_HELPER_1(mfc0_debug, tl, env)
DEF_HELPER_1(mftc0_debug, tl, env)
#ifdef TARGET_MIPS64
DEF_HELPER_1(dmfc0_tcrestart, tl, env)
DEF_HELPER_1(dmfc0_tchalt, tl, env)
DEF_HELPER_1(dmfc0_tccontext, tl, env)
DEF_HELPER_1(dmfc0_tcschedule, tl, env)
DEF_HELPER_1(dmfc0_tcschefback, tl, env)
DEF_HELPER_1(dmfc0_lladdr, tl, env)
DEF_HELPER_2(dmfc0_watchlo, tl, env, i32)
#endif /* TARGET_MIPS64 */

DEF_HELPER_2(mtc0_index, void, env, tl)
DEF_HELPER_2(mtc0_mvpcontrol, void, env, tl)
DEF_HELPER_2(mtc0_vpecontrol, void, env, tl)
DEF_HELPER_2(mttc0_vpecontrol, void, env, tl)
DEF_HELPER_2(mtc0_vpeconf0, void, env, tl)
DEF_HELPER_2(mttc0_vpeconf0, void, env, tl)
DEF_HELPER_2(mtc0_vpeconf1, void, env, tl)
DEF_HELPER_2(mtc0_yqmask, void, env, tl)
DEF_HELPER_2(mtc0_vpeopt, void, env, tl)
DEF_HELPER_2(mtc0_entrylo0, void, env, tl)
DEF_HELPER_2(mtc0_tcstatus, void, env, tl)
DEF_HELPER_2(mttc0_tcstatus, void, env, tl)
DEF_HELPER_2(mtc0_tcbind, void, env, tl)
DEF_HELPER_2(mttc0_tcbind, void, env, tl)
DEF_HELPER_2(mtc0_tcrestart, void, env, tl)
DEF_HELPER_2(mttc0_tcrestart, void, env, tl)
DEF_HELPER_2(mtc0_tchalt, void, env, tl)
DEF_HELPER_2(mttc0_tchalt, void, env, tl)
DEF_HELPER_2(mtc0_tccontext, void, env, tl)
DEF_HELPER_2(mttc0_tccontext, void, env, tl)
DEF_HELPER_2(mtc0_tcschedule, void, env, tl)
DEF_HELPER_2(mttc0_tcschedule, void, env, tl)
DEF_HELPER_2(mtc0_tcschefback, void, env, tl)
DEF_HELPER_2(mttc0_tcschefback, void, env, tl)
DEF_HELPER_2(mtc0_entrylo1, void, env, tl)
DEF_HELPER_2(mtc0_context, void, env, tl)
DEF_HELPER_2(mtc0_pagemask, void, env, tl)
DEF_HELPER_2(mtc0_pagegrain, void, env, tl)
DEF_HELPER_2(mtc0_wired, void, env, tl)
DEF_HELPER_2(mtc0_srsconf0, void, env, tl)
DEF_HELPER_2(mtc0_srsconf1, void, env, tl)
DEF_HELPER_2(mtc0_srsconf2, void, env, tl)
DEF_HELPER_2(mtc0_srsconf3, void, env, tl)
DEF_HELPER_2(mtc0_srsconf4, void, env, tl)
DEF_HELPER_2(mtc0_hwrena, void, env, tl)
DEF_HELPER_2(mtc0_count, void, env, tl)
DEF_HELPER_2(mtc0_entryhi, void, env, tl)
DEF_HELPER_2(mttc0_entryhi, void, env, tl)
DEF_HELPER_2(mtc0_compare, void, env, tl)
DEF_HELPER_2(mtc0_status, void, env, tl)
DEF_HELPER_2(mttc0_status, void, env, tl)
DEF_HELPER_2(mtc0_intctl, void, env, tl)
DEF_HELPER_2(mtc0_srsctl, void, env, tl)
DEF_HELPER_2(mtc0_cause, void, env, tl)
DEF_HELPER_2(mttc0_cause, void, env, tl)
DEF_HELPER_2(mtc0_ebase, void, env, tl)
DEF_HELPER_2(mttc0_ebase, void, env, tl)
DEF_HELPER_2(mtc0_config0, void, env, tl)
DEF_HELPER_2(mtc0_config2, void, env, tl)
DEF_HELPER_2(mtc0_lladdr, void, env, tl)
DEF_HELPER_3(mtc0_watchlo, void, env, tl, i32)
DEF_HELPER_3(mtc0_watchhi, void, env, tl, i32)
DEF_HELPER_2(mtc0_xcontext, void, env, tl)
DEF_HELPER_2(mtc0_framemask, void, env, tl)
DEF_HELPER_2(mtc0_debug, void, env, tl)
DEF_HELPER_2(mttc0_debug, void, env, tl)
DEF_HELPER_2(mtc0_performance0, void, env, tl)
DEF_HELPER_2(mtc0_taglo, void, env, tl)
DEF_HELPER_2(mtc0_datalo, void, env, tl)
DEF_HELPER_2(mtc0_taghi, void, env, tl)
DEF_HELPER_2(mtc0_datahi, void, env, tl)

/* MIPS MT functions */
DEF_HELPER_2(mftgpr, tl, env, i32);
DEF_HELPER_2(mftlo, tl, env, i32)
DEF_HELPER_2(mfthi, tl, env, i32)
DEF_HELPER_2(mftacx, tl, env, i32)
DEF_HELPER_1(mftdsp, tl, env)
DEF_HELPER_3(mttgpr, void, env, tl, i32)
DEF_HELPER_3(mttlo, void, env, tl, i32)
DEF_HELPER_3(mtthi, void, env, tl, i32)
DEF_HELPER_3(mttacx, void, env, tl, i32)
DEF_HELPER_2(mttdsp, void, env, tl)
DEF_HELPER_0(dmt, tl)
DEF_HELPER_0(emt, tl)
DEF_HELPER_1(dvpe, tl, env)
DEF_HELPER_1(evpe, tl, env)
#endif /* !CONFIG_USER_ONLY */

/* microMIPS functions */
DEF_HELPER_4(lwm, void, env, tl, tl, i32);
DEF_HELPER_4(swm, void, env, tl, tl, i32);
#ifdef TARGET_MIPS64
DEF_HELPER_4(ldm, void, env, tl, tl, i32);
DEF_HELPER_4(sdm, void, env, tl, tl, i32);
#endif

DEF_HELPER_2(fork, void, tl, tl)
DEF_HELPER_2(yield, tl, env, tl)

/* CP1 functions */
DEF_HELPER_2(cfc1, tl, env, i32)
DEF_HELPER_3(ctc1, void, env, tl, i32)

DEF_HELPER_2(float_cvtd_s, i64, env, i32)
DEF_HELPER_2(float_cvtd_w, i64, env, i32)
DEF_HELPER_2(float_cvtd_l, i64, env, i64)
DEF_HELPER_2(float_cvtl_d, i64, env, i64)
DEF_HELPER_2(float_cvtl_s, i64, env, i32)
DEF_HELPER_2(float_cvtps_pw, i64, env, i64)
DEF_HELPER_2(float_cvtpw_ps, i64, env, i64)
DEF_HELPER_2(float_cvts_d, i32, env, i64)
DEF_HELPER_2(float_cvts_w, i32, env, i32)
DEF_HELPER_2(float_cvts_l, i32, env, i64)
DEF_HELPER_2(float_cvts_pl, i32, env, i32)
DEF_HELPER_2(float_cvts_pu, i32, env, i32)
DEF_HELPER_2(float_cvtw_s, i32, env, i32)
DEF_HELPER_2(float_cvtw_d, i32, env, i64)

DEF_HELPER_3(float_addr_ps, i64, env, i64, i64)
DEF_HELPER_3(float_mulr_ps, i64, env, i64, i64)

#define FOP_PROTO(op)                            \
DEF_HELPER_2(float_ ## op ## l_s, i64, env, i32) \
DEF_HELPER_2(float_ ## op ## l_d, i64, env, i64) \
DEF_HELPER_2(float_ ## op ## w_s, i32, env, i32) \
DEF_HELPER_2(float_ ## op ## w_d, i32, env, i64)
FOP_PROTO(round)
FOP_PROTO(trunc)
FOP_PROTO(ceil)
FOP_PROTO(floor)
#undef FOP_PROTO

#define FOP_PROTO(op)                            \
DEF_HELPER_2(float_ ## op ## _s, i32, env, i32)  \
DEF_HELPER_2(float_ ## op ## _d, i64, env, i64)
FOP_PROTO(sqrt)
FOP_PROTO(rsqrt)
FOP_PROTO(recip)
#undef FOP_PROTO

#define FOP_PROTO(op)                       \
DEF_HELPER_1(float_ ## op ## _s, i32, i32)  \
DEF_HELPER_1(float_ ## op ## _d, i64, i64)  \
DEF_HELPER_1(float_ ## op ## _ps, i64, i64)
FOP_PROTO(abs)
FOP_PROTO(chs)
#undef FOP_PROTO

#define FOP_PROTO(op)                            \
DEF_HELPER_2(float_ ## op ## _s, i32, env, i32)  \
DEF_HELPER_2(float_ ## op ## _d, i64, env, i64)  \
DEF_HELPER_2(float_ ## op ## _ps, i64, env, i64)
FOP_PROTO(recip1)
FOP_PROTO(rsqrt1)
#undef FOP_PROTO

#define FOP_PROTO(op)                                  \
DEF_HELPER_3(float_ ## op ## _s, i32, env, i32, i32)   \
DEF_HELPER_3(float_ ## op ## _d, i64, env, i64, i64)   \
DEF_HELPER_3(float_ ## op ## _ps, i64, env, i64, i64)
FOP_PROTO(add)
FOP_PROTO(sub)
FOP_PROTO(mul)
FOP_PROTO(div)
FOP_PROTO(recip2)
FOP_PROTO(rsqrt2)
#undef FOP_PROTO

#define FOP_PROTO(op)                                      \
DEF_HELPER_4(float_ ## op ## _s, i32, env, i32, i32, i32)  \
DEF_HELPER_4(float_ ## op ## _d, i64, env, i64, i64, i64)  \
DEF_HELPER_4(float_ ## op ## _ps, i64, env, i64, i64, i64)
FOP_PROTO(muladd)
FOP_PROTO(mulsub)
FOP_PROTO(nmuladd)
FOP_PROTO(nmulsub)
#undef FOP_PROTO

#define FOP_PROTO(op)                                    \
DEF_HELPER_4(cmp_d_ ## op, void, env, i64, i64, int)     \
DEF_HELPER_4(cmpabs_d_ ## op, void, env, i64, i64, int)  \
DEF_HELPER_4(cmp_s_ ## op, void, env, i32, i32, int)     \
DEF_HELPER_4(cmpabs_s_ ## op, void, env, i32, i32, int)  \
DEF_HELPER_4(cmp_ps_ ## op, void, env, i64, i64, int)    \
DEF_HELPER_4(cmpabs_ps_ ## op, void, env, i64, i64, int)
FOP_PROTO(f)
FOP_PROTO(un)
FOP_PROTO(eq)
FOP_PROTO(ueq)
FOP_PROTO(olt)
FOP_PROTO(ult)
FOP_PROTO(ole)
FOP_PROTO(ule)
FOP_PROTO(sf)
FOP_PROTO(ngle)
FOP_PROTO(seq)
FOP_PROTO(ngl)
FOP_PROTO(lt)
FOP_PROTO(nge)
FOP_PROTO(le)
FOP_PROTO(ngt)
#undef FOP_PROTO

/* Special functions */
#ifndef CONFIG_USER_ONLY
DEF_HELPER_1(tlbwi, void, env)
DEF_HELPER_1(tlbwr, void, env)
DEF_HELPER_1(tlbp, void, env)
DEF_HELPER_1(tlbr, void, env)
DEF_HELPER_1(di, tl, env)
DEF_HELPER_1(ei, tl, env)
DEF_HELPER_1(eret, void, env)
DEF_HELPER_1(deret, void, env)
#endif /* !CONFIG_USER_ONLY */
DEF_HELPER_1(rdhwr_cpunum, tl, env)
DEF_HELPER_1(rdhwr_synci_step, tl, env)
DEF_HELPER_1(rdhwr_cc, tl, env)
DEF_HELPER_1(rdhwr_ccres, tl, env)
DEF_HELPER_2(pmon, void, env, int)
DEF_HELPER_1(wait, void, env)

#include "def-helper.h"
