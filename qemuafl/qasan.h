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

#ifndef __QASAN_H__
#define __QASAN_H__

#define QASAN_VERSTR "0.2"

#define QASAN_FAKEINSTR_X86 { 0x0f, 0x3a, 0xf2 }

#define QASAN_FAKESYS_NR 0xa2a4

enum {
  QASAN_ACTION_CHECK_LOAD,
  QASAN_ACTION_CHECK_STORE,
  QASAN_ACTION_POISON,
  QASAN_ACTION_USER_POISON,
  QASAN_ACTION_UNPOISON,
  QASAN_ACTION_IS_POISON,
  QASAN_ACTION_ALLOC,
  QASAN_ACTION_DEALLOC,
  QASAN_ACTION_ENABLE,
  QASAN_ACTION_DISABLE,
  QASAN_ACTION_SWAP_STATE,
};

/* shadow map byte values */
#define ASAN_VALID 0x00
#define ASAN_PARTIAL1 0x01
#define ASAN_PARTIAL2 0x02
#define ASAN_PARTIAL3 0x03
#define ASAN_PARTIAL4 0x04
#define ASAN_PARTIAL5 0x05
#define ASAN_PARTIAL6 0x06
#define ASAN_PARTIAL7 0x07
#define ASAN_ARRAY_COOKIE 0xac
#define ASAN_STACK_RZ 0xf0
#define ASAN_STACK_LEFT_RZ 0xf1
#define ASAN_STACK_MID_RZ 0xf2
#define ASAN_STACK_RIGHT_RZ 0xf3
#define ASAN_STACK_FREED 0xf5
#define ASAN_STACK_OOSCOPE 0xf8
#define ASAN_GLOBAL_RZ 0xf9
#define ASAN_HEAP_RZ 0xe9
#define ASAN_USER 0xf7
#define ASAN_HEAP_LEFT_RZ 0xfa
#define ASAN_HEAP_RIGHT_RZ 0xfb
#define ASAN_HEAP_FREED 0xfd

#define QASAN_ENABLED (0)
#define QASAN_DISABLED (1)

#if defined(__x86_64__) && __x86_64__

// The backdoor is more performant than the fake syscall
#define QASAN_CALL0(action) \
({ \
  uintptr_t __libqasan__ret__; \
  asm volatile ( \
    "movq %1, %%rax\n" \
    ".byte 0x0f\n" \
    ".byte 0x3a\n" \
    ".byte 0xf2\n" \
    "movq %%rax, %0\n" \
    : "=g"(__libqasan__ret__) \
    : "g"((uintptr_t)(action)) \
    : "%rax", "memory" \
  ); \
  __libqasan__ret__; \
})

#define QASAN_CALL1(action, arg1) \
({ \
  uintptr_t __libqasan__ret__; \
  asm volatile ( \
    "movq %1, %%rax\n" \
    "movq %2, %%rdi\n" \
    ".byte 0x0f\n" \
    ".byte 0x3a\n" \
    ".byte 0xf2\n" \
    "movq %%rax, %0\n" \
    : "=g"(__libqasan__ret__) \
    : "g"((uintptr_t)(action)), "g"((uintptr_t)(arg1)) \
    : "%rax", "%rdi", "memory" \
  ); \
  __libqasan__ret__; \
})

#define QASAN_CALL2(action, arg1, arg2) \
({ \
  uintptr_t __libqasan__ret__; \
  asm volatile ( \
    "movq %1, %%rax\n" \
    "movq %2, %%rdi\n" \
    "movq %3, %%rsi\n" \
    ".byte 0x0f\n" \
    ".byte 0x3a\n" \
    ".byte 0xf2\n" \
    "movq %%rax, %0\n" \
    : "=g"(__libqasan__ret__) \
    : "g"((uintptr_t)(action)), "g"((uintptr_t)(arg1)), "g"((uintptr_t)(arg2)) \
    : "%rax", "%rdi", "%rsi", "memory" \
  ); \
  __libqasan__ret__; \
})

#define QASAN_CALL3(action, arg1, arg2, arg3) \
({ \
  uintptr_t __libqasan__ret__; \
  asm volatile ( \
    "movq %1, %%rax\n" \
    "movq %2, %%rdi\n" \
    "movq %3, %%rsi\n" \
    "movq %4, %%rdx\n" \
    ".byte 0x0f\n" \
    ".byte 0x3a\n" \
    ".byte 0xf2\n" \
    "movq %%rax, %0\n" \
    : "=g"(__libqasan__ret__) \
    : "g"((uintptr_t)(action)), "g"((uintptr_t)(arg1)), "g"((uintptr_t)(arg2)), "g"((uintptr_t)(arg3)) \
    : "%rax", "%rdi", "%rsi", "%rdx", "memory" \
  ); \
  __libqasan__ret__; \
})

/*

#elif defined(__i386__) && __i386__

// The backdoor is more performant than the fake syscall
#define QASAN_CALL0(action) \
({ \
  uintptr_t __libqasan__ret__; \
  asm volatile ( \
    "movl %1, %%eax\n" \
    ".byte 0x0f\n" \
    ".byte 0x3a\n" \
    ".byte 0xf2\n" \
    "movl %%eax, %0\n" \
    : "=g"(__libqasan__ret__) \
    : "g"((uintptr_t)(action)) \
    : "%eax", "memory" \
  ); \
  __libqasan__ret__; \
})

#define QASAN_CALL1(action, arg1) \
({ \
  uintptr_t __libqasan__ret__; \
  asm volatile ( \
    "movl %1, %%eax\n" \
    "movl %2, %%edi\n" \
    ".byte 0x0f\n" \
    ".byte 0x3a\n" \
    ".byte 0xf2\n" \
    "movl %%eax, %0\n" \
    : "=g"(__libqasan__ret__) \
    : "g"((uintptr_t)(action)), "g"((uintptr_t)(arg1)) \
    : "%eax", "%edi", "memory" \
  ); \
  __libqasan__ret__; \
})

#define QASAN_CALL2(action, arg1, arg2) \
({ \
  uintptr_t __libqasan__ret__; \
  asm volatile ( \
    "movl %1, %%eax\n" \
    "movl %2, %%edi\n" \
    "movl %3, %%esi\n" \
    ".byte 0x0f\n" \
    ".byte 0x3a\n" \
    ".byte 0xf2\n" \
    "movl %%eax, %0\n" \
    : "=g"(__libqasan__ret__) \
    : "g"((uintptr_t)(action)), "g"((uintptr_t)(arg1)), "g"((uintptr_t)(arg2)) \
    : "%eax", "%edi", "%esi", "memory" \
  ); \
  __libqasan__ret__; \
})

#define QASAN_CALL3(action, arg1, arg2, arg3) \
({ \
  uintptr_t __libqasan__ret__; \
  asm volatile ( \
    "movl %1, %%eax\n" \
    "movl %2, %%edi\n" \
    "movl %3, %%esi\n" \
    "movl %4, %%edx\n" \
    ".byte 0x0f\n" \
    ".byte 0x3a\n" \
    ".byte 0xf2\n" \
    "movl %%eax, %0\n" \
    : "=g"(__libqasan__ret__) \
    : "g"((uintptr_t)(action)), "g"((uintptr_t)(arg1)), "g"((uintptr_t)(arg2)), "g"((uintptr_t)(arg3)) \
    : "%eax", "%edi", "%esi", "%edx", "memory" \
  ); \
  __libqasan__ret__; \
})

*/

#else

// fake syscall, works only for QASan user-mode!!!

#include <unistd.h>

#define QASAN_CALL0(action) \
  syscall(QASAN_FAKESYS_NR, action, NULL, NULL, NULL)
#define QASAN_CALL1(action, arg1) \
  syscall(QASAN_FAKESYS_NR, action, arg1, NULL, NULL)
#define QASAN_CALL2(action, arg1, arg2) \
  syscall(QASAN_FAKESYS_NR, action, arg1, arg2, NULL)
#define QASAN_CALL3(action, arg1, arg2, arg3) \
  syscall(QASAN_FAKESYS_NR, action, arg1, arg2, arg3)

#endif

#define QASAN_LOAD(ptr, len) \
  QASAN_CALL2(QASAN_ACTION_CHECK_LOAD, ptr, len)
#define QASAN_STORE(ptr, len) \
  QASAN_CALL2(QASAN_ACTION_CHECK_STORE, ptr, len)

#define QASAN_POISON(ptr, len, poison_byte) \
  QASAN_CALL3(QASAN_ACTION_POISON, ptr, len, poison_byte)
#define QASAN_USER_POISON(ptr, len) \
  QASAN_CALL3(QASAN_ACTION_POISON, ptr, len, ASAN_USER)
#define QASAN_UNPOISON(ptr, len) \
  QASAN_CALL2(QASAN_ACTION_UNPOISON, ptr, len)
#define QASAN_IS_POISON(ptr, len) \
  QASAN_CALL2(QASAN_ACTION_IS_POISON, ptr, len)

#define QASAN_ALLOC(start, end) \
  QASAN_CALL2(QASAN_ACTION_ALLOC, start, end)
#define QASAN_DEALLOC(ptr) \
  QASAN_CALL1(QASAN_ACTION_DEALLOC, ptr)

#define QASAN_SWAP(state) \
  QASAN_CALL1(QASAN_ACTION_SWAP_STATE, state)

#endif
