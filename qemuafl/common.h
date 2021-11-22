/*
   american fuzzy lop++ - high-performance binary-only instrumentation
   -------------------------------------------------------------------

   Originally written by Andrew Griffiths <agriffiths@google.com> and
                         Michal Zalewski

   TCG instrumentation and block chaining support by Andrea Biondo
                                      <andrea.biondo965@gmail.com>

   QEMU 3.1.1 port, TCG thread-safety, CompareCoverage and NeverZero
   counters by Andrea Fioraldi <andreafioraldi@gmail.com>

   Copyright 2015, 2016, 2017 Google Inc. All rights reserved.
   Copyright 2019-2020 AFLplusplus Project. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     http://www.apache.org/licenses/LICENSE-2.0

   This code is a shim patched into the separately-distributed source
   code of QEMU 3.1.0. It leverages the built-in QEMU tracing functionality
   to implement AFL-style instrumentation and to take care of the remaining
   parts of the AFL fork server logic.

   The resulting QEMU binary is essentially a standalone instrumentation
   tool; for an example of how to leverage it for other purposes, you can
   have a look at afl-showmap.c.

 */

#ifndef __AFL_QEMU_COMMON
#define __AFL_QEMU_COMMON

#include "imported/config.h"
#include "imported/types.h"
#include "imported/cmplog.h"
#include "api.h"

/* We use one additional file descriptor to relay "needs translation"
   messages between the child and the fork server. */

#define TSL_FD (FORKSRV_FD - 1)

#define AFL_G2H g2h_untagged

#if defined(TARGET_X86_64)
#define api_regs x86_64_regs
#elif defined(TARGET_I386)
#define api_regs x86_regs
#elif defined(TARGET_AARCH64)
#define api_regs arm64_regs
#elif defined(TARGET_ARM)
#define api_regs arm_regs
/* MIPS_PATCH */
#elif defined(TARGET_MIPS) || defined(TARGET_MIPS64)
#define api_regs mips_regs
#else
struct generic_api_regs { int v; };
#define api_regs generic_api_regs
#endif

/* NeverZero */

#if (defined(__x86_64__) || defined(__i386__)) && defined(AFL_QEMU_NOT_ZERO)
  #define INC_AFL_AREA(loc)           \
    asm volatile(                     \
        "addb $1, (%0, %1, 1)\n"      \
        "adcb $0, (%0, %1, 1)\n"      \
        : /* no out */                \
        : "r"(afl_area_ptr), "r"(loc) \
        : "memory", "eax")
#else
  #define INC_AFL_AREA(loc) afl_area_ptr[loc]++
#endif

typedef void (*afl_persistent_hook_fn)(struct api_regs *regs,
                                       uint64_t guest_base,
                                       uint8_t *input_buf,
                                       uint32_t input_buf_len);

/* Declared in afl-qemu-cpu-inl.h */

// This structure is used for tracking which
// code is to be instrumented via afl_instr_code.
struct vmrange {
  target_ulong start, end;
  char* name;
  bool exclude; // Exclude this region rather than include it
  struct vmrange* next;
};

extern struct vmrange* afl_instr_code;
extern unsigned char  *afl_area_ptr;
extern unsigned int    afl_inst_rms;
extern abi_ulong       afl_entry_point, afl_start_code, afl_end_code;
extern abi_ulong       afl_persistent_addr;
extern abi_ulong       afl_persistent_ret_addr;
extern u8              afl_compcov_level;
extern unsigned char   afl_fork_child;
extern unsigned int    afl_forksrv_pid;
extern unsigned char   is_persistent;
extern target_long     persistent_stack_offset;
extern unsigned char   persistent_first_pass;
extern unsigned char   persistent_exits;
extern unsigned char   persistent_save_gpr;
extern unsigned char   persistent_memory;
extern int             persisent_retaddr_offset;
extern int             use_qasan;
extern __thread int    cur_block_is_good;
extern struct api_regs saved_regs;

extern u8 * shared_buf;
extern u32 *shared_buf_len;
extern u8   sharedmem_fuzzing;

extern afl_persistent_hook_fn afl_persistent_hook_ptr;

extern __thread abi_ulong afl_prev_loc;

extern struct cmp_map *__afl_cmp_map;

void afl_setup(void);
void afl_forkserver(CPUState *cpu);
void afl_persistent_iter(CPUArchState *env);
void afl_persistent_loop(CPUArchState *env);

// void afl_debug_dump_saved_regs(void);

void afl_gen_tcg_plain_call(void *func);

void afl_float_compcov_log_32(target_ulong cur_loc, float32 arg1, float32 arg2,
                              void *status);
void afl_float_compcov_log_64(target_ulong cur_loc, float64 arg1, float64 arg2,
                              void *status);
void afl_float_compcov_log_80(target_ulong cur_loc, floatx80 arg1,
                              floatx80 arg2);

abi_ulong afl_get_brk(void);
abi_ulong afl_set_brk(abi_ulong new_brk);

#if defined(TARGET_X86_64) || defined(TARGET_I386) || defined(TARGET_AARCH64) || defined(TARGET_ARM) || defined(TARGET_MIPS) || defined(TARGET_MIPS64)
void afl_save_regs(struct api_regs* regs, CPUArchState* env);
void afl_restore_regs(struct api_regs* regs, CPUArchState* env);
#else
static void afl_save_regs(struct api_regs* regs, CPUArchState* env) {}
static void afl_restore_regs(struct api_regs* regs, CPUArchState* env) {}
#endif

void afl_target_unmap_trackeds(void);

int open_self_maps(void *cpu_env, int fd);

TranslationBlock *afl_gen_edge(CPUState *cpu, unsigned long afl_id);

/* Check if an address is valid in the current mapping */

static inline int is_valid_addr(target_ulong addr) {

  int          flags;
  target_ulong page;

  page = addr & TARGET_PAGE_MASK;

  flags = page_get_flags(page);
  if (!(flags & PAGE_VALID) || !(flags & PAGE_READ)) return 0;

  return 1;

}

static inline int afl_must_instrument(target_ulong addr) {

  // Reject any exclusion regions
  for (struct vmrange* n = afl_instr_code; n; n = n->next) {
    if (n->exclude && addr < n->end && addr >= n->start)
      return 0;
  }

  // Check for inclusion in instrumentation regions
  if (addr < afl_end_code && addr >= afl_start_code)
    return 1;

  for (struct vmrange* n = afl_instr_code; n; n = n->next) {
    if (!n->exclude && addr < n->end && addr >= n->start)
      return 1;
  }

  return 0;

}

#endif

