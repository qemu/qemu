#ifndef DEF_HELPER
#define DEF_HELPER(ret, name, params) ret name params;
#endif

DEF_HELPER(void, do_raise_exception_err, (int excp, int err))
DEF_HELPER(void, do_raise_exception, (int excp))
DEF_HELPER(void, do_interrupt_restart, (void))

#ifdef TARGET_MIPS64
DEF_HELPER(target_ulong, do_ldl, (target_ulong t0, target_ulong t1, int mem_idx))
DEF_HELPER(target_ulong, do_ldr, (target_ulong t0, target_ulong t1, int mem_idx))
DEF_HELPER(void, do_sdl, (target_ulong t0, target_ulong t1, int mem_idx))
DEF_HELPER(void, do_sdr, (target_ulong t0, target_ulong t1, int mem_idx))
#endif
DEF_HELPER(target_ulong, do_lwl, (target_ulong t0, target_ulong t1, int mem_idx))
DEF_HELPER(target_ulong, do_lwr, (target_ulong t0, target_ulong t1, int mem_idx))
DEF_HELPER(void, do_swl, (target_ulong t0, target_ulong t1, int mem_idx))
DEF_HELPER(void, do_swr, (target_ulong t0, target_ulong t1, int mem_idx))

DEF_HELPER(target_ulong, do_clo, (target_ulong t0))
DEF_HELPER(target_ulong, do_clz, (target_ulong t0))
#ifdef TARGET_MIPS64
DEF_HELPER(target_ulong, do_dclo, (target_ulong t0))
DEF_HELPER(target_ulong, do_dclz, (target_ulong t0))
DEF_HELPER(void, do_dmult, (target_ulong t0, target_ulong t1))
DEF_HELPER(void, do_dmultu, (target_ulong t0, target_ulong t1))
#endif

DEF_HELPER(target_ulong, do_muls, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_mulsu, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_macc, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_maccu, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_msac, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_msacu, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_mulhi, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_mulhiu, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_mulshi, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_mulshiu, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_macchi, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_macchiu, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_msachi, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_msachiu, (target_ulong t0, target_ulong t1))

#ifndef CONFIG_USER_ONLY
/* CP0 helpers */
DEF_HELPER(target_ulong, do_mfc0_mvpcontrol, (void))
DEF_HELPER(target_ulong, do_mfc0_mvpconf0, (void))
DEF_HELPER(target_ulong, do_mfc0_mvpconf1, (void))
DEF_HELPER(target_ulong, do_mfc0_random, (void))
DEF_HELPER(target_ulong, do_mfc0_tcstatus, (void))
DEF_HELPER(target_ulong, do_mftc0_tcstatus, (void))
DEF_HELPER(target_ulong, do_mfc0_tcbind, (void))
DEF_HELPER(target_ulong, do_mftc0_tcbind, (void))
DEF_HELPER(target_ulong, do_mfc0_tcrestart, (void))
DEF_HELPER(target_ulong, do_mftc0_tcrestart, (void))
DEF_HELPER(target_ulong, do_mfc0_tchalt, (void))
DEF_HELPER(target_ulong, do_mftc0_tchalt, (void))
DEF_HELPER(target_ulong, do_mfc0_tccontext, (void))
DEF_HELPER(target_ulong, do_mftc0_tccontext, (void))
DEF_HELPER(target_ulong, do_mfc0_tcschedule, (void))
DEF_HELPER(target_ulong, do_mftc0_tcschedule, (void))
DEF_HELPER(target_ulong, do_mfc0_tcschefback, (void))
DEF_HELPER(target_ulong, do_mftc0_tcschefback, (void))
DEF_HELPER(target_ulong, do_mfc0_count, (void))
DEF_HELPER(target_ulong, do_mftc0_entryhi, (void))
DEF_HELPER(target_ulong, do_mftc0_status, (void))
DEF_HELPER(target_ulong, do_mfc0_lladdr, (void))
DEF_HELPER(target_ulong, do_mfc0_watchlo, (uint32_t sel))
DEF_HELPER(target_ulong, do_mfc0_watchhi, (uint32_t sel))
DEF_HELPER(target_ulong, do_mfc0_debug, (void))
DEF_HELPER(target_ulong, do_mftc0_debug, (void))
#ifdef TARGET_MIPS64
DEF_HELPER(target_ulong, do_dmfc0_tcrestart, (void))
DEF_HELPER(target_ulong, do_dmfc0_tchalt, (void))
DEF_HELPER(target_ulong, do_dmfc0_tccontext, (void))
DEF_HELPER(target_ulong, do_dmfc0_tcschedule, (void))
DEF_HELPER(target_ulong, do_dmfc0_tcschefback, (void))
DEF_HELPER(target_ulong, do_dmfc0_lladdr, (void))
DEF_HELPER(target_ulong, do_dmfc0_watchlo, (uint32_t sel))
#endif /* TARGET_MIPS64 */

DEF_HELPER(void, do_mtc0_index, (target_ulong t0))
DEF_HELPER(void, do_mtc0_mvpcontrol, (target_ulong t0))
DEF_HELPER(void, do_mtc0_vpecontrol, (target_ulong t0))
DEF_HELPER(void, do_mtc0_vpeconf0, (target_ulong t0))
DEF_HELPER(void, do_mtc0_vpeconf1, (target_ulong t0))
DEF_HELPER(void, do_mtc0_yqmask, (target_ulong t0))
DEF_HELPER(void, do_mtc0_vpeopt, (target_ulong t0))
DEF_HELPER(void, do_mtc0_entrylo0, (target_ulong t0))
DEF_HELPER(void, do_mtc0_tcstatus, (target_ulong t0))
DEF_HELPER(void, do_mttc0_tcstatus, (target_ulong t0))
DEF_HELPER(void, do_mtc0_tcbind, (target_ulong t0))
DEF_HELPER(void, do_mttc0_tcbind, (target_ulong t0))
DEF_HELPER(void, do_mtc0_tcrestart, (target_ulong t0))
DEF_HELPER(void, do_mttc0_tcrestart, (target_ulong t0))
DEF_HELPER(void, do_mtc0_tchalt, (target_ulong t0))
DEF_HELPER(void, do_mttc0_tchalt, (target_ulong t0))
DEF_HELPER(void, do_mtc0_tccontext, (target_ulong t0))
DEF_HELPER(void, do_mttc0_tccontext, (target_ulong t0))
DEF_HELPER(void, do_mtc0_tcschedule, (target_ulong t0))
DEF_HELPER(void, do_mttc0_tcschedule, (target_ulong t0))
DEF_HELPER(void, do_mtc0_tcschefback, (target_ulong t0))
DEF_HELPER(void, do_mttc0_tcschefback, (target_ulong t0))
DEF_HELPER(void, do_mtc0_entrylo1, (target_ulong t0))
DEF_HELPER(void, do_mtc0_context, (target_ulong t0))
DEF_HELPER(void, do_mtc0_pagemask, (target_ulong t0))
DEF_HELPER(void, do_mtc0_pagegrain, (target_ulong t0))
DEF_HELPER(void, do_mtc0_wired, (target_ulong t0))
DEF_HELPER(void, do_mtc0_srsconf0, (target_ulong t0))
DEF_HELPER(void, do_mtc0_srsconf1, (target_ulong t0))
DEF_HELPER(void, do_mtc0_srsconf2, (target_ulong t0))
DEF_HELPER(void, do_mtc0_srsconf3, (target_ulong t0))
DEF_HELPER(void, do_mtc0_srsconf4, (target_ulong t0))
DEF_HELPER(void, do_mtc0_hwrena, (target_ulong t0))
DEF_HELPER(void, do_mtc0_count, (target_ulong t0))
DEF_HELPER(void, do_mtc0_entryhi, (target_ulong t0))
DEF_HELPER(void, do_mttc0_entryhi, (target_ulong t0))
DEF_HELPER(void, do_mtc0_compare, (target_ulong t0))
DEF_HELPER(void, do_mtc0_status, (target_ulong t0))
DEF_HELPER(void, do_mttc0_status, (target_ulong t0))
DEF_HELPER(void, do_mtc0_intctl, (target_ulong t0))
DEF_HELPER(void, do_mtc0_srsctl, (target_ulong t0))
DEF_HELPER(void, do_mtc0_cause, (target_ulong t0))
DEF_HELPER(void, do_mtc0_ebase, (target_ulong t0))
DEF_HELPER(void, do_mtc0_config0, (target_ulong t0))
DEF_HELPER(void, do_mtc0_config2, (target_ulong t0))
DEF_HELPER(void, do_mtc0_watchlo, (target_ulong t0, uint32_t sel))
DEF_HELPER(void, do_mtc0_watchhi, (target_ulong t0, uint32_t sel))
DEF_HELPER(void, do_mtc0_xcontext, (target_ulong t0))
DEF_HELPER(void, do_mtc0_framemask, (target_ulong t0))
DEF_HELPER(void, do_mtc0_debug, (target_ulong t0))
DEF_HELPER(void, do_mttc0_debug, (target_ulong t0))
DEF_HELPER(void, do_mtc0_performance0, (target_ulong t0))
DEF_HELPER(void, do_mtc0_taglo, (target_ulong t0))
DEF_HELPER(void, do_mtc0_datalo, (target_ulong t0))
DEF_HELPER(void, do_mtc0_taghi, (target_ulong t0))
DEF_HELPER(void, do_mtc0_datahi, (target_ulong t0))

/* MIPS MT functions */
DEF_HELPER(target_ulong, do_mftgpr, (uint32_t sel))
DEF_HELPER(target_ulong, do_mftlo, (uint32_t sel))
DEF_HELPER(target_ulong, do_mfthi, (uint32_t sel))
DEF_HELPER(target_ulong, do_mftacx, (uint32_t sel))
DEF_HELPER(target_ulong, do_mftdsp, (void))
DEF_HELPER(void, do_mttgpr, (target_ulong t0, uint32_t sel))
DEF_HELPER(void, do_mttlo, (target_ulong t0, uint32_t sel))
DEF_HELPER(void, do_mtthi, (target_ulong t0, uint32_t sel))
DEF_HELPER(void, do_mttacx, (target_ulong t0, uint32_t sel))
DEF_HELPER(void, do_mttdsp, (target_ulong t0))
DEF_HELPER(target_ulong, do_dmt, (target_ulong t0))
DEF_HELPER(target_ulong, do_emt, (target_ulong t0))
DEF_HELPER(target_ulong, do_dvpe, (target_ulong t0))
DEF_HELPER(target_ulong, do_evpe, (target_ulong t0))
#endif /* !CONFIG_USER_ONLY */
DEF_HELPER(void, do_fork, (target_ulong t0, target_ulong t1))
DEF_HELPER(target_ulong, do_yield, (target_ulong t0))

/* CP1 functions */
DEF_HELPER(target_ulong, do_cfc1, (uint32_t reg))
DEF_HELPER(void, do_ctc1, (target_ulong t0, uint32_t reg))

DEF_HELPER(uint64_t, do_float_cvtd_s, (uint32_t fst0))
DEF_HELPER(uint64_t, do_float_cvtd_w, (uint32_t wt0))
DEF_HELPER(uint64_t, do_float_cvtd_l, (uint64_t dt0))
DEF_HELPER(uint64_t, do_float_cvtl_d, (uint64_t fd0))
DEF_HELPER(uint64_t, do_float_cvtl_s, (uint32_t fst0))
DEF_HELPER(uint64_t, do_float_cvtps_pw, (uint64_t dt0))
DEF_HELPER(uint64_t, do_float_cvtpw_ps, (uint64_t fdt0))
DEF_HELPER(uint32_t, do_float_cvts_d, (uint64_t fd0))
DEF_HELPER(uint32_t, do_float_cvts_w, (uint32_t wt0))
DEF_HELPER(uint32_t, do_float_cvts_l, (uint64_t dt0))
DEF_HELPER(uint32_t, do_float_cvts_pl, (uint32_t wt0))
DEF_HELPER(uint32_t, do_float_cvts_pu, (uint32_t wth0))
DEF_HELPER(uint32_t, do_float_cvtw_s, (uint32_t fst0))
DEF_HELPER(uint32_t, do_float_cvtw_d, (uint64_t fd0))

DEF_HELPER(uint64_t, do_float_addr_ps, (uint64_t fdt0, uint64_t fdt1))
DEF_HELPER(uint64_t, do_float_mulr_ps, (uint64_t fdt0, uint64_t fdt1))

#define FOP_PROTO(op)                                          \
DEF_HELPER(uint64_t, do_float_ ## op ## l_s, (uint32_t fst0))  \
DEF_HELPER(uint64_t, do_float_ ## op ## l_d, (uint64_t fdt0))  \
DEF_HELPER(uint32_t, do_float_ ## op ## w_s, (uint32_t fst0))  \
DEF_HELPER(uint32_t, do_float_ ## op ## w_d, (uint64_t fdt0))
FOP_PROTO(round)
FOP_PROTO(trunc)
FOP_PROTO(ceil)
FOP_PROTO(floor)
#undef FOP_PROTO

#define FOP_PROTO(op)                                          \
DEF_HELPER(uint32_t, do_float_ ## op ## _s, (uint32_t fst0))   \
DEF_HELPER(uint64_t, do_float_ ## op ## _d, (uint64_t fdt0))
FOP_PROTO(sqrt)
FOP_PROTO(rsqrt)
FOP_PROTO(recip)
#undef FOP_PROTO

#define FOP_PROTO(op)                                          \
DEF_HELPER(uint32_t, do_float_ ## op ## _s, (uint32_t fst0))   \
DEF_HELPER(uint64_t, do_float_ ## op ## _d, (uint64_t fdt0))   \
DEF_HELPER(uint64_t, do_float_ ## op ## _ps, (uint64_t fdt0))
FOP_PROTO(abs)
FOP_PROTO(chs)
FOP_PROTO(recip1)
FOP_PROTO(rsqrt1)
#undef FOP_PROTO

#define FOP_PROTO(op)                                                       \
DEF_HELPER(uint32_t, do_float_ ## op ## _s, (uint32_t fst0, uint32_t fst2)) \
DEF_HELPER(uint64_t, do_float_ ## op ## _d, (uint64_t fdt0, uint64_t fdt2)) \
DEF_HELPER(uint64_t, do_float_ ## op ## _ps, (uint64_t fdt0, uint64_t fdt2))
FOP_PROTO(add)
FOP_PROTO(sub)
FOP_PROTO(mul)
FOP_PROTO(div)
FOP_PROTO(recip2)
FOP_PROTO(rsqrt2)
#undef FOP_PROTO

#define FOP_PROTO(op)                                                       \
DEF_HELPER(uint32_t, do_float_ ## op ## _s, (uint32_t fst0, uint32_t fst1,  \
                                             uint32_t fst2))                \
DEF_HELPER(uint64_t, do_float_ ## op ## _d, (uint64_t fdt0, uint64_t fdt1,  \
                                             uint64_t fdt2))                \
DEF_HELPER(uint64_t, do_float_ ## op ## _ps, (uint64_t fdt0, uint64_t fdt1, \
                                              uint64_t fdt2))
FOP_PROTO(muladd)
FOP_PROTO(mulsub)
FOP_PROTO(nmuladd)
FOP_PROTO(nmulsub)
#undef FOP_PROTO

#define FOP_PROTO(op)                                                        \
DEF_HELPER(void, do_cmp_d_ ## op, (uint64_t fdt0, uint64_t fdt1, int cc))    \
DEF_HELPER(void, do_cmpabs_d_ ## op, (uint64_t fdt0, uint64_t fdt1, int cc)) \
DEF_HELPER(void, do_cmp_s_ ## op, (uint32_t fst0, uint32_t fst1, int cc))    \
DEF_HELPER(void, do_cmpabs_s_ ## op, (uint32_t fst0, uint32_t fst1, int cc)) \
DEF_HELPER(void, do_cmp_ps_ ## op, (uint64_t fdt0, uint64_t fdt1, int cc))   \
DEF_HELPER(void, do_cmpabs_ps_ ## op, (uint64_t fdt0, uint64_t fdt1, int cc))
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
DEF_HELPER(target_ulong, do_di, (void))
DEF_HELPER(target_ulong, do_ei, (void))
DEF_HELPER(void, do_eret, (void))
DEF_HELPER(void, do_deret, (void))
#endif /* !CONFIG_USER_ONLY */
DEF_HELPER(target_ulong, do_rdhwr_cpunum, (void))
DEF_HELPER(target_ulong, do_rdhwr_synci_step, (void))
DEF_HELPER(target_ulong, do_rdhwr_cc, (void))
DEF_HELPER(target_ulong, do_rdhwr_ccres, (void))
DEF_HELPER(void, do_pmon, (int function))
DEF_HELPER(void, do_wait, (void))

/* Bit shuffle operations. */
DEF_HELPER(target_ulong, do_wsbh, (target_ulong t1))
#ifdef TARGET_MIPS64
DEF_HELPER(target_ulong, do_dsbh, (target_ulong t1))
DEF_HELPER(target_ulong, do_dshd, (target_ulong t1))
#endif
