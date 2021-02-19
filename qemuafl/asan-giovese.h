/*******************************************************************************
BSD 2-Clause License

Copyright (c) 2020-2021, Andrea Fioraldi
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#ifndef __ASAN_GIOVESE_H__
#define __ASAN_GIOVESE_H__

#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include "common.h"

#ifndef ASAN_NAME_STR
#define ASAN_NAME_STR "AddressSanitizer"
#endif

#define HIGH_SHADOW_ADDR ((void*)0x02008fff7000ULL)
#define LOW_SHADOW_ADDR ((void*)0x00007fff8000ULL)
#define GAP_SHADOW_ADDR ((void*)0x00008fff7000)

#define HIGH_SHADOW_SIZE (0xdfff0000fffULL)
#define LOW_SHADOW_SIZE (0xfffefffULL)
#define GAP_SHADOW_SIZE (0x1ffffffffff)

#define SHADOW_OFFSET (0x7fff8000ULL)

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

enum {

  ACCESS_TYPE_LOAD,
  ACCESS_TYPE_STORE,

};

struct call_context {

  target_ulong* addresses;
  uint32_t      tid;
  uint32_t      size;

};

struct chunk_info {

  target_ulong         start;
  target_ulong         end;
  struct call_context* alloc_ctx;
  struct call_context* free_ctx;  // NULL if chunk is allocated

};

extern void* __ag_high_shadow;
extern void* __ag_low_shadow;

// ------------------------------------------------------------------------- //
// Virtual functions, you have to implement them
// ------------------------------------------------------------------------- //

///////////////////////////////////////////////////////////////////////////////
void  asan_giovese_populate_context(struct call_context* ctx, target_ulong pc);
char* asan_giovese_printaddr(target_ulong addr);
///////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------- //
// Exposed functions
// ------------------------------------------------------------------------- //

void asan_giovese_init(void);

// this has to be fast, ptr is an host pointer

int asan_giovese_load1(void* ptr);
int asan_giovese_load2(void* ptr);
int asan_giovese_load4(void* ptr);
int asan_giovese_load8(void* ptr);
int asan_giovese_store1(void* ptr);
int asan_giovese_store2(void* ptr);
int asan_giovese_store4(void* ptr);
int asan_giovese_store8(void* ptr);
int asan_giovese_loadN(void* ptr, size_t n);
int asan_giovese_storeN(void* ptr, size_t n);
int asan_giovese_guest_loadN(target_ulong addr, size_t n);
int asan_giovese_guest_storeN(target_ulong addr, size_t n);

int asan_giovese_poison_region(void* ptr, size_t n,
                               uint8_t poison_byte);
int asan_giovese_user_poison_region(void* ptr, size_t n);
int asan_giovese_unpoison_region(void* ptr, size_t n);

int asan_giovese_poison_guest_region(target_ulong addr, size_t n, uint8_t poison_byte);
int asan_giovese_user_poison_guest_region(target_ulong addr, size_t n);
int asan_giovese_unpoison_guest_region(target_ulong addr, size_t n);

// addr is a guest pointer

int asan_giovese_report_and_crash(int access_type, target_ulong addr, size_t n,
                                  target_ulong pc, target_ulong bp,
                                  target_ulong sp);

int asan_giovese_deadly_signal(int signum, target_ulong addr, target_ulong pc,
                               target_ulong bp, target_ulong sp);

int asan_giovese_badfree(target_ulong addr, target_ulong pc);

struct chunk_info* asan_giovese_alloc_search(target_ulong query);
void asan_giovese_alloc_insert(target_ulong start, target_ulong end,
                               struct call_context* alloc_ctx);

#endif

