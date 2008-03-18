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
