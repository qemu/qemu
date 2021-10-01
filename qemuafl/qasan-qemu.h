/*******************************************************************************
Copyright (c) 2019-2021, Andrea Fioraldi


Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

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

/* MIPS_PATCH */
#elif defined(TARGET_MIPS) || defined(TARGET_MIPS64)

#define PC_GET(env) ((env)->active_tc.PC)
#define BP_GET(env) ((env)->active_tc.gpr[29])
#define SP_GET(env) ((env)->active_tc.gpr[30])

#else
//#error "Target not supported by asan-giovese"
#define DO_NOT_USE_QASAN 1
#endif

#ifndef DO_NOT_USE_QASAN
#define ASAN_NAME_STR "QEMU-AddressSanitizer"
#include "asan-giovese.h"
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
