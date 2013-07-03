#ifndef TCG_RUNTIME_H
#define TCG_RUNTIME_H

/* tcg-runtime.c */
int32_t tcg_helper_div_i32(int32_t arg1, int32_t arg2);
int32_t tcg_helper_rem_i32(int32_t arg1, int32_t arg2);
uint32_t tcg_helper_divu_i32(uint32_t arg1, uint32_t arg2);
uint32_t tcg_helper_remu_i32(uint32_t arg1, uint32_t arg2);

int64_t tcg_helper_shl_i64(int64_t arg1, int64_t arg2);
int64_t tcg_helper_shr_i64(int64_t arg1, int64_t arg2);
int64_t tcg_helper_sar_i64(int64_t arg1, int64_t arg2);
int64_t tcg_helper_div_i64(int64_t arg1, int64_t arg2);
int64_t tcg_helper_rem_i64(int64_t arg1, int64_t arg2);
int64_t tcg_helper_mulsh_i64(int64_t arg1, int64_t arg2);
uint64_t tcg_helper_divu_i64(uint64_t arg1, uint64_t arg2);
uint64_t tcg_helper_remu_i64(uint64_t arg1, uint64_t arg2);
uint64_t tcg_helper_muluh_i64(uint64_t arg1, uint64_t arg2);

#endif
