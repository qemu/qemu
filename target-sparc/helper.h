#define TCG_HELPER_PROTO

#ifndef TARGET_SPARC64
void TCG_HELPER_PROTO helper_rett(void);
void TCG_HELPER_PROTO helper_wrpsr(target_ulong new_psr);
target_ulong TCG_HELPER_PROTO helper_rdpsr(void);
#else
void TCG_HELPER_PROTO helper_wrpstate(target_ulong new_state);
void TCG_HELPER_PROTO helper_done(void);
void TCG_HELPER_PROTO helper_retry(void);
target_ulong TCG_HELPER_PROTO helper_rdccr(void);
void TCG_HELPER_PROTO helper_wrccr(target_ulong new_ccr);
target_ulong TCG_HELPER_PROTO helper_rdcwp(void);
void TCG_HELPER_PROTO helper_wrcwp(target_ulong new_cwp);
target_ulong TCG_HELPER_PROTO helper_array8(target_ulong pixel_addr,
                                            target_ulong cubesize);
target_ulong TCG_HELPER_PROTO helper_alignaddr(target_ulong addr,
                                               target_ulong offset);
target_ulong TCG_HELPER_PROTO helper_popc(target_ulong val);
void TCG_HELPER_PROTO helper_ldf_asi(target_ulong addr, int asi, int size,
                                     int rd);
void TCG_HELPER_PROTO helper_stf_asi(target_ulong addr, int asi, int size,
                                     int rd);
target_ulong TCG_HELPER_PROTO
helper_cas_asi(target_ulong addr, target_ulong val1,
               target_ulong val2, uint32_t asi);
target_ulong  TCG_HELPER_PROTO
helper_casx_asi(target_ulong addr, target_ulong val1,
                target_ulong val2, uint32_t asi);
void TCG_HELPER_PROTO helper_tick_set_count(void *opaque, uint64_t count);
uint64_t TCG_HELPER_PROTO helper_tick_get_count(void *opaque);
void TCG_HELPER_PROTO helper_tick_set_limit(void *opaque, uint64_t limit);
#endif
void TCG_HELPER_PROTO helper_trap(target_ulong nb_trap);
void TCG_HELPER_PROTO helper_trapcc(target_ulong nb_trap,
                                    target_ulong do_trap);
void TCG_HELPER_PROTO helper_debug(void);
void TCG_HELPER_PROTO helper_flush(target_ulong addr);
target_ulong TCG_HELPER_PROTO helper_udiv(target_ulong a, target_ulong b);
target_ulong TCG_HELPER_PROTO helper_sdiv(target_ulong a, target_ulong b);
uint64_t TCG_HELPER_PROTO helper_pack64(target_ulong high, target_ulong low);
uint64_t TCG_HELPER_PROTO helper_ld_asi(target_ulong addr, int asi,
                                        int size, int sign);
void TCG_HELPER_PROTO helper_st_asi(target_ulong addr, uint64_t val, int asi,
                                    int size);
void TCG_HELPER_PROTO helper_ldfsr(void);
void TCG_HELPER_PROTO helper_stfsr(void);
void TCG_HELPER_PROTO helper_check_ieee_exceptions(void);
void TCG_HELPER_PROTO helper_clear_float_exceptions(void);
void TCG_HELPER_PROTO helper_fabss(void);
void TCG_HELPER_PROTO helper_fsqrts(void);
void TCG_HELPER_PROTO helper_fsqrtd(void);
void TCG_HELPER_PROTO helper_fcmps(void);
void TCG_HELPER_PROTO helper_fcmpd(void);
void TCG_HELPER_PROTO helper_fcmpes(void);
void TCG_HELPER_PROTO helper_fcmped(void);
#if defined(CONFIG_USER_ONLY)
void TCG_HELPER_PROTO helper_fsqrtq(void);
void TCG_HELPER_PROTO helper_fcmpq(void);
void TCG_HELPER_PROTO helper_fcmpeq(void);
#endif
#ifdef TARGET_SPARC64
void TCG_HELPER_PROTO helper_fabsd(void);
void TCG_HELPER_PROTO helper_fcmps_fcc1(void);
void TCG_HELPER_PROTO helper_fcmpd_fcc1(void);
void TCG_HELPER_PROTO helper_fcmps_fcc2(void);
void TCG_HELPER_PROTO helper_fcmpd_fcc2(void);
void TCG_HELPER_PROTO helper_fcmps_fcc3(void);
void TCG_HELPER_PROTO helper_fcmpd_fcc3(void);
void TCG_HELPER_PROTO helper_fcmpes_fcc1(void);
void TCG_HELPER_PROTO helper_fcmped_fcc1(void);
void TCG_HELPER_PROTO helper_fcmpes_fcc2(void);
void TCG_HELPER_PROTO helper_fcmped_fcc2(void);
void TCG_HELPER_PROTO helper_fcmpes_fcc3(void);
void TCG_HELPER_PROTO helper_fcmped_fcc3(void);
#if defined(CONFIG_USER_ONLY)
void TCG_HELPER_PROTO helper_fabsq(void);
void TCG_HELPER_PROTO helper_fcmpq_fcc1(void);
void TCG_HELPER_PROTO helper_fcmpq_fcc2(void);
void TCG_HELPER_PROTO helper_fcmpq_fcc3(void);
void TCG_HELPER_PROTO helper_fcmpeq_fcc1(void);
void TCG_HELPER_PROTO helper_fcmpeq_fcc2(void);
void TCG_HELPER_PROTO helper_fcmpeq_fcc3(void);
#endif
#endif
void TCG_HELPER_PROTO raise_exception(int tt);
#define F_HELPER_0_0(name) void TCG_HELPER_PROTO helper_f ## name(void)
#if defined(CONFIG_USER_ONLY)
#define F_HELPER_SDQ_0_0(name)                  \
    F_HELPER_0_0(name ## s);                    \
    F_HELPER_0_0(name ## d);                    \
    F_HELPER_0_0(name ## q)
#else
#define F_HELPER_SDQ_0_0(name)                  \
    F_HELPER_0_0(name ## s);                    \
    F_HELPER_0_0(name ## d);
#endif

F_HELPER_SDQ_0_0(add);
F_HELPER_SDQ_0_0(sub);
F_HELPER_SDQ_0_0(mul);
F_HELPER_SDQ_0_0(div);

F_HELPER_0_0(smuld);
F_HELPER_0_0(dmulq);

F_HELPER_SDQ_0_0(neg);
F_HELPER_SDQ_0_0(ito);
#ifdef TARGET_SPARC64
F_HELPER_SDQ_0_0(xto);
#endif
F_HELPER_0_0(dtos);
F_HELPER_0_0(stod);
#if defined(CONFIG_USER_ONLY)
F_HELPER_0_0(qtos);
F_HELPER_0_0(stoq);
F_HELPER_0_0(qtod);
F_HELPER_0_0(dtoq);
#endif
F_HELPER_0_0(stoi);
F_HELPER_0_0(dtoi);
#if defined(CONFIG_USER_ONLY)
F_HELPER_0_0(qtoi);
#endif
#ifdef TARGET_SPARC64
F_HELPER_0_0(stox);
F_HELPER_0_0(dtox);
#if defined(CONFIG_USER_ONLY)
F_HELPER_0_0(qtox);
#endif
F_HELPER_0_0(aligndata);
void TCG_HELPER_PROTO helper_movl_FT0_0(void);
void TCG_HELPER_PROTO helper_movl_DT0_0(void);
void TCG_HELPER_PROTO helper_movl_FT0_1(void);
void TCG_HELPER_PROTO helper_movl_DT0_1(void);
F_HELPER_0_0(not);
F_HELPER_0_0(nots);
F_HELPER_0_0(nor);
F_HELPER_0_0(nors);
F_HELPER_0_0(or);
F_HELPER_0_0(ors);
F_HELPER_0_0(xor);
F_HELPER_0_0(xors);
F_HELPER_0_0(and);
F_HELPER_0_0(ands);
F_HELPER_0_0(ornot);
F_HELPER_0_0(ornots);
F_HELPER_0_0(andnot);
F_HELPER_0_0(andnots);
F_HELPER_0_0(nand);
F_HELPER_0_0(nands);
F_HELPER_0_0(xnor);
F_HELPER_0_0(xnors);
F_HELPER_0_0(pmerge);
F_HELPER_0_0(mul8x16);
F_HELPER_0_0(mul8x16al);
F_HELPER_0_0(mul8x16au);
F_HELPER_0_0(mul8sux16);
F_HELPER_0_0(mul8ulx16);
F_HELPER_0_0(muld8sux16);
F_HELPER_0_0(muld8ulx16);
F_HELPER_0_0(expand);
#define VIS_HELPER(name)                                 \
    F_HELPER_0_0(name##16);                              \
    F_HELPER_0_0(name##16s);                             \
    F_HELPER_0_0(name##32);                              \
    F_HELPER_0_0(name##32s)

VIS_HELPER(padd);
VIS_HELPER(psub);
#define VIS_CMPHELPER(name)                              \
    F_HELPER_0_0(name##16);                              \
    F_HELPER_0_0(name##32)
VIS_CMPHELPER(cmpgt);
VIS_CMPHELPER(cmpeq);
VIS_CMPHELPER(cmple);
VIS_CMPHELPER(cmpne);
#endif
#undef F_HELPER_0_0
#undef F_HELPER_SDQ_0_0
#undef VIS_HELPER
#undef VIS_CMPHELPER
