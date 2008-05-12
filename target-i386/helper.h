#define TCG_HELPER_PROTO

void TCG_HELPER_PROTO helper_divl_EAX_T0(target_ulong t0);
void TCG_HELPER_PROTO helper_idivl_EAX_T0(target_ulong t0);
void TCG_HELPER_PROTO helper_enter_mmx(void);
void TCG_HELPER_PROTO helper_emms(void);
void TCG_HELPER_PROTO helper_movq(uint64_t *d, uint64_t *s);

#define SHIFT 0
#include "ops_sse_header.h"
#define SHIFT 1
#include "ops_sse_header.h"

