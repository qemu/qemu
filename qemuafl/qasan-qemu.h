#ifndef __QASAN_QEMU_H__
#define __QASAN_QEMU_H__

#define ASAN_GIOVESE

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "qasan.h"
#include "common.h"
#include "tcg/tcg.h"

// options
extern int qasan_max_call_stack; // QASAN_MAX_CALL_STACK
extern int qasan_symbolize; // QASAN_SYMBOLIZE

#define SHADOW_BK_SIZE (4096*8)

struct shadow_stack_block {

  int index;
  target_ulong buf[SHADOW_BK_SIZE];
  
  struct shadow_stack_block* next;

};

struct shadow_stack {

  int size;
  struct shadow_stack_block* first;

};

extern __thread struct shadow_stack qasan_shadow_stack;

#ifdef ASAN_GIOVESE

#define ASAN_NAME_STR "QEMU-AddressSanitizer"
#include "asan-giovese.h"

#if defined(TARGET_X86_64) || defined(TARGET_I386)

#define PC_GET(env) ((env)->eip)
#define BP_GET(env) ((env)->regs[R_EBP])
#define SP_GET(env) ((env)->regs[R_ESP])

#elif defined(TARGET_ARM) && !defined(TARGET_AARCH64)

#define PC_GET(env) ((env)->regs[15])
#define BP_GET(env) ((env)->regs[11])
#define SP_GET(env) ((env)->regs[13])

#elif defined(TARGET_AARCH64)

#define PC_GET(env) ((env)->pc)
#define BP_GET(env) ((env)->aarch64 ? (env)->xregs[29] : (env)->regs[11])
#define SP_GET(env) ((env)->aarch64 ? (env)->xregs[31] : (env)->regs[13])

#else
#error "Target not supported by asan-giovese"
#endif

#else

void __asan_poison_memory_region(void const volatile *addr, size_t size);
void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
void *__asan_region_is_poisoned(void *beg, size_t size);

void __asan_load1(void*);
void __asan_load2(void*);
void __asan_load4(void*);
void __asan_load8(void*);
void __asan_store1(void*);
void __asan_store2(void*);
void __asan_store4(void*);
void __asan_store8(void*);
void __asan_loadN(void*, size_t);
void __asan_storeN(void*, size_t);

#endif

target_long qasan_actions_dispatcher(void *cpu_env, target_long action,
                                     target_long arg1, target_long arg2,
                                     target_long arg3);

void qasan_gen_load1(TCGv addr, int off);
void qasan_gen_load2(TCGv addr, int off);
void qasan_gen_load4(TCGv addr, int off);
void qasan_gen_load8(TCGv addr, int off);
void qasan_gen_store1(TCGv addr, int off);
void qasan_gen_store2(TCGv addr, int off);
void qasan_gen_store4(TCGv addr, int off);
void qasan_gen_store8(TCGv addr, int off);

#endif
