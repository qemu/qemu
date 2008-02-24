#define TCG_HELPER_PROTO

#ifndef TARGET_SPARC64
void TCG_HELPER_PROTO helper_rett(void);
void TCG_HELPER_PROTO helper_wrpsr(target_ulong new_psr);
target_ulong TCG_HELPER_PROTO helper_rdpsr(void);
#else
void TCG_HELPER_PROTO helper_wrpstate(target_ulong new_state);
void TCG_HELPER_PROTO helper_done(void);
void TCG_HELPER_PROTO helper_retry(void);
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
#endif
void TCG_HELPER_PROTO helper_trap(target_ulong nb_trap);
void TCG_HELPER_PROTO helper_trapcc(target_ulong nb_trap,
                                    target_ulong do_trap);
void TCG_HELPER_PROTO helper_debug(void);
void TCG_HELPER_PROTO helper_flush(target_ulong addr);
uint64_t TCG_HELPER_PROTO helper_pack64(target_ulong high, target_ulong low);
uint64_t TCG_HELPER_PROTO helper_ld_asi(target_ulong addr, int asi,
                                        int size, int sign);
void TCG_HELPER_PROTO helper_st_asi(target_ulong addr, uint64_t val, int asi,
                                    int size);
