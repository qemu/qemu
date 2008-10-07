#define TCG_HELPER_PROTO

void TCG_HELPER_PROTO helper_raise_exception(uint32_t index);
void TCG_HELPER_PROTO helper_tlb_flush_pid(uint32_t pid);
void TCG_HELPER_PROTO helper_spc_write(uint32_t pid);
void TCG_HELPER_PROTO helper_dump(uint32_t a0, uint32_t a1, uint32_t a2);
void TCG_HELPER_PROTO helper_rfe(void);
void TCG_HELPER_PROTO helper_rfn(void);

void TCG_HELPER_PROTO helper_movl_sreg_reg (uint32_t sreg, uint32_t reg);
void TCG_HELPER_PROTO helper_movl_reg_sreg (uint32_t reg, uint32_t sreg);

void TCG_HELPER_PROTO helper_evaluate_flags_muls(void);
void TCG_HELPER_PROTO helper_evaluate_flags_mulu(void);
void TCG_HELPER_PROTO helper_evaluate_flags_mcp(void);
void TCG_HELPER_PROTO helper_evaluate_flags_alu_4(void);
void TCG_HELPER_PROTO helper_evaluate_flags_move_4 (void);
void TCG_HELPER_PROTO helper_evaluate_flags_move_2 (void);
void TCG_HELPER_PROTO helper_evaluate_flags (void);
void TCG_HELPER_PROTO helper_top_evaluate_flags(void);
